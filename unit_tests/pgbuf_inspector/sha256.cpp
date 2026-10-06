/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * sha256.cpp - SHA-256 for the conformance corpus checksum file
 */

#include "sha256.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
  constexpr std::uint32_t ROUND_CONSTANTS[64] =
  {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  };

  inline std::uint32_t
  rotate_right (std::uint32_t value, unsigned bits)
  {
    return (value >> bits) | (value << (32 - bits));
  }

  void
  compress_block (std::uint32_t state[8], const unsigned char block[64])
  {
    std::uint32_t schedule[64];
    for (int i = 0; i < 16; ++i)
      {
	schedule[i] = (static_cast<std::uint32_t> (block[4 * i]) << 24)
		      | (static_cast<std::uint32_t> (block[4 * i + 1]) << 16)
		      | (static_cast<std::uint32_t> (block[4 * i + 2]) << 8)
		      | static_cast<std::uint32_t> (block[4 * i + 3]);
      }
    for (int i = 16; i < 64; ++i)
      {
	const std::uint32_t s0 = rotate_right (schedule[i - 15], 7) ^ rotate_right (schedule[i - 15], 18)
				 ^ (schedule[i - 15] >> 3);
	const std::uint32_t s1 = rotate_right (schedule[i - 2], 17) ^ rotate_right (schedule[i - 2], 19)
				 ^ (schedule[i - 2] >> 10);
	schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
      }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i)
      {
	const std::uint32_t big_s1 = rotate_right (e, 6) ^ rotate_right (e, 11) ^ rotate_right (e, 25);
	const std::uint32_t choice = (e & f) ^ (~e & g);
	const std::uint32_t t1 = h + big_s1 + choice + ROUND_CONSTANTS[i] + schedule[i];
	const std::uint32_t big_s0 = rotate_right (a, 2) ^ rotate_right (a, 13) ^ rotate_right (a, 22);
	const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
	const std::uint32_t t2 = big_s0 + majority;
	h = g;
	g = f;
	f = e;
	e = d + t1;
	d = c;
	c = b;
	b = a;
	a = t1 + t2;
      }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }
}

std::string
corpus::sha256_hex (const std::string &data)
{
  std::uint32_t state[8] =
  {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };

  const unsigned char *bytes = reinterpret_cast<const unsigned char *> (data.data ());
  const std::size_t length = data.size ();
  std::size_t offset = 0;
  for (; offset + 64 <= length; offset += 64)
    {
      compress_block (state, bytes + offset);
    }

  /* Pad the remainder: 0x80, zeros, then the bit length big-endian in the last eight bytes. */
  unsigned char tail[128];
  std::size_t tail_length = length - offset;
  memcpy (tail, bytes + offset, tail_length);
  tail[tail_length++] = 0x80;
  const std::size_t padded_length = (tail_length <= 56) ? 64 : 128;
  memset (tail + tail_length, 0, padded_length - tail_length);
  const std::uint64_t bit_length = static_cast<std::uint64_t> (length) * 8;
  for (int i = 0; i < 8; ++i)
    {
      tail[padded_length - 1 - i] = static_cast<unsigned char> (bit_length >> (8 * i));
    }
  compress_block (state, tail);
  if (padded_length == 128)
    {
      compress_block (state, tail + 64);
    }

  char hex[65];
  for (int i = 0; i < 8; ++i)
    {
      snprintf (hex + 8 * i, 9, "%08x", state[i]);
    }
  return std::string (hex, 64);
}
