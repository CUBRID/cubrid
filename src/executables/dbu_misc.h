/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * dbu_misc.h - header file for common routines
 */

#ifndef _DBU_MISC_H_
#define _DBU_MISC_H_

#ident "$Id$"

#include "utility.h"

extern char *utility_make_getopt_optstring (const GETOPT_LONG * opt_array,
					    char *buf);

extern int utility_load_library (DSO_HANDLE * handle, const char *lib_path);
extern int utility_load_symbol (DSO_HANDLE library_handle,
				DSO_HANDLE * symbol_handle,
				const char *symbol_name);
extern void utility_load_print_error (FILE * fp);
extern int util_parse_argument (UTIL_MAP *util_map, int argc, char **argv);


#endif /* _DBU_MISC_H_ */
