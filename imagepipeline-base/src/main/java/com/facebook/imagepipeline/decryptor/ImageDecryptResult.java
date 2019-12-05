package com.facebook.imagepipeline.decryptor;

import java.util.Locale;

public class ImageDecryptResult {

  private @DecryptStatus
  final int mDecryptStatus;

  public ImageDecryptResult(@DecryptStatus int decryptStatus) {
    mDecryptStatus = decryptStatus;
  }

  @DecryptStatus
  public int getDecryptStatus() {
    return mDecryptStatus;
  }

  @Override
  public String toString() {
    return String.format((Locale) null, "Status: %d", mDecryptStatus);
  }
}
