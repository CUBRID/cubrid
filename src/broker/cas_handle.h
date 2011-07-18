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
 * cas_handle.h -
 */

#ifndef	_CAS_HANDLE_H_
#define	_CAS_HANDLE_H_

#ident "$Id$"

#if defined(CAS_FOR_ORACLE)
#include "cas_oracle.h"
#elif defined(CAS_FOR_MYSQL)
#include "cas_mysql.h"
#else /* CAS_FOR_MYSQL */
#include "cas_db_inc.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#define SRV_HANDLE_QUERY_SEQ_NUM(SRV_HANDLE)    \
        ((SRV_HANDLE) ? (SRV_HANDLE)->query_seq_num : 0)

#if defined(CAS_FOR_ORACLE)
#define CLOB_LOCATOR_LIMIT	32 * 1000	/* 32K */
#define MAX_LOCP_COUNT 		1024
#define DATA_SIZE		1024 * 1000
#endif

typedef struct t_prepare_call_info T_PREPARE_CALL_INFO;
struct t_prepare_call_info
{
  void *dbval_ret;
  void *dbval_args;
#if defined(CAS_FOR_ORACLE)
  void **bind;			/* OCIBind ** */
#elif defined(CAS_FOR_MYSQL)
  void *bind;			/* MYSQL_BIND * */
#endif
  char *param_mode;
  int num_args;
  int is_first_out;
};

typedef struct t_col_update_info T_COL_UPDATE_INFO;
struct t_col_update_info
{
  char *attr_name;
  char *class_name;
  char updatable;
};

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
typedef union db_data DB_DATA;
union db_data
{
  int i;
  short sh;
  int64_t bi;
  float f;
  double d;
#if defined(CAS_FOR_ORACLE)
  OCIDate date;
  OCINumber number;
#else				/* CAS_FOR_ORACLE */
  MYSQL_TIME t;
#endif				/* !CAS_FOR_ORACLE */
  void *p;
};

#if defined(CAS_FOR_ORACLE)
typedef struct locator_list LOCATOR_LIST;
struct locator_list
{
  OCILobLocator *locp[MAX_LOCP_COUNT];
  int locp_count;
};
#endif

typedef struct db_value DB_VALUE;
#if defined(CAS_FOR_ORACLE)
struct db_value
{
  unsigned short db_type;
  sb2 is_null;
  void *define;
  void *buf;			/* data pointer */
  DB_DATA data;
  unsigned int size;
  unsigned short rlen;
  bool need_clear;
};
#else
struct db_value
{
  unsigned short db_type;
  my_bool is_null;
  void *buf;			/* data pointer */
  DB_DATA data;
  unsigned long size;
  bool need_clear;
};
#endif /* CAS_FOR_ORACLE */
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */

typedef struct t_query_result T_QUERY_RESULT;
struct t_query_result
{
#if defined(CAS_FOR_ORACLE)
  int column_count;
  DB_VALUE *columns;
#elif defined(CAS_FOR_MYSQL)
  int column_count;
  DB_VALUE *columns;
  MYSQL_BIND *defines;
#else				/* CAS_FOR_MYSQL */
  void *result;
  char *null_type_column;
  T_COL_UPDATE_INFO *col_update_info;
  void *column_info;
  int copied;
  int tuple_count;
  int stmt_id;
  int num_column;
  char stmt_type;
  char col_updatable;
  char include_oid;
  char async_flag;
#endif				/* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
};

typedef struct t_srv_handle T_SRV_HANDLE;
struct t_srv_handle
{
  int id;
  void *session;		/* query : DB_SESSION*
				   schema : schema info table pointer */
  /* CAS4MySQL : MYSQL_STMT* */
  T_PREPARE_CALL_INFO *prepare_call_info;
  T_QUERY_RESULT *q_result;
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  int stmt_type;
  int tuple_count;
  bool is_no_data;
#else				/* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  void *cur_result;		/* query : &(q_result[cur_result])
				   schema info : &(session[cursor_pos]) */
#endif				/* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
  char *sql_stmt;
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  void **classes;
  int *classes_chn;
  int cur_result_index;
  int num_q_result;
  int num_markers;
#endif				/* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
  int max_col_size;
  int cursor_pos;
  int schema_type;
  int sch_tuple_num;
  int max_row;
  int num_classes;
  unsigned int query_seq_num;
  char prepare_flag;
  char is_prepared;
  char is_updatable;
  char query_info_flag;
  char is_pooled;
  char need_force_commit;
  char auto_commit_mode;
  char forward_only_cursor;
  bool use_plan_cache;
  bool use_query_cache;
  bool is_fetch_completed;
  bool is_holdable;
};

extern int hm_new_srv_handle (T_SRV_HANDLE ** new_handle,
			      unsigned int seq_num);
extern void hm_srv_handle_free (int h_id);
extern void hm_srv_handle_free_all (void);
extern void hm_srv_handle_qresult_end_all (bool end_holdable);
extern T_SRV_HANDLE *hm_find_srv_handle (int h_id);
extern void hm_qresult_clear (T_QUERY_RESULT * q_result);
extern void hm_qresult_end (T_SRV_HANDLE * srv_handle, char free_flag);
extern void hm_session_free (T_SRV_HANDLE * srv_handle);
extern void hm_col_update_info_clear (T_COL_UPDATE_INFO * col_update_info);
extern void hm_srv_handle_set_holdable (T_SRV_HANDLE * srv_handle, bool hold);
extern bool hm_has_holdable_results (void);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void hm_srv_handle_set_pooled (void);
#endif

extern int hm_srv_handle_append_active (T_SRV_HANDLE * srv_handle);
extern void hm_srv_handle_set_fetch_completed (T_SRV_HANDLE * srv_handle);
extern bool hm_srv_handle_is_all_active_fetch_completed (void);
extern void hm_srv_handle_reset_active (void);
#endif /* _CAS_HANDLE_H_ */
