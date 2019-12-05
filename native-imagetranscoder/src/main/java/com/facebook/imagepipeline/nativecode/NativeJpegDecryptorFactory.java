package com.facebook.imagepipeline.nativecode;

import com.facebook.common.internal.DoNotStrip;
import com.facebook.imageformat.DefaultImageFormats;
import com.facebook.imageformat.ImageFormat;
import com.facebook.imagepipeline.decryptor.ImageDecryptor;
import com.facebook.imagepipeline.decryptor.ImageDecryptorFactory;

import javax.annotation.Nullable;

public class NativeJpegDecryptorFactory implements ImageDecryptorFactory {

  private final int mMaxBitmapSize;

  @DoNotStrip
  public NativeJpegDecryptorFactory(final int maxBitmapSize) {
    mMaxBitmapSize = maxBitmapSize;
  }

  @DoNotStrip
  @Override
  @Nullable
  public ImageDecryptor createImageDecryptor(ImageFormat imageFormat) {
    if (imageFormat != DefaultImageFormats.JPEG) {
      return null;
    }
    return new NativeJpegDecryptor(mMaxBitmapSize);
  }
}
