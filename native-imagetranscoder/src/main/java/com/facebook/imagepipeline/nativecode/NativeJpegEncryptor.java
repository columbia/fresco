package com.facebook.imagepipeline.nativecode;

import com.facebook.common.internal.Closeables;
import com.facebook.common.internal.DoNotStrip;
import com.facebook.common.internal.Preconditions;
import com.facebook.common.internal.VisibleForTesting;
import com.facebook.imageformat.DefaultImageFormats;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.encryptor.EncryptStatus;
import com.facebook.imagepipeline.encryptor.ImageEncryptResult;
import com.facebook.imagepipeline.encryptor.ImageEncryptor;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import javax.annotation.Nullable;

/** Encryptor for jpeg images, using native code and libjpeg-turbo library. */
@DoNotStrip
public class NativeJpegEncryptor implements ImageEncryptor {

  public static final String TAG = "NativeJpegEncryptor";

  private int mMaxBitmapSize;

  static {
    NativeJpegTranscoderSoLoader.ensure();
  }

  public NativeJpegEncryptor(final int maxBitmapSize) {
    mMaxBitmapSize = maxBitmapSize;
  }

  @Override
  public ImageEncryptResult encrypt(
          final EncodedImage encodedImage,
          final OutputStream outputStream,
          @Nullable ImageFormat outputFormat,
          @Nullable Integer quality)
          throws IOException {
    if (quality == null) {
      //quality = DEFAULT_JPEG_QUALITY;
    }

    InputStream is = null;
    try {
      is = encodedImage.getInputStream();
      encryptJpeg(is, outputStream);
    } finally {
      Closeables.closeQuietly(is);
    }
    return new ImageEncryptResult(EncryptStatus.ENCRYPTING_SUCCESS);
  }

  @Override
  public boolean canEncrypt(ImageFormat imageFormat) {
    return imageFormat == DefaultImageFormats.JPEG;
  }

  @Override
  public String getIdentifier() {
    return TAG;
  }

  /**
   * Encrypts a JPEG.
   *
   * @param inputStream The {@link InputStream} of the image that will be encrypted.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   */
  @VisibleForTesting
  public static void encryptJpeg(
          final InputStream inputStream,
          final OutputStream outputStream)
          throws IOException {
    NativeJpegTranscoderSoLoader.ensure();
    nativeEncryptJpeg(
            Preconditions.checkNotNull(inputStream),
            Preconditions.checkNotNull(outputStream));
  }

  @DoNotStrip
  private static native void nativeEncryptJpeg(
          InputStream inputStream,
          OutputStream outputStream)
          throws IOException;
}
