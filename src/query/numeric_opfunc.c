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
#define GET_LOWER_BYTE(arg)	((arg) & 0xff)
#define NUMERIC_ABS(a)		((a) >= 0 ? a : -a)
//#define TWICE_NUM_MAX_PREC    (2*DB_MAX_NUMERIC_PRECISION)

/* TWICE_NUM_MAX_PREC : num(base-256) 값을 10진수 문자로 표현하기 위한 크기, 최대 소수 범위(38 + 127) 이내에 일단 최대 정수, 소수 다 표현 가능 */
// TWICE_NUM_MAX_PREC :  최대 소수 (38 + 127) = 165
#define TWICE_NUM_MAX_PREC        (DB_MAX_NUMERIC_PRECISION + DB_MAX_NUMERIC_SCALE) + 10

#define SECONDS_IN_A_DAY	  (int)(24L * 60L * 60L)

#define ROUND(x)                  ((x) > 0 ? ((x) + .5) : ((x) - .5))

// (38 + 127) + (38 - (-84)) = 165 + 122 = 287
#define POW10_MAX_INDEX            (DB_MAX_NUMERIC_PRECISION + DB_MAX_NUMERIC_SCALE) + (DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE)
// 287 * log2(10) = 119
#define POW10_BUF_SIZE            (120)

typedef struct dec_string DEC_STRING;
struct dec_string
{
  char digits[TWICE_NUM_MAX_PREC + 1];
};

static const char fast_mod[20] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

static DEC_STRING powers_of_2[DB_NUMERIC_BUF_SIZE * 16];
#if !defined(SERVER_MODE)
static bool initialized_2 = false;
#endif
// [10의 지수 개수][10의 지수를 base-256의 값으로 표현한 값]
static unsigned char powers_of_10[POW10_MAX_INDEX + 1][POW10_BUF_SIZE];
// 각 10^n의 유효 바이트 개수
static uint16_t _gv_powers_of_10_effective_bytes[POW10_BUF_SIZE];
#if !defined(SERVER_MODE)
static bool initialized_10 = false;
#endif

static double numeric_Pow_of_10[10] = {
  1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9
};

typedef enum fp_value_type
{
  FP_VALUE_TYPE_NUMBER,
  FP_VALUE_TYPE_INFINITE,
  FP_VALUE_TYPE_NAN,
  FP_VALUE_TYPE_ZERO
}
FP_VALUE_TYPE;

typedef enum numeric_parse_state
{
  STR_START,			// 아직 아무것도 안 본 상태 (leading spaces / sign 대기)
  STR_SIGNED,			// 부호 한 번 본 상태 (아직 숫자는 안 본)
  STR_INTEGER,			// 정수부 읽고 있는 중
  STR_FRACTION,			// 소수점(‘.’) 이후 소수부 읽고 있는 중
  STR_TRAIL			// 숫자/소수점 뒤에 공백 읽은 중 (trailing spaces)
}
NUMERIC_PARSE_STATE;

static bool numeric_is_negative (DB_C_NUMERIC arg);
static void numeric_copy (DB_C_NUMERIC dest, DB_C_NUMERIC source);
static void numeric_copy_long (DB_C_NUMERIC dest, DB_C_NUMERIC source, bool is_long_num);
static void numeric_increase (DB_C_NUMERIC answer);
static void numeric_increase_long (DB_C_NUMERIC answer, bool is_long_num);
static void numeric_decrease (DB_C_NUMERIC answer);
static void numeric_zero (DB_C_NUMERIC answer, int size);
static void numeric_init_dec_str (DEC_STRING * answer);
static void numeric_add_dec_str (DEC_STRING * arg1, DEC_STRING * arg2, DEC_STRING * answer);
static void numeric_init_pow_of_2_helper (void);
#if defined(SERVER_MODE)
static void numeric_init_pow_of_2 (void);
#endif
static DEC_STRING *numeric_get_pow_of_2 (int exp);
static void numeric_init_pow_of_10_helper (void);
#if defined(SERVER_MODE)
static void numeric_init_pow_of_10 (void);
#endif
static DB_C_NUMERIC numeric_get_pow_of_10 (int exp);
static void numeric_double_shift_bit (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, int numbits, DB_C_NUMERIC lsb,
				      DB_C_NUMERIC msb, bool is_long_num);
static int numeric_compare_pos (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2);
static int numeric_compare_with_pow10 (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2);
static void numeric_negate (DB_C_NUMERIC answer);
static void numeric_negate_long (DB_C_NUMERIC answer, bool is_long_num);
static void numeric_shift_byte (DB_C_NUMERIC arg, int numbytes, DB_C_NUMERIC answer, int length);
static bool numeric_is_zero (DB_C_NUMERIC arg);
static bool numeric_is_long (DB_C_NUMERIC arg);
static bool numeric_is_bigint (DB_C_NUMERIC arg);
static bool numeric_is_bit_set (DB_C_NUMERIC arg, int pos);
static bool numeric_overflow (DB_C_NUMERIC arg, int exp);
static void numeric_add (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, int size);
static void numeric_sub (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, int size);
static void numeric_mul (DB_C_NUMERIC a1, DB_C_NUMERIC a2, bool * positive_flag, DB_C_NUMERIC answer);
static void numeric_long_div (DB_C_NUMERIC a1, DB_C_NUMERIC a2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder,
			      bool is_long_num);
static void numeric_div (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder);
static int numeric_compare (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2);
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
static int numeric_fast_convert (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale);
static FP_VALUE_TYPE get_fp_value_type (double d);
static int numeric_internal_real_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale,
					 bool is_float);
static void numeric_get_integral_part (const DB_C_NUMERIC num, const int src_prec, const int src_scale,
				       const int dst_prec, DB_C_NUMERIC dest);
static void numeric_get_fractional_part (const DB_C_NUMERIC num, const int src_scale, const int dst_prec,
					 DB_C_NUMERIC dest);
static bool numeric_is_fraction_part_zero (const DB_C_NUMERIC num, const int scale);
static bool numeric_is_longnum_value (DB_C_NUMERIC arg);
static int numeric_longnum_to_shortnum (DB_C_NUMERIC answer, DB_C_NUMERIC long_arg);
static void numeric_shortnum_to_longnum (DB_C_NUMERIC long_answer, DB_C_NUMERIC arg);
static int get_significant_digit (DB_BIGINT i);

// numeric 파싱 하는 함수
static int parse_decimal_string4 (const char *astring, int astring_length, INTL_CODESET codeset, bool * negate_value,
				  char *int_digits, int *int_len, char *frac_digits, int *frac_len, int *int_first_nz,
				  int *int_last_nz, int *frac_first_nz, int *frac_last_nz);
static void compute_prec_scale3 (const char *int_digits, int int_len, const char *frac_digits, int frac_len,
				 int int_first_nz, int int_last_nz, int frac_first_nz, int frac_last_nz,
				 char *num_string, int *out_prec, int *out_scale, bool * need_round);
static void round_and_clamp (char *out_str, int *out_prec, int *out_scale, int temp_int_len, int temp_frac_len,
			     int frac_zero_cnt, char next_digit);

// base-256 덧셈 (1byte 씩 기존과 동일)
static int floating_point_numeric_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec,
				       int *result_scale, DB_VALUE * answer);
static int calc_bytes_from_prec (int prec);
static void fp_numeric_pad (uint8_t * src_buf, uint8_t * padded_dbv_buf, int calc_bytes);
static void fp_numeric_mul_pow10 (uint8_t * dbv_buf, int calc_bytes, int exponent);
static void fp_numeric_add (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, uint8_t * calc_buf, int calc_bytes);

// 요거는 임시 대기
static int count_digits_by_division (const uint8_t * calc_buf, int calc_bytes);
//base-256에서 연산 결과 자릿수 확인하는 함수들
static void fp_numeric_init_pow10_table (void);
static int fp_numeric_cmp_base256 (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, int calc_bytes);
//static int fp_numeric_overflow (const uint8_t * calc_buf, int calc_bytes);
static int fp_numeric_overflow2 (const uint8_t * calc_buf, int calc_bytes);
static void fp_numeric_round_and_pack (uint8_t * calc_buf, int calc_bytes, uint8_t * result_buf, int *result_prec,
				       int *result_scale);
static uint16_t fp_numeric_div_pow10 (uint8_t * calc_buf, int calc_bytes);
static void fp_numeric_increment (uint8_t * calc_buf, int calc_bytes, uint8_t val);

// base-256 뺄셈 (1byte 씩 기존과 동일)
static int floating_point_numeric_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec,
				       int *result_scale, DB_VALUE * answer);
static void fp_numeric_sub (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, uint8_t * calc_buf, int calc_bytes);

// base-256 곱셈 (1byte 씩 기존과 동일)
static int floating_point_numeric_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec,
				       int *result_scale, DB_VALUE * answer);
static void fp_numeric_mul (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, uint8_t * calc_buf, int calc_bytes);

// base-256 나눗셈 (1byte 씩 기존과 동일)
static int floating_point_numeric_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec,
				       int *result_scale, DB_VALUE * answer);
static void fp_numeric_div (uint8_t * dbv1_buf, uint8_t * dbv2_buf, uint8_t * quo_buf, uint8_t * rem_buf,
			    int calc_bytes);
static void fp_numeric_shift_left (uint8_t * buf, int calc_bytes);
static void fp_numeric_shift_right (uint8_t * buf, int calc_bytes);

static void fp_numeric_div2 (uint8_t * dbv1_buf, uint8_t * dbv2_buf, uint8_t * quo_buf, uint8_t * rem_buf,
			     int calc_bytes);
static void fp_numeric_double_shift_bit (uint8_t * arg1, uint8_t * arg2, int calc_bytes, uint8_t * lsb, uint8_t * msb);
static void fp_numeric_trim_trailing_zeros (uint8_t * result_buf, int *result_prec, int *result_scale);

/*
 * numeric_is_negative () -
 *   return: true, false
 *   arg(in) : DB_C_NUMERIC value
 */
static bool
numeric_is_negative (DB_C_NUMERIC arg)
{
  return (arg[0] & 0x80) ? true : false;
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
 * numeric_decrease () -
 *   return:
 *   answer(in/out) : DB_C_NUMERIC value
 *
 * Note: This routine decrements a numeric value.
 */
static void
numeric_decrease (DB_C_NUMERIC answer)
{
  int carry = 1;
  int digit;

  /* Loop through answer as long as there is a carry */
  for (digit = DB_NUMERIC_BUF_SIZE - 1; digit >= 0 && carry == 1; digit--)
    {
      answer[digit] -= 1;
      carry = (answer[digit] == 0xff) ? 1 : 0;
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
 * numeric_negative_one () -
 *   return:
 *   answer(in) : DB_C_NUMERIC value
 *   size(in)   :
 *
 * Note: This routine make a numeric value as -1
 */
static void
numeric_negative_one (DB_C_NUMERIC answer, int size)
{
  memset (answer, 0xff, size);
}

/*
 * numeric_init_dec_str () -
 *   return:
 *   answer(in/out) : (IN/OUT) ptr to a DEC_STRING
 *
 * Note: Fills a DEC_STRING with -1 constant bytes and zero rightmost byte
 *
 *       digits:[00][01][02]......[73][74][75]
 *       values: -1  -1  -1 ...... -1  -1   0
 */
static void
numeric_init_dec_str (DEC_STRING * answer)
{
  /* sizeof(answer->digits[0]) == 1 */
  memset (answer->digits, -1, TWICE_NUM_MAX_PREC);

  /* Set first element to 0 */
  answer->digits[TWICE_NUM_MAX_PREC - 1] = 0;
}

/*
 * numeric_add_dec_str () -
 *   arg1(in)   : ptr to a DEC_STRING
 *   arg2(in)   : ptr to a DEC_STRING
 *   answer(out): ptr to a DEC_STRING
 *
 * Note: This routine adds two DEC_STRINGs and returns the result.  It assumes
 *       that arg1 and arg2 have the same scaling.
 */

static void
numeric_add_dec_str (DEC_STRING * arg1, DEC_STRING * arg2, DEC_STRING * answer)
{
  unsigned int answer_bit = 0;
  int digit;
  char arg1_dec, arg2_dec;

  /* Loop through the characters setting answer */
  for (digit = TWICE_NUM_MAX_PREC - 1; digit >= 0; digit--)
    {
      arg1_dec = arg1->digits[digit];
      arg2_dec = arg2->digits[digit];

      if (arg1_dec == -1)
	{
	  arg1_dec = 0;

	  if (answer_bit < 10)
	    {
	      break;		/* pass through the leftmost digits */
	    }
	}

      if (arg2_dec == -1)
	{
	  /* is not first element */
	  assert (digit < TWICE_NUM_MAX_PREC - 1);

	  arg2_dec = 0;
	}

      assert (arg1_dec >= 0);
      assert (arg2_dec >= 0);

      answer_bit = (arg1_dec + arg2_dec) + (answer_bit >= 10);
      answer->digits[digit] = fast_mod[answer_bit];
    }
}

/*
 * numeric_init_pow_of_2_helper () -
 *   return:
 */
static void
numeric_init_pow_of_2_helper (void)
{
  unsigned int i;

  numeric_init_dec_str (&(powers_of_2[0]));

  /* Set first element to 1 */
  powers_of_2[0].digits[TWICE_NUM_MAX_PREC - 1] = 1;

  /* Loop through array elements setting each one to twice the prior */
  for (i = 1; i < DB_NUMERIC_BUF_SIZE * 16; i++)
    {
      numeric_init_dec_str (&(powers_of_2[i]));
      numeric_add_dec_str (&(powers_of_2[i - 1]), &(powers_of_2[i - 1]), &(powers_of_2[i]));
    }
}

#if defined(SERVER_MODE)
/*
 * numeric_init_pow_of_2 () -
 *   return:
 */
static void
numeric_init_pow_of_2 (void)
{
  numeric_init_pow_of_2_helper ();
}
#endif

/*
 * numeric_get_pow_of_2 () -
 *   return: DEC_STRING containing the equivalent base 10 representation
 *   exp(in)    : positive integer exponent base 2
 *
 * Note: This routine returns a DEC_STRING that holds the base 10 digits of a
 *       power of 2.
 */
static DEC_STRING *
numeric_get_pow_of_2 (int exp)
{
  assert (exp < (int) (DB_NUMERIC_BUF_SIZE * 16 - 3));	/* exp < 253 */

#if !defined(SERVER_MODE)
  /* If this is the first time to call this routine, initialize */
  if (!initialized_2)
    {
      numeric_init_pow_of_2_helper ();
      initialized_2 = true;
    }
#endif

  /* Return the appropriate power of 2 */
  return &powers_of_2[exp];
}

/*
 * numeric_init_pow_of_10_helper () -
 *   return:
 */
static void
numeric_init_pow_of_10_helper (void)
{
  int i, j, k;
  uint32_t carry, temp;
  int effective_bytes;
  int current_byte_size = 1;

  numeric_zero (powers_of_10[0], POW10_BUF_SIZE);

  /* Set first element to 1 */
  powers_of_10[0][POW10_BUF_SIZE - 1] = 1;

  /* Set effective bytes for 10^0 */
  _gv_powers_of_10_effective_bytes[0] = 0;

  /* Loop through elements setting each one to 10 times the prior */
  for (i = 1; i < POW10_MAX_INDEX + 1; i++)
    {
      memcpy (powers_of_10[i], powers_of_10[i - 1], POW10_BUF_SIZE);

      // 120바이트 전체에 대해 10 곱셈
      carry = 0;
      for (j = POW10_BUF_SIZE - 1; j >= 0; j--)
	{
	  temp = (uint32_t) powers_of_10[i][j] * 10 + carry;
	  powers_of_10[i][j] = (uint8_t) (temp & 0xFF);
	  carry = temp >> 8;
	}

      // 유효 바이트 개수 계산 및 저장
      effective_bytes = 0;
      for (k = 0; k < POW10_BUF_SIZE; k++)
	{
	  if (powers_of_10[i][k] != 0)
	    {
	      effective_bytes = POW10_BUF_SIZE - k;
	      break;
	    }
	}

      if (effective_bytes > current_byte_size)
	{
	  current_byte_size = effective_bytes;
	  if (current_byte_size - 1 < 120 && current_byte_size - 1 >= 0)
	    {
	      _gv_powers_of_10_effective_bytes[current_byte_size - 1] = i;
	    }
	}
    }
}

#if defined(SERVER_MODE)
/*
 * numeric_init_pow_of_10 () -
 *   return:
 */
static void
numeric_init_pow_of_10 (void)
{
  numeric_init_pow_of_10_helper ();
}
#endif

/*
 * numeric_get_pow_of_10 () -
 *   return: DB_C_NUMERIC containing the equivalent base 2 representation
 *   exp(in)    : positive integer exponent base 10
 *
 * Note: This routine returns a DB_C_NUMERIC that holds the base 2 digits of a
 *       power of 10.
 */
static DB_C_NUMERIC
numeric_get_pow_of_10 (int exp)
{
  assert (exp < (int) sizeof (powers_of_10));

#if !defined(SERVER_MODE)
  /* If this is the first time to call this routine, initialize */
  if (!initialized_10)
    {
      numeric_init_pow_of_10_helper ();
      initialized_10 = true;
    }
#endif

  /* Return the appropriate power of 10 */
  return powers_of_10[exp];
}

#if defined(SERVER_MODE)
/*
 * numeric_init_power_value_string () -
 *   return:
 */
void
numeric_init_power_value_string (void)
{
  numeric_init_pow_of_2 ();
  numeric_init_pow_of_10 ();
}
#endif

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

static int
numeric_compare_with_pow10 (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2)
{
  unsigned int digit;
  int arg2_start_offset = 0;

  // arg2의 시작 오프셋 계산 (120 - 유효바이트)
  arg2_start_offset = POW10_BUF_SIZE - DB_NUMERIC_BUF_SIZE;

  // 16바이트 비교 (arg2는 오프셋부터 시작)
  for (digit = 0; digit < DB_NUMERIC_BUF_SIZE; digit++)
    {
      if (arg1[digit] != arg2[arg2_start_offset + digit])
	{
	  return (arg1[digit] > arg2[arg2_start_offset + digit]) ? 1 : (-1);
	}
    }

  /* If all bytes have been compared, then args are equal */
  return (0);
}

/*
 * numeric_negate () -
 *   return:
 *   answer(in/out) : DB_C_NUMERIC
 *
 * Note: This routine returns the negative (2's complement) of arg in answer.
 *       The argument answer is modified in place.
 */
static void
numeric_negate (DB_C_NUMERIC answer)
{
  numeric_negate_long (answer, false);
}

/*
 * numeric_negate_long () -
 *   return:
 *   answer(in/out) : DB_C_NUMERIC
 *   is_long_num(in): is long NUMERIC
 *
 * Note: This routine returns the negative (2's complement) of arg in answer.
 *       The argument answer is modified in place.
 */
static void
numeric_negate_long (DB_C_NUMERIC answer, bool is_long_num)
{
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

  /* Complement all bits of answer */
  for (digit = 0; digit < buf_size; digit++)
    {
      answer[digit] = ~(answer[digit]);
    }

  /* Add one to answer */
  numeric_increase_long (answer, is_long_num);
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
 * numeric_is_long () -
 *   return: bool
 *   arg(in)    : DB_C_NUMERIC
 *
 * Note: This routine checks if -2**31 <= arg <= 2**31-1
 */
static bool
numeric_is_long (DB_C_NUMERIC arg)
{
  unsigned int digit;
  unsigned char pad;

  /* Get pad value */
  pad = arg[0];
  if (pad != 0xff && pad != 0)
    {
      return (false);
    }

  /*
   * Loop through arg's bits except the 32 LSB looking for non-sign
   * extended values
   */
  for (digit = 1; digit < DB_NUMERIC_BUF_SIZE - sizeof (int); digit++)
    {
      if (arg[digit] != pad)
	{
	  return (false);
	}
    }

  return (arg[digit] & 0x80) == (pad & 0x80) ? true : false;
}

/*
 * numeric_is_bigint () -
 *   return: bool
 *   arg(in)    : DB_C_NUMERIC
 *
 * Note: This routine checks if -2**63 <= arg <= 2**63-1
 */
static bool
numeric_is_bigint (DB_C_NUMERIC arg)
{
  unsigned int digit;
  unsigned char pad;

  /* Get pad value */
  pad = arg[0];
  if (pad != 0xff && pad != 0)
    {
      return (false);
    }

  /*
   * Loop through arg's bits except the 64 LSB looking for non-sign
   * extended values
   */
  for (digit = 1; digit < DB_NUMERIC_BUF_SIZE - sizeof (DB_BIGINT); digit++)
    {
      if (arg[digit] != pad)
	{
	  return (false);
	}
    }

  return (arg[digit] & 0x80) == (pad & 0x80) ? true : false;
}

/*
 * numeric_is_bit_set () -
 *   return: bool
 *   arg(in)    : DB_C_NUMERIC
 *   pos(in)    : position of the bit inside arg
 *
 * Note: This routine checks if pos'th bit of arg is 1.
 */
static bool
numeric_is_bit_set (DB_C_NUMERIC arg, int pos)
{
  return ((arg[pos / 8]) & (0x01 << (7 - (pos % 8)))) ? true : false;
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
  unsigned char narg[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */

  if (numeric_is_negative (arg))
    {
      numeric_copy (narg, arg);
      numeric_negate (narg);
      return (numeric_compare_with_pow10 (narg, numeric_get_pow_of_10 (exp)) >= 0) ? true : false;
    }
  else
    {
      return (numeric_compare_with_pow10 (arg, numeric_get_pow_of_10 (exp)) >= 0) ? true : false;
    }
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
  unsigned char neg_arg2[2 * DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */

  /* Make arg2 negative (use 2's complement) */
  numeric_copy (neg_arg2, arg2);
  numeric_negate (neg_arg2);

  /* Add arg1 and neg_arg2 */
  numeric_add (arg1, neg_arg2, answer, size);
}

/*
 * numeric_mul () -
 *   return:
 *   a1(in)     : DB_C_NUMERIC
 *   a2(in)     : DB_C_NUMERIC
 *   positive_ans(out): bool if the answer's is positive (true)
 *                      or negative (false)
 *   answer(out) : DB_C_NUMERIC
 *
 * Note: This routine multiplies two numerics and returns the results.
 */
static void
numeric_mul (DB_C_NUMERIC a1, DB_C_NUMERIC a2, bool * positive_ans, DB_C_NUMERIC answer)
{
  unsigned int answer_bit;
  int digit1;
  int digit2;
  int shift;
  unsigned char temp_term[2 * DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char temp_arg1[2 * DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char temp_arg2[2 * DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char arg1[DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */
  unsigned char arg2[DB_NUMERIC_BUF_SIZE];	/* copy of DB_C_NUMERIC */

  /* Initialize the answer */
  numeric_zero (answer, 2 * DB_NUMERIC_BUF_SIZE);
  *positive_ans = true;

  /* Check if either arg = 0 */
  if (numeric_is_zero (a1) || numeric_is_zero (a2))
    {
      return;
    }

  /* If arg1 is negative, toggle sign and make arg1 positive */
  numeric_copy (arg1, a1);
  numeric_copy (arg2, a2);
  if (numeric_is_negative (arg1))
    {
      numeric_negate (arg1);
      *positive_ans = false;
    }

  /* If arg2 is negative, toggle sign and make arg2 positive */
  if (numeric_is_negative (arg2))
    {
      numeric_negate (arg2);
      *positive_ans = !(*positive_ans);
    }

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
  int neg_sign = 0;
  int neg_remainder = false;

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

  /* If arg1 is negative, toggle sign and make arg1 positive */
  if (numeric_is_negative (arg1))
    {
      numeric_negate_long (arg1, is_long_num);
      neg_sign = ~neg_sign;
      neg_remainder = true;
    }

  /* If arg2 is negative, toggle sign and make arg2 positive */
  if (numeric_is_negative (arg2))
    {
      numeric_negate (arg2);
      neg_sign = ~neg_sign;
    }

  /* Initialize variables */
  numeric_coerce_int_to_num (0, remainder);
  numeric_copy_long (answer, arg1, is_long_num);
  numeric_copy (neg_arg2, arg2);
  numeric_negate (neg_arg2);

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
	  numeric_add (remainder, neg_arg2, remainder, DB_NUMERIC_BUF_SIZE);
	  answer[buf_size - 1] += 1;
	}
    }

  /* If the sign is negative, negate the answer */
  if (neg_sign)
    {
      numeric_negate_long (answer, is_long_num);
    }

  /* If the remainder is negative, negate it */
  if (neg_remainder)
    {
      numeric_negate (remainder);
    }
}

/*
 * numeric_div () -
 *   return:
 *   arg1(in)   : DB_C_NUMERIC             (numerator)
 *   arg2(in)   : DB_C_NUMERIC             (denominator)

 *   answer(in) : DB_C_NUMERIC
 *   remainder(in)      : DB_C_NUMERIC
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
numeric_div (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2, DB_C_NUMERIC answer, DB_C_NUMERIC remainder)
{
  /* Case 1 - arg2 = 0 */
  if (numeric_is_zero (arg2))
    {
      /* SIGFPE ??, +/- MAX_NUM_DATA ?? */
    }

  /* Case 2 - arg1 = 0.  Set answer and remainder to 0.  */
  else if (numeric_is_zero (arg1))
    {
      numeric_coerce_int_to_num (0, remainder);
      numeric_coerce_int_to_num (0, answer);
    }

  /* Case 3 - arg1, arg2 are long ints. Do machine divide */
  else if (numeric_is_long (arg1) && numeric_is_long (arg2))
    {
      int long_arg1, long_arg2;

      numeric_coerce_num_to_int (arg1, &long_arg1);
      numeric_coerce_num_to_int (arg2, &long_arg2);
      numeric_coerce_int_to_num ((long_arg1 / long_arg2), answer);
      numeric_coerce_int_to_num ((long_arg1 % long_arg2), remainder);
    }

  /* Case 4 - arg1, arg2 are bigints. Do machine divide */
  else if (numeric_is_bigint (arg1) && numeric_is_bigint (arg2))
    {
      DB_BIGINT bi_arg1, bi_arg2;

      numeric_coerce_num_to_bigint (arg1, 0, &bi_arg1);
      numeric_coerce_num_to_bigint (arg2, 0, &bi_arg2);
      numeric_coerce_bigint_to_num ((bi_arg1 / bi_arg2), answer);
      numeric_coerce_bigint_to_num ((bi_arg1 % bi_arg2), remainder);
    }

  /* Default case: perform long division */
  else
    {
      numeric_long_div (arg1, arg2, answer, remainder, false);
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

  if (numeric_is_negative (arg))
    {
      for (i = 0; i < total_nums; i++)
	{
	  if (arg[i] != 0xff)
	    {
	      return true;
	    }
	}

      if (!(arg[i] & 0x80))
	{
	  return true;
	}

    }
  else
    {
      for (i = 0; i < total_nums; i++)
	{
	  if (arg[i] != 0)
	    {
	      return true;
	    }
	}

      if (arg[i] & 0x80)
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
  bool is_negative;
  int i;

  is_negative = numeric_is_negative (arg);
  for (i = 0; i < DB_LONG_NUMERIC_MULTIPLIER - 1; i++)
    {
      if (is_negative)
	{
	  numeric_negative_one (long_answer + i * DB_NUMERIC_BUF_SIZE, DB_NUMERIC_BUF_SIZE);
	}
      else
	{
	  numeric_zero (long_answer + i * DB_NUMERIC_BUF_SIZE, DB_NUMERIC_BUF_SIZE);
	}
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
 * numeric_compare () -
 *   return:
 *   arg1(in)   : DB_C_NUMERIC
 *   arg2(in)   : DB_C_NUMERIC
 *
 * Note: This routine compares two DB_C_NUMERIC values.
 *       This function returns:
 *          -1   if    arg1 < arg2
 *           0   if    arg1 = arg2 and
 *           1   if    arg1 > arg2.
 */
static int
numeric_compare (DB_C_NUMERIC arg1, DB_C_NUMERIC arg2)
{
  unsigned char narg1[DB_NUMERIC_BUF_SIZE];
  unsigned char narg2[DB_NUMERIC_BUF_SIZE];
  int arg1_sign, arg2_sign;	/* 0 if positive */

  arg1_sign = numeric_is_negative (arg1) ? 1 : 0;
  arg2_sign = numeric_is_negative (arg2) ? 1 : 0;

  if (arg1_sign < arg2_sign)
    {				/* arg1 >= 0, arg2 < 0 */
      return (1);
    }
  else if (arg1_sign > arg2_sign)
    {				/* arg1 < 0, arg2 >= 0 */
      return (-1);
    }
  else
    {
      if (arg1_sign == 0)
	{			/* arg1 >= 0, arg2 >= 0 */
	  return numeric_compare_pos (arg1, arg2);
	}
      else
	{			/* arg1 < 0, arg2 < 0 */
	  numeric_copy (narg1, arg1);	/* need copy? */
	  numeric_negate (narg1);
	  numeric_copy (narg2, arg2);	/* need copy? */
	  numeric_negate (narg2);
	  return -numeric_compare_pos (narg1, narg2);
	}
    }
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
  bool negative = false;

  answer = 0;
  if (numeric_is_negative (arg))
    {
      negative = true;
      numeric_negate_long (arg, is_long_num);
    }

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

  if (negative)
    {
      numeric_negate_long (arg, is_long_num);
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
  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);
  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);
  if (scale1 == scale2)
    {
      cprec = MAX (prec1, prec2);
      db_make_numeric (dbv1_common, db_locate_numeric (dbv1), cprec, scale1);
      db_make_numeric (dbv2_common, db_locate_numeric (dbv2), cprec, scale2);
    }

  /* Otherwise scale and reset the numbers */
  else if (scale1 < scale2)
    {
      scale_diff = scale2 - scale1;
      prec1 = scale_diff + prec1;
      if (prec1 > DB_MAX_NUMERIC_PRECISION)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  return ER_IT_DATA_OVERFLOW;
	}
      numeric_scale_dec (db_locate_numeric (dbv1), scale_diff, temp);
      cprec = MAX (prec1, prec2);
      db_make_numeric (dbv1_common, temp, cprec, scale2);
      db_make_numeric (dbv2_common, db_locate_numeric (dbv2), cprec, scale2);
    }
  else
    {
      scale_diff = scale1 - scale2;
      prec2 = scale_diff + prec2;
      if (prec2 > DB_MAX_NUMERIC_PRECISION)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  return ER_IT_DATA_OVERFLOW;
	}
      numeric_scale_dec (db_locate_numeric (dbv2), scale_diff, temp);
      cprec = MAX (prec1, prec2);
      db_make_numeric (dbv2_common, temp, cprec, scale1);
      db_make_numeric (dbv1_common, db_locate_numeric (dbv1), cprec, scale1);
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
  unsigned char num1[DB_NUMERIC_BUF_SIZE], num2[DB_NUMERIC_BUF_SIZE];
  unsigned char temp[DB_NUMERIC_BUF_SIZE];
  int ret;

  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);
  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);

  scale = MAX (scale1, scale2);
  prec = DB_MAX_NUMERIC_PRECISION;

  numeric_copy (num1, db_locate_numeric (dbv1));
  numeric_copy (num2, db_locate_numeric (dbv2));

  ret = numeric_coerce_num_to_num (num1, prec1, scale1, prec, scale, temp);
  if (ret != NO_ERROR)
    {
      return ret;
    }
  db_make_numeric (dbv1_common, temp, prec, scale);

  ret = numeric_coerce_num_to_num (num2, prec2, scale2, prec, scale, temp);
  if (ret != NO_ERROR)
    {
      return ret;
    }
  db_make_numeric (dbv2_common, temp, prec, scale);

  return ret;
}

/*
 * numeric_coerce_big_num_to_dec_str () -
 *   return:
 *   num(in)    : buffer twice the size of a DB_C_NUMERIC
 *   dec_str(out): returned string of decimal digits as ASCII chars
 *
 * Note: This routine converts a DB_C_NUMERIC into a character string that is
 *       TWICE_NUM_MAX_PREC characters long that contains the decimal digits of
 *       the numeric encoded as ASCII characters.
 *       THIS ROUTINE ASSUMES THAT THE NUMERIC BUFFER REPRESENTS A POSITIVE
 *       VALUE.
 */
static void
numeric_coerce_big_num_to_dec_str (unsigned char *num, char *dec_str)
{
  DEC_STRING *bit_value;
  DEC_STRING result;
  unsigned int i;

  /* Loop through the bits of the numeric building up string */
  numeric_init_dec_str (&result);
  for (i = 0; i < DB_NUMERIC_BUF_SIZE * 16; i++)
    {
      if (numeric_is_bit_set (num, i))
	{
	  bit_value = numeric_get_pow_of_2 ((DB_NUMERIC_BUF_SIZE * 16) - i - 1);
	  numeric_add_dec_str (bit_value, &result, &result);
	}
    }

  /* Convert result into ASCII array */
  for (i = 0; i < TWICE_NUM_MAX_PREC; i++)
    {
      if (result.digits[i] == -1)
	{
	  result.digits[i] = 0;
	}
      assert (result.digits[i] >= 0);

      *dec_str = result.digits[i] + '0';
      dec_str++;
    }

  /* Null terminate */
  *dec_str = '\0';
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
  if (src_prec <= DB_MAX_NUMERIC_PRECISION)
    //if (src_prec <= DB_INTERNAL_NUMERIC_PRECISION_LIMIT)
    {
      numeric_copy (dest, &(src[DB_NUMERIC_BUF_SIZE]));
      *dest_prec = src_prec;
      *dest_scale = src_scale;
    }

  /* The remaining cases are for when the precision of the source overflows. */

  /* Case 1: The scale of the source does *not* overflow */
  else if (src_scale <= DB_MAX_NUMERIC_PRECISION)
    //else if (src_scale <= DB_INTERNAL_NUMERIC_PRECISION_LIMIT)
    {
      /* If upper half of *src is zero, merely copy, reset precision, and return */
      if (numeric_is_zero (src) && src[DB_NUMERIC_BUF_SIZE] <= 0x7F)
	{
	  numeric_copy (dest, &(src[DB_NUMERIC_BUF_SIZE]));
	  *dest_prec = DB_MAX_NUMERIC_PRECISION;
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
      int truncation_diff = src_prec - DB_MAX_NUMERIC_PRECISION;

      *dest_scale = src_scale - truncation_diff;
      *dest_prec = DB_MAX_NUMERIC_PRECISION;

      /* Truncate the obsolete trailing digits. (Note: numeric_coerce_big_num_to_dec_str is guaranteed ro return a
       * NULL-terminated buffer that is TWICE_NUM_MAX_PREC characters long.) */
      numeric_coerce_big_num_to_dec_str (src, dec_digits);
      dec_digits[TWICE_NUM_MAX_PREC - truncation_diff] = '\0';
      numeric_coerce_dec_str_to_num (dec_digits, dest);
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
numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer, bool * is_fp_numeric_op)
{
  DB_VALUE dbv1_common, dbv2_common;
  int ret = NO_ERROR;
  unsigned int prec;
  unsigned char temp[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  TP_DOMAIN *domain;
  int scale1, scale2, result_scale;
  int prec1, prec2, result_prec, calc_prec1, calc_prec2;

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
      //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);
      return NO_ERROR;
    }

  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);
  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);
  calc_prec1 = prec1 - scale1;
  calc_prec2 = prec2 - scale2;

  result_scale = MAX (scale1, scale2);
  result_prec = MAX (calc_prec1, calc_prec2) + result_scale;

  if (result_prec >= DB_MAX_NUMERIC_PRECISION)
    {
      /* 1바이트 씩 계산하는 새로운 함수 테스트 */
//       {
//         struct timespec start, end;
//         double time_spent;

//      clock_gettime(CLOCK_REALTIME, &start);

//      int i = 0;
//      int tmp_prec = result_prec;
//      int tmp_scale = result_scale;
//      int cnt = 1000000;
//      while (i < cnt)
//        {
//          result_prec = tmp_prec;
//          result_scale = tmp_scale;
//          ret = floating_point_numeric_add(dbv1, dbv2, &result_prec, &result_scale, answer);  
//          i++;
//        }
//      clock_gettime(CLOCK_REALTIME, &end);

//      time_spent = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;
//      fprintf(stderr, "[DEBUG] %d번 실행 시간: %.6f 초\n", cnt, time_spent);
//      fprintf(stderr, "[DEBUG] 1회당 평균 시간: %.9f 초\n", time_spent / cnt);
//       }

      /* 1바이트 씩 계산하는 새로운 함수 */
      ret = floating_point_numeric_add (dbv1, dbv2, &result_prec, &result_scale, answer);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      *is_fp_numeric_op = true;
    }
  else
    {
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
      numeric_add (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common), temp, DB_NUMERIC_BUF_SIZE);
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
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		      pr_type_name (TP_DOMAIN_TYPE (domain)));
	      ret = ER_IT_DATA_OVERFLOW;
	      goto exit_on_error;
	    }
	}
      db_make_numeric (answer, temp, prec, DB_VALUE_SCALE (&dbv1_common));
    }

  return ret;

exit_on_error:

  //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

static int
floating_point_numeric_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec, int *result_scale,
			    DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int calc_bytes, calc_prec;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE] = { 0 };

  /* 1) 지수 정렬 계산 */
  int scale1 = DB_VALUE_SCALE (dbv1);
  int scale2 = DB_VALUE_SCALE (dbv2);
  int exponent1 = *result_scale - scale1;
  int exponent2 = *result_scale - scale2;

  // 2) 부호 확인 및 음수 -> 양수 변환
  unsigned char *dbv1_copy = (unsigned char *) db_locate_numeric (dbv1);
  unsigned char *dbv2_copy = (unsigned char *) db_locate_numeric (dbv2);
  bool arg1_sign = false, arg2_sign = false, result_sign = false;

  // 덧셈도 numeric_is_zero 추가해서, 바로 return 할 수 있게 처리 필요
  // 123 + 0 = 123, 0 + 123 = 123, 0 + 0 = 0

  arg1_sign = numeric_is_negative (db_locate_numeric (dbv1)) ? true : false;
  if (arg1_sign)
    {
      numeric_negate (dbv1_copy);
    }

  arg2_sign = numeric_is_negative (db_locate_numeric (dbv2)) ? true : false;
  if (arg2_sign)
    {
      numeric_negate (dbv2_copy);
    }

  // 3) 필요한 바이트 수 계산
  calc_bytes = calc_bytes_from_prec (*result_prec) + 1;
  if (calc_bytes < 1)
    {
      return ER_FAILED;
    }

  // 4) 새로운 계산 버퍼 초기화
  uint8_t dbv1_buf[calc_bytes];
  uint8_t dbv2_buf[calc_bytes];
  uint8_t calc_buf[calc_bytes] = { 0 };

  (void) fp_numeric_pad (dbv1_copy, dbv1_buf, calc_bytes);
  (void) fp_numeric_pad (dbv2_copy, dbv2_buf, calc_bytes);

  // 5) scale 보정
  if (exponent1)
    {
      fp_numeric_mul_pow10 (dbv1_buf, calc_bytes, exponent1);
    }
  if (exponent2)
    {
      fp_numeric_mul_pow10 (dbv2_buf, calc_bytes, exponent2);
    }

//   fprintf(stderr, "[DEBUG] dbv1_buf : ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", dbv1_buf[i]);
//   fprintf(stderr, "\n");

//   fprintf(stderr, "[DEBUG] dbv2_buf : ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", dbv2_buf[i]);
//   fprintf(stderr, "\n");

  // 6) 덧셈
  // 덧셈 결과를 add 와 sub로 분기를 둔 이유 :
  //   음수의 경우 각 바이트에 반전을 하여 저장하는데, scale 보정 단계에서 calc_bytes 보다 더 큰 바이트 가 필요하게됨.
  //   해당 case들만 따로 분기처리 하는 경우 오히려 더 많은 분기가 발생할 수 있어 아래와 같이 모든 경우를 처리할 수 있게 분기됨.
  if (arg1_sign == arg2_sign)
    {
      (void) fp_numeric_add (dbv1_buf, dbv2_buf, calc_buf, calc_bytes);
      result_sign = arg1_sign;	// 결과 부호 = 입력 부호
    }
  else
    {
      if (fp_numeric_cmp_base256 (dbv1_buf, dbv2_buf, calc_bytes) >= 0)
	{
	  // |arg1| >= |arg2|
	  (void) fp_numeric_sub (dbv1_buf, dbv2_buf, calc_buf, calc_bytes);
	  result_sign = arg1_sign;	// 결과 부호 = 큰 수의 부호
	}
      else
	{
	  // |arg1| < |arg2|
	  (void) fp_numeric_sub (dbv2_buf, dbv1_buf, calc_buf, calc_bytes);
	  result_sign = arg2_sign;	// 결과 부호 = 큰 수의 부호
	}
    }

//   fprintf(stderr, "[DEBUG] add mantissa_hex: ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", calc_buf[i]);
//   fprintf(stderr, "\n");

  // 7) 덧셈 이후 prec 재계산
  // 7-1) 0이 될때까지 10으로 나누며 몇 번 나눴는지 카운트 -- 제거
  //calc_prec = count_digits_by_division (calc_buf, calc_bytes);

  // 7-2) 미리 계산된 테이블 과 이진 탐색 사용
  calc_prec = fp_numeric_overflow2 (calc_buf, calc_bytes);
  if (calc_prec > *result_prec)
    {
      (*result_prec) = calc_prec;
    }

//   fprintf(stderr, "[DEBUG] calc_prec=%d\n", calc_prec);
//   fprintf(stderr, "\n");

  // 8) 반올림 & 다시 16바이트로 pack
  fp_numeric_round_and_pack (calc_buf, calc_bytes, result_buf, result_prec, result_scale);

//   fprintf(stderr, "[DEBUG] final mantissa_hex: ");
//   for (int i = 0; i < 16; i++)
//     fprintf(stderr, "%02X", result_buf[i]);
//   fprintf(stderr, "\n");

  // 9) 결과
  if (result_sign)
    {
      // 결과가 음수인 경우
      numeric_negate ((unsigned char *) result_buf);
    }
  db_make_numeric (answer, result_buf, *result_prec, *result_scale);

  return ret;
}

static int
calc_bytes_from_prec (int prec)
{
  const double log2_10 = 3.3219280948873626;
  double bits;
  // 필요한 비트 수
  bits = 1.0 + prec * log2_10;
  // 바이트로 나눠서 올림
  return (int) ceil (bits / 8.0);
}

/* 바이트 값 padding 후, 바이트 단위 버퍼를 워드 단위 버퍼로 변환 */
static void
fp_numeric_pad (uint8_t * src_buf, uint8_t * padded_dbv_buf, int calc_bytes)
{
  int pad = calc_bytes - DB_NUMERIC_BUF_SIZE;
  memset (padded_dbv_buf, 0, pad);
  memcpy (padded_dbv_buf + pad, src_buf, DB_NUMERIC_BUF_SIZE);
}

/* 2) buf[bytes] *= 10^d  (한 바이트당 0..255인 base-256) */
static void
fp_numeric_mul_pow10 (uint8_t * dbv_buf, int calc_bytes, int exponent)
{
  int i = 0;
  uint16_t carry = 0;
  uint32_t temp = 0;
  while (exponent-- > 0)
    {
      carry = 0;
      for (i = calc_bytes - 1; i >= 0; --i)
	{
	  temp = (uint32_t) dbv_buf[i] * 10 + carry;
	  dbv_buf[i] = (uint8_t) (temp & 0xFF);
	  carry = temp >> 8;
	}
    }
}

/* 3) res[i] = a[i] + b[i]  (i=bytes-1..0), carry 전파 */
static void
fp_numeric_add (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, uint8_t * calc_buf, int calc_bytes)
{
  int i = 0;
  uint16_t carry = 0;
  uint16_t temp = 0;
  for (i = calc_bytes - 1; i >= 0; --i)
    {
      temp = (uint16_t) dbv1_buf[i] + dbv2_buf[i] + carry;
      calc_buf[i] = (uint8_t) (temp & 0xFF);
      carry = temp >> 8;
    }
}

static int
fp_numeric_cmp_base256 (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, int calc_bytes)
{
  int i = 0;
  for (i = 0; i < calc_bytes; i++)
    {
      if (dbv1_buf[i] != dbv2_buf[i])
	{
	  return dbv1_buf[i] < dbv2_buf[i] ? -1 : 1;
	}
    }
  return 0;
}

static void
fp_numeric_init_pow10_table (void)
{
#if !defined(SERVER_MODE)
  /* If this is the first time to call this routine, initialize */
  if (!initialized_10)
    {
      numeric_init_pow_of_10_helper ();
      initialized_10 = true;
    }
#endif
}

// static int
// fp_numeric_overflow (const uint8_t * calc_buf, int calc_bytes)
// {
//   int lo = 0, hi = 0;
//   int mid = 0;
//   int i = 0;
//   const uint8_t *tmp = NULL;
//   int calc_first_nonzero = 0;
//   int tmp_first_nonzero = 0;

//   // 매핑 테이블 생성
//   fp_numeric_init_pow10_table ();
//   hi = POW10_MAX_INDEX;

//   // val의 첫 번째 0이 아닌 바이트 위치 찾기
//   for (i = 0; i < calc_bytes; i++)
//     {
//       if (calc_buf[i] != 0)
//      {
//        calc_first_nonzero = calc_bytes - i;
//        break;
//      }
//     }

//   while (lo < hi)
//     {
//       mid = (lo + hi + 1) >> 1;
//       tmp = (uint8_t *) powers_of_10[mid] + (POW10_BUF_SIZE - calc_bytes);

//       tmp_first_nonzero = _gv_powers_of_10_effective_bytes[mid];

// //       printf("compare[%2d]: val = ", mid);
// //       for (int i = 0; i < calc_bytes; i++)
// //         printf("%02X", calc_buf[i]);
// //       printf("  vs pow10[%2d] = ", mid + 1);
// //       for (int i = 0; i < POW10_BUF_SIZE; i++)
// //         printf("%02X", powers_of_10[mid][i]);
// //       printf("  val_first_nonzero = %d, tmp_first_nonzero = %d\n", calc_first_nonzero, tmp_first_nonzero);
// //       printf("\n");

//       if (calc_first_nonzero < tmp_first_nonzero)
//      {
//        // val이 더 작음
//        hi = mid - 1;
//      }
//       else if (calc_first_nonzero > tmp_first_nonzero)
//      {
//        // val이 더 큼
//        lo = mid;
//      }
//       else
//      {
//        // 위치가 같으면 상세 비교
//        if (fp_numeric_cmp_base256 (calc_buf, tmp, calc_bytes) >= 0)
//          {
//            lo = mid;
//          }
//        else
//          {
//            hi = mid - 1;
//          }
//      }
//     }
//   return lo + 1;
// }

static int
fp_numeric_overflow2 (const uint8_t * calc_buf, int calc_bytes)
{
  int start_idx, idx;
  int calc_first_nonzero = 0;
  const uint8_t *tmp;
  int tmp_first_nonzero;

  // 매핑 테이블 준비
  fp_numeric_init_pow10_table ();

  // calc_buf의 유효 바이트 수 계산
  for (int i = 0; i < calc_bytes; i++)
    {
      if (calc_buf[i] != 0)
	{
	  calc_first_nonzero = calc_bytes - i;
	  break;
	}
    }

  // 그 유효바이트의 첫 등장 지수
  start_idx = _gv_powers_of_10_effective_bytes[calc_first_nonzero - 1];

  // start_idx부터 같은 유효바이트 구간을 순차 비교
  idx = start_idx;
  while (idx <= POW10_MAX_INDEX)
    {
      tmp = powers_of_10[idx] + (POW10_BUF_SIZE - calc_bytes);

      // pow10[idx]의 유효 바이트 계산
      tmp_first_nonzero = calc_bytes;
      for (int i = 0; i < calc_bytes; i++)
	{
	  if (tmp[i] != 0)
	    {
	      tmp_first_nonzero = calc_bytes - i;
	      break;
	    }
	}

      // 유효 바이트가 다르면 범위 끝
      if (tmp_first_nonzero != calc_first_nonzero)
	{
	  break;
	}

      // 유효 바이트가 같으면 값 비교
      if (fp_numeric_cmp_base256 (calc_buf, tmp, calc_bytes) < 0)
	{
	  // calc_buf < 10^idx → 이전 지수가 결과
	  return idx;
	}
      idx++;
    }

  // 같은 유효 바이트 구간 끝까지도 calc_buf >= pow10 → overflow 한계는 idx
  return idx;
}

/*
 * buf : big-endian base-256 동적 버퍼
 * bytes : buf 길이
 * raw_prec : 전체 10진 자릿수 (예:45)
 * out_scale : 음수 scale 반환
 * raw_prec 자리 중 앞 KEEP=38만 살리고 39번째 자리에서 반올림
 */
static void
fp_numeric_round_and_pack (uint8_t * calc_buf, int calc_bytes, uint8_t * result_buf, int *result_prec,
			   int *result_scale)
{
  int keep = DB_MAX_NUMERIC_PRECISION;
  uint16_t rem10 = 0;
  int drop = 0;
  int i = 0;
  int round_prec = 0;

  drop = *result_prec - keep;
  if (drop <= 0)
    {
      // 38자리 이하면, 잘라내거나 반올림 전 단계가 전혀 없이 끝남. 
      memcpy (result_buf, calc_buf + (calc_bytes - DB_NUMERIC_BUF_SIZE), DB_NUMERIC_BUF_SIZE);
      return;
    }

  // 39자리 이상이면, 38자리까지 잘라내고, 39자리에서 반올림 처리

  // 1) prec, scale 재조정
  *result_prec = keep;
  *result_scale = *result_scale - drop;

  // 2) 38 자리 까지 값을 나눠서 짜르고, 39자리는 rem10에 저장(반올림 여부 확인용)
  for (i = 0; i < drop; i++)
    {
      rem10 = fp_numeric_div_pow10 (calc_buf, calc_bytes);
    }

  // 3) half-up 반올림: rem10 >= 5 이면 +1
  if (rem10 >= 5)
    {
      (void) fp_numeric_increment (calc_buf, calc_bytes, 1);
      //round_prec = count_digits_by_division (calc_buf, calc_bytes);
      round_prec = fp_numeric_overflow2 (calc_buf, calc_bytes);
      if (round_prec > keep)
	{
	  (void) fp_numeric_div_pow10 (calc_buf, calc_bytes);
	  (*result_scale)--;
	}
    }

  // 4) 38자리로 다시 재정렬한 버퍼를 result_buf에 복사
  memcpy (result_buf, calc_buf + (calc_bytes - DB_NUMERIC_BUF_SIZE), DB_NUMERIC_BUF_SIZE);

  return;
}

static uint16_t
fp_numeric_div_pow10 (uint8_t * calc_buf, int calc_bytes)
{
  uint32_t temp = 0;		// temp를 16비트로하면, (*rem10 << 8) | calc_buf[i] 과정에서 오버플로우 발생.
  uint16_t rem10 = 0;		// rem10 값을 8비트로하면, 중간계산에서 부족할 수 있음.
  int i = 0;

  for (i = 0; i < calc_bytes; i++)
    {
      temp = (rem10 << 8) | calc_buf[i];
      calc_buf[i] = (uint8_t) (temp / 10);
      rem10 = (uint16_t) (temp % 10);
    }
  return rem10;
}

 /**
  * buf += val (val <= 255)
  */
static void
fp_numeric_increment (uint8_t * calc_buf, int calc_bytes, uint8_t val)
{
  int i = 0;
  uint16_t temp = 0;
  uint16_t carry = val;
  for (i = calc_bytes - 1; i >= 0 && carry; --i)
    {
      temp = (uint16_t) calc_buf[i] + carry;
      calc_buf[i] = (uint8_t) (temp & 0xFF);
      carry = temp >> 8;
    }
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
numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer, bool * is_fp_numeric_op)
{
  DB_VALUE dbv1_common, dbv2_common;
  int ret = NO_ERROR;
  unsigned int prec;
  unsigned char temp[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  TP_DOMAIN *domain;
  int scale1, scale2, result_scale;
  int prec1, prec2, result_prec, calc_prec1, calc_prec2;

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
      //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);
      return NO_ERROR;
    }

  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);
  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);
  calc_prec1 = prec1 - scale1;
  calc_prec2 = prec2 - scale2;

  result_scale = MAX (scale1, scale2);
  result_prec = MAX (calc_prec1, calc_prec2) + result_scale;

  if (result_prec >= DB_MAX_NUMERIC_PRECISION)
    {
      ret = floating_point_numeric_sub (dbv1, dbv2, &result_prec, &result_scale, answer);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      *is_fp_numeric_op = true;
    }
  else
    {
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
      numeric_sub (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common), temp, DB_NUMERIC_BUF_SIZE);
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
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		      pr_type_name (TP_DOMAIN_TYPE (domain)));
	      ret = ER_IT_DATA_OVERFLOW;
	      goto exit_on_error;
	    }
	}
      db_make_numeric (answer, temp, prec, DB_VALUE_SCALE (&dbv1_common));
    }

  return ret;

exit_on_error:

  //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

static int
floating_point_numeric_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec, int *result_scale,
			    DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int calc_bytes, calc_prec;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE] = { 0 };

  /* 1) 지수 정렬 계산 */
  int scale1 = DB_VALUE_SCALE (dbv1);
  int scale2 = DB_VALUE_SCALE (dbv2);
  int exponent1 = *result_scale - scale1;
  int exponent2 = *result_scale - scale2;

  // 2) 부호 확인 및 음수 -> 양수 변환
  unsigned char *dbv1_copy = (unsigned char *) db_locate_numeric (dbv1);
  unsigned char *dbv2_copy = (unsigned char *) db_locate_numeric (dbv2);
  bool arg1_sign = false, arg2_sign = false, result_sign = false;

  // 뺄셈도 numeric_is_zero 추가해서, 바로 return 할 수 있게 처리 필요
  // 123 - 0 = 123, 0 - 123 = -123, 0 - 0 = 0

  arg1_sign = numeric_is_negative (db_locate_numeric (dbv1)) ? true : false;
  if (arg1_sign)
    {
      numeric_negate (dbv1_copy);
    }

  arg2_sign = numeric_is_negative (db_locate_numeric (dbv2)) ? true : false;
  if (arg2_sign)
    {
      numeric_negate (dbv2_copy);
    }

  // 3) 필요한 바이트 수 계산
  calc_bytes = calc_bytes_from_prec (*result_prec) + 1;
  if (calc_bytes < 1)
    {
      return ER_FAILED;
    }

  // 4) 새로운 계산 버퍼 초기화
  uint8_t dbv1_buf[calc_bytes];
  uint8_t dbv2_buf[calc_bytes];
  uint8_t calc_buf[calc_bytes] = { 0 };

  (void) fp_numeric_pad (dbv1_copy, dbv1_buf, calc_bytes);
  (void) fp_numeric_pad (dbv2_copy, dbv2_buf, calc_bytes);

  // 5) scale 보정
  if (exponent1)
    {
      fp_numeric_mul_pow10 (dbv1_buf, calc_bytes, exponent1);
    }
  if (exponent2)
    {
      fp_numeric_mul_pow10 (dbv2_buf, calc_bytes, exponent2);
    }

  // 6) 뺄셈
  if (arg1_sign == arg2_sign)
    {
      if (fp_numeric_cmp_base256 (dbv1_buf, dbv2_buf, calc_bytes) >= 0)
	{
	  // |arg1| >= |arg2|
	  (void) fp_numeric_sub (dbv1_buf, dbv2_buf, calc_buf, calc_bytes);
	  result_sign = arg1_sign;	// 결과 부호 = 큰 수의 부호
	}
      else
	{
	  // |arg1| < |arg2|
	  (void) fp_numeric_sub (dbv2_buf, dbv1_buf, calc_buf, calc_bytes);
	  result_sign = !arg2_sign;	// 결과 부호 = 큰 수의 부호
	}
    }
  else
    {
      (void) fp_numeric_add (dbv1_buf, dbv2_buf, calc_buf, calc_bytes);
      result_sign = arg1_sign;	// 결과 부호 = 입력 부호
    }

  // 7) 뺄셈 이후 prec 재계산
  calc_prec = fp_numeric_overflow2 (calc_buf, calc_bytes);
  if (calc_prec > *result_prec)
    {
      (*result_prec) = calc_prec;
    }

  // 8) 반올림 & 다시 16바이트로 pack
  fp_numeric_round_and_pack (calc_buf, calc_bytes, result_buf, result_prec, result_scale);


  // 9) 결과 저장
  if (result_sign)
    {
      // 결과가 음수인 경우
      numeric_negate ((unsigned char *) result_buf);
    }
  db_make_numeric (answer, result_buf, *result_prec, *result_scale);

  return ret;
}

static void
fp_numeric_sub (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, uint8_t * calc_buf, int calc_bytes)
{
  int i = 0;
  int borrow = 0;
  int temp = 0;
  for (i = calc_bytes - 1; i >= 0; i--)
    {
      temp = (int) dbv1_buf[i] - (int) dbv2_buf[i] - borrow;
      if (temp < 0)
	{
	  temp += 256;
	  borrow = 1;
	}
      else
	{
	  borrow = 0;
	}

      calc_buf[i] = (uint8_t) (temp & 0xFF);
    }
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
numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer, bool * is_fp_numeric_op)
{
  int ret = NO_ERROR;
  int prec;
  int scale;
  bool positive_ans;
  unsigned char temp[2 * DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  unsigned char result[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  int scale1, scale2, result_scale;	// 나중에 기존 scale 와 합칠 수 있도록 예정
  int prec1, prec2, result_prec;	// 나중에 기존 prec 와 합칠 수 있도록 예정

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
      //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);
      return NO_ERROR;
    }

  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);
  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);
  if (scale1 < 0)
    {
      prec1 -= scale1;
      scale1 = 0;
    }
  if (scale2 < 0)
    {
      prec2 -= scale2;
      scale2 = 0;
    }

  result_scale = scale1 + scale2;
  result_prec = prec1 + prec2;

  if (result_prec >= DB_MAX_NUMERIC_PRECISION)
    {
      ret = floating_point_numeric_mul (dbv1, dbv2, &result_prec, &result_scale, answer);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      *is_fp_numeric_op = true;
    }
  else
    {
      /* Perform the multiplication */
      numeric_mul (db_locate_numeric (dbv1), db_locate_numeric (dbv2), &positive_ans, temp);
      /* Check for overflow.  Reset precision & scale if necessary */
      prec = DB_VALUE_PRECISION (dbv1) + DB_VALUE_PRECISION (dbv2) + 1;
      scale = DB_VALUE_SCALE (dbv1) + DB_VALUE_SCALE (dbv2);
      ret = numeric_get_msb_for_dec (prec, scale, temp, &prec, &scale, result);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      /* If no error, make the answer */
      if (!positive_ans)
	{
	  numeric_negate (result);
	}
      db_make_numeric (answer, result, prec, scale);
    }

  return ret;

exit_on_error:

  //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

static int
floating_point_numeric_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec, int *result_scale,
			    DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int calc_bytes, calc_prec;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE] = { 0 };

  /* 1) 지수 정렬 계산 */
  int scale1 = DB_VALUE_SCALE (dbv1);
  int scale2 = DB_VALUE_SCALE (dbv2);
  int exponent1 = *result_scale - scale1;
  int exponent2 = *result_scale - scale2;

  // 2) 부호 확인 및 음수 -> 양수 변환
  unsigned char *dbv1_copy = (unsigned char *) db_locate_numeric (dbv1);
  unsigned char *dbv2_copy = (unsigned char *) db_locate_numeric (dbv2);
  bool arg1_sign = false, arg2_sign = false, result_sign = false;

  // 0 * 0 = 0, 0 * 12 = 0, 12 * 0 = 0 이런 경우 바로 0 리턴
  if (numeric_is_zero (dbv1_copy) || numeric_is_zero (dbv2_copy))
    {
      *result_prec = 1;
      *result_scale = 0;
      db_make_numeric (answer, result_buf, *result_prec, *result_scale);
      return NO_ERROR;
    }

  arg1_sign = numeric_is_negative (db_locate_numeric (dbv1)) ? true : false;
  if (arg1_sign)
    {
      numeric_negate (dbv1_copy);
    }

  arg2_sign = numeric_is_negative (db_locate_numeric (dbv2)) ? true : false;
  if (arg2_sign)
    {
      numeric_negate (dbv2_copy);
    }
  result_sign = arg1_sign ^ arg2_sign;

  // 3) 필요한 바이트 수 계산
  calc_bytes = calc_bytes_from_prec (*result_prec) + 1;
  if (calc_bytes < 1)
    {
      return ER_FAILED;
    }

  // 4) 새로운 계산 버퍼 초기화
  uint8_t dbv1_buf[calc_bytes];
  uint8_t dbv2_buf[calc_bytes];
  uint8_t calc_buf[calc_bytes] = { 0 };

  (void) fp_numeric_pad (dbv1_copy, dbv1_buf, calc_bytes);
  (void) fp_numeric_pad (dbv2_copy, dbv2_buf, calc_bytes);

  // 5) scale 보정
  if (exponent1)
    {
      fp_numeric_mul_pow10 (dbv1_buf, calc_bytes, exponent1);
    }
  if (exponent2)
    {
      fp_numeric_mul_pow10 (dbv2_buf, calc_bytes, exponent2);
    }

//   fprintf(stderr, "[DEBUG] exponent1 : ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", dbv1_buf[i]);
//   fprintf(stderr, "\n");

//   fprintf(stderr, "[DEBUG] exponent2 : ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", dbv2_buf[i]);
//   fprintf(stderr, "\n");

  // 6) 곱셈
  fp_numeric_mul (dbv1_buf, dbv2_buf, calc_buf, calc_bytes);

//   fprintf(stderr, "[DEBUG] mul result: ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", calc_buf[i]);
//   fprintf(stderr, "\n");

//   fprintf(stderr, "[DEBUG] result_prec=%d\n", *result_prec);
//   fprintf(stderr, "[DEBUG] result_scale=%d\n", *result_scale);
//   fprintf(stderr, "\n");

  // 7) 곱셈 이후 prec 재계산
  calc_prec = fp_numeric_overflow2 (calc_buf, calc_bytes);
  if (calc_prec > *result_prec)
    {
      (*result_prec) = calc_prec;
    }

//   fprintf(stderr, "[DEBUG] calc_prec=%d\n", calc_prec);
//   fprintf(stderr, "\n");

  // 8) 반올림 & 다시 16바이트로 pack
  fp_numeric_round_and_pack (calc_buf, calc_bytes, result_buf, result_prec, result_scale);

//   fprintf(stderr, "[DEBUG] final result: ");
//   for (int i = 0; i < 16; i++)
//     fprintf(stderr, "%02X", result_buf[i]);
//   fprintf(stderr, "\n");

  // 9) 결과 저장
  if (result_sign)
    {
      // 결과가 음수인 경우
      numeric_negate ((unsigned char *) result_buf);
    }
  db_make_numeric (answer, result_buf, *result_prec, *result_scale);

  return ret;
}

static void
fp_numeric_mul (const uint8_t * dbv1_buf, const uint8_t * dbv2_buf, uint8_t * calc_buf, int calc_bytes)
{
  int outer_idx = 0, inner_idx = 0, result_idx = 0, carry_idx = 0;
  uint16_t carry = 0;
  uint32_t product = 0, sum = 0, carry_sum = 0;

  for (outer_idx = calc_bytes - 1; outer_idx >= 0; outer_idx--)
    {
      carry = 0;
      for (inner_idx = calc_bytes - 1; inner_idx >= 0; inner_idx--)
	{
	  // 각 자릿수의 위치를 계산
	  result_idx = outer_idx + inner_idx - (calc_bytes - 1);
	  if (result_idx < 0)
	    {
	      /*
	       * 이 곱셈 항(outer_idx + inner_idx)이 만들어내는 비트 위치가
	       * outer_idx + inner_idx < (calc_bytes-1) 이므로 버퍼[0]보다 더 “왼쪽”(더 높은 자리)에 해당됨.
	       * calc_bytes가 담기로 예약한 MSB(Most Significant Bit) 범위를 벗어난 overflow 비트이므로
	       * 결과에 영향을 주지 않고 버려도 안전하다.
	       */
	      continue;
	    }
	  // 곱셈 + 누적 합계 계산
	  product = (uint32_t) dbv1_buf[outer_idx] * dbv2_buf[inner_idx];
	  sum = (uint32_t) calc_buf[result_idx] + product + carry;
	  // 결과 저장 + carry 계산
	  calc_buf[result_idx] = (uint8_t) (sum & 0xFF);
	  carry = (uint16_t) (sum >> 8);
	}
      // carry 위치 계산
      carry_idx = outer_idx - calc_bytes;
      if (carry_idx >= 0)
	{
	  // carry 전파 연산
	  carry_sum = (uint32_t) calc_buf[carry_idx] + carry;
	  calc_buf[carry_idx] = (uint8_t) (carry_sum & 0xFF);
	}
    }
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
numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer, bool * is_fp_numeric_op)
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
  int result_scale;
  int prec1, prec2;
  int calc_prec1;
  int result_prec;

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
      //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);
      return NO_ERROR;
    }

  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);
  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);

  calc_prec1 = prec1 - scale1;
  result_scale = MAX (scale1, scale2);
  result_prec = calc_prec1 + (scale2 == 0 ? scale1 : scale2) + result_scale;

  if (result_prec >= DB_MAX_NUMERIC_PRECISION)
    {
      ret = floating_point_numeric_div (dbv1, dbv2, &result_prec, &result_scale, answer);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}
      *is_fp_numeric_op = true;
    }
  else
    {
      /* In order to maintain the proper number of scaling in the output, find the maximum scale of the two args and make
       * sure that the scale of dbv1 exceeds the scale of dbv2 by that amount. */
      numeric_shortnum_to_longnum (long_dbv1_copy, db_locate_numeric (dbv1));
      scale1 = DB_VALUE_SCALE (dbv1);
      scale2 = DB_VALUE_SCALE (dbv2);
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
      prec = DB_VALUE_PRECISION (dbv1) + scaleup;
      scale = max_scale;
      if (prec > DB_MAX_NUMERIC_PRECISION)
	{
	  prec = DB_MAX_NUMERIC_PRECISION;
	}

      if (!prm_get_bool_value (PRM_ID_COMPAT_NUMERIC_DIVISION_SCALE) && scale < DB_DEFAULT_NUMERIC_DIVISION_SCALE)
	{
	  int new_scale, new_prec;
	  int scale_delta;
	  scale_delta = DB_DEFAULT_NUMERIC_DIVISION_SCALE - scale;
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
	  numeric_div (dbv1_copy, db_locate_numeric (dbv2), temp_quo, temp_rem);
	}

      /* round! Check if remainder is larger than or equal to 2*divisor. i.e. rem / divisor >= 0.5 */

      /* first convert to positive number Note that reminder and dbv2 must be numeric, so we don't consider long numeric. */
      if (numeric_is_negative (temp_rem))
	{
	  numeric_negate (temp_rem);
	}

      if (numeric_is_negative (db_locate_numeric (dbv2)))
	{
	  numeric_copy (dbv2_copy, db_locate_numeric (dbv2));
	  numeric_negate (dbv2_copy);
	  divisor_p = dbv2_copy;
	}
      else
	{
	  divisor_p = db_locate_numeric (dbv2);
	}

      numeric_add (temp_rem, temp_rem, temp_rem, DB_NUMERIC_BUF_SIZE);
      if (numeric_compare (temp_rem, divisor_p) >= 0)
	{
	  if (numeric_is_negative (temp_quo))
	    {
	      /* for negative number */
	      numeric_decrease (temp_quo);
	    }
	  else
	    {
	      numeric_increase (temp_quo);
	    }
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
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		      pr_type_name (TP_DOMAIN_TYPE (domain)));
	      ret = ER_IT_DATA_OVERFLOW;
	      goto exit_on_error;
	    }
	}

      db_make_numeric (answer, temp_quo, prec, scale);
    }

  return ret;

exit_on_error:

  //db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, 0);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

static int
floating_point_numeric_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, int *result_prec, int *result_scale,
			    DB_VALUE * answer)
{
  int ret = NO_ERROR;
  int calc_bytes, calc_prec;
  int cmp;
  uint8_t result_buf[DB_NUMERIC_BUF_SIZE] = { 0 };

  /* 1) 지수 정렬 계산 */
  int scale1 = DB_VALUE_SCALE (dbv1);
  int scale2 = DB_VALUE_SCALE (dbv2);
  int exponent1 = *result_scale - scale1;
  int exponent2 = *result_scale - scale2;

  // 2) 부호 확인 및 음수 -> 양수 변환
  unsigned char *dbv1_copy = (unsigned char *) db_locate_numeric (dbv1);
  unsigned char *dbv2_copy = (unsigned char *) db_locate_numeric (dbv2);
  bool arg1_sign = false, arg2_sign = false, result_sign = false;

  // 향후 numeric_is_zero 추가해서  0 / 12 이런 얘들 찾아서 바로 skip 하게 추가 필요!!
  // 12 / 0 = err 로 이미 파싱 부분에서 처리됨, 따라서 따로 확인 안함.
  // 0 / 12 = 0
  if (numeric_is_zero (dbv1_copy))
    {
      *result_prec = 1;
      *result_scale = 0;
      db_make_numeric (answer, result_buf, *result_prec, *result_scale);
      return NO_ERROR;
    }

  arg1_sign = numeric_is_negative (db_locate_numeric (dbv1)) ? true : false;
  if (arg1_sign)
    {
      numeric_negate (dbv1_copy);
    }

  arg2_sign = numeric_is_negative (db_locate_numeric (dbv2)) ? true : false;
  if (arg2_sign)
    {
      numeric_negate (dbv2_copy);
    }
  result_sign = arg1_sign ^ arg2_sign;

  // 3) 필요한 바이트 수 계산
  calc_bytes = calc_bytes_from_prec (*result_prec) * 2;
  if (calc_bytes <= (int) DB_NUMERIC_BUF_SIZE)
    {
      // 16바이트 이하인 경우, 38자리 까지 보여주기 위해 16바이트로 늘림.
      calc_bytes = (int) DB_NUMERIC_BUF_SIZE *2;
    }

  // 4) 새로운 계산 버퍼 초기화
  uint8_t dbv1_buf[calc_bytes];
  uint8_t dbv2_buf[calc_bytes];
  uint8_t calc_buf[calc_bytes] = { 0 };
  uint8_t quo_buf[calc_bytes] = { 0 };
  uint8_t rem_buf[calc_bytes] = { 0 };

  (void) fp_numeric_pad (dbv1_copy, dbv1_buf, calc_bytes);
  (void) fp_numeric_pad (dbv2_copy, dbv2_buf, calc_bytes);

  // 5) scale 보정
  if (exponent1)
    {
      fp_numeric_mul_pow10 (dbv1_buf, calc_bytes, exponent1);
    }
  if (exponent2)
    {
      fp_numeric_mul_pow10 (dbv2_buf, calc_bytes, exponent2);
    }

//   fprintf(stderr, "[DEBUG] exponent1 : ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", dbv1_buf[i]);
//   fprintf(stderr, "[DEBUG] exponent1=%d\n", exponent1);
//   fprintf(stderr, "\n");

//   fprintf(stderr, "[DEBUG] exponent2 : ");
//   for (int i = 0; i < calc_bytes; i++)
//     fprintf(stderr, "%02X", dbv2_buf[i]);
//   fprintf(stderr, "[DEBUG] exponent2=%d\n", exponent2);
//   fprintf(stderr, "\n");

  // scale 보정 전에 같은지 확인 불가능 1.2 와 12는 같은 12로 저장하여 보정 후 확인하는게 맞음
  // 12 / 12 = 1, 12 / -12 = -1, -12 / 12 = -1, -12 / -12 = 1
  cmp = fp_numeric_cmp_base256 (dbv1_buf, dbv2_buf, calc_bytes);
  if (cmp == 0)
    {
      *result_prec = 1;
      *result_scale = 0;
      result_buf[DB_NUMERIC_BUF_SIZE - 1] = 1;

      if (result_sign)
	{
	  // 결과가 음수인 경우
	  numeric_negate ((unsigned char *) result_buf);
	}
      db_make_numeric (answer, result_buf, *result_prec, *result_scale);
      return NO_ERROR;
    }

  // 6) 나눗셈
//   {
//     // Knuth 장 나눗셈 구현
//     fp_numeric_div (dbv1_buf, dbv2_buf, quo_buf, rem_buf, calc_bytes);
//   }
  {
    // 기존 numeric_long_div 나눗셈 구현
    fp_numeric_div2 (dbv1_buf, dbv2_buf, quo_buf, rem_buf, calc_bytes);
  }

  // 7) 나눗셈 이후 prec 재계산
  // calc_buf는 0으로 초기화 한 상태로 0인지 확인 가능
  cmp = fp_numeric_cmp_base256 (quo_buf, calc_buf, calc_bytes);
  if (cmp != 0)
    {
      // 7-1-a) 몫이 0이 아닌 경우, 몫의 자릿수 계산
      calc_prec = fp_numeric_overflow2 (quo_buf, calc_bytes);
      if (calc_prec >= DB_MAX_NUMERIC_PRECISION + 1)
	{
	  // 7-1-b) 몫의 자릿수가 39 이상인 경우, 소수 자리는 없음
	  *result_prec = calc_prec;
	  *result_scale = 0;
	  memcpy (calc_buf, quo_buf, calc_bytes);
	}
      else
	{
	  // 7-1-c) 몫의 자릿수가 38 이하인 경우, 소수 자리 있음
	  int scal_digits = DB_MAX_NUMERIC_PRECISION - calc_prec;
	  int round_digits = 0;

	  *result_prec = DB_MAX_NUMERIC_PRECISION;
	  *result_scale = scal_digits;

	  uint8_t decimal_quo_buf[calc_bytes] = { 0 };
	  // 아래 배열은 기존 long_div 에서만 필요함
	  uint8_t decimal_rem_buf[calc_bytes] = { 0 };

	  // A) 몫(정수부) scale 보정
	  fp_numeric_mul_pow10 (quo_buf, calc_bytes, scal_digits);

	  // B) 나머지(소수부) scale 보정, 39자리 확인을 위해 1 더함
	  fp_numeric_mul_pow10 (rem_buf, calc_bytes, scal_digits + 1);

	  // C) 나머지(소수부) / 제수 연산, 이제 나머지(소수부)의 몫만 필요
	  // Knuth 장 나눗셈 구현
	  //fp_numeric_div (rem_buf, dbv2_buf, decimal_quo_buf, rem_buf, calc_bytes);
	  // 기존 long_div 나눗셈 구현 (얘는 임시 저장인 decimal_rem_buf 가 필요함)
	  fp_numeric_div2 (rem_buf, dbv2_buf, decimal_quo_buf, decimal_rem_buf, calc_bytes);

	  // D) 나머지(소수부)의 몫에서 10을 나눠 반올림 확인
	  round_digits = fp_numeric_div_pow10 (decimal_quo_buf, calc_bytes);

	  // E) half-up 반올림: rem10 >= 5 이면 +1
	  if (round_digits >= 5)
	    {
	      (void) fp_numeric_increment (decimal_quo_buf, calc_bytes, 1);
	    }

	  // F) 몫(정수부) + 나머지(소수부)의 몫
	  fp_numeric_add (quo_buf, decimal_quo_buf, calc_buf, calc_bytes);
	}
    }
  else
    {
      // 7-2-a) 나머지의 자릿수 계산, 몫이 0인 경우
      int divisor_digits = 0;
      int decimal_quo_digits = 0;
      uint8_t decimal_quo_buf[calc_bytes] = { 0 };
      uint8_t decimal_rem_buf[calc_bytes] = { 0 };

      // A) 제수의 자릿수 확인 -- 나머지 scale 보정을 위해 사용    
      divisor_digits = fp_numeric_overflow2 (dbv2_buf, calc_bytes);
      if (divisor_digits < DB_MAX_NUMERIC_PRECISION)
	{
	  divisor_digits = DB_MAX_NUMERIC_PRECISION;
	}

      // B) 나머지(소수부) scale 보정
      fp_numeric_mul_pow10 (rem_buf, calc_bytes, divisor_digits);

      // C) 나머지(소수부) / 제수 연산, 이제 나머지(소수부)의 몫만 필요
      // Knuth 장 나눗셈 구현
      //fp_numeric_div (rem_buf, dbv2_buf, decimal_quo_buf, rem_buf, calc_bytes);
      // 기존 long_div 나눗셈 구현 (얘는 임시 저장인 decimal_rem_buf 가 필요함)
      fp_numeric_div2 (rem_buf, dbv2_buf, decimal_quo_buf, decimal_rem_buf, calc_bytes);

      // D) 나머지의 몫 자릿수 확인
      decimal_quo_digits = fp_numeric_overflow2 (decimal_quo_buf, calc_bytes);

      // E) 나머지의 몫 자릿수가 39 이상이면 반올림 처리
      if (decimal_quo_digits >= DB_MAX_NUMERIC_PRECISION + 1)
	{
	  int round_digits = 0;
	  round_digits = fp_numeric_div_pow10 (decimal_quo_buf, calc_bytes);
	  if (round_digits >= 5)
	    {
	      (void) fp_numeric_increment (decimal_quo_buf, calc_bytes, 1);
	    }
	}

      //몫에 얼만큼 늘렸는지를 확인해서 이걸 scale 로  처리하는거야!
      // 100 / 9990 = 0 // 실제 1자리
      // 100 * 10 / 9990 = 1 // 실제 2자리, 10을 곱해줬으니 2자리
      // 100 * 100 / 9990 = 10 // 실제 3자리, 100을 곱해줬으니 3자리

      *result_prec = decimal_quo_digits;
      *result_scale = divisor_digits;

      memcpy (calc_buf, decimal_quo_buf, calc_bytes);
    }

//   fprintf(stderr, "[DEBUG] calc_prec=%d\n", calc_prec);
//   fprintf(stderr, "\n");

  // 8) 반올림 & 다시 16바이트로 pack
  fp_numeric_round_and_pack (calc_buf, calc_bytes, result_buf, result_prec, result_scale);

  // 9) 나머지 자릿수 계산을 해야하는데, trailing zero 를 제거해야함.
  if (*result_scale > 0)
    {
      (void) fp_numeric_trim_trailing_zeros (result_buf, result_prec, result_scale);
    }

  // 10) 결과 저장
  if (result_sign)
    {
      // 결과가 음수인 경우
      numeric_negate ((unsigned char *) result_buf);
    }
  db_make_numeric (answer, result_buf, *result_prec, *result_scale);

  return ret;
}

/*
 * knuth 나눗셈 알고리즘
 * 핵심 : 이 알고리즘은 추정 → 검증 → 보정의 반복 과정
 * 1. 유효 바이트 길이를 구해 워킹 버퍼 준비
 *    - shift를 과도하게 이동을 방지하기 위해
 *    - leading zeros 제거
 * 2. 제수의 최상위 비트가 진법의 1/2이 되도록 shift (정규화)
 *    - 몫 추정 단계에서 오차를 1 이하로 낮추기 위해
 *    - 예: 256진법에서는 최상위 비트가 1(128)이 되도록
 * 3. 몫의 첫 번째, 두 번째 자리 추정 (추정)
 *    - 첫 번째: (dividend_high * 256 + dividend_mid) / divisor_high
 *    - 두 번째: estimated_quotient * divisor_low > (estimated_remainder * 256 + dividend_low) 검증
 * 4. 추정된 몫과 제수를 곱해서 피제수에서 뺌 (검증)
 *    - 추정된 몫이 정확한지 검증하는 과정
 *    - 뺄셈 결과가 음수가 되면 추정이 잘못된 것
 * 5. 추정된 몫의 최상위 바이트를 뺄셈으로 처리 (보정)
 *    - 4번에서 처리하지 못한 최상위 바이트 처리
 *    - 곱셈의 캐리와 뺄셈의 보로우를 모두 고려
 * 6. borrow(빌림)이 발생한 경우 제수를 다시 더해서 보정 (보정)
 *    - 뺄셈 결과가 음수면 추정된 몫이 너무 큼
 *    - 몫을 1 감소시키고 제수를 다시 더함
 * 7. 계산된 몫 저장
 *    - 검증된 몫을 결과에 저장
 * 8. 정규화 할 때 shift 한 만큼 다시 복원
 *    - 원래 크기로 복원
 * 9. 나머지 저장
 *    - 최종 나머지를 결과에 저장
 *
 * dbv1_buf : 피제수(dividend) = 나누어지는 수
 * dbv2_buf : 제수(divisor) = 나누는 수
 * calc_buf : 결과 몫
 * calc_bytes : 버퍼 크기
 */
static void
fp_numeric_div (uint8_t * dbv1_buf, uint8_t * dbv2_buf, uint8_t * quo_buf, uint8_t * rem_buf, int calc_bytes)
{
  int d = 0;
  int nz1 = 0, nz2 = 0;

  // 1) 유효(non-zero) 바이트 길이 구하기
  // 정규화 할 때, shift를 너무 많이하는 경우 의도한 값과 달라짐.
  while (nz1 < calc_bytes && dbv1_buf[nz1] == 0)
    {
      nz1++;
    }

  while (nz2 < calc_bytes && dbv2_buf[nz2] == 0)
    {
      nz2++;
    }
  int dividend_bytes = calc_bytes - nz1;	// dividend 유효 길이
  int divisor_bytes = calc_bytes - nz2;	// divisor 유효 길이

  // 2) 워킹 버퍼 준비 (right-justify)
  uint8_t rem_work[dividend_bytes + 1] = { 0 };
  uint8_t div_work[divisor_bytes] = { 0 };
//   memset(rem_work, 0, dividend_bytes+1);
//   memset(div_work, 0, divisor_bytes);
  memcpy (rem_work + 1, dbv1_buf + nz1, dividend_bytes);
  memcpy (div_work, dbv2_buf + nz2, divisor_bytes);

//   printf("START rem_work: ");
//   for(int k=0;k<dividend_bytes+1;k++)
//     printf("%02X", rem_work[k]);
//   printf("\n");
//   printf("START div_work: ");
//   for(int k=0;k<divisor_bytes;k++)
//     printf("%02X", div_work[k]);
//   printf("\n");

  // 3) Normalize: Knuth D1 -- div_work[0]의 MSB가 1이 될 때까지 shift
  /* Knuth 장 나눗셈을 정확히 그리고 효율적으로 구현하기 위해서는 “제수의 최상위 비트가 1이 되도록” 하는 normalize(정규화) 과정이 필수
   * 1. 10진법 예시, 12345 / 234 = 52.756410...
   *  정규화 전
   *    - B = 10 (10진법)
   *    - u0 = 12 (12345 앞 두 자리)
   *    - v0 = 2 (234 첫 자리)
   *    - q = floor((u0*B + u1) / v0))
   *        = floor((12*10 + 3) / 2) = 61
   *    - 실제 첫 번째 몫의 값은 : 5
   *    - 61 - 5 = 56 만큼의 오차가 발생함
   *  정규화 후
   *    - B = 10 (10진법)
   *    - u0 = 12 (123450 앞 두 자리)
   *    - v0 = 23 (2340 앞 두 자리)
   *    - q = floor((u0*B + u1) / v0))
   *        = floor((12*10 + 3) / 23) = 5
   *    - 실제 첫 번째 몫의 값은 : 5
   *    - 오차가 발생하지 않음
   * 2. 256 진법 예시, 12345 / 234 = 146.964285...
   *  정규화 전
   *    - B = 256 (256진법)
   *    - u0 = 48 ( 48, 57 첫 바이트)
   *    - v0 = 84 ( 84 첫 바이트)
   *    - q = floor((u0*B + u1) / v0))
   *        = floor((48*256 + 57) / 84) = 146
   */
  // 제수가 최상위 바이트 ≥ B/2 가 되도록 shift (여기선 B=256이므로, dbv2_buf[0] != 0만 보장하면 스킵 가능)
  while ((div_work[0] & 0x80) == 0)
    {
      fp_numeric_shift_left (div_work, divisor_bytes);
      fp_numeric_shift_left (rem_work, dividend_bytes + 1);
      d++;
    }

//   printf("AFTER D1 normalize (d=%d)\n", d);
//   printf("  rem_work: ");
//   for(int k=0;k<dividend_bytes+1;k++)
//     printf("%02X", rem_work[k]);
//   printf("\n");
//   printf("  div_work: ");
//   for(int k=0;k<divisor_bytes;k++)
//     printf("%02X", div_work[k]);
//   printf("\n");

  int m = dividend_bytes - divisor_bytes;
  int Qlen = m + 1;		// 실제 몫 바이트 수
  int outQ = calc_bytes - Qlen;	// calc_buf[outQ]부터 채워야 함

  // 4) Main loop: Knuth D2–D7
  for (int j = 0; j <= m; j++)
    {
//       printf("j=%d: window U=%02X%02X%02X  V=%02X%02X\n", j, rem_work[j], rem_work[j+1],
//              (j+2 < dividend_bytes+1 ? rem_work[j+2]:0), div_work[0], (divisor_bytes>1?div_work[1]:0));
      // D2–D3: Estimate & correction
      uint32_t u0 = rem_work[j];
      uint32_t u1 = rem_work[j + 1];
      uint32_t u2 = (j + 2 < dividend_bytes + 1 ? rem_work[j + 2] : 0);
      uint32_t v0 = div_work[0];
      uint32_t v1 = (divisor_bytes > 1 ? div_work[1] : 0);

      // D2
      uint32_t qhat = ((u0 << 8) | u1) / v0;
      if (qhat > 0xFF)
	{
	  qhat = 0xFF;
	}
      uint32_t rhat = ((u0 << 8) | u1) - qhat * v0;

      // D3: second-digit correction
      while (qhat * v1 > (rhat << 8) + u2)
	{
	  qhat--;
	  rhat += v0;
	}

//       printf("  qhat0=%u, rhat0=%u\n", qhat, rhat);
//       printf("  after D3 correction qhat=%u, rhat=%u\n", qhat, rhat);

      // D4–D7: Multiply & subtract, borrow check, add-back if needed
      uint32_t borrow = 0, carry_v = 0;
      for (int i = divisor_bytes - 1; i >= 0; i--)
	{
	  uint32_t prod = qhat * div_work[i] + carry_v;
	  carry_v = prod >> 8;
	  uint32_t sub = (uint32_t) rem_work[j + i + 1] - (prod & 0xFF) - borrow;
	  borrow = (sub >> 31) & 1;
	  rem_work[j + i + 1] = (uint8_t) sub;
	}

//       printf("  after subtract U: ");
//       for(int k=0;k<=divisor_bytes;k++)
//         printf("%02X", rem_work[j+k]);
//       printf("\n");

      {
	uint32_t sub = (uint32_t) rem_work[j] - carry_v - borrow;
	borrow = (sub >> 31) & 1;
	rem_work[j] = (uint8_t) sub;
      }

      // D5: If we still have borrow, do add-back (including that top byte)
      if (borrow)
	{
	  //   printf("  add-back carry, U after add-back: ");
	  //   for(int k=0;k<=divisor_bytes;k++)
	  //     printf("%02X", rem_work[j+k]);
	  //   printf("\n");

	  qhat--;
	  uint32_t carry = 0;
	  // add divisor back into U[j..j+n2-1]
	  for (int i = divisor_bytes - 1; i >= 0; i--)
	    {
	      uint32_t sum = (uint32_t) rem_work[j + i] + div_work[i] + carry;
	      rem_work[j + i] = (uint8_t) sum;
	      carry = sum >> 8;
	    }
	  // **그리고 이 carry도 U[j+n2] 에 더해 줍니다**
	  rem_work[j + divisor_bytes] = (uint8_t) (rem_work[j + divisor_bytes] + carry);
	}

      quo_buf[outQ + j] = (uint8_t) qhat;


//       printf("  after full subtract U: ");
//       for (int k = 0; k <= divisor_bytes; k++)
//         printf("%02X", rem_work[j + k]);
//       printf("\n");
    }

  // 5) Denormalize remainder: reverse shift
  // 이제 dbv1_buf[0..n-1]에 나머지, calc_buf[0..n-1]에 몫이 들어 있음
  while (d-- > 0)
    {
      fp_numeric_shift_right (rem_work, dividend_bytes + 1);
    }

//   printf("AFTER D8 final U: ");
//   for (int k = 0; k < dividend_bytes+1; k++)
//     printf("%02X", rem_work[k]);
//   printf("\n");

  // 6) 결과 반영
  int outR = calc_bytes - divisor_bytes;	// 나머지도 right-justify
  for (int i = 0; i < divisor_bytes; i++)
    {
      rem_buf[outR + i] = rem_work[Qlen + i];
    }

//   printf("FINAL quotient buf: ");
//   for(int i=0;i<calc_bytes;i++)
//     printf("%02X", quo_buf[i]);
//   printf("\n");
//   printf("FINAL remainder buf: ");
//   for(int i=0;i<calc_bytes;i++)
//     printf("%02X", rem_buf[i]);
//   printf("\n");
}

static void
fp_numeric_shift_left (uint8_t * buf, int calc_bytes)
{
  int i;
  uint8_t carry = 0, next_carry = 0;
  for (i = calc_bytes; i-- > 0;)
    {
      // 현재 바이트에서 최상위 비트가 유출되는지 검사
      next_carry = (buf[i] & 0x80) ? 1 : 0;
      // 왼쪽으로 1비트 시프트, 이전 단계의 캐리(하위 바이트로부터 올라옴)를 OR
      buf[i] = (uint8_t) ((buf[i] << 1) | carry);
      carry = next_carry;
    }
}

static void
fp_numeric_shift_right (uint8_t * buf, int calc_bytes)
{
  int i;
  uint8_t carry = 0, next_carry = 0;
  for (i = 0; i < calc_bytes; i++)
    {
      // 현재 바이트에서 최하위 비트가 유출되는지 검사
      next_carry = (buf[i] & 0x01) ? 1 : 0;
      // 오른쪽으로 1비트 시프트, 이전 단계의 캐리(상위 바이트로부터 내려옴)를 MSB 위치에 OR
      buf[i] = (uint8_t) ((buf[i] >> 1) | (carry << 7));
      carry = next_carry;
    }
}

static void
fp_numeric_div2 (uint8_t * dbv1_buf, uint8_t * dbv2_buf, uint8_t * quo_buf, uint8_t * rem_buf, int calc_bytes)
{
  int nbit, total_bit;

  /* calculate basic variables */
  total_bit = calc_bytes * 8;

  /* Initialize variables */
  memcpy (quo_buf, dbv1_buf, calc_bytes);

  /* Shift *answer and *remainder.  Bits shifted out of *answer * are placed into *remainder.  */
  /*****  NEEDS TO BE UPGRADED TO SHIFT SO THAT FIRST NON-ZERO BIT OF *****/
  /*****  REMAINDER IS AT LEAST EQUAL TO FIRST NON_ZERO BIT OF ARG2.  *****/
  /*****  DON'T DO THIS ONE BIT AT A TIME.                            *****/
  for (nbit = 0; nbit < total_bit; nbit++)
    {
      fp_numeric_double_shift_bit (quo_buf, rem_buf, calc_bytes, quo_buf, rem_buf);

      /* If remainder >= arg2, subtract arg2 from remainder and increment the answer.  */
      if (fp_numeric_cmp_base256 (rem_buf, dbv2_buf, calc_bytes) >= 0)
	{
	  fp_numeric_sub (rem_buf, dbv2_buf, rem_buf, calc_bytes);
	  quo_buf[calc_bytes - 1] += 1;
	}
    }
}

static void
fp_numeric_double_shift_bit (uint8_t * arg1, uint8_t * arg2, int calc_bytes, uint8_t * lsb, uint8_t * msb)
{
  int digit;
  int numbits = 1;
  uint8_t local_arg1[calc_bytes];
  uint8_t local_arg2[calc_bytes];

  /* Copy args into local variables */
  memcpy (local_arg1, arg1, calc_bytes);
  memcpy (local_arg2, arg2, calc_bytes);

  /* Loop through all but last word of msb shifting bits */
  for (digit = 0; digit < calc_bytes - 1; digit++)
    {
      msb[digit] = (local_arg2[digit] << numbits) | (local_arg2[digit + 1] >> (8 - numbits));
    }

  /* Do last word of msb separately using upper word of lsb */
  msb[calc_bytes - 1] = (local_arg2[calc_bytes - 1] << numbits) | (local_arg1[0] >> (8 - numbits));

  /* Loop through all but last word of lsb shifting bits */
  for (digit = 0; digit < calc_bytes - 1; digit++)
    {
      lsb[digit] = (local_arg1[digit] << numbits) | (local_arg1[digit + 1] >> (8 - numbits));
    }

  /* Do last word of lsb separately.  */
  lsb[calc_bytes - 1] = local_arg1[calc_bytes - 1] << numbits;
}

static void
fp_numeric_trim_trailing_zeros (uint8_t * result_buf, int *result_prec, int *result_scale)
{
  int i;
  int last_digit = 0;
  int decimal_digits = 0;
  uint8_t tmp_buf[DB_NUMERIC_BUF_SIZE];

  memcpy (tmp_buf, result_buf, DB_NUMERIC_BUF_SIZE);
  decimal_digits = fp_numeric_overflow2 (tmp_buf, DB_NUMERIC_BUF_SIZE);

  for (i = 0; i < decimal_digits; i++)
    {
      last_digit = fp_numeric_div_pow10 (tmp_buf, DB_NUMERIC_BUF_SIZE);
      if (last_digit == 0)
	{
	  (*result_prec)--;
	  (*result_scale)--;
	  memcpy (result_buf, tmp_buf, DB_NUMERIC_BUF_SIZE);
	  continue;
	}
      break;
    }
}

/*
 * numeric_db_value_negate () -
 *   return: NO_ERROR, or ER_code
             The argument answer is modified in place.
 *     Errors:
 *       ER_OBJ_INVALID_ARGUMENTS - answer is not DB_TYPE_NUMERIC
 *   answer(in/out) : ptr to a DB_VALUE of type DB_TYPE_NUMERIC
 *
 * Note: This routine returns the negative (2's complement) of arg in answer.
 */
int
numeric_db_value_negate (DB_VALUE * answer)
{
  /* Check for NULL value */
  if (DB_IS_NULL (answer))
    {
      return NO_ERROR;
    }

  /* Check for bad inputs */
  if (answer == NULL || DB_VALUE_TYPE (answer) != DB_TYPE_NUMERIC)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  /* Perform the negation */
  numeric_negate (db_locate_numeric (answer));

  return NO_ERROR;
}

/*
 * numeric_db_value_abs () -
 *   return:
 *   src_num(in)        :
 *   dest_num(in)       :
 */
void
numeric_db_value_abs (DB_C_NUMERIC src_num, DB_C_NUMERIC dest_num)
{
  numeric_copy (dest_num, src_num);
  if (numeric_is_negative (src_num))
    {
      numeric_negate (dest_num);
    }
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

  ret = numeric_is_negative ((DB_C_NUMERIC) db_locate_numeric (dbvalue));

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
  int prec_common = 0, scale_common = 0;
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

  scale1 = DB_VALUE_SCALE (dbv1);
  scale2 = DB_VALUE_SCALE (dbv2);
  prec1 = DB_VALUE_PRECISION (dbv1);
  prec2 = DB_VALUE_PRECISION (dbv2);

  if (prec1 == prec2 && scale1 == scale2)
    {
      /* Simple case. Just compare two numbers. */
      cmp_rez = numeric_compare (db_locate_numeric (dbv1), db_locate_numeric (dbv2));
      db_make_int (answer, cmp_rez);
      return NO_ERROR;
    }
  else
    {
      DB_VALUE dbv1_common, dbv2_common;

      /* First try to coerce to common prec/scale numbers and compare. */
      ret = numeric_common_prec_scale (dbv1, dbv2, &dbv1_common, &dbv2_common);
      if (ret == NO_ERROR)
	{
	  cmp_rez = numeric_compare (db_locate_numeric (&dbv1_common), db_locate_numeric (&dbv2_common));
	  db_make_int (answer, cmp_rez);
	  return NO_ERROR;
	}
      else if (ret == ER_IT_DATA_OVERFLOW)
	{
	  /* For example, if we want to compare a NUMERIC(31,2) with a NUMERIC(21, 14) the common precision and scale
	   * is (43, 14) which is an overflow. To avoid this issue we compare the integral parts and the fractional
	   * parts of dbv1 and dbv2 separately. */
	  unsigned char num1_integ[DB_NUMERIC_BUF_SIZE];
	  unsigned char num2_integ[DB_NUMERIC_BUF_SIZE];
	  unsigned char num1_frac[DB_NUMERIC_BUF_SIZE];
	  unsigned char num2_frac[DB_NUMERIC_BUF_SIZE];

	  er_clear ();		/* reset ER_IT_DATA_OVERFLOW */

	  if (prec1 - scale1 < prec2 - scale2)
	    {
	      prec_common = prec2 - scale2;
	    }
	  else
	    {
	      prec_common = prec1 - scale1;
	    }

	  // [임시] 일단, 음수를 양수로 변환,
	  // 음.. 정수로 변환 하지 않고 0으로 변경해보니 비교 결과가 안나옴..
	  // 버퍼 크기를 늘려주니, 음수도 잘 처리하네..? 일단 다시 주석처리!
	  //   if (scale1 < 0)
	  //     {
	  //       scale1 *= -1;
	  //     }
	  //   else if (scale2 < 0)
	  //     {
	  //       scale2 *= -1;
	  //     }

	  if (scale1 > scale2)
	    {
	      scale_common = scale1;
	    }
	  else
	    {
	      scale_common = scale2;
	    }

	  /* first compare integral parts */
	  numeric_get_integral_part (db_locate_numeric (dbv1), prec1, scale1, prec_common, num1_integ);
	  numeric_get_integral_part (db_locate_numeric (dbv2), prec2, scale2, prec_common, num2_integ);
	  cmp_rez = numeric_compare (num1_integ, num2_integ);
	  if (cmp_rez != 0)
	    {
	      /* if the integral parts differ, we don't need to compare fractional parts */
	      db_make_int (answer, cmp_rez);
	      return NO_ERROR;
	    }

	  /* the integral parts are equal, now compare fractional parts */
	  numeric_get_fractional_part (db_locate_numeric (dbv1), scale1, scale_common, num1_frac);
	  numeric_get_fractional_part (db_locate_numeric (dbv2), scale2, scale_common, num2_frac);

	  /* compare fractional parts and return the result */
	  cmp_rez = numeric_compare (num1_frac, num2_frac);
	  db_make_int (answer, cmp_rez);
	}
      else
	{
	  db_make_null (answer);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * numeric_coerce_int_to_num () -
 *   return:
 *   arg(in)    : unsigned int value
 *   answer(out): DB_C_NUMERIC
 *
 * Note: This routine converts 32 bit integer into DB_C_NUMERIC format and
 * returns the result.
 */
void
numeric_coerce_int_to_num (int arg, DB_C_NUMERIC answer)
{
  unsigned char pad;
  int digit;

  /* Check for negative/positive and set pad accordingly */
  pad = (arg >= 0) ? 0 : 0xff;

  /* Copy the lower 32 bits into answer */
  answer[DB_NUMERIC_BUF_SIZE - 1] = ((arg) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 2] = ((arg >> 8) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 3] = ((arg >> 16) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 4] = ((arg >> 24) & 0xff);

  /* Pad extra bytes of answer accordingly */
  for (digit = DB_NUMERIC_BUF_SIZE - 5; digit >= 0; digit--)
    {
      answer[digit] = pad;
    }
}

/*
 * numeric_coerce_bigint_to_num () -
 *   return:
 *   arg(in)    : unsigned bigint value
 *   answer(out): DB_C_NUMERIC
 *
 * Note: This routine converts 64 bit integer into DB_C_NUMERIC format and
 * returns the result.
 */
void
numeric_coerce_bigint_to_num (DB_BIGINT arg, DB_C_NUMERIC answer)
{
  unsigned char pad;
  int digit;

  /* Check for negative/positive and set pad accordingly */
  pad = (arg >= 0) ? 0 : 0xff;

  /* Copy the lower 64 bits into answer */
  answer[DB_NUMERIC_BUF_SIZE - 1] = ((arg) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 2] = ((arg >> 8) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 3] = ((arg >> 16) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 4] = ((arg >> 24) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 5] = ((arg >> 32) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 6] = ((arg >> 40) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 7] = ((arg >> 48) & 0xff);
  answer[DB_NUMERIC_BUF_SIZE - 8] = ((arg >> 56) & 0xff);

  /* Pad extra bytes of answer accordingly */
  for (digit = DB_NUMERIC_BUF_SIZE - 9; digit >= 0; digit--)
    {
      answer[digit] = pad;
    }
}

/*
 * numeric_coerce_num_to_int () -
 *   return:
 *   arg(in)    : ptr to a DB_C_NUMERIC
 *   answer(out): ptr to an integer
 *
 * Note: This routine converts a numeric into an integer returns the result.
 * If arg overflows answer, answer is set to +/- MAXINT.
 */
void
numeric_coerce_num_to_int (DB_C_NUMERIC arg, int *answer)
{
  int digit;
  unsigned char pad;

  /* Check for negative/positive and overflow */
  pad = (numeric_is_negative (arg)) ? 0xff : 0;
  for (digit = DB_NUMERIC_BUF_SIZE - 5; digit >= 1; digit--)
    {
      if (arg[digit] != pad)
	{
	  if (pad == 0xff)
	    {
	      *answer = ~0;
	    }
	  else
	    {
	      *answer = ~0 >> 1;
	    }
	  return;
	}
    }

  /* Copy the lower 32 bits into answer */
  *answer =
    ((arg[DB_NUMERIC_BUF_SIZE - 1]) + (((unsigned int) (arg[DB_NUMERIC_BUF_SIZE - 2])) << 8) +
     (((unsigned int) (arg[DB_NUMERIC_BUF_SIZE - 3])) << 16) + (((unsigned int) (arg[DB_NUMERIC_BUF_SIZE - 4])) << 24));
}

/*
 * numeric_coerce_num_to_bigint () -
 *   return:
 *   arg(in)    : ptr to a DB_C_NUMERIC
 *   answer(out): ptr to an bigint
 *
 * Note: This routine converts a numeric into an bigint returns the result.
 * If arg overflows answer, answer is set to +/- MAXINT.
 */
int
numeric_coerce_num_to_bigint (DB_C_NUMERIC arg, int scale, DB_BIGINT * answer)
{
  DB_NUMERIC zero_scale_numeric, numeric_rem, numeric_tmp;

  zero_scale_numeric.d.buf[0] = '\0';
  numeric_rem.d.buf[0] = '\0';
  numeric_tmp.d.buf[0] = '\0';

  DB_C_NUMERIC zero_scale_arg = zero_scale_numeric.d.buf;
  DB_C_NUMERIC rem = numeric_rem.d.buf;
  DB_C_NUMERIC tmp = numeric_tmp.d.buf;
  unsigned int i;
  char *ptr;

  if (scale >= (int) (sizeof (powers_of_10) / sizeof (powers_of_10[0])))
    {
      return ER_IT_DATA_OVERFLOW;
    }

  if (scale > 0)
    {
      numeric_div (arg, numeric_get_pow_of_10 (scale), zero_scale_arg, rem);
      if (!numeric_is_negative (zero_scale_arg))
	{
	  numeric_negate (rem);
	}

      /* round */
      numeric_add (numeric_get_pow_of_10 (scale), rem, tmp, DB_NUMERIC_BUF_SIZE);
      numeric_add (tmp, rem, tmp, DB_NUMERIC_BUF_SIZE);
      if (numeric_is_negative (tmp) || numeric_is_zero (tmp))
	{
	  if (numeric_is_negative (zero_scale_arg))
	    {
	      numeric_decrease (zero_scale_arg);
	    }
	  else
	    {
	      numeric_increase (zero_scale_arg);
	    }
	}
    }
  else
    {
      numeric_copy (zero_scale_arg, arg);
    }

  if (!numeric_is_bigint (zero_scale_arg))
    {
      return ER_IT_DATA_OVERFLOW;
    }

  /* Copy the lower 64 bits into answer */
  ptr = (char *) answer;
  for (i = 0; i < sizeof (DB_BIGINT); i++)
    {
#if OR_BYTE_ORDER == OR_LITTLE_ENDIAN
      ptr[i] = zero_scale_arg[DB_NUMERIC_BUF_SIZE - (i + 1)];
#else
      ptr[sizeof (DB_BIGINT) - (i + 1)] = zero_scale_arg[DB_NUMERIC_BUF_SIZE - (i + 1)];
#endif
    }

  return NO_ERROR;
}

/*
 * numeric_coerce_dec_str_to_num () -
 *   return:
 *   dec_str(in): char * holds positive decimal digits as ASCII chars
 *   result(out): ptr to a DB_C_NUMERIC
 *
 * Note: This routine converts a character string that contains the positive
 * decimal digits of a numeric encoded as ASCII characters.
 */
void
numeric_coerce_dec_str_to_num (const char *dec_str, DB_C_NUMERIC result)
{
  unsigned char big_chunk[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */
  int ntot_digits;
  int ndigits;
  int dec_dig;
  int chunk_value;
  char temp_buffer[10];
  char *chunk;
  bool is_negative = false;

  /* Zero out the result */
  numeric_zero (result, DB_NUMERIC_BUF_SIZE);
  /* Check for a negative number. Negative sign must be in the first decimal place */
  if (*dec_str == '-')
    {
      is_negative = true;
      dec_str++;
    }

  /* Loop through string reading 9 decimal digits at a time */
  ntot_digits = strlen ((char *) dec_str);
  chunk = (char *) dec_str + ntot_digits;
  for (dec_dig = ntot_digits - 1; dec_dig >= 0; dec_dig -= 9)
    {
      ndigits = MIN (dec_dig + 1, 9);
      chunk -= ndigits;
      memcpy (temp_buffer, chunk, ndigits);
      temp_buffer[ndigits] = '\0';
      chunk_value = (int) atol (temp_buffer);
      if (chunk_value != 0)
	{
	  numeric_coerce_int_to_num (chunk_value, big_chunk);
	  /* Scale the number if not first time through */
	  if (dec_dig != ntot_digits - 1)
	    {
	      numeric_scale_dec (big_chunk, ntot_digits - dec_dig - 1, big_chunk);
	    }
	  numeric_add (big_chunk, result, result, DB_NUMERIC_BUF_SIZE);
	}
    }

  /* If negative, negate the result */
  if (is_negative)
    {
      numeric_negate (result);
    }
}

/*
 * numeric_coerce_num_to_dec_str () -
 *   return:
 *   num(in)    : DB_C_NUMERIC
 *   dec_str(out): returned string of decimal digits as ASCII chars
 *
 * Note: This routine converts a DB_C_NUMERIC into a character string that
 * contains the decimal digits of the numeric encoded as ASCII characters.
 */
void
numeric_coerce_num_to_dec_str (DB_C_NUMERIC num, char *dec_str)
{
  unsigned char local_num[DB_NUMERIC_BUF_SIZE];	/* copy of a DB_C_NUMERIC */
  DEC_STRING *bit_value;
  DEC_STRING result;
  unsigned int i, j;

  /* Check if the number is negative */
  numeric_copy (local_num, num);
  if (numeric_is_negative (local_num))
    {
      *dec_str = '-';
      dec_str++;
      numeric_negate (local_num);
    }

  /* Loop through the bits of the numeric building up string */
  numeric_init_dec_str (&result);
  for (i = 0; i < DB_NUMERIC_BUF_SIZE * 8; i += 8)
    {
      if (local_num[i / 8] == 0)
	{
	  continue;
	}
      for (j = 0; j < 8; j++)
	{
	  if (numeric_is_bit_set (local_num, i + j))
	    {
	      bit_value = numeric_get_pow_of_2 ((DB_NUMERIC_BUF_SIZE * 8) - (i + j) - 1);
	      numeric_add_dec_str (bit_value, &result, &result);
	    }
	}
    }

  /* Convert result into ASCII array */
  for (i = 0; i < TWICE_NUM_MAX_PREC; i++)
    {
      if (result.digits[i] == -1)
	{
	  result.digits[i] = 0;
	}
      assert (result.digits[i] >= 0);

      *dec_str = result.digits[i] + '0';
      dec_str++;
    }

  /* Null terminate */
  *dec_str = '\0';
}

static int
numeric_coerce_num_to_dec_str2 (DB_C_NUMERIC num, unsigned int num_size, char *dec_str)
{
  unsigned char local_num[num_size];	/* copy of a DB_C_NUMERIC */
  DEC_STRING *bit_value;
  DEC_STRING result;
  unsigned int i, j;
  int digit_count = 0;
  bool started = false;

  /* Check if the number is negative */
  numeric_copy (local_num, num);
  if (numeric_is_negative (local_num))
    {
      *dec_str = '-';
      dec_str++;
      numeric_negate (local_num);
    }

  /* Loop through the bits of the numeric building up string */
  numeric_init_dec_str (&result);
  for (i = 0; i < num_size * 8; i += 8)
    {
      if (local_num[i / 8] == 0)
	{
	  continue;
	}
      for (j = 0; j < 8; j++)
	{
	  if (numeric_is_bit_set (local_num, i + j))
	    {
	      bit_value = numeric_get_pow_of_2 ((num_size * 8) - (i + j) - 1);
	      numeric_add_dec_str (bit_value, &result, &result);
	    }
	}
    }

  /* Convert result into ASCII array */
  for (i = 0; i < TWICE_NUM_MAX_PREC; i++)
    {
      if (result.digits[i] == -1)
	{
	  result.digits[i] = 0;
	}
      assert (result.digits[i] >= 0);

      if (!started)
	{
	  if (result.digits[i] == 0)
	    {
	      continue;		/* 아직 앞쪽 불필요한 '0' */
	    }
	  started = true;	/* 첫 유효 숫자 만났음 */
	}

      *dec_str = result.digits[i] + '0';
      dec_str++;
      digit_count++;
    }

  /* Null terminate */
  *dec_str = '\0';

  return digit_count;
}

/*
 * numeric_coerce_num_to_double () -
 *   return:
 *   num(in)    : DB_C_NUMERIC
 *   scale(in)  : integer value of the scale
 *   adouble(out): ptr to the returned double value
 *
 * Note: This routine converts a DB_C_NUMERIC into a double precision value.
 */
void
numeric_coerce_num_to_double (DB_C_NUMERIC num, int scale, double *adouble)
{
  char num_string[TWICE_NUM_MAX_PREC + 2];	/* 2: Sign, Null terminate */

  /* Convert the numeric to a decimal string */
  numeric_coerce_num_to_dec_str (num, num_string);

  /* Convert the decimal string into a double */
  /* Problem at precision with line below */
  /* 123.445 was converted to 123.444999999999999999 */
  *adouble = atof (num_string) / pow (10.0, scale);

  /* TODO: [CUBRIDSUS-2637] revert to early code for now. adouble = atof (num_string); for (i = 0; i < scale; i++)
   * adouble /= 10; */
}

/*
 * numeric_fast_convert () -
 *   return:
 *   adouble(in)        :
 *   dst_scale(in)      :
 *   num(in)    :
 *   prec(in)   :
 *   scale(in)  :
 */
static int
numeric_fast_convert (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale)
{
  double scaled_double;
  int scaled_int, estimated_precision;
  scaled_double = (adouble * numeric_Pow_of_10[dst_scale]) + (adouble < 0.0 ? -0.5 : 0.5);
  scaled_int = (int) scaled_double;
  num[DB_NUMERIC_BUF_SIZE - 1] = (scaled_int >> 0) & 0xff;
  num[DB_NUMERIC_BUF_SIZE - 2] = (scaled_int >> 8) & 0xff;
  num[DB_NUMERIC_BUF_SIZE - 3] = (scaled_int >> 16) & 0xff;
  num[DB_NUMERIC_BUF_SIZE - 4] = (scaled_int >> 24) & 0xff;
  memset (num, (scaled_int < 0) ? 0xff : 0x0, DB_NUMERIC_BUF_SIZE - 4);
  /*
   * Now try to make an educated guess at the actual precision.  The
   * actual value of scaled_int is no longer of much interest, just so
   * long as the general magnitude is maintained (i.e., make sure you
   * keep the same number of significant decimal digits).
   */
  if (scaled_int < 0)
    {
      scaled_int = (scaled_int == DB_INT32_MIN) ? DB_INT32_MAX : -scaled_int;
    }

  if (scaled_int < 10L)
    {
      estimated_precision = 1;
    }
  else if (scaled_int < 100L)
    {
      estimated_precision = 2;
    }
  else if (scaled_int < 1000L)
    {
      estimated_precision = 3;
    }
  else if (scaled_int < 10000L)
    {
      estimated_precision = 4;
    }
  else if (scaled_int < 100000L)
    {
      estimated_precision = 5;
    }
  else if (scaled_int < 1000000L)
    {
      estimated_precision = 6;
    }
  else if (scaled_int < 10000000L)
    {
      estimated_precision = 7;
    }
  else if (scaled_int < 100000000L)
    {
      estimated_precision = 8;
    }
  else if (scaled_int < 1000000000L)
    {
      estimated_precision = 9;
    }
  else
    {
      estimated_precision = 10;
    }

  /*
   * No matter what we think it is, it has to be at least as big as the
   * scale.
   */
  if (estimated_precision < dst_scale)
    {
      estimated_precision = dst_scale;
    }

  *prec = estimated_precision;
  *scale = dst_scale;
  return NO_ERROR;
}

/*
 * numeric_get_integral_part  () - return the integral part of a numeric
 *   return: NO_ERROR, or ER_code
 *   num(in)       : the numeric from which to get the integral part
 *   src_prec(in)  : the precision of num
 *   src_scale(in) : the scale of num
 *   dst_prec(in)  : the desired precision of the result
 *   dest(out)	   : the result
 *
 * Note: This function returns a NUMERIC value of precision dst_prec and
 *	 0 scale representing the integral part of the num number.
 */
static void
numeric_get_integral_part (const DB_C_NUMERIC num, const int src_prec, const int src_scale, const int dst_prec,
			   DB_C_NUMERIC dest)
{
  //char dec_str[DB_MAX_NUMERIC_PRECISION * 4];
  //char new_dec_num[DB_MAX_NUMERIC_PRECISION + 1];
  // dec_str 버퍼는 정수, 소수 모두 포함하는 버퍼임.
  char dec_str[NUMERIC_MAX_STRING_SIZE + 1];
  // new_dec_num 버퍼는 정수부 자릿수만큼 필요함.
  char new_dec_num[(DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + 1];
  int i = 0;

  /* the number of digits of the result */
  const int res_num_digits = src_prec - src_scale;

  assert (src_prec - src_scale <= dst_prec);
  assert (num != dest);

  numeric_zero (dest, DB_NUMERIC_BUF_SIZE);
  memset (new_dec_num, 0, (DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE) + 1);

  /* 1. get the dec representation of the numeric value */
  numeric_coerce_num_to_dec_str (num, dec_str);

  /* 2. "zero" the MSB of new_dec_num. */
  for (i = 0; i < dst_prec - res_num_digits; i++)
    {
      new_dec_num[i] = '0';
    }

  /* 3. copy the integral digits from dec_str to the end of the new_dec_num */
  for (i = 0; i < res_num_digits; i++)
    {
      const int idx_new_dec = dst_prec - res_num_digits + i;
      const int idx_dec_str = strlen (dec_str) - src_prec + i;
      new_dec_num[idx_new_dec] = dec_str[idx_dec_str];
    }

  numeric_coerce_dec_str_to_num (new_dec_num, dest);
  if (numeric_is_negative (num))
    {
      numeric_negate (dest);
    }
}

/*
 * numeric_get_fractional_part  () - return the fractional part of a numeric
 *   return: NO_ERROR, or ER_code
 *   num(in)       : the numeric from which to get the fractional part
 *   src_prec(in)  : the precision of num
 *   src_scale(in) : the scale of num
 *   dst_scale(in) : the desired scale of the result
 *   dest(out)	   : the result
 *
 * Note:  This function returns a numeric with precision dst_scale and scale 0
 *	  which contains the fractional part of a numeric
 */
static void
numeric_get_fractional_part (const DB_C_NUMERIC num, const int src_scale, const int dst_scale, DB_C_NUMERIC dest)
{
  //char dec_str[DB_MAX_NUMERIC_PRECISION * 4];
  //char new_dec_num[DB_MAX_NUMERIC_PRECISION + 1];
  // numeric_coerce_num_to_dec_str 함수에서 TWICE_NUM_MAX_PREC(122 + 127) 만큼 버퍼를 읽어서 처리함.
  // dec_str 버퍼는 정수, 소수 모두 포함하는 버퍼임.
  char dec_str[NUMERIC_MAX_STRING_SIZE + 1];	// 300, 최대 정수(38 + 84) + 최대 소수 (38 + 127) + 여유 13자리
  // new_dec_num 버퍼는 소수부 자릿수만큼 필요함.
  char new_dec_num[(DB_MAX_NUMERIC_PRECISION + DB_MAX_NUMERIC_SCALE) + 1];
  int i = 0;

  assert (src_scale <= dst_scale);
  assert (num != dest);

  numeric_zero (dest, DB_NUMERIC_BUF_SIZE);
  memset (new_dec_num, 0, (DB_MAX_NUMERIC_PRECISION + DB_MAX_NUMERIC_SCALE) + 1);

  /* 1. get the dec representation of the numeric value */
  numeric_coerce_num_to_dec_str (num, dec_str);

  /* 2. copy all scale digits to the beginning of the new_dec_num buffer */
  for (i = 0; i < src_scale; i++)
    {
      new_dec_num[i] = dec_str[strlen (dec_str) - src_scale + i];
    }

  /* 3. add 0's for the reminder of the dst_scale */
  for (i = src_scale; i < dst_scale; i++)
    {
      new_dec_num[i] = '0';
    }

  /* 4. null-terminate the string */
  new_dec_num[dst_scale] = '\0';

  numeric_coerce_dec_str_to_num (new_dec_num, dest);
  if (numeric_is_negative (num))
    {
      numeric_negate (dest);
    }
}

/*
 * numeric_is_fraction_part_zero () - check if fractional part of a numeric is
 *				      equal to 0
 * return : boolean
 * num (in)   : numeric value
 * scale (in) : scale of the numeric
 */
static bool
numeric_is_fraction_part_zero (const DB_C_NUMERIC num, const int scale)
{
  int i, len = 0;
  //char dec_str[(2 * DB_MAX_NUMERIC_PRECISION) + 4];
  char dec_str[NUMERIC_MAX_STRING_SIZE + 1];
  numeric_coerce_num_to_dec_str (num, dec_str);
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
 */
int
numeric_internal_double_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale)
{
  return numeric_internal_real_to_num (adouble, dst_scale, num, prec, scale, false);
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
numeric_internal_float_to_num (float afloat, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale)
{
  return numeric_internal_real_to_num (afloat, dst_scale, num, prec, scale, true);
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
 */
int
numeric_internal_real_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale, bool is_float)
{
  //char numeric_str[MAX (TP_DOUBLE_AS_CHAR_LENGTH + 1, DB_MAX_NUMERIC_PRECISION + 4)];
  char numeric_str[MAX (TP_DOUBLE_AS_CHAR_LENGTH + 1, NUMERIC_MAX_STRING_SIZE + 1)];
  int i = 0;

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

      numeric_coerce_dec_str_to_num (numeric_str, num);
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

	      numeric_coerce_dec_str_to_num ("0", num);
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
		  char *dst = MIN (numeric_str + 1 + ndigits - decpt,
				   numeric_str + sizeof numeric_str / sizeof numeric_str[0] - 1), *src = dst + decpt;

		  *prec = MIN (DB_MAX_NUMERIC_PRECISION, -decpt + ndigits);
		  *scale = *prec;

		  /* actually rounding should also be performed if value gets truncated. */
		  *dst = '\0';
		  dst--;
		  src--;

		  /* shift all digits in the string */
		  while (src >= numeric_str + 1)
		    {
		      *dst = *src;
		      dst--;
		      src--;
		    }

		  /* prepend 0s from right to left until the decimal point position is reached */
		  while (dst > numeric_str)
		    {
		      *dst-- = '0';
		    }
		}
	      else
		{
		  /* the numer is greater than 1, either insert the decimal point at the correct position in the digits
		   * sequence, or append 0s to the digits from left to right until the decimal point is reached. */

		  if (decpt > DB_MAX_NUMERIC_PRECISION)
		    //if (decpt > DB_INTERNAL_NUMERIC_PRECISION_LIMIT)
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
		    }
		}

	      /* append zeroes until dst_scale is reached */
	      while (*prec < DB_MAX_NUMERIC_PRECISION && *scale < dst_scale)
		//while (*prec < DB_INTERNAL_NUMERIC_PRECISION_LIMIT && *scale < dst_scale)
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
		  numeric_coerce_dec_str_to_num (numeric_str, num);
		}
	      else
		{
		  numeric_coerce_dec_str_to_num (numeric_str + 1, num);
		}

	      return NO_ERROR;
	    }
	}
      break;
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * numeric_coerce_double_to_num () -
 *   return:
 *   adouble(in): ptr to the returned double value
 *   num(out)   : DB_C_NUMERIC
 *   prec(out)  : integer value of the precision
 *   scale(out) : integer value of the scale
 *
 * Note: This routine converts a double precision value into a DB_C_NUMERIC.
 *     Works via the static routine numeric_internal_double_to_num (), which is
 *     also called from numeric_db_value_coerce_to_num () so that we can exploit info
 *     about the scale of the destination.
 */
int
numeric_coerce_double_to_num (double adouble, DB_C_NUMERIC num, int *prec, int *scale)
{
  /*
   *   return numeric_internal_double_to_num(adouble, DB_MAX_NUMERIC_PRECISION,
   */
  return numeric_internal_double_to_num (adouble, 16, num, prec, scale);
}
#endif /* ENABLE_UNUSED_FUNCTION */


// 춘택님 아이디어 : 하나의 for문이 아닌 딱 필요한 부분만 반복문으로 처리하도록 분리 필요!
static int
parse_decimal_string4 (const char *astring, int astring_length, INTL_CODESET codeset, bool * negate_value,
		       char *int_digits, int *int_len, char *frac_digits, int *frac_len, int *int_first_nz,
		       int *int_last_nz, int *frac_first_nz, int *frac_last_nz)
{
  int int_count = 0;		// 정수부 전체 자릿수
  int frac_count = 0;		// 소수부 전체 자릿수
  int parse_pos = 0;		// 현재 파싱 위치 (index)
  int skip = 1;			// 공백 건너뛰기용
  char current_char = '\0';
  int int_parse_limit = DB_MAX_NUMERIC_PRECISION - DB_MIN_NUMERIC_SCALE;	// 38 - (-84) = 122
  int frac_parse_limit = DB_MAX_NUMERIC_PRECISION + DB_MAX_NUMERIC_SCALE;	// 38 + 127 = 165
  bool has_digit = false;
  bool pad_character_zero = false;
  bool sign_found = false;
  bool trailing_spaces = false;
  bool valid_zero = false;

  *int_first_nz = *int_last_nz = -1;
  *frac_first_nz = *frac_last_nz = -1;
  *negate_value = false;

  // 1) 공백, 부호, 0 처리
  while (parse_pos < astring_length)
    {
      skip = 1;

      if (astring[parse_pos] >= '1' && astring[parse_pos] <= '9')
	{
	  has_digit = true;
	  break;
	}
      else if (astring[parse_pos] == '0')
	{
	  /* leading pad '0' found */
	  pad_character_zero = true;
	  parse_pos++;
	  continue;
	}
      else if (astring[parse_pos] == '.')
	{
	  has_digit = true;	// Case : "0.0000"
	  break;
	}
      else if (astring[parse_pos] == '+' || astring[parse_pos] == '-')
	{
	  if (!sign_found)
	    {
	      sign_found = true;
	      if (astring[parse_pos] == '-')
		{
		  *negate_value = true;
		  parse_pos++;
		  continue;
		}
	    }
	  else
	    {			/* Duplicate sign characters */
	      return DOMAIN_INCOMPATIBLE;
	    }
	}
      else if (intl_is_space (astring + parse_pos, NULL, codeset, &skip))
	{
	  parse_pos += skip;	// 공백 건너뛰기
	  continue;
	}
      else
	{
	  /* Stray Non-numeric compatible character */
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  // 2) 정수부 파싱
  while (parse_pos < astring_length && has_digit)
    {
      current_char = astring[parse_pos];

      if (current_char >= '0' && current_char <= '9' && !trailing_spaces)
	{
	  if (int_count < int_parse_limit)
	    {
	      if (current_char != '0' && *int_first_nz < 0)
		{
		  *int_first_nz = int_count;
		  pad_character_zero = false;
		}
	      if (current_char != '0')
		{
		  *int_last_nz = int_count;
		}
	      int_digits[int_count++] = current_char;
	    }
	  else
	    {
	      return ER_IT_DATA_OVERFLOW;	// 정수부가 overflow 되면 에러
	    }
	  parse_pos++;
	  continue;
	}
      else if (current_char == '.')
	{
	  has_digit = true;	// Case : "0.0000"
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
	  parse_pos += skip;	// 공백 건너뛰기
	  continue;
	}
      else if (current_char == ',')
	{
	  return DOMAIN_INCOMPATIBLE;
	}
      else
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  // 3) 소수부 파싱
  while (parse_pos < astring_length && has_digit)
    {
      current_char = astring[parse_pos];

      if (current_char >= '0' && current_char <= '9' && !trailing_spaces)
	{
	  if (frac_count < frac_parse_limit)
	    {
	      frac_digits[frac_count] = current_char;
	      // non-zero 추적
	      if (current_char != '0' || valid_zero)
		{
		  if (*frac_first_nz < 0)
		    {
		      *frac_first_nz = frac_count;
		      pad_character_zero = false;
		      valid_zero = true;
		    }
		  *frac_last_nz = frac_count;
		}
	      frac_count++;
	    }
	  else
	    {
	      return ER_IT_DATA_OVERFLOW;	// 소수부가 overflow 되면 에러
	    }
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
	  parse_pos += skip;	// 공백 건너뛰기
	  continue;
	}
      else if (current_char == ',')
	{
	  return DOMAIN_INCOMPATIBLE;
	}
      else
	{
	  return DOMAIN_INCOMPATIBLE;
	}
    }

  if (pad_character_zero)
    {
      // 4) 0 처리
      // 0 : pad_character_zero(t), has_digit(f), int_count(0), frac_count(0)
      // 00000 : pad_character_zero(t), has_digit(f), int_count(0), frac_count(0)
      // 0.0000 : pad_character_zero(t), has_digit(t), int_count(0), frac_count(1 이상)
      // 0.000001 : pad_character_zero(f), has_digit(t), int_count(0), frac_count(1 이상)
      // 0009000000 : pad_character_zero(?), has_digit(t), int_count(6), frac_count(0)
      int_digits[0] = '\0';
      frac_digits[0] = '\0';
      *int_first_nz = -1;
      *int_last_nz = -1;
      *int_len = 1;
      *frac_len = 0;
    }
  else if (!has_digit && (int_count + frac_count) == 0)
    {
      // 5) NULL 처리 (space만 있는 경우)
      // '' : has_digit(f), int_count(0), frac_count(0)
      // '   ' : has_digit(f), int_count(0), frac_count(0)
      // '1  ' : has_digit(t), int_count(1), frac_count(0)
      int_digits[0] = '\0';
      frac_digits[0] = '\0';
      *int_len = 0;
      *frac_len = 0;
    }
  else
    {
      // 6) 정상 케이스: 채운 길이만큼 널 종결
      int_digits[int_count] = '\0';
      frac_digits[frac_count] = '\0';
      *int_len = int_count;
      *frac_len = frac_count;
    }

  return NO_ERROR;
}

/* 2. 정수부와 소수부를 분석하여 precision과 scale을 계산 */
// 0인 경우의 처리 리팩토링 필요!, 굳이 밑에 까지 안가고 바로 return 하면 안되나?
static void
compute_prec_scale3 (const char *int_digits, int int_len, const char *frac_digits, int frac_len, int int_first_nz,
		     int int_last_nz, int frac_first_nz, int frac_last_nz, char *out_num_string, int *out_prec,
		     int *out_scale, bool * need_round)
{
  int total = int_len + frac_len;

  /* Step 1: 각 케이스별로 복사할 위치와 반올림 정보, 임시 precision/scale 계산 */
  const char *temp_int_digits = NULL, *temp_frac_digits = NULL;
  int temp_int_len = 0, temp_frac_len = 0;
  char next_digit = '0';
  int tmp_prec = 0, tmp_scale = 0;
  int frac_zero_cnt = 0;

  if (frac_len == 0)
    {
      /* Case 1: 정수부만 존재 */
//       if (int_len == 0)
//      {
//        /* Case 1-A: 0인 경우 (일단 임시로 살려!) */
//        tmp_prec = 1;
//        tmp_scale = 0;
//        temp_int_digits = "0";
//        temp_int_len = 1;
//      }
//       else 
      if (int_len <= DB_MAX_NUMERIC_PRECISION)
	{
	  /* Case 1-B: 기존 방식 - 정수부 길이를 precision으로 사용 */
	  tmp_prec = int_len;
	  tmp_scale = 0;
	  temp_int_digits = int_digits;
	  temp_int_len = int_len;
	}
      else
	{
	  /* Case 1-C: Oracle 스타일 - trailing zero 개수만큼 음수 scale 적용 */
	  int sig_len = int_last_nz - int_first_nz + 1;
	  int tz_cnt = int_len - (int_last_nz + 1);
	  tmp_prec = sig_len;
	  tmp_scale = -tz_cnt;
	  temp_int_digits = int_digits + int_first_nz;
	  temp_int_len = sig_len;
	  // 아래 조건은 향후 F-P NUMERIC을 도입하면서, DB_MAX_NUMERIC_PRECISION 값이 38 -> 40 으로 늘어나면 다시 살리는 용도로 남겨둠
	  // 만약, 지금 살려두면 precision이 38인 테이블에 39 자리 이상의 값이 들어오면 항상 true라 반올림되어 overflow 에러가 발생하지 않음.
	  /*
	     if (sig_len > DB_MAX_NUMERIC_PRECISION)
	     {
	     tmp_prec = DB_MAX_NUMERIC_PRECISION;
	     tmp_scale = -tz_cnt;
	     temp_int_digits = int_digits + int_first_nz;
	     temp_int_len = DB_MAX_NUMERIC_PRECISION;
	     next_digit = temp_int_digits[int_first_nz + DB_MAX_NUMERIC_PRECISION];
	     *need_round = true;
	     }
	     else
	     {
	     tmp_prec = sig_len;
	     tmp_scale = -tz_cnt;
	     temp_int_digits = int_digits + int_first_nz;
	     temp_int_len = sig_len;
	     *need_round = false;
	     }
	   */
	}
    }
  else if (int_len == 0)
    {
      /* Case 2: 소수부만 존재 */
//       if (frac_first_nz < 0)
//      {
//        /* Case 2-A: 0인 경우 (일단 임시로 살려!) */
//        tmp_prec = 1;
//        tmp_scale = frac_len;
//        temp_frac_digits = "0";
//        temp_frac_len = 1;
//      }
//       else 
      if (frac_len <= DB_MAX_NUMERIC_PRECISION)
	{
	  /* Case 2-B: 기존 방식 - 소수부 길이를 precision으로 사용 */
	  tmp_prec = frac_len;
	  tmp_scale = frac_len;
	  temp_frac_digits = frac_digits;
	  temp_frac_len = frac_len;
	}
      else
	{
	  /* Case 2-C: Oracle 스타일 - leading zero 건너뛰고 최대 MAX_PRECISION개 사용 */
	  int nz_len = frac_last_nz - frac_first_nz + 1;
	  if (nz_len > DB_MAX_NUMERIC_PRECISION)
	    {
	      /*
	       * 예시 : 0.123456789012345678901234567890123456789
	       * 결과 : 0.12345678901234567890123456789012345679
	       * 
	       * 아래 case는 나중에 prec이 40까지 늘어나면 고민해야할듯?
	       * scale이 166 이면, 반올림을 하니 frac_len - tmp_prec 를 해야 scale이 깔끔해지지 않을까?
	       * 예시 : 0.0000000000000000000000...0999..9999 (0: 127개, 9: 39개)
	       * 결과 : 0.0000000000000000000000...01 (0: 126개, 1: 1개 = 총 127자리)
	       */
	      tmp_prec = DB_MAX_NUMERIC_PRECISION;
	      tmp_scale = frac_len;
	      temp_frac_digits = frac_digits + frac_first_nz;
	      temp_frac_len = DB_MAX_NUMERIC_PRECISION;
	      next_digit = frac_digits[frac_first_nz + DB_MAX_NUMERIC_PRECISION];
	      *need_round = true;

	      frac_zero_cnt = frac_len - nz_len;	// 순수 0 개수
	    }
	  else
	    {
	      /*
	       * 예시 : 0.0000000000000000000000...0999..9999 (0: 127개, 9: 38개)
	       * 결과 : 0.0000000000000000000000...0999..9999 (0: 127개, 9: 38개)
	       */
	      tmp_prec = nz_len;
	      tmp_scale = frac_len;
	      temp_frac_digits = frac_digits + frac_first_nz;
	      temp_frac_len = nz_len;
	      *need_round = false;
	    }
	}
    }
  else
    {
      /* Case 3: 정수부와 소수부 모두 존재 */
      if (total <= DB_MAX_NUMERIC_PRECISION)
	{
	  /* Case 3-A: 기존 방식 - 전체 길이를 precision으로 사용 */
	  tmp_prec = total;
	  tmp_scale = frac_len;
	  temp_int_digits = int_digits;
	  temp_int_len = int_len;
	  temp_frac_digits = frac_digits;
	  temp_frac_len = frac_len;
	}
      else
	{
	  /* Case 3-B: 전체 길이가 MAX_PRECISION을 초과하는 경우 */
	  int drop_total = total - DB_MAX_NUMERIC_PRECISION;
	  if (drop_total <= frac_len)
	    {
	      /* Case 3-B-1: 소수부가 더 많은 경우 - 소수부에서 반올림 */
	      /*
	       * 예시: 234209384023423842384.238942938479238749238492834792384
	       * 결과: 234209384023423842384.49238492834792385 (38자리에서 반올림)
	       */
	      int keep_frac = frac_len - drop_total;
	      tmp_prec = int_len + keep_frac;
	      tmp_scale = keep_frac;
	      temp_int_digits = int_digits;
	      temp_int_len = int_len;
	      temp_frac_digits = frac_digits;
	      temp_frac_len = keep_frac;
	      next_digit = frac_digits[keep_frac];
	      *need_round = true;
	    }
	  else
	    {
	      /* Case 3-B-2: 정수부가 더 많은 경우 - 정수부에서 처리 */
	      int drop_int = drop_total - frac_len;
	      int trailing_zeros_count = int_len - (int_last_nz + 1);
	      if (drop_int <= trailing_zeros_count)
		{
		  /* Oracle 스타일: trailing zero를 음수 scale로 처리 */
		  int sig_len = int_last_nz - int_first_nz + 1;
		  tmp_prec = sig_len;
		  tmp_scale = -trailing_zeros_count;
		  temp_int_digits = int_digits + int_first_nz;
		  temp_int_len = sig_len;
		  *need_round = false;
		}
	      else
		{
		  /* 정수부만 출력, scale 제거 */
		  /*
		   * 중요: 정수부 반올림은 flag로 처리
		   * 
		   * 예시: 123456789012345678901234567890123456789.0
		   * 결과: 12345678901234567890123456789012345679
		   *
		   */
		  int keep_int = int_len - drop_int;
		  tmp_prec = keep_int;
		  tmp_scale = 0;
		  temp_int_digits = int_digits;
		  temp_int_len = keep_int;
		  next_digit = int_digits[keep_int];
		  *need_round = true;
		  //   if (keep_int <= 0)
		  //     {
		  //       // 여기에 들어오는 case는 없어서, 지워도 가능함.
		  //       tmp_prec = 1;
		  //       tmp_scale = 0;
		  //       temp_int_digits = "0";
		  //       temp_int_len = 1;
		  //       *need_round = false;
		  //     }
		  //   else
		  //     {
		  //       tmp_prec = keep_int;
		  //       tmp_scale = 0;
		  //       temp_int_digits = int_digits;
		  //       temp_int_len = keep_int;
		  //       /*
		  //        * 중요: 정수부 반올림은 flag로 처리
		  //        * 
		  //        * 예시: 123456789012345678901234567890123456789.0
		  //        * 결과: 12345678901234567890123456789012345679
		  //        *
		  //        */
		  //       next_digit = int_digits[keep_int];
		  //       *need_round = true;
		  //     }
		}
	    }
	}
    }

  /* Step 2: 범위 검사 (memcpy 전에 수행) */
  // 소수부는 127이 아닌 165까지 허용하며, 나중에 num_to_num 함수에서 반올림 처리됨
  if (tmp_prec > DB_MAX_NUMERIC_PRECISION || tmp_scale > (DB_MAX_NUMERIC_SCALE + DB_MAX_NUMERIC_PRECISION)
      || tmp_scale < DB_MIN_NUMERIC_SCALE)
    {
      // 밖에서 에러 처리
      *out_prec = tmp_prec;
      *out_scale = tmp_scale;
      return;
    }

  /* Step 3: 실제 문자열 복사 */
  char *tmp_num_string = out_num_string;
  if (temp_int_len)
    {
      memcpy (tmp_num_string, temp_int_digits, temp_int_len);
      tmp_num_string += temp_int_len;
    }

  if (temp_frac_len)
    {
      memcpy (tmp_num_string, temp_frac_digits, temp_frac_len);
      tmp_num_string += temp_frac_len;
    }
  *tmp_num_string = '\0';

  /* Step 4: 반올림 및 자릿수 조정 */
  int final_len = temp_int_len + temp_frac_len;
  if (*need_round)
    {
      // 반올림 하는 곳에서는 prec이 늘어나는 경우는 없음, 대부분 sacle 조정만함
      // 그래서 따로 prec 검사는 skip 함.
      (void) round_and_clamp (out_num_string, &tmp_prec, &tmp_scale, temp_int_len, temp_frac_len, frac_zero_cnt,
			      next_digit);
    }

  *out_prec = tmp_prec;
  *out_scale = tmp_scale;
}

// 3. prec/scale 계산시, 반올림 및 clamp 할때 사용하는 함수
/**
 * out_str: 숫자 문자열
 * out_prec: precision (총 자릿수)
 * out_scale: scale (소수부 자릿수)
 * temp_int_len: 정수부 길이 (0 이면 순수 소수)
 * temp_frac_len: 소수부 길이
 * frac_zero_cnt: 소수부 순수 0 개수
 * next_digit: 잘린 다음 자리(반올림 기준)
 */
static void
round_and_clamp (char *out_str, int *out_prec, int *out_scale, int temp_int_len, int temp_frac_len, int frac_zero_cnt,
		 char next_digit)
{
  int prec = *out_prec;
  int scale = *out_scale;
  bool all_nine = false;

  // 1) 잘린 첫 자리(next_digit) 보고 반올림
  if (next_digit >= '5')
    {
      int i = DB_MAX_NUMERIC_PRECISION - 1;
      while (i >= 0 && out_str[i] == '9')
	{
	  out_str[i] = '0';
	  i--;
	}

      if (i >= 0)
	{
	  // 123.456...
	  // 0.55555555555555...
	  out_str[i] += 1;
	}
      else
	{
	  // 모두 9 였다면 "1000…", 맨 앞만 '1'로, 나머지는 '0'
	  all_nine = true;
	  if (temp_int_len == 0)
	    {
	      // 0.9999..
	      // 0.0000..9999
	      prec = 1;
	      out_str[DB_MAX_NUMERIC_PRECISION - 1] = '1';
	    }
	  else
	    {
	      // 99999.99999..
	      // 99999.00000..
	      // 99999.. -- 정수만 있으니, scale은 그대로
	      memmove (out_str + 1, out_str, DB_MAX_NUMERIC_PRECISION);
	      out_str[0] = '1';
	    }
	}
    }

  // 2) 널종결자 추가
  out_str[DB_MAX_NUMERIC_PRECISION] = '\0';

  // 3) scale 재계산
  if (all_nine)
    {
      if (temp_int_len == 0)
	{
	  // 0.9999..
	  // 0.0000..9999
	  scale = frac_zero_cnt == 0 ? 0 : frac_zero_cnt;
	}
      else if (temp_frac_len != 0)
	{
	  // 99999.99999..
	  // 99999.00000..
	  scale--;
	}
    }
  else
    {
      // 123.456...
      if (temp_int_len == 0)
	{
	  // 0.55555555555555...
	  scale = frac_zero_cnt + prec;
	}
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
  //char num_string[TWICE_NUM_MAX_PREC + 1]; // 130 + 1 = 131
  char num_string[DB_MAX_NUMERIC_PRECISION + 1];	// 최종 저장할 10진수 유효 숫자임으로 38 + 1 = 39
  unsigned char num[DB_NUMERIC_BUF_SIZE];	// 10진수 유효 숫자를 256진수로 변환해서 저장할 버퍼
  bool negate_value = false, need_round = false;
  int prec, scale;
  int int_len, frac_len;
  int int_first_nz, int_last_nz, frac_first_nz, frac_last_nz;
  char int_digits[astring_length + 1];	// 정수부 유효 숫자
  char frac_digits[astring_length + 1];	// 소수부 유효 숫자
  int ret = NO_ERROR;
  TP_DOMAIN *domain;

  // 1) parse and compute prec/scale
  ret =
    parse_decimal_string4 (astring, astring_length, codeset, &negate_value, int_digits, &int_len, frac_digits,
			   &frac_len, &int_first_nz, &int_last_nz, &frac_first_nz, &frac_last_nz);
  if (ret != NO_ERROR)
    {
      if (ret == ER_IT_DATA_OVERFLOW)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	}
      goto exit_on_error;
    }

  if ((int_len + frac_len) == 0)
    {
      // 5) NULL 처리 (space만 있는 경우)
      prec = 0;
      scale = 0;
      num_string[0] = '\0';
    }
  else if (int_len == 1 && int_first_nz < 0 && int_last_nz < 0)
    {
      // 4) 0 처리
      prec = 1;
      scale = 0;
      num_string[0] = '0';
      num_string[1] = '\0';
    }
  else
    {
      (void) compute_prec_scale3 (int_digits, int_len, frac_digits, frac_len, int_first_nz, int_last_nz, frac_first_nz,
				  frac_last_nz, num_string, &prec, &scale, &need_round);

      /* If there is no overflow, try to parse the decimal string */
      if (prec > DB_MAX_NUMERIC_PRECISION || (scale > DB_MAX_NUMERIC_SCALE + DB_MAX_NUMERIC_PRECISION)
	  || scale < DB_MIN_NUMERIC_SCALE)
	{
	  domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  // 3) 10진 문자열 -> base-256 바이너리
  numeric_coerce_dec_str_to_num (num_string, num);

  // 4) 부호 반영
  if (negate_value)
    {
      numeric_negate (num);
    }

  // 5) DB_VALUE 생성
  // 아니... NULL 표현을 위해 여기서 그냥 err를 통과시켜버리네...
  // ex) select cast(cast('' as char varying(10)) as numeric(38,10));
  db_make_numeric (result, num, prec, scale);
  result->domain.numeric_info.has_round = need_round;
  return ret;

exit_on_error:

  db_value_domain_init (result, DB_TYPE_NUMERIC, DB_DEFAULT_NUMERIC_PRECISION, DB_DEFAULT_NUMERIC_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
}

/*
 * numeric_coerce_num_to_num () -
 *   return: NO_ERROR, or ER_code
 *   src_num(in)        : DB_C_NUMERIC
 *   src_prec(in)       : integer value of the precision
 *   src_scale(in)      : integer value of the scale
 *   dest_prec(in)      : integer value of the precision
 *   dest_scale(in)     : integer value of the scale
 *   dest_num(out)      : DB_C_NUMERIC
 * Note: This routine converts a numeric of a given precision and scale to
 * another precision and scale.
 */
int
numeric_coerce_num_to_num (DB_C_NUMERIC src_num, int src_prec, int src_scale, int dest_prec, int dest_scale,
			   DB_C_NUMERIC dest_num)
{
  int ret = NO_ERROR;
  //char num_string[DB_MAX_NUMERIC_PRECISION * 4];
  char num_string[NUMERIC_MAX_STRING_SIZE];	// 정수(38+84) + 소수(38+127) + 13 = 300
  int scale_diff;
  int orig_length;
  int i, len;
  bool round_up = false;
  bool negate_answer;

  if (src_num == NULL)
    {
      return ER_FAILED;
    }

  /* 기타 메모 :
   * insert 할 때는 dest 가 도메인 값임
   * select 할 때는 src가 도메인 값임
   */
  /* Check for trivial case */
  if (src_prec <= dest_prec && src_scale == dest_scale)
    {
      numeric_copy (dest_num, src_num);
      return NO_ERROR;
    }

/* 동적 처리 (나중에)  
  if (src_scale < 0 || dest_scale < 0)
    {
      num_string_size = DB_MAX_NUMERIC_PRECISION + (-DB_MIN_NUMERIC_SCALE) + 3;
      num_string = (char *) db_private_alloc (NULL, num_string_size);
      if (num_string == NULL)
        {
          ret = ER_OUT_OF_VIRTUAL_MEMORY;
          goto exit_on_error;
        }
    }
  else if (src_scale > src_prec || dest_scale > dest_prec)
    {
      num_string_size = TWICE_NUM_MAX_PREC;
      num_string = (char *) db_private_alloc (NULL, num_string_size);
      if (num_string == NULL)
        {
          ret = ER_OUT_OF_VIRTUAL_MEMORY;
          goto exit_on_error;
        }
    }
  else 
    {
      num_string_size = DB_MAX_NUMERIC_PRECISION * 4;
      num_string = (char *) db_private_alloc (NULL, num_string_size);
      if (num_string == NULL)
        {
          ret = ER_OUT_OF_VIRTUAL_MEMORY;
          goto exit_on_error;
        }
    }
*/

  /* If src is negative, coerce the positive part now so that rounding is always done in the correct 'direction'. */
  if (numeric_is_negative (src_num))
    {
      negate_answer = true;
      numeric_copy (dest_num, src_num);
      numeric_negate (dest_num);
    }
  else
    {
      negate_answer = false;
      numeric_copy (dest_num, src_num);
    }

  /* Convert the src_num into a decimal string */
  numeric_coerce_num_to_dec_str (dest_num, num_string);
  /* Scale the number */

  // 소수부 자리가 더 많은 경우
  // 입력값(src) < 도메인(dest) == 0.009 
  if (src_scale < dest_scale)
    {				/* add trailing zeroes */
      /* scale 값 만큼 값을 보정하는 곳
       * num_string 버퍼 크기 늘려서 기존 로직이 동작되게 함.
       *
       * CREATE TABLE t1 (col1 numeric(1,127));
       * INSERT INTO t1 values(0.0000000000000000000000009);
       */

      scale_diff = dest_scale - src_scale;
      orig_length = strlen (num_string);
      for (i = 0; i < scale_diff; i++)
	{
	  num_string[orig_length + i] = '0';
	}
      num_string[orig_length + scale_diff] = '\0';
    }
  else if (dest_scale < src_scale)
    {				/* Truncate and prepare for rounding */
      /* TC :
       * CREATE TABLE t1 (col1 numeric(2,-84));
       * INSERT INTO t1 values(99000);
       */
      scale_diff = src_scale - dest_scale;
      orig_length = strlen (num_string);
      if (num_string[orig_length - scale_diff] >= '5' && num_string[orig_length - scale_diff] <= '9')
	{
	  round_up = true;
	}
      num_string[orig_length - scale_diff] = '\0';
    }

  /*
   * Check to see if the scaled number 'fits' into the desired precision
   * and scaling by looking for significant digits prior to the last
   * 'precision' digits.
   */
  for (i = 0, len = strlen (num_string) - dest_prec; i < len; i++)
    {
      if (num_string[i] >= '1' && num_string[i] <= '9')
	{
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  /* only when all number are 9, round up will led overflow. */
  if (round_up)
    {
      bool is_all_nine = true;
      for (len = strlen (num_string), i = len - dest_prec; i < len; i++)
	{
	  if (num_string[i] != '9')
	    {
	      is_all_nine = false;
	      break;
	    }
	}
      if (is_all_nine)
	{
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  /* Convert scaled string into destination */
  numeric_coerce_dec_str_to_num (num_string, dest_num);
  /* Round up, if necessary */
  if (round_up)
    {
      numeric_increase (dest_num);
    }

  /* Negate the answer, if necessary */
  if (negate_answer)
    {
      numeric_negate (dest_num);
    }

// 동적 처리 (나중에) 
//   if (num_string != NULL)
//     {
//       db_private_free (NULL, num_string);
//     }

  return ret;

exit_on_error:

// 동적 처리 (나중에)
//   if (num_string != NULL)
//     {
//       db_private_free (NULL, num_string);
//     }

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
	ret = numeric_internal_double_to_num (adouble, desired_scale, num, &precision, &scale);
	break;
      }

    case DB_TYPE_FLOAT:
      {
	float adouble = (float) db_get_float (src);
	ret = numeric_internal_float_to_num (adouble, desired_scale, num, &precision, &scale);
	break;
      }

    case DB_TYPE_MONETARY:
      {
	double adouble = db_value_get_monetary_amount_as_double (src);
	ret = numeric_internal_double_to_num (adouble, desired_scale, num, &precision, &scale);
	break;
      }

    case DB_TYPE_INTEGER:
      {
	int anint = db_get_int (src);

	numeric_coerce_int_to_num (anint, num);
	precision = get_significant_digit (anint);
	scale = 0;
	break;
      }

    case DB_TYPE_SMALLINT:
      {
	int anint = (int) db_get_short (src);

	numeric_coerce_int_to_num (anint, num);
	precision = get_significant_digit (anint);
	scale = 0;
	break;
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bigint = db_get_bigint (src);

	numeric_coerce_bigint_to_num (bigint, num);
	precision = get_significant_digit (bigint);
	desired_precision = MAX (desired_precision, precision);
	scale = 0;
	break;
      }

    case DB_TYPE_NUMERIC:
      {
	precision = DB_VALUE_PRECISION (src);
	scale = DB_VALUE_SCALE (src);
	// 도메인이 음수 scale이고, 입력 값에 소수가 있을 때
	// 예) 도메인(38, -1), 입력 값(1234567890123456789012345678901234567890123456789012345678901234567890123456789.0)
	// 이 경우 numeric_coerce_string_to_num 에서 이미 왼쪽 유효 자리부터 38자리 까지 반올림을 수행함.
	// 따라서, num_to_num 에서 반올림을 skip 하기 위해 flag 추가 
	if (src->domain.numeric_info.has_round && desired_scale < 0)
	  {
	    scale = desired_scale;
	  }
	numeric_copy (num, db_locate_numeric (src));
	break;
      }

    case DB_TYPE_ENUMERATION:
      {
	int anint = db_get_enum_short (src);
	numeric_coerce_int_to_num (anint, num);
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
      /* Make the intermediate value */
      db_make_numeric (dest, num, precision, scale);
      ret =
	numeric_coerce_num_to_num (db_locate_numeric (dest), DB_VALUE_PRECISION (dest), DB_VALUE_SCALE (dest),
				   desired_precision, desired_scale, num);
      if (ret != NO_ERROR)
	{
	  goto exit_on_error;
	}

      db_make_numeric (dest, num, desired_precision, desired_scale);
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

  *data_status = DATA_STATUS_OK;
  /* Check for a DB_TYPE_NUMERIC src and a non NULL numerical dest */
  /* Switch on the dest type */
  switch (DB_VALUE_DOMAIN_TYPE (dest))
    {
    case DB_TYPE_DOUBLE:
      {
	double adouble;
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
	db_make_monetary (dest, DB_CURRENCY_DEFAULT, adouble);
	break;
      }

    case DB_TYPE_INTEGER:
      {
	double adouble;
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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

	ret = numeric_coerce_num_to_bigint (db_locate_numeric (src), DB_VALUE_SCALE (src), &bint);
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
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARNCHAR:
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
	else if (type == DB_TYPE_NCHAR)
	  {
	    db_make_nchar (dest, size, return_string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  }
	else if (type == DB_TYPE_VARNCHAR)
	  {
	    db_make_varnchar (dest, size, return_string, size, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  }
	dest->need_clear = true;
	break;
      }

    case DB_TYPE_TIME:
      {
	double adouble;
	DB_TIME v_time;
	int hour, minute, second;

	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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

	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
	v_date = (DB_DATE) (adouble);
	db_date_decode (&v_date, &month, &day, &year);
	db_make_date (dest, month, day, year);
	break;
      }

    case DB_TYPE_TIMESTAMP:
      {
	double adouble;
	DB_TIMESTAMP v_timestamp;

	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
	v_timestamp = (DB_TIMESTAMP) (adouble);
	db_make_timestamp (dest, v_timestamp);
	break;
      }

    case DB_TYPE_DATETIME:
      {
	DB_BIGINT bi, tmp_bi;
	DB_DATETIME v_datetime;

	ret = numeric_coerce_num_to_bigint (db_locate_numeric (src), DB_VALUE_SCALE (src), &bi);
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

  switch (DB_VALUE_DOMAIN_TYPE (dest))
    {
    case DB_TYPE_DOUBLE:
      {
	double adouble;
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
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
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
	if (OR_CHECK_INT_OVERFLOW (adouble))
	  {
	    return ER_FAILED;
	  }
	if (!numeric_is_fraction_part_zero (db_locate_numeric (src), DB_VALUE_SCALE (src)))
	  {
	    return ER_FAILED;
	  }
	db_make_int (dest, (int) (adouble));
	break;
      }

    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bint;

	ret = numeric_coerce_num_to_bigint (db_locate_numeric (src), DB_VALUE_SCALE (src), &bint);
	if (ret != NO_ERROR)
	  {
	    return ER_FAILED;
	  }

	if (!numeric_is_fraction_part_zero (db_locate_numeric (src), DB_VALUE_SCALE (src)))
	  {
	    return ER_FAILED;
	  }
	db_make_bigint (dest, bint);
	break;
      }

    case DB_TYPE_SMALLINT:
      {
	double adouble;
	numeric_coerce_num_to_double (db_locate_numeric (src), DB_VALUE_SCALE (src), &adouble);
	if (OR_CHECK_SHORT_OVERFLOW (adouble))
	  {
	    return ER_FAILED;
	  }
	if (!numeric_is_fraction_part_zero (db_locate_numeric (src), DB_VALUE_SCALE (src)))
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
  //char temp[80];
//  int ret = NO_ERROR;
//  char *temp; // 130(127 + sign + dot + \0)
  char temp[NUMERIC_MAX_STRING_SIZE];	// 300, 최대 정수(38 + 84) + 최대 소수 (38 + 127) + 여유 13자리
  int nbuf;
  int temp_size;
  int i;
  bool found_first_non_zero = false;
//  int prec = db_value_precision (val);
  int scale = db_value_scale (val);
//  int eff_scale, temp_len;

  /* it should not be static because the parameter could be changed without broker restart */
  bool oracle_compat_number = prm_get_bool_value (PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR);

  assert (val != NULL && buf != NULL);

  if (DB_IS_NULL (val))
    {
      buf[0] = '\0';
      return buf;
    }

//   eff_scale = scale > 0 ? scale : 0;
//   temp_len  = prec + eff_scale;       // 숫자 자릿수
//   temp = (char *) malloc (temp_len + 1);  // +1 for '\0'
//   if (temp == NULL)
//     {
//       er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 0);
//       buf[0] = '\0';
//       return buf;
//     }

  /* Retrieve raw decimal string */
  numeric_coerce_num_to_dec_str (db_get_numeric (val), temp);

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

  /* 음수 scale 처리: 소수점 오른쪽에 0 추가 */
  if (scale < 0)
    {
      int abs_scale = -scale;	// scale의 절대값

      if (!found_first_non_zero)
	{
	  // 숫자가 없으면 0 하나만 출력
	  buf[0] = '0';
	  buf[1] = '\0';
	  nbuf = 1;
	}
      else
	{
	  // 숫자가 있으면 abs_scale만큼 0 추가
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

/*
 * numeric_db_value_increase () -
 *   return: NO_ERROR or Error status
 *   arg(in)    : DB_VALUE of type DB_NUMERIC
 *
 * Note: This routine increments a numeric value.
 *
 */
int
numeric_db_value_increase (DB_VALUE * arg)
{
  /* Check for bad inputs */
  if (DB_IS_NULL (arg) || DB_VALUE_TYPE (arg) != DB_TYPE_NUMERIC)
    {
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  numeric_increase (db_get_numeric (arg));

  return NO_ERROR;
}
