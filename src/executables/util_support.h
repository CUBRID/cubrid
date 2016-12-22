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
