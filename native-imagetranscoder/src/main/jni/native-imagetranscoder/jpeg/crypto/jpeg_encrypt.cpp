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

static void encryptByRow(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  // Iterate over every DCT coefficient in the image, for every color component
  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    bool *sorted_blocks;
    struct chaos_dc *chaotic_seq;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int n_blocks = comp_info->width_in_blocks * comp_info->height_in_blocks;

    // Note: comp_info->width_in_blocks (chaos_len) is not the same for every component
    sorted_blocks = (bool *) malloc(width * sizeof(bool));
    if (sorted_blocks == NULL) {
      LOGE("encryptByRow failed to alloc memory for sorted_blocks");
      return;
    }

    chaotic_seq = (struct chaos_dc *) malloc(n_blocks * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("encryptByRow failed to alloc memory for chaotic_seq");
      return;
    }

    gen_chaotic_per_row(chaotic_seq, width, comp_info->height_in_blocks, x_0, mu);

    LOGD("encryptByRow iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks
      int k, curr_block;

      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      // Shuffle pointers to MCUs based on chaotic sequence
      // ex) Chaotic sequence:
      //  chaos  1.29   2.96   3.10   3.29   5.22
      //  pos    3      5      2      1      4
      //         A      B      C      D      E
      //  pos    1      2      3      4      5
      //         D      C      A      E      B
      k = 0;
      curr_block = k;
      std::fill(sorted_blocks, sorted_blocks + width, false);
      while (k < width) {
        unsigned int mcu_ptr_new_pos;

        if (sorted_blocks[k]) {
          k += 1;
          curr_block = k;
          continue;
        }

        // curr_block is in the wrong spot, look up where it should go
        mcu_ptr_new_pos = chaotic_seq[y * width + curr_block].chaos_pos;
        //LOGD("encryptByRow mcu_ptr_new_pos=%d", mcu_ptr_new_pos);

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

      //LOGD("encryptByRow finished swap, values: x_0=%f, mu=%f", x_0, mu);
    }

    for (int i; i < n_blocks; i++) {
      mpf_clear(chaotic_seq[i].chaos_gmp);
    }

    free(chaotic_seq);
    free(sorted_blocks);
  }
}

static void encryptByColumn(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_n,
    mpf_t mu_n) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    int k = 0;
    int curr_row = k;
    unsigned int chaos_len = comp_info->height_in_blocks;
    struct chaos_dc *chaotic_seq;
    bool *sorted_rows;

    LOGD("encryptByColumn iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    // Note: comp_info->height_in_blocks (chaos_len) is not the same for every component
    chaotic_seq = (struct chaos_dc *) malloc(chaos_len * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("encryptByColumn failed to alloc memory for chaotic_seq");
      return;
    }

    sorted_rows = (bool *) malloc(chaos_len * sizeof(bool));
    if (sorted_rows == NULL) {
      LOGE("encryptByColumn failed to alloc memory for sorted_rows");
      free(chaotic_seq);
      return;
    }

    gen_chaotic_sequence(chaotic_seq, chaos_len, x_n, mu_n);

    std::fill(sorted_rows, sorted_rows + chaos_len, false);

    while (k < comp_info->height_in_blocks) {
      unsigned int row_new_pos;

      if (sorted_rows[k]) {
        k += 1;
        curr_row = k;
        continue;
      }

      // curr_row is in the wrong spot, look up where it should go
      row_new_pos = chaotic_seq[curr_row].chaos_pos;

      if (row_new_pos != k) {
        JBLOCKARRAY row_a; // JBLOCKROW *
        JBLOCKARRAY row_b; // JBLOCKROW *
        // Swap curr_block with its correct new position, then mark the new position as sorted
        row_a = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], k, (JDIMENSION) 1, TRUE);
        row_b = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], row_new_pos, (JDIMENSION) 1, TRUE);

        std::swap(row_a[0], row_b[0]);

        sorted_rows[row_new_pos] = true;

        // The row that we swapped with might be in the wrong spot now,
        // so keep k the same but update curr_row so it can do the look up
        // on the original position of the row now in position k
        curr_row = row_new_pos;
      } else {
        sorted_rows[k] = true;
        k += 1;
        curr_row = k;
      }
    }

    for (int i; i < chaos_len; i++) {
      mpf_clear(chaotic_seq[i].chaos_gmp);
    }

    free(sorted_rows);
    free(chaotic_seq);
  }
}

void encryptJpegByRowAndColumn(
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
  mp_exp_t exponent;
  char *mpf_val;
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
    LOGD("encryptJpegByRowAndColumn failed to mpf_set_str(x_0)");
    goto teardown;
  }
  if (mpf_init_set_str(mu, mu_char, 10)) {
    LOGD("encryptJpegByRowAndColumn failed to mpf_set_str(mu)");
    goto teardown;
  }

  mpf_val = mpf_get_str(NULL, &exponent, 10, 500, x_0);
  LOGD("encryptJpegByRowAndColumn allocated mpf_t x_0 and mu");
  LOGD("fixed point mpf %s", mpf_val);
  free(mpf_val);
  LOGD("encryptJpegByRowAndColumn allocated mpf_t x_0 and mu 2");

  encryptByRow(&dinfo, src_coefs, x_0, mu);
  encryptByColumn(&dinfo, src_coefs, x_0, mu);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("encryptJpegByRowAndColumn finished");

teardown:
  env->ReleaseStringUTFChars(x_0_jstr, x_0_char);
  env->ReleaseStringUTFChars(mu_jstr, mu_char);
  mpf_clears(x_0, mu, NULL);
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);
}

/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
/////////////////////////////////////////////////////

struct mcu_sign {
  short sign; // 0 = negative, 1 = non-negative
};

static void permuteSameSignDCGroup(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    unsigned int width,
    unsigned int height,
    int comp_i,
    unsigned int start,
    unsigned int end,
    mpf_t x_0,
    mpf_t mu) {
  bool *sorted_dcs;
  struct chaos_dc *chaotic_seq;
  unsigned int n_blocks = end - start + 1;
  unsigned int k, curr_block;

  sorted_dcs = (bool *) malloc(n_blocks * sizeof(bool));
  if (sorted_dcs == NULL) {
    LOGE("permuteSameSignDCGroup failed to alloc memory for sorted_dcs");
    return;
  }
  chaotic_seq = (struct chaos_dc *) malloc(n_blocks * sizeof(struct chaos_dc));
  if (chaotic_seq == NULL) {
    LOGE("permuteSameSignDCGroup failed to alloc memory for chaotic_seq");
    return;
  }
  gen_chaotic_sequence(chaotic_seq, n_blocks, x_0, mu);

  k = 0;
  curr_block = k;
  std::fill(sorted_dcs, sorted_dcs + n_blocks, false);
  while (k < n_blocks) {
    if (sorted_dcs[k]) {
      k += 1;
      curr_block = k;
      continue;
    } else {
      JBLOCKARRAY mcu_src_rows;
      JBLOCKARRAY mcu_dst_rows;
      int src_row_idx = 0;
      int dst_row_idx = 0;
      int src_mcu_idx = 0;
      int dst_mcu_idx = 0;

      // figure out where block k is in the image in the context of the 2D image array
      src_row_idx = (k + start) / width;
      src_mcu_idx = (k + start) - src_row_idx * width;

      // look up where curr_block should go in the context of the 2D image array
      dst_row_idx = (chaotic_seq[curr_block].chaos_pos + start) / width;
      dst_mcu_idx = (chaotic_seq[curr_block].chaos_pos + start) - dst_row_idx * width;

      if (chaotic_seq[curr_block].chaos_pos != k) {
        mcu_src_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], src_row_idx, (JDIMENSION) 1, TRUE);
        mcu_dst_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], dst_row_idx, (JDIMENSION) 1, TRUE);

        // mcu_rows[y][x][c]
        // - the yth vertical block
        // - the xth horizontal block
        // - the cth coefficient
        std::swap(mcu_src_rows[0][src_mcu_idx][0], mcu_dst_rows[0][dst_mcu_idx][0]);

        sorted_dcs[chaotic_seq[curr_block].chaos_pos] = true;
        curr_block = chaotic_seq[curr_block].chaos_pos;
      } else {
        sorted_dcs[k] = true;
        k += 1;
        curr_block = k;
      }
    }
  }

  //LOGD("permuteSameSignDCGroup finished: start=%d, end=%d", start, end);

  for (int i; i < n_blocks; i++) {
    mpf_clear(chaotic_seq[i].chaos_gmp);
  }

  free(chaotic_seq);
  free(sorted_dcs);
}

static void permuteDCs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    unsigned int n_blocks = comp_info->width_in_blocks * comp_info->height_in_blocks;
    struct mcu_sign *dc_signs;
    unsigned int dc_i;
    // Note: comp_info->width_in_blocks is not the same for every component
    LOGD("permuteDCs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, height);

    dc_signs = (struct mcu_sign *) malloc(n_blocks * sizeof(struct mcu_sign));
    if (dc_signs == NULL) {
      LOGE("permuteDCs failed to alloc memory for dc_signs");
      return;
    }

    dc_i = 0;

    for (int y = 0; y < height; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks

      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr)dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < width; x++) {
        JCOEF dc = mcu_buff[0][x][0];

        if (dc < 0)
          dc_signs[dc_i].sign = 0;
        else
          dc_signs[dc_i].sign = 1;


        dc_i++;
      }
    }

    dc_i = 0;
    unsigned int same_start = 0;
    unsigned int same_end = 0;
    short curr_sign = -1;

    while (dc_i < n_blocks) {
      // If first iteration, set curr_sign and keep moving
      if (curr_sign == -1) {
        curr_sign = dc_signs[dc_i].sign;
        dc_i++;
        continue;
      }

      // DC coef is within current window, move the end of the window forward
      if (dc_signs[dc_i].sign == curr_sign) {
        same_end = dc_i;
        dc_i++;
        continue;
      }

      // DC coef is not within the current window, permute the group then reset the window
      permuteSameSignDCGroup(dinfo, src_coefs, width, height, comp_i, same_start, same_end, x_0, mu);

      // Create new window
      same_start = dc_i;
      same_end = dc_i;
      curr_sign = dc_signs[dc_i].sign;
      dc_i++;
    }
    // Permute the final window which was missed in the loop
    permuteSameSignDCGroup(dinfo, src_coefs, width, height, comp_i, same_start, same_end, x_0, mu);

    free(dc_signs);
  }
}

static void permuteDCsSimple(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    bool *sorted_blocks;
    struct chaos_dc *chaotic_seq;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    unsigned int n_blocks = comp_info->width_in_blocks * comp_info->height_in_blocks;
    // Note: comp_info->width_in_blocks is not the same for every component
    LOGD("permuteDCs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, height);

    sorted_blocks = (bool *) malloc(n_blocks * sizeof(bool));
    if (sorted_blocks == NULL) {
      LOGE("permuteDCs failed to alloc memory for sorted_blocks");
      return;
    }
    chaotic_seq = (struct chaos_dc *) malloc(n_blocks * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("permuteDCs failed to alloc memory for chaotic_seq");
      return;
    }
    gen_chaotic_sequence(chaotic_seq, n_blocks, x_0, mu);

    int k, curr_block;
    k = 0;
    curr_block = k;
    std::fill(sorted_blocks, sorted_blocks + n_blocks, false);
    while (k < n_blocks) {
      if (sorted_blocks[k]) {
        k += 1;
        curr_block = k;
        continue;
      } else {
        JBLOCKARRAY mcu_src_rows;
        JBLOCKARRAY mcu_dst_rows;
        int src_row_idx = 0;
        int dst_row_idx = 0;
        int src_mcu_idx = 0;
        int dst_mcu_idx = 0;

        // figure out where block k is in the image in the context of the 2D image array
        src_row_idx = k / width;
        src_mcu_idx = k - src_row_idx * width;

        // look up where curr_block should go in the context of the 2D image array
        dst_row_idx = chaotic_seq[curr_block].chaos_pos / width;
        dst_mcu_idx = chaotic_seq[curr_block].chaos_pos - dst_row_idx * width;

        //LOGD("permuteMCUs src_row_idx=%d, src_mcu_idx=%d, dst_row_idx=%d, dst_mcu_idx=%d", src_row_idx, src_mcu_idx, dst_row_idx, dst_mcu_idx);

        if (chaotic_seq[curr_block].chaos_pos != k) {
          mcu_src_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], src_row_idx, (JDIMENSION) 1, TRUE);
          mcu_dst_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], dst_row_idx, (JDIMENSION) 1, TRUE);

          // mcu_rows[y][x][c]
          // - the yth vertical block
          // - the xth horizontal block
          // - the cth coefficient
          std::swap(mcu_src_rows[0][src_mcu_idx][0], mcu_dst_rows[0][dst_mcu_idx][0]);

          sorted_blocks[chaotic_seq[curr_block].chaos_pos] = true;
          curr_block = chaotic_seq[curr_block].chaos_pos;
        } else {
          sorted_blocks[k] = true;
          k += 1;
          curr_block = k;
        }
      }
    }

    for (int i; i < n_blocks; i++) {
      mpf_clear(chaotic_seq[i].chaos_gmp);
    }

    free(chaotic_seq);
    free(sorted_blocks);
  }
}

static void permuteMCUs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    bool *sorted_blocks;
    struct chaos_dc *chaotic_seq;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    unsigned int n_blocks = width * height;
    // Note: comp_info->width_in_blocks is not the same for every component
    LOGD("permuteMCUs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, height);

    sorted_blocks = (bool *) malloc(n_blocks * sizeof(bool));
    if (sorted_blocks == NULL) {
      LOGE("permuteMCUs failed to alloc memory for sorted_blocks");
      return;
    }
    chaotic_seq = (struct chaos_dc *) malloc(n_blocks * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("permuteMCUs failed to alloc memory for chaotic_seq");
      return;
    }
    gen_chaotic_sequence(chaotic_seq, n_blocks, x_0, mu);

    int k, curr_block;
    k = 0;
    curr_block = k;
    std::fill(sorted_blocks, sorted_blocks + n_blocks, false);
    while (k < n_blocks) {
      if (sorted_blocks[k]) {
        k += 1;
        curr_block = k;
        continue;
      } else {
        JBLOCKARRAY mcu_src_rows;
        JBLOCKARRAY mcu_dst_rows;
        int src_row_idx = 0;
        int dst_row_idx = 0;
        int src_mcu_idx = 0;
        int dst_mcu_idx = 0;

        // figure out where block k is in the image in the context of the 2D image array
        src_row_idx = k / width;
        src_mcu_idx = k - src_row_idx * width;

        // look up where curr_block should go in the context of the 2D image array
        dst_row_idx = chaotic_seq[curr_block].chaos_pos / width;
        dst_mcu_idx = chaotic_seq[curr_block].chaos_pos - dst_row_idx * width;

        //LOGD("permuteMCUs src_row_idx=%d, src_mcu_idx=%d, dst_row_idx=%d, dst_mcu_idx=%d", src_row_idx, src_mcu_idx, dst_row_idx, dst_mcu_idx);

        if (chaotic_seq[curr_block].chaos_pos != k) {
          JCOEF *temp;
          mcu_src_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], src_row_idx, (JDIMENSION) 1, TRUE);
          mcu_dst_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], dst_row_idx, (JDIMENSION) 1, TRUE);

          temp = (JCOEF *) malloc((DCTSIZE2 - 1) * sizeof(JCOEF));
          if (temp == NULL) {
           LOGE("permuteMCUs failed to alloc memory for JCOEF temp");
           return;
          }

          // mcu_rows[y][x][c]
          // - the yth vertical block
          // - the xth horizontal block
          // - the cth coefficient
          std::swap(mcu_src_rows[0][src_mcu_idx], mcu_dst_rows[0][dst_mcu_idx]);
          //std::copy_n(&mcu_src_rows[0][src_mcu_idx][1], DCTSIZE2 - 1, temp);
          //std::copy_n(mcu_dst_rows[0][dst_mcu_idx], DCTSIZE2 - 1, &mcu_src_rows[0][src_mcu_idx][1]);
          //std::copy_n(temp, DCTSIZE2 - 1, &mcu_dst_rows[0][dst_mcu_idx][1]);

          sorted_blocks[chaotic_seq[curr_block].chaos_pos] = true;
          curr_block = chaotic_seq[curr_block].chaos_pos;

          free(temp);
        } else {
          sorted_blocks[k] = true;
          k += 1;
          curr_block = k;
        }
      }
    }

    for (int i; i < n_blocks; i++) {
      mpf_clear(chaotic_seq[i].chaos_gmp);
    }

    free(chaotic_seq);
    free(sorted_blocks);
  }
}

static void encryptDCsACsMCUs(
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
    LOGD("encryptDCsACsMCUs failed to mpf_set_str(x_0)");
    goto teardown;
  }
  if (mpf_init_set_str(mu, mu_char, 10)) {
    LOGD("encryptDCsACsMCUs failed to mpf_set_str(mu)");
    goto teardown;
  }

  LOGD("encryptDCsACsMCUs allocated mpf_t x_0 and mu");

  permuteDCsSimple(&dinfo, src_coefs, x_0, mu);
  //permuteDCs(&dinfo, src_coefs, x_0, mu);
  permuteMCUs(&dinfo, src_coefs, x_0, mu);

  //encryptByRow(&dinfo, src_coefs, x_0, mu);
  //encryptByColumn(&dinfo, src_coefs, x_0, mu);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("encryptDCsACsMCUs finished");

teardown:
  env->ReleaseStringUTFChars(x_0_jstr, x_0_char);
  env->ReleaseStringUTFChars(mu_jstr, mu_char);
  mpf_clears(x_0, mu, NULL);
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);

}

void encryptJpeg(
    JNIEnv *env,
    jobject is,
    jobject os,
    jstring x_0_jstr,
    jstring mu_jstr) {
  //encryptJpegByRowAndColumn(env, is, os, x_0_jstr, mu_jstr);
  encryptDCsACsMCUs(env, is, os, x_0_jstr, mu_jstr);
}

} } } }