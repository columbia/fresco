#ifndef FRESCO_JPEG_CRYPTO_H
#define FRESCO_JPEG_CRYPTO_H

#include <gmp.h>

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

const float SCALE_MIN_X = 0.0;
const float SCALE_MAX_X = 1.0;
const float SCALE_MIN_MU = 3.57;
const float SCALE_MAX_MU = 4.0;

struct chaos_dc {
  float chaos;
  unsigned int chaos_pos;
  mpf_t chaos_gmp;
  bool flip_sign;

  JCOEF dc;
  unsigned int block_pos;
};

bool chaos_sorter(struct chaos_dc left, struct chaos_dc right);

bool chaos_pos_sorter(struct chaos_dc left, struct chaos_dc right);

bool chaos_gmp_sorter(struct chaos_dc left, struct chaos_dc right);

void generateChaoticSequence(
    struct chaos_dc *chaotic_seq,
    int n,
    float x_0,
    float mu);

void gen_chaotic_sequence(
    struct chaos_dc *chaotic_seq,
    int n,
    mpf_t x_0,
    mpf_t mu);

void gen_chaotic_sequence(
    struct chaos_dc *chaotic_seq,
    int n,
    mpf_t x_0,
    mpf_t mu,
    bool sort);

void gen_chaotic_per_row(
    struct chaos_dc *chaotic_seq,
    int width,
    int height,
    float x_0,
    float mu);

void gen_chaotic_per_row(
    struct chaos_dc *chaotic_seq,
    int width,
    int height,
    mpf_t x_0,
    mpf_t mu);

void diffuseACs(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu,
    mpf_t alpha,
    mpf_t beta,
    bool encrypt);

void diffuseACsFlipSigns(
    j_decompress_ptr dinfo,
    jvirt_barray_ptr* src_coefs,
    mpf_t x_0,
    mpf_t mu,
    mpf_t alpha,
    mpf_t beta);

float scaleToRange(float input, float input_min, float input_max, float scale_min, float scale_max);
void construct_alpha_beta(mpf_t output, const char *input, int input_len);

int sameSign(JCOEF a, JCOEF b);

} } } }

#endif //FRESCO_JPEG_CRYPTO_H
