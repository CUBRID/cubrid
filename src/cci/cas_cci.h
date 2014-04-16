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
 * cas_cci.h -
 */

#ifndef	_CAS_CCI_H_
#define	_CAS_CCI_H_

#ifdef __cplusplus
extern "C"
{
#endif

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include <stdlib.h>

/************************************************************************
 * IMPORTED OTHER HEADER FILES						*
 ************************************************************************/
#include "cas_error.h"

/************************************************************************
 * EXPORTED DEFINITIONS							*
 ************************************************************************/

#define CCI_GET_RESULT_INFO_TYPE(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].type)

#define CCI_GET_RESULT_INFO_SCALE(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].scale)

#define CCI_GET_RESULT_INFO_PRECISION(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].precision)

#define CCI_GET_RESULT_INFO_NAME(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].col_name)

#define CCI_GET_RESULT_INFO_ATTR_NAME(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].real_attr)

#define CCI_GET_RESULT_INFO_CLASS_NAME(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].class_name)

#define CCI_GET_RESULT_INFO_IS_NON_NULL(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_non_null)

#define CCI_GET_RESULT_INFO_DEFAULT_VALUE(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].default_value)

#define CCI_GET_RESULT_INFO_IS_AUTO_INCREMENT(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_auto_increment)

#define CCI_GET_RESULT_INFO_IS_UNIQUE_KEY(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_unique_key)

#define CCI_GET_RESULT_INFO_IS_PRIMARY_KEY(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_primary_key)

#define CCI_GET_RESULT_INFO_IS_FOREIGN_KEY(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_foreign_key)

#define CCI_GET_RESULT_INFO_IS_REVERSE_INDEX(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_reverse_index)

#define CCI_GET_RESULT_INFO_IS_REVERSE_UNIQUE(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_reverse_unique)

#define CCI_GET_RESULT_INFO_IS_SHARED(RES_INFO, INDEX)	\
		(((T_CCI_COL_INFO*) (RES_INFO))[(INDEX) - 1].is_shared)

#define CCI_IS_SET_TYPE(TYPE)	\
	(((((TYPE) & CCI_CODE_COLLECTION) == CCI_CODE_SET) || ((TYPE) == CCI_U_TYPE_SET)) ? 1 : 0)

#define CCI_IS_MULTISET_TYPE(TYPE)	\
	(((((TYPE) & CCI_CODE_COLLECTION) == CCI_CODE_MULTISET) || ((TYPE) == CCI_U_TYPE_MULTISET)) ? 1 : 0)

#define CCI_IS_SEQUENCE_TYPE(TYPE)	\
	(((((TYPE) & CCI_CODE_COLLECTION) == CCI_CODE_SEQUENCE) || ((TYPE) == CCI_U_TYPE_SEQUENCE)) ? 1 : 0)

#define CCI_IS_COLLECTION_TYPE(TYPE)	\
	((((TYPE) & CCI_CODE_COLLECTION) || ((TYPE) == CCI_U_TYPE_SET) || ((TYPE) == CCI_U_TYPE_MULTISET) || ((TYPE) == CCI_U_TYPE_SEQUENCE)) ? 1 : 0)

#define CCI_GET_COLLECTION_DOMAIN(TYPE)	(~(CCI_CODE_COLLECTION) & (TYPE))

#define CCI_QUERY_RESULT_RESULT(QR, INDEX)	\
	(((T_CCI_QUERY_RESULT*) (QR))[(INDEX) - 1].result_count)

#define CCI_QUERY_RESULT_ERR_NO(QR, INDEX)	\
	(((T_CCI_QUERY_RESULT*) (QR))[(INDEX) - 1].err_no)

#define CCI_QUERY_RESULT_ERR_MSG(QR, INDEX)	\
	((((T_CCI_QUERY_RESULT*) (QR))[(INDEX) - 1].err_msg) == NULL ? "" : (((T_CCI_QUERY_RESULT*) (QR))[(INDEX) - 1].err_msg))

#define CCI_QUERY_RESULT_STMT_TYPE(QR, INDEX)	\
	(((T_CCI_QUERY_RESULT*) (QR))[(INDEX) - 1].stmt_type)

#define CCI_QUERY_RESULT_OID(QR, INDEX)	\
	(((T_CCI_QUERY_RESULT*) (QR))[(INDEX) - 1].oid)

#define CCI_GET_PARAM_INFO_MODE(PARAM_INFO, INDEX)	\
	(((T_CCI_PARAM_INFO*) (PARAM_INFO))[(INDEX) - 1].mode)
#define CCI_GET_PARAM_INFO_TYPE(PARAM_INFO, INDEX)	\
	(((T_CCI_PARAM_INFO*) (PARAM_INFO))[(INDEX) - 1].type)
#define CCI_GET_PARAM_INFO_SCALE(PARAM_INFO, INDEX)	\
	(((T_CCI_PARAM_INFO*) (PARAM_INFO))[(INDEX) - 1].scale)
#define CCI_GET_PARAM_INFO_PRECISION(PARAM_INFO, INDEX)	\
	(((T_CCI_PARAM_INFO*) (PARAM_INFO))[(INDEX) - 1].precision)

#define CCI_BIND_PTR			1

#define CCI_TRAN_COMMIT			1
#define CCI_TRAN_ROLLBACK		2

#define CCI_PREPARE_INCLUDE_OID		0x01
#define CCI_PREPARE_UPDATABLE		0x02
#define CCI_PREPARE_QUERY_INFO          0x04
#define CCI_PREPARE_HOLDABLE		0x08
#define CCI_PREPARE_CALL		0x40

#define CCI_EXEC_ASYNC			0x01
#define CCI_EXEC_QUERY_ALL		0x02
#define CCI_EXEC_QUERY_INFO		0x04
#define CCI_EXEC_ONLY_QUERY_PLAN        0x08
#define CCI_EXEC_THREAD			0x10
#define CCI_EXEC_NOT_USED		0x20	/* not currently used */
#define CCI_EXEC_RETURN_GENERATED_KEYS	0x40

#define CCI_FETCH_SENSITIVE		1

#define CCI_CLASS_NAME_PATTERN_MATCH	1
#define CCI_ATTR_NAME_PATTERN_MATCH	2

#define CCI_CODE_SET			0x20
#define CCI_CODE_MULTISET		0x40
#define CCI_CODE_SEQUENCE		0x60
#define CCI_CODE_COLLECTION		0x60

#define CCI_LOCK_TIMEOUT_INFINITE	-1
#define CCI_LOCK_TIMEOUT_DEFAULT	-2

#define CCI_LOGIN_TIMEOUT_INFINITE      (0)
#define CCI_LOGIN_TIMEOUT_DEFAULT       (30000)

#define CCI_CLOSE_CURRENT_RESULT	0
#define CCI_KEEP_CURRENT_RESULT		1

#define CCI_CONNECT_INTERNAL_FUNC_NAME	cci_connect_3_0
#define cci_connect(IP,PORT,DBNAME,DBUSER,DBPASSWD)	\
	CCI_CONNECT_INTERNAL_FUNC_NAME(IP,PORT,DBNAME,DBUSER,DBPASSWD)

/* schema_info CONSTRAINT */
#define CCI_CONSTRAINT_TYPE_UNIQUE	0
#define CCI_CONSTRAINT_TYPE_INDEX	1

/* shard */
#define CCI_SHARD_ID_INVALID 		(-1)
#define CCI_SHARD_ID_UNSUPPORTED	(-2)

#if defined(WINDOWS)
#define SSIZEOF(val) ((SSIZE_T) sizeof(val))
#else
#define SSIZEOF(val) ((ssize_t) sizeof(val))
#endif

#define CON_HANDLE_ID_FACTOR		1000000

#define GET_CON_ID(H) ((H) / CON_HANDLE_ID_FACTOR)
#define GET_REQ_ID(H) ((H) % CON_HANDLE_ID_FACTOR)
#define MAKE_REQ_ID(C,R) ((C) * CON_HANDLE_ID_FACTOR + (R))

/* database user */
#define CCI_DS_PROPERTY_USER				"user"
/* password for a specified user */
#define CCI_DS_PROPERTY_PASSWORD			"password"
/* database connection URL */
#define CCI_DS_PROPERTY_URL				"url"

/* number of connection that are borrowed */
#define CCI_DS_PROPERTY_POOL_SIZE			"pool_size"
/* number of connection that are created when the pool is started */
#define CCI_DS_PROPERTY_MAX_POOL_SIZE			"max_pool_size"
/* max wait msec for a connection to be returned, or -1 to wait indefinitely */
#define CCI_DS_PROPERTY_MAX_WAIT			"max_wait"
/* enable prepared statement pooling */
#define CCI_DS_PROPERTY_POOL_PREPARED_STATEMENT		"pool_prepared_statement"
/* max size of the prepared statement pool */
#define CCI_DS_PROPERTY_MAX_OPEN_PREPARED_STATEMENT     "max_open_prepared_statement"
/* timeout in msec for datebase login or -1 to wait indefinitely */
#define CCI_DS_PROPERTY_LOGIN_TIMEOUT			"login_timeout"
/* timeout in msec for slow query or -1 to wait indefinitely */
#define CCI_DS_PROPERTY_QUERY_TIMEOUT			"query_timeout"
/* disconnect a released socket when query timeout is occurred */
#define CCI_DS_PROPERTY_DISCONNECT_ON_QUERY_TIMEOUT	"disconnect_on_query_timeout"
/* default autocommit state for connections created by pool*/
#define CCI_DS_PROPERTY_DEFAULT_AUTOCOMMIT		"default_autocommit"
/* default isolation state for connections created by pool*/
#define CCI_DS_PROPERTY_DEFAULT_ISOLATION		"default_isolation"
/* default lock timeout in sec for connections created by pool*/
#define CCI_DS_PROPERTY_DEFAULT_LOCK_TIMEOUT		"default_lock_timeout"

/* for cci auto_comit mode support */
  typedef enum
  {
    CCI_AUTOCOMMIT_FALSE = 0,
    CCI_AUTOCOMMIT_TRUE
  } CCI_AUTOCOMMIT_MODE;

  /* for cci cas_change mode support */
  typedef enum
  {
    CCI_CAS_CHANGE_MODE_UNKNOWN = 0,
    CCI_CAS_CHANGE_MODE_AUTO = 1,
    CCI_CAS_CHANGE_MODE_KEEP = 2
  } CCI_CAS_CHANGE_MODE_MODE;

#define SET_AUTOCOMMIT_FROM_CASINFO(c) \
  (c)->autocommit_mode = \
  (c)->cas_info[CAS_INFO_ADDITIONAL_FLAG] & CAS_INFO_FLAG_MASK_AUTOCOMMIT ? \
      CCI_AUTOCOMMIT_TRUE : CCI_AUTOCOMMIT_FALSE

  typedef enum
  {
    CCI_NO_BACKSLASH_ESCAPES_FALSE = -1,
    CCI_NO_BACKSLASH_ESCAPES_TRUE = -2,
    CCI_NO_BACKSLASH_ESCAPES_NOT_SET = -3
  } CCI_NO_BACKSLASH_ESCAPES_MODE;


/************************************************************************
 * EXPORTED TYPE DEFINITIONS						*
 ************************************************************************/

  typedef struct
  {
    int err_code;
    char err_msg[1024];
  } T_CCI_ERROR;

  typedef struct
  {
    int size;
    char *buf;
  } T_CCI_BIT;

  typedef struct
  {
    short yr;
    short mon;
    short day;
    short hh;
    short mm;
    short ss;
    short ms;
  } T_CCI_DATE;

  typedef struct
  {
    int result_count;
    int stmt_type;
    int err_no;
    char *err_msg;
    char oid[32];
  } T_CCI_QUERY_RESULT;

  typedef enum
  {
    CCI_U_TYPE_FIRST = 0,
    CCI_U_TYPE_UNKNOWN = 0,
    CCI_U_TYPE_NULL = 0,

    CCI_U_TYPE_CHAR = 1,
    CCI_U_TYPE_STRING = 2,
    CCI_U_TYPE_NCHAR = 3,
    CCI_U_TYPE_VARNCHAR = 4,
    CCI_U_TYPE_BIT = 5,
    CCI_U_TYPE_VARBIT = 6,
    CCI_U_TYPE_NUMERIC = 7,
    CCI_U_TYPE_INT = 8,
    CCI_U_TYPE_SHORT = 9,
    CCI_U_TYPE_MONETARY = 10,
    CCI_U_TYPE_FLOAT = 11,
    CCI_U_TYPE_DOUBLE = 12,
    CCI_U_TYPE_DATE = 13,
    CCI_U_TYPE_TIME = 14,
    CCI_U_TYPE_TIMESTAMP = 15,
    CCI_U_TYPE_SET = 16,
    CCI_U_TYPE_MULTISET = 17,
    CCI_U_TYPE_SEQUENCE = 18,
    CCI_U_TYPE_OBJECT = 19,
    CCI_U_TYPE_RESULTSET = 20,
    CCI_U_TYPE_BIGINT = 21,
    CCI_U_TYPE_DATETIME = 22,
    CCI_U_TYPE_BLOB = 23,
    CCI_U_TYPE_CLOB = 24,
    CCI_U_TYPE_ENUM = 25,

    CCI_U_TYPE_LAST = CCI_U_TYPE_ENUM
  } T_CCI_U_TYPE;

  typedef void *T_CCI_SET;

  typedef enum
  {
    CCI_A_TYPE_FIRST = 1,
    CCI_A_TYPE_STR = 1,
    CCI_A_TYPE_INT,
    CCI_A_TYPE_FLOAT,
    CCI_A_TYPE_DOUBLE,
    CCI_A_TYPE_BIT,
    CCI_A_TYPE_DATE,
    CCI_A_TYPE_SET,
    CCI_A_TYPE_BIGINT,
    CCI_A_TYPE_BLOB,
    CCI_A_TYPE_CLOB,
    CCI_A_TYPE_REQ_HANDLE,
    CCI_A_TYPE_LAST = CCI_A_TYPE_REQ_HANDLE,

    CCI_A_TYTP_LAST = CCI_A_TYPE_LAST	/* typo but backward compatibility */
  } T_CCI_A_TYPE;

  enum
  {
    UNMEASURED_LENGTH = -1
  };

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
    CCI_SCH_LAST = CCI_SCH_CROSS_REFERENCE
  } T_CCI_SCH_TYPE;

  typedef enum
  {
    CCI_ER_NO_ERROR = 0,
    CCI_ER_DBMS = -20001,
    CCI_ER_CON_HANDLE = -20002,
    CCI_ER_NO_MORE_MEMORY = -20003,
    CCI_ER_COMMUNICATION = -20004,
    CCI_ER_NO_MORE_DATA = -20005,
    CCI_ER_TRAN_TYPE = -20006,
    CCI_ER_STRING_PARAM = -20007,
    CCI_ER_TYPE_CONVERSION = -20008,
    CCI_ER_BIND_INDEX = -20009,
    CCI_ER_ATYPE = -20010,
    CCI_ER_NOT_BIND = -20011,
    CCI_ER_PARAM_NAME = -20012,
    CCI_ER_COLUMN_INDEX = -20013,
    CCI_ER_SCHEMA_TYPE = -20014,
    CCI_ER_FILE = -20015,
    CCI_ER_CONNECT = -20016,

    CCI_ER_ALLOC_CON_HANDLE = -20017,
    CCI_ER_REQ_HANDLE = -20018,
    CCI_ER_INVALID_CURSOR_POS = -20019,
    CCI_ER_OBJECT = -20020,
    CCI_ER_CAS = -20021,
    CCI_ER_HOSTNAME = -20022,
    CCI_ER_OID_CMD = -20023,

    CCI_ER_BIND_ARRAY_SIZE = -20024,
    CCI_ER_ISOLATION_LEVEL = -20025,

    CCI_ER_SET_INDEX = -20026,
    CCI_ER_DELETED_TUPLE = -20027,

    CCI_ER_SAVEPOINT_CMD = -20028,
    CCI_ER_THREAD_RUNNING = -20029,
    CCI_ER_INVALID_URL = -20030,
    CCI_ER_INVALID_LOB_READ_POS = -20031,
    CCI_ER_INVALID_LOB_HANDLE = -20032,

    CCI_ER_NO_PROPERTY = -20033,

    CCI_ER_PROPERTY_TYPE = -20034,
    CCI_ER_INVALID_PROPERTY_VALUE = CCI_ER_PROPERTY_TYPE,

    CCI_ER_INVALID_DATASOURCE = -20035,
    CCI_ER_DATASOURCE_TIMEOUT = -20036,
    CCI_ER_DATASOURCE_TIMEDWAIT = -20037,

    CCI_ER_LOGIN_TIMEOUT = -20038,
    CCI_ER_QUERY_TIMEOUT = -20039,

    CCI_ER_RESULT_SET_CLOSED = -20040,

    CCI_ER_INVALID_HOLDABILITY = -20041,
    CCI_ER_NOT_UPDATABLE = -20042,

    CCI_ER_INVALID_ARGS = -20043,
    CCI_ER_USED_CONNECTION = -20044,

    CCI_ER_NO_SHARD_AVAILABLE = -20045,
    CCI_ER_INVALID_SHARD = -20046,

    CCI_ER_NOT_IMPLEMENTED = -20099,
    CCI_ER_END = -20100
  } T_CCI_ERROR_CODE;

#if !defined(CAS)
#ifdef DBDEF_HEADER_
  typedef int T_CCI_CUBRID_STMT;
#else
  typedef enum
  {
    CUBRID_STMT_ALTER_CLASS,
    CUBRID_STMT_ALTER_SERIAL,
    CUBRID_STMT_COMMIT_WORK,
    CUBRID_STMT_REGISTER_DATABASE,
    CUBRID_STMT_CREATE_CLASS,
    CUBRID_STMT_CREATE_INDEX,
    CUBRID_STMT_CREATE_TRIGGER,
    CUBRID_STMT_CREATE_SERIAL,
    CUBRID_STMT_DROP_DATABASE,
    CUBRID_STMT_DROP_CLASS,
    CUBRID_STMT_DROP_INDEX,
    CUBRID_STMT_DROP_LABEL,
    CUBRID_STMT_DROP_TRIGGER,
    CUBRID_STMT_DROP_SERIAL,
    CUBRID_STMT_EVALUATE,
    CUBRID_STMT_RENAME_CLASS,
    CUBRID_STMT_ROLLBACK_WORK,
    CUBRID_STMT_GRANT,
    CUBRID_STMT_REVOKE,
    CUBRID_STMT_STATISTICS,
    CUBRID_STMT_INSERT,
    CUBRID_STMT_SELECT,
    CUBRID_STMT_UPDATE,
    CUBRID_STMT_DELETE,
    CUBRID_STMT_CALL,
    CUBRID_STMT_GET_ISO_LVL,
    CUBRID_STMT_GET_TIMEOUT,
    CUBRID_STMT_GET_OPT_LVL,
    CUBRID_STMT_SET_OPT_LVL,
    CUBRID_STMT_SCOPE,
    CUBRID_STMT_GET_TRIGGER,
    CUBRID_STMT_SET_TRIGGER,
    CUBRID_STMT_SAVEPOINT,
    CUBRID_STMT_PREPARE,
    CUBRID_STMT_ATTACH,
    CUBRID_STMT_USE,
    CUBRID_STMT_REMOVE_TRIGGER,
    CUBRID_STMT_RENAME_TRIGGER,
    CUBRID_STMT_ON_LDB,
    CUBRID_STMT_GET_LDB,
    CUBRID_STMT_SET_LDB,

    CUBRID_STMT_GET_STATS,
    CUBRID_STMT_CREATE_USER,
    CUBRID_STMT_DROP_USER,
    CUBRID_STMT_ALTER_USER,
    CUBRID_STMT_SET_SYS_PARAMS,
    CUBRID_STMT_ALTER_INDEX,

    CUBRID_STMT_CREATE_STORED_PROCEDURE,
    CUBRID_STMT_DROP_STORED_PROCEDURE,
    CUBRID_STMT_PREPARE_STATEMENT,
    CUBRID_STMT_EXECUTE_PREPARE,
    CUBRID_STMT_DEALLOCATE_PREPARE,
    CUBRID_STMT_TRUNCATE,
    CUBRID_STMT_DO,
    CUBRID_STMT_SELECT_UPDATE,
    CUBRID_STMT_SET_SESSION_VARIABLES,
    CUBRID_STMT_DROP_SESSION_VARIABLES,
    CUBRID_STMT_MERGE,
    CUBRID_STMT_SET_NAMES,
    CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER,

    CUBRID_MAX_STMT_TYPE
  } T_CCI_CUBRID_STMT;

  typedef int T_CCI_CONN;
  typedef int T_CCI_REQ;
  typedef struct PROPERTIES_T T_CCI_PROPERTIES;
  typedef struct DATASOURCE_T T_CCI_DATASOURCE;

#endif
#endif
#define CUBRID_STMT_CALL_SP	0x7e
#define CUBRID_STMT_UNKNOWN	0x7f

/* for backward compatibility */
#define T_CCI_SQLX_CMD T_CCI_CUBRID_STMT

#define SQLX_CMD_ALTER_CLASS   CUBRID_STMT_ALTER_CLASS
#define SQLX_CMD_ALTER_SERIAL   CUBRID_STMT_ALTER_SERIAL
#define SQLX_CMD_COMMIT_WORK   CUBRID_STMT_COMMIT_WORK
#define SQLX_CMD_REGISTER_DATABASE   CUBRID_STMT_REGISTER_DATABASE
#define SQLX_CMD_CREATE_CLASS   CUBRID_STMT_CREATE_CLASS
#define SQLX_CMD_CREATE_INDEX   CUBRID_STMT_CREATE_INDEX
#define SQLX_CMD_CREATE_TRIGGER   CUBRID_STMT_CREATE_TRIGGER
#define SQLX_CMD_CREATE_SERIAL   CUBRID_STMT_CREATE_SERIAL
#define SQLX_CMD_DROP_DATABASE   CUBRID_STMT_DROP_DATABASE
#define SQLX_CMD_DROP_CLASS   CUBRID_STMT_DROP_CLASS
#define SQLX_CMD_DROP_INDEX   CUBRID_STMT_DROP_INDEX
#define SQLX_CMD_DROP_LABEL   CUBRID_STMT_DROP_LABEL
#define SQLX_CMD_DROP_TRIGGER   CUBRID_STMT_DROP_TRIGGER
#define SQLX_CMD_DROP_SERIAL   CUBRID_STMT_DROP_SERIAL
#define SQLX_CMD_EVALUATE   CUBRID_STMT_EVALUATE
#define SQLX_CMD_RENAME_CLASS   CUBRID_STMT_RENAME_CLASS
#define SQLX_CMD_ROLLBACK_WORK   CUBRID_STMT_ROLLBACK_WORK
#define SQLX_CMD_GRANT   CUBRID_STMT_GRANT
#define SQLX_CMD_REVOKE   CUBRID_STMT_REVOKE
#define SQLX_CMD_UPDATE_STATS   CUBRID_STMT_UPDATE_STATS
#define SQLX_CMD_INSERT   CUBRID_STMT_INSERT
#define SQLX_CMD_SELECT   CUBRID_STMT_SELECT
#define SQLX_CMD_UPDATE   CUBRID_STMT_UPDATE
#define SQLX_CMD_DELETE   CUBRID_STMT_DELETE
#define SQLX_CMD_CALL   CUBRID_STMT_CALL
#define SQLX_CMD_GET_ISO_LVL   CUBRID_STMT_GET_ISO_LVL
#define SQLX_CMD_GET_TIMEOUT   CUBRID_STMT_GET_TIMEOUT
#define SQLX_CMD_GET_OPT_LVL   CUBRID_STMT_GET_OPT_LVL
#define SQLX_CMD_SET_OPT_LVL   CUBRID_STMT_SET_OPT_LVL
#define SQLX_CMD_SCOPE   CUBRID_STMT_SCOPE
#define SQLX_CMD_GET_TRIGGER   CUBRID_STMT_GET_TRIGGER
#define SQLX_CMD_SET_TRIGGER   CUBRID_STMT_SET_TRIGGER
#define SQLX_CMD_SAVEPOINT   CUBRID_STMT_SAVEPOINT
#define SQLX_CMD_PREPARE   CUBRID_STMT_PREPARE
#define SQLX_CMD_ATTACH   CUBRID_STMT_ATTACH
#define SQLX_CMD_USE   CUBRID_STMT_USE
#define SQLX_CMD_REMOVE_TRIGGER   CUBRID_STMT_REMOVE_TRIGGER
#define SQLX_CMD_RENMAE_TRIGGER   CUBRID_STMT_RENAME_TRIGGER
#define SQLX_CMD_ON_LDB   CUBRID_STMT_ON_LDB
#define SQLX_CMD_GET_LDB   CUBRID_STMT_GET_LDB
#define SQLX_CMD_SET_LDB   CUBRID_STMT_SET_LDB
#define SQLX_CMD_GET_STATS   CUBRID_STMT_GET_STATS
#define SQLX_CMD_CREATE_USER   CUBRID_STMT_CREATE_USER
#define SQLX_CMD_DROP_USER   CUBRID_STMT_DROP_USER
#define SQLX_CMD_ALTER_USER   CUBRID_STMT_ALTER_USER
#define SQLX_CMD_SET_SYS_PARAMS   CUBRID_STMT_SET_SYS_PARAMS
#define SQLX_CMD_ALTER_INDEX   CUBRID_STMT_ALTER_INDEX

#define SQLX_CMD_CREATE_STORED_PROCEDURE   CUBRID_STMT_CREATE_STORED_PROCEDURE
#define SQLX_CMD_DROP_STORED_PROCEDURE   CUBRID_STMT_DROP_STORED_PROCEDURE
#define SQLX_CMD_PREPARE_STATEMENT  CUBRID_STMT_PREPARE_STATEMENT
#define SQLX_CMD_EXECUTE_PREPARE  CUBRID_STMT_EXECUTE_PREPARE
#define SQLX_CMD_DEALLOCATE_PREPARE  CUBRID_STMT_DEALLOCATE_PREPARE
#define SQLX_CMD_TRUNCATE  CUBRID_STMT_TRUNCATE
#define SQLX_CMD_DO  CUBRID_STMT_DO
#define SQLX_CMD_SELECT_UPDATE   CUBRID_STMT_SELECT_UPDATE
#define SQLX_CMD_SET_SESSION_VARIABLES  CUBRID_STMT_SET_SESSION_VARIABLES
#define SQLX_CMD_DROP_SESSION_VARIABLES  CUBRID_STMT_DROP_SESSION_VARIABLES
#define SQLX_CMD_STMT_MERGE  CUBRID_STMT_MERGE
#define SQLX_CMD_SET_NAMES   CUBRID_STMT_SET_NAMES
#define SQLX_CMD_ALTER_STORED_PROCEDURE_OWNER   CUBRID_STMT_ALTER_STORED_PROCEDURE_OWNER

#define SQLX_MAX_CMD_TYPE   CUBRID_MAX_STMT_TYPE

#define SQLX_CMD_CALL_SP CUBRID_STMT_CALL_SP
#define SQLX_CMD_UNKNOWN CUBRID_STMT_UNKNOWN

  typedef enum
  {
    CCI_CURSOR_FIRST = 0,
    CCI_CURSOR_CURRENT = 1,
    CCI_CURSOR_LAST = 2
  } T_CCI_CURSOR_POS;

  typedef struct
  {
    T_CCI_U_TYPE type;
    char is_non_null;
    short scale;
    int precision;
    char *col_name;
    char *real_attr;
    char *class_name;
    char *default_value;
    char is_auto_increment;
    char is_unique_key;
    char is_primary_key;
    char is_foreign_key;
    char is_reverse_index;
    char is_reverse_unique;
    char is_shared;
  } T_CCI_COL_INFO;

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
    CCI_SP_CMD_FIRST = 1,
    CCI_SP_SET = 1,
    CCI_SP_ROLLBACK = 2,
    CCI_SP_CMD_LAST = CCI_SP_ROLLBACK
  } T_CCI_SAVEPOINT_CMD;

  typedef enum
  {
    CCI_DS_KEY_USER,
    CCI_DS_KEY_PASSWORD,
    CCI_DS_KEY_URL,
    CCI_DS_KEY_POOL_SIZE,
    CCI_DS_KEY_MAX_WAIT,
    CCI_DS_KEY_POOL_PREPARED_STATEMENT,
    CCI_DS_KEY_MAX_OPEN_PREPARED_STATEMENT,
    CCI_DS_KEY_LOGIN_TIMEOUT,
    CCI_DS_KEY_QUERY_TIMEOUT,
    CCI_DS_KEY_DISCONNECT_ON_QUERY_TIMEOUT,
    CCI_DS_KEY_DEFAULT_AUTOCOMMIT,
    CCI_DS_KEY_DEFAULT_ISOLATION,
    CCI_DS_KEY_DEFAULT_LOCK_TIMEOUT,
    CCI_DS_KEY_MAX_POOL_SIZE
  } T_CCI_DATASOURCE_KEY;

#if !defined(CAS)
#ifdef DBDEF_HEADER_
  typedef int T_CCI_TRAN_ISOLATION;
#else
  typedef enum
  {
    TRAN_UNKNOWN_ISOLATION = 0,
    TRAN_ISOLATION_MIN = 1,

    TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE = 1,
    TRAN_COMMIT_CLASS_COMMIT_INSTANCE = 2,
    TRAN_REP_CLASS_UNCOMMIT_INSTANCE = 3,
    TRAN_REP_CLASS_COMMIT_INSTANCE = 4,
    TRAN_REP_CLASS_REP_INSTANCE = 5,
    TRAN_SERIALIZABLE = 6,

    TRAN_ISOLATION_MAX = 6
  } T_CCI_TRAN_ISOLATION;
#endif
#endif

  typedef enum
  {
    CCI_PARAM_MODE_UNKNOWN = 0,
    CCI_PARAM_MODE_IN = 1,
    CCI_PARAM_MODE_OUT = 2,
    CCI_PARAM_MODE_INOUT = 3
  } T_CCI_PARAM_MODE;

  /* delete or update action type for foreign key */
  typedef enum
  {
    CCI_FOREIGN_KEY_CASCADE = 0,
    CCI_FOREIGN_KEY_RESTRICT = 1,
    CCI_FOREIGN_KEY_NO_ACTION = 2,
    CCI_FOREIGN_KEY_SET_NULL = 3
  } T_CCI_FOREIGN_KEY_ACTION;

  typedef struct
  {
    T_CCI_PARAM_MODE mode;
    T_CCI_U_TYPE type;
    short scale;
    int precision;
  } T_CCI_PARAM_INFO;

  typedef void *T_CCI_BLOB;

  typedef void *T_CCI_CLOB;

  typedef struct
  {
    int shard_id;
    char *db_name;
    char *db_server;
  } T_CCI_SHARD_INFO;

  /* memory allocators */
  typedef void *(*CCI_MALLOC_FUNCTION) (size_t);
  typedef void *(*CCI_CALLOC_FUNCTION) (size_t, size_t);
  typedef void *(*CCI_REALLOC_FUNCTION) (void *, size_t);
  typedef void (*CCI_FREE_FUNCTION) (void *);

/************************************************************************
 * EXPORTED FUNCTION PROTOTYPES						*
 ************************************************************************/

#if !defined(CAS)
  extern void cci_init (void);
  extern void cci_end (void);

  extern int cci_get_version_string (char *str, size_t len);
  extern int cci_get_version (int *major, int *minor, int *patch);
  extern int CCI_CONNECT_INTERNAL_FUNC_NAME (char *ip,
					     int port,
					     char *db_name,
					     char *db_user, char *dbpasswd);
  extern int cci_connect_ex (char *ip, int port, char *db, char *user,
			     char *pass, T_CCI_ERROR * err_buf);
  extern int cci_connect_with_url (char *url, char *user, char *password);
  extern int cci_connect_with_url_ex (char *url, char *user, char *pass,
				      T_CCI_ERROR * err_buf);
  extern int cci_disconnect (int con_handle, T_CCI_ERROR * err_buf);
  extern int cci_end_tran (int con_handle, char type, T_CCI_ERROR * err_buf);
  extern int cci_prepare (int con_handle,
			  char *sql_stmt, char flag, T_CCI_ERROR * err_buf);
  extern int cci_get_bind_num (int req_handle);
  extern T_CCI_COL_INFO *cci_get_result_info (int req_handle,
					      T_CCI_CUBRID_STMT * cmd_type,
					      int *num);
  extern int cci_bind_param (int req_handle, int index, T_CCI_A_TYPE a_type,
			     void *value, T_CCI_U_TYPE u_type, char flag);
  extern int cci_bind_param_ex (int mapped_stmt_id, int index,
				T_CCI_A_TYPE a_type, void *value, int length,
				T_CCI_U_TYPE u_type, char flag);
  extern int cci_execute (int req_handle,
			  char flag, int max_col_size, T_CCI_ERROR * err_buf);
  extern int cci_prepare_and_execute (int con_handle, char *sql_stmt,
				      int max_col_size, int *exec_retval,
				      T_CCI_ERROR * err_buf);
  extern int cci_get_db_parameter (int con_handle, T_CCI_DB_PARAM param_name,
				   void *value, T_CCI_ERROR * err_buf);
  extern int cci_set_db_parameter (int con_handle, T_CCI_DB_PARAM param_name,
				   void *value, T_CCI_ERROR * err_buf);
  extern int cci_set_cas_change_mode (int mapped_conn_id, int mode,
				      T_CCI_ERROR * err_buf);
  extern long cci_escape_string (int con_h_id, char *to, const char *from,
				 unsigned long length, T_CCI_ERROR * err_buf);
  extern int cci_close_query_result (int req_handle, T_CCI_ERROR * err_buf);
  extern int cci_close_req_handle (int req_handle);
  extern int cci_cursor (int req_handle,
			 int offset,
			 T_CCI_CURSOR_POS origin, T_CCI_ERROR * err_buf);
  extern int cci_fetch_size (int req_handle, int fetch_size);
  extern int cci_fetch (int req_handle, T_CCI_ERROR * err_buf);
  extern int cci_get_data (int req_handle,
			   int col_no, int type, void *value, int *indicator);
  extern int cci_schema_info (int con_handle,
			      T_CCI_SCH_TYPE type,
			      char *arg1, char *arg2,
			      char flag, T_CCI_ERROR * err_buf);
  extern int cci_get_cur_oid (int req_handle, char *oid_str_buf);
  extern int cci_oid_get (int con_handle,
			  char *oid_str,
			  char **attr_name, T_CCI_ERROR * err_buf);
  extern int cci_oid_put (int con_handle,
			  char *oid_str,
			  char **attr_name,
			  char **new_val, T_CCI_ERROR * err_buf);
  extern int cci_oid_put2 (int con_h_id,
			   char *oid_str,
			   char **attr_name,
			   void **new_val,
			   int *a_type, T_CCI_ERROR * err_buf);
  extern int cci_get_db_version (int con_handle, char *out_buf, int buf_size);
  extern CCI_AUTOCOMMIT_MODE cci_get_autocommit (int con_handle);
  extern int cci_set_autocommit (int con_handle,
				 CCI_AUTOCOMMIT_MODE autocommit_mode);
  extern int cci_set_holdability (int con_handle_id, int holdable);
  extern int cci_get_holdability (int con_handle_id);
  extern int cci_set_login_timeout (int mapped_conn_id, int timeout,
				    T_CCI_ERROR * err_buf);
  extern int cci_get_login_timeout (int mapped_conn_id, int *timeout,
				    T_CCI_ERROR * err_buf);

  extern int cci_get_class_num_objs (int conn_handle, char *class_name,
				     int flag, int *num_objs, int *num_pages,
				     T_CCI_ERROR * err_buf);
  extern int cci_oid (int con_h_id, T_CCI_OID_CMD cmd, char *oid_str,
		      T_CCI_ERROR * err_buf);
  extern int cci_oid_get_class_name (int con_h_id, char *oid_str,
				     char *out_buf, int out_buf_len,
				     T_CCI_ERROR * err_buf);
  extern int cci_col_get (int con_h_id, char *oid_str, char *col_attr,
			  int *col_size, int *col_type,
			  T_CCI_ERROR * err_buf);
  extern int cci_col_size (int con_h_id, char *oid_str, char *col_attr,
			   int *col_size, T_CCI_ERROR * err_buf);
  extern int cci_col_set_drop (int con_h_id, char *oid_str, char *col_attr,
			       char *value, T_CCI_ERROR * err_buf);
  extern int cci_col_set_add (int con_h_id, char *oid_str, char *col_attr,
			      char *value, T_CCI_ERROR * err_buf);
  extern int cci_col_seq_drop (int con_h_id, char *oid_str, char *col_attr,
			       int index, T_CCI_ERROR * err_buf);
  extern int cci_col_seq_insert (int con_h_id, char *oid_str, char *col_attr,
				 int index, char *value,
				 T_CCI_ERROR * err_buf);
  extern int cci_col_seq_put (int con_h_id, char *oid_str, char *col_attr,
			      int index, char *value, T_CCI_ERROR * err_buf);

  extern int cci_is_updatable (int req_h_id);
  extern int cci_is_holdable (int req_h_id);
  extern int cci_next_result (int req_h_id, T_CCI_ERROR * err_buf);
  extern int cci_bind_param_array_size (int req_h_id, int array_size);
  extern int cci_bind_param_array (int req_h_id,
				   int index,
				   T_CCI_A_TYPE a_type,
				   void *value,
				   int *null_ind, T_CCI_U_TYPE u_type);
  extern int cci_execute_array (int req_h_id,
				T_CCI_QUERY_RESULT ** qr,
				T_CCI_ERROR * err_buf);
  extern int cci_query_result_free (T_CCI_QUERY_RESULT * qr, int num_q);
  extern int cci_fetch_sensitive (int req_h_id, T_CCI_ERROR * err_buf);
  extern int cci_cursor_update (int req_h_id,
				int cursor_pos,
				int index,
				T_CCI_A_TYPE a_type,
				void *value, T_CCI_ERROR * err_buf);
  extern int cci_execute_batch (int con_h_id,
				int num_query,
				char **sql_stmt,
				T_CCI_QUERY_RESULT ** qr,
				T_CCI_ERROR * err_buf);
  extern int cci_fetch_buffer_clear (int req_h_id);
  extern int cci_execute_result (int req_h_id,
				 T_CCI_QUERY_RESULT ** qr,
				 T_CCI_ERROR * err_buf);
  extern int cci_set_isolation_level (int con_id,
				      T_CCI_TRAN_ISOLATION val,
				      T_CCI_ERROR * err_buf);
  extern int cci_set_lock_timeout (int con_id, int val,
				   T_CCI_ERROR * err_buf);

  extern void cci_set_free (T_CCI_SET set);
  extern int cci_set_size (T_CCI_SET set);
  extern int cci_set_element_type (T_CCI_SET set);
  extern int cci_set_get (T_CCI_SET set,
			  int index,
			  T_CCI_A_TYPE a_type, void *value, int *indicator);
  extern int cci_set_make (T_CCI_SET * set,
			   T_CCI_U_TYPE u_type,
			   int size, void *value, int *indicator);
  extern int cci_get_attr_type_str (int con_h_id,
				    char *class_name,
				    char *attr_name,
				    char *buf,
				    int buf_size, T_CCI_ERROR * err_buf);
  extern int cci_get_query_plan (int req_h_id, char **out_buf);
  extern int cci_query_info_free (char *out_buf);
  extern int cci_set_max_row (int req_h_id, int max_row);
  extern int cci_savepoint (int con_h_id,
			    T_CCI_SAVEPOINT_CMD cmd,
			    char *savepoint_name, T_CCI_ERROR * err_buf);
  extern int cci_get_param_info (int req_handle,
				 T_CCI_PARAM_INFO ** param,
				 T_CCI_ERROR * err_buf);
  extern int cci_param_info_free (T_CCI_PARAM_INFO * param);

  extern int cci_blob_new (int con_h_id, T_CCI_BLOB * blob,
			   T_CCI_ERROR * err_buf);
  extern long long cci_blob_size (T_CCI_BLOB blob);
  extern int cci_blob_write (int con_h_id, T_CCI_BLOB blob,
			     long long start_pos, int length,
			     const char *buf, T_CCI_ERROR * err_buf);
  extern int cci_blob_read (int con_h_id, T_CCI_BLOB blob,
			    long long start_pos, int length, char *buf,
			    T_CCI_ERROR * err_buf);
  extern int cci_blob_free (T_CCI_BLOB blob);
  extern int cci_clob_new (int con_h_id, T_CCI_CLOB * clob,
			   T_CCI_ERROR * err_buf);
  extern long long cci_clob_size (T_CCI_CLOB clob);
  extern int cci_clob_write (int con_h_id, T_CCI_CLOB clob,
			     long long start_pos, int length,
			     const char *buf, T_CCI_ERROR * err_buf);
  extern int cci_clob_read (int con_h_id, T_CCI_CLOB clob,
			    long long start_pos, int length, char *buf,
			    T_CCI_ERROR * err_buf);
  extern int cci_clob_free (T_CCI_CLOB clob);
  extern int cci_get_dbms_type (int con_h_id);
  extern int cci_register_out_param (int req_h_id, int index);
  extern int cci_register_out_param_ex (int req_h_id, int index,
					T_CCI_U_TYPE u_type);
  extern int cci_cancel (int con_h_id);
  extern int cci_get_error_msg (int err_code, T_CCI_ERROR * err_buf,
				char *out_buf, int out_buf_size);
  extern int cci_get_err_msg (int err_code, char *buf, int bufsize);
  extern int cci_set_charset (int con_h_id, char *charset);
  extern int cci_row_count (int con_h_id, int *row_count,
			    T_CCI_ERROR * err_buf);

  extern int cci_get_shard_id_with_con_handle (int con_h_id, int *shard_id,
					       T_CCI_ERROR * err_buf);
  extern int cci_get_shard_id_with_req_handle (int req_h_id, int *shard_id,
					       T_CCI_ERROR * err_buf);

  /*
   * IMPORTANT: cci_last_insert_id and cci_get_last_insert_id
   *
   *   cci_get_last_insert_id set value as last insert id in con_handle
   *   so it could be changed when new insertion is executed.
   *
   *   cci_last_insert_id set value as allocated last insert id
   *   so it won't be changed but user should free it manually.
   *
   *   But, It's possible to make some problem when it working with
   *   user own memory allocators or Windows shared library memory space.
   *
   *   So we deprecate cci_last_insert_id and strongly recommend to use
   *   cci_get_last_insert_id.
   */
  extern int cci_last_insert_id (int con_h_id, void *value,
				 T_CCI_ERROR * err_buf);

  extern int cci_get_last_insert_id (int con_h_id, void *value,
				     T_CCI_ERROR * err_buf);
  extern T_CCI_PROPERTIES *cci_property_create (void);
  extern void cci_property_destroy (T_CCI_PROPERTIES * properties);
  extern int cci_property_set (T_CCI_PROPERTIES * properties, char *key,
			       char *value);
  extern char *cci_property_get (T_CCI_PROPERTIES * properties,
				 const char *key);

  extern T_CCI_DATASOURCE *cci_datasource_create (T_CCI_PROPERTIES *
						  properties,
						  T_CCI_ERROR * err_buf);
  extern void cci_datasource_destroy (T_CCI_DATASOURCE * data_source);
  extern T_CCI_CONN cci_datasource_borrow (T_CCI_DATASOURCE * date_source,
					   T_CCI_ERROR * err_buf);
  extern int cci_datasource_release (T_CCI_DATASOURCE * date_source,
				     T_CCI_CONN conn, T_CCI_ERROR * err_buf);
  extern int cci_datasource_change_property (T_CCI_DATASOURCE * ds,
					     const char *key,
					     const char *val);

  extern int cci_set_query_timeout (int req_h_id, int timeout);
  extern int cci_get_query_timeout (int req_h_id);

  extern int cci_set_allocators (CCI_MALLOC_FUNCTION malloc_func,
				 CCI_FREE_FUNCTION free_func,
				 CCI_REALLOC_FUNCTION realloc_func,
				 CCI_CALLOC_FUNCTION calloc_func);

  extern int cci_get_shard_info (int con_h_id, T_CCI_SHARD_INFO ** shard_info,
				 T_CCI_ERROR * err_buf);
  extern int cci_shard_info_free (T_CCI_SHARD_INFO * shard_info);
  extern int cci_shard_schema_info (int con_h_id, int shard_id,
				    T_CCI_SCH_TYPE type, char *class_name,
				    char *attr_name, char flag,
				    T_CCI_ERROR * err_buf);
  extern int cci_is_shard (int con_h_id, T_CCI_ERROR * err_buf);
  extern int cci_get_cas_info (int mapped_conn_id, char *info_buf,
			       int buf_length, T_CCI_ERROR * err_buf);

#endif

/************************************************************************
 * EXPORTED VARIABLES							*
 ************************************************************************/

#ifdef __cplusplus
}
#endif

#endif				/* _CAS_CCI_H_ */
