/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef _JPEG_CODEC_H_
#define _JPEG_CODEC_H_

#include <jni.h>

#include <string.h>

#include "jpeg_error_handler.h"
#include "decoded_image.h"
#include "transformations.h"

namespace facebook {
namespace imagepipeline {
namespace jpeg {

/**
 * Encodes given image using libjpeg and writtes encoded bytes
 * into provided output stream.
 *
 * @param env
 * @param decoded_image
 * @param os output stream to write data to
 * @param quality value passed to jpeg encoder
 */
void encodeJpegIntoOutputStream(
    JNIEnv* env,
    DecodedImage& decoded_image,
    jobject os,
    int quality);

/**
 * Downscales and rotates jpeg image
 *
 * @param env
 * @param is InputStream
 * @param os OutputStream
 * @param rotation_type
 * @param scale_factor
 * @param crop_info
 * @param quality
 */
void transformJpeg(
    JNIEnv* env,
    jobject is,
    jobject os,
    RotationType rotation_type,
    const ScaleFactor& scale_factor,
    int quality);

/**
 * Initializes decompress struct.
 *
 * <p> Sets source and error handling.
 *
 * <p> Sets decompress parameters to optimize decode time.
 */
void initDecompressStruct(
    struct jpeg_decompress_struct& dinfo,
    JpegErrorHandler& error_handler,
    struct jpeg_source_mgr& source);

/**
 * Initializes compress struct.
 *
 * <p> Sets destination and error handler.
 *
 * <p> Sets copies params from given decompress struct
 */
void initCompressStruct(
    struct jpeg_compress_struct& cinfo,
    struct jpeg_decompress_struct& dinfo,
    JpegErrorHandler& error_handler,
    struct jpeg_destination_mgr& destination);

} } }

#endif /* _JPEG_CODEC_H_ */
