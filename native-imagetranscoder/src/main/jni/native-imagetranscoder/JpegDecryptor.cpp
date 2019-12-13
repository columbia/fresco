#include <type_traits>

#include <stdint.h>

#include <jni.h>

#include "exceptions_handler.h"
#include "jpeg/crypto/jpeg_decrypt.h"
#include "logging.h"

using facebook::imagepipeline::jpeg::crypto::decryptJpeg;

static void JpegDecryptor_decryptJpeg(
    JNIEnv* env,
    jclass /* clzz */,
    jobject is,
    jobject os) {
  RETURN_IF_EXCEPTION_PENDING;
  decryptJpeg(
      env,
      is,
      os);
}

static JNINativeMethod gJpegDecryptorMethods[] = {
  { "nativeDecryptJpeg",
      "(Ljava/io/InputStream;Ljava/io/OutputStream;)V",
      (void*) JpegDecryptor_decryptJpeg },
};

bool registerJpegDecryptorMethods(JNIEnv* env) {
  auto nativeJpegDecryptorClass = env->FindClass(
      "com/facebook/imagepipeline/nativecode/NativeJpegDecryptor");
  if (nativeJpegDecryptorClass == nullptr) {
    LOGE("could not find NativeJpegDecryptor class");
    return false;
  }

  auto result = env->RegisterNatives(
      nativeJpegDecryptorClass,
      gJpegDecryptorMethods,
      std::extent<decltype(gJpegDecryptorMethods)>::value);

  if (result != 0) {
    LOGE("could not register JpegDecryptor methods");
    return false;
  }

  return true;
}