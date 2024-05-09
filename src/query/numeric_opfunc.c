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
#define TWICE_NUM_MAX_PREC	(2*DB_MAX_NUMERIC_PRECISION)
#define SECONDS_IN_A_DAY	  (int)(24L * 60L * 60L)

#define ROUND(x)                  ((x) > 0 ? ((x) + .5) : ((x) - .5))

typedef struct dec_string DEC_STRING;
struct dec_string
{
  char digits[TWICE_NUM_MAX_PREC];
};

static const char fast_mod[20] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

static DEC_STRING powers_of_2[DB_NUMERIC_BUF_SIZE * 16];
#if !defined(SERVER_MODE)
static bool initialized_2 = false;
#endif
static unsigned char powers_of_10[TWICE_NUM_MAX_PREC + 1][DB_NUMERIC_BUF_SIZE];
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
  int i;

  numeric_zero (powers_of_10[0], DB_NUMERIC_BUF_SIZE);

  /* Set first element to 1 */
  powers_of_10[0][DB_NUMERIC_BUF_SIZE - 1] = 1;

  /* Loop through elements setting each one to 10 times the prior */
  for (i = 1; i < TWICE_NUM_MAX_PREC + 1; i++)
    {
      numeric_scale_dec (powers_of_10[i - 1], 1, powers_of_10[i]);
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
      return (numeric_compare_pos (narg, numeric_get_pow_of_10 (exp)) >= 0) ? true : false;
    }
  else
    {
      return (numeric_compare_pos (arg, numeric_get_pow_of_10 (exp)) >= 0) ? true : false;
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
    {
      numeric_copy (dest, &(src[DB_NUMERIC_BUF_SIZE]));
      *dest_prec = src_prec;
      *dest_scale = src_scale;
    }

  /* The remaining cases are for when the precision of the source overflows. */

  /* Case 1: The scale of the source does *not* overflow */
  else if (src_scale <= DB_MAX_NUMERIC_PRECISION)
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
numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer)
{
  DB_VALUE dbv1_common, dbv2_common;
  int ret = NO_ERROR;
  unsigned int prec;
  unsigned char temp[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  TP_DOMAIN *domain;

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
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }
  db_make_numeric (answer, temp, prec, DB_VALUE_SCALE (&dbv1_common));

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }
  db_make_numeric (answer, temp, prec, DB_VALUE_SCALE (&dbv1_common));

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
  bool positive_ans;
  unsigned char temp[2 * DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */
  unsigned char result[DB_NUMERIC_BUF_SIZE];	/* Copy of a DB_C_NUMERIC */

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

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
	  ret = ER_IT_DATA_OVERFLOW;
	  goto exit_on_error;
	}
    }

  db_make_numeric (answer, temp_quo, prec, scale);

  return ret;

exit_on_error:

  db_value_domain_init (answer, DB_TYPE_NUMERIC, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);

  return (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
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
  char dec_str[DB_MAX_NUMERIC_PRECISION * 4];
  char new_dec_num[DB_MAX_NUMERIC_PRECISION + 1];
  int i = 0;

  /* the number of digits of the result */
  const int res_num_digits = src_prec - src_scale;

  assert (src_prec - src_scale <= dst_prec);
  assert (num != dest);

  numeric_zero (dest, DB_NUMERIC_BUF_SIZE);
  memset (new_dec_num, 0, DB_MAX_NUMERIC_PRECISION + 1);

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
  char dec_str[DB_MAX_NUMERIC_PRECISION * 4];
  char new_dec_num[DB_MAX_NUMERIC_PRECISION + 1];
  int i = 0;

  assert (src_scale <= dst_scale);
  assert (num != dest);

  numeric_zero (dest, DB_NUMERIC_BUF_SIZE);
  memset (new_dec_num, 0, DB_MAX_NUMERIC_PRECISION + 1);

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
  char dec_str[(2 * DB_MAX_NUMERIC_PRECISION) + 4];
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
  char numeric_str[MAX (TP_DOUBLE_AS_CHAR_LENGTH + 1, DB_MAX_NUMERIC_PRECISION + 4)];
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
  char num_string[TWICE_NUM_MAX_PREC + 1];
  unsigned char num[DB_NUMERIC_BUF_SIZE];
  int i;
  int prec = 0;
  int scale = 0;
  bool leading_zeroes = true;
  bool sign_found = false;
  bool negate_value = false;
  bool pad_character_zero = false;
  bool trailing_spaces = false;
  bool decimal_part = false;
  int ret = NO_ERROR;
  int skip_size = 1;
  TP_DOMAIN *domain;

  /* Remove the decimal point, track the prec & scale */
  prec = 0;
  scale = 0;
  for (i = 0; i < astring_length && ret == NO_ERROR; i += skip_size)
    {
      skip_size = 1;
      if (astring[i] == '.')
	{
	  leading_zeroes = false;
	  decimal_part = true;
	  scale = astring_length - (i + 1);
	}
      else if (leading_zeroes)
	{			/* Look for 1st digit between 1 & 9 */
	  if (astring[i] >= '1' && astring[i] <= '9')
	    {
	      leading_zeroes = false;
	      num_string[prec] = astring[i];
	      if (++prec > DB_MAX_NUMERIC_PRECISION)
		{
		  break;
		}
	    }
	  else if (astring[i] == '+' || astring[i] == '-')
	    {			/* sign found */
	      if (!sign_found)
		{
		  sign_found = true;
		  if (astring[i] == '-')
		    {
		      negate_value = true;
		    }
		}
	      else
		{		/* Duplicate sign characters */
		  ret = DOMAIN_INCOMPATIBLE;
		}
	    }
	  else if (astring[i] == '0')
	    {
	      /* leading pad '0' found */
	      pad_character_zero = true;
	    }
	  else if (intl_is_space (astring + i, NULL, codeset, &skip_size))
	    {
	      /* Just skip this.  OK to have leading spaces */
	      ;
	    }
	  else
	    {
	      /* Stray Non-numeric compatible character */
	      ret = DOMAIN_INCOMPATIBLE;
	    }
	}
      else
	{
	  /* Only space character should be allowed on trailer. If the first space character is shown after digits, we
	   * consider it as the beginning of trailer. */
	  if (trailing_spaces && !intl_is_space (astring + i, NULL, codeset, &skip_size))
	    {
	      ret = DOMAIN_INCOMPATIBLE;
	    }
	  else if (intl_is_space (astring + i, NULL, codeset, &skip_size))
	    {
	      if (!trailing_spaces)
		{
		  trailing_spaces = true;
		}
	      /* Decrease scale if decimal part exists. */
	      scale -= skip_size;
	      if (scale < 0)
		{
		  scale = 0;
		}
	    }
	  else if (astring[i] == ',')
	    {
	      /* Accept ',' character on integer part. */
	      if (decimal_part)
		{
		  ret = DOMAIN_INCOMPATIBLE;
		}
	    }
	  else if (astring[i] >= '0' && astring[i] <= '9')
	    {
	      num_string[prec] = astring[i];
	      if (++prec > DB_MAX_NUMERIC_PRECISION)
		{
		  break;
		}
	    }
	  else
	    {
	      /* Characters excluding digit, space and comma are not acceptable. */
	      ret = DOMAIN_INCOMPATIBLE;
	    }
	}
    }

  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  /* If there is no overflow, try to parse the decimal string */
  if (prec > DB_MAX_NUMERIC_PRECISION)
    {
      domain = tp_domain_resolve_default (DB_TYPE_NUMERIC);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (TP_DOMAIN_TYPE (domain)));
      ret = ER_IT_DATA_OVERFLOW;
      goto exit_on_error;
    }

  if (prec == 0 && pad_character_zero)
    {
      prec = 1;
      num_string[0] = '0';
      num_string[prec] = '\0';
      numeric_coerce_dec_str_to_num (num_string, num);
    }
  else
    {
      num_string[prec] = '\0';
      numeric_coerce_dec_str_to_num (num_string, num);
    }

  /* Make the return value */
  if (negate_value)
    {
      numeric_negate (num);
    }
  db_make_numeric (result, num, prec, scale);

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
  char num_string[DB_MAX_NUMERIC_PRECISION * 4];
  int scale_diff;
  int orig_length;
  int i, len;
  bool round_up = false;
  bool negate_answer;

  if (src_num == NULL)
    {
      return ER_FAILED;
    }

  /* Check for trivial case */
  if (src_prec <= dest_prec && src_scale == dest_scale)
    {
      numeric_copy (dest_num, src_num);
      return NO_ERROR;
    }

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
  if (src_scale < dest_scale)
    {				/* add trailing zeroes */
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

  return ret;

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
  char temp[80];
  int nbuf;
  int temp_size;
  int i;
  bool found_first_non_zero = false;
  int scale = db_value_scale (val);

  /* it should not be static because the parameter could be changed without broker restart */
  bool oracle_compat_number = prm_get_bool_value (PRM_ID_ORACLE_COMPAT_NUMBER_BEHAVIOR);

  assert (val != NULL && buf != NULL);

  if (DB_IS_NULL (val))
    {
      buf[0] = '\0';
      return buf;
    }

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
