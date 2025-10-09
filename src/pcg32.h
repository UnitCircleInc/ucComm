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
/// @file
///

#pragma once

#include <stdint.h>
#include <stddef.h>

/// @brief PCG state structure
///
/// @note
/// Internals are *Private*.
///
/// @note
/// `inc` field must be odd - this is enforced by pcf32_srandom_r
///
typedef struct pcg_state_setseq_64 {
  uint64_t state;  ///< RNG state.  All values are possible.
  uint64_t inc;    ///< Controls which RNG sequence (stream) is selected.
} pcg32_random_t;

/// @brief Useful stactic initializer
///
/// Use `static pcg32_random_t rng = PCG32_INITIALIZER;`
///
#define PCG32_INITIALIZER { 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL }

/// @brief Generate random value in [0, 2^32)
///
///@param[in, out] rng rng state to use
///@returns random value in [0, 2^32)
///
uint32_t pcg32_random_r(pcg32_random_t* rng);

/// @brief Initialized random number generator.
///
/// @param[out] rng rng state to initialize
/// @param[in]  initstate part 1 of the state initalizer
/// @param[in]  initseq part 2 of the state initializer
///
void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq);

/// @brief Generate random value in [0, bound)
///
///@param[in, out] rng rng state to use
///@param[in] bound the upper bound of values to generate
///@returns random value in [0, bound)
///
uint32_t pcg32_boundedrand_r(pcg32_random_t* rng, uint32_t bound);

/// @ brief Generate a sequence of random bytes
///
///@param[in, out] rng rng state to use
///@param[in] n number of bytes to output in b
///@param[out] b place to store randomly genereated bytes
///
void pcg32_randbytes(pcg32_random_t* rng, size_t n, uint8_t b[n]);
