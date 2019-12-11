package com.facebook.fresco.samples.showcase.drawee;

import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.Nullable;

import com.facebook.common.internal.ByteStreams;
import com.facebook.common.internal.Closeables;
import com.facebook.common.internal.Preconditions;
import com.facebook.common.logging.FLog;
import com.facebook.common.memory.PooledByteBuffer;
import com.facebook.common.memory.PooledByteBufferInputStream;
import com.facebook.common.references.CloseableReference;
import com.facebook.datasource.BaseDataSubscriber;
import com.facebook.datasource.DataSource;
import com.facebook.datasource.DataSubscriber;
import com.facebook.drawee.backends.pipeline.Fresco;
import com.facebook.drawee.view.SimpleDraweeView;
import com.facebook.fresco.samples.showcase.BaseShowcaseFragment;
import com.facebook.fresco.samples.showcase.R;
import com.facebook.fresco.samples.showcase.misc.ImageUriProvider;
import com.facebook.imagepipeline.common.ImageDecodeOptionsBuilder;
import com.facebook.imagepipeline.core.DefaultExecutorSupplier;
import com.facebook.imagepipeline.core.ImagePipeline;
import com.facebook.imagepipeline.encryptor.ImageEncryptor;
import com.facebook.imagepipeline.encryptor.ImageEncryptorFactory;
import com.facebook.imagepipeline.image.EncodedImage;
import com.facebook.imagepipeline.nativecode.NativeImageEncryptorFactory;
import com.facebook.imagepipeline.request.ImageRequest;
import com.facebook.imagepipeline.request.ImageRequestBuilder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.Executor;

public class DraweeEncryptFragment extends BaseShowcaseFragment {

  private final String TAG = "DraweeEncryptFragment";

  private SimpleDraweeView mDraweeEncryptView;
  private SimpleDraweeView mDraweeDecryptView;
  private Uri mUri;

  private Uri lastEncryptedImage = null;

  private ImagePipeline pipeline;

  private MediaScannerConnection.MediaScannerConnectionClient msClient = new MediaScannerConnection.MediaScannerConnectionClient() {
    @Override
    public void onMediaScannerConnected() {
      FLog.d(TAG, "MediaScanner connected");
    }

    @Override
    public void onScanCompleted(String path, Uri uri) {
      FLog.d(TAG, "Done scanning %s", path);
    }
  };

  @Nullable
  @Override
  public View onCreateView(
          LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
    return inflater.inflate(R.layout.fragment_drawee_encrypt, container, false);
  }

  @Override
  public void onViewCreated(View view, @Nullable Bundle savedInstanceState) {
    pipeline = Fresco.getImagePipeline();

    mUri = sampleUris().createSampleUri(ImageUriProvider.ImageSize.XL);
    mDraweeEncryptView = view.findViewById(R.id.drawee_view);
    mDraweeDecryptView = view.findViewById(R.id.drawee_decrypt);

    setEncryptOptions();

    view.findViewById(R.id.btn_random_uri)
            .setOnClickListener(
                    new View.OnClickListener() {
                      @Override
                      public void onClick(View v) {
                        mUri = sampleUris().createSampleUri();
                        setEncryptOptions();
                      }
                    });

    view.findViewById(R.id.btn_decrypt_image)
            .setOnClickListener(
                    new View.OnClickListener() {
                      @Override
                      public void onClick(View v) {
                        setDecryptOptions();
                      }
                    });
  }

  private void setEncryptOptions() {
    pipeline.clearCaches();
    ImageRequest imageRequest =
            ImageRequestBuilder.newBuilderWithSource(mUri)
                    .setEncrypt(true)
                    .setImageDecodeOptions(new ImageDecodeOptionsBuilder().build())
                    .build();

    DataSource<CloseableReference<PooledByteBuffer>> dataSource = pipeline
            .fetchEncodedImage(imageRequest, this);

    final Executor executor = new DefaultExecutorSupplier(1).forBackgroundTasks();

    final File encryptedImageDir = Preconditions.checkNotNull(this.getContext()).getExternalFilesDir(null);

    DataSubscriber<CloseableReference<PooledByteBuffer>> dataSubscriber =
            new BaseDataSubscriber<CloseableReference<PooledByteBuffer>>() {
              @Override
              protected void onNewResultImpl(
                      DataSource<CloseableReference<PooledByteBuffer>> dataSource) {
                if (!dataSource.isFinished()) {
                  // if we are not interested in the intermediate images,
                  // we can just return here.
                  return;
                }

                CloseableReference<PooledByteBuffer> ref = dataSource.getResult();
                if (ref != null) {
                  File tempFile = null;
                  try {
                    final EncodedImage encodedImage = new EncodedImage(ref);

                    final ImageEncryptorFactory factory = NativeImageEncryptorFactory.getNativeImageEncryptorFactory(pipeline.getConfig().getExperiments().getMaxBitmapSize());
                    final ImageEncryptor encryptor = factory.createImageEncryptor(encodedImage.getImageFormat());
                    final InputStream is = encodedImage.getInputStream();

                    try {
                      tempFile = File.createTempFile(mUri.getLastPathSegment(), ".jpg", encryptedImageDir);
                      FileOutputStream fileOutputStream = new FileOutputStream(tempFile);

                      encryptor.encrypt(encodedImage, fileOutputStream, null, null);

                      MediaScannerConnection.scanFile(getContext(),
                              new String[]{tempFile.getAbsolutePath()},
                              null,
                              msClient);

                      FLog.d(TAG, "Wrote %s as encrypted JPEG to location %s (size: %s bytes)", mUri, tempFile.getAbsolutePath(), tempFile.length() / 8);

                      Closeables.close(fileOutputStream, true);
                    } catch (IOException e) {
                      FLog.e(TAG, "IOException while trying to write encrypted JPEG to disk", e);
                    } finally {
                      Closeables.closeQuietly(is);
                    }
                  } finally {
                    CloseableReference.closeSafely(ref);
                  }

                  if (tempFile != null) {
                    lastEncryptedImage = Uri.parse(tempFile.toURI().toString());
                    ImageRequest encryptedImageRequest =
                            ImageRequestBuilder.newBuilderWithSource(lastEncryptedImage)
                                    .setImageDecodeOptions(new ImageDecodeOptionsBuilder().build())
                                    .build();
                    mDraweeEncryptView.setImageRequest(encryptedImageRequest);
                  }
                }
              }

              @Override
              protected void onFailureImpl(DataSource<CloseableReference<PooledByteBuffer>> dataSource) {
                Throwable t = dataSource.getFailureCause();
                FLog.e(TAG, "Failed to load and encrypt JPEG", t);
              }
            };

    dataSource.subscribe(dataSubscriber, executor);
  }

  private void setDecryptOptions() {
    if (lastEncryptedImage != null) {
      pipeline.clearCaches();
      ImageRequest imageRequest =
              ImageRequestBuilder.newBuilderWithSource(lastEncryptedImage)
                      .setDecrypt(true)
                      .setImageDecodeOptions(new ImageDecodeOptionsBuilder().build())
                      .build();
      mDraweeDecryptView.setImageRequest(imageRequest);
    }
  }

  @Override
  public int getTitleId() {
    return R.string.drawee_encrypt_title;
  }
}
