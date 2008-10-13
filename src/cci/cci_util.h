/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * cci_util.h -
 */

#ifndef	_CCI_UTIL_H_
#define	_CCI_UTIL_H_

#ident "$Id$"

#if defined(CAS) || defined(CAS_BROKER)
#error include error
#endif

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

#include "cci_handle_mng.h"

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/


/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern int ut_str_to_int (char *str, int *value);
extern int ut_str_to_float (char *str, float *value);
extern int ut_str_to_double (char *str, double *value);
extern int ut_str_to_date (char *str, T_CCI_DATE * value);
extern int ut_str_to_time (char *str, T_CCI_DATE * value);
extern int ut_str_to_timestamp (char *str, T_CCI_DATE * value);
extern int ut_str_to_oid (char *str, T_OBJECT * value);
extern void ut_int_to_str (int value, char *str);
extern void ut_float_to_str (float value, char *str);
extern void ut_double_to_str (double value, char *str);
extern void ut_date_to_str (T_CCI_DATE * value, T_CCI_U_TYPE u_type,
			    char *str);
extern void ut_oid_to_str (T_OBJECT * oid, char *str);
extern void ut_bit_to_str (char *bit_str, int size, char *str);
extern int ut_is_deleted_oid (T_OBJECT * oid);

#ifdef UNICODE_DATA
extern char *ut_ansi_to_unicode (char *str);
extern char *ut_unicode_to_ansi (char *str);
#endif


/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_UTIL_H_ */
