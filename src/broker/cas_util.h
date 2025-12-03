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
 * cas_util.h - Utility functions shared by CAS and CGW
 * 
 * This file contains declarations for:
 * - Basic utility functions (IP, string, time operations)
 * - Type conversion functions (DB type to CAS type)
 * - SQL parsing functions
 * - Error handling functions
 * - Protocol utility functions
 */

#ifndef	_CAS_UTIL_H_
#define	_CAS_UTIL_H_

#ident "$Id$"

#include "cas_common.h"

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#endif

#include "broker_cas_cci.h"
#include "cas_common_vars.h"
#include "cas_handle.h"
#include "cas_error.h"

#define ut_trim  trim

extern char *ut_uchar2ipstr (unsigned char *ip_addr);
extern void ut_tolower (char *str);
extern void ut_timeval_diff (struct timeval *start, struct timeval *end, int *res_sec, int *res_msec);
extern int ut_check_timeout (struct timeval *start_time, struct timeval *end_time, int timeout_msec, int *res_sec,
			     int *res_msec);

typedef enum
{
  STMT_NONE_TOKENS,
  SQL_STYLE_COMMENT,
  C_STYLE_COMMENT,
  CPP_STYLE_COMMENT,
  SINGLE_QUOTED_STRING,
  DOUBLE_QUOTED_STRING
} STATEMENT_STATUS;

extern const char *get_schema_type_str (int schema_type);
extern const char *get_tran_type_str (int tran_type);

#endif /* _CAS_UTIL_H_ */
