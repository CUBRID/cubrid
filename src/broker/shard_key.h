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
 * shard_key.h -
 */

#ifndef	_SHARD_KEY_H_
#define	_SHARD_KEY_H_

#ident "$Id$"

#ifdef __cplusplus
extern "C"
{
#endif

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

  typedef enum
  {
    SHARD_U_TYPE_FIRST = 0,
    SHARD_U_TYPE_UNKNOWN = 0,
    SHARD_U_TYPE_NULL = 0,

    SHARD_U_TYPE_CHAR = 1,
    SHARD_U_TYPE_STRING = 2,
    SHARD_U_TYPE_NCHAR = 3,
    SHARD_U_TYPE_VARNCHAR = 4,
    SHARD_U_TYPE_BIT = 5,
    SHARD_U_TYPE_VARBIT = 6,
    SHARD_U_TYPE_NUMERIC = 7,
    SHARD_U_TYPE_INT = 8,
    SHARD_U_TYPE_SHORT = 9,
    SHARD_U_TYPE_MONETARY = 10,
    SHARD_U_TYPE_FLOAT = 11,
    SHARD_U_TYPE_DOUBLE = 12,
    SHARD_U_TYPE_DATE = 13,
    SHARD_U_TYPE_TIME = 14,
    SHARD_U_TYPE_TIMESTAMP = 15,
    SHARD_U_TYPE_SET = 16,
    SHARD_U_TYPE_MULTISET = 17,
    SHARD_U_TYPE_SEQUENCE = 18,
    SHARD_U_TYPE_OBJECT = 19,
    SHARD_U_TYPE_RESULTSET = 20,
    SHARD_U_TYPE_BIGINT = 21,
    SHARD_U_TYPE_DATETIME = 22,
    SHARD_U_TYPE_BLOB = 23,
    SHARD_U_TYPE_CLOB = 24,
    SHARD_U_TYPE_ENUM = 25,
    SHARD_U_TYPE_USHORT = 26,
    SHARD_U_TYPE_UINT = 27,
    SHARD_U_TYPE_UBIGINT = 28,

    SHARD_U_TYPE_LAST = SHARD_U_TYPE_UBIGINT
  } T_SHARD_U_TYPE;

#define ERROR_ON_ARGUMENT	-1
#define ERROR_ON_MAKE_SHARD_KEY	-2

/*
   return value :
	success - shard key id(>0)
	fail 	- invalid argument(ERROR_ON_ARGUMENT), shard key id make fail(ERROR_ON_MAKE_SHARD_KEY)
   type 	: shard key value type
   val 		: shard key value
 */
  typedef int (*FN_GET_SHARD_KEY) (const char *shard_key, T_SHARD_U_TYPE type, const void *val, int val_size);

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#ifdef __cplusplus
}
#endif

#endif				/* _SHARD_KEY_H_ */
