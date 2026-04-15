/*
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

#include "test_copy_binary_decoder.hpp"
#include "copy_binary_decoder.hpp"
#include "copy_binary_format.hpp"
#include "dbtype_function.h"

#include <arpa/inet.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

/* encoding helpers — inverse of the decoder */

static void
write_int16 (std::vector<char> &buf, int16_t v)
{
  uint16_t nv = htons ((uint16_t) v);
  const char *p = (const char *) &nv;
  buf.insert (buf.end (), p, p + sizeof (nv));
}

static void
write_int32 (std::vector<char> &buf, int32_t v)
{
  uint32_t nv = htonl ((uint32_t) v);
  const char *p = (const char *) &nv;
  buf.insert (buf.end (), p, p + sizeof (nv));
}

static void
write_int64 (std::vector<char> &buf, int64_t v)
{
  uint32_t hi = htonl ((uint32_t) (v >> 32));
  uint32_t lo = htonl ((uint32_t) v);
  const char *p;
  p = (const char *) &hi;
  buf.insert (buf.end (), p, p + sizeof (hi));
  p = (const char *) &lo;
  buf.insert (buf.end (), p, p + sizeof (lo));
}

static void
write_float (std::vector<char> &buf, float f)
{
  int32_t v;
  memcpy (&v, &f, sizeof (v));
  write_int32 (buf, v);
}

static void
write_double (std::vector<char> &buf, double d)
{
  int64_t v;
  memcpy (&v, &d, sizeof (v));
  write_int64 (buf, v);
}

static void
write_field_null (std::vector<char> &buf)
{
  write_int32 (buf, COPY_BINARY_NULL_FIELD_LEN);
}

static void
write_field_int (std::vector<char> &buf, int32_t val)
{
  write_int32 (buf, 4);		/* field_len */
  write_int32 (buf, val);
}

static void
write_field_bigint (std::vector<char> &buf, int64_t val)
{
  write_int32 (buf, 8);		/* field_len */
  write_int64 (buf, val);
}

static void
write_field_float (std::vector<char> &buf, float val)
{
  write_int32 (buf, 4);		/* field_len */
  write_float (buf, val);
}

static void
write_field_double (std::vector<char> &buf, double val)
{
  write_int32 (buf, 8);		/* field_len */
  write_double (buf, val);
}

static void
write_field_varchar (std::vector<char> &buf, const char *str, int len)
{
  write_int32 (buf, len);	/* field_len */
  buf.insert (buf.end (), str, str + len);
}

static void
write_row_header (std::vector<char> &buf, int16_t ncols)
{
  write_int16 (buf, ncols);
}

static void
write_footer (std::vector<char> &buf)
{
  write_int16 (buf, COPY_BINARY_FOOTER_SENTINEL);
}

#define ASSERT_INT_EQ(actual, expected, msg) \
  do { \
    if ((actual) != (expected)) { \
      std::cerr << "FAIL: " << (msg) << " expected=" << (expected) << " actual=" << (actual) << std::endl; \
      return 1; \
    } \
  } while (0)

#define ASSERT_TRUE(cond, msg) \
  do { \
    if (!(cond)) { \
      std::cerr << "FAIL: " << (msg) << std::endl; \
      return 1; \
    } \
  } while (0)

namespace test_copy_binary_decoder
{

  int
  test_decode_int (void)
  {
    std::cout << "test_decode_int" << std::endl;

    std::vector<char> buf;
    write_row_header (buf, 1);
    write_field_int (buf, 42);

    DB_TYPE types[] = { DB_TYPE_INTEGER };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");
    ASSERT_INT_EQ (consumed, (int) buf.size (), "bytes consumed");
    ASSERT_INT_EQ (db_get_int (&vals[0]), 42, "INT value");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_bigint (void)
  {
    std::cout << "test_decode_bigint" << std::endl;

    std::vector<char> buf;
    write_row_header (buf, 1);
    write_field_bigint (buf, 1234567890123LL);

    DB_TYPE types[] = { DB_TYPE_BIGINT };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");
    ASSERT_INT_EQ (db_get_bigint (&vals[0]), 1234567890123LL, "BIGINT value");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_float (void)
  {
    std::cout << "test_decode_float" << std::endl;

    std::vector<char> buf;
    write_row_header (buf, 1);
    write_field_float (buf, 3.14f);

    DB_TYPE types[] = { DB_TYPE_FLOAT };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");
    ASSERT_TRUE (fabsf (db_get_float (&vals[0]) - 3.14f) < 1e-6f, "FLOAT value");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_double (void)
  {
    std::cout << "test_decode_double" << std::endl;

    std::vector<char> buf;
    write_row_header (buf, 1);
    write_field_double (buf, 2.718281828);

    DB_TYPE types[] = { DB_TYPE_DOUBLE };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");
    ASSERT_TRUE (fabs (db_get_double (&vals[0]) - 2.718281828) < 1e-9, "DOUBLE value");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_varchar (void)
  {
    std::cout << "test_decode_varchar" << std::endl;

    const char *str = "hello";
    int len = 5;

    std::vector<char> buf;
    write_row_header (buf, 1);
    write_field_varchar (buf, str, len);

    DB_TYPE types[] = { DB_TYPE_VARCHAR };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");

    int result_len = 0;
    const char *result = db_get_string (&vals[0]);
    result_len = db_get_string_size (&vals[0]);

    ASSERT_INT_EQ (result_len, len, "VARCHAR length");
    ASSERT_TRUE (memcmp (result, str, len) == 0, "VARCHAR content");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_null (void)
  {
    std::cout << "test_decode_null" << std::endl;

    std::vector<char> buf;
    write_row_header (buf, 1);
    write_field_null (buf);

    DB_TYPE types[] = { DB_TYPE_INTEGER };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");
    ASSERT_TRUE (DB_IS_NULL (&vals[0]), "value is NULL");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_multi_column (void)
  {
    std::cout << "test_decode_multi_column" << std::endl;

    std::vector<char> buf;
    write_row_header (buf, 3);
    write_field_int (buf, 100);
    write_field_varchar (buf, "test", 4);
    write_field_double (buf, 9.99);

    DB_TYPE types[] = { DB_TYPE_INTEGER, DB_TYPE_VARCHAR, DB_TYPE_DOUBLE };
    DB_VALUE vals[3];
    for (int i = 0; i < 3; i++)
      {
	db_make_null (&vals[i]);
      }
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 3, vals, &consumed);
    ASSERT_INT_EQ (rc, 0, "decode_binary_row");
    ASSERT_INT_EQ (consumed, (int) buf.size (), "bytes consumed");
    ASSERT_INT_EQ (db_get_int (&vals[0]), 100, "INT column");

    const char *s = db_get_string (&vals[1]);
    int slen = db_get_string_size (&vals[1]);
    ASSERT_INT_EQ (slen, 4, "VARCHAR length");
    ASSERT_TRUE (memcmp (s, "test", 4) == 0, "VARCHAR content");

    ASSERT_TRUE (fabs (db_get_double (&vals[2]) - 9.99) < 1e-9, "DOUBLE column");

    for (int i = 0; i < 3; i++)
      {
	db_value_clear (&vals[i]);
      }
    return 0;
  }

  int
  test_decode_footer_sentinel (void)
  {
    std::cout << "test_decode_footer_sentinel" << std::endl;

    std::vector<char> buf;
    write_footer (buf);

    DB_TYPE types[] = { DB_TYPE_INTEGER };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_INT_EQ (rc, 1, "footer sentinel return code");
    ASSERT_INT_EQ (consumed, 2, "footer consumes 2 bytes");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_truncated_header (void)
  {
    std::cout << "test_decode_truncated_header" << std::endl;

    /* buffer too short for even the int16 row header */
    char buf[1] = { 0 };
    DB_TYPE types[] = { DB_TYPE_INTEGER };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf, 1, types, 1, vals, &consumed);
    ASSERT_TRUE (rc != 0 && rc != 1, "truncated header returns error");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_truncated_field (void)
  {
    std::cout << "test_decode_truncated_field" << std::endl;

    /* row header says 1 field, but the field data is truncated */
    std::vector<char> buf;
    write_row_header (buf, 1);
    write_int32 (buf, 100);	/* field_len = 100, but no data follows */

    DB_TYPE types[] = { DB_TYPE_INTEGER };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_TRUE (rc != 0 && rc != 1, "truncated field returns error");

    db_value_clear (&vals[0]);
    return 0;
  }

  int
  test_decode_field_count_mismatch (void)
  {
    std::cout << "test_decode_field_count_mismatch" << std::endl;

    /* header says 2 fields but we expect 3 columns */
    std::vector<char> buf;
    write_row_header (buf, 2);
    write_field_int (buf, 1);
    write_field_int (buf, 2);

    DB_TYPE types[] = { DB_TYPE_INTEGER, DB_TYPE_INTEGER, DB_TYPE_INTEGER };
    DB_VALUE vals[3];
    for (int i = 0; i < 3; i++)
      {
	db_make_null (&vals[i]);
      }
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 3, vals, &consumed);
    ASSERT_TRUE (rc != 0 && rc != 1, "field count mismatch returns error");

    for (int i = 0; i < 3; i++)
      {
	db_value_clear (&vals[i]);
      }
    return 0;
  }

  int
  test_decode_int_wrong_size (void)
  {
    std::cout << "test_decode_int_wrong_size" << std::endl;

    /* INT expects 4 bytes but we send 8 */
    std::vector<char> buf;
    write_row_header (buf, 1);
    write_int32 (buf, 8);	/* field_len = 8 */
    write_int64 (buf, 42);	/* 8 bytes of data */

    DB_TYPE types[] = { DB_TYPE_INTEGER };
    DB_VALUE vals[1];
    db_make_null (&vals[0]);
    int consumed = 0;

    int rc = decode_binary_row (buf.data (), (int) buf.size (), types, 1, vals, &consumed);
    ASSERT_TRUE (rc != 0 && rc != 1, "INT wrong size returns error");

    db_value_clear (&vals[0]);
    return 0;
  }

} /* namespace test_copy_binary_decoder */
