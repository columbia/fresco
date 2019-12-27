package com.facebook.imagepipeline.nativecode;

import com.facebook.common.internal.DoNotStrip;
import com.facebook.imageformat.DefaultImageFormats;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.encryptor.ImageEncryptor;
import com.facebook.imagepipeline.encryptor.ImageEncryptorFactory;

import javax.annotation.Nullable;

public class NativeJpegEncryptorFactory implements ImageEncryptorFactory {

  @DoNotStrip
  public NativeJpegEncryptorFactory(final int maxBitmapSize) {

  }

  @DoNotStrip
  @Override
  @Nullable
  public ImageEncryptor createImageEncryptor(ImageFormat imageFormat) {
    if (imageFormat != DefaultImageFormats.JPEG) {
      return null;
    }
    return new NativeJpegEncryptor();
  }
}
