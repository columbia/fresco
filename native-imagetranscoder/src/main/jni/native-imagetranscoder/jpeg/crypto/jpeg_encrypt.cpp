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
#include "jpeg_encrypt.h"

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

static void permuteDCGroup(
    JBLOCKARRAY mcu_buff,
    int s_start,
    int s_end,
    struct chaos_dc *chaotic_seq,
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

    chaos_dcs[chaos_i].chaos = chaotic_seq[k].chaos;
    chaos_dcs[chaos_i].chaos_pos = chaotic_seq[k++].chaos_pos;
    chaos_dcs[chaos_i].dc = mcu_ptr[0];
    LOGD("permuteDCGroup chaos_dc[%d].dc = %d", chaos_i, chaos_dcs[i].dc);
  }

  std::sort(chaos_dcs, chaos_dcs + num_blocks, &chaos_pos_sorter);

  for (int i = 0; i < num_blocks; i++) {
    LOGD("permuteDCGroup sorted chaos_dcs[%d]: pos=%u, chaos=%f, dc=%d", i, chaos_dcs[i].chaos_pos, chaos_dcs[i].chaos, chaos_dcs[i].dc);
    JCOEFPTR mcu_ptr = mcu_buff[0][i];

    mcu_ptr[0] = chaos_dcs[i].dc;
  }
}

// Only iterates over DC coefficients, one per 8x8 DCT block
static void iterateDCs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr *src_coefs,
    struct chaos_dc *chaotic_seq,
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

      // s_start and s_end represent a sliding window of DCs with
      // the same sign in this row. We then permute the DCs within
      // a same sign group according to the permuted random chaotic
      // sequence passed in.
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

static void encryptJpegHe2018(
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
  struct chaos_dc *chaotic_seq;
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
  chaotic_seq = (struct chaos_dc *) malloc(n_blocks * sizeof(struct chaos_dc));
  if (chaotic_seq == NULL) {
    LOGE("encryptJpegHe2018 failed to alloc memory for chaotic_seq");
    goto teardown;
  }
  generateChaoticSequence(chaotic_seq, n_blocks, 0.5, 3.57);
  iterateDCs(&dinfo, src_coefs, chaotic_seq, n_blocks);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("encryptJpegHe2018 finished");

teardown:
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
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

static void encryptAlternatingMCUs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    struct chaos_dc *chaotic_dim_array,
    int chaotic_n) {
  int chaotic_i = 0;
  float x_0 = 0.5; // Should choose from [0, 1.0] - use (He 2018)'s approach for creating a key as inputs
  float mu = 3.57; // Should choose from [3.57, 4.0]
  float x_n = x_0;
  float mu_n = mu;
  struct chaos_dc *chaotic_dim_array_y;

  // Iterate over every DCT coefficient in the image, for every color component
  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    bool *sorted_blocks;
    unsigned int chaos_len = comp_info->width_in_blocks;

    sorted_blocks = (bool *) malloc(comp_info->width_in_blocks * sizeof(bool));
    if (sorted_blocks == NULL) {
      LOGE("encryptAlternatingMCUs failed to alloc memory for sorted_blocks");
      return;
    }

    std::fill(sorted_blocks, sorted_blocks + comp_info->width_in_blocks, false);

    LOGD("encryptAlternatingMCUs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks
      int k, curr_block;

      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      if (y > 0) {
        float min_dct = -128;
        float max_dct = 128;
        float min_x = 0.0;
        float max_x = 1.0;
        float min_mu = 3.57;
        float max_mu = 4.0;

        x_n = scaleToRange(55, min_dct, max_dct, min_x, max_x);
        mu_n = scaleToRange(55, min_dct, max_dct, min_mu, max_mu);
      }

      chaotic_dim_array_y = (struct chaos_dc *) malloc(chaos_len * sizeof(struct chaos_dc));
      if (chaotic_dim_array_y == NULL) {
        LOGE("encryptAlternatingMCUs failed to alloc memory for chaotic_dim_array_y (%d)", y);
        continue;
      }

      generateChaoticSequence(chaotic_dim_array_y, chaos_len, x_n, mu_n);

      // Alternating DCT modification for reduced security but increased usability
      // Shuffle pointers to MCUs based on chaotic sequence
      // ex) Chaotic sequence:
      //  chaos  1.29   2.96   3.10   3.29   5.22
      //  pos    3      5      2      1      4
      //         A      B      C      D      E
      //  pos    1      2      3      4      5
      //         D      C      A      E      B
      k = 0;
      curr_block = k;
      std::fill(sorted_blocks, sorted_blocks + comp_info->width_in_blocks, false);
      while (k < comp_info->width_in_blocks) {
        JCOEFPTR mcu_ptr;
        unsigned int mcu_ptr_new_pos;

        if (sorted_blocks[k]) {
          k += 1;
          curr_block = k;
          continue;
        }

        // curr_block is in the wrong spot, look up where it should go
        mcu_ptr = mcu_buff[0][k];
        mcu_ptr_new_pos = chaotic_dim_array_y[curr_block].chaos_pos;
        //LOGD("iterateAlternatingMCUs curr_block=%d, mcu_ptr_new_pos=%d", curr_block, mcu_ptr_new_pos);

        if (mcu_ptr_new_pos != k) {
          // Swap curr_block with its correct new position, then mark the new position as sorted
          std::swap(mcu_buff[0][k], mcu_buff[0][mcu_ptr_new_pos]);
          sorted_blocks[mcu_ptr_new_pos] = true;

          // The block that we swapped with might be in the wrong spot now,
          // so keep k the same but update curr_block so it can do the look up
          // on the original position of the block now in position k
          curr_block = mcu_ptr_new_pos;
        } else {
          sorted_blocks[k] = true;
          k += 1;
          curr_block = k;
        }
      }

      LOGD("encryptAlternatingMCUs finished swap, values: x_n=%f, mu_n=%f", x_n, mu_n);
      free(chaotic_dim_array_y);
    }

    free(sorted_blocks);
  }
}

void encryptJpegAlternatingMCUs(
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
    LOGE("encryptJpegAlternatingMCUs failed to alloc memory for chaotic_dim_array");
    goto teardown;
  }

  generateChaoticSequence(chaotic_dim_array, dinfo.comp_info->height_in_blocks, 0.5, 3.57);

  LOGD("encryptJpegAlternatingMCUs dinfo.comp_info->height_in_blocks=%d", dinfo.comp_info->height_in_blocks);

  encryptAlternatingMCUs(&dinfo, src_coefs, chaotic_dim_array, dinfo.comp_info->height_in_blocks);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("encryptJpegAlternatingMCUs finished");

teardown:
  free(chaotic_dim_array);
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

void encryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os) {
  encryptJpegAlternatingMCUs(env, is, os);
}

} } } }