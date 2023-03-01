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

typedef enum
{
  DATA_STATUS_OK = 0,		/* Operation proceeded without error */
  DATA_STATUS_TRUNCATED = 1004,	/* Operation caused truncation */
  DATA_STATUS_NOT_CONSUMED = 1005	/* Operation not consumed all input */
} DB_DATA_STATUS;

#define NUMERIC_MAX_STRING_SIZE (80 + 1)

#define SECONDS_OF_ONE_DAY      86400	/* 24 * 60 * 60 */
#define MILLISECONDS_OF_ONE_DAY 86400000	/* 24 * 60 * 60 * 1000 */

#define db_locate_numeric(value) ((DB_C_NUMERIC) ((value)->data.num.d.buf))

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
extern int numeric_db_value_to_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num (DB_VALUE * src, DB_VALUE * dest, DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num_strict (DB_VALUE * src, DB_VALUE * dest);
extern char *numeric_db_value_print (const DB_VALUE * val, char *buf);

/* Testing Routines */
extern bool numeric_db_value_is_zero (const DB_VALUE * arg);

extern int numeric_db_value_is_positive (const DB_VALUE * arg);
#endif /* _NUMERIC_OPFUNC_H_ */
