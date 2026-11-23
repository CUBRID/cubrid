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
 * test_value_hash.cpp - mht_valhash () hashes the numbers that compare equal alike within a family (exact, floating)
 *
 * - SMALLINT, INTEGER, BIGINT and NUMERIC (fixed or float, of any precision and scale) of one value hash alike
 * - a FLOAT and a DOUBLE of one value hash alike, and -0.0 as 0.0
 * - every NaN hashes alike, as a NaN equals every other NaN (MR_CMP_FLOATING ())
 * - distinct integers hash apart
 */

#include "catch2/catch.hpp"

#include "dbtype.h"		/* db_make_*: the values hashed */
#include "memory_hash.h"	/* mht_valhash */
#include "numeric_opfunc.h"	/* numeric_coerce_dec_str_to_num: a NUMERIC from its digits */

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>

namespace
{
  /* the hash of a value: the whole hash, as UINT_MAX asks for */
  unsigned int
  hash_of (const DB_VALUE &value)
  {
    return mht_valhash (&value, UINT_MAX);
  }

  unsigned int
  hash_short (short number)
  {
    DB_VALUE value;
    db_make_short (&value, number);
    return hash_of (value);
  }

  unsigned int
  hash_int (int number)
  {
    DB_VALUE value;
    db_make_int (&value, number);
    return hash_of (value);
  }

  unsigned int
  hash_bigint (int64_t number)
  {
    DB_VALUE value;
    db_make_bigint (&value, number);
    return hash_of (value);
  }

  unsigned int
  hash_float (float number)
  {
    DB_VALUE value;
    db_make_float (&value, number);
    return hash_of (value);
  }

  unsigned int
  hash_double (double number)
  {
    DB_VALUE value;
    db_make_double (&value, number);
    return hash_of (value);
  }

  unsigned int
  hash_monetary (double amount)
  {
    DB_VALUE value;
    db_make_monetary (&value, DB_CURRENCY_DOLLAR, amount);
    return hash_of (value);
  }

  /* a NUMERIC of the digits (a leading '-' for a negative one, no point), the last scale of them after the point */
  unsigned int
  hash_numeric_of (const std::string &digits, int precision, int scale, bool is_float)
  {
    const bool negative = (digits[0] == '-');
    DB_VALUE value;
    unsigned char num[DB_NUMERIC_BUF_SIZE];
    bool spelt_negative = false;

    numeric_coerce_dec_str_to_num (digits.c_str () + (negative ? 1 : 0), num, &spelt_negative);
    REQUIRE (db_make_numeric (&value, num, precision, scale, DB_NUMERIC_BUF_SIZE, negative, is_float) == NO_ERROR);
    return hash_of (value);
  }

  unsigned int
  hash_numeric (const std::string &digits, int precision, int scale)
  {
    return hash_numeric_of (digits, precision, scale, false);
  }

  unsigned int
  hash_float_numeric (const std::string &digits, int precision, int scale)
  {
    return hash_numeric_of (digits, precision, scale, true);
  }

  /* the NUMERICs of an integer: fixed of its own precision, float NUMERIC (38, 0), and fixed and float with two zeros
   * after the point */
  void
  check_numerics_of (int64_t integer, unsigned int expected)
  {
    const std::string digits = std::to_string (integer);
    const int precision = (int) digits.size () - (integer < 0 ? 1 : 0);

    CHECK (hash_numeric (digits, precision, 0) == expected);
    CHECK (hash_float_numeric (digits, 38, 0) == expected);
    CHECK (hash_numeric (digits + "00", precision + 2, 2) == expected);
    CHECK (hash_float_numeric (digits + "00", precision + 2, 2) == expected);
  }

  /* the DOUBLE of these bits */
  double
  double_of_bits (uint64_t bits)
  {
    double number;
    memcpy (&number, &bits, sizeof (number));
    return number;
  }
}

TEST_CASE ("SMALLINT, INTEGER, BIGINT and NUMERIC of one value hash alike", "[value_hash]")
{
  const int64_t smallints[] = { 0, 1, -1, 5, -5, SHRT_MAX, SHRT_MIN };

  for (int64_t integer : smallints)
    {
      const unsigned int expected = hash_bigint (integer);

      INFO ("integer " << integer);
      CHECK (hash_short ((short) integer) == expected);
      CHECK (hash_int ((int) integer) == expected);
      check_numerics_of (integer, expected);
    }

  const int64_t int_limits[] = { INT_MAX, INT_MIN };

  for (int64_t integer : int_limits)
    {
      const unsigned int expected = hash_bigint (integer);

      INFO ("integer " << integer);
      CHECK (hash_int ((int) integer) == expected);
      check_numerics_of (integer, expected);
    }

  /* from 2^61 - 1 on in magnitude, where the value is reduced mod the prime */
  const int64_t past_prime[] = { (int64_t) ((1ULL << 61) - 1), (int64_t) (1ULL << 61), INT64_MAX, INT64_MIN };

  for (int64_t integer : past_prime)
    {
      INFO ("integer " << integer);
      check_numerics_of (integer, hash_bigint (integer));
    }
}

TEST_CASE ("NUMERIC of one value hashes alike whatever its scale", "[value_hash]")
{
  const unsigned int one_and_half = hash_numeric ("15", 2, 1);	/* 1.5 */

  CHECK (hash_numeric ("150", 3, 2) == one_and_half);	/* 1.50 */
  CHECK (hash_numeric ("1500", 20, 3) == one_and_half);	/* 1.500 of a wider precision */
  CHECK (hash_float_numeric ("150", 38, 2) == one_and_half);	/* 1.50 of a float NUMERIC */
  CHECK (hash_numeric ("-15", 2, 1) != one_and_half);	/* -1.5: the sign counts */

  /* a negative scale: 15 x 10^5 */
  const unsigned int scaled = hash_numeric ("1500000", 7, 0);

  CHECK (hash_numeric ("15", 38, -5) == scaled);
  CHECK (hash_float_numeric ("15", 38, -5) == scaled);
}

TEST_CASE ("FLOAT and DOUBLE of one value hash alike", "[value_hash]")
{
  const double numbers[] = { 0.0, 1.0, -1.5, 0.25, 1048576.0 };

  for (double number : numbers)
    {
      INFO ("number " << number);
      CHECK (hash_float ((float) number) == hash_double (number));
    }

  const unsigned int zero = hash_double (0.0);

  CHECK (hash_double (-0.0) == zero);	/* -0.0 differs from 0.0 only in its sign bit */
  CHECK (hash_float (-0.0f) == zero);
  CHECK (hash_monetary (-0.0) == zero);
}

TEST_CASE ("every NaN hashes alike", "[value_hash]")
{
  const double nan_of_payload = double_of_bits (0x7ff0000000000001ULL);	/* a NaN of another fraction */

  REQUIRE (std::isnan (nan_of_payload));

  const unsigned int nan_hash = hash_double (NAN);

  CHECK (hash_double (-NAN) == nan_hash);	/* the sign bit set */
  CHECK (hash_double (nan_of_payload) == nan_hash);	/* other fraction bits */
  CHECK (hash_float (NAN) == nan_hash);	/* a FLOAT NaN, widened to the DOUBLE NaN */
  CHECK (hash_float (-NAN) == nan_hash);
  CHECK (hash_monetary (-NAN) == nan_hash);	/* a MONETARY hashes its amount as a DOUBLE */
  CHECK (hash_double (INFINITY) != nan_hash);	/* an infinity is not a NaN */
}

TEST_CASE ("distinct integers hash apart", "[value_hash]")
{
  std::set<unsigned int> hashes;

  for (int integer = -500; integer < 500; integer++)
    {
      hashes.insert (hash_int (integer));
    }
  CHECK (hashes.size () == 1000);
}
