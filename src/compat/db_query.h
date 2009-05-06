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
 * db_query.h - Query interface header file(Client Side)
 */

#ifndef _DB_QUERY_H_
#define _DB_QUERY_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "dbdef.h"
#include "object_primitive.h"
#include "class_object.h"
#include "cursor.h"

/* QUERY TYPE/FORMAT STRUCTURES */
typedef enum
{
  OID_COLUMN,			/* hidden OID column */
  USER_COLUMN,			/* user visible column */
  SYSTEM_ADDED_COLUMN		/* system added hidden column */
} COL_VISIBLE_TYPE;

struct db_query_type
{
  struct db_query_type *next;
  DB_COL_TYPE col_type;		/* Column type */
  char *name;			/* Column name */
  char *attr_name;		/* Attribute name */
  char *spec_name;		/* Spec name */
  char *original_name;		/* user specified column name */
  DB_TYPE db_type;		/* Column data type */
  int size;			/* Column data size */
  SM_DOMAIN *domain;		/* Column domain information */
  SM_DOMAIN *src_domain;	/* Column source domain information */
  COL_VISIBLE_TYPE visible_type;	/* Is the column user visible? */
};

/* QUERY RESULT STRUCTURES */

typedef struct db_select_result
{
  QUERY_ID query_id;		/* Query Identifier */
  int stmt_id;			/* Statement identifier */
  int stmt_type;		/* Statement type */
  CURSOR_ID cursor_id;		/* Cursor on the query result */
  CACHE_TIME cache_time;	/* query cache time */
} DB_SELECT_RESULT;		/* Select query result structure */

typedef struct db_call_result
{
  CURSOR_POSITION crs_pos;	/* Cursor position relative to value tuple */
  DB_VALUE *val_ptr;		/* single value  */
} DB_CALL_RESULT;		/* call_method result structure */

typedef struct db_objfetch_result
{
  CURSOR_POSITION crs_pos;	/* Cursor position relative to value list tuple */
  DB_VALUE **valptr_list;	/* list of value pointers */
} DB_OBJFETCH_RESULT;		/* object_fetch result structure */

typedef struct db_get_result
{
  CURSOR_POSITION crs_pos;	/* Cursor position relative to value list tuple */
  int tpl_idx;
  int n_tuple;
  DB_VALUE *tpl_list;
} DB_GET_RESULT;

typedef struct db_query_tplpos
{
  CURSOR_POSITION crs_pos;	/* Cursor position                       */
  VPID vpid;			/* Real page identifier containing tuple */
  int tpl_no;			/* Tuple number inside the page          */
  int tpl_off;			/* Tuple offset inside the page          */
} DB_QUERY_TPLPOS;		/* Tuple position structure              */

typedef enum
{ T_SELECT = 1, T_CALL, T_OBJFETCH, T_GET, T_CACHE_HIT } DB_RESULT_TYPE;

typedef enum
{ T_OPEN = 1, T_CLOSED } DB_RESULT_STATUS;

struct db_query_result
{
  DB_RESULT_TYPE type;		/* result type */
  DB_RESULT_STATUS status;	/* result status */
  int col_cnt;			/* number of values */
  bool oid_included;		/* first oid column included? */
  DB_QUERY_TYPE *query_type;	/* query type list */
  int type_cnt;			/* query type list count */
  int qtable_ind;		/* index to the query table */
  union
  {
    DB_SELECT_RESULT s;		/* select query result */
    DB_CALL_RESULT c;		/* CALL result */
    DB_OBJFETCH_RESULT o;	/* OBJFETCH result */
    DB_GET_RESULT g;		/* GET result */
  } res;
  struct db_query_result *next;	/* next str. ptr, used internally */
};

extern SM_DOMAIN *db_query_format_src_domain (DB_QUERY_TYPE * query_type);

extern int db_execute_with_values (const char *CSQL_query,
				   DB_QUERY_RESULT ** result,
				   DB_QUERY_ERROR * query_error,
				   int arg_count, DB_VALUE * vals);

extern int db_query_seek_tuple (DB_QUERY_RESULT * result,
				int offset, int seek_mode);

extern DB_QUERY_TPLPOS *db_query_get_tplpos (DB_QUERY_RESULT * result);

extern int db_query_set_tplpos (DB_QUERY_RESULT * result,
				DB_QUERY_TPLPOS * tplpos);

extern void db_query_free_tplpos (DB_QUERY_TPLPOS * tplpos);

extern int db_query_get_tuple_object (DB_QUERY_RESULT * result,
				      int index, DB_OBJECT ** object);

extern int db_query_get_tuple_object_by_name (DB_QUERY_RESULT * result,
					      char *column_name,
					      DB_OBJECT ** object);

extern int db_query_get_value_length (DB_QUERY_RESULT * result, int index);

extern int db_query_get_value_to_space (DB_QUERY_RESULT * result,
					int index,
					unsigned char *ptr,
					int maxlength,
					bool * truncated,
					DB_TYPE user_type, bool * null_flag);

extern int
db_query_get_value_to_pointer (DB_QUERY_RESULT * result, int index,
			       unsigned char **ptr, DB_TYPE user_type,
			       bool * null_flag);

extern DB_TYPE db_query_get_value_type (DB_QUERY_RESULT * result, int index);

extern void db_sqlx_debug_print_result (DB_QUERY_RESULT * result);
extern bool db_is_client_cache_reusable (DB_QUERY_RESULT * result);

extern int db_query_get_cache_time (DB_QUERY_RESULT * result,
				    CACHE_TIME * cache_time);

extern DB_QUERY_TYPE *db_cp_query_type (DB_QUERY_TYPE * query_type,
					int copy_only_user);

extern DB_QUERY_TYPE *db_alloc_query_format (int cnt);
extern void db_free_query_format (DB_QUERY_TYPE * q);
extern DB_QUERY_RESULT *db_get_db_value_query_result (DB_VALUE * var);

extern void db_free_colname_list (char **colname_list, int cnt);
extern void db_free_domain_list (SM_DOMAIN ** domain_list, int cnt);
extern void db_free_query_result (DB_QUERY_RESULT * r);
extern DB_QUERY_RESULT *db_alloc_query_result (DB_RESULT_TYPE r_type,
					       int col_cnt);
extern void db_init_query_result (DB_QUERY_RESULT * r, DB_RESULT_TYPE r_type);
extern void db_dump_query_result (DB_QUERY_RESULT * r);
extern char **db_cp_colname_list (char **colname_list, int cnt);
extern SM_DOMAIN **db_cp_domain_list (SM_DOMAIN ** domain_list, int cnt);
extern DB_QUERY_TYPE *db_get_query_type (DB_TYPE * type_list, int *size_list,
					 char **colname_list,
					 char **attrname_list,
					 SM_DOMAIN ** domain_list,
					 SM_DOMAIN ** src_domain_list,
					 int cnt, bool oid_included);
extern int db_query_execute_immediate (const char *CSQL_query,
				       DB_QUERY_RESULT ** result,
				       DB_QUERY_ERROR * query_error);
extern DB_QUERY_RESULT *db_get_objfetch_query_result (DB_VALUE * val_list,
						      int val_cnt,
						      int *size_list,
						      char **colname_list,
						      char **attrname_list);
extern int db_query_stmt_id (DB_QUERY_RESULT * result);

extern int db_query_end (DB_QUERY_RESULT * result);
extern int db_query_sync_end (DB_QUERY_RESULT * result);
extern int db_query_end_internal (DB_QUERY_RESULT * result,
				  bool notify_server);

extern void db_clear_client_query_result (int notify_server);
extern void db_final_client_query_result (void);

#endif /* _DB_QUERY_H_ */
