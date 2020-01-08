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

  // 8 bits per character, if there are N MCUs, then we only need ceil(N / 8) characters
  while (char_count < (n / 8) + 1) {
    concat_hashes.append(sw::sha512::calculate(concat_hashes));
    char_count += concat_hashes.size();
  }

  for (int i = 0; i < n; i++) {
    //sign_flips[i] = std::bitset<8>(concat_hashes[i]).count() % 2;
    sign_flips[i] = i % 2;
  }
}

void gen_chaotic_sequence(
    struct chaos_dc *chaotic_seq,
    int n,
    mpf_t x_0,
    mpf_t mu) {

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
  std::sort(chaotic_seq, chaotic_seq + n, &chaos_gmp_sorter);
  free(sign_flips);
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

float scaleToRange(float input, float input_min, float input_max, float scale_min, float scale_max) {
  return (scale_max - scale_min) * (input - input_min) / (input_max - input_min) + scale_min;
}

int sameSign(JCOEF a, JCOEF b) {
  return (a < 0 && b < 0) || (a >= 0 && b >= 0);
}

} } } }
