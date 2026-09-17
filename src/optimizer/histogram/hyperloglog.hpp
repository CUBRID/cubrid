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
 * hyperloglog.hpp - HyperLogLog distinct-value (NDV) estimator
 *
 *  Estimates the number of distinct values in a stream from a fixed-size sketch (Flajolet et al.
 *  2007), with linear counting for the small-cardinality range. Each value is hashed and only the
 *  position of the leftmost set bit per register is kept, so a value seen once and a value seen a
 *  million times update the sketch identically -- the estimate is frequency-blind and therefore
 *  immune to skew, unlike a uniform sample whose distinct count collapses on a long tail.
 *
 *  P = 14 registers (16 KiB, 1 byte each), standard error ~1.04 / sqrt(2^P) ~= 0.8%. Sketches from
 *  parallel workers combine by register-wise max (merge ()), which is exactly equivalent to having
 *  hashed every value into one sketch.
 */

#ifndef _HYPERLOGLOG_HPP_
#define _HYPERLOGLOG_HPP_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cubsampling
{
  class hyperloglog
  {
    public:
      static constexpr int P = 14;
      static constexpr std::size_t M = (std::size_t) 1 << P;	/* 16384 registers */

      hyperloglog ()
	: m_reg (M, 0)
      {
      }

      /* feed one already-hashed value (64-bit, well-mixed) */
      void add_hash (std::uint64_t h)
      {
	const std::size_t idx = (std::size_t) (h >> (64 - P));	/* top P bits select the register */
	const std::uint64_t w = h << P;				/* remaining bits, index shifted out */
	const std::uint8_t rank =
		(w == 0) ? (std::uint8_t) (64 - P + 1) : (std::uint8_t) (clz64 (w) + 1);
	if (rank > m_reg[idx])
	  {
	    m_reg[idx] = rank;
	  }
      }

      /* combine another sketch into this one (register-wise max) */
      void merge (const hyperloglog &other)
      {
	for (std::size_t i = 0; i < M; i++)
	  {
	    if (other.m_reg[i] > m_reg[i])
	      {
		m_reg[i] = other.m_reg[i];
	      }
	  }
      }

      /* estimated number of distinct add_hash () inputs */
      double estimate () const
      {
	const double m = (double) M;
	double sum = 0.0;
	std::size_t zeros = 0;
	for (std::size_t i = 0; i < M; i++)
	  {
	    sum += 1.0 / (double) ((std::uint64_t) 1 << m_reg[i]);
	    if (m_reg[i] == 0)
	      {
		zeros++;
	      }
	  }

	const double alpha = 0.7213 / (1.0 + 1.079 / m);
	double e = alpha * m * m / sum;

	/* small range: linear counting while many registers are still empty */
	if (e <= 2.5 * m && zeros > 0)
	  {
	    e = m * std::log (m / (double) zeros);
	  }
	/* 64-bit hash space => the 2^32 large-range correction is unnecessary */
	return e;
      }

      void clear ()
      {
	std::fill (m_reg.begin (), m_reg.end (), (std::uint8_t) 0);
      }

    private:
      static int clz64 (std::uint64_t x)	/* count leading zeros; x != 0 */
      {
#if defined(__GNUC__) || defined(__clang__)
	return __builtin_clzll (x);
#else
	int n = 0;
	while ((x & ((std::uint64_t) 1 << 63)) == 0)
	  {
	    n++;
	    x <<= 1;
	  }
	return n;
#endif
      }

      std::vector<std::uint8_t> m_reg;
  };

  /* 64-bit avalanche finalizer (murmur3 fmix64) -- spreads integer/double bits across the word */
  inline std::uint64_t
  hll_mix64 (std::uint64_t x)
  {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
  }

  /* 64-bit hash of a double's bit pattern */
  inline std::uint64_t
  hll_hash_double (double d)
  {
    std::uint64_t u;
    std::memcpy (&u, &d, sizeof (u));
    return hll_mix64 (u);
  }

  /* 64-bit hash of a byte string (FNV-1a, then avalanche-mixed for register distribution) */
  inline std::uint64_t
  hll_hash_bytes (const char *p, std::size_t len)
  {
    std::uint64_t h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < len; i++)
      {
	h ^= (std::uint64_t) (unsigned char) p[i];
	h *= 1099511628211ULL;
      }
    return hll_mix64 (h);
  }
}

#endif /* _HYPERLOGLOG_HPP_ */
