package com.facebook.imagepipeline.encryptor;

import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.image.EncodedImage;

import java.io.IOException;
import java.io.OutputStream;

import javax.annotation.Nullable;

public interface ImageEncryptor {

  /**
   * Encrypts an image.
   *
   * @param encodedImage The {@link EncodedImage} that will be transcoded.
   * @param outputStream The {@link OutputStream} where the newly created image is written to.
   * @param outputFormat The desired {@link ImageFormat} of the newly created image. If this is null
   *     the same format as the input image will be used.
   * @param quality The desired quality of the newly created image. If this is null, the default
   *     quality of the encryptor will be applied.
   * @return The {@link ImageEncryptResult} generated when encoding the image.
   * @throws IOException if I/O error happens when reading or writing the images.
   */
  ImageEncryptResult encrypt(
          EncodedImage encodedImage,
          OutputStream outputStream,
          @Nullable ImageFormat outputFormat,
          @Nullable Integer quality)
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
