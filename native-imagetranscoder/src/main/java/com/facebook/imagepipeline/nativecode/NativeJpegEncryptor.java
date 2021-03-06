package com.facebook.imagepipeline.nativecode;

import com.facebook.common.internal.Closeables;
import com.facebook.common.internal.DoNotStrip;
import com.facebook.common.internal.Preconditions;
import com.facebook.common.internal.VisibleForTesting;
import com.facebook.imageformat.DefaultImageFormats;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.common.JpegCryptoKey;
import com.facebook.imagepipeline.encryptor.EncryptStatus;
import com.facebook.imagepipeline.encryptor.ImageEncryptResult;
import com.facebook.imagepipeline.encryptor.ImageEncryptor;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** Encryptor for jpeg images, using native code and libjpeg-turbo library. */
@DoNotStrip
public class NativeJpegEncryptor implements ImageEncryptor {

  public static final String TAG = "NativeJpegEncryptor";

  static {
    NativeJpegTranscoderSoLoader.ensure();
  }

  public NativeJpegEncryptor() {

  }

  @Override
  public ImageEncryptResult encrypt(
          final EncodedImage encodedImage,
          final OutputStream outputStream,
          final JpegCryptoKey key)
          throws IOException {
    InputStream is = null;
    try {
      is = encodedImage.getInputStream();
      encryptJpeg(is, outputStream, key);
    } finally {
      Closeables.closeQuietly(is);
    }
    return new ImageEncryptResult(EncryptStatus.ENCRYPTING_SUCCESS);
  }

  @Override
  public ImageEncryptResult encryptEtc(
          EncodedImage encodedImage,
          OutputStream outputStreamRed,
          OutputStream outputStreamGreen,
          OutputStream outputStreamBlue,
          JpegCryptoKey key,
          int quality)
          throws IOException {
    InputStream is = null;
    try {
      is = encodedImage.getInputStream();
      encryptJpegEtc(is, outputStreamRed, outputStreamGreen, outputStreamBlue, key, quality);
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
          final OutputStream outputStream,
          final JpegCryptoKey key)
          throws IOException {
    NativeJpegTranscoderSoLoader.ensure();
    nativeEncryptJpeg(
            Preconditions.checkNotNull(inputStream),
            Preconditions.checkNotNull(outputStream),
            key.getX0(),
            key.getMu());
  }

  @VisibleForTesting
  public static void encryptJpegEtc(
          final InputStream inputStream,
          final OutputStream outputStreamRed,
          final OutputStream outputStreamGreen,
          final OutputStream outputStreamBlue,
          final JpegCryptoKey key,
          final int quality)
          throws IOException {
    NativeJpegTranscoderSoLoader.ensure();
    nativeEncryptJpegEtc(
            Preconditions.checkNotNull(inputStream),
            Preconditions.checkNotNull(outputStreamRed),
            Preconditions.checkNotNull(outputStreamGreen),
            Preconditions.checkNotNull(outputStreamBlue),
            key.getX0(),
            key.getMu(),
            quality);
  }

  @DoNotStrip
  private static native void nativeEncryptJpeg(
          InputStream inputStream,
          OutputStream outputStream,
          String x0,
          String mu)
          throws IOException;

  @DoNotStrip
  private static native void nativeEncryptJpegEtc(
          InputStream inputStream,
          OutputStream outputStreamRed,
          OutputStream outputStreamGreen,
          OutputStream outputStreamBlue,
          String x0,
          String mu,
          int quality)
          throws IOException;
}
