package com.facebook.imagepipeline.encryptor;

import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.common.JpegCryptoKey;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.OutputStream;

public interface ImageEncryptor {

  /**
   * Encrypts an image.
   *
   * @param encodedImage The {@link EncodedImage} that will be encrypted.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   * @param key The {@link JpegCryptoKey} to use.
   * @return The {@link ImageEncryptResult} generated when encoding the image.
   * @throws IOException if I/O error happens when reading or writing the images.
   */
  ImageEncryptResult encrypt(
          EncodedImage encodedImage,
          OutputStream outputStream,
          JpegCryptoKey key)
          throws IOException;

  /**
   * Encrypts then compresses an image.
   *
   * @param encodedImage The {@link EncodedImage} that will be encrypted.
   * @param outputStreamRed The {@link OutputStream} where the newly created image is written to.
   * @param outputStreamGreen The {@link OutputStream} where the newly created image is written to.
   * @param outputStreamBlue The {@link OutputStream} where the newly created image is written to.
   * @param key The {@link JpegCryptoKey} to use.
   * @return The {@link ImageEncryptResult} generated when encoding the image.
   * @throws IOException if I/O error happens when reading or writing the images.
   */
  ImageEncryptResult encryptEtc(
          EncodedImage encodedImage,
          OutputStream outputStreamRed,
          OutputStream outputStreamGreen,
          OutputStream outputStreamBlue,
          JpegCryptoKey key,
          int quality)
          throws IOException;

  /**
   * Whether the input {@link ImageFormat} can be encrypted by the image encryptor.
   *
   * @param imageFormat The {@link ImageFormat} that will be encrypted.
   * @return true if this image format is handled by the image encryptor, else false.
   */
  boolean canEncrypt(ImageFormat imageFormat);

  /**
   * Gets the identifier of the image encryptor. This is mostly used for logging purposes.
   *
   * @return the identifier of the image encryptor.
   */
  String getIdentifier();
}
