/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * api_common.h -
 */

#ifndef _API_COMMON_H_
#define _API_COMMON_H_

#include "cubrid_api.h"
#include "api_handle.h"

/*
 * This file defines abstract structures that are used in CUBRID C API
 * implementation. Each API sub-system (e.g. CCI or client C API) hides
 * their implementation detailes behind these structures.
 */

typedef enum handle_type CI_HANDLE_TYPE;
typedef enum ci_conn_status CI_CONN_STATUS;
typedef enum ci_stmt_status CI_STMT_STATUS;

typedef struct common_api_structure_s COMMON_API_STRUCTURE;
typedef struct api_resultset_meta_s API_RESULTSET_META;
typedef struct api_resultset_meta_ifs_s API_RESULTSET_META_IFS;
typedef struct api_resultset_s API_RESULTSET;
typedef struct api_resultset_ifs_s API_RESULTSET_IFS;
typedef struct api_object_resultset_pool_s API_OBJECT_RESULTSET_POOL;

typedef struct VALUE_AREA VALUE_AREA;
typedef struct API_VALUE API_VALUE;
typedef struct value_indexer_s VALUE_INDEXER;
typedef struct value_indexer_ifs_s VALUE_INDEXER_IFS;

typedef struct value_bind_table_s VALUE_BIND_TABLE;
typedef struct value_bind_table_ifs_s VALUE_BIND_TABLE_IFS;

typedef struct api_collection_s API_COLLECTION;
typedef struct api_collection_ifs_s API_COLLECTION_IFS;

typedef struct cubrid_api_function_table_s CUBRID_API_FUNCTION_TABLE;

/*
 * type of CUBRID API handle
 */
enum handle_type
{
  HANDLE_TYPE_INVALID = 0,
  HANDLE_TYPE_CONNECTION = 1,
  HANDLE_TYPE_STATEMENT,
  HANDLE_TYPE_RESULTSET,
  HANDLE_TYPE_PMETA,
  HANDLE_TYPE_RMETA,
  HANDLE_TYPE_BATCH_RESULT
};

/*
 * connection handle status
 */
enum ci_conn_status
{
  CI_CONN_STATUS_INITIALIZED = 0,
  CI_CONN_STATUS_CONNECTED = 1,
  CI_CONN_STATUS_DISCONNECTED = 2
};

/*
 * statement handle status
 */
enum ci_stmt_status
{
  CI_STMT_STATUS_INITIALIZED = 0x01,
  CI_STMT_STATUS_PREPARED = 0x02,
  CI_STMT_STATUS_EXECUTED = 0x04,
  CI_STMT_STATUS_BATCH_ADDED = 0x08,
  CI_STMT_STATUS_CLOSED = 0x10
};

#define COMMON_API_STRUCTURE_HEADER \
  BH_BIND bind; \
  CI_HANDLE_TYPE handle_type

/*
 * COMMON_API_STRUCTURE is base of the implementation structure of
 * CUBRID API handle.
 */
struct common_api_structure_s
{
  COMMON_API_STRUCTURE_HEADER;
};

#define COMMON_RESULTSET_META_HEADER \
  COMMON_API_STRUCTURE_HEADER; \
  API_RESULTSET_META_IFS *ifs

/*
 * API_RESULTSET_META structure is COMMON_API_STRUCTURE that provides
 * API_RESULTSET_META_IFS in addition.
 */
struct api_resultset_meta_s
{
  COMMON_RESULTSET_META_HEADER;
};

/*
 * API_RESULTSET_META_IFS structure is abstract data structure that
 * provides resultset meta data information.
 */
struct api_resultset_meta_ifs_s
{
  int (*get_count) (API_RESULTSET_META * rm, int *count);
  int (*get_info) (API_RESULTSET_META * rm, int index,
		   CI_RMETA_INFO_TYPE type, void *arg, size_t size);
};

#define COMMON_RESULTSET_HEADER \
  COMMON_API_STRUCTURE_HEADER; \
  API_RESULTSET_IFS *ifs

/*
 * API_RESULTSET structure is COMMON_API_STRUCTURE that provides
 * API_RESULTSET_IFS in addition
 */
struct api_resultset_s
{
  COMMON_RESULTSET_HEADER;
};

/*
 * API_RESULTSET_IFS structure is abstract data structure that provides
 * resultset related informations. Each function matches actual CUBRID
 * C API
 */
struct api_resultset_ifs_s
{
  int (*get_resultset_metadata) (API_RESULTSET * res,
				 API_RESULTSET_META ** rimpl);
  int (*fetch) (API_RESULTSET * res, int offset, CI_FETCH_POSITION pos);
  int (*tell) (API_RESULTSET * res, int *offset);
  int (*clear_updates) (API_RESULTSET * res);
  int (*delete_row) (API_RESULTSET * res);
  int (*get_value) (API_RESULTSET * res, int index, CI_TYPE type,
		    void *addr, size_t len, size_t * outlen, bool * is_null);
  int (*get_value_by_name) (API_RESULTSET * res, const char *name,
			    CI_TYPE type, void *addr, size_t len,
			    size_t * outlen, bool * isnull);
  int (*update_value) (API_RESULTSET * res, int index,
		       CI_TYPE type, void *addr, size_t len);
  int (*apply_update) (API_RESULTSET * res);
  void (*destroy) (API_RESULTSET * res);
};

/*
 * API_OBJECT_RESULTSET_POOL is abstract structure that provides
 * object related functions.
 */
struct api_object_resultset_pool_s
{
  int (*get_object_resultset) (API_OBJECT_RESULTSET_POOL * pool,
			       CI_OID * oid, API_RESULTSET ** rres);
  int (*oid_delete) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * oid);
  int (*oid_get_classname) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * oid,
			    char *name, size_t size);
  void (*destroy) (API_OBJECT_RESULTSET_POOL * pool);
  int (*glo_create) (API_OBJECT_RESULTSET_POOL * pool, CI_CONNECTION conn,
		     const char *file_path, const char *init_file_path,
		     CI_OID * glo);
  int (*glo_get_path_name) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			    char *buf, size_t bufsz, bool full_path);
  int (*glo_do_compaction) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo);
  int (*glo_insert) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		     const char *buf, size_t bufsz);
  int (*glo_delete) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		     size_t sz, size_t * ndeleted);
  int (*glo_read) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		   char *buf, size_t bufsz, size_t * nread);
  int (*glo_write) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		    const char *buf, size_t sz);
  int (*glo_truncate) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		       size_t * ndeleted);
  int (*glo_seek) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		   long offset, int whence);
  int (*glo_tell) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
		   long *offset);
  int (*glo_like_search) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			  const char *str, size_t strsz, bool * found);
  int (*glo_reg_search) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			 const char *str, size_t strsz, bool * found);
  int (*glo_bin_search) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * glo,
			 const char *str, size_t strsz, bool * found);
  int (*glo_copy) (API_OBJECT_RESULTSET_POOL * pool, CI_OID * to,
		   CI_OID * from);
};

/*
 * CHECK_PURPOSE is an enumeration definition which defines check purpose
 * of VALUE_INDEXER_IFS::check() function (see below)
 */
typedef enum check_purpose_s
{
  CHECK_FOR_GET = 0x1,
  CHECK_FOR_SET = 0x2,
  CHECK_FOR_INSERT = 0x4,
  CHECK_FOR_DELETE = 0x8
} CHECK_PURPOSE;

/*
 * VALUE_INDEXER is somthing that provides VALUE_INDEXER_IFS
 */
struct value_indexer_s
{
  VALUE_INDEXER_IFS *ifs;
};

/*
 * VALUE_INDEXER_IFS is abstract data structure that defines interface for
 * manipulating tuples of (VALUE_AREA, API_VALUE) pair indexed by unique
 * integer position.
 */
struct value_indexer_ifs_s
{
  int (*check) (VALUE_INDEXER * indexer, int index, CHECK_PURPOSE pup);
  int (*length) (VALUE_INDEXER * indexer, int *len);
  int (*get) (VALUE_INDEXER * indexer, int index, VALUE_AREA ** rva,
	      API_VALUE ** rv);
  int (*set) (VALUE_INDEXER * indexer, int index, VALUE_AREA * va,
	      API_VALUE * val);
  int (*map) (VALUE_INDEXER * indexer,
	      int (*mapf) (void *, int, VALUE_AREA *, API_VALUE *),
	      void *arg);
  int (*insert) (VALUE_INDEXER * indexer, int index, VALUE_AREA * va,
		 API_VALUE * dval);
  int (*delete) (VALUE_INDEXER * indexer, int index, VALUE_AREA ** rva,
		 API_VALUE ** rval);
  void (*destroy) (VALUE_INDEXER * indexer,
		   void (*df) (VALUE_AREA * va, API_VALUE * db));
};

/*
 * VALUE_BIND_TABLE is something that provides VALUE_BIND_TABLE_IFS
 */
struct value_bind_table_s
{
  VALUE_BIND_TABLE_IFS *ifs;
};

/*
 * VALUE_BIND_TABLE_IFS is abstract data structure that provides value
 * containing and binding to actual implementation of target API sub-system.
 */
struct value_bind_table_ifs_s
{
  int (*get_value) (VALUE_BIND_TABLE * tbl, int index, CI_TYPE type,
		    void *addr, size_t len, size_t * outlen, bool * isnull);
  int (*set_value) (VALUE_BIND_TABLE * tbl, int index, CI_TYPE type,
		    void *addr, size_t len);
  int (*get_value_by_name) (VALUE_BIND_TABLE * tbl, const char *name,
			    CI_TYPE type, void *addr, size_t len,
			    size_t * outlen, bool * isnull);
  int (*set_value_by_name) (VALUE_BIND_TABLE * tbl, const char *name,
			    CI_TYPE type, void *addr, size_t len);
  int (*apply_updates) (VALUE_BIND_TABLE * tbl);
  int (*reset) (VALUE_BIND_TABLE * tbl);
  void (*destroy) (VALUE_BIND_TABLE * tbl);
};

/*
 * API_COLLECTION is value-copy of API sub-system collection implementation.
 */
struct api_collection_s
{
  BIND_HANDLE conn;		/* connection handle */
  API_COLLECTION_IFS *ifs;	/* collection interface */
};

/*
 * API_COLLECTION_IFS is abstract structure that provides collection related
 * CUBRID C API function.
 */
struct api_collection_ifs_s
{
  int (*length) (API_COLLECTION * col, int *len);
  int (*insert) (API_COLLECTION * col, long pos, CI_TYPE type, void *ptr,
		 size_t size);
  int (*update) (API_COLLECTION * col, long pos, CI_TYPE type, void *ptr,
		 size_t size);
  int (*delete) (API_COLLECTION * col, long pos);
  int (*get_elem_domain_info) (API_COLLECTION * col, long pos,
			       CI_TYPE * type, int *precision, int *scale);
  int (*get_elem) (API_COLLECTION * col, long pos, CI_TYPE type,
		   void *addr, size_t len, size_t * outlen, bool * isnull);
  void (*destroy) (API_COLLECTION * col);
};

/* api_value_indexer.c */
extern int array_indexer_create (int nvalue, VALUE_INDEXER ** rvi);
extern int list_indexer_create (VALUE_INDEXER ** rvi);

/*
 * API sub-system CUBRID C API implemetation table
 */
struct cubrid_api_function_table_s
{
  int (*create_connection) (CI_CONNECTION * conn);
  int (*err_set) (int err_code);
  int (*conn_connect) (COMMON_API_STRUCTURE * conn, const char *host,
		       unsigned short port, const char *databasename,
		       const char *user_name, const char *password);
  int (*conn_close) (COMMON_API_STRUCTURE * conn);
  int (*conn_create_statement) (COMMON_API_STRUCTURE * conn,
				CI_STATEMENT * stmt);
  int (*conn_set_option) (COMMON_API_STRUCTURE * conn,
			  CI_CONNECTION_OPTION option, void *arg,
			  size_t size);
  int (*conn_get_option) (COMMON_API_STRUCTURE * conn,
			  CI_CONNECTION_OPTION option, void *arg,
			  size_t size);
  int (*conn_commit) (COMMON_API_STRUCTURE * conn);
  int (*conn_rollback) (COMMON_API_STRUCTURE * conn);
  int (*conn_get_error) (COMMON_API_STRUCTURE * conn, int *err,
			 char *msg, size_t size);
  int (*stmt_add_batch_query) (COMMON_API_STRUCTURE * stmt, const char *sql,
			       size_t len);
  int (*stmt_add_batch) (COMMON_API_STRUCTURE * stmt);
  int (*stmt_clear_batch) (COMMON_API_STRUCTURE * stmt);
  int (*stmt_execute_immediate) (COMMON_API_STRUCTURE * stmt, char *sql,
				 size_t len, CI_RESULTSET * rs, int *r);
  int (*stmt_execute) (COMMON_API_STRUCTURE * stmt, CI_RESULTSET * rs,
		       int *r);
  int (*stmt_execute_batch) (COMMON_API_STRUCTURE * stmt,
			     CI_BATCH_RESULT * br);
  int (*stmt_get_option) (COMMON_API_STRUCTURE * stmt,
			  CI_STATEMENT_OPTION option, void *arg, size_t size);
  int (*stmt_set_option) (COMMON_API_STRUCTURE * stmt,
			  CI_STATEMENT_OPTION option, void *arg, size_t size);
  int (*stmt_prepare) (COMMON_API_STRUCTURE * stmt, const char *sql,
		       size_t len);
  int (*stmt_register_out_parameter) (COMMON_API_STRUCTURE * stmt, int index);
  int (*stmt_get_resultset_metadata) (COMMON_API_STRUCTURE * stmt,
				      CI_RESULTSET_METADATA * r);
  int (*stmt_get_parameter_metadata) (COMMON_API_STRUCTURE * stmt,
				      CI_PARAMETER_METADATA * r);
  int (*stmt_get_parameter) (COMMON_API_STRUCTURE * stmt, int index,
			     CI_TYPE type, void *addr, size_t len,
			     size_t * outlen, bool * isnull);
  int (*stmt_set_parameter) (COMMON_API_STRUCTURE * stmt, int index,
			     CI_TYPE type, void *val, size_t size);
  int (*stmt_get_resultset) (COMMON_API_STRUCTURE * stmt, CI_RESULTSET * res);
  int (*stmt_affected_rows) (COMMON_API_STRUCTURE * stmt, int *out);
  int (*stmt_get_query_type) (COMMON_API_STRUCTURE * stmt,
			      CUBRID_STMT_TYPE * type);
  int (*stmt_get_start_line) (COMMON_API_STRUCTURE * stmt, int *line);
  int (*stmt_next_result) (COMMON_API_STRUCTURE * stmt, bool * exist_result);
  int (*stmt_get_first_error) (COMMON_API_STRUCTURE * stmt, int *line,
			       int *col, int *errcode, char *err_msg,
			       size_t size);
  int (*stmt_get_next_error) (COMMON_API_STRUCTURE * stmt, int *line,
			      int *col, int *errcode, char *err_msg,
			      size_t size);
  int (*batch_res_query_count) (COMMON_API_STRUCTURE * pst, int *count);
  int (*batch_res_get_result) (COMMON_API_STRUCTURE * pst, int index,
			       int *ret, int *nr);
  int (*batch_res_get_error) (COMMON_API_STRUCTURE * pst, int index,
			      int *err_code, char *err_msg, size_t size);
  int (*pmeta_get_count) (COMMON_API_STRUCTURE * pst, int *count);
  int (*pmeta_get_info) (COMMON_API_STRUCTURE * pst, int index,
			 CI_PMETA_INFO_TYPE type, void *arg, size_t size);
  int (*get_connection_opool) (COMMON_API_STRUCTURE * pst,
			       API_OBJECT_RESULTSET_POOL ** rpool);
  int (*collection_new) (CI_CONNECTION conn, CI_COLLECTION * coll);
};

/* external function table */
extern CUBRID_API_FUNCTION_TABLE Cubrid_api_function_table;

#endif /* _API_COMMON_H_ */
