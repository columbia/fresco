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

float scaleToRange(float input, float input_min, float input_max, float scale_min, float scale_max) {
  return (scale_max - scale_min) * (input - input_min) / (input_max - input_min) + scale_min;
}

int sameSign(JCOEF a, JCOEF b) {
  return (a < 0 && b < 0) || (a >= 0 && b >= 0);
}

} } } }
