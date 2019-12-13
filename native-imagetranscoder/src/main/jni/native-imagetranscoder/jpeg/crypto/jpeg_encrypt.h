//
// Created by mauzel on 12/12/2019.
//

#ifndef FRESCO_JPEG_CRYPTO_H
#define FRESCO_JPEG_CRYPTO_H
namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

void encryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os);

} } } }
#endif //FRESCO_JPEG_CRYPTO_H
