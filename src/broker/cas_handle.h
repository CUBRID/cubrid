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

#define SRV_HANDLE_QUERY_SEQ_NUM(SRV_HANDLE)    \
        ((SRV_HANDLE) ? (SRV_HANDLE)->query_seq_num : 0)

typedef struct t_prepare_call_info T_PREPARE_CALL_INFO;
struct t_prepare_call_info
{
  void *dbval_ret;
  void *dbval_args;
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


typedef struct t_query_result T_QUERY_RESULT;
struct t_query_result
{
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
};

typedef struct t_srv_handle T_SRV_HANDLE;
struct t_srv_handle
{
  void *session;		/* query : DB_SESSION*
				   schema : schema info table pointer */
  T_PREPARE_CALL_INFO *prepare_call_info;
  T_QUERY_RESULT *q_result;
  void *cur_result;		/* query : &(q_result[cur_result])
				   schema info : &(session[cursor_pos]) */
  char *sql_stmt;
  void **classes;
  int *classes_chn;
  int id;
  int cur_result_index;
  int num_q_result;
  int num_markers;
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
};

extern int hm_new_srv_handle (T_SRV_HANDLE ** new_handle,
			      unsigned int seq_num);
extern void hm_srv_handle_free (int h_id);
extern void hm_srv_handle_free_all (void);
extern T_SRV_HANDLE *hm_find_srv_handle (int h_id);
extern void hm_qresult_clear (T_QUERY_RESULT * q_result);
extern void hm_qresult_end (T_SRV_HANDLE * srv_handle, char free_flag);
extern void hm_session_free (T_SRV_HANDLE * srv_handle);
extern void hm_col_update_info_clear (T_COL_UPDATE_INFO * col_update_info);
extern void hm_srv_handle_set_pooled (void);

#endif /* _CAS_HANDLE_H_ */
