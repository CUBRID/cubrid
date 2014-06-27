/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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
  typedef int (*FN_GET_SHARD_KEY) (const char *shard_key, T_SHARD_U_TYPE type,
				   const void *val, int val_size);

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#ifdef __cplusplus
}
#endif

#endif				/* _SHARD_KEY_H_ */
