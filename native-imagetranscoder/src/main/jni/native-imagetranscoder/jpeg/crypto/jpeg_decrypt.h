#ifndef FRESCO_JPEG_DECRYPT_H
#define FRESCO_JPEG_DECRYPT_H

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

void decryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os,
    jstring x_0_jstr,
    jstring mu_jstr);

} } } }
#endif //FRESCO_JPEG_DECRYPT_H
