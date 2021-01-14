/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */


/*
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
#include "cci_t_lob.h"

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/
#define MAX_URL_MATCH_COUNT 6	/* sizeof (match_idx) / sizeof (int) - 1 */

#define MAKE_STRI(x) #x
#define MAKE_STR(x) MAKE_STRI(x)

/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/


/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

extern int ut_str_to_bigint (char *str, INT64 * value);
extern int ut_str_to_ubigint (char *str, UINT64 * value);
extern int ut_str_to_int (char *str, int *value);
extern int ut_str_to_uint (char *str, unsigned int *value);
extern int ut_str_to_float (char *str, float *value);
extern int ut_str_to_double (char *str, double *value);
extern int ut_str_to_date (char *str, T_CCI_DATE * value);
extern int ut_str_to_time (char *str, T_CCI_DATE * value);
extern int ut_str_to_timetz (char *str, T_CCI_DATE_TZ * value);
extern int ut_str_to_mtime (char *str, T_CCI_DATE * value);
extern int ut_str_to_timestamp (char *str, T_CCI_DATE * value);
extern int ut_str_to_timestamptz (char *str, T_CCI_DATE_TZ * value);
extern int ut_str_to_datetime (char *str, T_CCI_DATE * value);
extern int ut_str_to_datetimetz (char *str, T_CCI_DATE_TZ * value);
extern int ut_str_to_oid (char *str, T_OBJECT * value);
extern void ut_int_to_str (INT64 value, char *str, int size);
extern void ut_uint_to_str (UINT64 value, char *str, int size);
extern void ut_float_to_str (float value, char *str, int size);
extern void ut_double_to_str (double value, char *str, int size);
extern void ut_date_to_str (T_CCI_DATE * value, T_CCI_U_TYPE u_type, char *str, int size);
extern void ut_date_tz_to_str (T_CCI_DATE_TZ * value, T_CCI_U_TYPE u_type, char *str, int size);
extern void ut_oid_to_str (T_OBJECT * oid, char *str);
extern void ut_lob_to_str (T_LOB * lob, char *str, int size);
extern void ut_bit_to_str (char *bit_str, int bit_size, char *str, int str_size);
extern int ut_is_deleted_oid (T_OBJECT * oid);

extern int cci_url_match (const char *src, char *token[]);
extern long ut_timeval_diff_msec (struct timeval *start, struct timeval *end);

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#endif /* _CCI_UTIL_H_ */
