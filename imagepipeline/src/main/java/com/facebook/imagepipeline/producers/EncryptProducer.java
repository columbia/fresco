package com.facebook.imagepipeline.producers;

import com.facebook.common.internal.ImmutableMap;
import com.facebook.common.internal.Preconditions;
import com.facebook.common.internal.VisibleForTesting;
import com.facebook.common.memory.PooledByteBuffer;
import com.facebook.common.memory.PooledByteBufferFactory;
import com.facebook.common.memory.PooledByteBufferOutputStream;
import com.facebook.common.references.CloseableReference;
import com.facebook.common.util.TriState;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.encryptor.EncryptStatus;
import com.facebook.imagepipeline.encryptor.ImageEncryptResult;
import com.facebook.imagepipeline.encryptor.ImageEncryptor;
import com.facebook.imagepipeline.encryptor.ImageEncryptorFactory;
import com.facebook.imagepipeline.image.EncodedImage;
import com.facebook.imagepipeline.request.ImageRequest;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.Executor;

import javax.annotation.Nullable;

import static com.facebook.imageformat.DefaultImageFormats.JPEG;
import static com.facebook.imagepipeline.transcoder.JpegTranscoderUtils.DEFAULT_JPEG_QUALITY;

public class EncryptProducer implements Producer<EncodedImage> {
  private static final String PRODUCER_NAME = "EncryptProducer";
  private static final String INPUT_IMAGE_FORMAT = "Image format";
  private static final String ORIGINAL_SIZE_KEY = "Original size";
  private static final String ENCRYPTING_RESULT = "Encrypting result";
  private static final String ENCRYPTOR_ID = "Encryptor id";

  private final Executor mExecutor;
  private final PooledByteBufferFactory mPooledByteBufferFactory;
  private final Producer<EncodedImage> mInputProducer;
  private final ImageEncryptorFactory mImageEncryptorFactory;

  @VisibleForTesting
  static final int MIN_ENCRYPT_INTERVAL_MS = 100;

  public EncryptProducer(
          final Executor executor,
          final PooledByteBufferFactory pooledByteBufferFactory,
          final Producer<EncodedImage> inputProducer,
          final ImageEncryptorFactory imageEncryptorFactory) {
    mExecutor = Preconditions.checkNotNull(executor);
    mPooledByteBufferFactory = Preconditions.checkNotNull(pooledByteBufferFactory);
    mInputProducer = Preconditions.checkNotNull(inputProducer);
    mImageEncryptorFactory = Preconditions.checkNotNull(imageEncryptorFactory);
  }

  @Override
  public void produceResults(final Consumer<EncodedImage> consumer, final ProducerContext context) {
    mInputProducer.produceResults(
            new EncryptingProducer(consumer, context, mImageEncryptorFactory),
            context);
  }

  private class EncryptingProducer extends DelegatingConsumer<EncodedImage, EncodedImage> {

    private final ImageEncryptorFactory mImageEncryptorFactory;
    private final ProducerContext mProducerContext;
    private boolean mIsCancelled;

    private final JobScheduler mJobScheduler;

    EncryptingProducer(
            final Consumer<EncodedImage> consumer,
            final ProducerContext producerContext,
            final ImageEncryptorFactory imageEncryptorFactory) {
      super(consumer);
      mIsCancelled = false;
      mProducerContext = producerContext;

      mImageEncryptorFactory = imageEncryptorFactory;

      JobScheduler.JobRunnable job =
              new JobScheduler.JobRunnable() {
                @Override
                public void run(EncodedImage encodedImage, @Status int status) {
                  final ImageEncryptor encryptor = mImageEncryptorFactory.createImageEncryptor(
                          encodedImage.getImageFormat());
                  doEncrypt(
                          encodedImage,
                          status,
                          Preconditions.checkNotNull(encryptor));
                }
              };
      mJobScheduler = new JobScheduler(mExecutor, job, MIN_ENCRYPT_INTERVAL_MS);

      mProducerContext.addCallbacks(
              new BaseProducerContextCallbacks() {
                @Override
                public void onIsIntermediateResultExpectedChanged() {
                  if (mProducerContext.isIntermediateResultExpected()) {
                    mJobScheduler.scheduleJob();
                  }
                }

                @Override
                public void onCancellationRequested() {
                  mJobScheduler.clearJob();
                  mIsCancelled = true;
                  // this only works if it is safe to discard the output of previous producer
                  consumer.onCancellation();
                }
              });
    }

    @Override
    protected void onNewResultImpl(@Nullable EncodedImage newResult, @Status int status) {
      if (mIsCancelled) {
        return;
      }
      boolean isLast = isLast(status);
      if (newResult == null) {
        if (isLast) {
          getConsumer().onNewResult(null, Consumer.IS_LAST);
        }
        return;
      }
      ImageFormat imageFormat = newResult.getImageFormat();
      TriState shouldEncrypt =
              shouldEncrypt(
                      mProducerContext.getImageRequest(),
                      newResult,
                      Preconditions.checkNotNull(
                              mImageEncryptorFactory.createImageEncryptor(imageFormat)));
      // ignore the intermediate result if we don't know what to do with it
      if (!isLast && shouldEncrypt == TriState.UNSET) {
        return;
      }
      // just forward the result if we know that it shouldn't be encrypted
      if (shouldEncrypt != TriState.YES) {
        forwardNewResult(newResult, status, imageFormat);
        return;
      }
      // we know that the result should be encrypted, hence schedule it
      if (!mJobScheduler.updateJob(newResult, status)) {
        return;
      }
      if (isLast || mProducerContext.isIntermediateResultExpected()) {
        mJobScheduler.scheduleJob();
      }
    }

    private void forwardNewResult(
            EncodedImage newResult, @Status int status, ImageFormat imageFormat) {
      newResult = EncodedImage.cloneOrNull(newResult);
      getConsumer().onNewResult(newResult, status);
    }

    private void doEncrypt(
            EncodedImage encodedImage, @Consumer.Status int status, ImageEncryptor imageEncryptor) {
      mProducerContext.getProducerListener().onProducerStart(mProducerContext, PRODUCER_NAME);
      //ImageRequest imageRequest = mProducerContext.getImageRequest();
      PooledByteBufferOutputStream outputStream = mPooledByteBufferFactory.newOutputStream();
      Map<String, String> extraMap = null;
      EncodedImage ret;

      try {
        ImageEncryptResult result =
                imageEncryptor.encrypt(
                        encodedImage,
                        outputStream,
                        null,
                        DEFAULT_JPEG_QUALITY);

        if (result.getEncryptStatus() == EncryptStatus.ENCRYPTING_ERROR) {
          throw new RuntimeException("Error while encrypting the image");
        }

        extraMap =
                getExtraMap(
                        encodedImage,
                        result,
                        imageEncryptor.getIdentifier());

        CloseableReference<PooledByteBuffer> ref =
                CloseableReference.of(outputStream.toByteBuffer());
        try {
          ret = new EncodedImage(ref);
          ret.setImageFormat(JPEG);
          try {
            ret.parseMetaData();
            mProducerContext
                    .getProducerListener()
                    .onProducerFinishWithSuccess(mProducerContext, PRODUCER_NAME, extraMap);
            getConsumer().onNewResult(ret, status);
          } finally {
            EncodedImage.closeSafely(ret);
          }
        } finally {
          CloseableReference.closeSafely(ref);
        }
      } catch (Exception e) {
        mProducerContext
                .getProducerListener()
                .onProducerFinishWithFailure(mProducerContext, PRODUCER_NAME, e, extraMap);
        if (isLast(status)) {
          getConsumer().onFailure(e);
        }
      } finally {
        outputStream.close();
      }
    }

    private @Nullable Map<String, String> getExtraMap(
            EncodedImage encodedImage,
            @Nullable ImageEncryptResult encryptResult,
            @Nullable String encryptorId) {
      if (!mProducerContext
              .getProducerListener()
              .requiresExtraMap(mProducerContext, PRODUCER_NAME)) {
        return null;
      }
      String originalSize = encodedImage.getWidth() + "x" + encodedImage.getHeight();

      final Map<String, String> map = new HashMap<>();
      map.put(INPUT_IMAGE_FORMAT, String.valueOf(encodedImage.getImageFormat()));
      map.put(ORIGINAL_SIZE_KEY, originalSize);
      map.put(JobScheduler.QUEUE_TIME_KEY, String.valueOf(mJobScheduler.getQueuedTime()));
      map.put(ENCRYPTOR_ID, encryptorId);
      map.put(ENCRYPTING_RESULT, String.valueOf(encryptResult));
      return ImmutableMap.copyOf(map);
    }
  }

  private static TriState shouldEncrypt(
          ImageRequest request, EncodedImage encodedImage, ImageEncryptor imageEncryptor) {
    if (encodedImage == null || encodedImage.getImageFormat() == ImageFormat.UNKNOWN) {
      return TriState.UNSET;
    }

    if (!request.shouldEncrypt() || !imageEncryptor.canEncrypt(encodedImage.getImageFormat())) {
      return TriState.NO;
    }

    return TriState.valueOf(request.shouldEncrypt());
  }
}
