package com.facebook.imagepipeline.encryptor;

import androidx.annotation.IntDef;

import java.lang.annotation.Retention;

import static java.lang.annotation.RetentionPolicy.SOURCE;

/**
 * Status used by {@link ImageEncryptResult} to supply additional information.
 */
@Retention(SOURCE)
@IntDef({
  EncryptStatus.ENCRYPTING_SUCCESS,
  EncryptStatus.ENCRYPTING_ERROR
})
public @interface EncryptStatus {
  /**
   * Status flag to show that the image was encrypted successfully.
   */
  int ENCRYPTING_SUCCESS = 0;

  /**
   * Status flag to show that an error occurred while encrypting the image.
   */
  int ENCRYPTING_ERROR = 1;
}
