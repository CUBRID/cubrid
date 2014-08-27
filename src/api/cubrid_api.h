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
 * cubrid_api.h -
 */

#ifndef _CUBRID_API_H_
#define _CUBRID_API_H_

#include "config.h"
#include <stdlib.h>
#include "error_code.h"

#define IS_VALID_ISOLATION_LEVEL(isolation_level) \
    (TRAN_MINVALUE_ISOLATION <= (isolation_level) \
     && (isolation_level) <= TRAN_MAXVALUE_ISOLATION)

#define TRAN_DEFAULT_ISOLATION_LEVEL()	(TRAN_DEFAULT_ISOLATION)

typedef enum
{
  TRAN_UNKNOWN_ISOLATION = 0x00,	/*        0  0000 */

  TRAN_READ_COMMITTED = 0x04,	/*        0  0100 */
  TRAN_REP_CLASS_COMMIT_INSTANCE = 0x04,	/* Alias of above */
  TRAN_CURSOR_STABILITY = 0x04,	/* Alias of above */

  TRAN_REPEATABLE_READ = 0x05,	/*        0  0101 */
  TRAN_REP_READ = 0x05,		/* Alias of above */
  TRAN_REP_CLASS_REP_INSTANCE = 0x05,	/* Alias of above */
  TRAN_DEGREE_2_9999_CONSISTENCY = 0x05,	/* Alias of above */

  TRAN_SERIALIZABLE = 0x06,	/*        0  0110 */
  TRAN_DEGREE_3_CONSISTENCY = 0x06,	/* Alias of above */
  TRAN_NO_PHANTOM_READ = 0x06,	/* Alias of above */

  TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,
  MVCC_TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,

  TRAN_MINVALUE_ISOLATION = 0x04,	/* internal use only */
  TRAN_MAXVALUE_ISOLATION = 0x06	/* internal use only */
} DB_TRAN_ISOLATION;

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
  CUBRID_STMT_UPDATE_STATS,
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
  CUBRID_STMT_KILL,
  CUBRID_STMT_VACUUM,

  CUBRID_MAX_STMT_TYPE
} CUBRID_STMT_TYPE;

typedef enum ci_type CI_TYPE;
typedef UINT64 CI_CONNECTION;
typedef UINT64 CI_STATEMENT;
typedef UINT64 CI_PARAMETER_METADATA;
typedef UINT64 CI_RESULTSET_METADATA;
typedef UINT64 CI_RESULTSET;
typedef UINT64 CI_BATCH_RESULT;
typedef struct ci_oid_s CI_OID;
typedef struct ci_time_s CI_TIME;
typedef void *CI_COLLECTION;

struct ci_time_s
{
  short year;
  short month;
  short day;
  short hour;
  short minute;
  short second;
  short millisecond;
};

typedef enum ci_conn_option CI_CONNECTION_OPTION;
typedef enum ci_stmt_option CI_STATEMENT_OPTION;
typedef enum ci_fetch_position CI_FETCH_POSITION;
typedef enum ci_rmeta_info_type CI_RMETA_INFO_TYPE;
typedef enum ci_pmeta_info_type CI_PMETA_INFO_TYPE;
typedef enum ci_param_mode CI_PARAMETER_MODE;

struct ci_oid_s
{
  int d1;
  int d2;
  CI_CONNECTION conn;
};

enum ci_fetch_position
{
  CI_FETCH_POSITION_FIRST = 1,
  CI_FETCH_POSITION_CURRENT = 2,
  CI_FETCH_POSITION_LAST = 3
};

enum ci_type
{
  CI_TYPE_NULL = 0,
  CI_TYPE_INT = 1,
  CI_TYPE_SHORT,
  CI_TYPE_FLOAT,
  CI_TYPE_DOUBLE,
  CI_TYPE_CHAR,
  CI_TYPE_VARCHAR,
  CI_TYPE_NCHAR,
  CI_TYPE_VARNCHAR,
  CI_TYPE_BIT,
  CI_TYPE_VARBIT,
  CI_TYPE_TIME,
  CI_TYPE_DATE,
  CI_TYPE_TIMESTAMP,
  CI_TYPE_MONETARY,
  CI_TYPE_NUMERIC,
  CI_TYPE_OID,
  CI_TYPE_COLLECTION,
  CI_TYPE_BIGINT,
  CI_TYPE_DATETIME
};

enum ci_conn_option
{
  CI_CONNECTION_OPTION_CLIENT_VERSION = 1,
  CI_CONNECTION_OPTION_SERVER_VERSION = 2,
  CI_CONNECTION_OPTION_LOCK_TIMEOUT = 3,
  CI_CONNECTION_OPTION_TRAN_ISOLATION_LV = 4,
  CI_CONNECTION_OPTION_AUTOCOMMIT = 5
};

enum ci_stmt_option
{
  CI_STATEMENT_OPTION_HOLD_CURSORS_OVER_COMMIT = 1,
  CI_STATEMENT_OPTION_UPDATABLE_RESULT = 2,
  CI_STATEMENT_OPTION_ASYNC_QUERY = 3,
  CI_STATEMENT_OPTION_EXEC_CONTINUE_ON_ERROR = 4,
  CI_STATEMENT_OPTION_GET_GENERATED_KEYS = 5,
  CI_STATEMENT_OPTION_LAZY_EXEC = 6,
};

enum ci_rmeta_info_type
{
  CI_RMETA_INFO_COL_LABEL = 1,
  CI_RMETA_INFO_COL_NAME = 2,
  CI_RMETA_INFO_COL_TYPE = 3,
  CI_RMETA_INFO_PRECISION = 4,
  CI_RMETA_INFO_SCALE = 5,
  CI_RMETA_INFO_TABLE_NAME = 7,
  CI_RMETA_INFO_IS_AUTO_INCREMENT = 8,
  CI_RMETA_INFO_IS_NULLABLE = 9,
  CI_RMETA_INFO_IS_WRITABLE = 10
};

enum ci_pmeta_info_type
{
  CI_PMETA_INFO_MODE = 1,
  CI_PMETA_INFO_COL_TYPE = 2,
  CI_PMETA_INFO_PRECISION = 3,
  CI_PMETA_INFO_SCALE = 4,
  CI_PMETA_INFO_NULLABLE = 5
};

enum ci_param_mode
{
  CI_PARAM_MODE_IN = 0,
  CI_PARAM_MODE_OUT = 1
};

extern int ci_create_connection (CI_CONNECTION * conn);
extern int ci_conn_connect (CI_CONNECTION conn, const char *host,
			    unsigned short port, const char *databasename,
			    const char *user_name, const char *password);
extern int ci_conn_close (CI_CONNECTION conn);
extern int ci_conn_create_statement (CI_CONNECTION conn, CI_STATEMENT * stmt);
extern int
ci_conn_set_option (CI_CONNECTION conn, CI_CONNECTION_OPTION option,
		    void *arg, size_t size);
extern int
ci_conn_get_option (CI_CONNECTION conn, CI_CONNECTION_OPTION option,
		    void *arg, size_t size);

extern int ci_conn_commit (CI_CONNECTION conn);
extern int ci_conn_rollback (CI_CONNECTION conn);
extern int
ci_conn_get_error (CI_CONNECTION handle, int *err, char *msg, size_t size);

extern int ci_stmt_close (CI_STATEMENT stmt);
extern int ci_stmt_execute_immediate (CI_STATEMENT stmt, char *sql,
				      size_t len, CI_RESULTSET * rs, int *r);
extern int ci_stmt_execute (CI_STATEMENT stmt, CI_RESULTSET * rs, int *r);
extern int
ci_stmt_get_option (CI_STATEMENT stmt,
		    CI_STATEMENT_OPTION option, void *arg, size_t size);
extern int
ci_stmt_set_option (CI_STATEMENT stmt,
		    CI_STATEMENT_OPTION option, void *arg, size_t size);
extern int ci_stmt_prepare (CI_STATEMENT stmt, const char *sql, size_t len);
extern int ci_stmt_register_out_parameter (CI_STATEMENT stmt, int index);
extern int
ci_stmt_get_resultset_metadata (CI_STATEMENT stmt, CI_RESULTSET_METADATA * r);

extern int
ci_stmt_get_parameter_metadata (CI_STATEMENT stmt, CI_PARAMETER_METADATA * r);
extern int
ci_stmt_get_parameter (CI_STATEMENT stmt, int index, CI_TYPE type,
		       void *addr, size_t len, size_t * outlen,
		       bool * isnull);
extern int
ci_stmt_set_parameter (CI_STATEMENT stmt,
		       int index, CI_TYPE type, void *val, size_t size);
extern int ci_stmt_get_resultset (CI_STATEMENT stmt, CI_RESULTSET * res);

extern int ci_stmt_affected_rows (CI_STATEMENT stmt, int *out);

extern int ci_stmt_get_query_type (CI_STATEMENT stmt,
				   CUBRID_STMT_TYPE * type);

extern int ci_stmt_get_start_line (CI_STATEMENT stmt, int *line);
extern int ci_stmt_next_result (CI_STATEMENT stmt, bool * exist_result);
extern int
ci_res_get_resultset_metadata (CI_RESULTSET res, CI_RESULTSET_METADATA * r);
extern int ci_res_fetch (CI_RESULTSET res, int offset, CI_FETCH_POSITION pos);

extern int ci_res_fetch_tell (CI_RESULTSET res, int *offset);
extern int ci_res_clear_updates (CI_RESULTSET res);
extern int ci_res_delete_row (CI_RESULTSET res);
extern int
ci_res_get_value (CI_RESULTSET res, int index, CI_TYPE type,
		  void *addr, size_t len, size_t * outlen, bool * isnull);

extern int
ci_res_get_value_by_name (CI_RESULTSET res, const char *name,
			  CI_TYPE type, void *addr, size_t len,
			  size_t * outlen, bool * isnull);
extern int
ci_res_update_value (CI_RESULTSET res, int index, CI_TYPE type,
		     void *addr, size_t len);
extern int ci_res_apply_row (CI_RESULTSET res);
extern int ci_res_close (CI_RESULTSET res);

extern int ci_pmeta_get_count (CI_PARAMETER_METADATA pmeta, int *count);
extern int ci_pmeta_get_info (CI_PARAMETER_METADATA pmeta, int index,
			      CI_PMETA_INFO_TYPE type, void *arg,
			      size_t size);
extern int ci_rmeta_get_count (CI_RESULTSET_METADATA rmeta, int *count);
extern int ci_rmeta_get_info (CI_RESULTSET_METADATA rmeta, int index,
			      CI_RMETA_INFO_TYPE type, void *arg,
			      size_t size);
extern int
ci_stmt_get_first_error (CI_STATEMENT stmt, int *line, int *col,
			 int *errcode, char *err_msg, size_t size);
extern int
ci_stmt_get_next_error (CI_STATEMENT stmt, int *line, int *col,
			int *errcode, char *err_msg, size_t size);

extern int ci_stmt_add_batch_query (CI_STATEMENT stmt, const char *sql,
				    size_t len);
extern int ci_stmt_add_batch (CI_STATEMENT stmt);
extern int ci_stmt_execute_batch (CI_STATEMENT stmt, CI_BATCH_RESULT * br);
extern int ci_stmt_clear_batch (CI_STATEMENT stmt);

extern int ci_batch_res_query_count (CI_BATCH_RESULT br, int *count);
extern int ci_batch_res_get_result (CI_BATCH_RESULT br, int index,
				    int *ret, int *nr);
extern int ci_batch_res_get_error (CI_BATCH_RESULT br, int index,
				   int *err_code, char *err_msg,
				   size_t buf_size);

extern int ci_oid_delete (CI_OID * oid);
extern int ci_oid_get_classname (CI_OID * oid, char *name, size_t size);
extern int ci_oid_get_resultset (CI_OID * oid, CI_RESULTSET * rs);

extern int ci_collection_new (CI_CONNECTION conn, CI_COLLECTION * coll);
extern int ci_collection_free (CI_COLLECTION coll);
extern int ci_collection_length (CI_COLLECTION coll, long *length);
extern int ci_collection_insert (CI_COLLECTION coll, long pos,
				 CI_TYPE type, void *ptr, size_t size);
extern int ci_collection_update (CI_COLLECTION coll, long pos,
				 CI_TYPE type, void *ptr, size_t size);
extern int ci_collection_delete (CI_COLLECTION coll, long pos);
extern int ci_collection_get_elem_domain_info (CI_COLLECTION coll,
					       long pos, CI_TYPE * type,
					       int *precision, int *scale);
extern int ci_collection_get (CI_COLLECTION coll, long pos,
			      CI_TYPE type, void *addr, size_t len,
			      size_t * outlen, bool * isnull);

#endif /* _CUBRID_API_H_ */
