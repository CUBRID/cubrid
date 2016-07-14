/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#include "dbtype.h"
#include "error_manager.h"

typedef enum
{
  DATA_STATUS_OK = 0,		/* Operation proceeded without error */
  DATA_STATUS_TRUNCATED = 1004,	/* Operation caused truncation */
  DATA_STATUS_NOT_CONSUMED = 1005	/* Operation not consumed all input */
} DB_DATA_STATUS;

#define NUMERIC_MAX_STRING_SIZE (80 + 1)

#if defined(SERVER_MODE)
extern void numeric_init_power_value_string (void);
#endif

/* Arithmetic routines */
extern int numeric_db_value_add (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_sub (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_mul (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_div (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_negate (DB_VALUE * answer);
extern void numeric_db_value_abs (DB_C_NUMERIC src_num, DB_C_NUMERIC dest_num);
extern int numeric_db_value_increase (DB_VALUE * arg);

/* Comparison routines */
extern int numeric_db_value_compare (const DB_VALUE * dbv1, const DB_VALUE * dbv2, DB_VALUE * answer);

/* Coercion routines */
extern void numeric_coerce_int_to_num (int arg, DB_C_NUMERIC answer);
extern void numeric_coerce_bigint_to_num (DB_BIGINT arg, DB_C_NUMERIC answer);
extern void numeric_coerce_num_to_int (DB_C_NUMERIC arg, int *answer);
extern int numeric_coerce_num_to_bigint (DB_C_NUMERIC arg, int scale, DB_BIGINT * answer);

extern void numeric_coerce_dec_str_to_num (const char *dec_str, DB_C_NUMERIC result);
extern void numeric_coerce_num_to_dec_str (DB_C_NUMERIC num, char *dec_str);

extern void numeric_coerce_num_to_double (DB_C_NUMERIC num, int scale, double *adouble);
extern int numeric_internal_double_to_num (double adouble, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale);
extern int numeric_internal_float_to_num (float afloat, int dst_scale, DB_C_NUMERIC num, int *prec, int *scale);

#if defined (ENABLE_UNUSED_FUNCTION)
extern int numeric_coerce_double_to_num (double adouble, DB_C_NUMERIC num, int *prec, int *scale);
#endif

extern int numeric_coerce_string_to_num (const char *astring, int astring_len, INTL_CODESET codeset, DB_VALUE * num);

extern int numeric_coerce_num_to_num (DB_C_NUMERIC src_num, int src_prec, int src_scale, int dest_prec, int dest_scale,
				      DB_C_NUMERIC dest_num);

extern int numeric_db_value_coerce_to_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num_strict (DB_VALUE * src, DB_VALUE * dest);
extern char *numeric_db_value_print (DB_VALUE * val, char *buf);

/* Testing Routines */
extern bool numeric_db_value_is_zero (const DB_VALUE * arg);

extern int numeric_db_value_is_positive (const DB_VALUE * arg);
#endif /* _NUMERIC_OPFUNC_H_ */
