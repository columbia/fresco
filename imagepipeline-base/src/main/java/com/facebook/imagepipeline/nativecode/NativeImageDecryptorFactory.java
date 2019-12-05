package com.facebook.imagepipeline.nativecode;

import com.facebook.imagepipeline.decryptor.ImageDecryptorFactory;

import java.lang.reflect.InvocationTargetException;

public final class NativeImageDecryptorFactory {

  private NativeImageDecryptorFactory() {}

  public static ImageDecryptorFactory getNativeImageDecryptorFactory(final int maxBitmapSize) {
    try {
      return (ImageDecryptorFactory)
              Class.forName("com.facebook.imagepipeline.nativecode.NativeJpegDecryptorFactory")
                      .getConstructor(Integer.TYPE)
                      .newInstance(maxBitmapSize);
    } catch (NoSuchMethodException
            | SecurityException
            | InstantiationException
            | InvocationTargetException
            | IllegalAccessException
            | IllegalArgumentException
            | ClassNotFoundException e) {
      throw new RuntimeException(
              "Dependency ':native-imagetranscoder' is needed to use the default native image decryptor.",
              e);
    }
  }
}
