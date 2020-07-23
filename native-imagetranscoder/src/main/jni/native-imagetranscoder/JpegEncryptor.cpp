#include <type_traits>

#include <stdint.h>

#include <jni.h>

#include "exceptions_handler.h"
#include "jpeg/crypto/jpeg_encrypt.h"
#include "logging.h"

using facebook::imagepipeline::jpeg::crypto::encryptJpeg;
using facebook::imagepipeline::jpeg::crypto::encryptJpegEtc;

static void JpegEncryptor_encryptJpeg(
    JNIEnv* env,
    jclass /* clzz */,
    jobject is,
    jobject os,
    jstring x_0_jstr,
    jstring mu_jstr) {
  RETURN_IF_EXCEPTION_PENDING;
  encryptJpeg(
      env,
      is,
      os,
      x_0_jstr,
      mu_jstr);
}

static void JpegEncryptor_encryptJpegEtc(
    JNIEnv* env,
    jclass /* clzz */,
    jobject is,
    jobject os_red,
    jobject os_green,
    jobject os_blue,
    jstring x_0_jstr,
    jstring mu_jstr,
    jint quality) {
  RETURN_IF_EXCEPTION_PENDING;
  encryptJpegEtc(
      env,
      is,
      os_red,
      os_green,
      os_blue,
      x_0_jstr,
      mu_jstr,
      quality);
}

static JNINativeMethod gJpegEncryptorMethods[] = {
  { "nativeEncryptJpeg",
      "(Ljava/io/InputStream;Ljava/io/OutputStream;Ljava/lang/String;Ljava/lang/String;)V",
      (void*) JpegEncryptor_encryptJpeg },
  { "nativeEncryptJpegEtc",
      "(Ljava/io/InputStream;Ljava/io/OutputStream;Ljava/io/OutputStream;Ljava/io/OutputStream;Ljava/lang/String;Ljava/lang/String;I)V",
      (void*) JpegEncryptor_encryptJpegEtc },
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