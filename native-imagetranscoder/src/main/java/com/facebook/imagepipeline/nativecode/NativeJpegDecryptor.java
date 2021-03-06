package com.facebook.imagepipeline.nativecode;

import com.facebook.common.internal.Closeables;
import com.facebook.common.internal.DoNotStrip;
import com.facebook.common.internal.Preconditions;
import com.facebook.common.internal.VisibleForTesting;
import com.facebook.imageformat.DefaultImageFormats;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.common.JpegCryptoKey;
import com.facebook.imagepipeline.decryptor.DecryptStatus;
import com.facebook.imagepipeline.decryptor.ImageDecryptResult;
import com.facebook.imagepipeline.decryptor.ImageDecryptor;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Decryptor for jpeg images, using native code and libjpeg-turbo library.
 */
@DoNotStrip
public class NativeJpegDecryptor implements ImageDecryptor {

  public static final String TAG = "NativeJpegDecryptor";

  static {
    NativeJpegTranscoderSoLoader.ensure();
  }

  @Override
  public ImageDecryptResult decrypt(
          final EncodedImage encodedImage,
          final OutputStream outputStream,
          final JpegCryptoKey key)
          throws IOException {

    InputStream is = null;
    try {
      is = encodedImage.getInputStream();
      decryptJpeg(is, outputStream, key);
    } finally {
      Closeables.closeQuietly(is);
    }
    return new ImageDecryptResult(DecryptStatus.DECRYPTING_SUCCESS);
  }

  @Override
  public ImageDecryptResult decryptEtc(
          EncodedImage encodedImageRed,
          EncodedImage encodedImageGreen,
          EncodedImage encodedImageBlue,
          OutputStream outputStream,
          JpegCryptoKey key)
          throws IOException {
    InputStream isRed = null;
    InputStream isGreen = null;
    InputStream isBlue = null;
    try {
      isRed = encodedImageRed.getInputStream();
      isGreen = encodedImageGreen.getInputStream();
      isBlue = encodedImageBlue.getInputStream();
      decryptJpegEtc(isRed, isGreen, isBlue, outputStream, key);
    } finally {
      Closeables.closeQuietly(isRed);
      Closeables.closeQuietly(isGreen);
      Closeables.closeQuietly(isBlue);
    }
    return new ImageDecryptResult(DecryptStatus.DECRYPTING_SUCCESS);
  }

  @Override
  public boolean canDecrypt(ImageFormat imageFormat) {
    return imageFormat == DefaultImageFormats.JPEG;
  }

  @Override
  public String getIdentifier() {
    return TAG;
  }

  /**
   * Decrypts a JPEG.
   *
   * @param inputStream  The {@link InputStream} of the image that will be decrypted.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   */
  @VisibleForTesting
  public static void decryptJpeg(
          final InputStream inputStream,
          final OutputStream outputStream,
          final JpegCryptoKey key)
          throws IOException {
    NativeJpegTranscoderSoLoader.ensure();
    nativeDecryptJpeg(
            Preconditions.checkNotNull(inputStream),
            Preconditions.checkNotNull(outputStream),
            key.getX0(),
            key.getMu());
  }

  @VisibleForTesting
  public static void decryptJpegEtc(
          final InputStream inputStreamRed,
          final InputStream inputStreamGreen,
          final InputStream inputStreamBlue,
          final OutputStream outputStream,
          final JpegCryptoKey key)
          throws IOException {
    NativeJpegTranscoderSoLoader.ensure();
    nativeDecryptJpegEtc(
            Preconditions.checkNotNull(inputStreamRed),
            Preconditions.checkNotNull(inputStreamGreen),
            Preconditions.checkNotNull(inputStreamBlue),
            Preconditions.checkNotNull(outputStream),
            key.getX0(),
            key.getMu());
  }

  @DoNotStrip
  private static native void nativeDecryptJpeg(
          InputStream inputStream,
          OutputStream outputStream,
          String x0,
          String mu)
          throws IOException;

  @DoNotStrip
  private static native void nativeDecryptJpegEtc(
          InputStream inputStreamRed,
          InputStream inputStreamGreen,
          InputStream inputStreamBlue,
          OutputStream outputStream,
          String x0,
          String mu)
          throws IOException;
}
