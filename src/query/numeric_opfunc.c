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
 * numeric_opfunc.c - Basig manipulations of DB_NUMERIC type data
 */

#ident "$Id$"

/* The bits in the character string of a DB_NUMERIC are the binary digits of
 * the number. The LSB's of the DB_NUMERIC are in buf[DB_NUMERIC_BUF_SIZE-1].
 */

#include <float.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <cstdint>

#include "mprec.h"
#include "numeric_opfunc.h"
#include "tz_support.h"
#include "db_date.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "byte_order.h"
#include "object_primitive.h"
#include "object_representation.h"

#if defined (__cplusplus)
#include <cmath>
#endif

#include "dbtype.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/* the multipler of long NUMERIC, internal used */
#define DB_LONG_NUMERIC_MULTIPLIER 2

#define CARRYOVER(arg)		((arg) >> 8)
#define BORROW_NEXT(minuend, subtrahend, borrow) ((unsigned)(minuend) < (unsigned)((subtrahend) + (borrow)))
#define GET_LOWER_BYTE(arg)	((arg) & 0xff)
#define NUMERIC_ABS(a)		((a) >= 0 ? a : -a)

#define NUMERIC_AS_WORDS                (3)	/* (DB_NUMERIC_BUF_SIZE(17) + 7) / 8 */
#define NUMERIC_AS_WORD_BYTES           (24)	/* NUMERIC_AS_WORDS * 8 */
#define NUMERIC_GET_FULL_WORDS(bytes)   ((bytes) >> 3)	/* Convert bytes to full words (floor) */
#define NUMERIC_GET_REM_BYTES(bytes)    ((bytes) & 7)	/* Remaining bytes after word alignment */
#define NUMERIC_GET_WORD_COUNT(bytes)   (((bytes) + 7) >> 3)	/* Total word count to cover bytes (ceiling) */
#define NUMERIC_GET_BYTE_COUNT(words)   ((words) << 3)	/* Convert words to byte count */
/*
 * TWICE_NUM_MAX_PREC:
 * - Defines the maximum number of significant digits that can be stored
 *   in a NUMERIC value (excluding sign and decimal point).
 * - Used as the internal buffer size for representing the maximum
 *   range of NUMERIC values.
 * - Also used when converting base-256 encoded NUMERIC values
 *   into their decimal string representation.
 *
 * The maximum allowable range of NUMERIC values is:
 *   1) Exponential form
 *        1.0 × 10^-252 <= |Value| < 1.0 × 10^254
 *   2) Precision/Scale form
 *        (prec, scale) in the range: (1,252) <= (prec,scale) <= (40,-214)
 *
 * TWICE_NUM_MAX_PREC: 256
 * = max digits (40 + 214) = 254 + 2 extra digit
 * Must always be smaller than NUMERIC_MAX_STRING_SIZE.
 */
#define TWICE_NUM_MAX_PREC      ((DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + 2)
#define SECONDS_IN_A_DAY	(int)(24L * 60L * 60L)

#define ROUND(x)                ((x) > 0 ? ((x) + .5) : ((x) - .5))
#define ROUND_HALF_UP_DIGIT     (5)

/* 
 * (((DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + DB_MAX_NUMERIC_SCALE) + 6) 
 * = ((40 - (-214)) + 252) + 6 
 * = 506 + 6 = 512
 */
#define POW10_MAX_INDEX         (((DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + DB_MAX_NUMERIC_SCALE) + 6)
/* 
 * floor(POW10_MAX_INDEX * log256​(10)) + 1 
 * = (512 * 0.41524) + 1 
 * = 212 + 1 = 213
 * = 213 / 8 = 26.6 -> 27
 */
#define POW10_BUF_WORDS         (27)
#define POW10_BUF_SIZE          (POW10_BUF_WORDS * sizeof(uint64_t))

/* [10^n][multi-precision buffer (uint64_t words, MSB-first, each word in host endianness)] */
static uint64_t powers_of_10[POW10_MAX_INDEX + 1][POW10_BUF_WORDS];

static const double numeric_Pow_of_10[10] = {
  1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9
};

/* look-up table : containing pre-calculated 4-byte ASCII representations for numbers 0000-9999. */
static uint32_t _gv_digits4_ascii_lut[10000];

/*
 * _gv_numeric_precision_to_bytes_lookup
 *
 * this is a lookup table (LUT) that precomputes the number of bytes
 * required for a numeric value based on its decimal digits (precision),
 * instead of calculating it with floating-point operations each time.
 *
 *   bytes = ceil(precision / log10(256))
 *
 * - Index/size:
 *   513 = (((DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + DB_MAX_NUMERIC_SCALE) + 6) + 1
 *       = (((40 - (-214)) + 252) + 6) + 1
 *
 *   +6 : Extra space added to align the maximum index used internally
 *        to a power-of-two boundary (e.g., aligning 506 -> 512).
 *   +1 : Index 0 is reserved for special use / adjustment.
 *
 * the size is chosen generously to safely cover the full range of
 * precision/scale values that are actually used, including the
 * extra range required when adjusting scale and allocating working buffers.
 */
static const int _gv_numeric_precision_to_bytes_lookup[513] = {
  0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8,
  9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 15, 16, 16, 17,
  17, 18, 18, 18, 19, 19, 20, 20, 20, 21, 21, 22, 22, 23, 23, 23, 24, 24, 25, 25,
  25, 26, 26, 27, 27, 27, 28, 28, 29, 29, 30, 30, 30, 31, 31, 32, 32, 32, 33, 33,
  34, 34, 35, 35, 35, 36, 36, 37, 37, 37, 38, 38, 39, 39, 40, 40, 40, 41, 41, 42,
  42, 42, 43, 43, 44, 44, 45, 45, 45, 46, 46, 47, 47, 47, 48, 48, 49, 49, 49, 50,
  50, 51, 51, 52, 52, 52, 53, 53, 54, 54, 54, 55, 55, 56, 56, 57, 57, 57, 58, 58,
  59, 59, 59, 60, 60, 61, 61, 62, 62, 62, 63, 63, 64, 64, 64, 65, 65, 66, 66, 67,
  67, 67, 68, 68, 69, 69, 69, 70, 70, 71, 71, 72, 72, 72, 73, 73, 74, 74, 74, 75,
  75, 76, 76, 76, 77, 77, 78, 78, 79, 79, 79, 80, 80, 81, 81, 81, 82, 82, 83, 83,
  84, 84, 84, 85, 85, 86, 86, 86, 87, 87, 88, 88, 89, 89, 89, 90, 90, 91, 91, 91,
  92, 92, 93, 93, 94, 94, 94, 95, 95, 96, 96, 96, 97, 97, 98, 98, 98, 99, 99, 100,
  100, 101, 101, 101, 102, 102, 103, 103, 103, 104, 104, 105, 105, 106, 106, 106, 107, 107, 108, 108,
  108, 109, 109, 110, 110, 111, 111, 111, 112, 112, 113, 113, 113, 114, 114, 115, 115, 116, 116, 116,
  117, 117, 118, 118, 118, 119, 119, 120, 120, 121, 121, 121, 122, 122, 123, 123, 123, 124, 124, 125,
  125, 125, 126, 126, 127, 127, 128, 128, 128, 129, 129, 130, 130, 130, 131, 131, 132, 132, 133, 133,
  133, 134, 134, 135, 135, 135, 136, 136, 137, 137, 138, 138, 138, 139, 139, 140, 140, 140, 141, 141,
  142, 142, 143, 143, 143, 144, 144, 145, 145, 145, 146, 146, 147, 147, 147, 148, 148, 149, 149, 150,
  150, 150, 151, 151, 152, 152, 152, 153, 153, 154, 154, 155, 155, 155, 156, 156, 157, 157, 157, 158,
  158, 159, 159, 160, 160, 160, 161, 161, 162, 162, 162, 163, 163, 164, 164, 165, 165, 165, 166, 166,
  167, 167, 167, 168, 168, 169, 169, 170, 170, 170, 171, 171, 172, 172, 172, 173, 173, 174, 174, 174,
  175, 175, 176, 176, 177, 177, 177, 178, 178, 179, 179, 179, 180, 180, 181, 181, 182, 182, 182, 183,
  183, 184, 184, 184, 185, 185, 186, 186, 187, 187, 187, 188, 188, 189, 189, 189, 190, 190, 191, 191,
  192, 192, 192, 193, 193, 194, 194, 194, 195, 195, 196, 196, 196, 197, 197, 198, 198, 199, 199, 199,
  200, 200, 201, 201, 201, 202, 202, 203, 203, 204, 204, 204, 205, 205, 206, 206, 206, 207, 207, 208,
  208, 209, 209, 209, 210, 210, 211, 211, 211, 212, 212, 213, 213
};

/* precomputed lookup table for 10^1 through 10^19 */
static const uint64_t _gv_mul_normalize_pow10_lookup[19] = {
  10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL, 1000000ULL, 10000000ULL, 100000000ULL, 1000000000ULL,
  10000000000ULL, 100000000000ULL, 1000000000000ULL, 10000000000000ULL, 100000000000000ULL,
  1000000000000000ULL, 10000000000000000ULL,
  100000000000000000ULL, 1000000000000000000ULL, 10000000000000000000ULL
};

typedef enum fp_value_type
{
  FP_VALUE_TYPE_NUMBER,
  FP_VALUE_TYPE_INFINITE,
  FP_VALUE_TYPE_NAN,
  FP_VALUE_TYPE_ZERO
}
FP_VALUE_TYPE;

typedef enum
{
  NUMERIC_MAG_ZERO,		/* all bytes are zero */
  NUMERIC_MAG_INT,		/* magnitude fits in int range */
  NUMERIC_MAG_BIGINT,		/* magnitude fits in DB_BIGINT range */
  NUMERIC_MAG_NUMERIC		/* magnitude exceeds DB_BIGINT range */
} numeric_magnitude_t;

static inline bool numeric_is_negative (const DB_VALUE * value);
static void numeric_copy (DB_C_NUMERIC dest, DB_C_NUMERIC source);
static void numeric_copy_long (DB_C_NUMERIC dest, DB_C_NUMERIC source, bool is_long_num);
static void numeric_increase (DB_C_NUMERIC answer);
static void numeric_increase_long (DB_C_NUMERIC answer, bool is_long_num);
static void numeric_zero (DB_C_NUMERIC answer, int size);
static void numeric_init_pow_of_10_helper (void);
static void numeric_get_pow_of_10 (int exp, uint8_t * result);
static void numeric_init_digits4_ascii_helper (void);
static inline uint32_t numeric_get_digits4_ascii (uint32_t val);
static int float_numeric_find_first_nz_idx_msb (const uint64_t * word_buf, int calc_words);
static int float_numeric_get_decimal_digit (const uint64_t * word_buf, int calc_words);
static void numeric_double_shift_bit (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, int numbits, DB_C_NUMERIC lsb,
				      DB_C_NUMERIC msb, bool is_long_num);
static int numeric_compare_pos (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2);
static void numeric_shift_byte (DB_C_NUMERIC arg, int numbytes, DB_C_NUMERIC answer, int length);
static bool numeric_is_zero (DB_C_NUMERIC arg);
static numeric_magnitude_t numeric_classify_magnitude (DB_C_NUMERIC arg, bool is_value_negative);
static bool numeric_overflow (DB_C_NUMERIC arg, int exp);
static void numeric_add (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, int size);
static void float_numeric_add (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word,
			       int calc_words);
static int float_numeric_add_fast (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word,
				   int calc_words, bool dbv1_sign, bool dbv2_sign, bool * result_sign,
				   uint8_t * result_buf);
static void numeric_sub (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, int size);
static void float_numeric_sub (const uint64_t * arg1_word, const uint64_t * arg2_word, uint64_t * result_word,
			       int calc_words);
static int float_numeric_sub_fast (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word,
				   int calc_words, bool dbv1_sign, bool dbv2_sign, bool * result_sign,
				   uint8_t * result_buf);
static void numeric_mul (DB_C_NUMERIC a1, DB_C_NUMERIC a2, DB_C_NUMERIC answer, bool * is_value_negative);
static void float_numeric_mul (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word,
			       int calc_words, int calc_nbytes);
static int float_numeric_mul_fast (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word,
				   int calc_words, uint8_t * result_buf, int *result_scale);
static void numeric_long_div (DB_C_NUMERIC a1, DB_C_NUMERIC a2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder,
			      bool is_long_num);
static void numeric_div (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder,
			 bool arg1_value_is_negative, bool arg2_value_is_negative);
static void float_numeric_div_fast (uint64_t dividend_val, uint64_t divisor_val, int prec1, int scale1, int prec2,
				    int scale2, int exponent_diff, uint8_t * result_buf, int *result_prec_out,
				    int *result_scale_out, bool * result_sign);
static void float_numeric_mod_fast (uint64_t dividend_val, uint64_t divisor_val, uint8_t * result_buf,
				    int *result_prec_out, int *result_scale_out, bool * result_sign);
static int knuth_find_first_nz_idx (const knuth_digit_t * digit_buf, int total_digits);
static unsigned int knuth_count_leading_zero_bits (knuth_digit_t value);
static void knuth_normalize_left_shift_msb (knuth_digit_t * buffer, int buffer_size, unsigned int k_bit);
static void knuth_normalize_right_shift_msb (knuth_digit_t * buffer, int buffer_size, unsigned int k_bit);
static knuth_digit_t knuth_estimate_quotient_digit (const knuth_digit_t * u_work, const knuth_digit_t * v_work,
						    int window_offset, int dividend_words, int divisor_words,
						    knuth_digit_t * out_trial_remainder);
static knuth_digit_t knuth_multiply_and_subtract (knuth_digit_t * u_work, const knuth_digit_t * v_work,
						  int window_offset, int divisor_words, knuth_digit_t trial_quotient);
static void float_numeric_knuth_div (knuth_digit_t * dbv1_buf, knuth_digit_t * dbv2_buf, knuth_digit_t * quo_buf,
				     knuth_digit_t * rem_buf, knuth_digit_t calc_words);
static int float_numeric_compare (uint8_t * arg1, uint8_t * arg2, int prec1, int scale1, int prec2, int scale2,
				  bool arg1_sign, bool arg2_sign);
static int numeric_scale_by_ten (DB_C_NUMERIC arg, bool is_long_num);
static int numeric_scale_dec (const DB_C_NUMERIC arg, int dscale, DB_C_NUMERIC answer);
static int numeric_scale_dec_long (DB_C_NUMERIC answer, int dscale, bool is_long_num);
static int numeric_common_prec_scale (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * dbv1_common,
				      DB_VALUE * dbv2_common);
static int numeric_prec_scale_when_overflow (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * dbv1_common,
					     DB_VALUE * dbv2_common);
static void numeric_coerce_big_num_to_dec_str (unsigned char *num, char *dec_str);
static int numeric_get_msb_for_dec (int src_prec, int src_scale, unsigned char *src, int *dest_prec, int *dest_scale,
				    DB_C_NUMERIC dest);
static FP_VALUE_TYPE get_fp_value_type (double d);
static int numeric_internal_real_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
					 bool is_float, bool * is_value_negative);
static int analyze_numeric_string (const char *astring, int astring_length, INTL_CODESET codeset, bool * negate_value,
				   char *int_digits, int *int_len, char *frac_digits, int *frac_len,
				   int *frac_first_sig_digit, int *frac_last_sig_digit, bool * is_zero);
static void determine_prec_scale (const char *int_digits, int int_len, const char *frac_digits, int frac_len,
				  int frac_first_sig_digit, int frac_last_sig_digit, char *num_string, int *out_prec,
				  int *out_scale);
static void determine_round (char *out_str, int *out_prec, int *out_scale, int tmp_int_len, int tmp_frac_len,
			     int frac_zero_cnt, char next_digit);
static void numeric_get_integral_part (const DB_C_NUMERIC num, const int src_prec, const int src_scale,
				       const int dst_prec, DB_C_NUMERIC dest);
static void numeric_get_fractional_part (const DB_C_NUMERIC num, const int src_scale, const int dst_prec,
					 DB_C_NUMERIC dest);
static bool numeric_is_fraction_part_zero (const DB_VALUE * num_value, const int scale);
static bool numeric_is_longnum_value (DB_C_NUMERIC arg);
static int numeric_longnum_to_shortnum (DB_C_NUMERIC answer, DB_C_NUMERIC long_arg);
static void numeric_shortnum_to_longnum (DB_C_NUMERIC long_answer, DB_C_NUMERIC arg);
static int get_significant_digit (DB_BIGINT i);
static void float_numeric_mul_pow10 (uint64_t * dbv_buf, int calc_words, int calc_bytes, uint64_t multiplier);
static void float_numeric_mul_normalize (uint64_t * dbv_buf, int calc_words, int calc_bytes, int exponent);
static uint64_t float_numeric_div_pow10 (uint64_t * dbv_buf, int calc_words, int calc_bytes, uint64_t divisor);
static int float_numeric_div_normalize (uint64_t * dbv_buf, int calc_words, int calc_bytes, int exponent);
static void float_numeric_increment (uint64_t * calc_buf, int calc_words, uint64_t val);
static int numeric_operation_compare (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, int calc_bytes);
static int float_numeric_operation_compare (const uint64_t * arg1_word, const uint64_t * arg2_word, int calc_words);
static int float_numeric_check_overflow_and_adjust_scale (int *result_prec, int *result_scale, DB_VALUE * answer);
static int float_numeric_round_and_pack (uint64_t * word_buf, int calc_words, int calc_nbytes,
					 uint8_t * result_buf, int *result_prec, int *result_scale);
static int compare_mantissa_same_exponent (uint64_t * dividend_word, uint64_t * divisor_word,
					   int calc_words, int calc_nbytes, int prec1, int prec2);
static int float_numeric_compare_rem_round_up (const uint64_t * rem, const uint64_t * div, int calc_words);
static inline void numeric_pack_digits4_ascii (char *buf, uint64_t val);
static inline uint64_t numeric_get_uint64_from_be (const void *ptr);
static inline void numeric_put_uint64_to_be (void *ptr, uint64_t val);
static void numeric_bytes_to_words (const uint8_t * src, int src_bytes, uint64_t * dest, int dest_words,
				    int dest_bytes);
static void numeric_words_to_bytes (const uint64_t * src, int src_words, uint8_t * dest);

/*
 * numeric_is_negative () -
 *   return: true, false
 *   arg(in) : DB_VALUE value
 */
static inline bool
numeric_is_negative (const DB_VALUE * value)
{
  assert (value);
  return DB_VALUE_NUMERIC_IS_VALUE_NEGATIVE (value);
}

/*
 * numeric_copy () -
 *   return:
 *   dest(out)  : DB_C_NUMERIC value
 *   source(in) : DB_C_NUMERIC value
 * Note: This routine returns source copied into dest.
 */
static void
numeric_copy (DB_C_NUMERIC dest, DB_C_NUMERIC source)
{
  numeric_copy_long (dest, source, false);
}

/*
 * numeric_copy_long () -
 *   return:
 *   dest(out)  : DB_C_NUMERIC value
 *   source(in) : DB_C_NUMERIC value
 *   is_long_num(in): is long NUMERIC
 * Note: This routine returns source copied into dest.
 */
static void
numeric_copy_long (DB_C_NUMERIC dest, DB_C_NUMERIC source, bool is_long_num)
{
  int num_cnt = 1;

  if (dest != source)
    {
      if (source == NULL || dest == NULL)
	{
	  assert (0);
	  return;
	}

      if (is_long_num)
	{
	  num_cnt = DB_LONG_NUMERIC_MULTIPLIER;
	}
      memcpy (dest, source, DB_NUMERIC_BUF_SIZE * num_cnt);
    }
}

/*
 * numeric_increase () -
 *   return:
 *   answer(in/out) : DB_C_NUMERIC value
 *
 * Note: This routine increments a numeric value.
 */
static void
numeric_increase (DB_C_NUMERIC answer)
{
  numeric_increase_long (answer, false);
}

/*
 * numeric_increase_long () -
 *   return:
 *   answer(in/out) : DB_C_NUMERIC value
 *   is_long_num(in): is long NUMERIC
 *
 * Note: This routine increments a numeric value.
 */
static void
numeric_increase_long (DB_C_NUMERIC answer, bool is_long_num)
{
  int carry = 1;
  int digit;

  if (is_long_num)
    {
      digit = DB_NUMERIC_BUF_SIZE * DB_LONG_NUMERIC_MULTIPLIER - 1;
    }
  else
    {
      digit = DB_NUMERIC_BUF_SIZE - 1;
    }
  /* Loop through answer as long as there is a carry */
  for (; digit >= 0 && carry == 1; digit--)
    {
      answer[digit] += 1;
      carry = (answer[digit] == 0) ? 1 : 0;
    }
}

/*
 * numeric_zero () -
 *   return:
 *   answer(in) : DB_C_NUMERIC value
 *   size(in)   :
 *
 * Note: This routine zeroes out a numeric value and returns the result
 */
static void
numeric_zero (DB_C_NUMERIC answer, int size)
{
  memset (answer, 0, size);	/* sizeof(answer[0]) == 1 */
}

/*
 * numeric_init_pow_of_10_helper()
 *
 * Initialize the powers_of_10 lookup table used for precision checks.
 *
 * Each entry stores 10^n as a multi-precision integer in a uint64_t
 * word array (MSB-first). This matches the internal word-based
 * representation of numeric_buf.
 *
 * The table is used to compare against numeric values in word form
 * to determine precision/scale boundaries, avoiding repeated
 * conversions from byte-based representation.
 *
 * Values are built iteratively (10^n = 10^(n-1) * 10) with carry
 * propagation across words, and the number of significant words
 * is tracked for optimization.
 */
__attribute__ ((constructor))
     static void numeric_init_pow_of_10_helper (void)
{
  int i, j;
  uint64_t carry;
  uint128_t temp;

  memset (powers_of_10, 0, (POW10_MAX_INDEX + 1) * POW10_BUF_WORDS * sizeof (uint64_t));

  /* Set first element to 1 */
  powers_of_10[0][POW10_BUF_WORDS - 1] = 1;

  /* Loop through elements setting each one to 10 times the prior */
  for (i = 1; i < POW10_MAX_INDEX + 1; i++)
    {
      carry = 0;
      for (j = POW10_BUF_WORDS - 1; j >= 0; j--)
	{
	  temp = (uint128_t) powers_of_10[i - 1][j] * 10 + carry;
	  powers_of_10[i][j] = (uint64_t) (temp & 0xFFFFFFFFFFFFFFFFULL);
	  carry = (uint64_t) (temp >> 64);
	}
    }
}

/*
 * numeric_get_pow_of_10()
 *
 * Returns 10^exp as a DB_C_NUMERIC (17-byte big-endian format).
 *
 * The internal powers_of_10 table stores values as multi-precision
 * uint64_t words (MSB-first). This function extracts the least
 * significant 3 words and converts them into the NUMERIC byte layout.
 *
 * On little-endian systems, BSWAP64 is applied to ensure correct
 * byte order.
 */
static void
numeric_get_pow_of_10 (int exp, uint8_t * result)
{
  assert (exp >= 0 && exp <= POW10_MAX_INDEX);

  /* convert word-based (host-endian) powers_of_10 into
   * 17-byte big-endian NUMERIC format */
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN
  const uint64_t *src = &powers_of_10[exp][POW10_BUF_WORDS - NUMERIC_AS_WORDS];
  result[0] = (uint8_t) (src[0] & 0xFF);
  uint64_t temp_word[2];
  temp_word[0] = NUMERIC_BSWAP64 (src[1]);
  temp_word[1] = NUMERIC_BSWAP64 (src[2]);
  memcpy (result + 1, temp_word, sizeof (temp_word));
#else
  memcpy (result, (uint8_t *) powers_of_10[exp] + (POW10_BUF_SIZE - DB_NUMERIC_BUF_SIZE), DB_NUMERIC_BUF_SIZE);
#endif
}

/*
 * numeric_init_digits4_ascii_helper () -
 *   return:
 */
__attribute__ ((constructor))
     static void numeric_init_digits4_ascii_helper (void)
{
  int i;
  uint8_t digit[4];

  for (i = 0; i < 10000; i++)
    {
      digit[0] = '0' + (i / 1000);
      digit[1] = '0' + (i / 100) % 10;
      digit[2] = '0' + (i / 10) % 10;
      digit[3] = '0' + (i % 10);

      _gv_digits4_ascii_lut[i] =
	(uint32_t) digit[0] | ((uint32_t) digit[1] << 8) | ((uint32_t) digit[2] << 16) | ((uint32_t) digit[3] << 24);
    }
}

/*
 * numeric_get_digits4_ascii () - returns pre-calculated ASCII value for a 4-digit number
 *   return : uint32_t containing 4 ASCII bytes
 *   val(in): integer value between 0 and 9999
 *
 * Note: high-performance lookup for 4-byte packed ASCII represented as uint32.
 *       performs lazy initialization if not in SERVER_MODE.
 */
static inline uint32_t
numeric_get_digits4_ascii (uint32_t val)
{
  assert (val < 10000);
  return _gv_digits4_ascii_lut[val];
}

/*
 * float_numeric_find_first_nz_idx_msb() - Find the index of the first non-zero 64-bit word
 *   return        : Index of the first non-zero word,
 *                   or buffer_size if all bytes are zero
 *   buffer(in)    : Byte array stored in MSB-first order
 *   buffer_size(in): Size of the buffer in bytes
 */
static int
float_numeric_find_first_nz_idx_msb (const uint64_t * word_buf, int calc_words)
{
  int i = 0;

  for (i = 0; i < calc_words; i++)
    {
      if (word_buf[i] != 0)
	{
	  return i;
	}
    }
  return calc_words;
}

/*
 * float_numeric_get_decimal_digit()
 *
 * Estimate the number of decimal digits of a multi-precision value
 * stored in a uint64_t word buffer (MSB-first).
 *
 * Steps:
 *   1. Find the most significant non-zero word.
 *   2. Compute bit length of the value.
 *   3. Estimate decimal digits using log10(2):
 *        digits = bits * log10(2)
 *      (approximated with integer arithmetic).
 *   4. Refine the estimate by comparing against powers_of_10 LUT.
 */
static inline int
float_numeric_get_decimal_digit (const uint64_t * word_buf, int calc_words)
{
  /* 1. find first non-zero word (MSB side) */
  int first_nz_idx = float_numeric_find_first_nz_idx_msb (word_buf, calc_words);
  if (first_nz_idx == calc_words)
    {
      return 1;
    }

  /* 2. compute bit length (MSB position in the multi-precision value) */
  uint64_t ms_word = word_buf[first_nz_idx];
  int bits = (calc_words - 1 - first_nz_idx) * 64 + (64 - NUMERIC_CLZ64 (ms_word));

  /*
   * 3. estimate decimal digits from bit length:
   *
   *    a value with 'bits' bits satisfies:
   *      2^(bits-1) <= value < 2^bits
   *
   *    taking log10:
   *      (bits - 1) * log10(2) <= log10(value) < bits * log10(2)
   *
   *    therefore, decimal digit count is:
   *      digits = floor(log10(value)) + 1
   *             = floor((bits - 1) * log10(2)) + 1
   *
   *    using fixed-point approximation:
   *      log10(2) = 0.30103 = 1233 / 4096 (= 0.301025390625)
   *
   *    final integer form:
   *      digits = ((bits - 1) * 1233 / 4096) + 1
   *             = ((bits - 1) * 1233 >> 12) + 1
   *
   *    (right shift replaces division for performance)
   */
  int est_digits = (bits == 0) ? 1 : ((bits - 1) * 1233 >> 12) + 1;

  /* 4. refine using powers_of_10: find smallest idx such that value < 10^idx */
  const int offset = POW10_BUF_WORDS - calc_words;
  for (int idx = est_digits; idx <= POW10_MAX_INDEX; idx++)
    {
      if (float_numeric_operation_compare (word_buf, powers_of_10[idx] + offset, calc_words) < 0)
	{
	  return idx;
	}
    }
  return POW10_MAX_INDEX;
}

/*
 * numeric_double_shift_bit () -
 *   return:
 *   arg1(in)   : DB_C_NUMERIC
 *   arg2(in)   : DB_C_NUMERIC
 *   numbits(in): integer number of bits to shift
 *   lsb(out)   : DB_C_NUMERIC
 *   msb(out)   : DB_C_NUMERIC
 *   is_long_num(in) : is long NUMERIC.
 *
 * Note: This routine returns lsb, msb shifted by numbits from arg1, arg2.
 *       Bits that are shifted out of arg1 are placed into LSB of arg2.
 *       only arg1 and lsb may be long NUMERIC.
 */
static void
numeric_double_shift_bit (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, int numbits, DB_C_NUMERIC lsb, DB_C_NUMERIC msb,
			  bool is_long_num)
{
  /* the largest buf size of DB_C_NUMERIC */
  unsigned char local_arg1[DB_NUMERIC_BUF_SIZE * DB_LONG_NUMERIC_MULTIPLIER];
  unsigned char local_arg2[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */
  unsigned int digit;
  unsigned int buf_size;

  if (is_long_num)
    {
      buf_size = DB_NUMERIC_BUF_SIZE * DB_LONG_NUMERIC_MULTIPLIER;
    }
  else
    {
      buf_size = DB_NUMERIC_BUF_SIZE;
    }

  /* Copy args into local variables */
  numeric_copy_long (local_arg1, arg1, is_long_num);
  numeric_copy (local_arg2, arg2);

  /* Loop through all but last word of msb shifting bits */
  for (digit = 0; digit < DB_NUMERIC_BUF_SIZE - 1; digit++)
    {
      msb[digit] = (local_arg2[digit] << numbits) | (local_arg2[digit + 1] >> (8 - numbits));
    }

  /* Do last word of msb separately using upper word of lsb */
  msb[DB_NUMERIC_BUF_SIZE - 1] = (local_arg2[DB_NUMERIC_BUF_SIZE - 1] << numbits) | (local_arg1[0] >> (8 - numbits));

  /* Loop through all but last word of lsb shifting bits */
  for (digit = 0; digit < buf_size - 1; digit++)
    {
      lsb[digit] = (local_arg1[digit] << numbits) | (local_arg1[digit + 1] >> (8 - numbits));
    }

  /* Do last word of lsb separately.  */
  lsb[buf_size - 1] = local_arg1[buf_size - 1] << numbits;
}

/*
 * numeric_compare_pos () -
 *   return: Integer flag indicating whether arg1 is less than arg2
 *   arg1(in)   : DB_C_NUMERIC
 *   arg2(in)   : DB_C_NUMERIC
 *
 * Note: This routine compares two positive DB_C_NUMERIC values.
 *       This function returns:
 *          -1   if    arg1 < arg2
 *           0   if    arg1 = arg2 and
 *           1   if    arg1 > arg2.
 */
static int
numeric_compare_pos (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2)
{
  unsigned int digit;

  /* Loop through bytes looking for the largest */
  for (digit = 0; digit < DB_NUMERIC_BUF_SIZE; digit++)
    {
      if (arg1[digit] != arg2[digit])
	{
	  return (arg1[digit] > arg2[digit]) ? 1 : (-1);
	}
    }

  /* If all bytes have been compared, then args are equal */
  return (0);
}

/*
 * numeric_shift_byte () -
 *   return:
 *   arg(in)    : DB_C_NUMERIC
 *   numbytes(in): integer number of bytes to shift
 *   answer(out) : DB_C_NUMERIC
 *   length(in) : Length in bytes of answer
 *
 * Note: This routine returns arg shifted by numbytes in answer.  Empty bytes
 *       are zero filled.
 */
static void
numeric_shift_byte (DB_C_NUMERIC arg, int numbytes, DB_C_NUMERIC answer, int length)
{
  int digit;
  int first;
  int last;

  /* Loop through bytes in answer setting to 0 or arg1 */
  first = length - DB_NUMERIC_BUF_SIZE - numbytes;
  last = length - numbytes - 1;
  for (digit = 0; digit < length; digit++)
    {
      if (first <= digit && digit <= last)
	{
	  answer[digit] = arg[digit - first];
	}
      else
	{
	  answer[digit] = 0;
	}
    }
}

/*
 * numeric_is_zero () -
 *   return: bool
 *   arg(in)    : DB_C_NUMERIC
 *
 * Note: This routine checks if arg = 0.
 *       This function returns:
 *           true   if    arg1 = 0 and
 *           false  otherwise.
 */
static bool
numeric_is_zero (DB_C_NUMERIC arg)
{
  unsigned int digit;

  /* Loop through arg's bits looking for non-zero values */
  for (digit = 0; digit < DB_NUMERIC_BUF_SIZE; digit++)
    {
      if (arg[digit] != 0)
	{
	  return (false);
	}
    }

  return (true);
}

/*
 * numeric_classify_magnitude () -
 *   return: magnitude category of arg
 *   arg(in)    : DB_C_NUMERIC
 *   is_value_negative(in): sign of the value
 *
 * Note: Single 17-byte scan locates the most-significant non-zero byte and
 *       classifies the magnitude into ZERO / INT / BIGINT / NUMERIC.
 *       The first non-zero index alone determines the category in most
 *       regions; only the boundary indices (13 for INT, 9 for BIGINT) need
 *       a one-byte threshold check, plus a corner-case check for exactly
 *       -2^31 / -2^63 (magnitude == 0x80...0, negative sign).
 */
static numeric_magnitude_t
numeric_classify_magnitude (DB_C_NUMERIC arg, bool is_value_negative)
{
  int i;

  for (i = 0; i < DB_NUMERIC_BUF_SIZE; i++)
    {
      if (arg[i] != 0)
	{
	  break;
	}
    }

  if (i == DB_NUMERIC_BUF_SIZE)
    {
      return NUMERIC_MAG_ZERO;
    }

  /* INT region: top 13 bytes are zero (i >= 13) */
  if (i > 13)
    {
      /* magnitude < 0x01000000, well within int range */
      return NUMERIC_MAG_INT;
    }
  if (i == 13)
    {
      if (arg[13] < 0x80)
	{
	  /* magnitude <= 0x7FFFFFFF */
	  return NUMERIC_MAG_INT;
	}
      if (arg[13] == 0x80 && is_value_negative && arg[14] == 0 && arg[15] == 0 && arg[16] == 0)
	{
	  /* exactly -2^31 */
	  return NUMERIC_MAG_INT;
	}
      /* exceeds int range, but top 13 bytes are zero so it fits in 8 bytes */
      return NUMERIC_MAG_BIGINT;
    }

  /* BIGINT region: top 9 bytes are zero (i >= 9) */
  if (i > 9)
    {
      return NUMERIC_MAG_BIGINT;
    }
  if (i == 9)
    {
      if (arg[9] < 0x80)
	{
	  return NUMERIC_MAG_BIGINT;
	}
      if (arg[9] == 0x80 && is_value_negative
	  && arg[10] == 0 && arg[11] == 0 && arg[12] == 0
	  && arg[13] == 0 && arg[14] == 0 && arg[15] == 0 && arg[16] == 0)
	{
	  /* exactly -2^63 */
	  return NUMERIC_MAG_BIGINT;
	}
      return NUMERIC_MAG_NUMERIC;
    }

  return NUMERIC_MAG_NUMERIC;
}

/*
 * numeric_overflow () -
 *   return: bool
 *   arg(in)    : DB_C_NUMERIC
 *   exp(in)    : exponent (base 10) of domain
 *
 * Note: This routine checks to see if arg overflows a domain of precision exp.
 */
static bool
numeric_overflow (DB_C_NUMERIC arg, int exp)
{
  uint8_t pow10_buf[DB_NUMERIC_BUF_SIZE];
  numeric_get_pow_of_10 (exp, pow10_buf);
  return (numeric_compare_pos (arg, pow10_buf) >= 0) ? true : false;
}

/*
 * numeric_add () -
 *   return:
 *   arg1(in)   : DB_C_NUMERIC
 *   arg2(in)   : DB_C_NUMERIC
 *   answer(out): DB_C_NUMERIC
 *   size(in)   : int
 *
 * Note: This routine adds two numerics and returns the result.  It assumes
 *       that arg1 and arg2 have the same scaling.
 */
static void
numeric_add (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, int size)
{
  unsigned int answer_bit = 0;
  int digit;

  /* Loop through the characters setting answer */
  for (digit = size - 1; digit >= 0; digit--)
    {
      answer_bit = (arg1[digit] + arg2[digit]) + CARRYOVER (answer_bit);
      answer[digit] = GET_LOWER_BYTE (answer_bit);
    }
}

/*
 * float_numeric_add () -
 *   return: none
 *   dbv1_word(in)   : multi-precision operand 1 (word array)
 *   dbv2_word(in)   : multi-precision operand 2 (word array)
 *   result_word(out): result buffer (word array)
 *   calc_words(in)  : number of words
 *
 * Note: Performs multi-precision addition on MSB-first word arrays.
 *       The computation proceeds from least significant word to most
 *       significant word, propagating carry across words.
 */
static void
float_numeric_add (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word, int calc_words)
{
  uint64_t carry = 0;
  int digit;
  uint128_t sum;

  for (digit = calc_words - 1; digit >= 0; digit--)
    {
      sum = (uint128_t) dbv1_word[digit] + dbv2_word[digit] + carry;
      result_word[digit] = (uint64_t) sum;
      carry = (uint64_t) (sum >> 64);
    }
}

/*
 * float_numeric_add_fast () -
 *   return: estimated decimal digit count of result
 *   dbv1_word(in)    : operand 1 (word array)
 *   dbv2_word(in)    : operand 2 (word array)
 *   result_word(out) : result buffer (word array)
 *   calc_words(in)   : number of words (expected 3 for fast path)
 *   dbv1_sign(in)    : sign of operand 1
 *   dbv2_sign(in)    : sign of operand 2
 *   result_sign(out) : sign of result
 *   result_buf(out)  : result in 17-byte NUMERIC format
 *
 * Note: Optimized path for 3-word numeric values.
 *       - If signs are equal, performs addition.
 *       - If signs differ, performs subtraction based on magnitude.
 *       - Uses platform-specific fast instructions when available.
 *       The result is converted to byte format and its decimal digit
 *       count is returned.
 */
static int
float_numeric_add_fast (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word, int calc_words,
			bool dbv1_sign, bool dbv2_sign, bool * result_sign, uint8_t * result_buf)
{
  if (dbv1_sign == dbv2_sign)
    {
      *result_sign = dbv1_sign;
      uint64_t sum = _addcarry_u64 (0, dbv1_word[2], dbv2_word[2], (unsigned long long *) &result_word[2]);
      result_word[1] = sum;
    }
  else
    {
      if (dbv1_word[2] >= dbv2_word[2])
	{
	  *result_sign = dbv1_sign;
	  (void) _subborrow_u64 (0, dbv1_word[2], dbv2_word[2], (unsigned long long *) &result_word[2]);
	}
      else
	{
	  *result_sign = dbv2_sign;
	  (void) _subborrow_u64 (0, dbv2_word[2], dbv1_word[2], (unsigned long long *) &result_word[2]);
	}
    }

  numeric_words_to_bytes (result_word, calc_words, result_buf);

  return float_numeric_get_decimal_digit (result_word, calc_words);
}

/*
 * numeric_sub () -
 *   return:
 *   arg1(in)   : DB_C_NUMERIC
 *   arg2(in)   : DB_C_NUMERIC
 *   answer(out): DB_C_NUMERIC
 *   size(in)   : int
 *
 * Note: This routine subtracts arg2 from arg1 returns the result.
 *       It assumes that arg1 and arg2 have the same scaling.
 */
static void
numeric_sub (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, int size)
{
  unsigned int borrow = 0;
  unsigned int next_borrow = 0;
  int digit;

  for (digit = size - 1; digit >= 0; digit--)
    {
      next_borrow = BORROW_NEXT (arg1[digit], arg2[digit], borrow);
      answer[digit] = (uint8_t) ((unsigned) arg1[digit] - ((unsigned) arg2[digit] + borrow));
      borrow = next_borrow;
    }
}

/*
 * float_numeric_sub () -
 *   return: none
 *   arg1_word(in)   : multi-precision operand 1 (word array)
 *   arg2_word(in)   : multi-precision operand 2 (word array)
 *   result_word(out): result buffer (word array)
 *   calc_words(in)  : number of words
 *
 * Note: Performs multi-precision subtraction (arg1 - arg2) on
 *       MSB-first word arrays. The computation proceeds from least
 *       significant word to most significant word, propagating
 *       borrow across words.
 */
static void
float_numeric_sub (const uint64_t * arg1_word, const uint64_t * arg2_word, uint64_t * result_word, int calc_words)
{
  uint64_t borrow = 0;
  int digit;
  uint128_t diff;

  for (digit = calc_words - 1; digit >= 0; digit--)
    {
      diff = (uint128_t) arg1_word[digit] - arg2_word[digit] - borrow;
      result_word[digit] = (uint64_t) diff;
      borrow = (diff >> 64) ? 1 : 0;
    }
}

/*
 * float_numeric_sub_fast () -
 *   return: estimated decimal digit count of result
 *   dbv1_word(in)    : operand 1 (word array)
 *   dbv2_word(in)    : operand 2 (word array)
 *   result_word(out) : result buffer (word array)
 *   calc_words(in)   : number of words (expected 3 for fast path)
 *   dbv1_sign(in)    : sign of operand 1
 *   dbv2_sign(in)    : sign of operand 2
 *   result_sign(out) : sign of result
 *   result_buf(out)  : result in 17-byte NUMERIC format
 *
 * Note: Implements subtraction by reusing the fast addition path:
 *       A - B is transformed into A + (-B) by flipping the sign of
 *       the second operand.
 */
static int
float_numeric_sub_fast (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word, int calc_words,
			bool dbv1_sign, bool dbv2_sign, bool * result_sign, uint8_t * result_buf)
{
  return float_numeric_add_fast (dbv1_word, dbv2_word, result_word, calc_words,
				 dbv1_sign, !dbv2_sign, result_sign, result_buf);
}

/*
 * numeric_mul () -
 *   return:
 *   a1(in)     : DB_C_NUMERIC
 *   a2(in)     : DB_C_NUMERIC
 *   positive_ans(out): bool if the answer's is positive (true)
 *                      or negative (false)
 *   answer(out) : DB_C_NUMERIC
 *   is_value_negative(out): sign of the answer value
 *
 * Note: This routine multiplies two numerics and returns the results.
 */
static void
numeric_mul (DB_C_NUMERIC a1, DB_C_NUMERIC a2, DB_C_NUMERIC answer, bool * is_value_negative)
{
  unsigned int answer_bit;
  int digit1;
  int digit2;
  int shift;
  unsigned char temp_term[2 * DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char temp_arg1[2 * DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char temp_arg2[2 * DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char *arg1;		/* copy of DB_C_NUMERIC */
  unsigned char *arg2;		/* copy of DB_C_NUMERIC */

  assert (is_value_negative);

  /* Initialize the answer */
  numeric_zero (answer, 2 * DB_NUMERIC_BUF_SIZE);

  /* Check if either arg = 0 */
  if (numeric_is_zero (a1) || numeric_is_zero (a2))
    {
      *is_value_negative = false;
      return;
    }

  /* If arg1 is negative, toggle sign and make arg1 positive */
  arg1 = a1;
  arg2 = a2;

  /* Initialize temporary variables */
  numeric_zero (temp_arg2, DB_NUMERIC_BUF_SIZE);
  numeric_copy (temp_arg2 + DB_NUMERIC_BUF_SIZE, arg2);

  /* Loop through the 8-bit digits of temp_arg2 */
  shift = 0;
  for (digit2 = (2 * DB_NUMERIC_BUF_SIZE) - 1; digit2 >= 0; digit2--)
    {
      if (temp_arg2[digit2] != 0)
	{
	  answer_bit = 0;
	  numeric_shift_byte (arg1, shift, temp_arg1, 2 * DB_NUMERIC_BUF_SIZE);

	  /* Loop through the 8-bit digits of temp_arg1 */
	  for (digit1 = (2 * DB_NUMERIC_BUF_SIZE - 1); digit1 >= 0; digit1--)
	    {
	      /* the unsigned int casts are necessary here to avoid 16 bit integer overflow during the multiplication
	       * on PC's */
	      answer_bit =
		((unsigned int) temp_arg1[digit1] * (unsigned int) temp_arg2[digit2]) +
		(unsigned int) CARRYOVER (answer_bit);
	      temp_term[digit1] = GET_LOWER_BYTE (answer_bit);
	    }
	  numeric_add (temp_term, answer, answer, 2 * DB_NUMERIC_BUF_SIZE);
	}
      shift++;
    }
}

/*
 * float_numeric_mul () -
 *   return: none
 *   dbv1_word(in)   : operand 1 (word array)
 *   dbv2_word(in)   : operand 2 (word array)
 *   result_word(out): result buffer (word array)
 *   calc_words(in)  : number of words
 *   calc_nbytes(in) : number of bytes (used in non-128bit path)
 *
 * Note: Performs multi-precision multiplication using the classical
 *       O(n^2) algorithm (schoolbook method).
 *       Partial products are accumulated from least significant word
 *       to most significant word, with carry propagation across words.
 */
static void
float_numeric_mul (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word, int calc_words,
		   int calc_nbytes)
{
  int outer_idx, inner_idx, result_idx, carry_idx;
  int inner_min;
  uint64_t carry;
  uint128_t product, sum;

  const int last = calc_words - 1;
  /* outer: iterate dbv1 from LSW → MSW */
  for (outer_idx = last; outer_idx >= 0; outer_idx--)
    {
      if (dbv1_word[outer_idx] == 0)
	{
	  continue;
	}

      carry = 0;
      inner_min = last - outer_idx;
      /* inner: accumulate partial products into result */
      for (inner_idx = last; inner_idx >= inner_min; inner_idx--)
	{
	  result_idx = outer_idx + inner_idx - last;
	  product = (uint128_t) dbv1_word[outer_idx] * (uint128_t) dbv2_word[inner_idx];
	  sum = (uint128_t) result_word[result_idx] + (uint64_t) product + carry;
	  result_word[result_idx] = (uint64_t) sum;
	  carry = (uint64_t) (sum >> 64) + (uint64_t) (product >> 64);
	}

      /* propagate remaining carry to higher words */
      if (carry)
	{
	  carry_idx = (outer_idx + inner_min - last) - 1;
	  while (carry && carry_idx >= 0)
	    {
	      sum = (uint128_t) result_word[carry_idx] + carry;
	      result_word[carry_idx] = (uint64_t) sum;
	      carry = (uint64_t) (sum >> 64);
	      --carry_idx;
	    }
	}
    }
}

/*
 * float_numeric_mul_fast () -
 *   return: estimated decimal digit count of result
 *   dbv1_word(in)    : operand 1 (word array)
 *   dbv2_word(in)    : operand 2 (word array)
 *   result_word(out) : result buffer (word array)
 *   calc_words(in)   : number of words (expected 3 for fast path)
 *   result_buf(out)  : result in 17-byte NUMERIC format
 *
 * Note: Optimized multiplication for 64-bit operands (LSW only).
 *       Computes a 128-bit product and stores it into the lower
 *       two words of the result. The result is then converted to
 *       byte format and its decimal digit count is returned.
 */
static int
float_numeric_mul_fast (const uint64_t * dbv1_word, const uint64_t * dbv2_word, uint64_t * result_word, int calc_words,
			uint8_t * result_buf, int *result_scale)
{
  /* fast path: multiply least significant 64-bit words (word[2]) -> 128-bit result */
  uint128_t prod = (uint128_t) dbv1_word[2] * dbv2_word[2];

  result_word[2] = (uint64_t) prod;
  result_word[1] = (uint64_t) (prod >> 64);

  if (*result_scale > DB_MAX_NUMERIC_SCALE)
    {
      int scale_overflow = *result_scale - DB_MAX_NUMERIC_SCALE;
      *result_scale = DB_MAX_NUMERIC_SCALE;

      if (float_numeric_div_normalize (result_word, calc_words, NUMERIC_GET_BYTE_COUNT (calc_words), scale_overflow) >=
	  ROUND_HALF_UP_DIGIT)
	{
	  (void) float_numeric_increment (result_word, calc_words, 1);
	}
    }

  numeric_words_to_bytes (result_word, calc_words, result_buf);

  return float_numeric_get_decimal_digit (result_word, calc_words);
}

/*
 * numeric_long_div () -
 *   return:
 *   a1(in)     : DB_C_NUMERIC             (numerator)
 *   a2(in)     : DB_C_NUMERIC             (denominator)
 *   answer(in) : DB_C_NUMERIC
 *   remainder(in)      : DB_C_NUMERIC
 *   is_long_num(in)    : is a1 and answer is long NUMERIC
 *
 * Note: This routine divides two numeric values and returns the
 *       result and remainder.  This algorithm is based on the algorithm in
 *       "<Mark's Book>".
 *       Only a1(the dividend) and answer(the quotient) can be long numeric.
 */
static void
numeric_long_div (DB_C_NUMERIC a1, DB_C_NUMERIC a2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder, bool is_long_num)
{
  unsigned int nbit, total_bit;
  unsigned int buf_size;
  /* the largest buf size for DB_C_NUMERIC */
  unsigned char arg1[DB_LONG_NUMERIC_MULTIPLIER * DB_NUMERIC_BUF_SIZE];
  unsigned char arg2[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */
  unsigned char neg_arg2[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */

  /* calculate basic variables */
  if (is_long_num)
    {
      buf_size = DB_NUMERIC_BUF_SIZE * DB_LONG_NUMERIC_MULTIPLIER;
    }
  else
    {
      buf_size = DB_NUMERIC_BUF_SIZE;
    }

  total_bit = buf_size * 8;

  /* Copy inputs to local variables */
  numeric_copy_long (arg1, a1, is_long_num);
  numeric_copy (arg2, a2);

  /* Initialize variables */
  numeric_coerce_int_to_num (0, remainder, NULL);
  numeric_copy_long (answer, arg1, is_long_num);

  /* Shift *answer and *remainder.  Bits shifted out of *answer * are placed into *remainder.  */
    /*****  NEEDS TO BE UPGRADED TO SHIFT SO THAT FIRST NON-ZERO BIT OF *****/
    /*****  REMAINDER IS AT LEAST EQUAL TO FIRST NON_ZERO BIT OF ARG2.  *****/
    /*****  DON'T DO THIS ONE BIT AT A TIME.                            *****/
  for (nbit = 0; nbit < total_bit; nbit++)
    {
      numeric_double_shift_bit (answer, remainder, 1, answer, remainder, is_long_num);

      /* If remainder >= arg2, subtract arg2 from remainder and increment the answer.  */
      if (numeric_compare_pos (remainder, arg2) >= 0)
	{
	  numeric_sub (remainder, arg2, remainder, DB_NUMERIC_BUF_SIZE);
	  answer[buf_size - 1] += 1;
	}
    }
}

/*
 * numeric_div () -
 *   return:
 *   arg1(in)   : DB_C_NUMERIC             (numerator)
 *   arg2(in)   : DB_C_NUMERIC             (denominator)
 *   answer(in) : DB_C_NUMERIC
 *   remainder(in)      : DB_C_NUMERIC
 *   arg1_value_is_negative(in): sign of the arg1 value
 *   arg2_value_is_negative(in): sign of the arg2 value
 *
 * Note: This routine divides two numeric values and returns
 *       the result and remainder.  The division is broken down into 5 cases.
 *       Given arg1/arg2:
 *       a) if arg2 = 0, then SIGFPE ??, +/- MAX_NUM_DATA ??
 *       b) if arg1 = 0, then answer = remainder = 0
 *       c) if arg1, arg2 can be represented as a int
 *                       then answer = arg1/arg2,  remainder = arg1%arg2
 *       d) Otherwise, perform long division
 */
static void
numeric_div (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder,
	     bool arg1_value_is_negative, bool arg2_value_is_negative)
{
  numeric_magnitude_t divisor_mag = numeric_classify_magnitude (arg2, arg2_value_is_negative);

  /* Case 1 - arg2 = 0 */
  if (divisor_mag == NUMERIC_MAG_ZERO)
    {
      /* SIGFPE ??, +/- MAX_NUM_DATA ?? */
      return;
    }

  numeric_magnitude_t dividend_mag = numeric_classify_magnitude (arg1, arg1_value_is_negative);

  /* Case 2 - arg1 = 0.  Set answer and remainder to 0.  */
  if (dividend_mag == NUMERIC_MAG_ZERO)
    {
      numeric_coerce_int_to_num (0, remainder, NULL);
      numeric_coerce_int_to_num (0, answer, NULL);
    }

  /* Case 3 - arg1, arg2 are long ints. Do machine divide */
  else if (dividend_mag == NUMERIC_MAG_INT && divisor_mag == NUMERIC_MAG_INT)
    {
      int int_arg1, int_arg2;

      numeric_coerce_num_to_int (arg1, &int_arg1, arg1_value_is_negative);
      numeric_coerce_num_to_int (arg2, &int_arg2, arg2_value_is_negative);
      numeric_coerce_int_to_num ((int_arg1 / int_arg2), answer, NULL);
      numeric_coerce_int_to_num ((int_arg1 % int_arg2), remainder, NULL);
    }

  /* Case 4 - arg1, arg2 fit in DB_BIGINT. Do machine divide.
   * ZERO is already handled above; INT range is a subset of BIGINT range,
   * so any (INT|BIGINT) x (INT|BIGINT) combination fits in DB_BIGINT and
   * only NUMERIC needs to be excluded. */
  else if (dividend_mag != NUMERIC_MAG_NUMERIC && divisor_mag != NUMERIC_MAG_NUMERIC)
    {
      DB_BIGINT bi_arg1, bi_arg2;

      numeric_coerce_num_to_bigint (arg1, 0, &bi_arg1, arg1_value_is_negative);
      numeric_coerce_num_to_bigint (arg2, 0, &bi_arg2, arg2_value_is_negative);
      numeric_coerce_bigint_to_num ((bi_arg1 / bi_arg2), answer, NULL);
      numeric_coerce_bigint_to_num ((bi_arg1 % bi_arg2), remainder, NULL);
    }

  /* Default case: perform long division */
  else
    {
      numeric_long_div (arg1, arg2, answer, remainder, false);
    }
}

/*
 * float_numeric_div_fast () -
 *   dividend_val(in)     : 64-bit dividend
 *   divisor_val(in)      : 64-bit divisor
 *   prec1, scale1(in)    : precision/scale of dividend
 *   prec2, scale2(in)    : precision/scale of divisor
 *   exponent_diff(in)    : exponent difference (base-10)
 *   result_buf(out)      : result in 17-byte NUMERIC format
 *   result_prec_out(out) : resulting precision
 *   result_scale_out(out): resulting scale
 *
 * Note: Fast division path using __int128.
 *       Uses 64-bit mantissa with multi-precision scaling.
 *       The process consists of normalization, MSW-first division, and rounding.
 */
static void
float_numeric_div_fast (uint64_t dividend_val, uint64_t divisor_val,
			int prec1, int scale1, int prec2, int scale2,
			int exponent_diff, uint8_t * result_buf, int *result_prec_out, int *result_scale_out,
			bool * result_sign)
{
  int i;
  int result_prec, result_scale, result_digits, exponent10;
  int word_count = NUMERIC_AS_WORDS + 1;
  int word_bytes = NUMERIC_AS_WORD_BYTES + 8;
  uint128_t temp;

  /* use 4-word buffer to safely handle scaling (up to ~77 digits) */
  uint64_t dividend_word[word_count] = { 0, 0, 0, dividend_val };
  uint64_t quotient_word[word_count] = { 0 };
  uint64_t remainder_word = 0;

  /* 1) compare mantissa */
  int mantissa_compare = -1;
  int prec_diff = prec1 - prec2;
  if (prec_diff > 0)
    {
      /* align divisor to dividend precision; use uint128_t to prevent overflow (19 digits * 10^k) */
      uint128_t scaled_divisor = (uint128_t) divisor_val * _gv_mul_normalize_pow10_lookup[prec_diff - 1];
      mantissa_compare = (dividend_val >= scaled_divisor) ? 1 : -1;
    }
  else if (prec_diff < 0)
    {
      /* align dividend to divisor precision; use uint128_t to prevent overflow (19 digits * 10^k) */
      uint128_t scaled_dividend = (uint128_t) dividend_val * _gv_mul_normalize_pow10_lookup[-prec_diff - 1];
      mantissa_compare = (scaled_dividend >= divisor_val) ? 1 : -1;
    }
  else
    {
      mantissa_compare = (dividend_val >= divisor_val) ? 1 : -1;
    }

  /* 2) mantissa calculations */
  result_digits = (mantissa_compare >= 0) ? (exponent_diff + 1) : exponent_diff;
  result_scale = DB_MAX_NUMERIC_PRECISION - result_digits;
  exponent10 = result_scale + (scale2 - scale1);
  if (result_scale > DB_MAX_NUMERIC_SCALE)
    {
      int scale_overflow = DB_MAX_NUMERIC_SCALE + result_digits;
      exponent10 -= (DB_MAX_NUMERIC_PRECISION - scale_overflow);
      result_scale = DB_MAX_NUMERIC_SCALE;
    }

  /* 3) scale the dividend (normalization). exponent10 > 0 shifts up, < 0 truncates */
  if (exponent10 > 0)
    {
      float_numeric_mul_normalize (dividend_word, word_count, word_bytes, exponent10);
    }
  else if (exponent10 < 0)
    {
      /* reduces digits for normalization; does not perform rounding */
      (void) float_numeric_div_normalize (dividend_word, word_count, word_bytes, -exponent10);
    }

  /* 4) division */
  for (i = 0; i < word_count; i++)
    {
      temp = ((uint128_t) remainder_word << 64) | dividend_word[i];
      quotient_word[i] = (uint64_t) (temp / divisor_val);
      remainder_word = (uint64_t) (temp % divisor_val);
    }

  /* 5) round up if necessary */
  if (((uint128_t) remainder_word * 2) >= divisor_val)
    {
      float_numeric_increment (quotient_word, word_count, 1);
    }

  /* 6) round and pack to DB_NUMERIC_BUF_SIZE bytes */
  result_prec = float_numeric_get_decimal_digit (quotient_word, word_count);
  if (*result_sign && result_prec == 1 && quotient_word[word_count - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      *result_sign = false;
    }
  /*
   * Ignoring the return value here is intentional: any scale boundary overflow
   * from round-carry is surfaced by the caller's float_numeric_check_overflow_and_adjust_scale().
   */
  (void) float_numeric_round_and_pack (quotient_word, word_count, word_bytes, result_buf, &result_prec, &result_scale);
  *result_prec_out = result_prec;
  *result_scale_out = result_scale;
}

/*
 * float_numeric_mod_fast () -
 *   dividend_val(in)     : 64-bit dividend
 *   divisor_val(in)      : 64-bit divisor
 *   result_buf(out)      : result in 17-byte NUMERIC format
 *   result_prec_out(out) : resulting precision
 *   result_scale_out(in/out): scale (preserved)
 *
 * Note: Fast modulo path using __int128.
 *       Computes remainder using 64-bit arithmetic and stores it
 *       in NUMERIC format.
 */
static void
float_numeric_mod_fast (uint64_t dividend_val, uint64_t divisor_val,
			uint8_t * result_buf, int *result_prec_out, int *result_scale_out, bool * result_sign)
{
  assert (divisor_val != 0);

  int result_scale = *result_scale_out;
  uint64_t remainder = dividend_val % divisor_val;

  /*
   * although the remainder is 64-bit (8 bytes),
   * it must be stored in a 3-word (192-bit) buffer to conform to
   * CUBRID's 17-byte NUMERIC layout.
   *
   * due to numeric_words_to_bytes() mapping, the least significant
   * 8 bytes correspond to word[2], so the remainder is placed there.
   */
  uint64_t result_word[NUMERIC_AS_WORDS] = { 0, 0, remainder };

  int result_prec = float_numeric_get_decimal_digit (result_word, NUMERIC_AS_WORDS);
  if (*result_sign && result_prec == 1 && result_word[NUMERIC_AS_WORDS - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      *result_sign = false;
    }
  /*
   * Ignoring the return value here is intentional: any scale boundary overflow
   * from round-carry is surfaced by the caller's float_numeric_check_overflow_and_adjust_scale().
   */
  (void) float_numeric_round_and_pack (result_word, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES,
				       result_buf, &result_prec, &result_scale);
  *result_prec_out = result_prec;
  *result_scale_out = result_scale;
}

/*
 * knuth_find_first_nz_idx() - Find the index of the first non-zero word (MSB-first)
 *   return          : Index of the first non-zero word, or total_digits if all are zero
 *   digit_buf(in)   : Array of knuth digits stored in MSB-first order
 *   total_digits(in): Total number of words in the buffer
 */
static int
knuth_find_first_nz_idx (const knuth_digit_t * digit_buf, int total_digits)
{
  int i;

  for (i = 0; i < total_digits; i++)
    {
      if (digit_buf[i] != 0)
	{
	  return i;
	}
    }
  return total_digits;
}

/*
 * knuth_count_leading_zero_bits() - Count the number of leading zero bits in a knuth digit
 *   return        : Number of leading zero bits (0 ~ KNUTH_DIGIT_BITS)
 *   value(in) : Word (32-bit or 64-bit) to be examined
 *
 * Note : To quickly determine how many bits the divisor V
 *        must be shifted left so that its most significant
 *        digit (MSD) becomes normalized (MSB = 1).
 */
static unsigned int
knuth_count_leading_zero_bits (knuth_digit_t value)
{
  if (value == 0)
    {
      return KNUTH_DIGIT_BITS;
    }

  return (unsigned int) NUMERIC_CLZ (value);
}

/*
 * knuth_normalize_left_shift_msb() - Left shift an MSB-first word buffer by k bits
 *   return        : void
 *   buffer(in/out): Word array stored in MSB-first order (modified in place)
 *   buffer_size(in): Number of words in the buffer
 *   k_bit(in)     : Number of bits to shift (0..KNUTH_DIGIT_BITS-1)
 *
 * Note : Used in the normalization step of Knuth’s division algorithm
 *        to align the divisor or dividend at the bit level.
 */
static void
knuth_normalize_left_shift_msb (knuth_digit_t * buffer, int buffer_size, unsigned int k_bit)
{
  if (k_bit == 0)
    {
      return;
    }

  int i;
  for (i = 0; i < buffer_size - 1; i++)
    {
      buffer[i] = (buffer[i] << k_bit) | (buffer[i + 1] >> (KNUTH_DIGIT_BITS - k_bit));
    }
  buffer[buffer_size - 1] <<= k_bit;
}

/*
 * knuth_normalize_right_shift_msb() - Right shift an MSB-first word buffer by k bits
 *   return        : void
 *   buffer(in/out): Word array stored in MSB-first order (modified in place)
 *   buffer_size(in): Number of words in the buffer
 *   k_bit(in)     : Number of bits to shift (0..KNUTH_DIGIT_BITS-1)
 *
 * Note: Used in the denormalization step of Knuth’s division algorithm,
 *       when restoring the remainder by shifting back.
 */
static void
knuth_normalize_right_shift_msb (knuth_digit_t * buffer, int buffer_size, unsigned int k_bit)
{
  if (k_bit == 0)
    {
      return;
    }

  knuth_digit_t carry = 0;
  knuth_digit_t next = 0;
  int i = 0;

  for (i = 0; i < buffer_size; i++)
    {
      next = buffer[i] & (((knuth_digit_t) 1 << k_bit) - 1);
      buffer[i] = (buffer[i] >> k_bit) | (carry << (KNUTH_DIGIT_BITS - k_bit));
      carry = next;
    }
}

/*
 * knuth_estimate_quotient_digit() - Knuth’s division algorithm, steps D2–D3: trial quotient estimation and adjustment
 *   return             : Final trial_quotient (0..KNUTH_BASE-1)
 *   u_work(in)         : Dividend work array (MSB-first)
 *   v_work(in)         : Divisor work array (MSB-first)
 *   window_offset(in)  : Starting offset in dividend for quotient estimation
 *   dividend_words(in) : Size of dividend in words
 *   divisor_words(in)  : Size of divisor in words
 *   out_trial_remainder(out): Trial remainder after estimation
 *
 * D2 (Estimation):
 *   - Use the top 2 digits of U (u_high, u_next) to form a double-length numerator
 *   - Compute trial_quotient = numerator / v_high
 *   - Compute trial_remainder = numerator % v_high
 *
 * D3 (Adjustment):
 *   - While trial_quotient * v_next > trial_remainder * KNUTH_BASE + u_next2,
 *     decrement trial_quotient by 1 and update trial_remainder
 *   - Repeat until the condition is satisfied
 *
 * Note:
 *   - The trial_quotient may be as large as (2 * KNUTH_BASE - 1), but each quotient digit must lie in [0..KNUTH_BASE-1].
 *     Therefore, if >= KNUTH_BASE, it is clamped to KNUTH_BASE - 1.
 *   - This step is essential in Knuth’s division to ensure each quotient digit is safely and correctly determined.
 */
static knuth_digit_t
knuth_estimate_quotient_digit (const knuth_digit_t * u_work, const knuth_digit_t * v_work, int window_offset,
			       int dividend_words, int divisor_words, knuth_digit_t * out_trial_remainder)
{
  knuth_digit_t u_high, u_next, u_next2;
  knuth_digit_t v_high, v_next;
  knuth_double_digit_t trial_quotient, trial_remainder;
  knuth_double_digit_t numerator;

  /* extract the top 3 words from U and top 2 words from V */
  u_high = u_work[window_offset];
  u_next = u_work[window_offset + 1];
  u_next2 = (window_offset + 2 < dividend_words + 1) ? u_work[window_offset + 2] : 0;

  v_high = v_work[0];
  v_next = (divisor_words > 1) ? v_work[1] : 0;

  /* D2: quotient digit estimation */
  numerator = ((knuth_double_digit_t) u_high << KNUTH_DIGIT_BITS) | (knuth_double_digit_t) u_next;
  trial_quotient = numerator / v_high;	// may be as large as (2 * KNUTH_BASE - 1)
  trial_remainder = numerator % v_high;	// ranges up to v_high - 1

  if (divisor_words > 1)
    {
      /* D3: quotient correction */
      while ((trial_quotient == KNUTH_BASE) || (trial_quotient * v_next > (trial_remainder * KNUTH_BASE + u_next2)))
	{
	  --trial_quotient;
	  trial_remainder += v_high;
	  if (trial_remainder >= KNUTH_BASE)
	    {
	      break;
	    }
	}
    }

  if (trial_quotient >= KNUTH_BASE)
    {
      trial_quotient = KNUTH_BASE - 1;
    }

  *out_trial_remainder = (knuth_digit_t) trial_remainder;
  return (knuth_digit_t) trial_quotient;
}

/*
 * knuth_multiply_and_subtract() 
 *   - Knuth’s division algorithm, steps D4–D6: apply q·V to the dividend window U by subtraction, and perform add-back if needed
 *   return             : Final q (decremented by 1 if add-back occurred)
 *   u_work(in/out)     : Dividend work array U (MSB-first, modified in place)
 *   v_work(in)         : Divisor work array V (MSB-first)
 *   window_offset(in)  : Starting offset of the U window to be updated
 *   divisor_words(in)  : Number of words in the divisor V
 *   trial_quotient(in) : Trial quotient estimated in D2–D3
 *
 * D4 (Multiply & Subtract):
 *   - Compute trial_quotient * V
 *   - Subtract it from U window while propagating carry and borrow
 *
 * D5 (Update topmost word & Test remainder):
 *   - Apply remaining carry and borrow to the topmost word of the U window
 *   - Check if a borrow occurred (result is negative)
 *
 * D6 (Add-back correction):
 *   - If borrow occurred in the topmost word, decrement trial_quotient (q-1)
 *   - Add V back to the U window to restore the remainder
 *
 * Note:
 *   - At most one add-back is needed
 */
static knuth_digit_t
knuth_multiply_and_subtract (knuth_digit_t * u_work, const knuth_digit_t * v_work, int window_offset, int divisor_words,
			     knuth_digit_t trial_quotient)
{
  knuth_double_digit_t product;
  knuth_digit_t carry = 0;
  knuth_digit_t borrow = 0;
  knuth_digit_t temp_u;
  knuth_digit_t sub_val;
  int i;

  /* D4: multiply & subtract */
  for (i = divisor_words - 1; i >= 0; i--)
    {
      product = (knuth_double_digit_t) trial_quotient *v_work[i] + carry;
      carry = (knuth_digit_t) (product >> KNUTH_DIGIT_BITS);

      temp_u = u_work[window_offset + i + 1];
      sub_val = (knuth_digit_t) product;

      if ((knuth_double_digit_t) temp_u < (knuth_double_digit_t) sub_val + borrow)
	{
	  u_work[window_offset + i + 1] = temp_u - sub_val - borrow;
	  borrow = 1;
	}
      else
	{
	  u_work[window_offset + i + 1] = temp_u - sub_val - borrow;
	  borrow = 0;
	}
    }

  /* D5: update topmost word & test remainder */
  if (u_work[window_offset] < (knuth_double_digit_t) carry + borrow)
    {
      /* D6: add-back correction */
      u_work[window_offset] = u_work[window_offset] - carry - borrow;

      trial_quotient--;

      knuth_double_digit_t sum;
      knuth_digit_t add_carry = 0;
      for (i = divisor_words - 1; i >= 0; i--)
	{
	  sum = (knuth_double_digit_t) u_work[window_offset + i + 1] + v_work[i] + add_carry;
	  u_work[window_offset + i + 1] = (knuth_digit_t) sum;
	  add_carry = (knuth_digit_t) (sum >> KNUTH_DIGIT_BITS);
	}
      u_work[window_offset] += add_carry;
    }
  else
    {
      /* No add-back needed, just subtract remaining carry/borrow from U[j] */
      u_work[window_offset] -= (carry + borrow);
    }

  return trial_quotient;
}

/*
 * float_numeric_knuth_div() - Divide two big-endian numeric word buffers using
 *                             Knuth's long division algorithm (steps D1–D8)
 *   dbv1_buf(in) : Dividend buffer (MSB-first)
 *   dbv2_buf(in) : Divisor buffer (MSB-first)
 *   quo_buf(out) : Quotient buffer (MSB-first, right-justified)
 *   rem_buf(out) : Remainder buffer (MSB-first, right-justified)
 *   calc_words(in): Working precision in words
 *
 * Note :
 *   1) compute significant (non-zero) word lengths (not part of Knuth D-steps)
 *   2) prepare working buffers
 *   3) D1 normalize
 *   4) D2–D7 main loop (per quotient digit)
 *      - D2–D3: estimate and adjust trial quotient
 *      - D4–D6: subtract q*V from U window. if borrow, add back
 *      - D7: store the finalized quotient digit
 *   5) D8 denormalize
 *   6) extract remainder
 */
static void
float_numeric_knuth_div (knuth_digit_t * dbv1_buf, knuth_digit_t * dbv2_buf, knuth_digit_t * quo_buf,
			 knuth_digit_t * rem_buf, knuth_digit_t calc_words)
{
  int dividend_first_nz_index = 0;
  int divisor_first_nz_index = 0;
  int dividend_words = 0;
  int divisor_words = 0;
  int len_diff = 0;

  /*
   * 1) Compute significant (non-zero) word lengths (not part of Knuth D-steps)
   * - Risk if we normalize (D1) directly on the MSB-first buffer:
   *   Skipping leading zero words means shifting in multiples of KNUTH_DIGIT_BITS,
   *   which scales the value by KNUTH_BASE^k and distorts it.
   * - First find the index of the first non-zero word as the proper base.
   */
  dividend_first_nz_index = knuth_find_first_nz_idx (dbv1_buf, calc_words);
  divisor_first_nz_index = knuth_find_first_nz_idx (dbv2_buf, calc_words);
  dividend_words = calc_words - dividend_first_nz_index;
  divisor_words = calc_words - divisor_first_nz_index;

  /*
   * early exit: if dividend < divisor -> quotient = 0, remainder = dividend
   * - First compare by significant byte length
   * - If equal, compare the same-length region with the appropriate compare helper
   *   with float_numeric_operation_compare()
   */
  len_diff = dividend_words - divisor_words;
  if (len_diff < 0
      || (len_diff == 0
	  && float_numeric_operation_compare (dbv1_buf + dividend_first_nz_index, dbv2_buf + divisor_first_nz_index,
					      divisor_words) < 0))
    {
      /* quo_buf is already zero-initialized outside */
      memcpy (rem_buf, dbv1_buf, calc_words * sizeof (knuth_digit_t));
      return;
    }

  /* 2) prepare working buffers */
  // U: dividend working buffer (n+1 words, with leading zero space)
  knuth_digit_t u_work[dividend_words + 1];
  u_work[0] = 0;
  // V: divisor working buffer (n words)
  knuth_digit_t v_work[divisor_words];

  memcpy (u_work + 1, dbv1_buf + dividend_first_nz_index, dividend_words * sizeof (knuth_digit_t));
  memcpy (v_work, dbv2_buf + divisor_first_nz_index, divisor_words * sizeof (knuth_digit_t));

  /* 3) D1 normalize */
  unsigned int normalization_bit_count = (divisor_words > 0) ? knuth_count_leading_zero_bits (v_work[0]) : 0;
  if (normalization_bit_count)
    {
      knuth_normalize_left_shift_msb (v_work, divisor_words, normalization_bit_count);
      knuth_normalize_left_shift_msb (u_work, dividend_words + 1, normalization_bit_count);
    }
  unsigned int saved_normalization_bit_count = normalization_bit_count;

  int quo_window_count = len_diff;	// number of quotient digits - 1
  int quo_total_word_count = quo_window_count + 1;	// total number of quotient digits
  int quo_store_start_idx = calc_words - quo_total_word_count;	// starting index for writing quotient in right-justified form
  int quo_digit_index = 0;
  int window_offset = 0;
  knuth_digit_t trial_remainder = 0;
  knuth_digit_t trial_quotient = 0;

  /* 4) D2–D7 main loop (per quotient digit) */
  for (quo_digit_index = 0; quo_digit_index <= quo_window_count; quo_digit_index++)
    {
      window_offset = quo_digit_index;

      /* D2–D3: estimate and adjust trial quotient */
      trial_quotient =
	knuth_estimate_quotient_digit (u_work, v_work, window_offset, dividend_words, divisor_words, &trial_remainder);

      /* D4–D6: subtract q*V from U window. if borrow, add back */
      trial_quotient = knuth_multiply_and_subtract (u_work, v_work, window_offset, divisor_words, trial_quotient);

      /* D7: store the finalized quotient digit */
      quo_buf[quo_store_start_idx + quo_digit_index] = trial_quotient;
    }

  /* 5) D8 denormalize */
  if (saved_normalization_bit_count)
    {
      knuth_normalize_right_shift_msb (u_work, dividend_words + 1, saved_normalization_bit_count);
    }

  /* 6) extract remainder */
  int out_rem_idx = calc_words - divisor_words;	// remainder also right-justified
  int i = 0;
  for (i = 0; i < divisor_words; i++)
    {
      rem_buf[out_rem_idx + i] = u_work[quo_total_word_count + i];
    }
}

/*
 * numeric_is_longnum_value ()
 *   return:
 *   arg(in)   : DB_C_NUMERIC
 *
 * Note: This routine check whether the value numeric is long NUMERIC.
 *       Attention: the arg should be long NUMERIC.
 */
static bool
numeric_is_longnum_value (DB_C_NUMERIC arg)
{
  int total_nums = (DB_LONG_NUMERIC_MULTIPLIER - 1) * DB_NUMERIC_BUF_SIZE;
  int i;

  for (i = 0; i < total_nums; i++)
    {
      if (arg[i] != 0)
	{
	  return true;
	}
    }

  return false;
}

/*
 * numeric_shortnum_to_longnum ()
 *   return:
 *   long_answer(out): the long NUMERIC
 *   arg(in)         : DB_C_NUMERIC
 *
 * Note: This routine translate a normal NUMERIC to long NUMERIC.
 *       Attention: the long_answer should be long NUMERIC.
 */
static void
numeric_shortnum_to_longnum (DB_C_NUMERIC long_answer, DB_C_NUMERIC arg)
{
  int i;

  for (i = 0; i < DB_LONG_NUMERIC_MULTIPLIER - 1; i++)
    {
      numeric_zero (long_answer + i * DB_NUMERIC_BUF_SIZE, DB_NUMERIC_BUF_SIZE);
    }
  numeric_copy (long_answer + i * DB_NUMERIC_BUF_SIZE, arg);
}


/*
 * numeric_longnum_to_shortnum ()
 *   return:
 *  answer(out): DB_C_NUMERIC
 *   arg(in)   : long NUMERIC
 *
 * Note: This routine translate a long NUMERIC to normal NUMERIC.
 *       Attention: the long_answer should be long NUMERIC.
 */
static int
numeric_longnum_to_shortnum (DB_C_NUMERIC answer, DB_C_NUMERIC long_arg)
{
  if (numeric_is_longnum_value (long_arg))
    {
      return ER_IT_DATA_OVERFLOW;
    }

  numeric_copy (answer, long_arg + (DB_LONG_NUMERIC_MULTIPLIER - 1) * DB_NUMERIC_BUF_SIZE);
  return NO_ERROR;
}

/*
 * float_numeric_compare() - Compare two numeric values (byte-buffer form)
 *   return: -1 if arg1 < arg2, 0 if equal, +1 if arg1 > arg2
 */
static int
float_numeric_compare (uint8_t * arg1, uint8_t * arg2, int prec1, int scale1, int prec2, int scale2, bool arg1_sign,
		       bool arg2_sign)
{
  int common_prec, common_scale;
  int scale_adjust1, scale_adjust2;
  int needed_bytes, calc_words, calc_nbytes;
  int pad;
  int cmp_rez = 0;

  /* this comparison function requires that arg1 and arg2 always have the same sign. */
  assert (arg1_sign == arg2_sign);

  common_scale = MAX (scale1, scale2);
  common_prec = MAX (prec1 - scale1, prec2 - scale2) + common_scale;
  scale_adjust1 = common_scale - scale1;
  scale_adjust2 = common_scale - scale2;

  needed_bytes = MAX (_gv_numeric_precision_to_bytes_lookup[common_prec], DB_NUMERIC_BUF_SIZE) + 1;
  calc_words = NUMERIC_GET_WORD_COUNT (needed_bytes);
  calc_nbytes = NUMERIC_GET_BYTE_COUNT (calc_words);

  uint64_t arg1_buf[calc_words] = { 0 };
  uint64_t arg2_buf[calc_words] = { 0 };

  numeric_bytes_to_words (arg1, DB_NUMERIC_BUF_SIZE, arg1_buf, calc_words, calc_nbytes);
  numeric_bytes_to_words (arg2, DB_NUMERIC_BUF_SIZE, arg2_buf, calc_words, calc_nbytes);

  if (scale_adjust1)
    {
      float_numeric_mul_normalize (arg1_buf, calc_words, calc_nbytes, scale_adjust1);
    }
  if (scale_adjust2)
    {
      float_numeric_mul_normalize (arg2_buf, calc_words, calc_nbytes, scale_adjust2);
    }

  /* since we don't convert to absolute values when comparing negative numbers, there's no need to invert the result again */
  cmp_rez = float_numeric_operation_compare (arg1_buf, arg2_buf, calc_words);
  if (arg1_sign)
    {
      cmp_rez = -cmp_rez;
    }

  return cmp_rez;
}

/*
 * numeric_scale_by_ten () -
 *   return: NO_ERROR, or ER_code (ER_IT_DATA_OVERFLOW)
 *   arg(in/out)    : ptr to a DB_NUMERIC structure
 *   is_long_num(in): is long NUMERIC
 *
 * Note: This routine scales arg by a factor of ten.
 */
static int
numeric_scale_by_ten (DB_C_NUMERIC arg, bool is_long_num)
{
  int i, answer;

  answer = 0;

  if (is_long_num)
    {
      i = DB_NUMERIC_BUF_SIZE * DB_LONG_NUMERIC_MULTIPLIER;
    }
  else
    {
      i = DB_NUMERIC_BUF_SIZE;
    }
  while (i--)
    {
      answer = (10 * arg[i]) + CARRYOVER (answer);
      arg[i] = GET_LOWER_BYTE (answer);
    }

  if ((int) arg[0] > 0x7f)
    {
      return ER_IT_DATA_OVERFLOW;
    }

  return NO_ERROR;
}

/*
 * numeric_scale_dec () -
 *   return: NO_ERROR, or ER_code
 *   arg(in)    : ptr to a DB_C_NUMERIC structure
 *   dscale(in) : integer scaling factor (positive)
 *   answer(in) : ptr to a DB_C_NUMERIC structure
 *
 * Note: This routine returns a numeric value that has been scaled by the
 *       given number of decimal places.  The result is returned in answer.
 */
static int
numeric_scale_dec (const DB_C_NUMERIC arg, int dscale, DB_C_NUMERIC answer)
{
  int ret = NO_ERROR;

  if (dscale >= 0)
    {
      numeric_copy (answer, arg);
      ret = numeric_scale_dec_long (answer, dscale, false);
    }

  return ret;
}

/*
 * numeric_scale_dec_long () -
 *   return: NO_ERROR, or ER_code
 *   answer(in/out) : ptr to a DB_C_NUMERIC structure
 *   dscale(in) : integer scaling factor (positive)
 *   is_long_num: is long NUMERIC
 *
 * Note: This routine returns a numeric value that has been scaled by the
 *       given number of decimal places.  The result is returned in answer.
 */
static int
numeric_scale_dec_long (DB_C_NUMERIC answer, int dscale, bool is_long_num)
{
  int loop;
  int ret = NO_ERROR;

  if (dscale >= 0)
    {
      for (loop = 0; loop < dscale && ret == NO_ERROR; loop++)
	{
	  ret = numeric_scale_by_ten (answer, is_long_num);
	}
      if (ret != NO_ERROR)
	{
	  return ret;
	}
    }

  return ret;
}

/*
 * numeric_common_prec_scale () -
 *   return: NO_ERROR, or ER_code
 *     Errors:
 *       ER_IT_DATA_OVERFLOW          - if scaling would exceed max scale
 *   dbv1(in): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv2(in): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv1_common(out): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv2_common(out): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *
 * Note: This routine returns two DB_VALUE's of type numeric with the same
 *       scale.  dbv1_common, dbv2_common are set to dbv1, dbv2 respectively
 *       when an error occurs.
 */
static int
numeric_common_prec_scale (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * dbv1_common, DB_VALUE * dbv2_common)
{
  unsigned char temp[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */
  int scale1, scale2;
  int prec1, prec2;
  int cprec;
  int scale_diff;
  TP_DOMAIN *domain;

  /* If scales already match, merely copy them and return */
  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);
  if (scale1 == scale2)
    {
      cprec = MAX (prec1, prec2);
      db_make_numeric (dbv1_common, db_locate_numeric (dbv1), cprec, scale1, DB_NUMERIC_BUF_SIZE,
		       numeric_is_negative (dbv1), false);
      db_make_numeric (dbv2_common, db_locate_numeric (dbv2), cprec, scale2, DB_NUMERIC_BUF_SIZE,
		       numeric_is_negative (dbv2), false);
    }

  /* Otherwise scale and reset the numbers */
  else if (scale1 < scale2)
    {
      scale_diff = scale2 - scale1;
      prec1 = scale_diff + prec1;
      if (prec1 > DB_MAX_FIXED_NUMERIC_PRECISION)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  return ER_IT_DATA_OVERFLOW;
	}
      numeric_scale_dec (db_locate_numeric (dbv1), scale_diff, temp);
      cprec = MAX (prec1, prec2);
      db_make_numeric (dbv1_common, temp, cprec, scale2, DB_NUMERIC_BUF_SIZE, numeric_is_negative (dbv1), false);
      db_make_numeric (dbv2_common, db_locate_numeric (dbv2), cprec, scale2, DB_NUMERIC_BUF_SIZE,
		       numeric_is_negative (dbv2), false);
    }
  else
    {
      scale_diff = scale1 - scale2;
      prec2 = scale_diff + prec2;
      if (prec2 > DB_MAX_FIXED_NUMERIC_PRECISION)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  return ER_IT_DATA_OVERFLOW;
	}
      numeric_scale_dec (db_locate_numeric (dbv2), scale_diff, temp);
      cprec = MAX (prec1, prec2);
      db_make_numeric (dbv2_common, temp, cprec, scale1, DB_NUMERIC_BUF_SIZE, numeric_is_negative (dbv2), false);
      db_make_numeric (dbv1_common, db_locate_numeric (dbv1), cprec, scale1, DB_NUMERIC_BUF_SIZE,
		       numeric_is_negative (dbv1), false);
    }

  return NO_ERROR;
}

/*
 * numeric_prec_scale_when_overflow () -
 *   return: NO_ERROR, or ER_code
 *   dbv1(in)   :
 *   dbv2(in)   :
 *   dbv1_common(out)    :
 *   dbv2_common(out)    :
 */
static int
numeric_prec_scale_when_overflow (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * dbv1_common,
				  DB_VALUE * dbv2_common)
{
  int prec1, scale1, prec2, scale2;
  int prec, scale;
  unsigned char temp[DB_NUMERIC_BUF_SIZE];
  int ret;
  bool dbv1_is_negative = false, dbv2_is_negative = false;

  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);

  scale = MAX (scale1, scale2);
  prec = DB_MAX_FIXED_NUMERIC_PRECISION;

  ret = numeric_coerce_num_to_num (dbv1, prec1, scale1, prec, scale, temp, &dbv1_is_negative);
  if (ret != NO_ERROR)
    {
      return ret;
    }
  db_make_numeric (dbv1_common, temp, prec, scale, DB_NUMERIC_BUF_SIZE, dbv1_is_negative, false);

  ret = numeric_coerce_num_to_num (dbv2, prec2, scale2, prec, scale, temp, &dbv2_is_negative);
  if (ret != NO_ERROR)
    {
      return ret;
    }
  db_make_numeric (dbv2_common, temp, prec, scale, DB_NUMERIC_BUF_SIZE, dbv2_is_negative, false);

  return ret;
}

/*
 * numeric_coerce_big_num_to_dec_str () - Convert a large numeric buffer to a decimal string
 *   return      : void
 *   num(in)     : Input buffer (size: 2 * DB_NUMERIC_BUF_SIZE)
 *   dec_str(out): Output string (size: TWICE_NUM_MAX_PREC + 1)
 *
 * Note:
 *   - Converts a double-sized numeric buffer into a decimal string (ASCII).
 *   - The process involves converting bytes to 64-bit words and performing 
 *     successive division by 10^16 to extract 16-digit blocks from right to left.
 *   - ASSUMPTION: The input buffer represents a positive value.
 */
static void
numeric_coerce_big_num_to_dec_str (unsigned char *num, char *dec_str)
{
  /* 
   * big numeric buffer size = 17 * 2 = 34 bytes.
   * to fit this into 64-bit words: ceil(34 / 8) = 5 words (40 bytes).
   */
  uint64_t word_buf[5] = { 0 };
  int calc_words = 5;
  int calc_nbytes = 40;
  uint64_t chunk;
  char *p_digits;
  int i, j;
  bool all_zero = true;

  assert (num);
  assert (dec_str);

  /* 1. pre-fill with '0' for legacy alignment */
  memset (dec_str, '0', TWICE_NUM_MAX_PREC);
  dec_str[TWICE_NUM_MAX_PREC] = '\0';

  /* 2. copy source to a working buffer for successive division. */
  numeric_bytes_to_words (num, DB_NUMERIC_BUF_SIZE * 2, word_buf, calc_words, calc_nbytes);

  /* 3. check if the entire buffer is 0 */
  for (i = 0; i < calc_words; i++)
    {
      if (word_buf[i] != 0)
	{
	  all_zero = false;
	  break;
	}
    }

  if (all_zero)
    {
      return;
    }

  /* 4. extract digits in 16-digit blocks from right to left (up to 6 iterations, covers 96 digits > Big Num 82 digits) */
  p_digits = dec_str + TWICE_NUM_MAX_PREC - 16;

  /* 
   * maximum decimal digits for 34-byte Big Num is ~ 82.
   * number of 16-digit chunks to cover it: ceil(82 / 16) = 6. 
   */
  const int max_chunks = 6;
  for (i = 0; i < max_chunks; i++)
    {
      /* extract the remainder of division by 10^16 as a 64-bit integer chunk. */
      chunk = float_numeric_div_pow10 (word_buf, calc_words, calc_nbytes, _gv_mul_normalize_pow10_lookup[15]);

      /* convert and store 16 decimal digits as ASCII at once */
      numeric_pack_digits4_ascii (p_digits, chunk);
      p_digits -= 16;

      /* early exit if no more digits remain (quotient is zero) */
      all_zero = true;
      for (j = 0; j < calc_words; j++)
	{
	  if (word_buf[j] != 0)
	    {
	      all_zero = false;
	      break;
	    }
	}

      if (all_zero)
	{
	  break;
	}
    }
}

/*
 * numeric_get_msb_for_dec () -
 *   return:
 *     Errors:
 *       ER_IT_DATA_OVERFLOW          - if src exceeds max precision
 *   src_prec(in)       : int precision of src
 *   src_scale(in)      : int scale of src
 *   src(in)    : buffer to NUMERIC twice the length of the maximum
 *   dest_prec(out)      : ptr to a int precision of dest
 *   dest_scale(out)     : ptr to a int scale of dest
 *   dest(out)   : DB_C_NUMERIC
 *
 * Note: This routine returns a DB_C_NUMERIC along with the precision and
 *       scale of the MSB of the source.  Round-off occurs as long as the scale
 *       of the destination >= 0.
 * Note: it is assumed that src represents a positive number
 */
static int
numeric_get_msb_for_dec (int src_prec, int src_scale, unsigned char *src, int *dest_prec, int *dest_scale,
			 DB_C_NUMERIC dest)
{
  int ret = NO_ERROR;
  char dec_digits[TWICE_NUM_MAX_PREC + 2];

  /* If src precision fits without truncation, merely set dest to the lower half of the source buffer and return */
  if (src_prec <= DB_MAX_FIXED_NUMERIC_PRECISION)
    {
      numeric_copy (dest, &(src[DB_NUMERIC_BUF_SIZE]));
      *dest_prec = src_prec;
      *dest_scale = src_scale;
    }

  /* The remaining cases are for when the precision of the source overflows. */

  /* Case 1: The scale of the source does *not* overflow */
  else if (src_scale <= DB_MAX_FIXED_NUMERIC_PRECISION)
    {
      /* If upper half of *src is zero, merely copy, reset precision, and return */
      if (numeric_is_zero (src) && src[DB_NUMERIC_BUF_SIZE] <= 0x7F)
	{
	  numeric_copy (dest, &(src[DB_NUMERIC_BUF_SIZE]));
	  *dest_prec = DB_MAX_FIXED_NUMERIC_PRECISION;
	  *dest_scale = src_scale;
	}
      else
	{
	  /* Can't truncate answer - expected results must maintain the proper amount of scaling */
	  return ER_IT_DATA_OVERFLOW;
	}
    }

  /* Case 2: The scale of the source overflows. This means the number can't overflow as long as truncation occurs.
   * Reduce the scale and precision by the same amount. */
  else
    {
      int truncation_diff = src_prec - DB_MAX_FIXED_NUMERIC_PRECISION;

      *dest_scale = src_scale - truncation_diff;
      *dest_prec = DB_MAX_FIXED_NUMERIC_PRECISION;

      /* Truncate the obsolete trailing digits. (Note: numeric_coerce_big_num_to_dec_str is guaranteed ro return a
       * NULL-terminated buffer that is TWICE_NUM_MAX_PREC characters long.) */
      numeric_coerce_big_num_to_dec_str (src, dec_digits);
      dec_digits[TWICE_NUM_MAX_PREC - truncation_diff] = '\0';
      numeric_coerce_dec_str_to_num (dec_digits, dest, NULL);
    }

  return ret;
}

/*
 * numeric_db_value_add () -
 *   return: NO_ERROR, or ER_code
 *     Errors:
 *       ER_OBJ_INVALID_ARGUMENTS - if dbv1, dbv2, or answer are NULL or
 *                                  are not DB_TYPE_NUMERIC
 *   dbv1(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv2(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   answer(out): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *
 * Note: This routine adds the numeric values of two DB_VALUE structs and
 * returns the results in answer. The answer will be returned as either the
 * common type of dbv1 and dbv2 or as the common type of dbv1 and dbv2 with
 * an extra decimal place of precision if the sum requires it due to carry.
 * The answer is set to a NULL-valued DB_C_NUMERIC's when an error occurs.
 *
 */
int
numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  DB_VALUE dbv1_common, dbv2_common;
  int ret = NO_ERROR;
  unsigned int prec;
  unsigned char temp[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  TP_DOMAIN *domain;
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  /* Coerce, if necessary, to make prec & scale match */
  ret = numeric_common_prec_scale (dbv1, dbv2, &dbv1_common, &dbv2_common);
  if (ret == ER_IT_DATA_OVERFLOW)
    {
      ret = numeric_prec_scale_when_overflow (dbv1, dbv2, &dbv1_common, &dbv2_common);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else
	{
	  er_clear ();
	}
    }
  else if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Perform the addition */
  if (numeric_is_negative (dbv1) == numeric_is_negative (dbv2))
    {
      numeric_add (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common), temp, DB_NUMERIC_BUF_SIZE);
      result_sign = numeric_is_negative (dbv1);
    }
  else
    {
      if (numeric_compare_pos (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common)) >= 0)
	{
	  // |arg1| >= |arg2|
	  numeric_sub (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common), temp, DB_NUMERIC_BUF_SIZE);
	  result_sign = numeric_is_negative (dbv1);
	}
      else
	{
	  // |arg1| < |arg2|
	  numeric_sub (db_locate_numeric (&dbv2_common), db_locate_numeric (&dbv1_common), temp, DB_NUMERIC_BUF_SIZE);
	  result_sign = numeric_is_negative (dbv2);
	}
    }

  /*
   * Update the domin information of the answer. Check to see if precision
   * needs to be updated due to carry
   */
  prec = DB_VALUE_PRECISION (&dbv1_common);
  if (numeric_overflow (temp, prec))
    {
      if (prec < DB_MAX_NUMERIC_PRECISION)
	{
	  prec++;
	}
      else
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  if (result_sign && numeric_is_zero (temp))
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }
  db_make_numeric (answer, temp, prec, DB_VALUE_SCALE (&dbv1_common), DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * float_numeric_db_value_add() - Add two NUMERIC values
 *   return : NO_ERROR on success, or error code
 *
 * Note:
 *   - The legacy numeric_db_value_add() function was limited to 16 bytes and up to 38 digits.
 *   - float_numeric_db_value_add() supports the extended NUMERIC range, 
 *     allowing operations with larger precision.
 *   - If the result precision exceeds DB_MAX_NUMERIC_PRECISION,
 *     the value is rounded and stored using only DB_MAX_NUMERIC_PRECISION significant digits.
 */
int
float_numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int scale1, scale2, result_scale;
  int prec1, prec2, result_prec, calc_prec1, calc_prec2;
  int needed_bytes, calc_words, calc_nbytes;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE];
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);
  calc_prec1 = prec1 - scale1;
  calc_prec2 = prec2 - scale2;

  result_scale = MAX (scale1, scale2);
  result_prec = MAX (calc_prec1, calc_prec2) + result_scale;

  /* 1) compute common precision/scale and scale adjustments */
  int scale_adjust1 = result_scale - scale1;
  int scale_adjust2 = result_scale - scale2;

  /* 2) determine working buffer size */
  needed_bytes = MAX (_gv_numeric_precision_to_bytes_lookup[result_prec], DB_NUMERIC_BUF_SIZE) + 1;
  calc_words = NUMERIC_GET_WORD_COUNT (needed_bytes);
  calc_nbytes = NUMERIC_GET_BYTE_COUNT (calc_words);

  /* 3) initialize new calculation buffers and pad absolute values */
  uint64_t dbv1_word[calc_words] = { 0 };
  uint64_t dbv2_word[calc_words] = { 0 };
  uint64_t result_word[calc_words] = { 0 };

  numeric_bytes_to_words (db_locate_numeric (dbv1), DB_NUMERIC_BUF_SIZE, dbv1_word, calc_words, calc_nbytes);
  numeric_bytes_to_words (db_locate_numeric (dbv2), DB_NUMERIC_BUF_SIZE, dbv2_word, calc_words, calc_nbytes);

  /* 4) scale adjustments */
  if (scale_adjust1)
    {
      float_numeric_mul_normalize (dbv1_word, calc_words, calc_nbytes, scale_adjust1);
    }
  if (scale_adjust2)
    {
      float_numeric_mul_normalize (dbv2_word, calc_words, calc_nbytes, scale_adjust2);
    }

  /* fast path */
  if (calc_words == NUMERIC_AS_WORDS && (dbv1_word[0] | dbv1_word[1] | dbv2_word[0] | dbv2_word[1]) == 0)
    {
      result_prec =
	float_numeric_add_fast (dbv1_word, dbv2_word, result_word, calc_words, numeric_is_negative (dbv1),
				numeric_is_negative (dbv2), &result_sign, result_buf);
      if (result_sign && result_prec == 1 && result_word[calc_words - 1] == 0)
	{
	  /* Prevent -0; zero is always treated as positive. */
	  result_sign = false;
	}
      db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);
      return ret;
    }

  /* 5) addition */
  if (numeric_is_negative (dbv1) == numeric_is_negative (dbv2))
    {
      (void) float_numeric_add (dbv1_word, dbv2_word, result_word, calc_words);
      result_sign = numeric_is_negative (dbv1);	// result sign = input sign
    }
  else
    {
      if (float_numeric_operation_compare (dbv1_word, dbv2_word, calc_words) >= 0)
	{
	  // |arg1| >= |arg2|
	  (void) float_numeric_sub (dbv1_word, dbv2_word, result_word, calc_words);
	  result_sign = numeric_is_negative (dbv1);	// result sign = sign of larger number
	}
      else
	{
	  // |arg1| < |arg2|
	  (void) float_numeric_sub (dbv2_word, dbv1_word, result_word, calc_words);
	  result_sign = numeric_is_negative (dbv2);	// result sign = sign of larger number
	}
    }

  /* 6) check and recalculate precision/scale of the addition result */
  result_prec = float_numeric_get_decimal_digit (result_word, calc_words);
  if (result_sign && result_prec == 1 && result_word[calc_words - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }

  ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, answer);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* 7) round and pack to DB_NUMERIC_BUF_SIZE bytes */
  ret = float_numeric_round_and_pack (result_word, calc_words, calc_nbytes, result_buf, &result_prec, &result_scale);
  if (ret != NO_ERROR)
    {
      TP_DOMAIN *domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return ret;
    }

  /* 8) store result */
  db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;
}

/*
 * numeric_db_value_sub () -
 *   return: NO_ERROR, or ER_code
 *     Errors:
 *       ER_OBJ_INVALID_ARGUMENTS - if dbv1, dbv2, or answer are NULL or
 *                                  are not DB_TYPE_NUMERIC
 *   dbv1(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv2(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   answer(out): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *
 * Note: This routine subtracts the numeric values of two DB_VALUE's and
 * returns the results in answer. The answer will be returned as either the
 * common type of dbv1 and dbv2 or as the common type of dbv1 and dbv2 with
 * an extra decimal place of precision if the sum requires it due to carry.
 *
 * The answer is set to a NULL-valued DB_C_NUMERIC's when an error occurs.
 */
int
numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  DB_VALUE dbv1_common, dbv2_common;
  int ret = NO_ERROR;
  unsigned int prec;
  unsigned char temp[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  TP_DOMAIN *domain;
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  /* Coerce, if necessary, to make prec & scale match */
  ret = numeric_common_prec_scale (dbv1, dbv2, &dbv1_common, &dbv2_common);
  if (ret == ER_IT_DATA_OVERFLOW)
    {
      ret = numeric_prec_scale_when_overflow (dbv1, dbv2, &dbv1_common, &dbv2_common);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      else
	{
	  er_clear ();
	}
    }
  else if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* Perform the subtraction */
  if (numeric_is_negative (dbv1) == numeric_is_negative (dbv2))
    {
      if (numeric_compare_pos (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common)) >= 0)
	{
	  // |arg1| >= |arg2|
	  numeric_sub (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common), temp, DB_NUMERIC_BUF_SIZE);
	  result_sign = numeric_is_negative (dbv1);
	}
      else
	{
	  // |arg1| < |arg2|
	  numeric_sub (db_locate_numeric (&dbv2_common), db_locate_numeric (&dbv1_common), temp, DB_NUMERIC_BUF_SIZE);
	  result_sign = !numeric_is_negative (dbv2);
	}
    }
  else
    {
      numeric_add (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common), temp, DB_NUMERIC_BUF_SIZE);
      result_sign = numeric_is_negative (dbv1);
    }

  /*
   * Update the domin information of the answer. Check to see if precision
   * needs to be updated due to carry
   */
  prec = DB_VALUE_PRECISION (&dbv1_common);
  if (numeric_overflow (temp, prec))
    {
      if (prec < DB_MAX_FIXED_NUMERIC_PRECISION)
	{
	  prec++;
	}
      else
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  if (result_sign && numeric_is_zero (temp))
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }
  db_make_numeric (answer, temp, prec, DB_VALUE_SCALE (&dbv1_common), DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * float_numeric_db_value_sub() - Subtract two NUMERIC values
 *   return : NO_ERROR on success, or error code
 *
 * Note:
 *   - The legacy numeric_db_value_sub() function was limited to 16 bytes and up to 38 digits.
 *   - float_numeric_db_value_sub() supports the extended NUMERIC range, 
 *     allowing operations with larger precision.
 *   - If the result precision exceeds DB_MAX_NUMERIC_PRECISION,
 *     the value is rounded and stored using only DB_MAX_NUMERIC_PRECISION significant digits.
 */
int
float_numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int scale1, scale2, result_scale;
  int prec1, prec2, result_prec, calc_prec1, calc_prec2;
  int needed_bytes, calc_words, calc_nbytes;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE];
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);
  calc_prec1 = prec1 - scale1;
  calc_prec2 = prec2 - scale2;

  result_scale = MAX (scale1, scale2);
  result_prec = MAX (calc_prec1, calc_prec2) + result_scale;

  /* 1) compute common precision/scale and scale adjustments */
  int scale_adjust1 = result_scale - scale1;
  int scale_adjust2 = result_scale - scale2;

  /* 2) determine working buffer size */
  needed_bytes = MAX (_gv_numeric_precision_to_bytes_lookup[result_prec], DB_NUMERIC_BUF_SIZE) + 1;
  calc_words = NUMERIC_GET_WORD_COUNT (needed_bytes);
  calc_nbytes = NUMERIC_GET_BYTE_COUNT (calc_words);

  /* 3) initialize new calculation buffers and pad absolute values */
  uint64_t dbv1_word[calc_words] = { 0 };
  uint64_t dbv2_word[calc_words] = { 0 };
  uint64_t result_word[calc_words] = { 0 };

  numeric_bytes_to_words (db_locate_numeric (dbv1), DB_NUMERIC_BUF_SIZE, dbv1_word, calc_words, calc_nbytes);
  numeric_bytes_to_words (db_locate_numeric (dbv2), DB_NUMERIC_BUF_SIZE, dbv2_word, calc_words, calc_nbytes);

  /* 4) scale adjustments */
  if (scale_adjust1)
    {
      float_numeric_mul_normalize (dbv1_word, calc_words, calc_nbytes, scale_adjust1);
    }
  if (scale_adjust2)
    {
      float_numeric_mul_normalize (dbv2_word, calc_words, calc_nbytes, scale_adjust2);
    }

  /* fast path */
  if (calc_words == NUMERIC_AS_WORDS && (dbv1_word[0] | dbv1_word[1] | dbv2_word[0] | dbv2_word[1]) == 0)
    {
      result_prec =
	float_numeric_sub_fast (dbv1_word, dbv2_word, result_word, calc_words, numeric_is_negative (dbv1),
				numeric_is_negative (dbv2), &result_sign, result_buf);
      if (result_sign && result_prec == 1 && result_word[calc_words - 1] == 0)
	{
	  /* Prevent -0; zero is always treated as positive. */
	  result_sign = false;
	}
      db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);
      return ret;
    }

  /* 5) subtraction */
  if (numeric_is_negative (dbv1) == numeric_is_negative (dbv2))
    {
      if (float_numeric_operation_compare (dbv1_word, dbv2_word, calc_words) >= 0)
	{
	  // |arg1| >= |arg2|
	  (void) float_numeric_sub (dbv1_word, dbv2_word, result_word, calc_words);
	  result_sign = numeric_is_negative (dbv1);	// result sign = sign of larger number
	}
      else
	{
	  // |arg1| < |arg2|
	  (void) float_numeric_sub (dbv2_word, dbv1_word, result_word, calc_words);
	  result_sign = !numeric_is_negative (dbv2);	// result sign = sign of larger number
	}
    }
  else
    {
      (void) float_numeric_add (dbv1_word, dbv2_word, result_word, calc_words);
      result_sign = numeric_is_negative (dbv1);	// result sign = input sign
    }

  /* 6) check and recalculate precision/scale of the subtraction result */
  result_prec = float_numeric_get_decimal_digit (result_word, calc_words);
  if (result_sign && result_prec == 1 && result_word[calc_words - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }

  ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, answer);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* 7) round and pack to DB_NUMERIC_BUF_SIZE bytes */
  ret = float_numeric_round_and_pack (result_word, calc_words, calc_nbytes, result_buf, &result_prec, &result_scale);
  if (ret != NO_ERROR)
    {
      TP_DOMAIN *domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return ret;
    }

  /* 8) store result */
  db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;
}

/*
 * numeric_db_value_mul () -
 *   return: NO_ERROR, or ER_code
 *     Errors:
 *       ER_OBJ_INVALID_ARGUMENTS - if dbv1, dbv2, or answer are NULL or
 *                                  are not DB_TYPE_NUMERIC
 *   dbv1(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv2(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   answer(out): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *
 * Note: This routine multiplies the numeric values of two DB_VALUE's and
 * returns the results in answer. The answer will be returned as either the
 * common type of dbv1 and dbv2 or as the common type of dbv1 and dbv2 with
 * a extra decimal places of precision if the product requires it to avoid
 * loss of data.
 *
 * The answer is set to a NULL-valued DB_C_NUMERIC's when an error occurs.
 */
int
numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int prec;
  int scale;
  unsigned char temp[2 * DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  unsigned char result[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  result_sign = numeric_is_negative (dbv1) ^ numeric_is_negative (dbv2);

  /* Perform the multiplication */
  numeric_mul (db_locate_numeric (dbv1), db_locate_numeric (dbv2), temp, &result_sign);
  /* Check for overflow.  Reset precision & scale if necessary */
  prec = db_get_numeric_precision (dbv1, NULL) + db_get_numeric_precision (dbv2, NULL) + 1;
  scale = db_get_numeric_scale (dbv1, NULL) + db_get_numeric_scale (dbv2, NULL);
  ret = numeric_get_msb_for_dec (prec, scale, temp, &prec, &scale, result);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* If no error, make the answer */
  db_make_numeric (answer, result, prec, scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * float_numeric_db_value_mul() - Multiply two NUMERIC values
 *   return : NO_ERROR on success, or error code
 *
 * Note:
 *   - The legacy numeric_db_value_mul() function was limited to 16 bytes and up to 38 digits.
 *   - float_numeric_db_value_mul() supports the extended NUMERIC range, 
 *     allowing operations with larger precision.
 *   - If the result precision exceeds DB_MAX_NUMERIC_PRECISION,
 *     the value is rounded and stored using only DB_MAX_NUMERIC_PRECISION significant digits.
 */
int
float_numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int needed_bytes, calc_words, calc_nbytes;
  int scale1, scale2, result_scale;
  int prec1, prec2, result_prec;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE];
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  /* 
   * The following cases return 0 immediately without any operation
   * ex) 0 * 0 = 0
   *     0 * v = 0
   *     v * 0 = 0
   */
  if (numeric_is_zero (db_locate_numeric (dbv1)) || numeric_is_zero (db_locate_numeric (dbv2)))
    {
      memset (result_buf, 0, DB_NUMERIC_BUF_SIZE);
      result_prec = 1;
      result_scale = 0;
      db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, false, true);
      return NO_ERROR;
    }

  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);
  result_scale = scale1 + scale2;
  result_prec = prec1 + prec2;

  /* 1) determine common sign of the result */
  result_sign = numeric_is_negative (dbv1) ^ numeric_is_negative (dbv2);

  /* 2) determine working buffer size */
  needed_bytes = MAX (_gv_numeric_precision_to_bytes_lookup[result_prec], DB_NUMERIC_BUF_SIZE) + 1;
  calc_words = NUMERIC_GET_WORD_COUNT (needed_bytes);
  calc_nbytes = NUMERIC_GET_BYTE_COUNT (calc_words);

  /* 3) initialize new calculation buffers and pad absolute values */
  uint64_t dbv1_word[calc_words] = { 0 };
  uint64_t dbv2_word[calc_words] = { 0 };
  uint64_t result_word[calc_words] = { 0 };

  numeric_bytes_to_words (db_locate_numeric (dbv1), DB_NUMERIC_BUF_SIZE, dbv1_word, calc_words, calc_nbytes);
  numeric_bytes_to_words (db_locate_numeric (dbv2), DB_NUMERIC_BUF_SIZE, dbv2_word, calc_words, calc_nbytes);

  /* fast path */
  if (calc_words == NUMERIC_AS_WORDS && (dbv1_word[0] | dbv1_word[1] | dbv2_word[0] | dbv2_word[1]) == 0)
    {
      result_prec = float_numeric_mul_fast (dbv1_word, dbv2_word, result_word, calc_words, result_buf, &result_scale);
      if (result_sign && result_prec == 1 && result_word[calc_words - 1] == 0)
	{
	  /* Prevent -0; zero is always treated as positive. */
	  result_sign = false;
	}
      db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);
      return ret;
    }

  /* 4) scale adjustments */
  // multiplication does not require scale adjustments

  /* 5) multiplication */
  float_numeric_mul (dbv1_word, dbv2_word, result_word, calc_words, calc_nbytes);

  /* 6) check and recalculate precision/scale of the multiplication result */
  if (result_scale > DB_MAX_NUMERIC_SCALE)
    {
      int scale_overflow = result_scale - DB_MAX_NUMERIC_SCALE;
      result_scale = DB_MAX_NUMERIC_SCALE;

      if (float_numeric_div_normalize (result_word, calc_words, calc_nbytes, scale_overflow) >= ROUND_HALF_UP_DIGIT)
	{
	  (void) float_numeric_increment (result_word, calc_words, 1);
	}
    }
  result_prec = float_numeric_get_decimal_digit (result_word, calc_words);
  if (result_sign && result_prec == 1 && result_word[calc_words - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }

  ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, answer);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* 7) round and pack to DB_NUMERIC_BUF_SIZE bytes */
  ret = float_numeric_round_and_pack (result_word, calc_words, calc_nbytes, result_buf, &result_prec, &result_scale);
  if (ret != NO_ERROR)
    {
      TP_DOMAIN *domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return ret;
    }

  /* 8) store result */
  db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;
}

/*
 * numeric_db_value_div () -
 *   return: NO_ERROR, or ER_code
 *     Errors:
 *       ER_OBJ_INVALID_ARGUMENTS - if dbv1, dbv2, or answer are NULL or
 *                                  are not DB_TYPE_NUMERIC
 *   dbv1(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   dbv2(in)   : ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *   answer(out): ptr to a DB_VALUE structure of type DB_TYPE_NUMERIC
 *
 * Note: This routine divides the numeric values of two DB_VALUE's and
 * returns the results in answer. The answer will be returned as either the
 * common type of dbv1 and dbv2 or as the common type of dbv1 and dbv2 with
 * a extra decimal places of precision if the quotient requires it to avoid
 * loss of data.
 *
 * The answer is set to a NULL-valued DB_C_NUMERIC's when an error occurs.
 */
int
numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int prec;
  int max_scale, scale1, scale2;
  unsigned char long_dbv1_copy[DB_LONG_NUMERIC_MULTIPLIER * DB_NUMERIC_BUF_SIZE];
  unsigned char long_temp_quo[DB_LONG_NUMERIC_MULTIPLIER * DB_NUMERIC_BUF_SIZE];
  unsigned char dbv1_copy[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  unsigned char dbv2_copy[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  unsigned char temp_quo[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  unsigned char temp_rem[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  int scale, scaleup = 0;
  int ret = NO_ERROR;
  TP_DOMAIN *domain;
  DB_C_NUMERIC divisor_p;
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  result_sign = numeric_is_negative (dbv1) ^ numeric_is_negative (dbv2);

  /* In order to maintain the proper number of scaling in the output, find the maximum scale of the two args and make
   * sure that the scale of dbv1 exceeds the scale of dbv2 by that amount. */
  numeric_shortnum_to_longnum (long_dbv1_copy, db_locate_numeric (dbv1));
  scale1 = db_get_numeric_scale (dbv1, NULL);
  scale2 = db_get_numeric_scale (dbv2, NULL);
  max_scale = MAX (scale1, scale2);
  if (scale2 > 0)
    {
      scaleup = (max_scale + scale2) - scale1;
      ret = numeric_scale_dec_long (long_dbv1_copy, scaleup, true);
      if (ret != NO_ERROR)
	{			/* overflow */
	  goto exit_on_error;
	}
    }

  /*
   * Update the domain information of the answer. Check to see if precision
   * needs to be updated due to carry
   */
  prec = db_get_numeric_precision (dbv1, NULL) + scaleup;
  scale = max_scale;
  if (prec > DB_MAX_NUMERIC_PRECISION)
    {
      prec = DB_MAX_NUMERIC_PRECISION;
    }

  if (scale < DB_LEGACY_DEFAULT_NUMERIC_DIVISION_SCALE)
    {
      int new_scale, new_prec;
      int scale_delta;
      scale_delta = DB_LEGACY_DEFAULT_NUMERIC_DIVISION_SCALE - scale;
      new_scale = scale + scale_delta;
      new_prec = prec + scale_delta;
      if (new_prec > DB_MAX_NUMERIC_PRECISION)
	{
	  new_scale -= (new_prec - DB_MAX_NUMERIC_PRECISION);
	  new_prec = DB_MAX_NUMERIC_PRECISION;
	}

      ret = numeric_scale_dec_long (long_dbv1_copy, new_scale - scale, true);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      scale = new_scale;
      prec = new_prec;
    }

  if (numeric_is_longnum_value (long_dbv1_copy))
    {
      /* only the dividend and quotient maybe long numeric, divisor must be numeric */
      numeric_long_div (long_dbv1_copy, db_locate_numeric (dbv2), long_temp_quo, temp_rem, true);
      ret = numeric_longnum_to_shortnum (temp_quo, long_temp_quo);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }
  else
    {
      numeric_longnum_to_shortnum (dbv1_copy, long_dbv1_copy);
      numeric_div (dbv1_copy, db_locate_numeric (dbv2), temp_quo, temp_rem, numeric_is_negative (dbv1),
		   numeric_is_negative (dbv2));
    }

  /* round! Check if remainder is larger than or equal to 2*divisor. i.e. rem / divisor >= 0.5 */
  divisor_p = db_locate_numeric (dbv2);

  numeric_add (temp_rem, temp_rem, temp_rem, DB_NUMERIC_BUF_SIZE);
  if (numeric_compare_pos (temp_rem, divisor_p) >= 0)
    {
      numeric_increase (temp_quo);
    }

  if (numeric_overflow (temp_quo, prec))
    {
      if (prec < DB_MAX_NUMERIC_PRECISION)
	{
	  prec++;
	}
      else
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  if (result_sign && numeric_is_zero (temp_quo))
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }

  db_make_numeric (answer, temp_quo, prec, scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * float_numeric_db_value_div() - divide two NUMERIC values
 *   return : NO_ERROR on success, or error code
 *
 * Note:
 *   - The legacy numeric_db_value_div() function was limited to 16 bytes and up to 38 digits.
 *   - float_numeric_db_value_div() supports the extended NUMERIC range, 
 *     allowing operations with larger precision.
 *   - If the result precision exceeds DB_MAX_NUMERIC_PRECISION,
 *     the value is rounded and stored using only DB_MAX_NUMERIC_PRECISION significant digits.
 */
int
float_numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int result_prec;
  int result_scale;
  int prec1, prec2;
  int scale1, scale2;
  int needed_bytes, calc_words, calc_nbytes;
  int exponent10;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE];
  bool result_sign = false;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  /* 
   * The following cases return 0 immediately without any operation
   * ex) 12 / 0 = err (=already handled in parsing)
   *     0 / 12 = 0
   */
  if (numeric_is_zero (db_locate_numeric (dbv1)))
    {
      memset (result_buf, 0, DB_NUMERIC_BUF_SIZE);
      result_prec = 1;
      result_scale = 0;
      db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, false, true);
      return NO_ERROR;
    }

  /* 1) compute exact precision values for mantissa calculations */
  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);

  uint64_t dbv1_word[NUMERIC_AS_WORDS] = { 0 };
  uint64_t dbv2_word[NUMERIC_AS_WORDS] = { 0 };

  numeric_bytes_to_words (db_locate_numeric (dbv1), DB_NUMERIC_BUF_SIZE, dbv1_word, NUMERIC_AS_WORDS,
			  NUMERIC_AS_WORD_BYTES);
  numeric_bytes_to_words (db_locate_numeric (dbv2), DB_NUMERIC_BUF_SIZE, dbv2_word, NUMERIC_AS_WORDS,
			  NUMERIC_AS_WORD_BYTES);

  prec1 = float_numeric_get_decimal_digit (dbv1_word, NUMERIC_AS_WORDS);
  prec2 = float_numeric_get_decimal_digit (dbv2_word, NUMERIC_AS_WORDS);

  /* 2) determine common sign of the result */
  result_sign = numeric_is_negative (dbv1) ^ numeric_is_negative (dbv2);

  /* 3) compute exact exponent values for mantissa calculations */
  int dividend_exponent = (prec1 - 1) - scale1;
  int divisor_exponent = (prec2 - 1) - scale2;
  int exponent_diff = dividend_exponent - divisor_exponent;

  /* fast path */
  if (prec1 <= 19 && prec2 <= 19)
    {
      float_numeric_div_fast (dbv1_word[2], dbv2_word[2],
			      prec1, scale1, prec2, scale2,
			      exponent_diff, result_buf, &result_prec, &result_scale, &result_sign);

      ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, answer);
      if (ret == NO_ERROR)
	{
	  db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);
	}
      return ret;
    }

  /* 4) compare mantissa */
  int mantissa_compare =
    compare_mantissa_same_exponent (dbv1_word, dbv2_word, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES, prec1, prec2);
  int result_digits = (mantissa_compare >= 0) ? (exponent_diff + 1) : exponent_diff;

  /* 5) mantissa calculations */
  result_prec = DB_MAX_NUMERIC_PRECISION;
  result_scale = result_prec - result_digits;
  exponent10 = (result_scale) + (scale2 - scale1);
  if (result_scale > DB_MAX_NUMERIC_SCALE)
    {
      int scale_overflow = DB_MAX_NUMERIC_SCALE + result_digits;
      exponent10 -= (DB_MAX_NUMERIC_PRECISION - scale_overflow);
      result_scale = DB_MAX_NUMERIC_SCALE;
    }

  ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, answer);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* 6) initialize new calculation buffers and pad absolute values */
  needed_bytes = _gv_numeric_precision_to_bytes_lookup[(exponent10 > 0) ? exponent10 : 0];
  needed_bytes += ((int) DB_NUMERIC_BUF_SIZE + 2);
  calc_words = NUMERIC_GET_WORD_COUNT (needed_bytes);
  calc_nbytes = NUMERIC_GET_BYTE_COUNT (calc_words);

  uint64_t dividend_work[calc_words] = { 0 };
  uint64_t divisor_work[calc_words] = { 0 };
  uint64_t quotient_work[calc_words] = { 0 };
  uint64_t remainder_work[calc_words] = { 0 };

  numeric_bytes_to_words (db_locate_numeric (dbv1), DB_NUMERIC_BUF_SIZE, dividend_work, calc_words, calc_nbytes);
  numeric_bytes_to_words (db_locate_numeric (dbv2), DB_NUMERIC_BUF_SIZE, divisor_work, calc_words, calc_nbytes);

  /* 7) only dividend_work is scaled (div_pow10 if negative) */
  if (exponent10 > 0)
    {
      float_numeric_mul_normalize (dividend_work, calc_words, calc_nbytes, exponent10);
    }
  else if (exponent10 < 0)
    {
      /* reduces digits for normalization; does not perform rounding */
      (void) float_numeric_div_normalize (dividend_work, calc_words, calc_nbytes, -exponent10);
    }

  /* 8) division */
  float_numeric_knuth_div ((knuth_digit_t *) dividend_work, (knuth_digit_t *) divisor_work,
			   (knuth_digit_t *) quotient_work, (knuth_digit_t *) remainder_work, calc_words);

  /* 9) round up if necessary */
  if (float_numeric_compare_rem_round_up (remainder_work, divisor_work, calc_words) >= 0)
    {
      (void) float_numeric_increment (quotient_work, calc_words, 1);
    }

  /* 10) check and recalculate precision/scale of the division result */
  result_prec = float_numeric_get_decimal_digit (quotient_work, calc_words);
  /* 
   * step 5) mantissa calculation (result_scale = 40 - result_digits) constrains
   * precision to 40 or less. result_prec must not exceed 40 after division.
   */
  assert (result_prec <= DB_MAX_NUMERIC_PRECISION);
  if (result_sign && result_prec == 1 && quotient_work[calc_words - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }
  ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, answer);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* 11) round and pack to DB_NUMERIC_BUF_SIZE bytes */
  ret = float_numeric_round_and_pack (quotient_work, calc_words, calc_nbytes, result_buf, &result_prec, &result_scale);
  if (ret != NO_ERROR)
    {
      TP_DOMAIN *domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return ret;
    }

  /* 12) store result */
  db_make_numeric (answer, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;
}

/*
 * numeric_db_value_is_positive () -
 *   return: 1 (>= 0), 0 (< 0), error code (error)
 *   dbvalue(in): ptr to a DB_VALUE of type DB_TYPE_NUMERIC
 */
int
numeric_db_value_is_positive (const DB_VALUE * dbvalue)
{
  int ret;

  /* Check for bad inputs */
  if (dbvalue == NULL || DB_VALUE_TYPE (dbvalue) != DB_TYPE_NUMERIC || DB_IS_NULL (dbvalue))
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  ret = numeric_is_negative (dbvalue);

  return !ret;
}

/*
 * numeric_db_value_compare () -
 *   return: NO_ERROR, or ER_code
 *     Errors:
 *       ER_OBJ_INVALID_ARGUMENTS - if dbv1, dbv2, or answer are NULL or
 *                                  are not DB_TYPE_NUMERIC (for dbv*) or
 *				    DB_TYPE_INTEGER (for answer);
 *   dbv1(in)   : ptr to a DB_VALUE of type DB_TYPE_NUMERIC
 *   dbv2(in)   : ptr to a DB_VALUE of type DB_TYPE_NUMERIC
 *   answer(out): ptr to a DB_VALUE of type DB_TYPE_INTEGER
 *
 * Note: This routine compares two numeric DB_VALUE's and sets the value of
 * answer accordingly. This function returns:
 *          -1   if    dbv1 < dbv2
 *           0   if    dbv1 = dbv2 and
 *           1   if    dbv1 > dbv2.
 */
int
numeric_db_value_compare (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int prec1 = 0, prec2 = 0, scale1 = 0, scale2 = 0;
  int cmp_rez = 0;

  /* Check for bad inputs */
  if (answer == NULL)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv1 == NULL || DB_VALUE_TYPE (dbv1) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  if (dbv2 == NULL || DB_VALUE_TYPE (dbv2) != DB_TYPE_NUMERIC)
    {
      db_make_null (answer);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Check for NULL values */
  if (DB_IS_NULL (dbv1) || DB_IS_NULL (dbv2))
    {
      db_value_domain_init (answer, DB_TYPE_INTEGER, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return NO_ERROR;
    }

  db_get_numeric_precision_and_scale (dbv1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (dbv2, &prec2, &scale2, NULL);

  if (numeric_is_negative (dbv1) != numeric_is_negative (dbv2))
    {
      ret = db_make_int (answer, (numeric_is_negative (dbv1) ? -1 : 1));
    }
  else if (prec1 == prec2 && scale1 == scale2)
    {
      /* Simple case. Just compare two numbers. */
      cmp_rez = numeric_operation_compare (db_locate_numeric (dbv1), db_locate_numeric (dbv2), DB_NUMERIC_BUF_SIZE);
      if (numeric_is_negative (dbv1))
	{
	  cmp_rez = -cmp_rez;
	}
      ret = db_make_int (answer, cmp_rez);
    }
  else
    {
      /* positive vs positive, negative vs negative
       * 
       * compare after scale correction inside the function
       * ex) result :
       *      if(arg1 < arg2) = -1
       *      if(arg1 = arg2) = 0
       *      if(arg1 > arg2) = 1
       */
      cmp_rez =
	float_numeric_compare (db_locate_numeric (dbv1), db_locate_numeric (dbv2), prec1, scale1, prec2, scale2,
			       numeric_is_negative (dbv1), numeric_is_negative (dbv2));
      ret = db_make_int (answer, cmp_rez);
    }

  return ret;
}

/*
 * numeric_coerce_int_to_num () -
 *   return:
 *   arg(in)    : unsigned int value
 *   answer(out): DB_C_NUMERIC
 *   is_value_negative(out): sign of the value
 *
 * Note: This routine converts 32 bit integer into DB_C_NUMERIC format and
 * returns the result.
 */
void
numeric_coerce_int_to_num (int arg, DB_C_NUMERIC answer, bool * is_value_negative)
{
  uint32_t digit;
  unsigned int tmp_arg;

  tmp_arg = (arg < 0) ? -(unsigned int) arg : (unsigned int) arg;
  if (is_value_negative)
    {
      *is_value_negative = (arg < 0);
    }

  /* Copy the lower 32 bits into answer [13] ~ [16] (4 bytes) */
  digit = NUMERIC_BSWAP32 (tmp_arg);
  memcpy (answer + (DB_NUMERIC_BUF_SIZE - sizeof (int)), &digit, sizeof (digit));

  /* Pad extra bytes of answer accordingly [0] ~ [12] (13 bytes) */
  memset (answer, 0, DB_NUMERIC_BUF_SIZE - sizeof (int));
}

/*
 * numeric_coerce_bigint_to_num () -
 *   return:
 *   arg(in)    : unsigned bigint value
 *   answer(out): DB_C_NUMERIC
 *   is_value_negative(out): sign of the value
 *
 * Note: This routine converts 64 bit integer into DB_C_NUMERIC format and
 * returns the result.
 */
void
numeric_coerce_bigint_to_num (DB_BIGINT arg, DB_C_NUMERIC answer, bool * is_value_negative)
{
  uint64_t *digit;
  UINT64 tmp_arg;

  tmp_arg = (arg < 0) ? -(UINT64) arg : (UINT64) arg;
  if (is_value_negative)
    {
      *is_value_negative = (arg < 0);
    }

  /* Copy the lower 64 bits into answer [9] ~ [16] (8 bytes) */
  numeric_put_uint64_to_be (answer + (DB_NUMERIC_BUF_SIZE - sizeof (DB_BIGINT)), tmp_arg);

  /* Pad extra bytes of answer accordingly [0] ~ [8] (9 bytes) */
  memset (answer, 0, DB_NUMERIC_BUF_SIZE - sizeof (DB_BIGINT));
}

/*
 * numeric_coerce_num_to_int () -
 *   return:
 *   arg(in)    : ptr to a DB_C_NUMERIC
 *   answer(out): ptr to an integer
 *   is_value_negative(in): sign of the value
 *
 * Note: This routine converts a numeric into an integer returns the result.
 * If arg overflows answer, answer is set to +/- MAXINT.
 */
void
numeric_coerce_num_to_int (DB_C_NUMERIC arg, int *answer, const bool is_value_negative)
{
  uint32_t digit;

  /* Copy the lower 32 bits into answer. */
  memcpy (&digit, arg + (DB_NUMERIC_BUF_SIZE - sizeof (int)), sizeof (digit));
  *answer = (int) NUMERIC_BSWAP32 (digit);

  /* Apply sign
   *  Negating INT_MIN (-2147483648) is signed-overflow UB in C.
   *  When the magnitude is exactly 0x80000000 and the value is negative,
   *  the result must be INT_MIN, so skip the negation for that case. */
  if (is_value_negative)
    {
      if ((uint32_t) (*answer) == 0x80000000U)
	{
	  *answer = INT_MIN;
	}
      else
	{
	  *answer = -(*answer);
	}
    }
}

/*
 * numeric_coerce_num_to_bigint () - Convert a NUMERIC value to a 64-bit BIGINT
 *   return          : NO_ERROR on success, or ER_IT_DATA_OVERFLOW 
 *   arg(in)         : Pointer to a DB_C_NUMERIC buffer
 *   scale(in)       : Number of decimal positions to adjust
 *   answer(out)     : Pointer to the resulting DB_BIGINT
 *   is_value_negative(in): Sign of the input value
 *
 * Note: Performs scale adjustment (division by 10^scale and rounding) and 
 *       converts the resulting value into a signed 64-bit integer.
 *       If the value exceeds the 64-bit BIGINT range, ER_IT_DATA_OVERFLOW 
 *       is returned.
 */
int
numeric_coerce_num_to_bigint (DB_C_NUMERIC arg, int scale, DB_BIGINT * answer, const bool is_value_negative)
{
  uint64_t work_buf[NUMERIC_AS_WORDS] = { 0 };
  int rounding_digit = 0;
  uint64_t magnitude;

  if (scale >= (int) (sizeof (powers_of_10) / sizeof (powers_of_10[0])))
    {
      return ER_IT_DATA_OVERFLOW;
    }

  /* 1. load DB_C_NUMERIC (Big-endian bytes) into word-aligned work_buf */
  work_buf[0] = (uint64_t) arg[0];
  memcpy (&work_buf[1], arg + 1, 16);
  work_buf[1] = NUMERIC_BSWAP64 (work_buf[1]);
  work_buf[2] = NUMERIC_BSWAP64 (work_buf[2]);

  /* 2. perform scale adjustment if scale > 0 (divide by 10^scale) */
  if (scale > 0)
    {
      rounding_digit = float_numeric_div_normalize (work_buf, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES, scale);

      /* 3. round up if the fractional remainder is >= 5 */
      if (rounding_digit >= ROUND_HALF_UP_DIGIT)
	{
	  float_numeric_increment (work_buf, NUMERIC_AS_WORDS, 1);
	}
    }

  /* 4. check if the value fits within 64 bits (upper words must be zero) */
  if (work_buf[0] != 0 || work_buf[1] != 0)
    {
      return ER_IT_DATA_OVERFLOW;
    }

  /* 5. BIGINT range check considering the sign
   *    positive range: [0, 2^63 - 1] (0x7FFFFFFFFFFFFFFF)
   *    negative range: [0, 2^63] (0x8000000000000000)
   */
  magnitude = work_buf[2];
  if (magnitude > (is_value_negative ? 0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFULL))
    {
      return ER_IT_DATA_OVERFLOW;
    }

  /* 6. form final signed BIGINT result
   *    INT64_MIN (magnitude == 0x8000000000000000) is a special case:
   *    negating (DB_BIGINT)INT64_MIN is signed-overflow UB in C.
   *    Since the stored value is already the absolute magnitude,
   *    we can produce the correct two's-complement INT64_MIN directly. */
  if (is_value_negative)
    {
      if (magnitude == 0x8000000000000000ULL)
	{
	  *answer = INT64_MIN;
	}
      else
	{
	  *answer = -(DB_BIGINT) magnitude;
	}
    }
  else
    {
      *answer = (DB_BIGINT) magnitude;
    }

  return NO_ERROR;
}

/*
 * numeric_coerce_dec_str_to_num () -
 *   return:
 *   dec_str(in): char * holds positive decimal digits as ASCII chars
 *   result(out): ptr to a DB_C_NUMERIC
 *   is_value_negative(out): sign of the value
 *
 * Note: 
 *   - Converts a character string containing decimal digits into a NUMERIC format.
 *
 *   - Uses Horner's Method for efficient accumulation:
 *     Instead of power-of-10 multiplication for each digit, it iteratively 
 *     multiplies the current sum and adds the next part.
 *     Horizontal Example: 123 = (1 * 10 + 2) * 10 + 3
 *
 *   - For optimal performance, the string is processed in 16-digit chunks 
 *     (using 64-bit arithmetic) to minimize the number of high-precision 
 *     numeric operations.
 */
void
numeric_coerce_dec_str_to_num (const char *dec_str, DB_C_NUMERIC result, bool * is_value_negative)
{
  int i, ntot_digits, ndigits;
  uint64_t chunk_value;
  bool is_negative = false, has_non_zero = false;
  const char *curr = dec_str;

  uint64_t result_word[NUMERIC_AS_WORDS] = { 0 };

  numeric_zero (result, DB_NUMERIC_BUF_SIZE);

  if (*curr == '-')
    {
      is_negative = true;
      curr++;
    }

  ntot_digits = (int) strlen (curr);

  /* fast path : bigint max(19) -1 = 18 */
  if (ntot_digits <= 18)
    {
      uint64_t tmp_digits = 0;
      while (*curr)
	{
	  tmp_digits = tmp_digits * 10 + (*curr++ - '0');
	}

      numeric_coerce_bigint_to_num (tmp_digits, result, NULL);

      if (is_value_negative)
	{
	  *is_value_negative = (is_negative && tmp_digits > 0);
	}
      return;
    }

  /* maximize performance by processing in 16-digit chunks (uint64 threshold), 
   * resulting in at most 3 iterations for 38-digit values. */
  ndigits = ntot_digits % 16;
  if (ndigits == 0 && ntot_digits > 0)
    {
      ndigits = 16;
    }

  while (ntot_digits > 0)
    {
      chunk_value = 0;
      for (i = 0; i < ndigits; i++)
	{
	  chunk_value = chunk_value * 10 + (*curr++ - '0');
	}

      if (chunk_value != 0)
	{
	  float_numeric_increment (result_word, NUMERIC_AS_WORDS, chunk_value);
	  has_non_zero = true;
	}

      ntot_digits -= ndigits;
      if (ntot_digits > 0)
	{
	  float_numeric_mul_pow10 (result_word, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES,
				   _gv_mul_normalize_pow10_lookup[15]);
	  ndigits = 16;
	}
    }

  numeric_words_to_bytes (result_word, NUMERIC_AS_WORDS, result);

  if (is_value_negative)
    {
      *is_value_negative = is_negative && has_non_zero;
    }
}

/*
 * numeric_coerce_num_to_dec_str () -
 *   return:
 *   num(in)    : DB_VALUE
 *   dec_str(out): returned string of decimal digits as ASCII chars
 *
 * Note: This routine converts a DB_C_NUMERIC into a character string that
 *       contains the decimal digits of the numeric encoded as ASCII characters.
 *
 *       it efficiently extracts digits using successive division by 10^16 ~ 10^19.
 *
 *       since a 17-byte (136-bit) numeric buffer represents up to approximately 
 *       40 decimal digits, 3 iterations (reaching 48 digits) are sufficient 
 *       to extract all significant digits.
 *
 *       the output is zero-padded to TWICE_NUM_MAX_PREC characters to maintain 
 *       compatibility with existing CUBRID decimal formatting routines.
 */
void
numeric_coerce_num_to_dec_str (const DB_VALUE * num_value, char *dec_str)
{
  uint64_t work_word[NUMERIC_AS_WORDS] = { 0 };
  uint64_t chunk;
  char *p_digits;
  int i, j;
  bool all_zero = true;

  assert (num_value);
  assert (dec_str);

  /* 1. handle negative sign */
  if (numeric_is_negative (num_value))
    {
      *dec_str++ = '-';
    }

  /* 2. pre-fill with '0' for legacy alignment */
  memset (dec_str, '0', TWICE_NUM_MAX_PREC);
  dec_str[TWICE_NUM_MAX_PREC] = '\0';

  /* 3. fast exit for zero */
  if (numeric_is_zero (db_locate_numeric (num_value)))
    {
      return;
    }

  /* 4. copy to local buffer for division */
  numeric_bytes_to_words (db_locate_numeric (num_value), DB_NUMERIC_BUF_SIZE, work_word, NUMERIC_AS_WORDS,
			  NUMERIC_AS_WORD_BYTES);

  /* 5. successive division in 3 blocks of 16-digits
   *    17-byte buffer capacity (~40 digits) < 3 iterations (48 digits) */
  p_digits = dec_str + TWICE_NUM_MAX_PREC - 16;

  /* 
   * maximum decimal digits for 17-byte numeric is 40. 
   * 3 iterations cover up to 48 digits (16 * 3). 
   */
  for (i = 0; i < NUMERIC_AS_WORDS; i++)
    {
      /* extract remaining digits as a single 64-bit integer */
      chunk =
	float_numeric_div_pow10 (work_word, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES,
				 _gv_mul_normalize_pow10_lookup[15]);

      /* convert and store 16 decimal digits as ASCII at once */
      numeric_pack_digits4_ascii (p_digits, chunk);
      p_digits -= 16;		/* move block unit to the left */

      /* exit loop if no more digits to divide (prevent unnecessary 3rd iteration) */
      all_zero = true;
      for (j = 0; j < NUMERIC_AS_WORDS; j++)
	{
	  if (work_word[j] != 0)
	    {
	      all_zero = false;
	      break;
	    }
	}

      if (all_zero)
	{
	  break;
	}
    }
}

/*
 * float_numeric_normalize_for_hash() - Normalize a NUMERIC value for hashing
 *   num(in)       : original numeric value
 *   calc_buf(out) : normalized buffer (size: DB_NUMERIC_BUF_SIZE)
 *   precision(in) : precision of the input
 *   scale(in)     : scale of the input
 *
 * Note: 
 *   - This function ensures that equivalent numeric values (e.g., 1.0 and 1.00) 
 *     result in the same binary representation to produce identical hash values.
 *   - Case scale > 0: Trailing zeros are removed by dividing by powers of 10 
 *     using an optimized "Binary Skip" approach (e.g., 10^16, 10^8, etc.).
 *   - Case scale < 0: The value is multiplied by 10 to increase its precision 
 *     up to the maximum 40 digits.
 */
void
float_numeric_normalize_for_hash (DB_C_NUMERIC num, uint8_t * calc_buf, int precision, int scale)
{
  int tmp_scale = 0;
  uint64_t word_buf[NUMERIC_AS_WORDS] = { 0 };

  assert (scale != 0);

  if (numeric_is_zero (num))
    {
      memset (calc_buf, 0, DB_NUMERIC_BUF_SIZE);
      return;
    }

  numeric_bytes_to_words (num, DB_NUMERIC_BUF_SIZE, word_buf, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES);

  if (scale > 0)
    {
      /* divide by 10 repeatedly
       * remove trailing zeros by repeatedly dividing by powers of 10.
       * we use a chunked division (Binary Skip) approach with 10^19/16, 10^8, etc.,
       * to efficiently process multiple zeros in a single pass.
       */
      int i, step;
      uint64_t divisor, rem;
      static const int scale_steps[] = { 19, 8, 4, 2, 1 };
      static const int lookup_idx[] = { 18, 7, 3, 1, 0 };
      const int num_steps = 5;
      uint64_t backup_word[NUMERIC_AS_WORDS] = { 0 };

      tmp_scale = (scale > DB_MAX_NUMERIC_PRECISION ? DB_MAX_NUMERIC_PRECISION : scale);

      for (i = 0; i < num_steps; i++)
	{
	  step = scale_steps[i];
	  divisor = _gv_mul_normalize_pow10_lookup[lookup_idx[i]];
	  while (tmp_scale >= step)
	    {
	      /* backup the current word state to restore if division fails (remainder != 0) */
	      backup_word[0] = word_buf[0];
	      backup_word[1] = word_buf[1];
	      backup_word[2] = word_buf[2];

	      rem = float_numeric_div_pow10 (word_buf, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES, divisor);
	      if (rem == 0)
		{
		  tmp_scale -= step;
		}
	      else
		{
		  /* if the remainder is non-zero, restore the previous state and try a smaller step */
		  word_buf[0] = backup_word[0];
		  word_buf[1] = backup_word[1];
		  word_buf[2] = backup_word[2];
		  break;
		}
	    }
	}
    }
  else
    {
      /* multiply by 10
       * multiplication increases precision, but limited to
       * DB_MAX_NUMERIC_PRECISION(40) digits maximum.
       */
      int max_multiply = DB_MAX_NUMERIC_PRECISION - precision;
      tmp_scale = (-(scale) > max_multiply) ? max_multiply : -(scale);

      assert (tmp_scale > 0);

      if (tmp_scale > 0)
	{
	  float_numeric_mul_normalize (word_buf, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES, tmp_scale);
	}
    }

  numeric_words_to_bytes (word_buf, NUMERIC_AS_WORDS, calc_buf);
}

/*
 * numeric_coerce_num_to_double () -
 *   return:
 *   num(in)    : DB_VALUE
 *   scale(in)  : integer value of the scale
 *   adouble(out): ptr to the returned double value
 *
 * Note: This routine converts a DB_C_NUMERIC into a double precision value.
 */
void
numeric_coerce_num_to_double (const DB_VALUE * num_value, int scale, double *adouble)
{
  char num_string[TWICE_NUM_MAX_PREC + 2];	/* 2: Sign, Null terminate */

  assert (num_value);

  /* Convert the numeric to a decimal string */
  numeric_coerce_num_to_dec_str (num_value, num_string);

  /* Convert the decimal string into a double */
  /* Problem at precision with line below */
  /* 123.445 was converted to 123.444999999999999999 */
  *adouble = atof (num_string) / pow (10.0, scale);

  /* TODO: [CUBRIDSUS-2637] revert to early code for now. adouble = atof (num_string); for (i = 0; i < scale; i++)
   * adouble /= 10; */
}

/*
 * numeric_is_fraction_part_zero () - check if fractional part of a numeric is
 *				      equal to 0
 * return : boolean
 * num (in)   : numeric value
 * scale (in) : scale of the numeric
 */
static bool
numeric_is_fraction_part_zero (const DB_VALUE * num_value, const int scale)
{
  int i, len = 0;
  char dec_str[NUMERIC_MAX_STRING_SIZE];

  assert (num_value);

  numeric_coerce_num_to_dec_str (num_value, dec_str);
  len = strlen (dec_str);
  for (i = 0; i < scale; i++)
    {
      if (dec_str[len - scale + i] != '0')
	{
	  return false;
	}
    }
  return true;
}

/*
 * numeric_internal_double_to_num () -
 *   return: NO_ERROR, or ER_code
 *   adouble(in)        :
 *   dst_scale(in)      :
 *   num(in)    :
 *   prec(in)   :
 *   scale(in)  :
 *   is_value_negative(out): sign of the value
 */
int
numeric_internal_double_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
				bool * is_value_negative)
{
  assert (is_value_negative);
  return numeric_internal_real_to_num (adouble, dst_scale, num, prec, scale, false, is_value_negative);
}

/*
 * float_numeric_db_value_mod() - modulo two NUMERIC values and return the remainder
 *   return : NO_ERROR on success, or error code
 *
 * Note:
 *   - perform mod operation with double in existing db_mod_dbval.
 *   - float_numeric_db_value_mod() supports the extended NUMERIC range, 
 *     allowing operations with larger precision.
 *   - If the result precision exceeds DB_MAX_NUMERIC_PRECISION,
 *     the value is rounded and stored using only DB_MAX_NUMERIC_PRECISION significant digits.
 */
int
float_numeric_db_value_mod (const DB_VALUE * value1, const DB_VALUE * value2, DB_VALUE * result)
{
  int ret = NO_ERROR;
  int prec1 = 0, prec2 = 0, scale1 = 0, scale2 = 0;
  int extra_prec, needed_bytes, calc_words, calc_nbytes;
  int result_prec, result_scale;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE];
  bool result_sign = false;

  /* 
   * The following cases return 0 immediately without any operation
   * ex) 12 / 0 = err (=already handled in parsing)
   *     0 / 12 = 0
   */
  if (numeric_is_zero (db_locate_numeric (value1)))
    {
      memset (result_buf, 0, DB_NUMERIC_BUF_SIZE);
      result_prec = 1;
      result_scale = DB_VALUE_SCALE (value1);
      db_make_numeric (result, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, false, true);
      return NO_ERROR;
    }

  /* 1) compute exact precision values for mantissa calculations */
  db_get_numeric_precision_and_scale (value1, &prec1, &scale1, NULL);
  db_get_numeric_precision_and_scale (value2, &prec2, &scale2, NULL);

  /* 2) determine common sign of the result */
  result_sign = numeric_is_negative (value1);

  /* 3) compute exact exponent values for mantissa calculations */
  int dividend_exponent = (scale2 > scale1) ? (scale2 - scale1) : 0;
  int divisor_exponent = (scale1 > scale2) ? (scale1 - scale2) : 0;

  /* 4) determine exact scale for the result (MOD is independent of the quotient scale) */
  result_scale = (scale1 > scale2) ? scale1 : scale2;

  /* 5) initialize new calculation buffers and pad absolute values */
  extra_prec = (dividend_exponent > divisor_exponent) ? dividend_exponent : divisor_exponent;
  needed_bytes = _gv_numeric_precision_to_bytes_lookup[extra_prec];
  needed_bytes += ((int) DB_NUMERIC_BUF_SIZE + 2);
  calc_words = NUMERIC_GET_WORD_COUNT (needed_bytes);
  calc_nbytes = NUMERIC_GET_BYTE_COUNT (calc_words);

  uint64_t dividend_work[calc_words] = { 0 };
  uint64_t divisor_work[calc_words] = { 0 };
  uint64_t quotient_work[calc_words] = { 0 };
  uint64_t remainder_work[calc_words] = { 0 };

  numeric_bytes_to_words (db_locate_numeric (value1), DB_NUMERIC_BUF_SIZE, dividend_work, calc_words, calc_nbytes);
  numeric_bytes_to_words (db_locate_numeric (value2), DB_NUMERIC_BUF_SIZE, divisor_work, calc_words, calc_nbytes);

  /* 6) scale adjustments */
  if (dividend_exponent > 0)
    {
      float_numeric_mul_normalize (dividend_work, calc_words, calc_nbytes, dividend_exponent);
    }
  if (divisor_exponent > 0)
    {
      float_numeric_mul_normalize (divisor_work, calc_words, calc_nbytes, divisor_exponent);
    }

  /* fast path */
  if (calc_words == NUMERIC_AS_WORDS && (dividend_work[0] | dividend_work[1] | divisor_work[0] | divisor_work[1]) == 0)
    {
      float_numeric_mod_fast (dividend_work[2], divisor_work[2], result_buf, &result_prec, &result_scale, &result_sign);
      ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, result);
      if (ret == NO_ERROR)
	{
	  db_make_numeric (result, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);
	}
      return ret;
    }

  float_numeric_knuth_div ((knuth_digit_t *) dividend_work, (knuth_digit_t *) divisor_work,
			   (knuth_digit_t *) quotient_work, (knuth_digit_t *) remainder_work, calc_words);

  /* 7) check and recalculate precision/scale of the remainder result */
  result_prec = float_numeric_get_decimal_digit (remainder_work, calc_words);
  if (result_sign && result_prec == 1 && remainder_work[calc_words - 1] == 0)
    {
      /* Prevent -0; zero is always treated as positive. */
      result_sign = false;
    }
  ret = float_numeric_check_overflow_and_adjust_scale (&result_prec, &result_scale, result);
  if (ret != NO_ERROR)
    {
      return ret;
    }

  /* 8) round and pack to DB_NUMERIC_BUF_SIZE bytes */
  ret = float_numeric_round_and_pack (remainder_work, calc_words, calc_nbytes, result_buf, &result_prec, &result_scale);
  if (ret != NO_ERROR)
    {
      TP_DOMAIN *domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      db_value_domain_init (result, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      return ret;
    }

  /* 9) store result */
  db_make_numeric (result, result_buf, result_prec, result_scale, DB_NUMERIC_BUF_SIZE, result_sign, true);

  return ret;
}

/*
 * numeric_internal_float_to_num () - converts a float to a DB_C_NUMERIC
 *
 * return: NO_ERROR or ER_code
 * afloat(in): floating-point value to be converted to NUMERIC
 * dst_scale(in): expected scale for the destination NUMERIC type
 * num(in): an allocated DB_C_NUMERIC to be filled with the converted numeric
 *	    value
 * prec(out): resulting precision of the converted value
 * scale(out): resulting scale of the converted value
 */
int
numeric_internal_float_to_num (float afloat, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
			       bool * is_value_negative)
{
  assert (is_value_negative);
  return numeric_internal_real_to_num (afloat, dst_scale, num, prec, scale, true, is_value_negative);
}

/*
 * fp_value_type() - returns the type of a given value of type double, as one
 *		     of the above enumerators.
 *
 * returns: the type of the passed-in floating-point value
 * d(in):   floating-point value whose type is to be returned
 */
FP_VALUE_TYPE
get_fp_value_type (double d)
{
#ifdef WINDOWS
  /* actually the following symbols are dependent on the _MSC macro, not the WINDOWS macro */
  switch (_fpclass (d))
    {
    case _FPCLASS_NINF:	/* -Inf */
    case _FPCLASS_PINF:	/* +Inf */
      return FP_VALUE_TYPE_INFINITE;

    case _FPCLASS_SNAN:	/* signaling NaN */
    case _FPCLASS_QNAN:	/* quiet NaN */
      return FP_VALUE_TYPE_NAN;

    case _FPCLASS_NZ:		/* -0 */
    case _FPCLASS_PZ:		/* +0 */
      return FP_VALUE_TYPE_ZERO;

    default:
      return FP_VALUE_TYPE_NUMBER;
    }
#else
  switch (std::fpclassify (d))
    {
    case FP_INFINITE:
      return FP_VALUE_TYPE_INFINITE;
    case FP_NAN:
      return FP_VALUE_TYPE_NAN;
    case FP_ZERO:
      return FP_VALUE_TYPE_ZERO;
    default:
      return FP_VALUE_TYPE_NUMBER;
    }
#endif
}

/*
 * numeric_internal_real_to_num() - converts a floating point value (float or
 *				    double) to a DB_C_NUMERIC.
 *
 * return: NO_ERROR or ER_code
 * adouble(in):	floating-point value to be converted to NUMERIC. May be either
 *		float promoted to double, or a double.
 * dst_scale(in):   expected scale of the destination NUMERIC data type
 * prec(out):	    resulting precision of the converted value
 * scale(out):	    resulting scale of the converted value
 * is_float(in):    indicates adouble is a float promoted to double
 * is_value_negative(out): sign of the value
 */
int
numeric_internal_real_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale, bool is_float,
			      bool * is_value_negative)
{
  char numeric_str[MAX (TP_DOUBLE_AS_CHAR_LENGTH + 1, NUMERIC_MAX_STRING_SIZE)];
  int i = 0;

  assert (is_value_negative);

  switch (get_fp_value_type (adouble))
    {
    case FP_VALUE_TYPE_INFINITE:
      return ER_IT_DATA_OVERFLOW;
    case FP_VALUE_TYPE_NAN:
    case FP_VALUE_TYPE_ZERO:
      /* currently CUBRID returns 0 for a NaN converted to NUMERIC (??) */
      *scale = dst_scale;
      *prec = dst_scale ? dst_scale : 1;

      while (i < *prec)
	{
	  numeric_str[i++] = '0';
	}
      numeric_str[i] = '\0';

      numeric_coerce_dec_str_to_num (numeric_str, num, is_value_negative);
      return NO_ERROR;
    default:
      /* compare against pow(10, DB_MAX_NUMERIC_PRECISION) to check for overflow/underflow before actual conversion */
      if (NUMERIC_ABS (adouble) > DB_NUMERIC_OVERFLOW_LIMIT)
	{
	  return ER_IT_DATA_OVERFLOW;
	}
      else
	{
	  if (NUMERIC_ABS (adouble) < DB_NUMERIC_UNDERFLOW_LIMIT)
	    {
	      /* the floating-point number underflows any possible CUBRID NUMERIC domain type, so just return 0 with no
	       * other conversion */
	      *scale = dst_scale;
	      *prec = dst_scale ? dst_scale : 1;

	      while (i < *prec)
		{
		  numeric_str[i++] = '0';
		}
	      numeric_str[i] = '\0';

	      numeric_coerce_dec_str_to_num ("0", num, is_value_negative);
	      return NO_ERROR;
	    }
	  else
	    {
	      /* adouble might fit into a CUBRID NUMERIC domain type with sufficient precision. Invoke _dtoa() to get
	       * the sequence of digits and the decimal point position */
	      int decpt, sign;
	      char *rve;
	      int ndigits;

	      if (is_float)
		{
		  _dtoa (adouble, 0, TP_FLOAT_MANTISA_DECIMAL_PRECISION, &decpt, &sign, &rve, numeric_str + 1, 0);

		  numeric_str[TP_FLOAT_MANTISA_DECIMAL_PRECISION + 1] = '\0';
		}
	      else
		{
		  _dtoa (adouble, 0, TP_DOUBLE_MANTISA_DECIMAL_PRECISION, &decpt, &sign, &rve, numeric_str + 1, 0);

		  numeric_str[TP_DOUBLE_MANTISA_DECIMAL_PRECISION + 1] = '\0';
		}

	      /* shift the digits in the sequence to make room for and to reach the decimal point */
	      ndigits = strlen (numeric_str + 1);

	      if (decpt <= 0)
		{
		  /*
		   * ex) 1.234e-10  (= 0.0000000001234)
		   *   - digits="1234", ndigits=4, decpt=-9
		   *   - scale = -decpt + ndigits = 9 + 4 = 13
		   *
		   * legacy:
		   *   - materialize leading fractional zeros by shifting digits and prepending '0'
		   *     -> "0000000001234" (then truncation by DB_MAX_NUMERIC_PRECISION may drop significant digits)
		   *
		   * new:
		   *   - keep only significant digits and derive domain from (decpt, ndigits)
		   *     -> "1234" with (prec=4, scale=13)
		   */
		  *scale = -decpt + ndigits;
		  *prec = ndigits;

		  if (*prec > DB_MAX_NUMERIC_PRECISION)
		    {
		      *scale -= (*prec - DB_MAX_NUMERIC_PRECISION);
		      *prec = DB_MAX_NUMERIC_PRECISION;
		    }

		  /* keep only significant digits */
		  numeric_str[1 + *prec] = '\0';
		}
	      else
		{
		  /* the numer is greater than 1, either insert the decimal point at the correct position in the digits
		   * sequence, or append 0s to the digits from left to right until the decimal point is reached. */

		  if (decpt > (DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE))
		    {
		      /* should not happen since overflow has been checked for previously */
		      return ER_IT_DATA_OVERFLOW;
		    }
		  else
		    {
		      if (decpt < ndigits)
			{
			  *prec = ndigits;
			  *scale = ndigits - decpt;
			}
		      else
			{
			  /* append 0s to the digits sequence until the decimal point is reached */

			  char *dst = numeric_str + 1 + decpt, *src = numeric_str + 1 + ndigits;

			  while (src != dst)
			    {
			      *src++ = '0';
			    }

			  *src = '\0';

			  *prec = decpt;
			  *scale = 0;
			}

		      if (*prec > DB_MAX_NUMERIC_PRECISION)
			{
			  /* this path is taken only for inputs with a positive exponent (e.g., 1.0e+?).
			   * in this case, the scale may be adjusted to a negative value. */
			  *scale += (DB_MAX_NUMERIC_PRECISION - *prec);
			  *prec = DB_MAX_NUMERIC_PRECISION;
			}
		    }
		}

	      /* append zeroes until dst_scale is reached */
	      while (*prec < DB_MAX_NUMERIC_PRECISION && *scale < dst_scale)
		{
		  numeric_str[1 + *prec] = '0';
		  (*prec)++;
		  (*scale)++;
		}

	      numeric_str[1 + *prec] = '\0';

	      /* The number without sign is now written in decimal in numeric_str */

	      if (sign)
		{
		  numeric_str[0] = '-';
		  numeric_coerce_dec_str_to_num (numeric_str, num, is_value_negative);
		}
	      else
		{
		  numeric_coerce_dec_str_to_num (numeric_str + 1, num, is_value_negative);
		}

	      return NO_ERROR;
	    }
	}
      break;
    }
}

/*
 * analyze_numeric_string() () -
 *   return:
 *   astring(in) : Input numeric string to analyze
 *   astring_length(in) : Length of input string
 *   codeset(in) : International codeset for character handling
 *   negate_value(out) : Whether the value should be negated
 *   int_digits(out) : Extracted integer part digits
 *   int_len(out) : Length of integer part
 *   frac_digits(out) : Extracted fractional part digits
 *   frac_len(out) : Length of fractional part
 *   frac_first_sig_digit(out) : First non-zero position in fractional part
 *   frac_last_sig_digit(out) : Last non-zero position in fractional part
 *
 * Note: Parse and analyze numeric string to extract integer/fractional components
 */
static int
analyze_numeric_string (const char *astring, int astring_length, INTL_CODESET codeset, bool * negate_value,
			char *int_digits, int *int_len, char *frac_digits, int *frac_len, int *frac_first_sig_digit,
			int *frac_last_sig_digit, bool * is_zero)
{
  const int int_buf_max = (DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE);	/* 254 */
  const int frac_buf_max = DB_MAX_NUMERIC_SCALE;	/* 252, excludes leading "0." */
  int int_count = 0;
  int frac_count = 0;
  int parse_pos = 0;
  int skip = 1;
  char current_char = '\0';
  bool has_digit = false;
  bool pad_character_zero = false;
  bool sign_found = false;
  bool trailing_spaces = false;

  *frac_first_sig_digit = *frac_last_sig_digit = -1;
  *negate_value = false;

  /* Step 1: Handle spaces, signs, and leading zeros */
  while (parse_pos < astring_length)
    {
      current_char = astring[parse_pos];

      if (current_char >= '1' && current_char <= '9')
	{
	  has_digit = true;
	  break;
	}
      else if (current_char == '0')
	{
	  /* leading pad '0' found */
	  pad_character_zero = true;
	  parse_pos++;
	  continue;
	}
      else if (current_char == '.')
	{
	  break;
	}
      else if (current_char == '+' || current_char == '-')
	{
	  /* Sign is only allowed before any digit (rejects duplicates and signs after a leading zero, e.g. '0-1') */
	  if (sign_found || pad_character_zero)
	    {
	      return DOMAIN_INCOMPATIBLE;
	    }
	  sign_found = true;
	  if (current_char == '-')
	    {
	      *negate_value = true;
	    }
	  parse_pos++;
	  continue;
	}
      else if (intl_is_space (astring + parse_pos, NULL, codeset, &skip))
	{
	  /* A space after a sign without any digit is invalid (e.g. '-   1'). */
	  if (sign_found && !pad_character_zero)
	    {
	      return DOMAIN_INCOMPATIBLE;
	    }
	  /* A space after a leading zero ends the leading phase (e.g. '0   1' is invalid). */
	  if (pad_character_zero)
	    {
	      trailing_spaces = true;
	      break;
	    }
	  parse_pos += skip;	/* Skip spaces */
	  continue;
	}
      else
	{
	  /* Stray Non-numeric compatible character */
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  /* Step 2: Parse integer part */
  while (parse_pos < astring_length)
    {
      current_char = astring[parse_pos];

      if (current_char >= '0' && current_char <= '9' && !trailing_spaces)
	{
	  pad_character_zero = false;
	  has_digit = true;	/* Case: "0.0000" */
	  if (int_count >= int_buf_max)	/* overflow when integer part exceeds the storable digit bound */
	    {
	      return ER_IT_DATA_OVERFLOW;
	    }
	  int_digits[int_count++] = current_char;
	  parse_pos++;
	  continue;
	}
      else if (current_char == '.' && !trailing_spaces)
	{
	  /* '.' is only allowed immediately after a digit, not after trailing spaces (e.g. '1   .' is invalid) */
	  parse_pos++;
	  break;
	}
      else if (trailing_spaces && !intl_is_space (astring + parse_pos, NULL, codeset, &skip))
	{
	  return DOMAIN_INCOMPATIBLE;
	}
      else if (intl_is_space (astring + parse_pos, NULL, codeset, &skip))
	{
	  if (!trailing_spaces)
	    {
	      trailing_spaces = true;
	    }
	  parse_pos += skip;	/* Skip spaces */
	  continue;
	}
      else if (current_char == ',')
	{
	  /* Accept ',' character on integer part. */
	  parse_pos++;
	  continue;
	}
      else
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  /* Step 3: Parse fractional part */
  while (parse_pos < astring_length)
    {
      current_char = astring[parse_pos];

      if (current_char >= '0' && current_char <= '9' && !trailing_spaces)
	{
	  has_digit = true;	/* Case: "0.0000" */
	  if (frac_count <= frac_buf_max)	/* accept one extra digit beyond the storable bound, reserved for the round-decision */
	    {
	      frac_digits[frac_count] = current_char;
	      /* Track non-zero digits */
	      if (current_char != '0' || *frac_first_sig_digit >= 0)
		{
		  if (*frac_first_sig_digit < 0)
		    {
		      *frac_first_sig_digit = frac_count;
		      pad_character_zero = false;
		    }
		  *frac_last_sig_digit = frac_count;
		}
	      frac_count++;
	    }
	  else if (*frac_first_sig_digit < 0)
	    {
	      /* fractional underflow: no significant digit found within representable bound → zero */
	      pad_character_zero = true;
	    }
	  /* else: sig already found beyond bound — simply truncated; determine_prec_scale handles round/ovf */
	  parse_pos++;
	  continue;
	}
      else if (trailing_spaces && !intl_is_space (astring + parse_pos, NULL, codeset, &skip))
	{
	  return DOMAIN_INCOMPATIBLE;
	}
      else if (intl_is_space (astring + parse_pos, NULL, codeset, &skip))
	{
	  if (!trailing_spaces)
	    {
	      trailing_spaces = true;
	    }
	  parse_pos += skip;	/* Skip spaces */
	  continue;
	}
      else
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  if (pad_character_zero)
    {
      /* Zero padding case examples:
       * 0 : pad_character_zero(t), has_digit(f), int_count(0), frac_count(0)
       * 0.0 : pad_character_zero(t), has_digit(t), int_count(0), frac_count(1)
       */
      int_digits[0] = '\0';
      frac_digits[0] = '\0';
      *int_len = 1;
      *frac_len = frac_count;
      *is_zero = true;
    }
  else if (!has_digit && int_count == 0 && frac_count == 0)
    {
      /*
       * no valid digit was found in input.
       * reject strings that do not contain any numeric digit.
       *
       * examples:
       *   '+', '-'                  (sign only)
       *   '.', ' . ', '   .   '     (decimal point only)
       *   '', ' ', '    '               (whitespace only)
       *   '+.', '-.', ' + . '       (sign + non-digit combinations)
       */
      return DOMAIN_INCOMPATIBLE;
    }
  else
    {
      /* Normal case: add null terminators based on filled length */
      int_digits[int_count] = '\0';
      frac_digits[frac_count] = '\0';
      *int_len = int_count;
      *frac_len = frac_count;
    }

  return NO_ERROR;
}

/*
 * determine_prec_scale() () -
 *   return:
 *   int_digits(in) : Integer part digits
 *   int_len(in) : Length of integer part
 *   frac_digits(in) : Fractional part digits
 *   frac_len(in) : Length of fractional part
 *   frac_first_sig_digit(in) : First non-zero position in fractional part
 *   frac_last_sig_digit(in) : Last non-zero position in fractional part
 *   out_num_string(out) : Output numeric string
 *   out_prec(out) : Calculated precision
 *   out_scale(out) : Calculated scale
 *
 * Note: Calculate precision and scale based on numeric string analysis
 */
static void
determine_prec_scale (const char *int_digits, int int_len, const char *frac_digits, int frac_len,
		      int frac_first_sig_digit, int frac_last_sig_digit, char *out_num_string, int *out_prec,
		      int *out_scale)
{
  int total = int_len + frac_len;
  const char *tmp_int_digits = NULL, *tmp_frac_digits = NULL;
  int tmp_int_len = 0, tmp_frac_len = 0;
  char next_digit = '0';
  int tmp_prec = 0, tmp_scale = 0;
  int frac_zero_cnt = 0;
  bool need_round = false;

  /* Step 1: Calculate temporary precision/scale and determine copy positions for each case */
  if (frac_len == 0)
    {
      /* Case 1: Only integer part exists */
      if (int_len <= DB_MAX_NUMERIC_PRECISION)
	{
	  /* Case 1-A: When length is within 40 digits, use only the integer part */
	  tmp_prec = int_len;
	  tmp_scale = 0;
	  tmp_int_digits = int_digits;
	  tmp_int_len = int_len;
	}
      else
	{
	  /* Case 1-B: Apply negative scale based on trailing zero count */
	  tmp_prec = DB_MAX_NUMERIC_PRECISION;
	  tmp_scale = DB_MAX_NUMERIC_PRECISION - int_len;
	  tmp_int_digits = int_digits;
	  tmp_int_len = DB_MAX_NUMERIC_PRECISION;
	  /* Get the 41th digit for rounding decision (array index 40 since arrays start from 0) */
	  next_digit = tmp_int_digits[DB_MAX_NUMERIC_PRECISION];
	  need_round = true;
	}
    }
  else if (int_len == 0)
    {
      /* Case 2: Only fractional part exists */
      int nz_len = frac_last_sig_digit - frac_first_sig_digit + 1;
      if (frac_len <= DB_MAX_NUMERIC_PRECISION)
	{
	  /* Case 2-A: When length is within 40 digits, use only the fractional part. 
	     Precision is defined from the left-most nonzero digit to the right-most known digit. */
	  tmp_prec = nz_len;
	  tmp_scale = frac_len;
	  tmp_frac_digits = frac_digits;
	  tmp_frac_len = frac_len;
	}
      else
	{
	  /* Case 2-B: Skip leading zeros in the fractional part and use up to MAX_PRECISION digits */
	  if (nz_len > DB_MAX_NUMERIC_PRECISION)
	    {
	      tmp_prec = DB_MAX_NUMERIC_PRECISION;
	      tmp_scale = MIN (frac_len, DB_MAX_NUMERIC_SCALE);
	      tmp_frac_digits = frac_digits + frac_first_sig_digit;
	      tmp_frac_len = DB_MAX_NUMERIC_PRECISION;
	      next_digit = frac_digits[frac_first_sig_digit + DB_MAX_NUMERIC_PRECISION];
	      need_round = true;
	      frac_zero_cnt = frac_len - nz_len;	/* Count pure zeros */
	    }
	  else
	    {
	      tmp_prec = nz_len;
	      tmp_scale = frac_len;
	      tmp_frac_digits = frac_digits + frac_first_sig_digit;
	      tmp_frac_len = nz_len;
	      need_round = false;
	      if (tmp_scale > DB_MAX_NUMERIC_SCALE)
		{
		  next_digit = tmp_frac_digits[nz_len - 1];
		  frac_zero_cnt = frac_len - nz_len;	/* Count pure zeros */
		  need_round = true;
		}
	    }
	}
    }
  else
    {
      /* Case 3: Both integer and fractional parts exist */
      if (total <= DB_MAX_NUMERIC_PRECISION)
	{
	  /* Case 3-A: When total length is within 40 digits, use both integer and fractional parts */
	  tmp_prec = total;
	  tmp_scale = frac_len;
	  tmp_int_digits = int_digits;
	  tmp_int_len = int_len;
	  tmp_frac_digits = frac_digits;
	  tmp_frac_len = frac_len;
	}
      else
	{
	  /* Case 3-B: When total length exceeds 40 digits, determine whether to round or apply negative scale 
	     depending on which part (integer vs fractional) has more digits */
	  int drop_total = total - DB_MAX_NUMERIC_PRECISION;
	  if (drop_total <= frac_len)
	    {
	      /* Case 3-B-1: Fractional part is larger – round based on the fractional part */
	      int keep_frac = frac_len - drop_total;
	      tmp_prec = int_len + keep_frac;
	      tmp_scale = keep_frac;
	      tmp_int_digits = int_digits;
	      tmp_int_len = int_len;
	      tmp_frac_digits = frac_digits;
	      tmp_frac_len = keep_frac;
	      next_digit = frac_digits[keep_frac];
	      need_round = true;
	    }
	  else
	    {
	      /* Case 3-B-2: Integer part is larger – handle by trimming the integer part */
	      int drop_int = drop_total - frac_len;
	      int keep_int = int_len - drop_int;
	      tmp_prec = keep_int;
	      tmp_scale = -drop_int;
	      tmp_int_digits = int_digits;
	      tmp_int_len = keep_int;
	      next_digit = int_digits[keep_int];
	      need_round = true;
	    }
	}
    }

  /* Step 2: Range validation (before memcpy) */
  if (tmp_prec > DB_MAX_NUMERIC_PRECISION || tmp_scale < DB_MIN_NUMERIC_SCALE)
    {
      // Error handling done outside
      *out_prec = tmp_prec;
      *out_scale = tmp_scale;
      return;
    }

  /* Step 3: Actual string copy */
  char *tmp_num_string = out_num_string;
  if (tmp_int_len)
    {
      memcpy (tmp_num_string, tmp_int_digits, tmp_int_len);
      tmp_num_string += tmp_int_len;
    }

  if (tmp_frac_len)
    {
      memcpy (tmp_num_string, tmp_frac_digits, tmp_frac_len);
      tmp_num_string += tmp_frac_len;
    }
  *tmp_num_string = '\0';

  /* Step 4: Rounding and digit adjustment */
  if (need_round)
    {
      (void) determine_round (out_num_string, &tmp_prec, &tmp_scale, tmp_int_len, tmp_frac_len, frac_zero_cnt,
			      next_digit);
    }

  *out_prec = tmp_prec;
  *out_scale = tmp_scale;
}

/*
 * determine_round() () -
 *   return:
 *   out_str(in/out) : Numeric string to be rounded
 *   out_prec(in/out) : Precision (total significant digits)
 *   out_scale(in/out) : Scale (fractional digits)
 *   tmp_int_len(in) : Integer part length
 *   tmp_frac_len(in) : Fractional part length
 *   frac_zero_cnt(in) : Leading zeros in fractional part
 *   rounding_digit(in) : Next digit for rounding decision
 *
 * Note: Round numeric string and adjust precision/scale
 */
static void
determine_round (char *out_str, int *out_prec, int *out_scale, int tmp_int_len, int tmp_frac_len, int frac_zero_cnt,
		 char next_digit)
{
  int prec = *out_prec;
  int scale = *out_scale;
  int digit_pos = prec - 1;
  int max_frac_zero_cnt = 212;	// DB_MAX_NUMERIC_SCALE(252) - DB_MAX_NUMERIC_PRECISION(40)

  // Step 1: Scale exceeds maximum limit (252) - truncate first (pure decimal case only)
  if (scale > DB_MAX_NUMERIC_SCALE && tmp_int_len == 0 && frac_zero_cnt > max_frac_zero_cnt)
    {
      prec--;
      if (prec > 0)
	{
	  // Normal truncate: 0.000...000123 (scale 253) -> 0.000...00012 (scale 252)
	  out_str[prec] = '\0';
	  digit_pos = prec - 1;
	}
      else
	{
	  // Underflow case: 0.000...0001 (prec 1, scale 253) -> 0.000...0000 (prec 1, scale 252)
	  out_str[0] = '0';
	  out_str[1] = '\0';
	  frac_zero_cnt--;
	  prec = 1;
	  digit_pos = 0;
	}
    }

  // Step 2: Perform rounding based on the truncated digit
  if (next_digit >= '5')
    {
      while (digit_pos >= 0 && out_str[digit_pos] == '9')
	{
	  out_str[digit_pos] = '0';
	  digit_pos--;
	}

      if (digit_pos >= 0)
	{
	  // Normal case: 123.456...5 -> 123.456...6
	  out_str[digit_pos] += 1;
	}
      else
	{
	  // All digits were '9', overflow occurred - create "1000..." pattern
	  out_str[0] = '1';
	  if (tmp_int_len == 0)
	    {
	      if (frac_zero_cnt == 0)
		{
		  // Pure decimal case: 0.9999... -> 1.000...
		  tmp_int_len = 1;
		  prec = DB_MAX_NUMERIC_PRECISION;
		  scale = (prec - 1);
		}
	      else
		{
		  // Pure decimal case: 0.000...000999 -> 0.000...001
		  frac_zero_cnt--;

		  if (scale > DB_MAX_NUMERIC_SCALE)
		    {
		      prec++;
		    }
		  memset (out_str + 1, '0', prec - 1);
		}
	    }
	  else
	    {
	      // Integer with decimal: 99999.99999... -> 100000.0
	      memset (out_str + 1, '0', prec);
	      scale -= 1;
	    }
	}
    }

  // Step 3: Add null terminator
  out_str[prec] = '\0';

  // Step 4: Recalculate scale based on rounding result
  if (tmp_int_len == 0)
    {
      // Pure decimal case: 0.8888... -> 0.888...9
      scale = frac_zero_cnt + prec;
    }

  *out_prec = prec;
  *out_scale = scale;
}

/*
 * numeric_coerce_string_to_num () -
 *   return:
 *   astring(in) : ptr to the input character string
 *   astring_length(in) : length of the input character string
 *   codeset(in) : codeset of string
 *   result(out) : DB_VALUE of type numeric
 *
 * Note: This routine converts a string into a DB_VALUE.
 *	 It is not localized in relation to fractional and digit
 *	 grouping symbols.
 */
int
numeric_coerce_string_to_num (const char *astring, int astring_length, INTL_CODESET codeset, DB_VALUE * result)
{
  char num_string[DB_MAX_NUMERIC_PRECISION + 1];
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  bool negate_value = false;
  bool is_zero = false;
  int prec, scale;
  int int_len, frac_len;
  int frac_first_sig_digit, frac_last_sig_digit;
  char int_digits[NUMERIC_MAX_STRING_SIZE];	/* Integer part valid digits */
  char frac_digits[NUMERIC_MAX_STRING_SIZE];	/* Fractional part valid digits */
  int ret = NO_ERROR;
  TP_DOMAIN *domain;

  /* Parse and compute precision/scale */
  ret =
    analyze_numeric_string (astring, astring_length, codeset, &negate_value, int_digits, &int_len, frac_digits,
			    &frac_len, &frac_first_sig_digit, &frac_last_sig_digit, &is_zero);
  if (ret != NO_ERROR)
    {
      if (ret == ER_IT_DATA_OVERFLOW)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	}
      goto exit_on_error;
    }

  assert (int_len > 0 || frac_len > 0);
  if (is_zero)
    {
      /* Zero case */
      prec = 1;
      scale = MIN (frac_len, DB_MAX_NUMERIC_SCALE);
      num_string[0] = '0';
      num_string[1] = '\0';
      negate_value = false;
    }
  else
    {
      (void) determine_prec_scale (int_digits, int_len, frac_digits, frac_len, frac_first_sig_digit,
				   frac_last_sig_digit, num_string, &prec, &scale);
      assert (scale <= DB_MAX_NUMERIC_SCALE);
      /* If there is no overflow, try to parse the decimal string */
      if (prec > DB_MAX_NUMERIC_PRECISION || scale < DB_MIN_NUMERIC_SCALE)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  /* Convert decimal string to base-256 binary */
  numeric_coerce_dec_str_to_num (num_string, num, NULL);

  db_make_numeric (result, num, prec, scale, DB_NUMERIC_BUF_SIZE, negate_value, true);

  return ret;

exit_on_error:

  db_value_domain_init (result, DB_TYPE_NUMERIC, DB_DEFAULT_NUMERIC_PRECISION, DB_DEFAULT_NUMERIC_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * numeric_coerce_num_to_num () -
 *   return: NO_ERROR, or ER_code
 *   src_value(in)       : Pointer to the source DB_VALUE
 *   src_prec(in)        : Precision of the source numeric
 *   src_scale(in)       : Scale of the source numeric
 *   dest_prec(in)       : Target precision
 *   dest_scale(in)      : Target scale
 *   dest_num(out)       : Resulting DB_C_NUMERIC buffer
 *   dest_num_is_negative(out): Pointer to store the sign of the result
 * Note: This routine converts a numeric of a given precision and scale to
 * another precision and scale.
 */
int
numeric_coerce_num_to_num (const DB_VALUE * src_value, int src_prec, int src_scale, int dest_prec, int dest_scale,
			   DB_C_NUMERIC dest_num, bool * dest_num_is_negative)
{
  int ret = NO_ERROR;
  int src_actual_prec = 0;
  int final_prec = 0;
  bool is_value_negative = false;
  bool round_up = false;

  assert (src_value);
  assert (dest_num_is_negative);

  is_value_negative = numeric_is_negative (src_value);

  /* 1. trivial case: copy immediately if no conversion is needed */
  if (dest_prec == DB_DEFAULT_NUMERIC_PRECISION || (src_prec <= dest_prec && src_scale == dest_scale))
    {
      numeric_copy (dest_num, db_locate_numeric (src_value));
      *dest_num_is_negative = is_value_negative;
      return NO_ERROR;
    }

  /* 2. for fixed numeric values, check the actual number of significant digits in the input. */
  src_actual_prec = numeric_get_precision_digits (db_locate_numeric (src_value));

  /* 3. fast zero check: if the significant digit count is 1 and the last byte of the buffer is 0, 
   *    it is guaranteed to be zero (zero is treated as precision 1). */
  if (src_actual_prec == 1 && (db_locate_numeric (src_value))[DB_NUMERIC_BUF_SIZE - 1] == 0)
    {
      numeric_zero (dest_num, DB_NUMERIC_BUF_SIZE);
      *dest_num_is_negative = false;
      return NO_ERROR;
    }

  uint64_t result_word[NUMERIC_AS_WORDS] = { 0 };
  int scale_diff = dest_scale - src_scale;
  int required_prec = src_actual_prec + scale_diff;

  /* 4. pre-check for overflow and underflow (guaranteed zero). */
  if (required_prec > dest_prec)
    {
      ret = ER_IT_DATA_OVERFLOW;
      goto exit_on_error;
    }
  else if (required_prec < 0)
    {
      numeric_zero (dest_num, DB_NUMERIC_BUF_SIZE);
      *dest_num_is_negative = false;
      return NO_ERROR;
    }

  /* 5. prepare a temporary working buffer (17 bytes) and copy. */
  numeric_bytes_to_words (db_locate_numeric (src_value), DB_NUMERIC_BUF_SIZE, result_word, NUMERIC_AS_WORDS,
			  NUMERIC_AS_WORD_BYTES);

  /* 6. scale adjustment (aligning the decimal position). */
  if (scale_diff > 0)
    {
      /* increase scale: multiply by 10^delta. */
      float_numeric_mul_normalize (result_word, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES, scale_diff);
    }
  else if (scale_diff < 0)
    {
      /* decrease scale: perform truncation and rounding decisions. */
      int drop = -scale_diff;
      uint8_t last_digit = float_numeric_div_normalize (result_word, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES, drop);

      /* half-up rounding */
      if (last_digit >= ROUND_HALF_UP_DIGIT)
	{
	  (void) float_numeric_increment (result_word, NUMERIC_AS_WORDS, 1);
	  round_up = true;
	}
    }

  /* 7. determine the final precision (final_prec) and re-check for overflow during rounding. */
  final_prec = (required_prec == 0) ? 1 : required_prec;
  if (round_up)
    {
      /* scan the actual buffer to accurately check for precision changes after rounding (e.g., 9.9 -> 10.0). */
      final_prec = float_numeric_get_decimal_digit (result_word, NUMERIC_AS_WORDS);
      if (final_prec > dest_prec)
	{
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  /* 8. save result and prevent negative zero. */
  numeric_words_to_bytes (result_word, NUMERIC_AS_WORDS, dest_num);

  /* if final_prec is 1 and LSB is 0, it's always zero (covers cases where computation result becomes zero). */
  if (is_value_negative && final_prec == 1 && dest_num[DB_NUMERIC_BUF_SIZE - 1] == 0)
    {
      is_value_negative = false;
    }
  *dest_num_is_negative = is_value_negative;

  return NO_ERROR;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * get_significant_digit () -
 *   return: significant digit of integer value
 *   i(in) :
 */
static int
get_significant_digit (DB_BIGINT i)
{
  int n = 0;

  do
    {
      n++;
      i /= 10;
    }
  while (i != 0);

  return n;
}

/*
 * float_numeric_mul_pow10() - Multiply a base-256 big-endian buffer by multiplier
 *
 * Note:
 *   - Used for scale adjustment by shifting the decimal position right.
 *   - The multiplier must be a power of 10 that fits in a uint64_t.
 *   - Processes in 64-bit word chunks using __int128 for high performance.
 *   - Expects no overflow after multiplication (assert(carry == 0)).
 */
static void
float_numeric_mul_pow10 (uint64_t * dbv_buf, int calc_words, int calc_bytes, uint64_t multiplier)
{
  int i = 0;
  uint128_t res = 0;
  uint128_t carry = 0;

  uint64_t *word = dbv_buf;
  for (i = calc_words - 1; i >= 0; i--)
    {
      res = (uint128_t) word[i] * multiplier + carry;
      word[i] = (uint64_t) res;
      carry = res >> 64;
    }

  assert (carry == 0);
}

/*
 * float_numeric_mul_normalize() - multiply a base-256 big-endian buffer by 10^exponent
 *
 * Purpose:
 *  - adjust the internal decimal scale by multiplying the stored coefficient by 10^exponent.
 *    this is mainly used to align decimal positions between two values before arithmetic or
 *    formatting (e.g., shifting digits into the integer part for negative scales, or
 *    rescaling the counterpart operand so both share a common decimal position).
 *
 * Note:
 *  - the exponent is processed in chunks to prevent internal arithmetic overflow.
 *
 * Reason:
 *   - 64-bit word-wise multiplication using __int128:
 *     Temp = (word * 10^k) + carry must fit in uint128_t (~3.4 * 10^38).
 *     - If k=19: Max temp ~ 2^64 * 10^19 ~ 1.84 * 10^38 -> SAFE.
 *     - If k=20: Max temp ~ 2^64 * 10^20 ~ 1.84 * 10^39 -> Potential overflow.
 *     ==> Safe chunk size is 19.
 */
static void
float_numeric_mul_normalize (uint64_t * dbv_buf, int calc_words, int calc_bytes, int exponent)
{
  int step = 0;
  uint64_t multiplier = 0;

  assert (exponent > 0);

  while (exponent > 0)
    {
      step = (exponent > 19) ? 19 : exponent;
      multiplier = _gv_mul_normalize_pow10_lookup[step - 1];	// 10^step

      float_numeric_mul_pow10 (dbv_buf, calc_words, calc_bytes, multiplier);
      exponent -= step;
    }
}

/*
 * float_numeric_div_pow10() - Divide a base-256 big-endian buffer by divisor
 *
 * Note:
 *   - Used for scale reduction, rounding, and normalization.
 *   - The divisor must be a power of 10 that fits in a uint64_t.
 *   - Processes in 64-bit word chunks using __int128 for high performance.
 */
static uint64_t
float_numeric_div_pow10 (uint64_t * dbv_buf, int calc_words, int calc_bytes, uint64_t divisor)
{
  uint64_t rem10 = 0;
  int i = 0;

  uint128_t temp = 0;
  uint64_t *word_ptr = dbv_buf;

  for (i = 0; i < calc_words; i++)
    {
      temp = ((uint128_t) rem10 << 64) | word_ptr[i];
      word_ptr[i] = (uint64_t) (temp / divisor);
      rem10 = (uint64_t) (temp % divisor);
    }

  return rem10;
}

/*
 * float_numeric_div_normalize() - divide a base-256 big-endian buffer by 10^exponent
 *
 * Important:
 *   - this function MUST NOT be used to detect or remove trailing zeros.
 *     it adjusts decimal position by dividing the internal coefficient by 10^exponent
 *     for normalization and rounding, but it does not preserve information needed
 *     for trailing-zero analysis.
 *
 * Purpose:
 *   1. Adjust the coefficient by dividing it by 10^exponent.
 *   2. Return the MSB of the discarded fractional part to decide rounding.
 *
 *  example:
 *    a 50-digit value with exponent = 7 becomes a 43-digit value.
 *    the function returns the most significant digit of the discarded 7-digit block
 *    (i.e., the 44th digit) for rounding decision.
 *
 * Note:
 *   - Processes the exponent in chunks (19 digits) to prevent overflow,
 *     consistent with float_numeric_mul_normalize().
 */
static int
float_numeric_div_normalize (uint64_t * dbv_buf, int calc_words, int calc_bytes, int exponent)
{
  uint64_t last_rem = 0;
  uint64_t divisor = 0;
  int step = 0;
  int last_step = 0;

  assert (exponent > 0);

  while (exponent > 0)
    {
      step = exponent > 19 ? 19 : exponent;
      divisor = _gv_mul_normalize_pow10_lookup[step - 1];

      last_rem = float_numeric_div_pow10 (dbv_buf, calc_words, calc_bytes, divisor);
      last_step = step;
      exponent -= step;
    }

  if (last_step <= 1)
    {
      return (int) last_rem;	// 0..9
    }

  divisor = _gv_mul_normalize_pow10_lookup[last_step - 2];	// 10^(last_step-1)
  return (int) (last_rem / divisor);	// 0..9
}

/*
 * float_numeric_increment() - Increment a base-256 big-endian buffer by val
 *
 * Note:
 *   - Used for rounding, mainly to increment by 1
 */
static void
float_numeric_increment (uint64_t * calc_buf, int calc_words, uint64_t val)
{
  int i = 0;
  uint64_t temp = 0;
  uint64_t next_carry = 0;
  uint64_t carry = val;
  for (i = calc_words - 1; i >= 0 && carry; i--)
    {
      temp = calc_buf[i] + carry;
      next_carry = (temp < calc_buf[i]);
      calc_buf[i] = temp;
      carry = next_carry;
    }
}

/*
 * numeric_operation_compare() - Compare two byte-based numeric buffers
 *   return        : 1 if arg1 > arg2, -1 if arg1 < arg2, 0 if equal
 *   dbv1_buf(in)  : First byte buffer (MSB-first)
 *   dbv2_buf(in)  : Second byte buffer (MSB-first)
 *   calc_bytes(in): Number of bytes to compare
 */
static int
numeric_operation_compare (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, int calc_bytes)
{
  int cmp_result = memcmp (dbv1_buf, dbv2_buf, calc_bytes);
  if (cmp_result == 0)
    {
      return 0;
    }
  /* memcmp returns <0 if first differing byte in dbv1_buf is less than in dbv2_buf */
  return (cmp_result < 0) ? -1 : 1;
}

/*
 * float_numeric_operation_compare() - Compare two word-based numeric buffers
 *   return        : 1 if arg1 > arg2, -1 if arg1 < arg2, 0 if equal
 *   arg1_word(in) : First word buffer (MSB-first)
 *   arg2_word(in) : Second word buffer (MSB-first)
 *   calc_words(in): Number of words to compare
 *
 * Note: Performs high-speed word-by-word comparison starting from the MSB.
 */
static int
float_numeric_operation_compare (const uint64_t * arg1_word, const uint64_t * arg2_word, int calc_words)
{
  int digit;

  for (digit = 0; digit < calc_words; digit++)
    {
      if (arg1_word[digit] > arg2_word[digit])
	{
	  return 1;
	}
      else if (arg1_word[digit] < arg2_word[digit])
	{
	  return -1;
	}
    }
  return 0;
}

/*
 * float_numeric_check_overflow_and_adjust_scale() - Check precision overflow and adjust scale
 *
 * calc_buf(in)      : working buffer for calculation
 * calc_bytes(in)    : size of the working buffer
 * result_prec(in/out): result precision (calculated from calc_buf, may be adjusted)
 * result_scale(in/out): result scale (may be reduced if precision overflows)
 * answer(out)       : DB_VALUE to initialize on overflow error
 *
 * return: NO_ERROR on success, ER_IT_DATA_OVERFLOW on overflow
 *
 * Note:
 *   - Calculates the decimal digit count from calc_buf
 *   - If precision exceeds DB_MAX_NUMERIC_PRECISION, adjusts result_scale
 *   - If adjusted scale is below DB_MIN_NUMERIC_SCALE, returns overflow error
 */
static int
float_numeric_check_overflow_and_adjust_scale (int *result_prec, int *result_scale, DB_VALUE * answer)
{
  int error = NO_ERROR;
  int precision = *result_prec;
  int scale = *result_scale;

  assert (precision > 0);

  if (precision > DB_MAX_NUMERIC_PRECISION)
    {
      scale -= (precision - DB_MAX_NUMERIC_PRECISION);
      *result_scale = scale;
    }

  if (scale < DB_MIN_NUMERIC_SCALE)
    {
      TP_DOMAIN *domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      error = ER_IT_DATA_OVERFLOW;
    }

  return error;
}

/*
 * float_numeric_round_and_pack() - Round and pack intermediate NUMERIC buffer
 *
 * calc_buf(in/out): working buffer for calculation
 * calc_bytes(in)  : size of the working buffer
 * result_buf(out) : final result buffer (DB_NUMERIC_BUF_SIZE)
 * result_prec(in/out): result precision (adjusted after rounding)
 * result_scale(in/out): result scale (may be reduced if rounding overflows)
 *
 * Note:
 *   - Used to reduce extended numeric precision down to DB_MAX_NUMERIC_PRECISION,
 *     apply half-up rounding, and pack into the fixed-size NUMERIC buffer.
 */
static int
float_numeric_round_and_pack (uint64_t * word_buf, int calc_words, int calc_nbytes, uint8_t * result_buf,
			      int *result_prec, int *result_scale)
{
  int last_digit = 0;
  int drop = 0;
  int round_prec = 0;

  drop = *result_prec - DB_MAX_NUMERIC_PRECISION;
  if (drop <= 0)
    {
      numeric_words_to_bytes (word_buf, calc_words, result_buf);
      return NO_ERROR;
    }

  /* if more than 41 digits, truncate to 40 digits and round at the 41st digit */
  *result_prec = DB_MAX_NUMERIC_PRECISION;

  /*
   * divide the value up to 40 digits and store the 41th digit in last_digit for rounding check.
   * reduces digits and returns the most significant digit of the truncated portion for rounding.
   */
  last_digit = float_numeric_div_normalize (word_buf, calc_words, calc_nbytes, drop);

  /* half-up rounding: if last_digit >= 5, increment the buffer by 1 */
  if (last_digit >= ROUND_HALF_UP_DIGIT)
    {
      (void) float_numeric_increment (word_buf, calc_words, 1);
      round_prec = float_numeric_get_decimal_digit (word_buf, calc_words);
      if (round_prec > DB_MAX_NUMERIC_PRECISION)
	{
	  /* reduces digits for normalization; does not perform rounding */
	  (void) float_numeric_div_normalize (word_buf, calc_words, calc_nbytes, 1);
	  (*result_scale)--;
	  if (*result_scale < DB_MIN_NUMERIC_SCALE)
	    {
	      return ER_IT_DATA_OVERFLOW;
	    }
	  round_prec = DB_MAX_NUMERIC_PRECISION;
	}
    }

  /* copy the final DB_MAX_NUMERIC_PRECISION-digit value into result_buf (DB_NUMERIC_BUF_SIZE) */
  numeric_words_to_bytes (word_buf, calc_words, result_buf);

  return NO_ERROR;
}

/*
 * compare_mantissa_same_exponent() - Compare the mantissas of two NUMERIC values (only valid when exponents are equal)
 *   return: 1: dividend > divisor
 *          -1: dividend < divisor  
 *           0: equal
 */
static int
compare_mantissa_same_exponent (uint64_t * dividend_word, uint64_t * divisor_word,
				int calc_words, int calc_nbytes, int prec1, int prec2)
{
  assert (prec1 <= DB_MAX_NUMERIC_PRECISION);
  assert (prec2 <= DB_MAX_NUMERIC_PRECISION);

  int prec_diff = prec1 - prec2;

  /*
   * normalize precision by multiplying the shorter side by 10^diff (avoid division)
   * - prec1 > prec2 : divisor *= 10^(prec1-prec2)
   * - prec1 < prec2 : dividend *= 10^(prec2-prec1)
   */
  if (prec_diff > 0)
    {
      (void) float_numeric_mul_normalize (divisor_word, calc_words, calc_nbytes, prec_diff);
    }
  else if (prec_diff < 0)
    {
      (void) float_numeric_mul_normalize (dividend_word, calc_words, calc_nbytes, -prec_diff);
    }

  return float_numeric_operation_compare (dividend_word, divisor_word, calc_words);
}

/*
 * float_numeric_compare_rem_round_up() - compare remainder with divisor to determine rounding
 *   return        : 1 (round up), -1 (do not round up), 0 (equal)
 *   rem(in)       : Remainder buffer from division (MSB-first)
 *   div(in)       : Divisor buffer (MSB-first)
 *   calc_words(in): Number of words in the buffers
 *
 * Note:
 *   - Determines rounding by checking: (2 * Remainder >= Divisor)
 *   - Correctly handles potential overflow during doubling by checking the MSB first.
 *   - Performs high-speed word-by-word comparison starting from the MSB.
 */
static int
float_numeric_compare_rem_round_up (const uint64_t * rem, const uint64_t * div, int calc_words)
{
  int i;
  uint64_t next_carry, temp_rem;

  /* 1. if the most significant bit (MSB) is 1, then (2 * Remainder) will 
   *    always be greater than or equal to the divisor (since R < V). 
   */
  if (rem[0] >> 63)
    {
      assert (float_numeric_operation_compare (rem, div, calc_words) < 0);
      return 1;
    }

  /* 2. compare doubled remainder with divisor word-by-word from MSB to LSB. */
  for (i = 0; i < calc_words; i++)
    {
      /* double the current word including the carry bit from the next word. */
      next_carry = (i + 1 < calc_words) ? (rem[i + 1] >> 63) : 0;
      temp_rem = (rem[i] << 1) | next_carry;

      /* compare the doubled word with the divisor word immediately. */
      if (temp_rem > div[i])
	{
	  return 1;
	}
      else if (temp_rem < div[i])
	{
	  return -1;
	}
    }
  return 0;
}

/*
 * numeric_pack_digits4_ascii () -
 *   return:
 *   buf(out) : buffer to store ASCII digits
 *   val(in)  : 64-bit integer value (0 ~ 10^16 - 1)
 * 
 * Note:
 *   - Converts 16-digit decimal value to ASCII string in O(1) time.
 *   - Uses 64-bit division once and 32-bit division twice.
 *   - Uses LUT for fast ASCII conversion (no loops).
 */
static inline void
numeric_pack_digits4_ascii (char *buf, uint64_t val)
{
  uint64_t h8, l8;
  uint32_t digits[4];

  assert (buf);

  /* step 1: split 16-digit value into two 8-digit groups (one 64-bit division).
   * separates the total 16 digits (val) into upper 8 digits (h8) and lower 8 digits (l8). */
  h8 = val / 100000000ULL;
  l8 = val % 100000000ULL;

  /* step 2: split 8-digit groups into 4-digit blocks (32-bit math for better parallelism). */
  digits[0] = (uint32_t) (h8 / 10000);
  digits[1] = (uint32_t) (h8 % 10000);
  digits[2] = (uint32_t) (l8 / 10000);
  digits[3] = (uint32_t) (l8 % 10000);

  /* step 3: reference LUT to pack 4-byte ASCII blocks at once.
   * stores d1, d2, d3, d4 in sequence to the buffer starting from buf+0. */
  *(uint32_t *) (buf + 0) = numeric_get_digits4_ascii (digits[0]);
  *(uint32_t *) (buf + 4) = numeric_get_digits4_ascii (digits[1]);
  *(uint32_t *) (buf + 8) = numeric_get_digits4_ascii (digits[2]);
  *(uint32_t *) (buf + 12) = numeric_get_digits4_ascii (digits[3]);
}

/* 
 * Read a 64-bit value from a big-endian byte array and convert it to host order
 * (BSWAP64 is a no-op on big-endian CPUs).
 */
static inline uint64_t
numeric_get_uint64_from_be (const void *ptr)
{
  uint64_t val;
  memcpy (&val, ptr, sizeof (val));
  return NUMERIC_BSWAP64 (val);
}

/* 
 * Write a 64-bit value in host order to a big-endian byte array
 * (BSWAP64 ensures correct byte order regardless of CPU endianness).
 */
static inline void
numeric_put_uint64_to_be (void *ptr, uint64_t val)
{
  uint64_t swapped = NUMERIC_BSWAP64 (val);
  memcpy (ptr, &swapped, sizeof (swapped));
}

/*
 * numeric_bytes_to_words() - Convert a NUMERIC byte buffer to a word-based buffer
 *   src(in)       : Source byte array (MSB-first)
 *   src_bytes(in) : Number of bytes in source
 *   dest(out)     : Destination word array (MSB-first)
 *   dest_words(in): Number of words in destination
 *   dest_bytes(in): Total size of destination in bytes
 *
 * Endianness & layout transformation:
 *
 *   [CUBRID NUMERIC: 17-byte big-endian]
 *     b0  b1  b2  ...              b16
 *     <- MSB                   LSB ->
 *
 *   - Group into 64-bit words (MSB-first order preserved; memcpy only):
 *     w0            w1             w2
 *     [b0]       [b1..b8]       [b9..b16]
 *     <- MSB                   LSB ->
 *
 *   - Each 64-bit chunk is loaded and converted to host order:
 *     (memcpy + optional BSWAP64, unlike the memcpy-only step above)
 *
 *     Example (8-byte chunk):
 *       big-endian bytes : 01 02 03 04 05 06 07 08
 *
 *       little-endian CPU:
 *         - 08 07 06 05 04 03 02 01 (reversed via BSWAP64)
 *
 *       big-endian CPU:
 *         - 01 02 03 04 05 06 07 08 (no change)
 *
 * Summary:
 *   - Word order remains big-endian (MSW -> LSW)
 *   - Each word is stored in host endianness for efficient arithmetic
 *   - This hybrid layout enables fast 64-bit operations
 *
 * Note:
 *   - Includes a fast path optimized for the standard 17-byte layout.
 */
static void
numeric_bytes_to_words (const uint8_t * src, int src_bytes, uint64_t * dest, int dest_words, int dest_bytes)
{
  /* [FAST PATH] optimized for the standard 17-byte NUMERIC layout */
  if (src_bytes == DB_NUMERIC_BUF_SIZE && dest_words >= NUMERIC_AS_WORDS)
    {
      /* zero higher words if the destination buffer is larger than 3 words */
      if (dest_words > NUMERIC_AS_WORDS)
	{
	  memset (dest, 0, dest_bytes);
	}

      /* map 17-byte layout directly:
       *   src[0]      → MSB (1 byte)
       *   src[1..8]   → middle 64-bit word
       *   src[9..16]  → LSB 64-bit word
       */
      dest[dest_words - 3] = (uint64_t) src[0];
      memcpy (&dest[dest_words - 2], src + 1, 16);
      dest[dest_words - 2] = NUMERIC_BSWAP64 (dest[dest_words - 2]);
      dest[dest_words - 1] = NUMERIC_BSWAP64 (dest[dest_words - 1]);
      return;
    }

  /* [VARIABLE PATH] generic conversion for arbitrary byte lengths */
  memset (dest, 0, dest_bytes);
  int full_words = NUMERIC_GET_FULL_WORDS (src_bytes);
  int rem_bytes = NUMERIC_GET_REM_BYTES (src_bytes);
  int current_word_idx = dest_words - 1;
  int i;

  /* load full 64-bit words from LSB side (right-aligned) */
  for (i = 0; i < full_words && current_word_idx >= 0; i++)
    {
      dest[current_word_idx--] = numeric_get_uint64_from_be (src + src_bytes - 8 * (i + 1));
    }

  /* handle remaining leading bytes (< 8 bytes) */
  if (rem_bytes > 0 && current_word_idx >= 0)
    {
      uint64_t val = 0;
      /* build a partial word in big-endian order */
      for (i = 0; i < rem_bytes; i++)
	{
	  val = (val << 8) | src[i];
	}
      dest[current_word_idx] = val;
    }
}

/*
 * numeric_words_to_bytes() - Convert a word-based buffer back to a NUMERIC byte buffer
 *   src(in)       : Source word array (MSB-first)
 *   src_words(in) : Number of words in source
 *   dest(out)     : Destination byte array (fixed size: DB_NUMERIC_BUF_SIZE)
 *
 * Note:
 *   - Packs the least significant 3 words into the 17-byte CUBRID NUMERIC format.
 *   - The format is fixed at 17 bytes, so at least 3 words (192 bits) are required.
 *   - Results are rounded to 40 digits, so 3 words are sufficient.
 */
static void
numeric_words_to_bytes (const uint64_t * src, int src_words, uint8_t * dest)
{
  /* pointer to the 3 least significant words (MSW → LSW within this range) */
  const uint64_t *lsb_ptr = src + (src_words - NUMERIC_AS_WORDS);

#if !defined (NDEBUG)
  /* ensure no overflow: higher words beyond the 3-word range must be zero */
  for (int i = 0; i < src_words - NUMERIC_AS_WORDS; i++)
    {
      assert (src[i] == 0);
    }
  /* ensure the MSW fits into 1 byte (17-byte layout constraint) */
  assert ((lsb_ptr[0] >> 8) == 0);
#endif

  /*
   * 17-byte NUMERIC layout (big-endian):
   *   dest[0]      : most significant byte (high part of mantissa, no sign)
   *   dest[1..8]   : middle 64 bits  (lsb_ptr[1])
   *   dest[9..16]  : least 64 bits   (lsb_ptr[2])
   */
  dest[0] = (uint8_t) (lsb_ptr[0] & 0xFF);
  uint64_t temp_word[2];
  temp_word[0] = NUMERIC_BSWAP64 (lsb_ptr[1]);
  temp_word[1] = NUMERIC_BSWAP64 (lsb_ptr[2]);
  memcpy (dest + 1, temp_word, sizeof (temp_word));
}

/*
 * numeric_db_value_coerce_to_num () -
 *   return: NO_ERROR, or ER_code
 *   src(in)     : ptr to a DB_VALUE of some numerical type
 *   dest(in/out): ptr to a DB_VALUE of type DB_TYPE_NUMERIC
 *   data_status(out): ptr to a DB_DATA_STATUS value
 *
 * Note: This routine converts a DB_VALUE of some numerical type into a
 * DB_VALUE of type DB_TYPE_NUMERIC.  The precision and scale fields of
 * are assumed to represent the desired values of the output.  If they are
 * set to DB_DEFAULT_PRECISION/SCALE, the default values are implied. If
 * they are set to 0, the precision and scale are set to be the maximum
 * amount necessary in order to preserve as much data as possible.
 */
int
numeric_db_value_coerce_to_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_status)
{
  int ret = NO_ERROR;
  unsigned char num[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */
  int precision, scale;
  int desired_precision, desired_scale;
  bool num_is_negative = false;

  *data_status = DATA_STATUS_OK;
  desired_precision = DB_VALUE_PRECISION (dest);
  desired_scale = DB_VALUE_SCALE (dest);
  /* Check for a non NULL src and a dest whose type is DB_TYPE_NUMERIC */
  /* Switch on the src type */
  switch (DB_VALUE_TYPE (src))
    {
    case DB_TYPE_DOUBLE:
      {
	double adouble = db_get_double (src);
	ret = numeric_internal_double_to_num (adouble, desired_scale, num, &precision, &scale, &num_is_negative);
	break;
      }

    case DB_TYPE_FLOAT:
      {
	float adouble = (float) db_get_float (src);
	ret = numeric_internal_float_to_num (adouble, desired_scale, num, &precision, &scale, &num_is_negative);
	break;
      }

    case DB_TYPE_MONETARY:
      {
	double adouble = db_value_get_monetary_amount_as_double (src);
	ret = numeric_internal_double_to_num (adouble, desired_scale, num, &precision, &scale, &num_is_negative);
	break;
      }

    case DB_TYPE_INTEGER:
      {
	int anint = db_get_int (src);

	numeric_coerce_int_to_num (anint, num, &num_is_negative);
	precision = get_significant_digit (anint);
	scale = 0;
	break;
      }

    case DB_TYPE_SMALLINT:
      {
	int anint = (int) db_get_short (src);

	numeric_coerce_int_to_num (anint, num, &num_is_negative);
	precision = get_significant_digit (anint);
	scale = 0;
	break;
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bigint = db_get_bigint (src);

	numeric_coerce_bigint_to_num (bigint, num, &num_is_negative);
	precision = get_significant_digit (bigint);
	desired_precision = MAX (desired_precision, precision);
	scale = 0;
	break;
      }

    case DB_TYPE_NUMERIC:
      {
	bool src_is_float_numeric = false;
	db_get_numeric_precision_and_scale (src, &precision, &scale, &src_is_float_numeric);

	if (!src_is_float_numeric && precision == (unsigned char) DB_HJOIN_NUMERIC_PRECISION_DEFERRED)
	  {
	    precision = numeric_get_precision_digits (db_locate_numeric (src));
	  }

	numeric_copy (num, db_locate_numeric (src));
	num_is_negative = numeric_is_negative (src);
	break;
      }

    case DB_TYPE_ENUMERATION:
      {
	int anint = db_get_enum_short (src);
	numeric_coerce_int_to_num (anint, num, &num_is_negative);
	precision = 5;
	scale = 0;
	break;
      }

    default:
      ret = ER_FAILED;
      break;
    }

  /* Make the destination value */
  if (ret == NO_ERROR)
    {
      if (desired_precision == DB_DEFAULT_NUMERIC_PRECISION)
	{
	  db_make_numeric (dest, num, precision, scale, DB_NUMERIC_BUF_SIZE, num_is_negative, true);
	  return ret;
	}

      /* Make the intermediate value */
      bool dest_value_is_negative = num_is_negative;
      db_make_numeric (dest, num, precision, scale, DB_NUMERIC_BUF_SIZE, dest_value_is_negative, false);
      ret =
	numeric_coerce_num_to_num (dest, DB_VALUE_PRECISION (dest), DB_VALUE_SCALE (dest),
				   desired_precision, desired_scale, num, &dest_value_is_negative);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      db_make_numeric (dest, num, desired_precision, desired_scale, DB_NUMERIC_BUF_SIZE, dest_value_is_negative, false);
    }

  if (ret == ER_IT_DATA_OVERFLOW)
    {
      *data_status = DATA_STATUS_TRUNCATED;
    }

  return ret;

exit_on_error:

  if (ret == ER_IT_DATA_OVERFLOW)
    {
      *data_status = DATA_STATUS_TRUNCATED;
    }

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * numeric_db_value_coerce_from_num () -
 *   return: NO_ERROR, or ER_code
 *   src(in)     : ptr to a DB_VALUE of type DB_TYPE_NUMERIC
 *   dest(out)   : ptr to a DB_VALUE of some numerical type
 *   data_status(out): ptr to a DB_DATA_STATUS value
 *
 * Note: This routine converts a DB_VALUE of type DB_TYPE_NUMERIC into some
 * numerical type.
 */
int
numeric_db_value_coerce_from_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_status)
{
  int ret = NO_ERROR;
  int scale = db_get_numeric_scale (src, NULL);


  *data_status = DATA_STATUS_OK;
  /* Check for a DB_TYPE_NUMERIC src and a non NULL numerical dest */
  /* Switch on the dest type */
  switch (DB_VALUE_DOMAIN_TYPE (dest))
    {
    case DB_TYPE_DOUBLE:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_DOUBLE_OVERFLOW (adouble))
	  {
	    ret = ER_IT_DATA_OVERFLOW;
	    goto exit_on_error;
	  }
	db_make_double (dest, adouble);
	break;
      }

    case DB_TYPE_FLOAT:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_FLOAT_OVERFLOW (adouble))
	  {
	    ret = ER_IT_DATA_OVERFLOW;
	    goto exit_on_error;
	  }
	db_make_float (dest, (float) adouble);
	break;
      }

    case DB_TYPE_MONETARY:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	db_make_monetary (dest, DB_CURRENCY_DEFAULT, adouble);
	break;
      }

    case DB_TYPE_INTEGER:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_INT_OVERFLOW (adouble))
	  {
	    ret = ER_IT_DATA_OVERFLOW;
	    goto exit_on_error;
	  }
	db_make_int (dest, (int) ROUND (adouble));
	break;
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bint;
	ret = numeric_coerce_num_to_bigint (db_locate_numeric (src), scale, &bint, numeric_is_negative (src));
	if (ret != NO_ERROR)
	  {
	    goto exit_on_error;
	  }

	db_make_bigint (dest, bint);
	break;
      }

    case DB_TYPE_SMALLINT:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_SHORT_OVERFLOW (adouble))
	  {
	    ret = ER_IT_DATA_OVERFLOW;
	    goto exit_on_error;
	  }
	db_make_short (dest, (DB_C_SHORT) ROUND (adouble));
	break;
      }

    case DB_TYPE_NUMERIC:
      {
	ret = numeric_db_value_coerce_to_num (src, dest, data_status);
	break;
      }

    case DB_TYPE_CHAR:
    case DB_TYPE_VARCHAR:
      {
	char *return_string = NULL;
	char str_buf[NUMERIC_MAX_STRING_SIZE];
	int size = 0;
	DB_TYPE type;

	numeric_db_value_print (src, str_buf);
	size = strlen (str_buf);
	return_string = (char *) db_private_alloc (NULL, size + 1);
	if (return_string == NULL)
	  {
	    assert (er_errid () != NO_ERROR);
	    return er_errid ();
	  }

	strcpy (return_string, str_buf);
	type = DB_VALUE_DOMAIN_TYPE (dest);
	if (type == DB_TYPE_CHAR)
	  {
	    db_make_char (dest, size, return_string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  }
	else if (type == DB_TYPE_VARCHAR)
	  {
	    db_make_varchar (dest, size, return_string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  }
	dest->need_clear = true;
	break;
      }

    case DB_TYPE_TIME:
      {
	double adouble;
	DB_TIME v_time;
	int hour, minute, second;

	numeric_coerce_num_to_double (src, scale, &adouble);
	v_time = (int) (adouble + 0.5) % SECONDS_IN_A_DAY;
	db_time_decode (&v_time, &hour, &minute, &second);
	db_make_time (dest, hour, minute, second);
	break;
      }

    case DB_TYPE_DATE:
      {
	double adouble;
	DB_DATE v_date;
	int year, month, day;

	numeric_coerce_num_to_double (src, scale, &adouble);
	v_date = (DB_DATE) (adouble);
	db_date_decode (&v_date, &month, &day, &year);
	db_make_date (dest, month, day, year);
	break;
      }

    case DB_TYPE_TIMESTAMP:
      {
	double adouble;
	DB_TIMESTAMP v_timestamp;

	numeric_coerce_num_to_double (src, scale, &adouble);
	v_timestamp = (DB_TIMESTAMP) (adouble);
	db_make_timestamp (dest, v_timestamp);
	break;
      }

    case DB_TYPE_DATETIME:
      {
	DB_BIGINT bi, tmp_bi;
	DB_DATETIME v_datetime;

	ret = numeric_coerce_num_to_bigint (db_locate_numeric (src), scale, &bi, numeric_is_negative (src));
	if (ret == NO_ERROR)
	  {
	    /* make datetime value from interval value */
	    tmp_bi = (DB_BIGINT) (bi / MILLISECONDS_OF_ONE_DAY);
	    if (OR_CHECK_INT_OVERFLOW (tmp_bi))
	      {
		ret = ER_IT_DATA_OVERFLOW;
	      }
	    else
	      {
		v_datetime.date = (int) tmp_bi;
		v_datetime.time = (int) (bi % MILLISECONDS_OF_ONE_DAY);
		db_make_datetime (dest, &v_datetime);
	      }
	  }
	break;
      }

    default:
      ret = DOMAIN_INCOMPATIBLE;
      break;
    }

  return ret;

exit_on_error:

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * numeric_db_value_coerce_from_num_strict () - coerce a numeric to the type
 *						of dest
 * return : error code or NO_ERROR
 * src (in)	: the numeric value
 * dest(in/out) : the value to coerce to
 */
int
numeric_db_value_coerce_from_num_strict (DB_VALUE * src, DB_VALUE * dest)
{
  int ret = NO_ERROR;
  int scale = db_get_numeric_scale (src, NULL);

  switch (DB_VALUE_DOMAIN_TYPE (dest))
    {
    case DB_TYPE_DOUBLE:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_DOUBLE_OVERFLOW (adouble))
	  {
	    return ER_FAILED;
	  }
	db_make_double (dest, adouble);
	break;
      }

    case DB_TYPE_FLOAT:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_FLOAT_OVERFLOW (adouble))
	  {
	    return ER_FAILED;
	  }
	db_make_float (dest, (float) adouble);
	break;
      }

    case DB_TYPE_MONETARY:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_FLOAT_OVERFLOW (adouble))
	  {
	    return ER_FAILED;
	  }
	db_make_monetary (dest, DB_CURRENCY_DEFAULT, adouble);
	break;
      }

    case DB_TYPE_INTEGER:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_INT_OVERFLOW (adouble) || !numeric_is_fraction_part_zero (src, scale))
	  {
	    return ER_FAILED;
	  }
	db_make_int (dest, (int) (adouble));
	break;
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bint;

	ret = numeric_coerce_num_to_bigint (db_locate_numeric (src), scale, &bint, numeric_is_negative (src));
	if (ret != NO_ERROR || !numeric_is_fraction_part_zero (src, scale))
	  {
	    return ER_FAILED;
	  }
	db_make_bigint (dest, bint);
	break;
      }

    case DB_TYPE_SMALLINT:
      {
	double adouble;
	numeric_coerce_num_to_double (src, scale, &adouble);
	if (OR_CHECK_SHORT_OVERFLOW (adouble) || !numeric_is_fraction_part_zero (src, scale))
	  {
	    return ER_FAILED;
	  }
	db_make_short (dest, (DB_C_SHORT) ROUND (adouble));
	break;
      }

    case DB_TYPE_NUMERIC:
      {
	DB_DATA_STATUS data_status = DATA_STATUS_OK;
	ret = numeric_db_value_coerce_to_num (src, dest, &data_status);
	break;
      }

    default:
      ret = ER_FAILED;
      break;
    }

  return ER_FAILED;
}

/*
 * numeric_db_value_print () -
 *   return: a static character buffer that contains the numeric printed in an
 *           ASCII format.
 *   val(in)    : DB_VALUE of type numeric to print
 *
 * Note: returns the null-terminated string form of val
 */
char *
numeric_db_value_print (const DB_VALUE * val, char *buf)
{
  char temp[NUMERIC_MAX_STRING_SIZE];
  int nbuf;
  int temp_size;
  int i;
  bool found_first_non_zero = false;
  int scale = db_get_numeric_scale (val, NULL);

  /* it should not be static because the parameter could be changed without broker restart */
  bool oracle_compat_number = prm_get_bool_value (PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR);

  assert (val);
  assert (buf);

  if (DB_IS_NULL (val))
    {
      buf[0] = '\0';
      return buf;
    }

  /* Retrieve raw decimal string */
  numeric_coerce_num_to_dec_str (val, temp);

  /* Remove the extra padded zeroes and add the decimal point */
  nbuf = 0;
  temp_size = (int) strnlen (temp, sizeof (temp));
  for (i = 0; i < temp_size; i++)
    {
      /* Add the negative sign */
      if (temp[i] == '-')
	{
	  buf[nbuf++] = '-';
	}

      /* Add decimal point */
      if (i == temp_size - scale)
	{
	  int k = temp_size - 1;

	  if (oracle_compat_number)
	    {
	      /* remove trailing zero */
	      while (k > i && temp[k] == '0')
		{
		  k--;
		}

	      temp_size = k + 1;
	      if (temp[k] == '0')
		{
		  continue;
		}
	      else if (k >= i)
		{
		  buf[nbuf++] = '.';
		}
	    }
	  else
	    {
	      buf[nbuf++] = '.';
	    }
	}

      /* Check to see if the first significant digit has been found */
      if (!found_first_non_zero && temp[i] >= '1' && temp[i] <= '9')
	{
	  found_first_non_zero = true;
	}

      /* Remove leading zeroes */
      if (found_first_non_zero || i >= temp_size - scale - 1)
	{
	  buf[nbuf++] = temp[i];
	}
    }

  /* Null terminate */
  buf[nbuf] = '\0';

  /* Handling negative scale: append zeros to the right of the decimal point */
  if (scale < 0)
    {
      int abs_scale = -scale;

      if (!found_first_non_zero)
	{
	  // If no digit found: output only a single '0'
	  buf[0] = '0';
	  nbuf = 1;
	}
      else
	{
	  // If digits exist: append '0' abs_scale times
	  for (i = 0; i < abs_scale; i++)
	    {
	      buf[nbuf++] = '0';
	    }
	}
      buf[nbuf] = '\0';
    }

  return buf;
}

/*
 * numeric_db_value_is_zero () -
 *   return: bool
 *   arg(in)    : DB_VALUE of type DB_NUMERIC
 *
 * Note: This routine checks if arg = 0.
 *       This function returns:
 *           true   if    arg1 = 0 and
 *           false  otherwise.
 *
 */
bool
numeric_db_value_is_zero (const DB_VALUE * arg)
{
  if (DB_IS_NULL (arg))		/* NULL values are not 0 */
    {
      return false;
    }
  else
    {
      return (numeric_is_zero (db_get_numeric (arg)));
    }
}

int
numeric_get_precision_digits (uint8_t * calc_buf)
{
  uint64_t word_buf[NUMERIC_AS_WORDS] = { 0 };
  numeric_bytes_to_words (calc_buf, DB_NUMERIC_BUF_SIZE, word_buf, NUMERIC_AS_WORDS, NUMERIC_AS_WORD_BYTES);
  return float_numeric_get_decimal_digit (word_buf, NUMERIC_AS_WORDS);
}
