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
#include <math.h>
#include <bitset>

#include "decoded_image.h"
#include "exceptions_handler.h"
#include "logging.h"
#include "jpeg/jpeg_error_handler.h"
#include "jpeg/jpeg_memory_io.h"
#include "jpeg/jpeg_stream_wrappers.h"
#include "jpeg/jpeg_codec.h"
#include "jpeg_crypto.h"
#include "sha512.h"
#include "rand.h"

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

bool chaos_sorter(struct chaos_dc left, struct chaos_dc right) {
  if (left.chaos == right.chaos) {
    return left.chaos_pos < right.chaos_pos;
  }
  return left.chaos < right.chaos;
}

bool chaos_gmp_sorter(struct chaos_dc left, struct chaos_dc right) {
  int cmp_result = mpf_cmp(left.chaos_gmp, right.chaos_gmp);
  if (cmp_result == 0) {
    return left.chaos_pos < right.chaos_pos;
  }
  return cmp_result < 0;
}

bool chaos_pos_sorter(struct chaos_dc left, struct chaos_dc right) {
  return left.chaos_pos < right.chaos_pos;
}

/*
 * x_0 and mu are the secret values.
 * n is the length of chaotic_seq.
 */
void generateChaoticSequence(
    struct chaos_dc *chaotic_seq,
    int n,
    float x_0,
    float mu) {

  chaotic_seq[0].chaos = mu * x_0 * (1 - x_0);
  chaotic_seq[0].chaos_pos = 0;

  LOGD("generateChaoticSequence chaotic_seq[0].chaos: %f", chaotic_seq[0].chaos);

  for (int i = 1; i < n; i++) {
    float x_n = chaotic_seq[i - 1].chaos;
    chaotic_seq[i].chaos = mu * x_n * (1 - x_n);
    chaotic_seq[i].chaos_pos = i;
    //LOGD("Generated chaotic_seq value: %f", chaotic_seq[i]);
  }

  // Order the sequence in ascending order based on the chaotic value
  // Each value's original position is maintained via the chaotic_pos member
  std::sort(chaotic_seq, chaotic_seq + n, &chaos_sorter);
}

static void next_logistic_map_val(mpf_t output, mpf_t x_n, mpf_t mu) {
  mpf_t subtraction_part;
  mpf_t multiplication_part;

  mpf_init(subtraction_part);
  mpf_init(multiplication_part);

  mpf_ui_sub(subtraction_part, 1, x_n);
  mpf_mul(multiplication_part, mu, x_n);

  //Performs: output = mu * x_n * (1 - x_n);
  mpf_mul(output, multiplication_part, subtraction_part);

  mpf_clears(subtraction_part, multiplication_part, NULL);
}

static bool should_flip_sign(mpf_t chaos_gmp) {
  mp_exp_t exponent;
  char *mpf_val;
  std::string chaos_hash_str;
  int set_bit_count = 0;

  mpf_val = mpf_get_str(NULL, &exponent, 10, 500, chaos_gmp);
  chaos_hash_str = sw::sha512::calculate(mpf_val);

  for (int i = 0; i < 32; i++) {
    set_bit_count += std::bitset<8>(chaos_hash_str[i]).count();
  }

  free(mpf_val);

  return set_bit_count % 2;
}

static void generate_sign_flips(mpf_t x_0, mpf_t mu, bool *sign_flips, int n) {
  mp_exp_t exponent;
  char *mpf_val_x_0;
  char *mpf_val_mu;
  std::string concat_hashes;
  int char_count = 0;

  mpf_val_x_0 = mpf_get_str(NULL, &exponent, 10, 500, x_0);
  mpf_val_mu = mpf_get_str(NULL, &exponent, 10, 500, mu);
  concat_hashes.append(sw::sha512::calculate(mpf_val_x_0));
  concat_hashes.append(sw::sha512::calculate(mpf_val_mu));
  char_count = concat_hashes.size();

  free(mpf_val_x_0);
  free(mpf_val_mu);

  while (concat_hashes.size() < n) {
    concat_hashes.append(sw::sha512::calculate(concat_hashes));
  }

  for (int i = 0; i < n; i++) {
    sign_flips[i] = std::bitset<8>(concat_hashes[i]).count() % 2;
  }
}

void gen_chaotic_sequence(
    struct chaos_dc *chaotic_seq,
    int n,
    mpf_t x_0,
    mpf_t mu,
    bool sort) {

  bool *sign_flips;

  sign_flips = (bool *) malloc(n * sizeof(bool));
  if (sign_flips == NULL) {
    LOGE("gen_chaotic_sequence failed to allocate sign_flips");
    return;
  }
  generate_sign_flips(x_0, mu, sign_flips, n);

  mpf_init(chaotic_seq[0].chaos_gmp);

  // chaotic_seq[0].chaos_gmp = mu * x_0 * (1 - x_0);
  next_logistic_map_val(chaotic_seq[0].chaos_gmp, x_0, mu);
  chaotic_seq[0].chaos_pos = 0;

  chaotic_seq[0].flip_sign = sign_flips[0];

  for (int i = 1; i < n; i++) {
    // x_n = chaotic_seq[i - 1].chaos_gmp
    mpf_init(chaotic_seq[i].chaos_gmp);
    next_logistic_map_val(chaotic_seq[i].chaos_gmp, chaotic_seq[i - 1].chaos_gmp, mu);

    chaotic_seq[i].chaos_pos = i;

    chaotic_seq[i].flip_sign = sign_flips[i];
  }

  // Order the sequence in ascending order based on the chaotic value
  // Each value's original position is maintained via the chaotic_pos member
  if (sort)
    std::sort(chaotic_seq, chaotic_seq + n, &chaos_gmp_sorter);
  free(sign_flips);
}

void gen_chaotic_sequence(
    struct chaos_dc *chaotic_seq,
    int n,
    mpf_t x_0,
    mpf_t mu) {
  gen_chaotic_sequence(chaotic_seq, n, x_0, mu, true);
}

static void populate_row(struct chaos_dc *chaotic_seq_row, int y, int width, float prev_row_last_val, float mu) {
  for (int j = 0; j < width; j++) {
    float x_n;
    if (y == 0 && j == 0) continue;

    if (j == 0)
      x_n = prev_row_last_val;
    else
      x_n = chaotic_seq_row[j - 1].chaos;

    chaotic_seq_row[j].chaos = mu * x_n * (1 - x_n);
    chaotic_seq_row[j].chaos_pos = j;
  }
}

void gen_chaotic_per_row(
    struct chaos_dc *chaotic_seq,
    int width,
    int height,
    float x_0,
    float mu) {
  float prev_val = 0.0;

  chaotic_seq[0].chaos = mu * x_0 * (1 - x_0);
  chaotic_seq[0].chaos_pos = 0;

  for (int y = 0; y < height; y++) {
    populate_row(&chaotic_seq[y * width], y, width, prev_val, mu);
    prev_val = chaotic_seq[y * width + width - 1].chaos;

    // Order the sequence in ascending order based on the chaotic value
    // Each value's original position is maintained via the chaotic_pos member
    std::sort(&chaotic_seq[y * width], &chaotic_seq[y * width] + width, &chaos_sorter);
  }
}

static void populate_row(struct chaos_dc *chaotic_seq_row, int y, int width, mpf_t prev_row_last_val, mpf_t mu) {
  for (int j = 0; j < width; j++) {
    if (y == 0 && j == 0) continue;

    mpf_init(chaotic_seq_row[j].chaos_gmp);

    //chaotic_seq_row[j].chaos = mu * x_n * (1 - x_n);
    if (j == 0)
      next_logistic_map_val(chaotic_seq_row[j].chaos_gmp, prev_row_last_val, mu);
    else
      next_logistic_map_val(chaotic_seq_row[j].chaos_gmp, chaotic_seq_row[j - 1].chaos_gmp, mu);

    chaotic_seq_row[j].chaos_pos = j;
  }
}

void gen_chaotic_per_row(
    struct chaos_dc *chaotic_seq,
    int width,
    int height,
    mpf_t x_0,
    mpf_t mu) {
  mpf_t prev_val;

  mpf_init(prev_val);

  mpf_init(chaotic_seq[0].chaos_gmp);

  //Performs: chaotic_seq[0].chaos_gmp = mu * x_0 * (1 - x_0);
  next_logistic_map_val(chaotic_seq[0].chaos_gmp, x_0, mu);

  chaotic_seq[0].chaos_pos = 0;

  for (int y = 0; y < height; y++) {
    populate_row(&chaotic_seq[y * width], y, width, prev_val, mu);
    mpf_set(prev_val, chaotic_seq[y * width + width - 1].chaos_gmp);

    // Order the sequence in ascending order based on the chaotic value
    // Each value's original position is maintained via the chaotic_pos member
    std::sort(&chaotic_seq[y * width], &chaotic_seq[y * width] + width, &chaos_gmp_sorter);
  }

  mpf_clear(prev_val);
}

void diffuseACs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu,
    mpf_t alpha,
    mpf_t beta,
    bool encrypt) {

  mpf_t dc_coeff;
  mpf_t alpha_part;
  mpf_t dc_alpha_part;
  mpf_t beta_part;
  mpf_t xor_component_mpf;

  mpf_inits(dc_coeff, alpha_part, dc_alpha_part, beta_part, xor_component_mpf, NULL);

  LOGD("diffuseACs alpha=%lf, beta=%lf", mpf_get_d(alpha), mpf_get_d(beta));

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    unsigned int ith_mcu = 0;
    mpf_t last_xn;
    struct chaos_dc *chaotic_seq;
    unsigned int n_coefficients = comp_info->width_in_blocks * DCTSIZE2;

    chaotic_seq = (struct chaos_dc *) malloc(n_coefficients * sizeof(struct chaos_dc));
    if (chaotic_seq == NULL) {
      LOGE("diffuseACs failed to alloc memory for chaotic_seq");
      return;
    }

    mpf_init(last_xn);

    LOGD("diffuseACs iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

    for (int y = 0; y < comp_info->height_in_blocks; y++) {
      JBLOCKARRAY mcu_buff; // Pointer to list of horizontal 8x8 blocks

      if (ith_mcu)
        gen_chaotic_sequence(chaotic_seq, n_coefficients, last_xn, mu, false);
      else
        gen_chaotic_sequence(chaotic_seq, n_coefficients, x_0, mu, false);

      // mcu_buff[y][x][c]
      // - the cth coefficient
      // - the xth horizontal block
      // - the yth vertical block
      mcu_buff = (dinfo->mem->access_virt_barray)((j_common_ptr)dinfo, src_coefs[comp_i], y, (JDIMENSION) 1, TRUE);

      for (int x = 0; x < comp_info->width_in_blocks; x++) {
        JCOEFPTR mcu_ptr; // Pointer to 8x8 block of coefficients (I think)

        mcu_ptr = mcu_buff[0][x];

        for (int i = 1; i < DCTSIZE2; i++) {
          JCOEF xor_component;
          JCOEF new_ac_coeff;
          int mod_amt = 100;

          if (mcu_ptr[i] == 0)
            continue;

          // DC * alpha * chaos[i-1] + beta * chaos[i-1]
          mpf_set_d(dc_coeff, mcu_ptr[0]);

          mpf_mul(alpha_part, alpha, chaotic_seq[(i - 1) * x].chaos_gmp);

          // The two multiplied parts
          mpf_mul(dc_alpha_part, dc_coeff, alpha_part);
          mpf_mul(beta_part, beta, chaotic_seq[(i - 1) * x].chaos_gmp);

          mpf_add(xor_component_mpf, dc_alpha_part, beta_part);

          xor_component = mpf_get_d(xor_component_mpf);

          new_ac_coeff = mcu_ptr[i] ^ (xor_component % mod_amt);

          mcu_ptr[i] = new_ac_coeff;
        }

        ith_mcu++;
      }

      mpf_set(last_xn, chaotic_seq[n_coefficients - 1].chaos_gmp);
      for (int i; i < n_coefficients; i++) {
        mpf_clear(chaotic_seq[i].chaos_gmp);
      }
    }

    free(chaotic_seq);
    mpf_clear(last_xn);
  }

  mpf_clears(dc_coeff, alpha_part, dc_alpha_part, beta_part, xor_component_mpf, NULL);
}

std::string compute_isaac_seed(unsigned long main_seed, unsigned long other_seed1, unsigned long other_seed2) {
  std::string concat_hashes_r;
  std::string concat_hashes_r2;

  // 256 bytes
  concat_hashes_r.append(sw::sha512::calculate(std::to_string(main_seed)));
  concat_hashes_r2.append(sw::sha512::calculate(std::to_string(other_seed1)));
  concat_hashes_r2.append(sw::sha512::calculate(std::to_string(other_seed2)));
  concat_hashes_r.append(sw::sha512::calculate(concat_hashes_r2)); // H_0 (128 bytes)
  concat_hashes_r.append(sw::sha512::calculate(concat_hashes_r)); // 192 bytes
  concat_hashes_r.append(sw::sha512::calculate(concat_hashes_r)); // 256 bytes

  return concat_hashes_r;
}

void diffuseACsFlipSigns(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu,
    mpf_t alpha,
    mpf_t beta) {

  char *mpf_val_x_0;
  char *mpf_val_mu;
  mp_exp_t exponent;
  std::string concat_hashes;
  unsigned int isaac_i = 0;

  mpf_val_x_0 = mpf_get_str(NULL, &exponent, 10, 500, x_0);
  mpf_val_mu = mpf_get_str(NULL, &exponent, 10, 500, mu);

  LOGD("diffuseACsFlipSigns x_0=%s, mu=%s", mpf_val_x_0, mpf_val_mu);
  // 256 bytes
  concat_hashes.append(sw::sha512::calculate(mpf_val_x_0));
  concat_hashes.append(sw::sha512::calculate(mpf_val_mu));
  concat_hashes.append(sw::sha512::calculate(concat_hashes));
  concat_hashes.append(sw::sha512::calculate(concat_hashes));

  free(mpf_val_x_0);
  free(mpf_val_mu);

  LOGD("diffuseACsFlipSigns alpha=%lf, beta=%lf", mpf_get_d(alpha), mpf_get_d(beta));

  for (int comp_i = 0; comp_i < dinfo->num_components; comp_i++) {
    jpeg_component_info *comp_info = dinfo->comp_info + comp_i;
    unsigned int width = comp_info->width_in_blocks;
    unsigned int height = comp_info->height_in_blocks;
    unsigned int n_coefficients = comp_info->width_in_blocks * DCTSIZE2;

    unsigned int non_zero_ac_count = 0;
    unsigned int ac_flips = 0;
    randctx ctx;

    // Initialize ISAAC seed
    ctx.randa = ctx.randb = ctx.randc = (ub4) 0;
    for (int i = 0; i < RANDSIZ; i++) {
      ctx.randrsl[i] = concat_hashes[i];
    }
    randinit(&ctx, 1);

    LOGD("diffuseACsFlipSigns iterating over image component %d (comp_info->height_in_blocks=%d)", comp_i, comp_info->height_in_blocks);

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

        if (isaac_i % 2048 == 0) {
          isaac(&ctx);
        }

        for (int i = 1; i < DCTSIZE2; i++) {
          isaac_i++;
          if (mcu_ptr[i] == 0)
           continue;

          //LOGD("diffuseACsFlipSigns %d / %d", isaac_i % 256, isaac_i % 8);
          if (std::bitset<8>(ctx.randrsl[isaac_i % 256]).test(isaac_i % 8)) {
            mcu_ptr[i] *= -1;
            ac_flips++;
          }

          non_zero_ac_count++;
        }
      }
    }

    LOGD("diffuseACsFlipSigns non_zero_ac_count=%u, ac_flips=%u", non_zero_ac_count, ac_flips);
  }
}

float scaleToRange(float input, float input_min, float input_max, float scale_min, float scale_max) {
  return (scale_max - scale_min) * (input - input_min) / (input_max - input_min) + scale_min;
}

void construct_alpha_beta(mpf_t output, const char *input, int input_len) {
  char *output_str;
  int preset_parts_len = 5;
  int total_len = preset_parts_len + input_len + 1;

  output_str = (char *) malloc(total_len * sizeof(char));
  if (output_str == NULL) {
    LOGE("construct_alpha_beta() Failed to allocate output_str");
    return;
  }

  output_str[total_len - 1] = '\0';
  output_str[0] = '3';
  output_str[1] = '.';
  output_str[2] = '9';

  output_str[total_len - 3] = 'e';
  output_str[total_len - 2] = '0';

  strncpy(&output_str[3], input, input_len);

  LOGD("construct_alpha_beta %s", output_str);

  mpf_set_str(output, output_str, 10);

  free(output_str);
}

int sameSign(JCOEF a, JCOEF b) {
  return (a < 0 && b < 0) || (a >= 0 && b >= 0);
}

} } } }
