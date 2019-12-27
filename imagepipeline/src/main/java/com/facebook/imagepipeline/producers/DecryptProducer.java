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
import com.facebook.imagepipeline.common.JpegCryptoKey;
import com.facebook.imagepipeline.decryptor.DecryptStatus;
import com.facebook.imagepipeline.decryptor.ImageDecryptResult;
import com.facebook.imagepipeline.decryptor.ImageDecryptor;
import com.facebook.imagepipeline.decryptor.ImageDecryptorFactory;
import com.facebook.imagepipeline.image.EncodedImage;
import com.facebook.imagepipeline.request.ImageRequest;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.Executor;

import javax.annotation.Nullable;

import static com.facebook.imageformat.DefaultImageFormats.JPEG;

public class DecryptProducer implements Producer<EncodedImage> {
  private static final String PRODUCER_NAME = "DecryptProducer";
  private static final String INPUT_IMAGE_FORMAT = "Image format";
  private static final String ORIGINAL_SIZE_KEY = "Original size";
  private static final String DECRYPTING_RESULT = "Decrypting result";
  private static final String DECRYPTOR_ID = "Decryptor id";

  private final Executor mExecutor;
  private final PooledByteBufferFactory mPooledByteBufferFactory;
  private final Producer<EncodedImage> mInputProducer;
  private final ImageDecryptorFactory mImageDecryptorFactory;

  @VisibleForTesting
  static final int MIN_DECRYPT_INTERVAL_MS = 100;

  public DecryptProducer(
          final Executor executor,
          final PooledByteBufferFactory pooledByteBufferFactory,
          final Producer<EncodedImage> inputProducer,
          final ImageDecryptorFactory imageDecryptorFactory) {
    mExecutor = Preconditions.checkNotNull(executor);
    mPooledByteBufferFactory = Preconditions.checkNotNull(pooledByteBufferFactory);
    mInputProducer = Preconditions.checkNotNull(inputProducer);
    mImageDecryptorFactory = Preconditions.checkNotNull(imageDecryptorFactory);
  }

  @Override
  public void produceResults(final Consumer<EncodedImage> consumer, final ProducerContext context) {
    mInputProducer.produceResults(
            new DecryptingProducer(consumer, context, mImageDecryptorFactory),
            context);
  }

  private class DecryptingProducer extends DelegatingConsumer<EncodedImage, EncodedImage> {

    private final ImageDecryptorFactory mImageDecryptorFactory;
    private final ProducerContext mProducerContext;
    private boolean mIsCancelled;

    private final JobScheduler mJobScheduler;

    DecryptingProducer(
            final Consumer<EncodedImage> consumer,
            final ProducerContext producerContext,
            final ImageDecryptorFactory imageDecryptorFactory) {
      super(consumer);
      mIsCancelled = false;
      mProducerContext = producerContext;

      mImageDecryptorFactory = imageDecryptorFactory;

      JobScheduler.JobRunnable job =
              new JobScheduler.JobRunnable() {
                @Override
                public void run(EncodedImage encodedImage, @Status int status) {
                  final ImageDecryptor decryptor = mImageDecryptorFactory.createImageDecryptor(
                          encodedImage.getImageFormat());
                  doDecrypt(
                          encodedImage,
                          status,
                          Preconditions.checkNotNull(decryptor),
                          mProducerContext.getImageRequest().getJpegCryptoKey());
                }
              };
      mJobScheduler = new JobScheduler(mExecutor, job, MIN_DECRYPT_INTERVAL_MS);

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
      TriState shouldDecrypt =
              shouldDecrypt(
                      mProducerContext.getImageRequest(),
                      newResult,
                      Preconditions.checkNotNull(
                              mImageDecryptorFactory.createImageDecryptor(imageFormat)));
      // ignore the intermediate result if we don't know what to do with it
      if (!isLast && shouldDecrypt == TriState.UNSET) {
        return;
      }
      // just forward the result if we know that it shouldn't be encrypted
      if (shouldDecrypt != TriState.YES) {
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

    private void doDecrypt(
            EncodedImage encodedImage,
            @Status int status,
            ImageDecryptor imageDecryptor,
            JpegCryptoKey key) {
      mProducerContext.getProducerListener().onProducerStart(mProducerContext, PRODUCER_NAME);
      //ImageRequest imageRequest = mProducerContext.getImageRequest();
      PooledByteBufferOutputStream outputStream = mPooledByteBufferFactory.newOutputStream();
      Map<String, String> extraMap = null;
      EncodedImage ret;

      try {
        ImageDecryptResult result =
                imageDecryptor.decrypt(
                        encodedImage,
                        outputStream,
                        key);

        if (result.getDecryptStatus() == DecryptStatus.DECRYPTING_ERROR) {
          throw new RuntimeException("Error while encrypting the image");
        }

        extraMap =
                getExtraMap(
                        encodedImage,
                        result,
                        imageDecryptor.getIdentifier());

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

    private @Nullable
    Map<String, String> getExtraMap(
            EncodedImage encodedImage,
            @Nullable ImageDecryptResult decryptResult,
            @Nullable String decryptorId) {
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
      map.put(DECRYPTOR_ID, decryptorId);
      map.put(DECRYPTING_RESULT, String.valueOf(decryptResult));
      return ImmutableMap.copyOf(map);
    }
  }

  private static TriState shouldDecrypt(
          ImageRequest request, EncodedImage encodedImage, ImageDecryptor imageDecryptor) {
    if (encodedImage == null || encodedImage.getImageFormat() == ImageFormat.UNKNOWN) {
      return TriState.UNSET;
    }

    if (!request.shouldDecrypt() || !imageDecryptor.canDecrypt(encodedImage.getImageFormat()) || request.getJpegCryptoKey() == null) {
      return TriState.NO;
    }

    return TriState.valueOf(request.shouldDecrypt());
  }
}
