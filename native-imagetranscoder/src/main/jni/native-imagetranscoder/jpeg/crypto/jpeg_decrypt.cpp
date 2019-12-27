#include <algorithm>
#include <iterator>

#include <stdio.h>
#include <setjmp.h>

#include <jni.h>
#include <jpeglib.h>
extern "C" {
  #include "transupp.h"
}
#include <gmp.h>

#include "decoded_image.h"
#include "exceptions_handler.h"
#include "logging.h"
#include "jpeg/jpeg_error_handler.h"
#include "jpeg/jpeg_memory_io.h"
#include "jpeg/jpeg_stream_wrappers.h"
#include "jpeg/jpeg_codec.h"
#include "jpeg_crypto.h"
#include "jpeg_decrypt.h"

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

struct chaos_pos_jcoefptr {
  unsigned int chaos_pos;

  JCOEFPTR dcts;
};

struct chaos_pos_jblockrow {
  unsigned int chaos_pos;

  JBLOCKROW row;
};

static void decryptByRow(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  // Iterate over every DCT coefficient in the image, for every color component
  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    struct chaos_dc *chaotic_seq;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int n_blocks = comp_info->width_in_blocks * comp_info->height_in_blocks;

    chaotic_seq = (struct chaos_dc *) malloc(n_blocks * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("decryptByRow failed to alloc memory for chaotic_seq");
      return;
    }

    gen_chaotic_per_row(chaotic_seq, width, comp_info->height_in_blocks, x_0, mu);

    LOGD("decryptByRow iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      struct chaos_pos_jcoefptr *chaos_op;
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks

      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      // Shuffle pointers to MCUs based on chaotic sequence
      chaos_op = (struct chaos_pos_jcoefptr *) malloc(comp_info->width_in_blocks * sizeof(struct chaos_pos_jcoefptr));
      if (chaos_op == NULL) {
        LOGE("decryptByRow failed to alloc memory for chaos_op");
        goto end_loop;
      }

      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        JCOEFPTR dct_block = mcu_buff[0][i];
        JCOEFPTR dct_copy = (JCOEFPTR) malloc(DCTSIZE2 * sizeof(JCOEF));
        if (dct_copy == NULL) {
          LOGE("decryptByRow failed to alloc memory for dct_copy");
          goto end_loop;
        }

        std::copy(dct_block, dct_block + DCTSIZE2, dct_copy);

        chaos_op[i].dcts = dct_copy;
        chaos_op[i].chaos_pos = chaotic_seq[y * width + i].chaos_pos;
      }

      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        unsigned int dest_pos = chaos_op[i].chaos_pos;
        JCOEFPTR dct_block = mcu_buff[0][i];

        std::copy(chaos_op[dest_pos].dcts, chaos_op[dest_pos].dcts + DCTSIZE2, dct_block);
      }
      LOGD("decryptByRow finished swap for component %d", comp_i);

end_loop:
      if (chaos_op != NULL) {
        for (int i = 0; i < comp_info->width_in_blocks; i++) {
          if (chaos_op[i].dcts != NULL)
            free(chaos_op[i].dcts);
        }

        free(chaos_op);
      }
    }

    for (int i; i < n_blocks; i++) {
      mpf_clear(chaotic_seq[i].chaos_gmp);
    }

    free(chaotic_seq);
  }
}

static void decryptByColumn(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_n,
    mpf_t mu_n) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    struct chaos_dc *chaotic_seq;
    struct chaos_pos_jblockrow *chaos_op;
    unsigned int chaos_len = comp_info->height_in_blocks;

    LOGD("decryptByColumn iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    chaotic_seq = (struct chaos_dc *) malloc(chaos_len * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("decryptByColumn failed to alloc memory for chaotic_seq");
      return;
    }

    chaos_op = (struct chaos_pos_jblockrow *) malloc(chaos_len * sizeof(struct chaos_pos_jblockrow));
    if (chaos_op == NULL) {
      LOGE("decryptByColumn failed to alloc memory for chaos_op");
      goto end_loop;
    }

    gen_chaotic_sequence(chaotic_seq, chaos_len, x_n, mu_n);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKROW row;
      JBLOCKROW row_copy;

      row = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE)[0];
      row_copy = (JBLOCKROW) malloc(comp_info->width_in_blocks * sizeof(JBLOCK));
      if (row_copy == NULL) {
        LOGE("decryptByColumn failed to alloc memory for row_copy");
        goto end_loop;
      }

      // Copy each block in this row
      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        std::copy(row[i], row[i] + DCTSIZE2, row_copy[i]);
      }

      chaos_op[y].row = row_copy;
      chaos_op[y].chaos_pos = chaotic_seq[y].chaos_pos;
    }

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      unsigned int dest_pos = chaos_op[y].chaos_pos;
      JBLOCKROW row;

      row = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE)[0];

      // Copy each block to their correct row y
      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        std::copy(chaos_op[dest_pos].row[i], chaos_op[dest_pos].row[i] + DCTSIZE2, row[i]);
      }
    }
    LOGD("decryptByColumn finished swap for component %d", comp_i);

end_loop:
    for (int i = 0; i < comp_info->height_in_blocks; i++) {
      if (chaos_op != NULL && chaos_op[i].row != NULL)
        free(chaos_op[i].row);
    }
    free(chaos_op);

    for (int i; i < chaos_len; i++) {
      mpf_clear(chaotic_seq[i].chaos_gmp);
    }
    free(chaotic_seq);
  }
}

void decryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os,
    jstring x_0_jstr,
    jstring mu_jstr) {
  JpegInputStreamWrapper is_wrapper{env, is};
  JpegOutputStreamWrapper os_wrapper{env, os};
  JpegErrorHandler error_handler{env};
  struct jpeg_source_mgr& source = is_wrapper.public_fields;
  struct jpeg_destination_mgr& destination = os_wrapper.public_fields;
  mpf_t x_0;
  mpf_t mu;
  jsize x_0_len = env->GetStringUTFLength(x_0_jstr);
  jsize mu_len = env->GetStringUTFLength(mu_jstr);
  const char *x_0_char = env->GetStringUTFChars(x_0_jstr, (jboolean *) 0);
  const char *mu_char = env->GetStringUTFChars(mu_jstr, (jboolean *) 0);

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

  if (mpf_init_set_str(x_0, x_0_char, 10)) {
    LOGD("decryptJpeg failed to mpf_set_str(x_0)");
    goto teardown;
  }
  if (mpf_init_set_str(mu, mu_char, 10)) {
    LOGD("decryptJpeg failed to mpf_set_str(mu)");
    goto teardown;
  }

  decryptByColumn(&dinfo, src_coefs, x_0, mu);
  decryptByRow(&dinfo, src_coefs, x_0, mu);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("decryptJpeg finished");

teardown:
  env->ReleaseStringUTFChars(x_0_jstr, x_0_char);
  env->ReleaseStringUTFChars(mu_jstr, mu_char);
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

} } } }