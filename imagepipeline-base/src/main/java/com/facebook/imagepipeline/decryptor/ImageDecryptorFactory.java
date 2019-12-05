package com.facebook.imagepipeline.decryptor;

import com.facebook.imageformat.ImageFormat;

public interface ImageDecryptorFactory {
  /**
   * Creates an {@link ImageDecryptor}.
   * It can return null if the {@link ImageFormat} is not supported by this
   * {@link ImageDecryptor}.
   *
   * @param imageFormat the {@link ImageFormat} of the input images.
   * @return {@link ImageDecryptor} or null if the image format is not supported.
   */
  ImageDecryptor createImageDecryptor(ImageFormat imageFormat);
}
