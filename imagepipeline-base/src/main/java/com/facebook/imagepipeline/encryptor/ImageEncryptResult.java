package com.facebook.imagepipeline.encryptor;

import java.util.Locale;

public class ImageEncryptResult {

  private @EncryptStatus final int mEncryptStatus;

  public ImageEncryptResult(@EncryptStatus int encryptStatus) {
    mEncryptStatus = encryptStatus;
  }

  @EncryptStatus
  public int getEncryptStatus() {
    return mEncryptStatus;
  }

  @Override
  public String toString() {
    return String.format((Locale) null, "Status: %d", mEncryptStatus);
  }
}
