#ifndef FRESCO_JPEG_DECRYPT_H
#define FRESCO_JPEG_DECRYPT_H

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

void decryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os);

} } } }
#endif //FRESCO_JPEG_DECRYPT_H
