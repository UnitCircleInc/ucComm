/// @copyright © 2014 Melissa O'Neill <oneill@pcg-random.org>
///
/// Portions © 2023 Unit Circle Inc.
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Based on http://www.pcg-random.org
///

#include <stdint.h>
#include "pcg32.h"

uint32_t pcg32_random_r(pcg32_random_t* rng) {
  uint64_t oldstate = rng->state;
  rng->state = oldstate * 6364136223846793005ULL + rng->inc;
  uint32_t xorshifted = (uint32_t) (((oldstate >> 18u) ^ oldstate) >> 27u);
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq) {
  rng->state = 0U;
  rng->inc = (initseq << 1u) | 1u;
  pcg32_random_r(rng);
  rng->state += initstate;
  pcg32_random_r(rng);
}

uint32_t pcg32_boundedrand_r(pcg32_random_t* rng, uint32_t bound) {
  uint32_t threshold = -bound % bound;
  for (;;) {
    uint32_t r = pcg32_random_r(rng);
    if (r >= threshold)
      return r % bound;
  }
}

void pcg32_randbytes(pcg32_random_t* rng, size_t n, uint8_t b[n]) {
  while (n-- > 0) {
    *b++ = pcg32_boundedrand_r(rng, 256);
  }
}
