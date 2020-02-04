#include <algorithm>
#include <iterator>
#include <random>

#include <stdio.h>
#include <setjmp.h>
#include <math.h>

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
          //std::swap(mcu_src_rows[0][src_mcu_idx], mcu_dst_rows[0][dst_mcu_idx]);
          JCOEF temp[DCTSIZE2];
          for (int i = 0; i < DCTSIZE2; i++) {
            temp[i] = mcu_src_rows[0][src_mcu_idx][i];
            mcu_src_rows[0][src_mcu_idx][i] = mcu_dst_rows[0][dst_mcu_idx][i];
          }

          for (int i = 0; i < DCTSIZE2; i++) {
            mcu_dst_rows[0][dst_mcu_idx][i] = temp[i];
          }

          sorted_blocks[chaotic_seq[curr_block].chaos_pos] = true;
          curr_block = chaotic_seq[curr_block].chaos_pos;
        } else {
          sorted_blocks[k] = true;
          k += 1;
          curr_block = k;
        }
      }
    }

    curr_block = 0;
    for (int y = 0; y < height; y++) {
      JBLOCKARRAY mcu_buff;

      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < width; x++) {
        if (chaotic_seq[curr_block].flip_sign)
          mcu_buff[0][x][0] *= -1;
        curr_block++;
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
          mcu_src_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], src_row_idx, (JDIMENSION) 1, TRUE);
          mcu_dst_rows = (dinfo->mem->access_virt_barray)((j_common_ptr) dinfo, src_coefs[comp_i], dst_row_idx, (JDIMENSION) 1, TRUE);

          // mcu_rows[y][x][c]
          // - the yth vertical block
          // - the xth horizontal block
          // - the cth coefficient
          //std::swap(mcu_src_rows[0][src_mcu_idx][1], mcu_dst_rows[0][dst_mcu_idx][1]);
          JCOEF temp[DCTSIZE2];
          // Skip the DC coefficient
          for (int i = 1; i < DCTSIZE2; i++) {
            temp[i] = mcu_src_rows[0][src_mcu_idx][i];
            mcu_src_rows[0][src_mcu_idx][i] = mcu_dst_rows[0][dst_mcu_idx][i];
          }

          for (int i = 1; i < DCTSIZE2; i++) {
            mcu_dst_rows[0][dst_mcu_idx][i] = temp[i];
          }

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

static void permuteAllACs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    mpf_t last_xn;
    struct chaos_dc *chaotic_seq;
    unsigned int n_coefficients = DCTSIZE2 - 1;

    chaotic_seq = (struct chaos_dc *) malloc(n_coefficients * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("permuteACs failed to alloc memory for chaotic_seq");
      return;
    }

    mpf_init(last_xn);

    LOGD("permuteACs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks
      JCOEF ac_coef[DCTSIZE2];

      // mcu_buff[y][x][c]
      // - the cth coefficient
      // - the xth horizontal block
      // - the yth vertical block
      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr)dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < comp_info->width_in_blocks; x++) {
        JCOEFPTR mcu_ptr; // Pointer to 8x8 block of coefficients (I think)

        if (y == 0 && x == 0)
          gen_chaotic_sequence(chaotic_seq, n_coefficients, x_0, mu, false);
        else
          gen_chaotic_sequence(chaotic_seq, n_coefficients, last_xn, mu, false);

        mpf_set(last_xn, chaotic_seq[n_coefficients - 1].chaos_gmp);
        std::sort(chaotic_seq, chaotic_seq + n_coefficients, &chaos_gmp_sorter);

        mcu_ptr = mcu_buff[0][x];

        for (int i = 0; i < n_coefficients; i++) {
          ac_coef[i] = mcu_ptr[i + 1];
        }

        for (int i = 0; i < n_coefficients; i++) {
          mcu_ptr[chaotic_seq[i].chaos_pos + 1] = ac_coef[i];
        }

        for (int i; i < n_coefficients; i++)
          mpf_clear(chaotic_seq[i].chaos_gmp);
      }
    }
end_row:
    free(chaotic_seq);
    mpf_clear(last_xn);
  }
}

static void permuteNonZeroACs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu) {

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    mpf_t last_xn;
    struct chaos_dc *chaotic_seq;

    chaotic_seq = (struct chaos_dc *) malloc(DCTSIZE2 * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("permuteACs failed to alloc memory for chaotic_seq");
      return;
    }

    mpf_init(last_xn);

    LOGD("permuteACs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks

      // mcu_buff[y][x][c]
      // - the cth coefficient
      // - the xth horizontal block
      // - the yth vertical block
      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr)dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < comp_info->width_in_blocks; x++) {
        JCOEFPTR mcu_ptr; // Pointer to 8x8 block of coefficients
        JCOEF ac_coef[DCTSIZE2];
        int non_zero_idx[DCTSIZE2];
        int non_zero_count = 0;
        int processed = 0;

        mcu_ptr = mcu_buff[0][x];

        for (int i = 1; i < DCTSIZE2; i++) {
          ac_coef[i] = mcu_ptr[i];

          if (mcu_ptr[i] == 0)
            continue;

          non_zero_idx[non_zero_count] = i;
          non_zero_count++;
        }

        if (y == 0 && x == 0)
          gen_chaotic_sequence(chaotic_seq, non_zero_count, x_0, mu, false);
        else
          gen_chaotic_sequence(chaotic_seq, non_zero_count, last_xn, mu, false);

        mpf_set(last_xn, chaotic_seq[non_zero_count - 1].chaos_gmp);
        std::sort(chaotic_seq, chaotic_seq + non_zero_count, &chaos_gmp_sorter);

        for (int i = 1; i < DCTSIZE2; i++) {
          if (ac_coef[i] == 0)
            continue;

          mcu_ptr[non_zero_idx[chaotic_seq[processed].chaos_pos]] = ac_coef[i];

          processed++;
        }

        // Clean up
        for (int i; i < non_zero_count; i++)
          mpf_clear(chaotic_seq[i].chaos_gmp);
      }
    }
end_row:
    free(chaotic_seq);
    mpf_clear(last_xn);
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
  mpf_t alpha;
  mpf_t beta;
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

  mpf_inits(alpha, beta, NULL);

  permuteDCsSimple(&dinfo, src_coefs, x_0, mu);
  //permuteDCs(&dinfo, src_coefs, x_0, mu);

  //permuteNonZeroACs(&dinfo, src_coefs, x_0, mu);
  //permuteAllACs(&dinfo, src_coefs, x_0, mu);

  construct_alpha_beta(alpha, x_0_char + (x_0_len - 2 - 16 - 1), 16);
  construct_alpha_beta(beta, mu_char + (mu_len - 1 - 16 - 1), 16);
  //diffuseACs(&dinfo, src_coefs, x_0, mu, alpha, beta, true);
  diffuseACsFlipSigns(&dinfo, src_coefs, x_0, mu, alpha, beta);

  permuteMCUs(&dinfo, src_coefs, x_0, mu);

  //encryptByRow(&dinfo, src_coefs, x_0, mu);
  //encryptByColumn(&dinfo, src_coefs, x_0, mu);

  jpeg_write_coefficients(&cinfo, src_coefs);

  LOGD("encryptDCsACsMCUs finished");

teardown:
  env->ReleaseStringUTFChars(x_0_jstr, x_0_char);
  env->ReleaseStringUTFChars(mu_jstr, mu_char);
  mpf_clears(x_0, mu, alpha, beta, NULL);
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  jpeg_destroy_decompress(&dinfo);

}

/////////////
/////////////
/////////////
static void scramble_rgb(struct rgb_block **blocks,
    unsigned int rows,
    unsigned int columns) {

  std::default_random_engine generator;

  generator.seed(10000000);

  LOGD("scramble_rgb rows=%d, columns=%d", rows, columns);

  for (int i = columns * rows - 1; i >= 0; i--) {
    int j;
    struct rgb_block temp;
    int block_i_x = i % columns;
    int block_i_y = i / columns;
    int block_j_x;
    int block_j_y;

    std::uniform_int_distribution<int> dist(0, i);
    j = dist(generator);

    block_j_x = j % columns;
    block_j_y = j / columns;

    //LOGD("scramble_rgb i=%d, j=%d, i (%d, %d), j (%d, %d)", i, j, block_i_x, block_i_y, block_j_x, block_j_y);

    temp = blocks[block_i_y][block_i_x];
    blocks[block_i_y][block_i_x] = blocks[block_j_y][block_j_x];
    blocks[block_j_y][block_j_x] = temp;
  }

  LOGD("scramble_rgb finished");
}

static void do_encrypt_etc(j_decompress_ptr dinfo,
    struct rgb_block **rgb_copy,
    unsigned int rows,
    unsigned int columns) {
  JSAMPARRAY buffer;            /* Output row buffer */
  int row_stride;               /* physical row width in output buffer */

  // make an output work buffer of the right size.
  // JSAMPLEs per row in output buffer
  row_stride = dinfo->output_width * dinfo->output_components;

  // Make a sample array that will go away when done with image
  buffer = (*dinfo->mem->alloc_sarray)((j_common_ptr) dinfo, JPOOL_IMAGE, row_stride, 1);

  LOGD("do_encrypt_etc rows=%d (height=%d), columns=%d (width=%d) / row_stride=%d", rows, dinfo->output_height, columns, dinfo->output_width, row_stride);

  // Copy decompressed RGB values to our buffer (TODO: copy to the scrambled position in rgb_copy)
  while (dinfo->output_scanline < dinfo->output_height) {
    unsigned char *pixels;
    int block_y;
    int pixel_y;

    jpeg_read_scanlines(dinfo, buffer, 1);

    pixels = (unsigned char *) buffer[0];
    block_y = dinfo->output_scanline / BLOCK_HEIGHT;
    pixel_y = dinfo->output_scanline % BLOCK_HEIGHT;

    for (int i = 0; i < row_stride; i += dinfo->output_components) {
      int block_x = i / (dinfo->output_components * BLOCK_WIDTH); // buffer is R,G,B,X,R,G,B,X,...
      int pixel_x = (i / dinfo->output_components) % BLOCK_WIDTH;

      // if (block_x >= columns || block_y >= rows || pixel_x >= BLOCK_WIDTH || pixel_y >= BLOCK_HEIGHT)
        //LOGD("do_encrypt_etc (%d, %d) / (%d, %d)", block_x, block_y, pixel_x, pixel_y);

      rgb_copy[block_y][block_x].red[pixel_y][pixel_x] = *pixels++;
      rgb_copy[block_y][block_x].blue[pixel_y][pixel_x] = *pixels++;
      rgb_copy[block_y][block_x].green[pixel_y][pixel_x] = *pixels++;
      pixels++; // We are using RGBX so there's an extra byte
    }
  }

  // Now scramble the copied RGB values
  scramble_rgb(rgb_copy, rows, columns);

  LOGD("do_encrypt_etc finished");
}

static void initialize_grayscale_compress(struct jpeg_compress_struct& cinfo,
    struct jpeg_decompress_struct& dinfo,
    JpegErrorHandler& error_handler,
    struct jpeg_destination_mgr& destination) {
  initCompressStruct(cinfo, dinfo, error_handler, destination);
  // initialize with default params, then copy the ones needed for lossless transcoding
  //jpeg_copy_critical_parameters(&dinfo, &cinfo);
  //jcopy_markers_execute(&dinfo, &cinfo, JCOPYOPT_ALL);
  cinfo.jpeg_color_space = JCS_GRAYSCALE;
  cinfo.in_color_space = JCS_GRAYSCALE;
  cinfo.input_components = 1;
  cinfo.num_components = 1;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 85, TRUE);
}

static void encrypt_etc(
    JNIEnv *env,
    jobject is,
    jobject os_red,
    jobject os_green,
    jobject os_blue,
    jstring x_0_jstr,
    jstring mu_jstr) {
  JpegInputStreamWrapper is_wrapper{env, is};
  JpegOutputStreamWrapper os_wrapper_red{env, os_red};
  JpegOutputStreamWrapper os_wrapper_green{env, os_green};
  JpegOutputStreamWrapper os_wrapper_blue{env, os_blue};
  JpegErrorHandler error_handler{env};
  struct jpeg_source_mgr& source = is_wrapper.public_fields;
  struct jpeg_destination_mgr& dest_red = os_wrapper_red.public_fields;
  struct jpeg_destination_mgr& dest_green = os_wrapper_green.public_fields;
  struct jpeg_destination_mgr& dest_blue = os_wrapper_blue.public_fields;
  struct rgb_block **rgb_copy;
  struct jpeg_decompress_struct dinfo;
  struct jpeg_compress_struct cinfo_red;
  struct jpeg_compress_struct cinfo_green;
  struct jpeg_compress_struct cinfo_blue;
  unsigned int rows;
  unsigned int columns;
  unsigned int row_stride;
  JSAMPLE *r_row; // JSAMPLE is char
  JSAMPLE *g_row; // JSAMPLE is char
  JSAMPLE *b_row; // JSAMPLE is char

  if (setjmp(error_handler.setjmpBuffer)) {
    return;
  }

  // prepare decompress struct
  initDecompressStruct(dinfo, error_handler, source);
  dinfo.out_color_space = JCS_EXT_RGBX;

  jpeg_start_decompress(&dinfo);

  rows = ceil(dinfo.output_height / BLOCK_HEIGHT) + 1;
  columns = ceil(dinfo.output_width / BLOCK_WIDTH) + 1;
  rgb_copy = new struct rgb_block *[rows];
  for (int i = 0; i < rows; ++i)
    rgb_copy[i] = new rgb_block[columns];

  do_encrypt_etc(&dinfo, rgb_copy, rows, columns);

  // Now ready to write the output compressed JPEG
  // create compress struct
  initialize_grayscale_compress(cinfo_red, dinfo, error_handler, dest_red);
  initialize_grayscale_compress(cinfo_green, dinfo, error_handler, dest_green);
  initialize_grayscale_compress(cinfo_blue, dinfo, error_handler, dest_blue);
  jpeg_start_compress(&cinfo_red, TRUE);
  jpeg_start_compress(&cinfo_green, TRUE);
  jpeg_start_compress(&cinfo_blue, TRUE);

  // Now write the scrambled RGB channels
  row_stride = cinfo_red.image_width; /* JSAMPLEs per row in image_buffer */

  r_row = (JSAMPLE *) malloc(row_stride * sizeof(JSAMPLE));
  g_row = (JSAMPLE *) malloc(row_stride * sizeof(JSAMPLE));
  b_row = (JSAMPLE *) malloc(row_stride * sizeof(JSAMPLE));
  if (r_row == NULL || g_row == NULL || b_row == NULL) {
    LOGE("encrypt_etc failed to allocate r_row / g_row / b_row");
    goto teardown;
  }

  LOGD("encrypt_etc row_stride=%d, num_components=%d", row_stride, cinfo_red.num_components);
  while (cinfo_red.next_scanline < cinfo_red.image_height) {
    JSAMPROW row_pointer[row_stride];
    int block_y = cinfo_red.next_scanline / BLOCK_HEIGHT;
    int pixel_y = cinfo_red.next_scanline % BLOCK_HEIGHT;

    for (int i = 0; i < row_stride; i++) {
      int block_x = i / BLOCK_WIDTH;
      int pixel_x = i % BLOCK_WIDTH;

      r_row[i] = rgb_copy[block_y][block_x].red[pixel_y][pixel_x];
      g_row[i] = rgb_copy[block_y][block_x].green[pixel_y][pixel_x];
      b_row[i] = rgb_copy[block_y][block_x].blue[pixel_y][pixel_x];
    }

    row_pointer[0] = r_row;
    jpeg_write_scanlines(&cinfo_red, row_pointer, 1);
    row_pointer[0] = g_row;
    jpeg_write_scanlines(&cinfo_green, row_pointer, 1);
    row_pointer[0] = b_row;
    jpeg_write_scanlines(&cinfo_blue, row_pointer, 1);
  }

  LOGD("encrypt_etc finished");

teardown:
  for (int i = 0; i < rows; ++i)
    delete[] rgb_copy[i];

  delete[] rgb_copy;
  if (r_row != NULL)
    free(r_row);
  if (g_row != NULL)
    free(g_row);
  if (b_row != NULL)
    free(b_row);
  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);
  jpeg_finish_compress(&cinfo_red);
  jpeg_finish_compress(&cinfo_green);
  jpeg_finish_compress(&cinfo_blue);
  jpeg_destroy_compress(&cinfo_red);
  jpeg_destroy_compress(&cinfo_green);
  jpeg_destroy_compress(&cinfo_blue);
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

void encryptJpegEtc(
    JNIEnv *env,
    jobject is,
    jobject os_red,
    jobject os_green,
    jobject os_blue,
    jstring x_0_jstr,
    jstring mu_jstr) {
  encrypt_etc(env, is, os_red, os_green, os_blue, x_0_jstr, mu_jstr);
}

} } } }