/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <algorithm>
#include <iterator>

#include <stdio.h>
#include <setjmp.h>

#include <jni.h>
#include <jpeglib.h>
extern "C" {
  #include "transupp.h"
}

#include "decoded_image.h"
#include "exceptions_handler.h"
#include "logging.h"
#include "jpeg_error_handler.h"
#include "jpeg_memory_io.h"
#include "jpeg_stream_wrappers.h"
#include "transformations.h"
#include "jpeg_codec.h"

namespace facebook {
namespace imagepipeline {
namespace jpeg {

/**
 * The xmp segment header needs a trailing 0 character, so we need 29
 * characters instead of 28
 */
static const unsigned int JPEG_XMP_SEGMENT_HEADER_LENGTH = 29;
static const char* const JPEG_XMP_SEGMENT_HEADER =
    "http://ns.adobe.com/xap/1.0/";

/**
 * The upper bound for xmp metadata length stored in jpeg file
 */
static const unsigned int JPEG_METADATA_LIMIT =
  0xFFFF - 2 - JPEG_XMP_SEGMENT_HEADER_LENGTH;

/**
 * Uses jpeg library api to write APP01 segment consisting
 * of JPEG_XMP_SEGMENT_HEADER and metadata associated with
 * given image. If there is no metadata, or its size exceeds
 * JPEG_METADATA_LIMIT, then nothing is written.
 *
 * @param cinfo
 * @param decoded_image
 */
static void writeMetadata(
    jpeg_compress_struct& cinfo,
    const DecodedImage& decoded_image) {

  const unsigned metadata_length = decoded_image.getMetadataLength();
  if (metadata_length == 0 || metadata_length > JPEG_METADATA_LIMIT) {
    return;
  }

  jpeg_write_m_header(
      &cinfo,
      JPEG_APP0 + 1,
      JPEG_XMP_SEGMENT_HEADER_LENGTH + metadata_length);

  auto jpeg_metadata_writer = [&] (int c) { jpeg_write_m_byte(&cinfo, c); };

  // Write xmp header
  std::for_each(
      JPEG_XMP_SEGMENT_HEADER,
      JPEG_XMP_SEGMENT_HEADER + JPEG_XMP_SEGMENT_HEADER_LENGTH,
      jpeg_metadata_writer);

  // Write xmp data
  std::for_each(
      decoded_image.getMetadataPtr(),
      decoded_image.getMetadataPtr() + metadata_length,
      jpeg_metadata_writer);
}

void encodeJpegIntoOutputStream(
    JNIEnv* env,
    DecodedImage& decoded_image,
    jobject os,
    int quality) {
  // jpeg does not support alpha channel
  THROW_AND_RETURN_IF(
      decoded_image.getPixelFormat() != PixelFormat::RGB,
      "Wrong pixel format for jpeg encoding");

  struct jpeg_compress_struct cinfo;

  // set up error handling
  JpegErrorHandler error_handler{env};
  error_handler.setCompressStruct(cinfo);
  if (setjmp(error_handler.setjmpBuffer)) {
    return;
  }

  // set up OutputStream as jpeg codec destination
  jpeg_create_compress(&cinfo);
  JpegOutputStreamWrapper os_wrapper{env, os};
  cinfo.dest = &(os_wrapper.public_fields);

  // set up image properties
  cinfo.image_width = decoded_image.getWidth();
  cinfo.image_height = decoded_image.getHeight();
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  writeMetadata(cinfo, decoded_image);

  // write all pixels, row by row
  JSAMPROW row_pointer = decoded_image.getPixelsPtr();
  const int stride = decoded_image.getStride();
  while (cinfo.next_scanline < cinfo.image_height) {
    if (jpeg_write_scanlines(&cinfo, &row_pointer, 1) != 1) {
      jpegSafeThrow(
          (j_common_ptr) &cinfo,
          "Could not write scanline");
    }
    std::advance(row_pointer, stride);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
}

/**
 * Returns JXFORM_CODE corresponding to RotationType
 */
JXFORM_CODE getTransformForRotationType(RotationType rotation_type) {
  switch (rotation_type) {
  case RotationType::ROTATE_90:
    return JXFORM_ROT_90;
  case RotationType::ROTATE_180:
    return JXFORM_ROT_180;
  case RotationType::ROTATE_270:
    return JXFORM_ROT_270;
  case RotationType::FLIP_HORIZONTAL:
    return JXFORM_FLIP_H;
  case RotationType::FLIP_VERTICAL:
    return JXFORM_FLIP_V;
  case RotationType::TRANSPOSE:
    return JXFORM_TRANSPOSE;
  case RotationType::TRANSVERSE:
    return JXFORM_TRANSVERSE;
  case RotationType::ROTATE_0:
  default:
    return JXFORM_NONE;
  }
}

/**
 * Initializes decompress struct.
 *
 * <p> Sets source and error handling.
 *
 * <p> Sets decompress parameters to optimize decode time.
 */
static void initDecompressStruct(
    struct jpeg_decompress_struct& dinfo,
    JpegErrorHandler& error_handler,
    struct jpeg_source_mgr& source) {
  memset(&dinfo, 0, sizeof(struct jpeg_decompress_struct));
  error_handler.setDecompressStruct(dinfo);
  jpeg_create_decompress(&dinfo);

  // DCT method, one of JDCT_FASTEST, JDCT_IFAST, JDCT_ISLOW or JDCT_FLOAT
  dinfo.dct_method = JDCT_IFAST;
  // To perform 2-pass color quantization, the decompressor would need a
  // 128K color lookup table and a full-image pixel buffer (3 bytes/pixel).
  dinfo.two_pass_quantize = FALSE;
  // No dithering with RGB output. Use JDITHER_ORDERED only for JCS_RGB_565
  dinfo.dither_mode = JDITHER_NONE;
  // Low visual impact but big performance benefit when turning off fancy
  // up-sampling
  dinfo.do_fancy_upsampling = FALSE;
  dinfo.do_block_smoothing = FALSE;
  dinfo.enable_2pass_quant = FALSE;

  dinfo.src = &source;
  jpeg_read_header(&dinfo, true);
}

/**
 * Initializes compress struct.
 *
 * <p> Sets destination and error handler.
 *
 * <p> Sets copies params from given decompress struct
 */
static void initCompressStruct(
    struct jpeg_compress_struct& cinfo,
    struct jpeg_decompress_struct& dinfo,
    JpegErrorHandler& error_handler,
    struct jpeg_destination_mgr& destination) {
  memset(&cinfo, 0, sizeof(struct jpeg_compress_struct));
  error_handler.setCompressStruct(cinfo);
  jpeg_create_compress(&cinfo);
  cinfo.dct_method = JDCT_IFAST;
  cinfo.dest = &destination;
  cinfo.image_width = dinfo.output_width;
  cinfo.image_height = dinfo.output_height;
  cinfo.input_components = dinfo.output_components;
  cinfo.in_color_space = dinfo.out_color_space;
  jpeg_set_defaults(&cinfo);
}

/**
 * Initialize transform info structure.
 *
 * <p> Transformation is allowed to drop incomplete 8x8 blocks
 */
static void initTransformInfo(
    jpeg_transform_info& xinfo,
    jpeg_decompress_struct& dinfo,
    RotationType rotation_type) {
  memset(&xinfo, 0, sizeof(jpeg_transform_info));
  xinfo.transform = getTransformForRotationType(rotation_type);
  xinfo.trim = true;
  jtransform_request_workspace(&dinfo, &xinfo);
}

/**
 * Rotates jpeg image.
 *
 * <p> Operates on DCT blocks to avoid doing a full decode.
 */
static void rotateJpeg(
    JNIEnv* env,
    struct jpeg_source_mgr& source,
    struct jpeg_destination_mgr& destination,
    RotationType rotation_type) {
  JpegErrorHandler error_handler{env};
  if (setjmp(error_handler.setjmpBuffer)) {
    return;
  }

  // prepare decompress struct
  struct jpeg_decompress_struct dinfo;
  initDecompressStruct(dinfo, error_handler, source);

  // create compress struct
  struct jpeg_compress_struct cinfo;
  initCompressStruct(cinfo, dinfo, error_handler, destination);

  // prepare transform struct
  jpeg_transform_info xinfo;
  initTransformInfo(xinfo, dinfo, rotation_type);

  // transform
  jvirt_barray_ptr* srccoefs = jpeg_read_coefficients(&dinfo);
  jpeg_copy_critical_parameters(&dinfo, &cinfo);
  jvirt_barray_ptr* dstcoefs = jtransform_adjust_parameters(&dinfo, &cinfo, srccoefs, &xinfo);
  jpeg_write_coefficients(&cinfo, dstcoefs);
  jcopy_markers_execute(&dinfo, &cinfo, JCOPYOPT_ALL);
  jtransform_execute_transformation(&dinfo, &cinfo, srccoefs, &xinfo);

  // tear down
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

static void generateChaoticSequence(
    float *chaotic_seq,
    int n // The number of DC coefficients and also the length of chaotic_seq
    /*
    float x_0, // Secret value
    float m_u // Secret value
    */) {
  float x_0 = 0.5; // Should choose randomly from [0, 1.0]
  float mu = 3.57; // Should choose randomly from [3.57, 4.0]

  chaotic_seq[0] = mu * x_0 * (1 - x_0);

  LOGD("generateChaoticSequence chaotic_seq[0]: %f", chaotic_seq[0]);

  for (int i = 1; i < n; i++) {
    float x_n = chaotic_seq[i - 1];
    chaotic_seq[i] = mu * x_n * (1 - x_n);
    //LOGD("Generated chaotic_seq value: %f", chaotic_seq[i]);
  }

  std::sort(chaotic_seq, chaotic_seq + n, std::less<float>());
}

// The inner loop iterates over all the DC and AC coefficients in all 8x8 DCT blocks.
static void iterateDCTs(j_decompress_ptr dinfo, jvirt_barray_ptr* src_coefs) {
  // Iterate over every DCT coefficient in the image, for every color component
  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;

    LOGD("iterateDCTs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks

      // mcu_buff[y][x][c]
      // - the cth coefficient
      // - the xth horizontal block
      // - the yth vertical block
      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr)dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < comp_info->width_in_blocks; x++) {
        JCOEFPTR mcu_ptr; // Pointer to 8x8 block of coefficients (I think)
        mcu_ptr = mcu_buff[0][x];

        for (int i = 0; i < DCTSIZE2; i++) {
          mcu_ptr[i] = mcu_ptr[i] * -1; // Modify DC and AC coefficients
        }
      }
    }
  }
}

static int sameSign(JCOEF a, JCOEF b) {
  return (a < 0 && b < 0) || (a >= 0 && b >= 0);
}

struct chaos_dc {
  float chaos;
  JCOEF dc;
};

static void permuteDCGroup(
    JBLOCKARRAY mcu_buff,
    int s_start,
    int s_end,
    float *chaotic_seq,
    int chaotic_seq_n) {
  int num_blocks = s_end - s_start;
  struct chaos_dc chaos_dcs[num_blocks];
  int k = 0;

  LOGD("permuteDCGroup num_blocks=%d, s_start=%d, s_end=%d", num_blocks, s_start, s_end);

  // Permute the values in blocks mcu_buff[0][s_start, s_end]
  // according to the chaotic sequence.
  // mcu_buff[0] is constant because we are just looking at one row at a time.
  for (int i = s_start; i < s_end; i++) {
    int chaos_i = i - s_start;
    JCOEFPTR mcu_ptr = mcu_buff[0][i];

    chaos_dcs[chaos_i].chaos = chaotic_seq[k++];
    chaos_dcs[chaos_i].dc = mcu_ptr[0];
    LOGD("permuteDCGroup chaos_dc[%d].dc = %d", chaos_i, chaos_dcs[i].dc);
  }
}

// Only iterates over DC coefficients, one per 8x8 DCT block
static void iterateDCs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr *src_coefs,
    float *chaotic_seq,
    int chaotic_seq_n) {
  int chaotic_i = 0;

  // Iterate over every DCT coefficient in the image, for every color component
  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;

    LOGD("iterateDCs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks
      int s_start = 0;
      int s_end = 0;

      // mcu_buff[y][x][c]
      // - the cth coefficient
      // - the xth horizontal block
      // - the yth vertical block
      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr)dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < comp_info->width_in_blocks; x++) {
        JCOEFPTR mcu_ptr; // Pointer to 8x8 block of coefficients (I think)

        mcu_ptr = mcu_buff[0][x];
        LOGD("iterateDCs horizontal_block_x=%d, DC=%d", x, mcu_ptr[0]);

        if (s_end != 0 && !sameSign(mcu_ptr[0], mcu_buff[0][x - 1][0])) {
          LOGD("iterateDCs sameSign inputs: %d, %d", mcu_ptr[0], mcu_buff[0][x - 1][0]);
          // Permute same_sign_dcs then start the new group
          permuteDCGroup(mcu_buff, s_start, s_end, chaotic_seq, chaotic_seq_n);

          // Start the new group
          s_start = x;
          s_end = x;
          LOGD("iterateDCs s_start=%d, s_end=%d", s_start, s_end);
        } else {
          s_end++;
        }
      }

      // Permute the last same-sign DC group in the row
      permuteDCGroup(mcu_buff, s_start, s_end, chaotic_seq, chaotic_seq_n);
    }
  }
}

void decryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os) {
  JpegInputStreamWrapper is_wrapper{env, is};
  JpegOutputStreamWrapper os_wrapper{env, os};
  //JpegMemoryDestination mem_destination;
  //JpegMemorySource mem_source;
  JpegErrorHandler error_handler{env};
  struct jpeg_source_mgr& source = is_wrapper.public_fields;
  struct jpeg_destination_mgr& destination = os_wrapper.public_fields;
  float* chaotic_seq;
  int n_blocks;

  if (setjmp(error_handler.setjmpBuffer)) {
    return;
  }

  // prepare decompress struct
  struct jpeg_decompress_struct dinfo;
  initDecompressStruct(dinfo, error_handler, source);

  // create compress struct
  struct jpeg_compress_struct cinfo;
  initCompressStruct(cinfo, dinfo, error_handler, destination);

  // get DCT coefficients, 64 for 8x8 DCT blocks (first is DC, remaining 63 are AC?)
  jvirt_barray_ptr *src_coefs = jpeg_read_coefficients(&dinfo);

  // initialize with default params, then copy the ones needed for lossless transcoding
  jpeg_copy_critical_parameters(&dinfo, &cinfo);
  jcopy_markers_execute(&dinfo, &cinfo, JCOPYOPT_ALL);

  n_blocks = dinfo.comp_info->height_in_blocks * dinfo.comp_info->width_in_blocks;
  chaotic_seq = (float *) malloc(n_blocks * sizeof(float));
  if (chaotic_seq == NULL) {
    LOGE("decryptJpeg failed to alloc memory for chaotic_seq");
    goto teardown;
  }
  generateChaoticSequence(chaotic_seq, n_blocks);
  iterateDCs(&dinfo, src_coefs, chaotic_seq, n_blocks);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("decryptJpeg finished");

teardown:
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

void encryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os) {
  JpegInputStreamWrapper is_wrapper{env, is};
  JpegOutputStreamWrapper os_wrapper{env, os};
  //JpegMemoryDestination mem_destination;
  //JpegMemorySource mem_source;
  JpegErrorHandler error_handler{env};
  struct jpeg_source_mgr& source = is_wrapper.public_fields;
  struct jpeg_destination_mgr& destination = os_wrapper.public_fields;

  if (setjmp(error_handler.setjmpBuffer)) {
    return;
  }

  // prepare decompress struct
  struct jpeg_decompress_struct dinfo;
  initDecompressStruct(dinfo, error_handler, source);

  // create compress struct
  struct jpeg_compress_struct cinfo;
  initCompressStruct(cinfo, dinfo, error_handler, destination);

  // get DCT coefficients, 64 for 8x8 DCT blocks (first is DC, remaining 63 are AC?)
  jvirt_barray_ptr *src_coefs = jpeg_read_coefficients(&dinfo);

  // initialize with default params, then copy the ones needed for lossless transcoding
  jpeg_copy_critical_parameters(&dinfo, &cinfo);
  jcopy_markers_execute(&dinfo, &cinfo, JCOPYOPT_ALL);

  iterateDCTs(&dinfo, src_coefs);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("encryptJpeg finished");

  // tear down
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

/**
 * Resizes jpeg.
 *
 * <p> During the resize, the image is decoded line by line and encoded again.
 */
static void resizeJpeg(
    JNIEnv* env,
    struct jpeg_source_mgr& source,
    struct jpeg_destination_mgr& destination,
    const ScaleFactor& scale_factor,
    int quality) {
  THROW_AND_RETURN_IF(quality < 1, "quality should not be lower than 1");
  THROW_AND_RETURN_IF(quality > 100, "quality should not be greater than 100");
  THROW_AND_RETURN_IF(
      8 % scale_factor.getDenominator() > 0,
      "wrong scale denominator");
  THROW_AND_RETURN_IF(
      scale_factor.getNumerator() < 1,
      "scale numerator cannot be lower than 1");
  THROW_AND_RETURN_IF(
      scale_factor.getNumerator() > 16,
      "scale numerator cannot be greater than 16");

  JpegErrorHandler error_handler{env};
  if (setjmp(error_handler.setjmpBuffer)) {
    return;
  }

  // prepare decompress struct
  struct jpeg_decompress_struct dinfo;
  initDecompressStruct(dinfo, error_handler, source);
  dinfo.scale_num = scale_factor.getNumerator();
  dinfo.scale_denom = scale_factor.getDenominator();
  dinfo.out_color_space = JCS_RGB;
  (void) jpeg_start_decompress(&dinfo);

  // create compress struct
  struct jpeg_compress_struct cinfo;
  initCompressStruct(cinfo, dinfo, error_handler, destination);
  jpeg_set_quality(&cinfo, quality, false);
  jpeg_start_compress(&cinfo, true);

  jcopy_markers_execute(&dinfo, &cinfo, JCOPYOPT_ALL);
  size_t row_stride = dinfo.output_width * dinfo.output_components;
  JSAMPARRAY buffer = (*dinfo.mem->alloc_sarray)
    ((j_common_ptr) &dinfo, JPOOL_IMAGE, row_stride, 1);
  while (dinfo.output_scanline < dinfo.output_height) {
    jpeg_read_scanlines(&dinfo, buffer, 1);
    (void) jpeg_write_scanlines(&cinfo, buffer, 1);
  }

  // tear down
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
  jpeg_destroy_compress(&cinfo);
}

void transformJpeg(
    JNIEnv* env,
    jobject is,
    jobject os,
    RotationType rotation_type,
    const ScaleFactor& scale_factor,
    int quality) {
  const bool should_scale = scale_factor.shouldScale();
  const bool should_rotate = rotation_type != RotationType::ROTATE_0;
  THROW_AND_RETURN_IF(
      !should_scale && !should_rotate,
      "no transformation to perform");

  JpegInputStreamWrapper is_wrapper{env, is};
  JpegOutputStreamWrapper os_wrapper{env, os};
  JpegMemoryDestination mem_destination;
  JpegMemorySource mem_source;

  if (should_scale) {
    resizeJpeg(
        env,
        is_wrapper.public_fields,
        should_rotate ?
            mem_destination.public_fields : os_wrapper.public_fields,
        scale_factor,
        quality);
    RETURN_IF_EXCEPTION_PENDING;
  }

  if (should_rotate) {
    if (should_scale) {
      mem_source.setBuffer(std::move(mem_destination.buffer));
    }
    rotateJpeg(
        env,
        should_scale ? mem_source.public_fields : is_wrapper.public_fields,
        os_wrapper.public_fields,
        rotation_type);
  }
}

} } }
