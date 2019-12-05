package com.facebook.imagepipeline.nativecode;

import com.facebook.common.internal.Closeables;
import com.facebook.common.internal.DoNotStrip;
import com.facebook.common.internal.Preconditions;
import com.facebook.common.internal.VisibleForTesting;
import com.facebook.imageformat.DefaultImageFormats;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.decryptor.DecryptStatus;
import com.facebook.imagepipeline.decryptor.ImageDecryptResult;
import com.facebook.imagepipeline.decryptor.ImageDecryptor;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import javax.annotation.Nullable;

/** Decryptor for jpeg images, using native code and libjpeg-turbo library. */
@DoNotStrip
public class NativeJpegDecryptor implements ImageDecryptor {

  public static final String TAG = "NativeJpegDecryptor";

  private int mMaxBitmapSize;

  static {
    NativeJpegTranscoderSoLoader.ensure();
  }

  public NativeJpegDecryptor(final int maxBitmapSize) {
    mMaxBitmapSize = maxBitmapSize;
  }

  @Override
  public ImageDecryptResult decrypt(
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
      decryptJpeg(is, outputStream);
    } finally {
      Closeables.closeQuietly(is);
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
   * @param inputStream The {@link InputStream} of the image that will be decrypted.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   */
  @VisibleForTesting
  public static void decryptJpeg(
          final InputStream inputStream,
          final OutputStream outputStream)
          throws IOException {
    NativeJpegTranscoderSoLoader.ensure();
    nativeDecryptJpeg(
            Preconditions.checkNotNull(inputStream),
            Preconditions.checkNotNull(outputStream));
  }

  @DoNotStrip
  private static native void nativeDecryptJpeg(
          InputStream inputStream,
          OutputStream outputStream)
          throws IOException;
}
