package com.facebook.imagepipeline.decryptor;

import androidx.annotation.IntDef;

import java.lang.annotation.Retention;

import static java.lang.annotation.RetentionPolicy.SOURCE;

/**
 * Status used by {@link ImageDecryptResult} to supply additional information.
 */
@Retention(SOURCE)
@IntDef({
  DecryptStatus.DECRYPTING_SUCCESS,
  DecryptStatus.DECRYPTING_ERROR
})
public @interface DecryptStatus {
  /**
   * Status flag to show that the image was decrypted successfully.
   */
  int DECRYPTING_SUCCESS = 0;

  /**
   * Status flag to show that an error occurred while decrypting the image.
   */
  int DECRYPTING_ERROR = 1;
}
