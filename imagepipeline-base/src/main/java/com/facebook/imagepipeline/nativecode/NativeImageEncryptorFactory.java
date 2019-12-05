package com.facebook.imagepipeline.nativecode;

import com.facebook.imagepipeline.encryptor.ImageEncryptorFactory;

import java.lang.reflect.InvocationTargetException;

public final class NativeImageEncryptorFactory {

  private NativeImageEncryptorFactory() {}

  public static ImageEncryptorFactory getNativeImageEncryptorFactory(final int maxBitmapSize) {
    try {
      return (ImageEncryptorFactory)
              Class.forName("com.facebook.imagepipeline.nativecode.NativeJpegEncryptorFactory")
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
              "Dependency ':native-imagetranscoder' is needed to use the default native image encryptor.",
              e);
    }
  }
}
