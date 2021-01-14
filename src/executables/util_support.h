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
 * util_support.h - header file for common routines
 */

#ifndef _UTIL_SUPPORT_H_
#define _UTIL_SUPPORT_H_

#ident "$Id$"

#include "utility.h"

extern char *utility_make_getopt_optstring (const GETOPT_LONG * opt_array, char *buf);

extern int utility_load_library (DSO_HANDLE * handle, const char *lib_path);
extern int utility_load_symbol (DSO_HANDLE library_handle, DSO_HANDLE * symbol_handle, const char *symbol_name);
extern void utility_load_print_error (FILE * fp);
extern int util_parse_argument (UTIL_MAP * util_map, int argc, char **argv);
extern void util_hide_password (char *arg);


#endif /* _UTIL_SUPPORT_H_ */
