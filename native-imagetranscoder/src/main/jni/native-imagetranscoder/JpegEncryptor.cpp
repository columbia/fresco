#include <type_traits>

#include <stdint.h>

#include <jni.h>

#include "exceptions_handler.h"
#include "jpeg/crypto/jpeg_encrypt.h"
#include "logging.h"

using facebook::imagepipeline::jpeg::crypto::encryptJpeg;

static void JpegEncryptor_encryptJpeg(
    JNIEnv* env,
    jclass /* clzz */,
    jobject is,
    jobject os) {
  RETURN_IF_EXCEPTION_PENDING;
  encryptJpeg(
      env,
      is,
      os);
}

static JNINativeMethod gJpegEncryptorMethods[] = {
  { "nativeEncryptJpeg",
      "(Ljava/io/InputStream;Ljava/io/OutputStream;)V",
      (void*) JpegEncryptor_encryptJpeg },
};

bool registerJpegEncryptorMethods(JNIEnv* env) {
  auto nativeJpegEncryptorClass = env->FindClass(
      "com/facebook/imagepipeline/nativecode/NativeJpegEncryptor");
  if (nativeJpegEncryptorClass == nullptr) {
    LOGE("could not find NativeJpegEncryptor class");
    return false;
  }

  auto result = env->RegisterNatives(
      nativeJpegEncryptorClass,
      gJpegEncryptorMethods,
      std::extent<decltype(gJpegEncryptorMethods)>::value);

  if (result != 0) {
    LOGE("could not register JpegEncryptor methods");
    return false;
  }

  return true;
}