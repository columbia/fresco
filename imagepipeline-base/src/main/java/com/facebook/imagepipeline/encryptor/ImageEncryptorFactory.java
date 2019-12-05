package com.facebook.imagepipeline.encryptor;

import com.facebook.imageformat.ImageFormat;

public interface ImageEncryptorFactory {
  /**
   * Creates an {@link ImageEncryptor}.
   * It can return null if the {@link ImageFormat} is not supported by this
   * {@link ImageEncryptor}.
   *
   * @param imageFormat the {@link ImageFormat} of the input images.
   * @return {@link ImageEncryptor} or null if the image format is not supported.
   */
  ImageEncryptor createImageEncryptor(ImageFormat imageFormat);
}
