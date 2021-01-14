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
 * unicode_support.c : Unicode characters support
 */
#ifndef _UNICODE_SUPPORT_H_
#define _UNICODE_SUPPORT_H_

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>


typedef unsigned int uint32;
typedef unsigned short int uint16;
typedef unsigned char uchar;

#ifdef __cplusplus
extern "C"
{
#endif
  extern int unicode_process_alphabet (LOCALE_DATA * ld, bool is_verbose);
  extern int unicode_process_normalization (LOCALE_DATA * ld, bool is_verbose);
  extern void unicode_free_data (void);
#if !defined (SERVER_MODE)
  extern bool unicode_string_need_compose (const char *str_in, const int size_in, int *size_out,
					   const UNICODE_NORMALIZATION * norm);
  extern void unicode_compose_string (const char *str_in, const int size_in, char *str_out, int *size_out,
				      bool * is_composed, const UNICODE_NORMALIZATION * norm);
  extern bool unicode_string_need_decompose (const char *str_in, const int size_in, int *decomp_size,
					     const UNICODE_NORMALIZATION * norm);
  extern void unicode_decompose_string (const char *str_in, const int size_in, char *str_out, int *size_out,
					const UNICODE_NORMALIZATION * norm);
#endif
  extern int string_to_int_array (char *s, uint32 * cp_list, const int cp_list_size, const char *delims);

#ifdef __cplusplus
}
#endif

#endif				/* _UNICODE_SUPPORT_H_ */
