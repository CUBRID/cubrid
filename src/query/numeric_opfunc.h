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
 * Typedef's and defines for arithmetic package that handles
 * extended precision integers
 */

#ifndef _NUMERIC_OPFUNC_H_
#define _NUMERIC_OPFUNC_H_

#ident "$Id$"

#include "config.h"

#include "intl_support.h"
#include "dbtype_def.h"
#include "error_manager.h"
#include "byte_order.h"

/*
 * Build requirements (enforced via #error)
 *
 * - GCC or Clang: required for __builtin_clz/ctz/bswap intrinsics
 * - __int128: required for multi-precision arithmetic
 * - x86_64: required for x86 intrinsics (_addcarry_u64, _subborrow_u64)
 *
 * Fallback paths for unsupported toolchains/architectures have been
 * removed. If a build environment trips one of the #error directives
 * below, the policy decision should be revisited rather than re-adding
 * the fallback.
 */
#if !(defined(__GNUC__) || defined(__clang__))
#error "GCC or Clang required"
#endif

#if !defined(__SIZEOF_INT128__)
#error "__int128 support is required"
#endif

#if !defined(__x86_64__)
#error "x86_64 architecture required"
#endif

#include <x86intrin.h>

/*
 * CLZ / CTZ / BSWAP (GCC/Clang builtins)
 *
 * - __builtin_clz/ctz: undefined for 0; explicitly handled
 * - __builtin_bswap: always reverses byte order
 */
#define NUMERIC_CLZ64(x) ((x) ? __builtin_clzll(x) : 64)
#define NUMERIC_CTZ64(x) ((x) ? __builtin_ctzll(x) : 64)
#define NUMERIC_CLZ32(x) ((x) ? __builtin_clz(x) : 32)
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN
#define NUMERIC_BSWAP64(x) __builtin_bswap64(x)
#define NUMERIC_BSWAP32(x) __builtin_bswap32(x)
#else /* OR_BYTE_ORDER == OR_BIG_ENDIAN */
#define NUMERIC_BSWAP64(x) (x)
#define NUMERIC_BSWAP32(x) (x)
#endif /* OR_BYTE_ORDER */

/*
 * 128-bit integer support
 */
typedef __uint128_t uint128_t;
typedef uint64_t knuth_digit_t;
typedef uint128_t knuth_double_digit_t;
#define KNUTH_DIGIT_BITS (64)
#define KNUTH_BASE ((knuth_double_digit_t)1 << 64)
#define NUMERIC_CLZ(x) NUMERIC_CLZ64(x)

typedef enum
{
  DATA_STATUS_OK = 0,		/* Operation proceeded without error */
  DATA_STATUS_TRUNCATED = 1004,	/* Operation caused truncation */
  DATA_STATUS_NOT_CONSUMED = 1005	/* Operation not consumed all input */
} DB_DATA_STATUS;

/*
 * NUMERIC_MAX_STRING_SIZE:
 * Defines the maximum internal working buffer size for NUMERIC values.
 * This buffer is widely used in numeric processing, for example:
 *  - Output representation of NUMERIC results
 *  - Arithmetic operations and built-in functions
 *  - Scale adjustment (decimal digit alignment) and rounding
 *
 * The size is set to TWICE_NUM_MAX_PREC (256) * 2
 * to safely absorb digit growth that can occur during scale adjustment.
 * See the TWICE_NUM_MAX_PREC definition in numeric_opfunc.c
 * for a detailed explanation.
 *
 * Maximum string size for NUMERIC output: 256 * 2 = 512
 *   = (max digits (40 + 214) + 2 extra digits) * 2
 */
#define NUMERIC_MAX_STRING_SIZE (((DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + 2) * 2)

#define SECONDS_OF_ONE_DAY      86400	/* 24 * 60 * 60 */
#define MILLISECONDS_OF_ONE_DAY 86400000	/* 24 * 60 * 60 * 1000 */

#define db_locate_numeric(value) ((DB_C_NUMERIC) ((value)->data.num.d.buf))

#define NUMERIC_VALUE_SIGN_BIT_MASK 0x80
#define NUMERIC_HEADER_SCALE_SIGN_BIT_MASK 0x80

#define FIXED_TO_FLOAT_NUMERIC(value) \
  do { \
    (value)->data.num.header.precision = (value)->domain.numeric_info.precision; \
    (value)->data.num.header.scale = (value)->domain.numeric_info.scale; \
    (value)->domain.numeric_info.precision = DB_DEFAULT_NUMERIC_PRECISION; \
    (value)->domain.numeric_info.scale = 0; \
  } while(0)

#define FLOAT_TO_FIXED_NUMERIC(value) \
  do { \
    (value)->domain.numeric_info.precision = (value)->data.num.header.precision; \
    (value)->domain.numeric_info.scale = (value)->data.num.header.scale; \
    (value)->data.num.header.precision = 0; \
    (value)->data.num.header.scale = 0; \
  } while(0)

/* Arithmetic routines */
extern int numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);

/* Comparison routines */
extern int numeric_db_value_compare (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);

/* Coercion routines */
extern void numeric_coerce_int_to_num (int arg, DB_C_NUMERIC answer, bool * is_value_negative);
extern void numeric_coerce_bigint_to_num (DB_BIGINT arg, DB_C_NUMERIC answer, bool * is_value_negative);
extern void numeric_coerce_num_to_int (DB_C_NUMERIC arg, int *answer, const bool is_value_negative);
extern int numeric_coerce_num_to_bigint (DB_C_NUMERIC arg, int scale, DB_BIGINT * answer, const bool is_value_negative);

extern void numeric_coerce_dec_str_to_num (const char *dec_str, DB_C_NUMERIC result, bool * is_value_negative);
extern void numeric_coerce_num_to_dec_str (const DB_VALUE * num_value, char *dec_str);

extern void numeric_coerce_num_to_double (const DB_VALUE * num_value, int scale, double *adouble);
extern int numeric_internal_double_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
					   bool * is_value_negative);
extern int numeric_internal_float_to_num (float afloat, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
					  bool * is_value_negative);

extern int numeric_coerce_string_to_num (const char *astring, int astring_len, INTL_CODESET codeset, DB_VALUE * num);

extern int numeric_coerce_num_to_num (const DB_VALUE * src_value, int src_prec, int src_scale, int dest_prec,
				      int dest_scale, DB_C_NUMERIC dest_num, bool * dest_num_is_negative);

extern int numeric_db_value_coerce_to_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num_strict (DB_VALUE * src, DB_VALUE * dest);
extern char *numeric_db_value_print (const DB_VALUE * val, char *buf);

/* Floating-Point NUMERIC */
extern int numeric_get_precision_digits (uint8_t * calc_buf);
extern int float_numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_mod (const DB_VALUE * value1, const DB_VALUE * value2, DB_VALUE * result);
extern void float_numeric_normalize_for_hash (DB_C_NUMERIC num, uint8_t * calc_buf, int precision, int scale);

/* Testing Routines */
extern bool numeric_db_value_is_zero (const DB_VALUE * arg);

extern int numeric_db_value_is_positive (const DB_VALUE * arg);
#endif /* _NUMERIC_OPFUNC_H_ */
