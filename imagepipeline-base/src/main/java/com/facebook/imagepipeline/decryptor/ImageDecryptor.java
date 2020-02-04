package com.facebook.imagepipeline.decryptor;

import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.common.JpegCryptoKey;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.OutputStream;

import javax.annotation.Nullable;

public interface ImageDecryptor {

  /**
   * Decrypts an image.
   *
   * @param encodedImage The {@link EncodedImage} that will be decrypted.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   * @param key {@link JpegCryptoKey} representing the secret values to use for the crypto.
   * @return The {@link ImageDecryptResult} generated when encoding the image.
   * @throws IOException if I/O error happens when reading or writing the images.
   */
  ImageDecryptResult decrypt(
          EncodedImage encodedImage,
          OutputStream outputStream,
          JpegCryptoKey key)
          throws IOException;

  /**
   * Decrypts an image.
   *
   * @param encodedImageRed The red {@link EncodedImage} that will be decrypted.
   * @param encodedImageGreen The green {@link EncodedImage} that will be decrypted.
   * @param encodedImageBlue The blue {@link EncodedImage} that will be decrypted.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   * @param key {@link JpegCryptoKey} representing the secret values to use for the crypto.
   * @return The {@link ImageDecryptResult} generated when encoding the image.
   * @throws IOException if I/O error happens when reading or writing the images.
   */
  ImageDecryptResult decryptEtc(
          EncodedImage encodedImageRed,
          EncodedImage encodedImageGreen,
          EncodedImage encodedImageBlue,
          OutputStream outputStream,
          JpegCryptoKey key)
          throws IOException;

  /**
   * Whether the input {@link ImageFormat} can be encrypted by the image decryptor.
   *
   * @param imageFormat The {@link ImageFormat} that will be decrypted.
   * @return true if this image format is handled by the image decryptor, else false.
   */
  boolean canDecrypt(ImageFormat imageFormat);

  /**
   * Gets the identifier of the image decryptor. This is mostly used for logging purposes.
   *
   * @return the identifier of the image decryptor.
   */
  String getIdentifier();
}
