/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * Typedef's and defines for arithmetic package that handles
 * extended precision integers
 */

#ifndef _QP_NUM_H_
#define _QP_NUM_H_

#ident "$Id$"

#include "config.h"

#include "dbtype.h"
#include "error_manager.h"

typedef enum
{
  DATA_STATUS_OK = 0,		/* Operation proceeded without error */
  DATA_STATUS_TRUNCATED = 1004	/* Operation caused truncation */
} DB_DATA_STATUS;

#if defined(SERVER_MODE)
extern void numeric_init_power_value_string (void);
#endif

/* Arithmetic routines */
extern int numeric_db_value_add (DB_VALUE * dbv1,
				 DB_VALUE * dbv2, DB_VALUE * answer);
extern int numeric_db_value_sub (DB_VALUE * dbv1, DB_VALUE * dbv2,
				 DB_VALUE * answer);
extern int numeric_db_value_mul (DB_VALUE * dbv1, DB_VALUE * dbv2,
				 DB_VALUE * answer);
extern int numeric_db_value_div (DB_VALUE * dbv1, DB_VALUE * dbv2,
				 DB_VALUE * answer);
extern int numeric_db_value_negate (DB_VALUE * answer);

extern void numeric_db_value_abs (DB_C_NUMERIC src_num,
				  DB_C_NUMERIC dest_num);

/* Comparison routines */
extern int numeric_db_value_compare (DB_VALUE * dbv1,
				     DB_VALUE * dbv2, DB_VALUE * answer);

/* Coercion routines */
extern void numeric_coerce_int_to_num (int arg, DB_C_NUMERIC answer);
extern void numeric_coerce_num_to_int (DB_C_NUMERIC arg, int *answer);

extern void numeric_coerce_dec_str_to_num (const char *dec_str,
					   DB_C_NUMERIC result);
extern void numeric_coerce_num_to_dec_str (DB_C_NUMERIC num, char *dec_str);

extern void numeric_coerce_num_to_double (DB_C_NUMERIC num,
					  int scale, double *adouble);
extern int numeric_internal_double_to_num (double adouble,
					   int dst_scale,
					   DB_C_NUMERIC num,
					   int *prec, int *scale);
extern int numeric_coerce_double_to_num (double adouble,
					 DB_C_NUMERIC num, int *prec,
					 int *scale);

extern int numeric_coerce_string_to_num (const char *astring, DB_VALUE * num);

extern int numeric_coerce_num_to_num (DB_C_NUMERIC src_num,
				      int src_prec,
				      int src_scale,
				      int dest_prec,
				      int dest_scale, DB_C_NUMERIC dest_num);

extern int numeric_db_value_coerce_to_num (DB_VALUE * src,
					   DB_VALUE * dest,
					   DB_DATA_STATUS * data_stat);
extern int numeric_db_value_coerce_from_num (DB_VALUE * src,
					     DB_VALUE * dest,
					     DB_DATA_STATUS * data_stat);
extern char *numeric_db_value_print (DB_VALUE * val);

/* Testing Routines */
extern bool numeric_db_value_is_zero (const DB_VALUE * arg);

#endif /* _QP_NUM_H_ */
