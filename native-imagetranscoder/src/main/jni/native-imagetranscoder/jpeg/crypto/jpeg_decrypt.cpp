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

static void decryptAlternatingMCUs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    struct chaos_dc *chaotic_dim_array,
    int chaotic_n,
    float x_n,
    float mu_n) {
  int chaotic_i = 0;
  struct chaos_dc *chaotic_dim_array_y;

  // Iterate over every DCT coefficient in the image, for every color component
  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;

    LOGD("decryptAlternatingMCUs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks

      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      if (y > 0) {
        float min_input = 0;
        float max_input = (int) chaotic_n;
        float min_x = 0.0;
        float max_x = 1.0;
        float min_mu = 3.57;
        float max_mu = 4.0;
        int new_chaotic_input = chaotic_dim_array[chaotic_i++].chaos_pos;
        x_n = scaleToRange(y, min_input, max_input, min_x, max_x);
        mu_n = scaleToRange(y, min_input, max_input, min_mu, max_mu);
      }

      chaotic_dim_array_y = (struct chaos_dc *) malloc(comp_info->width_in_blocks * sizeof(struct chaos_dc));
      if (chaotic_dim_array_y == NULL) {
        LOGE("decryptAlternatingMCUs failed to alloc memory for chaotic_dim_array_y (%d)", y);
        return;
      }

      generateChaoticSequence(chaotic_dim_array_y, comp_info->width_in_blocks, x_n, mu_n);

      // Alternating DCT modification for reduced security but increased usability
      // Shuffle pointers to MCUs based on chaotic sequence
      // ex) Chaotic sequence:
      //  chaos  1.29   2.96   3.10   3.29   5.22
      //  pos    3      5      2      1      4
      //         A      B      C      D      E
      //  pos    1      2      3      4      5
      //         D      C      A      E      B
      // Needs decrypt:
      //  pos    3      5      2      1      4
      //         D      C      A      E      B
      //         A      B      C      D      E
      struct chaos_pos_jcoefptr *chaos_op = (struct chaos_pos_jcoefptr *) malloc(comp_info->width_in_blocks * sizeof(struct chaos_pos_jcoefptr));
      if (chaos_op == NULL) {
        LOGE("decryptAlternatingMCUs failed to alloc memory for chaos_op");
        goto end_loop;
      }

      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        JCOEFPTR dct_block = mcu_buff[0][i];
        JCOEFPTR dct_copy = (JCOEFPTR) malloc(DCTSIZE2 * sizeof(JCOEF));
        if (dct_copy == NULL) {
          LOGE("decryptAlternatingMCUs failed to alloc memory for dct_copy");
          break;
        }

        std::copy(dct_block, dct_block + DCTSIZE2, dct_copy);

        chaos_op[i].dcts = dct_copy;
        chaos_op[i].chaos_pos = chaotic_dim_array_y[i].chaos_pos;
      }

      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        unsigned int dest_pos = chaos_op[i].chaos_pos;
        JCOEFPTR dct_block = mcu_buff[0][i];

        std::copy(chaos_op[dest_pos].dcts, chaos_op[dest_pos].dcts + DCTSIZE2, dct_block);
      }

      for (int i = 0; i < comp_info->width_in_blocks; i++) {
        free(chaos_op[i].dcts);
      }

      LOGD("decryptAlternatingMCUs finished swap, values: x_n=%f, mu_n=%f", x_n, mu_n);
end_loop:
      free(chaotic_dim_array_y);
      free(chaos_op);
    }
  }
}

void decryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os) {
  JpegInputStreamWrapper is_wrapper{env, is};
  JpegOutputStreamWrapper os_wrapper{env, os};
  JpegErrorHandler error_handler{env};
  struct jpeg_source_mgr& source = is_wrapper.public_fields;
  struct jpeg_destination_mgr& destination = os_wrapper.public_fields;
  struct chaos_dc *chaotic_dim_array;

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

  chaotic_dim_array = (struct chaos_dc *) malloc(dinfo.comp_info->height_in_blocks * sizeof(struct chaos_dc));
  if (chaotic_dim_array == NULL) {
    LOGE("decryptJpeg failed to alloc memory for chaotic_dim_array");
    goto teardown;
  }

  generateChaoticSequence(chaotic_dim_array, dinfo.comp_info->height_in_blocks, 0.5, 3.57);

  decryptAlternatingMCUs(&dinfo, src_coefs, chaotic_dim_array, dinfo.comp_info->height_in_blocks, 0.5, 3.57);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("decryptJpeg finished");

teardown:
  free(chaotic_dim_array);
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

} } } }