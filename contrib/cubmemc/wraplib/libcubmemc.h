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

#ifndef _LIBCUBMEMC_H_
#define _LIBCUBMEMC_H_

#include "dbi.h"

/* Meta data attribute name */
#define CACHE_ATTR_SERVER_LIST "server_list"
#define CACHE_ATTR_BEHAVIOR "behavior"

/* Maximum length of cache class string attribute */
#define CACHE_ATTR_SERVER_LIST_MAX_LEN  1024
#define CACHE_ATTR_BEHAVIOR_MAX_LEN     1024

/* 
 * Error code range allocation
 *
 * [1000, ~)          : libcubmemcached
 * (0, 1000)          : libmemcached
 * [0,0]              : success
 * (ER_LAST_ERROR, 0) : CUBRID (defined in dbi.h)
 */

#define CUBMEMC_IS_ERROR(r) ((r) != 0)
typedef enum
{
  CUBMEMC_OK = 0,
  /* Start indicator */
  CUBMEMC_ERROR_START = 1000,
  CUBMEMC_ERROR_NOT_IMPLEMENTED = CUBMEMC_ERROR_START,
  CUBMEMC_ERROR_INVALID_ARG,
  CUBMEMC_ERROR_OUT_OF_MEMORY,
  CUBMEMC_ERROR_NOT_A_CLASS,
  CUBMEMC_ERROR_SERVER_LIST,
  CUBMEMC_ERROR_BEHAVIOR,
  CUBMEMC_ERROR_SERVER_LIST_FORMAT,
  CUBMEMC_ERROR_BEHAVIOR_FORMAT,
  CUBMEMC_ERROR_CAS_OUT_OF_RANGE,
  CUBMEMC_ERROR_INVALID_VALUE_TYPE,
  CUBMEMC_ERROR_INTERNAL,
  /* End indicator */
  CUBMEMC_ERROR_END = CUBMEMC_ERROR_INTERNAL
} CUBMEMC_ERROR;

/* 
 * flags field in memcached storage 
 * Note: Any program which stores/retrieves memcached data should set/interpret
 *   flags field as defined below.
 */
typedef enum
{
  VALUE_TYPE_NULL = 0,
  VALUE_TYPE_ASIS = VALUE_TYPE_NULL,	/* internal use */
  VALUE_TYPE_STRING = 1,
  VALUE_TYPE_BINARY = 2,
} CUBMEMC_VALUE_TYPE;

/* Exported function declarations */
extern void
cubmemc_strerror (DB_OBJECT * obj, DB_VALUE * return_arg,
		  DB_VALUE * error_code);

extern void
cubmemc_get_string (DB_OBJECT * obj, DB_VALUE * return_arg, DB_VALUE * key);

extern void
cubmemc_get_binary (DB_OBJECT * obj, DB_VALUE * return_arg, DB_VALUE * key);

extern void
cubmemc_set (DB_OBJECT * obj, DB_VALUE * return_arg,
	     DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration);

extern void
cubmemc_delete (DB_OBJECT * obj, DB_VALUE *
		return_arg, DB_VALUE * key, DB_VALUE * expiration);

extern void cubmemc_add (DB_OBJECT * obj, DB_VALUE * return_arg,
			 DB_VALUE * key,
			 DB_VALUE * value, DB_VALUE * expiration);

extern void
cubmemc_replace (DB_OBJECT * obj, DB_VALUE * return_arg, DB_VALUE * key,
		 DB_VALUE * value, DB_VALUE * expiration);
extern void
cubmemc_append (DB_OBJECT * obj, DB_VALUE * return_arg,
		DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration);

extern void
cubmemc_prepend (DB_OBJECT * obj, DB_VALUE * return_arg,
		 DB_VALUE * key, DB_VALUE * value, DB_VALUE * expiration);

extern void
cubmemc_increment (DB_OBJECT * obj, DB_VALUE * return_arg,
		   DB_VALUE * key, DB_VALUE * offset);
extern void
cubmemc_decrement (DB_OBJECT * obj, DB_VALUE * return_arg,
		   DB_VALUE * key, DB_VALUE * offset);
#endif
