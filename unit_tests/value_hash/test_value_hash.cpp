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
 * test_value_hash.cpp - mht_valhash () hashes the values of one number alike, whatever their type
 *
 * - SMALLINT, INTEGER, BIGINT and NUMERIC (fixed or float, of any precision and scale) of one value hash alike
 * - a FLOAT and a DOUBLE of one value hash alike, and -0.0 as 0.0
 */

#include "catch2/catch.hpp"

#include "dbtype.h"		/* db_make_*: the values hashed */
#include "memory_hash.h"	/* mht_valhash */
#include "numeric_opfunc.h"	/* numeric_coerce_dec_str_to_num: a NUMERIC from its digits */

#include <climits>
#include <cstdint>
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

  /* a NUMERIC of the digits (without sign or point), scale of them after the point */
  DB_VALUE
  make_numeric (const std::string &digits, int precision, int scale, bool negative, bool is_float)
  {
    DB_VALUE value;
    unsigned char num[DB_NUMERIC_BUF_SIZE];
    bool spelt_negative = false;

    numeric_coerce_dec_str_to_num (digits.c_str (), num, &spelt_negative);
    REQUIRE (db_make_numeric (&value, num, precision, scale, DB_NUMERIC_BUF_SIZE, negative, is_float) == NO_ERROR);
    return value;
  }

  /* the NUMERICs of an integer: of its own digits, NUMERIC(38,0), float, and with two zeros after the point */
  void
  check_integer_family (int64_t integer, unsigned int expected)
  {
    const bool negative = integer < 0;
    const uint64_t magnitude = negative ? 0 - (uint64_t) integer : (uint64_t) integer;
    const std::string digits = std::to_string (magnitude);
    const int precision = (int) digits.size ();

    INFO ("integer " << integer);
    CHECK (hash_of (make_numeric (digits, precision, 0, negative, false)) == expected);
    CHECK (hash_of (make_numeric (digits, 38, 0, negative, true)) == expected);
    CHECK (hash_of (make_numeric (digits + "00", precision + 2, 2, negative, false)) == expected);
    CHECK (hash_of (make_numeric (digits + "00", precision + 2, 2, negative, true)) == expected);
  }
}

TEST_CASE ("SMALLINT, INTEGER, BIGINT and NUMERIC of one value hash alike", "[value_hash]")
{
  DB_VALUE value;

  const int64_t smallints[] = { 0, 1, -1, 5, -5, SHRT_MAX, SHRT_MIN };

  for (int64_t integer : smallints)
    {
      db_make_bigint (&value, integer);
      const unsigned int expected = hash_of (value);

      db_make_short (&value, (short) integer);
      CHECK (hash_of (value) == expected);
      db_make_int (&value, (int) integer);
      CHECK (hash_of (value) == expected);
      check_integer_family (integer, expected);
    }

  const int64_t int_limits[] = { (int64_t) INT_MAX, (int64_t) INT_MIN };

  for (int64_t integer : int_limits)
    {
      db_make_bigint (&value, integer);
      const unsigned int expected = hash_of (value);

      db_make_int (&value, (int) integer);
      CHECK (hash_of (value) == expected);
      check_integer_family (integer, expected);
    }

  /* past 2^61 - 1, where the value is reduced mod the prime */
  const int64_t past_prime[] = { (int64_t) ((1ULL << 61) - 1), (int64_t) (1ULL << 61), INT64_MAX, INT64_MIN };

  for (int64_t integer : past_prime)
    {
      db_make_bigint (&value, integer);
      check_integer_family (integer, hash_of (value));
    }
}

TEST_CASE ("NUMERIC of one value hashes alike whatever its scale", "[value_hash]")
{
  const unsigned int expected = hash_of (make_numeric ("15", 2, 1, false, false));

  CHECK (hash_of (make_numeric ("150", 3, 2, false, false)) == expected);
  CHECK (hash_of (make_numeric ("1500", 20, 3, false, false)) == expected);
  CHECK (hash_of (make_numeric ("150", 38, 2, false, true)) == expected);
  CHECK (hash_of (make_numeric ("15", 2, 1, true, false)) != expected);

  /* a negative scale: 15 x 10^5, fixed (NUMERIC(38,-5)) and float */
  const unsigned int scaled = hash_of (make_numeric ("1500000", 7, 0, false, false));

  CHECK (hash_of (make_numeric ("15", 38, -5, false, false)) == scaled);
  CHECK (hash_of (make_numeric ("15", 38, -5, false, true)) == scaled);
}

TEST_CASE ("FLOAT and DOUBLE of one value hash alike", "[value_hash]")
{
  DB_VALUE value;

  const double floats[] = { 0.0, 1.0, -1.5, 0.25, 1048576.0 };

  for (double number : floats)
    {
      db_make_double (&value, number);
      const unsigned int expected = hash_of (value);

      db_make_float (&value, (float) number);
      CHECK (hash_of (value) == expected);
    }

  db_make_double (&value, 0.0);
  const unsigned int zero = hash_of (value);
  db_make_double (&value, -0.0);
  CHECK (hash_of (value) == zero);
  db_make_float (&value, -0.0f);
  CHECK (hash_of (value) == zero);
}

TEST_CASE ("distinct integers hash apart", "[value_hash]")
{
  DB_VALUE value;
  std::set<unsigned int> hashes;

  for (int integer = -500; integer < 500; integer++)
    {
      db_make_int (&value, integer);
      hashes.insert (hash_of (value));
    }
  CHECK (hashes.size () == 1000);
}
