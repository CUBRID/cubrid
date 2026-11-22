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

#include "byte_order.h"
#include "dbtype_def.h"
#include "error_manager.h"
#include "intl_support.h"
#include "porting_inline.hpp"
#include "query_sum_accumulator.h"

#include <string.h>

/*
 * Build requirements (enforced via #error)
 *
 * - GCC or Clang: required for __builtin_clz/ctz/bswap intrinsics and __builtin_mul_overflow on __int128
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

/*
 * NUMERIC_SCALE_COUNT: The number of scales a NUMERIC value can have, DB_MIN_NUMERIC_SCALE to DB_MAX_NUMERIC_SCALE.
 *   = 252 - (-214) + 1 = 467
 */
#define NUMERIC_SCALE_COUNT (DB_MAX_NUMERIC_SCALE - DB_MIN_NUMERIC_SCALE + 1)

#define SECONDS_OF_ONE_DAY      86400	/* 24 * 60 * 60 */
#define MILLISECONDS_OF_ONE_DAY 86400000	/* 24 * 60 * 60 * 1000 */

#define db_locate_numeric(value) ((DB_C_NUMERIC) ((value)->data.num.d.buf))

#define NUMERIC_VALUE_SIGN_BIT_MASK 0x80
#define NUMERIC_HEADER_SCALE_SIGN_BIT_MASK 0x80

/* the Mersenne prime 2^61 - 1 of numeric_db_value_hash (): 2^61 is 1 mod it, so a reduction takes shifts and adds */
#define NUMERIC_HASH_PRIME_BITS (61)
#define NUMERIC_HASH_PRIME ((1ULL << NUMERIC_HASH_PRIME_BITS) - 1)

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

/* 10^-scale mod NUMERIC_HASH_PRIME for every scale, in a struct because numeric_hash_make_inverse_pow10_table (), a
 * constexpr function, cannot return an array */
typedef struct numeric_hash_inverse_pow10_table NUMERIC_HASH_INVERSE_POW10_TABLE;
struct numeric_hash_inverse_pow10_table
{
  uint64_t value[NUMERIC_SCALE_COUNT];
};

extern const NUMERIC_HASH_INVERSE_POW10_TABLE numeric_hash_inverse_pow10;

/* Arithmetic routines */
extern int numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);

/* Comparison routines */
extern int numeric_db_value_compare (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern DB_VALUE_COMPARE_RESULT numeric_compare_bigint_num_exact (DB_BIGINT value, const DB_VALUE * num_value);

/* Coercion routines */
extern void numeric_coerce_int_to_num (int arg, DB_C_NUMERIC answer, bool * is_value_negative);
extern void numeric_coerce_bigint_to_num (DB_BIGINT arg, DB_C_NUMERIC answer, bool * is_value_negative);
extern void numeric_coerce_num_to_int (DB_C_NUMERIC arg, int *answer, const bool is_value_negative);
extern int numeric_coerce_num_to_bigint (DB_C_NUMERIC arg, int scale, DB_BIGINT * answer, const bool is_value_negative);

extern void numeric_coerce_dec_str_to_num (const char *dec_str, DB_C_NUMERIC result, bool * is_value_negative);
extern void numeric_coerce_num_to_dec_str (const DB_VALUE * num_value, char *dec_str);

extern void numeric_coerce_num_to_double (const DB_VALUE * src, int scale, double *dest);
extern int numeric_internal_double_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
					   bool * is_value_negative);
extern void numeric_shortest_digits (double x, bool is_float, int max_digits, char *buf, int *decpt, int *sign);

extern void numeric_coerce_num_to_float (const DB_VALUE * src, int scale, float *dest);
extern int numeric_internal_float_to_num (float afloat, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
					  bool * is_value_negative);

extern int numeric_coerce_string_to_num (const char *astring, int astring_len, INTL_CODESET codeset, DB_VALUE * num);

extern int numeric_coerce_num_to_num (const DB_VALUE * src_value, int src_prec, int src_scale, int dest_prec,
				      int dest_scale, DB_C_NUMERIC dest_num, bool * dest_num_is_negative);

#if defined (__cplusplus)
/* Fixed-type operations shared by the legacy dispatchers and the converters. */
/* *INDENT-OFF* */
template <DB_TYPE SRC>
int numeric_coerce_value_to_num (const DB_VALUE *src, DB_VALUE *dest, DB_DATA_STATUS *data_status);
/* *INDENT-ON* */

extern int numeric_coerce_string_to_num_status (const char *astring, int astring_length, INTL_CODESET codeset,
						DB_VALUE * result);
extern int numeric_coerce_num_to_double (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_float (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_monetary (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_int (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_bigint (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_short (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_double_strict (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_float_strict (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_monetary_strict (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_int_strict (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_bigint_strict (const DB_VALUE * src, int scale, DB_VALUE * dest);
extern int numeric_coerce_num_to_short_strict (const DB_VALUE * src, int scale, DB_VALUE * dest);

#endif /* __cplusplus */

extern int numeric_db_value_coerce_to_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern char *numeric_db_value_print (const DB_VALUE * val, char *buf);

/* Floating-Point NUMERIC */
extern int numeric_get_precision_digits (uint8_t * calc_buf);
extern int float_numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int float_numeric_db_value_mod (const DB_VALUE * value1, const DB_VALUE * value2, DB_VALUE * result);
extern int float_numeric_normalize_for_sort (DB_C_NUMERIC num, int scale, uint8_t * mantissa);

/* NUMERIC hash */
#if defined (__cplusplus)
STATIC_INLINE constexpr uint64_t numeric_hash_mod (uint128_t value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE constexpr uint64_t numeric_hash_mul (uint64_t residue1, uint64_t residue2)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE constexpr uint64_t numeric_hash_int64 (int64_t value) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE uint64_t numeric_db_value_hash (const DB_VALUE * value) __attribute__ ((ALWAYS_INLINE));
#endif /* __cplusplus */

/* Testing Routines */
extern bool numeric_db_value_is_zero (const DB_VALUE * arg);

extern int numeric_db_value_is_positive (const DB_VALUE * arg);

/*
 * NUMERIC_AGG_EXPR_VAL: carrier for a {+,-,*} expression tree evaluated entirely
 * in the word domain.
 *
 * The legacy path materializes a DB_VALUE at each operation, packing the
 * coefficient into its 17-byte form and re-deriving precision each time.
 * An agg-expr value keeps the running coefficient in 128 bits and packs once.
 *
 * Bit-identity with the operation-by-operation path depends on the two places
 * where float_numeric_db_value_add/sub/mul can round:
 *
 *   1. A result exceeding DB_MAX_NUMERIC_PRECISION (40) digits, where
 *      float_numeric_check_overflow_and_adjust_scale () trims the scale and
 *      float_numeric_round_and_pack () rounds. Below 41 digits, round_and_pack
 *      only performs a plain pack.
 *   2. A product whose scale exceeds DB_MAX_NUMERIC_SCALE (252), which is
 *      rescaled with rounding.
 *
 * A uint128 coefficient has at most 39 digits, so case 1 cannot occur during
 * agg-expr evaluation. Each operation therefore only needs to check for uint128
 * overflow and scale above DB_MAX_NUMERIC_SCALE; digit counting is deferred
 * until the final pack. Division, non-NUMERIC operands, or either rejection
 * falls back to the legacy path.
 */
typedef struct numeric_agg_expr_val NUMERIC_AGG_EXPR_VAL;
struct numeric_agg_expr_val
{
  uint128_t coefficient;	/* unsigned magnitude; the sign lives in is_negative */
  int scale;			/* decimal scale, <= DB_MAX_NUMERIC_SCALE */
  bool is_negative;		/* always false when coefficient is zero */
};

extern bool numeric_agg_expr_from_dbv (const DB_VALUE * dbv, NUMERIC_AGG_EXPR_VAL * out);
extern bool numeric_agg_expr_from_int_dbv (const DB_VALUE * dbv, NUMERIC_AGG_EXPR_VAL * out);
extern bool numeric_agg_expr_from_disk (const unsigned char *disk, int avail, NUMERIC_AGG_EXPR_VAL * out);
extern bool numeric_agg_expr_mul (const NUMERIC_AGG_EXPR_VAL * left, const NUMERIC_AGG_EXPR_VAL * right,
				  NUMERIC_AGG_EXPR_VAL * out);
extern bool numeric_agg_expr_add (const NUMERIC_AGG_EXPR_VAL * left, const NUMERIC_AGG_EXPR_VAL * right,
				  bool flip_right_sign, NUMERIC_AGG_EXPR_VAL * out);
extern void numeric_agg_expr_to_dbv (const NUMERIC_AGG_EXPR_VAL * cv, DB_VALUE * answer);

extern bool numeric_db_value_round_to_place (const DB_VALUE * value, DB_BIGINT place, bool half_up, DB_VALUE * result);
extern bool numeric_db_value_floor_ceil (const DB_VALUE * value, bool is_floor, DB_VALUE * result);

extern int numeric_sum_acc_add_dbv (SUM_ACC * acc, const DB_VALUE * dbv);
extern void numeric_sum_acc_load_dbv (SUM_ACC * acc, const DB_VALUE * dbv);
extern int numeric_sum_acc_merge (SUM_ACC * acc, const SUM_ACC * other);
extern int numeric_sum_acc_add_expr_val (SUM_ACC * acc, const NUMERIC_AGG_EXPR_VAL * val);
extern int numeric_sum_acc_snapshot (const SUM_ACC * acc, DB_VALUE * result);

/*
 * NUMERIC_HEADER_SIZE
 * The numeric header is 3 bytes in size.  Shared by every reader of the disk bytes (mr_data_readval_numeric () in
 * object_primitive.c, numeric_repr_read_disk ()), which must agree on where the digits start.
 * - header[0]: Total byte size of the numeric data (header + data).
 *              Bit 7 (0x80): Data sign bit (1 = negative, 0 = positive).
 *              Bits 0-6: Total byte size value (4 ~ 20 bytes).
 *                        Minimum: precision 1 -> data 1 byte + header 3 = 4 bytes (aligned to 4).
 *                        Maximum: precision 40 -> data 17 bytes + header 3 = 20 bytes (aligned to 20).
 * - header[1]: Precision and scale sign bit.
 *              Bit 7 (0x80): Scale sign bit (1 = negative scale, 0 = positive scale).
 *              Bits 0-6: Precision value (1 ~ 40).
 * - header[2]: Scale value.
 *              Bits 0-7: Scale absolute value (0 ~ 214).
 *                        Positive scale: 0 ~ 252.
 *                        Negative scale: -214 to -1 (stored as absolute value, sign in header[1] bit 7).
 */
#define NUMERIC_HEADER_SIZE (3)

/* how the digits of a NUMERIC value are laid out in its DB_VALUE buffer (DB_NUMERIC::d.buf) and on disk; outside
 * numeric_opfunc.c, code that reads or writes those bytes goes through numeric_repr_*. The magnitude is one
 * big-endian binary integer of DB_NUMERIC_BUF_SIZE bytes; the disk keeps its low bytes, the header plus the digits
 * padded to a multiple of 4. Plain C: dbtype_function.c is compiled as C. */

/* disk size (header included) of a float NUMERIC by its precision */
static inline int
numeric_repr_disk_size_by_precision (int precision)
{
  static const int size_by_precision[DB_MAX_NUMERIC_PRECISION + 1] = {
    0, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 20, 20, 20, 20, 20, 20, 20, 20,
    20
  };

  return size_by_precision[precision];
}

/* disk size (header included) of a fixed NUMERIC by its value: a fixed precision is at most 38, so the magnitude
 * fits in the low 16 bytes */
static inline int
numeric_repr_disk_size_by_value (const unsigned char *buf)
{
  static const int size_by_bytes[17] = {
    0, 4, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20
  };
  uint64_t word_buf[2];

  /* the bytes stay big-endian: on a little-endian host the leading zero bytes are the trailing zero bits */
  memcpy (word_buf, buf + 1, sizeof (word_buf));
  if (word_buf[0] != 0)
    {
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN
      return size_by_bytes[16 - (__builtin_ctzll (word_buf[0]) >> 3)];
#else
      return size_by_bytes[16 - (__builtin_clzll (word_buf[0]) >> 3)];
#endif
    }
  if (word_buf[1] != 0)
    {
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN
      return size_by_bytes[8 - (__builtin_ctzll (word_buf[1]) >> 3)];
#else
      return size_by_bytes[8 - (__builtin_clzll (word_buf[1]) >> 3)];
#endif
    }
  return size_by_bytes[1];	/* zero */
}

/* the buffer from the digits of a disk value of disk_size bytes, or from a whole buffer (DB_NUMERIC_BUF_SIZE); 0 for
 * any other size. A constant length per case keeps the copies inline moves on the row read path (a variable length
 * becomes rep stos/movs), and one switch for both keeps the row path to a single jump. */
static inline int
numeric_repr_read_disk (unsigned char *buf, const unsigned char *digits, int disk_size)
{
  switch (disk_size)
    {
    case 4:
      memset (buf, 0, 16);
      memcpy (buf + 16, digits, 1);
      return 1;
    case 8:
      memset (buf, 0, 12);
      memcpy (buf + 12, digits, 5);
      return 1;
    case 12:
      memset (buf, 0, 8);
      memcpy (buf + 8, digits, 9);
      return 1;
    case 16:
      memset (buf, 0, 4);
      memcpy (buf + 4, digits, 13);
      return 1;
    case DB_NUMERIC_BUF_SIZE:
    case 20:
      memcpy (buf, digits, DB_NUMERIC_BUF_SIZE);
      return 1;
    default:
      return 0;
    }
}

/* the digits of the disk value of disk_size bytes */
static inline void
numeric_repr_write_disk (unsigned char *digits, const unsigned char *buf, int disk_size)
{
  const int digits_size = disk_size - NUMERIC_HEADER_SIZE;

  memcpy (digits, buf + DB_NUMERIC_BUF_SIZE - digits_size, digits_size);
}

#if defined (__cplusplus)
/*
 * numeric_hash_mod () - reduce a value mod NUMERIC_HASH_PRIME
 *   return: the value mod 2^61 - 1
 *   value(in): a value below 2^122, such as the product of two residues
 *
 * Note: each fold adds the bits above bit 61 to the bits below it. Two folds bring the value to at most the prime, and
 *       one subtraction below it.
 */
STATIC_INLINE constexpr uint64_t
numeric_hash_mod (uint128_t value)
{
  uint64_t residue = (uint64_t) (value & NUMERIC_HASH_PRIME) + (uint64_t) (value >> NUMERIC_HASH_PRIME_BITS);

  residue = (residue & NUMERIC_HASH_PRIME) + (residue >> NUMERIC_HASH_PRIME_BITS);
  return (residue >= NUMERIC_HASH_PRIME) ? residue - NUMERIC_HASH_PRIME : residue;
}

/*
 * numeric_hash_mul () - multiply two residues mod NUMERIC_HASH_PRIME
 *   return: residue1 x residue2 mod 2^61 - 1
 *   residue1(in): a residue mod 2^61 - 1
 *   residue2(in): a residue mod 2^61 - 1
 */
STATIC_INLINE constexpr uint64_t
numeric_hash_mul (uint64_t residue1, uint64_t residue2)
{
  return numeric_hash_mod ((uint128_t) residue1 * residue2);
}

/*
 * numeric_hash_int64 () - hash an integer as numeric_db_value_hash () hashes the NUMERIC of its value
 *   return: the value mod 2^61 - 1, its bits not mixed
 *   value(in): an integer
 *
 * Note: a SMALLINT, an INTEGER, a BIGINT and a NUMERIC of one value (e.g., 5 and 5.00) give one residue.
 */
STATIC_INLINE constexpr uint64_t
numeric_hash_int64 (int64_t value)
{
  /* the magnitude, INT64_MIN's (2^63) included */
  const uint64_t residue = numeric_hash_mod ((value < 0) ? 0 - (uint64_t) value : (uint64_t) value);

  /* negate a negative value, as numeric_db_value_hash () does */
  return (value < 0 && residue != 0) ? NUMERIC_HASH_PRIME - residue : residue;
}

static_assert (numeric_hash_int64 (-1) == NUMERIC_HASH_PRIME - 1, "-1 is the prime less 1");
static_assert (numeric_hash_int64 ((int64_t) NUMERIC_HASH_PRIME) == 0, "the prime is 0");
static_assert (numeric_hash_int64 (INT64_MIN) == NUMERIC_HASH_PRIME - 4, "2^63 is 4 mod 2^61 - 1");

/*
 * numeric_db_value_hash () - hash a NUMERIC value
 *   return: the value mod 2^61 - 1, its bits not mixed
 *   value(in): a NUMERIC value, fixed or float
 *
 * Steps:
 *   Step 1: read the scale as db_get_numeric_precision_and_scale () reads it: a float NUMERIC keeps it in the value, a
 *           fixed one in its domain.
 *   Step 2: read the magnitude, a byte then two words, big end first.
 *   Step 3: reduce the magnitude mod the prime 2^61 - 1, 2^128 being 64 and 2^64 being 8 mod it (numeric_hash_mod ()).
 *   Step 4: divide by 10^scale, multiplying by 10^-scale mod the prime (numeric_hash_inverse_pow10).
 *   Step 5: negate a negative value: -x is the prime less x, so 1.5 and -1.5 hash apart and a zero keeps one hash.
 *
 * Note:
 *   - db_get_numeric_precision_and_scale () cannot be called here, as dbtype.h includes this header before
 *     dbtype_function.i defines it.
 *   - The value is its magnitude x 10^-scale, and 10 has an inverse mod the prime. Equivalent numeric values (1.5 as 15
 *     at scale 1, 1.50 as 150 at scale 2) give one residue, whatever their precision and scale, with no zero stripped
 *     or appended.
 *   - Unlike float_numeric_normalize_for_sort (), the hash keeps no order.
 */
STATIC_INLINE uint64_t
numeric_db_value_hash (const DB_VALUE * value)
{
  const DB_C_NUMERIC num = db_locate_numeric (value);
  uint64_t high, low, residue;
  int scale;

  /* Step 1: read the scale: a float NUMERIC keeps it in the value, a fixed one in its domain */
  scale = ((value->domain.numeric_info.precision == DB_DEFAULT_NUMERIC_PRECISION)
	   ? value->data.num.header.scale : value->domain.numeric_info.scale);
  assert (scale >= DB_MIN_NUMERIC_SCALE && scale <= DB_MAX_NUMERIC_SCALE);

  /* Step 2: read the magnitude, a byte then two words, big end first. The buffer (DB_NUMERIC_BUF_SIZE, 17 bytes) holds
   * the magnitude unsigned, the sign kept in the domain (Step 5):
   *   num[0]         bits 128 - 135
   *   num[1 - 8]     high, bits 64 - 127
   *   num[9 - 16]    low, bits 0 - 63
   * so the magnitude is num[0] x 2^128 + high x 2^64 + low */
  memcpy (&high, num + 1, sizeof (high));
  memcpy (&low, num + 1 + sizeof (high), sizeof (low));
  high = NUMERIC_BSWAP64 (high);
  low = NUMERIC_BSWAP64 (low);

  /* Step 3: reduce the magnitude mod the prime, 2^128 being 64 and 2^64 being 8 mod it */
  residue = numeric_hash_mod ((uint128_t) num[0] * 64 + (uint128_t) high * 8 + low);

  /* Step 4: divide by 10^scale, multiplying by 10^-scale mod the prime */
  if (scale != 0)
    {
      residue = numeric_hash_mul (residue, numeric_hash_inverse_pow10.value[scale - DB_MIN_NUMERIC_SCALE]);
    }

  /* Step 5: negate a negative value */
  if (value->domain.numeric_info.is_value_negative && residue != 0)
    {
      residue = NUMERIC_HASH_PRIME - residue;
    }

  return residue;
}
#endif /* __cplusplus */

#endif /* _NUMERIC_OPFUNC_H_ */
