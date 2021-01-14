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
