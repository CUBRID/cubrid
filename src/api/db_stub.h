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
 * db_stub.h - cubrid api header file.
 */

#ifndef _DB_STUB_H_
#define _DB_STUB_H_

#include "config.h"

#include <stddef.h>
#include <stdlib.h>
#include "api_common.h"
#include "parser.h"
#include "dbdef.h"
#include "error_manager.h"
#include "transaction_cl.h"
#include "parse_tree.h"		/* TODO: remove */
#include "system_parameter.h"
#include "api_compat.h"

#define api_er_set(a,b,c,d)     er_set(a,b,c,d)
#define api_get_errmsg()        er_msg()
#define api_get_errid           er_errid

#define HAS_RESULT(stmt_type)   (stmt_type == CUBRID_STMT_SELECT || \
                                 stmt_type == CUBRID_STMT_CALL   || \
                                 stmt_type == CUBRID_STMT_GET_ISO_LVL || \
                                 stmt_type == CUBRID_STMT_GET_TIMEOUT || \
                                 stmt_type == CUBRID_STMT_GET_OPT_LVL || \
                                 stmt_type == CUBRID_STMT_GET_STATS || \
                                 stmt_type == CUBRID_STMT_EVALUATE)



#define VERSION_LENGTH 32

typedef struct ci_conn_s CI_CONN_STRUCTURE;
typedef struct ci_conn_option_s CI_CONN_OPT_STRUCTURE;

typedef struct ci_stmt_s CI_STMT_STRUCTURE;
typedef struct ci_stmt_option_s CI_STMT_OPT_STRUCTURE;

typedef struct ci_resultset_s CI_RESULTSET_STRUCTURE;
typedef struct ci_resultset_meta_s CI_RESULTSET_META_STRUCTURE;
typedef struct ci_parater_meta_s CI_PARAM_META_STRUCTURE;

typedef struct ci_batch_data CI_BATCH_DATA;
typedef struct ci_batch_result_s CI_BATCH_RESULT_STRUCTURE;
typedef struct ci_batch_result_info_s CI_BATCH_RESULT_INFO;
typedef struct ci_stmt_error_info_s STMT_ERROR_INFO;
typedef struct rs_meta_info RS_META_INFO;
typedef struct stmt_result_info_s STMT_RESULT_INFO;

struct ci_conn_option_s
{
  char cli_version[VERSION_LENGTH];
  char srv_version[VERSION_LENGTH];
  int lock_timeout;
  DB_TRAN_ISOLATION isolation;
  bool autocommit;
  char *host;
  short int port;
};

struct ci_stmt_option_s
{
  bool hold_cursors_over_commit;
  bool updatable_result;
  bool get_generated_key;
  bool async_query;
  bool exec_continue_on_error;
  bool lazy_exec;
};

struct ci_stmt_error_info_s
{
  STMT_ERROR_INFO *next;
  int err_code;
  int line;
  int column;
  char *err_msg;
};

/* connection structure */
struct ci_conn_s
{
  COMMON_API_STRUCTURE_HEADER;
  CI_CONN_OPT_STRUCTURE opt;
  CI_CONN_STATUS conn_status;
  BH_INTERFACE *bh_interface;
  char **host;
  short int port;
  char *databasename;
  char *username;
  API_OBJECT_RESULTSET_POOL *opool;

  /* for autocommit */
  bool need_defered_commit;
  bool need_immediate_commit;
};

struct rs_meta_info
{
  CUBRID_STMT_TYPE sql_type;
  bool has_result;
  int affected_row;
};

/* statement structure */
struct stmt_result_info_s
{
  RS_META_INFO metainfo;
  CI_RESULTSET_STRUCTURE *rs;
  CI_RESULTSET_META_STRUCTURE *rsmeta;
};

struct ci_batch_data
{
  struct ci_batch_data *next;
  union _data
  {
    char *query_string;
    DB_VALUE *val;
  } data;
  size_t query_length;
};

struct ci_batch_result_info_s
{
  RS_META_INFO metainfo;
  int err_code;
  char *err_msg;
};

struct ci_batch_result_s
{
  COMMON_API_STRUCTURE_HEADER;
  CI_BATCH_RESULT_INFO *rs_info;
  int rs_count;
};

struct ci_stmt_s
{
  COMMON_API_STRUCTURE_HEADER;
  CI_STMT_OPT_STRUCTURE opt;
  CI_STMT_STATUS stmt_status;
  DB_SESSION *session;
  DB_VALUE *param_val;
  CI_CONN_STRUCTURE *pconn;
  STMT_RESULT_INFO *rs_info;
  STMT_ERROR_INFO *err_info;
  int current_err_idx;
  int current_rs_idx;
  CI_PARAM_META_STRUCTURE *ppmeta;
  bool *param_value_is_set;

  /* for batch result */
  int batch_count;
  CI_BATCH_DATA *batch_data;
  CI_BATCH_RESULT_STRUCTURE *batch_result;
};

struct ci_resultset_s
{
  COMMON_RESULTSET_HEADER;
  BH_INTERFACE *bh_interface;
  DB_QUERY_RESULT *result;
  int stmt_idx;
  CI_RESULTSET_META_STRUCTURE *prsmeta;
  VALUE_BIND_TABLE *value_table;
  bool current_row_isupdated;
  bool current_row_isdeleted;
};

struct ci_resultset_meta_s
{
  COMMON_RESULTSET_META_HEADER;
  BH_INTERFACE *bh_interface;
  DB_QUERY_TYPE *query_type;
  /* session's query_type[x]  if prepared, */
  /* else resultset's result->query_type */
  int stmt_idx;
  int col_count;
};

struct ci_parater_meta_s
{
  COMMON_API_STRUCTURE_HEADER;
  BH_INTERFACE *bh_interface;
  DB_MARKER *marker;
  bool *is_out_param;
  int param_count;
};

#if !defined(WINDOWS)
extern void (*prev_sigfpe_handler) (int);

extern void sigfpe_handler (int sig);
#endif

extern int db_type_to_type (DB_TYPE dt, CI_TYPE * xt);
extern int type_to_db_type (CI_TYPE xt, DB_TYPE * dt);
extern void xoid2oid (CI_OID * xoid, OID * oid);
extern void oid2xoid (OID * oid, BIND_HANDLE conn, CI_OID * xoid);
extern int coerce_value_to_db_value (CI_TYPE type, void *addr, size_t len,
				     DB_VALUE * dbval,
				     bool domain_initialized);
extern int coerce_db_value_to_value (const DB_VALUE * dbval, BIND_HANDLE conn,
				     CI_TYPE type, void *addr, size_t len,
				     size_t * outlen, bool * isnull);

extern int create_db_value_bind_table (int nvalue, void *impl, int auto_apply,
				       BIND_HANDLE conn_handle,
				       int (*get_index_by_name) (void *,
								 const char
								 *,
								 int *ri),
				       int (*get_db_value) (void *,
							    int,
							    DB_VALUE *),
				       int (*set_db_value) (void *,
							    int,
							    DB_VALUE
							    *),
				       int (*init_domain) (void *,
							   int,
							   DB_VALUE *),
				       VALUE_BIND_TABLE ** rtable);

/* api_object.c */
extern int
  api_object_resultset_pool_create
  (BH_INTERFACE * ifs, BIND_HANDLE conn, API_OBJECT_RESULTSET_POOL ** pool);
/* api_collection.c */
extern int api_collection_create_from_db_value (BIND_HANDLE conn,
						const DB_VALUE * val,
						API_COLLECTION ** rc);
extern int api_collection_set_to_db_value (API_COLLECTION * col,
					   DB_VALUE * val);
extern int api_collection_create (BIND_HANDLE conn, API_COLLECTION ** rc);
extern int ci_err_set (int error_code);

#endif /* _DB_STUB_H_ */
