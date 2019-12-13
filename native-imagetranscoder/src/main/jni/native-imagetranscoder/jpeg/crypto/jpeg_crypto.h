#ifndef FRESCO_JPEG_CRYPTO_H
#define FRESCO_JPEG_CRYPTO_H

namespace facebook {
namespace imagepipeline {
namespace jpeg {
namespace crypto {

struct chaos_dc {
  float chaos;
  unsigned int chaos_pos;

  JCOEF dc;
  unsigned int block_pos;
};

bool chaos_sorter(struct chaos_dc left, struct chaos_dc right);

bool chaos_pos_sorter(struct chaos_dc left, struct chaos_dc right);

void generateChaoticSequence(
    struct chaos_dc *chaotic_seq,
    int n // The number of DC coefficients and also the length of chaotic_seq
    /*
    float x_0, // Secret value
    float m_u // Secret value
    */);

void generateChaoticSequence_WithInputs(
    struct chaos_dc *chaotic_seq,
    int n, // The number of DC coefficients and also the length of chaotic_seq
    float x_0,
    float mu
    );

float scaleToRange(float input, float input_min, float input_max, float scale_min, float scale_max);

int sameSign(JCOEF a, JCOEF b);

} } } }

#endif //FRESCO_JPEG_CRYPTO_H
