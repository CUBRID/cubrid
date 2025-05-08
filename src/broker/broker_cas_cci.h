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
 * broker_cas_cci.h -
 *
 * CAUTION!
 *
 * In case of common,  
 * cci repository source (src/cci/cas_cci.h) must be updated,
 * becuase CCI source and Engine source have been separated.
 */

#ifndef	_BROKER_CAS_CCI_H_
#define	_BROKER_CAS_CCI_H_

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include <stdlib.h>

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/
#include "cas_error.h"
#include "dbtran_def.h"

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/


#define CCI_GET_COLLECTION_DOMAIN(TYPE)	(((CCI_TYPE_BIT7_MASK & (TYPE)) >> 2) \
  | ((TYPE) & CCI_TYPE_LSB_MASK))

#define CCI_TRAN_COMMIT			1
#define CCI_TRAN_ROLLBACK		2

#define CCI_PREPARE_INCLUDE_OID		0x01
#define CCI_PREPARE_UPDATABLE		0x02
#define CCI_PREPARE_QUERY_INFO          0x04
#define CCI_PREPARE_HOLDABLE		0x08
#define CCI_PREPARE_XASL_CACHE_PINNED 	0x10
#define CCI_PREPARE_CALL		0x40

#define CCI_EXEC_ASYNC			0x01	/* obsoleted */
#define CCI_EXEC_QUERY_ALL		0x02
#define CCI_EXEC_QUERY_INFO		0x04
#define CCI_EXEC_ONLY_QUERY_PLAN        0x08
#define CCI_EXEC_THREAD			0x10
#define CCI_EXEC_NOT_USED		0x20	/* not currently used */
#define CCI_EXEC_RETURN_GENERATED_KEYS	0x40

#define CCI_FETCH_SENSITIVE		1

#define CCI_CLASS_NAME_PATTERN_MATCH	1
#define CCI_ATTR_NAME_PATTERN_MATCH	2

/* encoding of composed type :
 * TCCT TTTT  (T - set type bits ; C - collection bits) */
#define CCI_TYPE_BIT7_MASK		0x80
#define CCI_TYPE_LSB_MASK		0x1f

#define CCI_CODE_SET			0x20
#define CCI_CODE_MULTISET		0x40
#define CCI_CODE_SEQUENCE		0x60
#define CCI_CODE_COLLECTION		0x60

#define CCI_CLOSE_CURRENT_RESULT	0
#define CCI_KEEP_CURRENT_RESULT		1
/* schema_info CONSTRAINT */
#define CCI_CONSTRAINT_TYPE_UNIQUE	0
#define CCI_CONSTRAINT_TYPE_INDEX	1

#define CCI_TZ_SIZE 63


typedef enum
{
  CCI_NO_BACKSLASH_ESCAPES_FALSE = -1,
  CCI_NO_BACKSLASH_ESCAPES_TRUE = -2,
  CCI_NO_BACKSLASH_ESCAPES_NOT_SET = -3
} CCI_NO_BACKSLASH_ESCAPES_MODE;

/* todo: T_CCI_U_TYPE duplicates db types. */
typedef enum
{
  CCI_U_TYPE_FIRST = 0,
  CCI_U_TYPE_UNKNOWN = 0,
  CCI_U_TYPE_NULL = 0,

  CCI_U_TYPE_CHAR = 1,
  CCI_U_TYPE_STRING,
#if 1
  /* TODO:
   * We need to keep it until we remove U_TYPE_VARNCHAR and U_TYPE_NCHAR from UUType.java.
   */
  CCI_U_TYPE_NCHAR,
  CCI_U_TYPE_VARNCHAR,
#endif
  CCI_U_TYPE_BIT,
  CCI_U_TYPE_VARBIT,
  CCI_U_TYPE_NUMERIC,
  CCI_U_TYPE_INT,
  CCI_U_TYPE_SHORT,
  CCI_U_TYPE_MONETARY,
  CCI_U_TYPE_FLOAT,
  CCI_U_TYPE_DOUBLE,
  CCI_U_TYPE_DATE,
  CCI_U_TYPE_TIME,
  CCI_U_TYPE_TIMESTAMP,
  CCI_U_TYPE_SET,
  CCI_U_TYPE_MULTISET,
  CCI_U_TYPE_SEQUENCE,
  CCI_U_TYPE_OBJECT,
  CCI_U_TYPE_RESULTSET,
  CCI_U_TYPE_BIGINT,
  CCI_U_TYPE_DATETIME,
  CCI_U_TYPE_BLOB,
  CCI_U_TYPE_CLOB,
  CCI_U_TYPE_ENUM,
  CCI_U_TYPE_USHORT,
  CCI_U_TYPE_UINT,
  CCI_U_TYPE_UBIGINT,
  CCI_U_TYPE_TIMESTAMPTZ,
  CCI_U_TYPE_TIMESTAMPLTZ,
  CCI_U_TYPE_DATETIMETZ,
  CCI_U_TYPE_DATETIMELTZ,
  /* Disabled type */
  CCI_U_TYPE_TIMETZ,		/* internal use only - RESERVED */
  /* end of disabled types */
  CCI_U_TYPE_JSON,
  CCI_U_TYPE_LAST = CCI_U_TYPE_JSON
} T_CCI_U_TYPE;


typedef enum
{
  CCI_PARAM_FIRST = 1,
  CCI_PARAM_ISOLATION_LEVEL = 1,
  CCI_PARAM_LOCK_TIMEOUT = 2,
  CCI_PARAM_MAX_STRING_LENGTH = 3,
  CCI_PARAM_AUTO_COMMIT = 4,
  CCI_PARAM_LAST = CCI_PARAM_AUTO_COMMIT,

  /* below parameters are used internally */
  CCI_PARAM_NO_BACKSLASH_ESCAPES = 5
} T_CCI_DB_PARAM;

typedef enum
{
  CCI_SCH_FIRST = 1,
  CCI_SCH_CLASS = 1,
  CCI_SCH_VCLASS,
  CCI_SCH_QUERY_SPEC,
  CCI_SCH_ATTRIBUTE,
  CCI_SCH_CLASS_ATTRIBUTE,
  CCI_SCH_METHOD,
  CCI_SCH_CLASS_METHOD,
  CCI_SCH_METHOD_FILE,
  CCI_SCH_SUPERCLASS,
  CCI_SCH_SUBCLASS,
  CCI_SCH_CONSTRAINT,
  CCI_SCH_TRIGGER,
  CCI_SCH_CLASS_PRIVILEGE,
  CCI_SCH_ATTR_PRIVILEGE,
  CCI_SCH_DIRECT_SUPER_CLASS,
  CCI_SCH_PRIMARY_KEY,
  CCI_SCH_IMPORTED_KEYS,
  CCI_SCH_EXPORTED_KEYS,
  CCI_SCH_CROSS_REFERENCE,
  CCI_SCH_ATTR_WITH_SYNONYM,
  CCI_SCH_LAST = CCI_SCH_ATTR_WITH_SYNONYM
} T_CCI_SCH_TYPE;

#define CUBRID_STMT_CALL_SP	0x7e
#define CUBRID_STMT_UNKNOWN	0x7f
typedef enum
{
  CCI_OID_CMD_FIRST = 1,

  CCI_OID_DROP = 1,
  CCI_OID_IS_INSTANCE = 2,
  CCI_OID_LOCK_READ = 3,
  CCI_OID_LOCK_WRITE = 4,
  CCI_OID_CLASS_NAME = 5,

  CCI_OID_CMD_LAST = CCI_OID_CLASS_NAME
} T_CCI_OID_CMD;

typedef enum
{
  CCI_COL_CMD_FIRST = 1,
  CCI_COL_GET = 1,
  CCI_COL_SIZE = 2,
  CCI_COL_SET_DROP = 3,
  CCI_COL_SET_ADD = 4,
  CCI_COL_SEQ_DROP = 5,
  CCI_COL_SEQ_INSERT = 6,
  CCI_COL_SEQ_PUT = 7,
  CCI_COL_CMD_LAST = CCI_COL_SEQ_PUT
} T_CCI_COLLECTION_CMD;

typedef enum
{
  CCI_PARAM_MODE_UNKNOWN = 0,
  CCI_PARAM_MODE_IN = 1,
  CCI_PARAM_MODE_OUT = 2,
  CCI_PARAM_MODE_INOUT = 3
} T_CCI_PARAM_MODE;

/*
 * CAUTION!
 *
 * In case of common,  
 * cci repository source (src/cci/cas_cci.h) must be updated,
 * becuase CCI source and Engine source have been separated.
 */

#endif /* _BROKER_CAS_CCI_H_ */
