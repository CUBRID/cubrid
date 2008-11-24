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
 * db_stub.c -
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "api_handle.h"
#include "db_stub.h"
#include "db.h"
#include "parser.h"
#include "schema_manager.h"
#include "execute_schema.h"
#include "execute_statement.h"
#include "boot_cl.h"
#include "system_parameter.h"
#include "authenticate.h"
#include "glo_class.h"
#include "object_accessor.h"
#include "jsp_sr.h"

#define API_PROGRAM_NAME "CUBRID C API"

#define ER_SET_AND_RETURN(X)               \
  do {                                     \
    if (X != NO_ERROR) {                   \
      ci_err_set(X);                    \
      return X;                            \
    }                                      \
  } while(0)

static int conn_end_tran (BH_INTERFACE * bh_interface,
			  CI_CONN_STRUCTURE * pconn, bool commit);
static int stmt_release_children (BH_INTERFACE * bh_interface,
				  CI_STMT_STRUCTURE * pstmt,
				  bool resultset_only);
static int stmt_bind_pmeta_handle (BH_INTERFACE * bh_interface,
				   CI_STMT_STRUCTURE * pstmt,
				   CI_PARAM_META_STRUCTURE ** outmeta);
static int api_rmeta_get_count (API_RESULTSET_META * impl, int *count);
static int api_rmeta_get_info (API_RESULTSET_META * impl, int index,
			       CI_RMETA_INFO_TYPE type, void *arg,
			       size_t size);
static int api_rs_get_resultset_metadata (API_RESULTSET * res,
					  API_RESULTSET_META ** rimpl);
static int api_rs_fetch (API_RESULTSET * res, int offset,
			 CI_FETCH_POSITION pos);
static int api_rs_fetch_tell (API_RESULTSET * res, int *offset);
static int api_rs_clear_updates (API_RESULTSET * res);
static int api_rs_delete_row (API_RESULTSET * res);
static int api_rs_get_value (API_RESULTSET * res, int index, CI_TYPE type,
			     void *addr, size_t len, size_t * outlen,
			     bool * is_null);
static int api_rs_get_value_by_name (API_RESULTSET * res, const char *name,
				     CI_TYPE type, void *addr, size_t len,
				     size_t * outlen, bool * isnull);
static int api_rs_update_value (API_RESULTSET * res, int index,
				CI_TYPE type, void *addr, size_t len);
static int api_rs_apply_row (API_RESULTSET * res);
static void api_rs_destroy (API_RESULTSET * res);
static int get_col_count (DB_QUERY_TYPE * query_type);
static int create_resultset_value_table (int num_col, BIND_HANDLE conn,
					 CI_RESULTSET_STRUCTURE * prs,
					 VALUE_BIND_TABLE ** value_table);
static void conn_destroyf (BH_BIND * ptr);
static void stmt_destroyf (BH_BIND * ptr);
static void rs_destroyf (BH_BIND * ptr);
static void rs_meta_destroyf (BH_BIND * ptr);
static void pmeta_destroyf (BH_BIND * ptr);
static void batch_res_destroyf (BH_BIND * ptr);
static int init_conn_structure (CI_CONN_STRUCTURE * pconn);
static int init_stmt_structure (CI_STMT_STRUCTURE * pstmt);
static int init_resultset_structure (CI_RESULTSET_STRUCTURE * prs);
static int init_rmeta_structure (CI_RESULTSET_META_STRUCTURE * prmeta);
static int init_pmeta_structure (CI_PARAM_META_STRUCTURE * ppmeta);
static int init_batch_rs_structure (CI_BATCH_RESULT_STRUCTURE * pbrs);
static int
stmt_exec_immediate_internal (BH_INTERFACE * bh_interface,
			      CI_STMT_STRUCTURE * pstmt, char *sql,
			      size_t len, CI_RESULTSET * rs, int *r);
static int
stmt_prepare_internal (BH_INTERFACE * bh_interface,
		       CI_STMT_STRUCTURE * pstmt, const char *sql,
		       size_t len);
static int
stmt_get_parameter_internal (BH_INTERFACE * bh_interface,
			     CI_STMT_STRUCTURE * pstmt, int index,
			     CI_TYPE type, void *addr, size_t len,
			     size_t * outlen, bool * isnull);
static int
stmt_set_parameter_internal (BH_INTERFACE * bh_interface,
			     CI_STMT_STRUCTURE * pstmt, int index,
			     CI_TYPE type, void *val, size_t size);
static int
stmt_reset_session_and_parse (CI_STMT_STRUCTURE * pstmt, const char *sql,
			      size_t len);
static int
stmt_exec_internal (BH_INTERFACE * bh_interface,
		    CI_STMT_STRUCTURE * pstmt, CI_RESULTSET * rs, int *r);
static int
stmt_get_resultset_metadata_internal (BH_INTERFACE * bh_interface,
				      CI_STMT_STRUCTURE * pstmt,
				      CI_RESULTSET_METADATA * r);
static API_RESULTSET_META_IFS *get_query_rmeta_ifs (void);
static API_RESULTSET_IFS *get_query_rs_ifs (void);
static int
stmt_bind_resultset_meta_handle (BH_INTERFACE * bh_interface,
				 CI_STMT_STRUCTURE * pstmt, int stmt_idx,
				 CI_RESULTSET_META_STRUCTURE ** outmeta);
static int
res_fetch_internal (BH_INTERFACE * bh_interface,
		    CI_RESULTSET_STRUCTURE * prs, int offset,
		    CI_FETCH_POSITION pos);
static int
res_delete_row_internal (BH_INTERFACE * bh_interface,
			 CI_RESULTSET_STRUCTURE * prs);
static int rs_get_index_by_name (void *impl, const char *name, int *ri);
static int rs_get_db_value (void *impl, int index, DB_VALUE * val);
static int rs_set_db_value (void *impl, int index, DB_VALUE * val);
static int rs_init_domain (void *impl, int index, DB_VALUE * val);
static int
pmeta_get_info_internal (BH_INTERFACE * bh_interface,
			 CI_PARAM_META_STRUCTURE * ppmeta, int index,
			 CI_PMETA_INFO_TYPE type, void *arg, size_t size);
static int
stmt_add_batch_param (CI_STMT_STRUCTURE * pstmt, DB_VALUE * val, int num_val);
static int
stmt_exec_prepared_batch_internal (BH_INTERFACE * bh_interface,
				   CI_STMT_STRUCTURE * pstmt,
				   CI_BATCH_RESULT * br);
static int
stmt_exec_batch_query_internal (BH_INTERFACE * bh_interface,
				CI_STMT_STRUCTURE * pstmt,
				CI_BATCH_RESULT * br);
static int
conn_restart_client (CI_CONN_STRUCTURE * pconn, const char *program,
		     int print_version, const char *volume, short port);
static int get_connection_opool (COMMON_API_STRUCTURE * pst,
				 API_OBJECT_RESULTSET_POOL ** opool);
static int
stmt_batch_alloc_and_bind_new_result (BH_INTERFACE * bh_interface,
				      CI_STMT_STRUCTURE * pstmt,
				      CI_BATCH_RESULT_STRUCTURE **
				      out_pbrs, CI_BATCH_RESULT * outhbr);
static int stmt_batch_result_free (BH_INTERFACE * bh_interface,
				   CI_STMT_STRUCTURE * pstmt);
static int stmt_add_batch_string (CI_STMT_STRUCTURE * pstmt, const char *sql,
				  size_t len);
static int stmt_make_error_info (CI_STMT_STRUCTURE * pstmt, int statement_id);
static int stmt_remove_error_info (CI_STMT_STRUCTURE * pstmt);

static int
stmt_make_error_info (CI_STMT_STRUCTURE * pstmt, int statement_id)
{
  DB_SESSION_ERROR *db_err;
  DB_SESSION *session;

  session = pstmt->session;

  db_err = db_get_errors (session);

  do
    {
      STMT_ERROR_INFO *err_info, *tmp;
      const char *err_msg;
      int line, col;

      line = col = -1;

      db_err = db_get_next_error (db_err, &line, &col);

      err_info = (STMT_ERROR_INFO *) malloc (sizeof (STMT_ERROR_INFO));
      memset (err_info, '\0', sizeof (err_info));
      if (err_info)
	{
	  err_info->line = line;
	  err_info->column = col;
	  err_info->err_code = statement_id;
	  err_msg = api_get_errmsg ();

	  if (err_msg)
	    {
	      err_info->err_msg = strdup (err_msg);
	    }

	  if (pstmt->err_info == NULL)
	    {
	      pstmt->err_info = err_info;
	    }
	  else
	    {
	      tmp = pstmt->err_info;
	      while (tmp->next)
		tmp = tmp->next;

	      tmp->next = err_info;
	    }
	}
    }
  while (db_err);

  return NO_ERROR;
}

static API_RESULTSET_META_IFS RMETA_IFS_ = {
  api_rmeta_get_count,
  api_rmeta_get_info
};

static API_RESULTSET_IFS RS_IFS_ = {
  api_rs_get_resultset_metadata,
  api_rs_fetch,
  api_rs_fetch_tell,
  api_rs_clear_updates,
  api_rs_delete_row,
  api_rs_get_value,
  api_rs_get_value_by_name,
  api_rs_update_value,
  api_rs_apply_row,
  api_rs_destroy
};

/*
 * stmt_remove_error_info -
 *    return:
 *    pstmt():
 */
static int
stmt_remove_error_info (CI_STMT_STRUCTURE * pstmt)
{
  STMT_ERROR_INFO *err_info, *tmp;

  err_info = pstmt->err_info;
  while (err_info)
    {
      tmp = err_info->next;
      if (err_info->err_msg)
	free (err_info->err_msg);
      free (err_info);

      err_info = tmp;
    }
  pstmt->err_info = NULL;

  return NO_ERROR;
}

/*
 * conn_destroyf -
 *    return:
 *    ptr():
 */
static void
conn_destroyf (BH_BIND * ptr)
{
  CI_CONN_STRUCTURE *pconn;
  assert (ptr);

  if (ptr)
    {
      pconn = (CI_CONN_STRUCTURE *) ptr;
      if (pconn->handle_type != HANDLE_TYPE_CONNECTION)
	{
	  return;
	}

      if (pconn->opool)
	{
	  pconn->opool->destroy (pconn->opool);
	}
      free (ptr);
    }
}

/*
 * stmt_destroyf -
 *    return:
 *    ptr():
 */
static void
stmt_destroyf (BH_BIND * ptr)
{
  int i, param_count;
  CI_STMT_STRUCTURE *pstmt;

  assert (ptr);

  if (ptr)
    {
      pstmt = (CI_STMT_STRUCTURE *) ptr;
      if (pstmt->handle_type != HANDLE_TYPE_STATEMENT)
	return;

      stmt_remove_error_info (pstmt);

      if (pstmt->rs_info != NULL)
	{
	  free (pstmt->rs_info);
	}

      if (pstmt->batch_data)
	{
	  bool is_prepared_batch;
	  CI_BATCH_DATA *batch_data, *tmp;
	  is_prepared_batch =
	    (pstmt->stmt_status & CI_STMT_STATUS_PREPARED) ? true : false;

	  batch_data = pstmt->batch_data;
	  for (i = 0; i < pstmt->batch_count && batch_data; i++)
	    {
	      if (is_prepared_batch)
		{
		  if (batch_data->data.val)
		    {
		      DB_VALUE *val;
		      param_count = pstmt->session->parser->host_var_count;

		      for (i = 0, val = batch_data->data.val; i < param_count;
			   i++, val++)
			{
			  pr_clear_value (val);
			}

		      free (batch_data->data.val);
		    }
		}
	      else
		{
		  free (batch_data->data.query_string);
		}

	      tmp = batch_data;
	      batch_data = tmp->next;
	      free (tmp);
	    }
	}

      if (pstmt->param_val)
	{
	  DB_VALUE *val;
	  param_count = pstmt->session->parser->host_var_count;

	  for (i = 0, val = pstmt->param_val; i < param_count; i++, val++)
	    {
	      pr_clear_value (val);
	    }

	  free (pstmt->param_val);
	}

      if (pstmt->session != NULL)
	db_close_session_local (pstmt->session);
      free (pstmt);
    }
}

/*
 * rs_destroyf -
 *    return:
 *    ptr():
 */
static void
rs_destroyf (BH_BIND * ptr)
{
  CI_RESULTSET_STRUCTURE *prs;
  CI_STMT_STRUCTURE *pstmt = NULL;
  BH_INTERFACE *bh_interface;

  assert (ptr);

  if (ptr)
    {
      prs = (CI_RESULTSET_STRUCTURE *) ptr;
      if (prs->handle_type != HANDLE_TYPE_RESULTSET)
	return;

      if (prs->result != NULL)
	{
	  db_query_end (prs->result);
	}

      if (prs->value_table != NULL)
	{
	  prs->value_table->ifs->destroy (prs->value_table);
	}

      bh_interface = prs->bh_interface;
      bh_interface->bind_get_parent (bh_interface, (BH_BIND *) prs,
				     (BH_BIND **) & pstmt);
      if (pstmt != NULL)
	{
	  int retval;
	  int stmt_index = prs->stmt_idx;
	  CI_RESULTSET_META_STRUCTURE *prsmeta = NULL;
	  CI_RESULTSET_METADATA hrsmeta;

	  if (pstmt->rs_info)
	    {
	      prsmeta = pstmt->rs_info[stmt_index].rsmeta;
	      pstmt->rs_info[stmt_index].rs = NULL;
	    }

	  if ((prsmeta != NULL)
	      && !(pstmt->stmt_status | CI_STMT_STATUS_PREPARED))
	    {
	      retval = bh_interface->bind_to_handle (bh_interface,
						     (BH_BIND *) prsmeta,
						     ((BIND_HANDLE *)
						      & hrsmeta));
	      if (retval == NO_ERROR)
		{
		  retval = bh_interface->destroy_handle (bh_interface,
							 (BIND_HANDLE)
							 hrsmeta);
		  pstmt->rs_info[stmt_index].rsmeta = NULL;
		}
	    }
	  if (pstmt->pconn->opt.autocommit
	      && pstmt->pconn->need_defered_commit)
	    {
	      int i, rs_count;

	      rs_count = pstmt->session->dimension;

	      for (i = 0; i < rs_count; i++)
		{
		  if (pstmt->rs_info[i].rs != NULL)
		    {
		      break;
		    }
		}

	      if (i == rs_count)
		{
		  conn_end_tran (bh_interface, pstmt->pconn, true);
		  pstmt->pconn->need_defered_commit = false;
		}
	    }

	}

      free (prs);
    }
}

/*
 * rs_meta_destroyf -
 *    return:
 *    ptr():
 */
static void
rs_meta_destroyf (BH_BIND * ptr)
{
  CI_RESULTSET_META_STRUCTURE *prmeta;

  assert (ptr);

  if (ptr)
    {
      prmeta = (CI_RESULTSET_META_STRUCTURE *) ptr;
      if (prmeta->handle_type != HANDLE_TYPE_RMETA)
	return;

      free (prmeta);
    }
}

/*
 * pmeta_destroyf -
 *    return:
 *    ptr():
 */
static void
pmeta_destroyf (BH_BIND * ptr)
{
  CI_PARAM_META_STRUCTURE *ppmeta;

  assert (ptr);

  if (ptr)
    {
      ppmeta = (CI_PARAM_META_STRUCTURE *) ptr;
      if (ppmeta->handle_type != HANDLE_TYPE_PMETA)
	return;

      free (ppmeta);
    }
}

/*
 * batch_res_destroyf -
 *    return:
 *    ptr():
 */
static void
batch_res_destroyf (BH_BIND * ptr)
{
  int i;
  CI_BATCH_RESULT_STRUCTURE *pbrs;

  assert (ptr);

  if (ptr)
    {
      pbrs = (CI_BATCH_RESULT_STRUCTURE *) ptr;
      if (pbrs->handle_type != HANDLE_TYPE_BATCH_RESULT)
	return;

      for (i = 0; i < pbrs->rs_count; i++)
	{
	  if (pbrs->rs_info[i].err_msg != NULL)
	    {
	      free (pbrs->rs_info[i].err_msg);
	    }
	}

      if (pbrs->rs_info)
	free (pbrs->rs_info);

      free (pbrs);
    }

}

/*
 * init_conn_structure -
 *    return:
 *    pconn():
 */
static int
init_conn_structure (CI_CONN_STRUCTURE * pconn)
{
  assert (pconn);

  memset (pconn, '\0', sizeof (CI_CONN_STRUCTURE));

  pconn->bind.dtor = conn_destroyf;
  pconn->handle_type = HANDLE_TYPE_CONNECTION;

  return NO_ERROR;
}

/*
 * init_stmt_structure -
 *    return:
 *    pstmt():
 */
static int
init_stmt_structure (CI_STMT_STRUCTURE * pstmt)
{
  assert (pstmt);

  memset (pstmt, '\0', sizeof (CI_STMT_STRUCTURE));
  /* This will set every option to zero(false)
   * pstmt->opt.hold_cursors_over_commit = 0;
   * pstmt->opt.updatable_result = 0;
   * pstmt->opt.get_generated_key = 0;
   */

  pstmt->bind.dtor = stmt_destroyf;
  pstmt->handle_type = HANDLE_TYPE_STATEMENT;

  return NO_ERROR;
}

/*
 * init_resultset_structure -
 *    return:
 *    prs():
 */
static int
init_resultset_structure (CI_RESULTSET_STRUCTURE * prs)
{
  assert (prs);
  memset (prs, '\0', sizeof (CI_RESULTSET_STRUCTURE));

  prs->bind.dtor = rs_destroyf;
  prs->handle_type = HANDLE_TYPE_RESULTSET;
  prs->ifs = get_query_rs_ifs ();

  return NO_ERROR;
}

/*
 * init_rmeta_structure -
 *    return:
 *    prmeta():
 */
static int
init_rmeta_structure (CI_RESULTSET_META_STRUCTURE * prmeta)
{
  assert (prmeta);
  memset (prmeta, '\0', sizeof (CI_RESULTSET_META_STRUCTURE));

  prmeta->bind.dtor = rs_meta_destroyf;
  prmeta->handle_type = HANDLE_TYPE_RMETA;
  prmeta->ifs = get_query_rmeta_ifs ();

  return NO_ERROR;
}

/*
 * init_pmeta_structure -
 *    return:
 *    ppmeta():
 */
static int
init_pmeta_structure (CI_PARAM_META_STRUCTURE * ppmeta)
{
  assert (ppmeta);
  memset (ppmeta, '\0', sizeof (CI_PARAM_META_STRUCTURE));

  ppmeta->bind.dtor = pmeta_destroyf;
  ppmeta->handle_type = HANDLE_TYPE_PMETA;

  return NO_ERROR;
}

/*
 * init_batch_rs_structure -
 *    return:
 *    pbrs():
 */
static int
init_batch_rs_structure (CI_BATCH_RESULT_STRUCTURE * pbrs)
{
  assert (pbrs);
  pbrs->handle_type = HANDLE_TYPE_BATCH_RESULT;
  pbrs->bind.dtor = batch_res_destroyf;

  pbrs->rs_count = 0;

  return NO_ERROR;
}

/*
 * api_rs_get_resultset_metadata -
 *    return:
 *    res():
 *    rimpl():
 */
static int
api_rs_get_resultset_metadata (API_RESULTSET * res,
			       API_RESULTSET_META ** rimpl)
{
  CI_RESULTSET_STRUCTURE *prs;
  CI_RESULTSET_META_STRUCTURE *prsmeta;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;
  int stmt_idx, retval;

  assert (res && rimpl);

  prs = (CI_RESULTSET_STRUCTURE *) res;

  /* 1. find prsmeta from prs structure */
  prsmeta = prs->prsmeta;

  if (prsmeta == NULL)
    {
      bh_interface = prs->bh_interface;

      /* 2. find prsmeta from pstmt structure's rs_info */
      retval =
	bh_interface->bind_get_parent (bh_interface, (BH_BIND *) prs,
				       (BH_BIND **) & pstmt);

      if (retval != NO_ERROR)
	{
	  ER_SET_AND_RETURN (retval);
	}

      if (pstmt->handle_type != HANDLE_TYPE_STATEMENT)
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_HANDLE);
	}

      stmt_idx = prs->stmt_idx;

      prsmeta = pstmt->rs_info[stmt_idx].rsmeta;

      if (prsmeta == NULL)
	{
	  /* 3. there is no prsmeta structure,
	   * so now we will make this and return */
	  retval = stmt_bind_resultset_meta_handle (bh_interface,
						    pstmt, stmt_idx,
						    &prsmeta);
	  if (retval != NO_ERROR)
	    {
	      return retval;
	    }
	}
      else
	{
	  prs->prsmeta = pstmt->rs_info[stmt_idx].rsmeta;
	}
    }

  *rimpl = (API_RESULTSET_META *) prsmeta;

  return NO_ERROR;
}

/*
 * api_rs_fetch -
 *    return:
 *    res():
 *    offset():
 *    pos():
 */
static int
api_rs_fetch (API_RESULTSET * res, int offset, CI_FETCH_POSITION pos)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;

  assert (res);
  prs = (CI_RESULTSET_STRUCTURE *) res;

  retval = res_fetch_internal (prs->bh_interface, prs, offset, pos);

  if (retval == NO_ERROR)
    {
      prs->value_table->ifs->reset (prs->value_table);
      prs->current_row_isupdated = prs->current_row_isdeleted = false;
    }

  return retval;
}

/*
 * api_rs_fetch_tell -
 *    return:
 *    res():
 *    offset():
 */
static int
api_rs_fetch_tell (API_RESULTSET * res, int *offset)
{
  CI_RESULTSET_STRUCTURE *prs;
  int tpl_pos;

  prs = (CI_RESULTSET_STRUCTURE *) res;

  tpl_pos = 0;
  switch (prs->result->type)
    {
    case T_SELECT:
      tpl_pos = prs->result->res.s.cursor_id.tuple_no;
      break;
    case T_CALL:
    case T_OBJFETCH:
      break;
    case T_GET:
      tpl_pos = prs->result->res.g.tpl_idx;
      break;
    default:
      break;
    }
  *offset = tpl_pos + 1;

  return NO_ERROR;
}

/*
 * api_rs_clear_updates -
 *    return:
 *    res():
 */
static int
api_rs_clear_updates (API_RESULTSET * res)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;
  CI_STMT_STRUCTURE *pstmt;

  assert (res);
  prs = (CI_RESULTSET_STRUCTURE *) res;

  if (prs->current_row_isdeleted == true)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_ROW_IS_DELETED);
    }

  retval =
    prs->bh_interface->bind_get_parent (prs->bh_interface, (BH_BIND *) prs,
					(BH_BIND **) & pstmt);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  if (pstmt->opt.updatable_result == false)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_RESULTSET_NOT_UPDATABLE);
    }

  if (prs->current_row_isupdated == true)
    {
      retval = prs->value_table->ifs->reset (prs->value_table);
      if (retval == NO_ERROR)
	{
	  prs->current_row_isupdated = false;
	}
    }

  return retval;
}

/*
 * api_rs_delete_row -
 *    return:
 *    res():
 */
static int
api_rs_delete_row (API_RESULTSET * res)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;
  CI_STMT_STRUCTURE *pstmt;

  assert (res);

  prs = (CI_RESULTSET_STRUCTURE *) res;

  if (prs->current_row_isdeleted == true)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_ROW_IS_DELETED);
    }

  retval =
    prs->bh_interface->bind_get_parent (prs->bh_interface, (BH_BIND *) prs,
					(BH_BIND **) & pstmt);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  if (pstmt->opt.updatable_result == false)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_RESULTSET_NOT_UPDATABLE);
    }

  retval = res_delete_row_internal (prs->bh_interface, prs);

  if (retval == NO_ERROR)
    {
      prs->current_row_isdeleted = true;
    }

  return retval;
}

/*
 * api_rs_get_value -
 *    return:
 *    res():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    is_null():
 */
static int
api_rs_get_value (API_RESULTSET * res, int index, CI_TYPE type,
		  void *addr, size_t len, size_t * outlen, bool * is_null)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;

  assert (res);

  prs = (CI_RESULTSET_STRUCTURE *) res;

  /* convert to zero based index */
  index--;

  if (prs->current_row_isdeleted == true)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_ROW_IS_DELETED);
    }

  retval =
    prs->value_table->ifs->get_value (prs->value_table, index, type, addr,
				      len, outlen, is_null);

  return retval;
}

/*
 * api_rs_get_value_by_name -
 *    return:
 *    res():
 *    name():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
api_rs_get_value_by_name (API_RESULTSET * res, const char *name,
			  CI_TYPE type, void *addr, size_t len,
			  size_t * outlen, bool * isnull)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;

  assert (res);

  prs = (CI_RESULTSET_STRUCTURE *) res;


  if (prs->current_row_isdeleted == true)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_ROW_IS_DELETED);
    }

  retval =
    prs->value_table->ifs->get_value_by_name (prs->value_table, name, type,
					      addr, len, outlen, isnull);
  return retval;
}

/*
 * api_rs_update_value -
 *    return:
 *    res():
 *    index():
 *    type():
 *    addr():
 *    len():
 */
static int
api_rs_update_value (API_RESULTSET * res, int index,
		     CI_TYPE type, void *addr, size_t len)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;
  CI_STMT_STRUCTURE *pstmt;

  assert (res);

  prs = (CI_RESULTSET_STRUCTURE *) res;

  if (prs->current_row_isdeleted == true)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_ROW_IS_DELETED);
    }

  retval =
    prs->bh_interface->bind_get_parent (prs->bh_interface, (BH_BIND *) prs,
					(BH_BIND **) & pstmt);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  if (pstmt->opt.updatable_result == false)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_RESULTSET_NOT_UPDATABLE);
    }

  /* convert to zero based index */
  index--;

  retval =
    prs->value_table->ifs->set_value (prs->value_table, index, type, addr,
				      len);

  if (retval == NO_ERROR)
    {
      prs->current_row_isupdated = true;
    }

  return retval;
}

/*
 * api_rs_apply_row -
 *    return:
 *    res():
 */
static int
api_rs_apply_row (API_RESULTSET * res)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;
  CI_STMT_STRUCTURE *pstmt;

  assert (res);

  prs = (CI_RESULTSET_STRUCTURE *) res;

  if (prs->current_row_isdeleted == true)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_ROW_IS_DELETED);
    }

  if (prs->current_row_isupdated == false)
    {
      return NO_ERROR;
    }

  retval =
    prs->bh_interface->bind_get_parent (prs->bh_interface, (BH_BIND *) prs,
					(BH_BIND **) & pstmt);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  if (pstmt->opt.updatable_result == false)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_RESULTSET_NOT_UPDATABLE);
    }

  retval = prs->value_table->ifs->apply_updates (prs->value_table);

  return retval;
}

/*
 * api_rs_destroy -
 *    return:
 *    res():
 */
static void
api_rs_destroy (API_RESULTSET * res)
{
  return;
}

/*
 * api_rmeta_get_count -
 *    return:
 *    impl():
 *    count():
 */
static int
api_rmeta_get_count (API_RESULTSET_META * impl, int *count)
{
  CI_RESULTSET_META_STRUCTURE *prsmeta;

  assert (impl && count);

  prsmeta = (CI_RESULTSET_META_STRUCTURE *) impl;

  *count = prsmeta->col_count;

  return NO_ERROR;
}

/*
 * api_rmeta_get_info -
 *    return:
 *    impl():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
api_rmeta_get_info (API_RESULTSET_META * impl, int index,
		    CI_RMETA_INFO_TYPE type, void *arg, size_t size)
{
  CI_RESULTSET_META_STRUCTURE *prsmeta;
  DB_QUERY_TYPE *query_type;
  int i, retval;
  size_t data_size;

  prsmeta = (CI_RESULTSET_META_STRUCTURE *) impl;

  query_type = prsmeta->query_type;

  if (query_type == NULL)
    {
      /* query_type may be closed */
      ER_SET_AND_RETURN (ER_INTERFACE_RESULTSET_CLOSED);
    }

  for (i = 1; i < index; i++)
    {
      query_type = query_type->next;
    }

  switch (type)
    {
    case CI_RMETA_INFO_COL_LABEL:
      {
	char *data;

	data = (char *) arg;
	if (query_type->original_name == NULL)
	  {
	    *data = '\0';
	  }
	else
	  {
	    data_size = strlen (query_type->original_name);
	    if (data_size > size)
	      {
		ER_SET_AND_RETURN (ER_INTERFACE_NOT_ENOUGH_DATA_SIZE);
	      }

	    memcpy (data, query_type->original_name, data_size);
	    *(data + data_size) = '\0';
	  }
      }
      break;
    case CI_RMETA_INFO_COL_NAME:
      {
	char *data;

	data = (char *) arg;
	if (query_type->name == NULL)
	  {
	    *data = '\0';
	  }
	else
	  {
	    data_size = strlen (query_type->name);
	    if (data_size > size)
	      {
		ER_SET_AND_RETURN (ER_INTERFACE_NOT_ENOUGH_DATA_SIZE);
	      }

	    memcpy (data, query_type->name, data_size);
	    *(data + data_size) = '\0';
	  }
      }
      break;
    case CI_RMETA_INFO_COL_TYPE:
      {
	int *data;
	CI_TYPE xtype = CI_TYPE_NULL;

	data = (int *) arg;
	retval = (int) db_type_to_type (query_type->db_type, &xtype);
	*data = (int) xtype;
      }
      break;
    case CI_RMETA_INFO_PRECISION:
      {
	int *data;

	data = (int *) arg;
	if (query_type->domain)
	  {
	    *data = query_type->domain->precision;
	  }
	else
	  {
	    *data = -1;
	  }
      }
      break;
    case CI_RMETA_INFO_SCALE:
      {
	int *data;
	data = (int *) arg;
	if (query_type->domain)
	  {
	    *data = query_type->domain->scale;
	  }
	else
	  {
	    *data = -1;
	  }
      }
      break;
    case CI_RMETA_INFO_TABLE_NAME:
      {
	char *data;
	const char *class_name;

	data = (char *) arg;
	class_name = NULL;

	if (query_type->domain && query_type->domain->class_mop)
	  {
	    class_name = sm_class_name (query_type->domain->class_mop);
	    if (class_name)
	      {
		data_size = strlen (class_name);
		if (data_size > size)
		  {
		    ER_SET_AND_RETURN (ER_INTERFACE_NOT_ENOUGH_DATA_SIZE);
		  }
		memcpy (data, class_name, data_size);
		*(data + data_size) = '\0';
		break;
	      }
	  }
	if (class_name == NULL)
	  {
	    *data = '\0';
	  }
      }
      break;
    case CI_RMETA_INFO_IS_AUTO_INCREMENT:
      {
	int *data;
	SM_ATTRIBUTE *att;
	SM_CLASS *class;

	data = (int *) arg;
	if (query_type->src_domain && query_type->src_domain->class_mop)
	  {
	    retval = au_fetch_class (query_type->src_domain->class_mop,
				     &class, AU_FETCH_READ, AU_SELECT);
	    if (retval == NO_ERROR)
	      {
		att = classobj_find_attribute (class, query_type->name, 0);
		if (att)
		  {
		    *data = (att->flags & SM_ATTFLAG_AUTO_INCREMENT) ? 1 : 0;
		    break;
		  }
	      }
	  }

	/* error condition */
	*data = 0;
	return NO_ERROR;
      }
      break;
    case CI_RMETA_INFO_IS_NULLABLE:
      {
	int *data;
	SM_ATTRIBUTE *att;
	SM_CLASS *class;

	data = (int *) arg;
	if (query_type->src_domain && query_type->src_domain->class_mop)
	  {
	    retval = au_fetch_class (query_type->src_domain->class_mop,
				     &class, AU_FETCH_READ, AU_SELECT);
	    if (retval == NO_ERROR)
	      {
		att = classobj_find_attribute (class, query_type->name, 0);
		if (att)
		  {
		    *data = (att->flags & SM_ATTFLAG_NON_NULL) ? 0 : 1;
		    break;
		  }
	      }
	  }

	/* error condition */
	*data = 0;
	return NO_ERROR;
      }
      break;
    case CI_RMETA_INFO_IS_WRITABLE:
      {
	int *data;
	BH_INTERFACE *bh_interface;
	CI_STMT_STRUCTURE *pstmt;

	data = (int *) arg;

	bh_interface = prsmeta->bh_interface;
	retval = bh_interface->bind_get_parent (bh_interface,
						(BH_BIND *) prsmeta,
						(BH_BIND **) & pstmt);
	if (retval != NO_ERROR)
	  {
	    *data = 0;
	  }

	*data = (pstmt->opt.updatable_result == true) ? 1 : 0;
      }
      break;

    default:
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  return NO_ERROR;
}

static int
stmt_exec_one_statement (BH_INTERFACE * bh_interface, int stmt_idx,
			 CI_STMT_STRUCTURE * pstmt, CI_RESULTSET * rs, int *r)
{
  CI_RESULTSET hresult;
  CI_RESULTSET_STRUCTURE *rs_ptr;
  DB_QUERY_RESULT *db_q_result;
  int statement_id, affected_row;
  DB_SESSION *session = pstmt->session;
  int retval;
  CI_CONNECTION conn;

  bh_interface->bind_to_handle (bh_interface, (BH_BIND *) pstmt->pconn,
				&conn);

  statement_id = db_compile_statement_local (session);
  if (statement_id < 0)
    {
      if (pstmt->opt.exec_continue_on_error == false)
	{
	  stmt_remove_error_info (pstmt);
	}

      stmt_make_error_info (pstmt, statement_id);

      if (pstmt->opt.exec_continue_on_error
	  && statement_id != ER_IT_EMPTY_STATEMENT)
	{
	  pstmt->rs_info[stmt_idx].metainfo.affected_row = -1;

	  return NO_ERROR;
	}
      else
	{
	  return statement_id;
	}
    }
#if defined(CS_MODE)
  session->parser->exec_mode =
    (pstmt->opt.async_query) ? ASYNC_EXEC : SYNC_EXEC;
#else
  session->parser->exec_mode = SYNC_EXEC;
#endif
  pstmt->rs_info[stmt_idx].metainfo.sql_type =
    db_get_statement_type (session, stmt_idx + 1);
  if (HAS_RESULT (pstmt->rs_info[stmt_idx].metainfo.sql_type))
    {
      pstmt->rs_info[stmt_idx].metainfo.has_result = true;
      pstmt->pconn->need_defered_commit = true;
      pstmt->pconn->need_immediate_commit = false;
    }
  else
    {
      pstmt->rs_info[stmt_idx].metainfo.has_result = false;
      pstmt->pconn->need_defered_commit = false;
      pstmt->pconn->need_immediate_commit = true;
    }

  retval =
    db_execute_and_keep_statement (session, statement_id, &db_q_result);
  if (retval >= 0)
    {
      affected_row = retval;

      if (pstmt->rs_info[stmt_idx].metainfo.has_result == true)
	{
	  rs_ptr = (CI_RESULTSET_STRUCTURE *)
	    malloc (sizeof (CI_RESULTSET_STRUCTURE));

	  if (rs_ptr == NULL)
	    {
	      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
	    }

	  init_resultset_structure (rs_ptr);
	  rs_ptr->result = db_q_result;
	  rs_ptr->stmt_idx = stmt_idx;
	  rs_ptr->bh_interface = bh_interface;
	  retval = create_resultset_value_table (db_q_result->col_cnt,
						 (BIND_HANDLE) conn,
						 rs_ptr,
						 &(rs_ptr->value_table));

	  if (retval != NO_ERROR)
	    {
	      free (rs_ptr);
	      ER_SET_AND_RETURN (retval);
	    }

	  retval =
	    bh_interface->alloc_handle (bh_interface, (BH_BIND *) rs_ptr,
					&hresult);

	  if (retval != NO_ERROR)
	    {
	      free (rs_ptr);
	      ER_SET_AND_RETURN (retval);
	    }

	  retval = bh_interface->bind_graft (bh_interface,
					     (BH_BIND *) rs_ptr,
					     (BH_BIND *) pstmt);

	  if (retval != NO_ERROR)
	    {
	      bh_interface->destroy_handle (bh_interface, hresult);
	      ER_SET_AND_RETURN (retval);
	    }
	  pstmt->rs_info[stmt_idx].rs = rs_ptr;
	}

      pstmt->rs_info[stmt_idx].metainfo.affected_row = affected_row;

      if (r)
	{
	  *r = affected_row;
	}

      if (rs && pstmt->rs_info[stmt_idx].metainfo.has_result == true)
	{
	  *rs = hresult;
	}
    }
  else
    {
      pstmt->rs_info[stmt_idx].metainfo.affected_row = -1;

      if (pstmt->opt.exec_continue_on_error == false)
	{
	  stmt_remove_error_info (pstmt);
	}

      stmt_make_error_info (pstmt, retval);

      if (pstmt->opt.exec_continue_on_error == false)
	{
	  return retval;
	}
    }

  return NO_ERROR;
}

/*
 * stmt_exec_immediate_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    sql():
 *    rs():
 *    r():
 */
static int
stmt_exec_immediate_internal (BH_INTERFACE * bh_interface,
			      CI_STMT_STRUCTURE * pstmt, char *sql,
			      size_t len, CI_RESULTSET * rs, int *r)
{
  DB_SESSION *session;
  int retval, i;
  CI_RESULTSET current_rs;
  int current_r;

  assert (pstmt && sql);

  if (pstmt->rs_info != NULL)
    {
      free (pstmt->rs_info);
    }

  retval = stmt_reset_session_and_parse (pstmt, sql, len);

  if (retval != NO_ERROR)
    {
      return retval;
    }

  pstmt->current_rs_idx = 0;

  session = pstmt->session;

  pstmt->rs_info = (STMT_RESULT_INFO *)
    malloc (sizeof (STMT_RESULT_INFO) * session->dimension);

  memset (pstmt->rs_info, '\0',
	  sizeof (STMT_RESULT_INFO) * session->dimension);

  if (!pstmt->rs_info)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  if (pstmt->opt.lazy_exec == true)
    {
      retval = stmt_exec_one_statement (bh_interface, 0, pstmt, rs, r);
    }
  else
    {
      for (i = 0; i < session->dimension; i++)
	{
	  retval =
	    stmt_exec_one_statement (bh_interface, i, pstmt,
				     &current_rs, &current_r);

	  if (i == 0)
	    {
	      *rs = current_rs;
	      *r = current_r;
	    }
	}

      if (pstmt->pconn->opt.autocommit == true)
	{
	  for (i = 0; i < session->dimension; i++)
	    {
	      if (pstmt->rs_info[i].metainfo.has_result == true)
		{
		  pstmt->pconn->need_immediate_commit = false;
		  pstmt->pconn->need_defered_commit = true;
		  break;
		}
	      else
		{
		  pstmt->pconn->need_immediate_commit = true;
		}
	    }
	}
    }

  return retval;
}

/*
 * stmt_prepare_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    sql():
 *    len():
 */
static int
stmt_prepare_internal (BH_INTERFACE * bh_interface,
		       CI_STMT_STRUCTURE * pstmt, const char *sql, size_t len)
{
  int retval, i, host_var_count;
  DB_SESSION *session;
  int statement_id;

  assert (pstmt != NULL);

  if (pstmt->rs_info != NULL)
    {
      free (pstmt->rs_info);
    }

  retval = stmt_reset_session_and_parse (pstmt, sql, len);
  session = pstmt->session;

  pstmt->current_rs_idx = 0;

  pstmt->rs_info = (STMT_RESULT_INFO *)
    malloc (sizeof (STMT_RESULT_INFO) * session->dimension);

  if (!pstmt->rs_info)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  memset (pstmt->rs_info, '\0', sizeof (pstmt->rs_info));

  if (retval == NO_ERROR)
    {
      for (i = 0; i < session->dimension; i++)
	{
	  statement_id = db_compile_statement_local (session);

	  if (statement_id < 0)
	    {
	      stmt_remove_error_info (pstmt);
	      stmt_make_error_info (pstmt, statement_id);

	      return statement_id;
	    }

	  pstmt->rs_info[i].metainfo.sql_type =
	    db_get_statement_type (session, i + 1);
	  pstmt->rs_info[i].metainfo.has_result =
	    (HAS_RESULT (pstmt->rs_info[i].metainfo.sql_type)) ? true : false;
	}

      host_var_count = pstmt->session->parser->host_var_count;

      if (pstmt->param_val != NULL)
	{
	  DB_VALUE *val;
	  for (i = 0, val = pstmt->param_val; i < host_var_count; i++, val++)
	    {
	      pr_clear_value (val);
	    }

	  free (pstmt->param_val);
	  pstmt->param_val = NULL;
	}

      if (pstmt->param_value_is_set != NULL)
	{
	  free (pstmt->param_value_is_set);
	  pstmt->param_value_is_set = NULL;
	}

      if (host_var_count != 0)
	{
	  pstmt->param_val =
	    (DB_VALUE *) malloc (sizeof (DB_VALUE) * host_var_count);
	  pstmt->param_value_is_set =
	    (bool *) malloc (sizeof (bool) * host_var_count);

	  if (!(pstmt->param_val) || !(pstmt->param_value_is_set))
	    {
	      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
	    }

	  memset (pstmt->param_val, '\0', sizeof (pstmt->param_val));
	  memset (pstmt->param_value_is_set, '\0',
		  sizeof (pstmt->param_value_is_set));
	}
    }

  return retval;
}

/*
 * stmt_get_parameter_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
stmt_get_parameter_internal (BH_INTERFACE * bh_interface,
			     CI_STMT_STRUCTURE * pstmt, int index,
			     CI_TYPE type, void *addr, size_t len,
			     size_t * outlen, bool * isnull)
{
  int retval;
  DB_VALUE *dbval;

  if (index <= 0 || index > pstmt->session->parser->host_var_count)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  if ((pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
      && (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      /* read db values from session->parser->host_variables */
      dbval = pstmt->session->parser->host_variables;
    }
  else
    {
      /* read db values from stmt's val_table if (is_set == true) */
      if ((pstmt->param_value_is_set == NULL)
	  || (pstmt->param_value_is_set[index - 1] == false))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_PARAM_IS_NOT_SET);
	}
      dbval = pstmt->param_val;
    }

  dbval += (index - 1);

  retval =
    coerce_db_value_to_value (dbval, (UINT64) 0, type, addr, len,
			      outlen, isnull);

  return retval;
}

/*
 * stmt_set_parameter_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    index():
 *    type():
 *    val():
 *    size():
 */
static int
stmt_set_parameter_internal (BH_INTERFACE * bh_interface,
			     CI_STMT_STRUCTURE * pstmt, int index,
			     CI_TYPE type, void *val, size_t size)
{
  int retval, i;
  DB_VALUE *dbval;

  if (index <= 0 || index > pstmt->session->parser->host_var_count)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  dbval = pstmt->param_val;
  for (i = 1; i < index; i++)
    dbval++;

  retval = coerce_value_to_db_value (type, val, size, dbval, false);

  if (retval == NO_ERROR)
    {
      pstmt->param_value_is_set[index - 1] = true;
    }

  return retval;
}

/*
 * stmt_reset_session_and_parse -
 *    return:
 *    pstmt():
 *    sql():
 *    len():
 */
static int
stmt_reset_session_and_parse (CI_STMT_STRUCTURE * pstmt,
			      const char *sql, size_t len)
{
  DB_SESSION *session;
  DB_SESSION_ERROR *pterror = NULL;

  if (pstmt->session != NULL)
    {
      db_close_session_local (pstmt->session);
    }

  pstmt->session = (DB_SESSION *) malloc (sizeof (DB_SESSION));
  if (pstmt->session == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  memset (pstmt->session, '\0', sizeof (DB_SESSION));
  pstmt->session->parser = parser_create_parser ();
  if (pstmt->session->parser == NULL)
    {
      free (pstmt->session);
      pstmt->session = NULL;
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  session = pstmt->session;

  session->include_oid =
    (pstmt->opt.updatable_result) ? DB_ROW_OIDS : DB_NO_OIDS;
  session->statements = parser_parse_binary (session->parser, sql, len);
  pterror = pt_get_errors (session->parser);

  if (pterror != NULL)
    {
      stmt_make_error_info (pstmt, api_get_errid ());
      db_close_session_local (pstmt->session);
      pstmt->session = NULL;
      return api_get_errid ();
    }

  if (session->statements)
    {
      PT_NODE **tmp = session->statements;

      if (tmp == NULL)
	{
	  session->dimension = 0;
	}
      else
	{
	  while (*tmp++)
	    {
	      (session->dimension)++;
	    }
	}
    }
  else
    {
      STMT_ERROR_INFO *err_info, *tmp;

      ci_err_set (ER_IT_EMPTY_STATEMENT);
      err_info = (STMT_ERROR_INFO *) malloc (sizeof (STMT_ERROR_INFO));
      memset (err_info, '\0', sizeof (err_info));

      if (err_info)
	{
	  const char *err_msg;

	  err_info->line = 0;
	  err_info->column = 0;
	  err_info->err_code = ER_IT_EMPTY_STATEMENT;
	  err_msg = api_get_errmsg ();

	  if (err_msg)
	    {
	      err_info->err_msg = strdup (err_msg);
	    }

	  if (pstmt->err_info == NULL)
	    {
	      pstmt->err_info = err_info;
	    }
	  else
	    {
	      tmp = pstmt->err_info;
	      while (tmp->next)
		tmp = tmp->next;

	      tmp->next = err_info;
	    }
	}

      db_close_session_local (pstmt->session);
      pstmt->session = NULL;
      return ER_IT_EMPTY_STATEMENT;
    }

  return NO_ERROR;
}

/*
 * stmt_exec_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    rs():
 *    r():
 */
static int
stmt_exec_internal (BH_INTERFACE * bh_interface,
		    CI_STMT_STRUCTURE * pstmt, CI_RESULTSET * rs, int *r)
{
  DB_SESSION *session;
  int retval, i, affected_row, j;
  CI_CONNECTION conn;

  assert (pstmt);

  session = pstmt->session;

  memset (pstmt->rs_info, '\0', sizeof (pstmt->rs_info));

  pstmt->current_rs_idx = 0;

  for (j = 0; j < pstmt->session->parser->host_var_count; j++)
    {
      if (pstmt->param_value_is_set[j] != true)
	{
	  ER_SET_AND_RETURN (ER_UCI_TOO_FEW_HOST_VARS);
	}
    }

  db_push_values (session, pstmt->session->parser->host_var_count,
		  pstmt->param_val);

  bh_interface->bind_to_handle (bh_interface, (BH_BIND *) pstmt->pconn,
				&conn);

  for (i = 0; i < session->dimension; i++)
    {
      CI_RESULTSET hresult;
      CI_RESULTSET_STRUCTURE *rs_ptr;
      DB_QUERY_RESULT *db_q_result;
      CI_RESULTSET_META_STRUCTURE *pnext;

      retval = db_execute_and_keep_statement (session, i + 1, &db_q_result);
      if (retval >= 0)
	{
	  affected_row = retval;

	  if (pstmt->rs_info[i].metainfo.has_result == true)
	    {
	      rs_ptr = (CI_RESULTSET_STRUCTURE *)
		malloc (sizeof (CI_RESULTSET_STRUCTURE));
	      if (rs_ptr == NULL)
		{
		  ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
		}

	      init_resultset_structure (rs_ptr);
	      rs_ptr->result = db_q_result;
	      rs_ptr->stmt_idx = i;
	      rs_ptr->bh_interface = bh_interface;
	      retval =
		create_resultset_value_table (db_q_result->col_cnt,
					      (BIND_HANDLE) conn,
					      rs_ptr, &(rs_ptr->value_table));

	      if (retval != NO_ERROR)
		{
		  free (rs_ptr);
		  ER_SET_AND_RETURN (retval);
		}

	      retval = bh_interface->bind_get_first_child (bh_interface,
							   (BH_BIND *)
							   pstmt,
							   (BH_BIND **)
							   & pnext);

	      while (pnext != NULL)
		{
		  if ((pnext->handle_type == HANDLE_TYPE_RMETA)
		      && (pnext->stmt_idx == i))
		    {
		      rs_ptr->prsmeta = pnext;
		      break;
		    }
		  else
		    {
		      BH_BIND *tmp;
		      tmp = (BH_BIND *) pnext;
		      retval =
			bh_interface->
			bind_get_next_sibling (bh_interface, tmp,
					       (BH_BIND **) & pnext);
		    }
		}

	      retval =
		bh_interface->alloc_handle (bh_interface,
					    (BH_BIND *) rs_ptr, &hresult);

	      if (retval != NO_ERROR)
		{
		  free (rs_ptr);
		  ER_SET_AND_RETURN (retval);
		}

	      retval = bh_interface->bind_graft (bh_interface,
						 (BH_BIND *) rs_ptr,
						 (BH_BIND *) pstmt);

	      if (retval != NO_ERROR)
		{
		  bh_interface->destroy_handle (bh_interface, hresult);
		  ER_SET_AND_RETURN (retval);
		}

	      pstmt->rs_info[i].rs = rs_ptr;

	      pstmt->pconn->need_defered_commit = true;
	    }
	  else
	    {
	      pstmt->pconn->need_immediate_commit = true;
	    }

	  pstmt->rs_info[i].metainfo.affected_row = affected_row;

	  if (i == 0)
	    {
	      /* set the first query's result info into out param */
	      if (r)
		*r = affected_row;
	      if (rs && pstmt->rs_info[0].metainfo.has_result == true)
		*rs = hresult;
	    }
	}
      else
	{
	  pstmt->rs_info[i].metainfo.affected_row = -1;

	  if (pstmt->opt.exec_continue_on_error == false)
	    {
	      stmt_remove_error_info (pstmt);
	    }

	  stmt_make_error_info (pstmt, retval);

	  if (pstmt->opt.exec_continue_on_error == false)
	    {
	      return retval;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * stmt_get_resultset_metadata_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    r():
 */
static int
stmt_get_resultset_metadata_internal (BH_INTERFACE * bh_interface,
				      CI_STMT_STRUCTURE * pstmt,
				      CI_RESULTSET_METADATA * r)
{
  int retval;
  CI_RESULTSET_META_STRUCTURE *prmeta;
  STMT_RESULT_INFO *stmt_rs_info;

  assert (pstmt->rs_info);

  stmt_rs_info = &(pstmt->rs_info[pstmt->current_rs_idx]);

  assert (stmt_rs_info);

  if (stmt_rs_info->metainfo.has_result == false)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_HAS_NO_RESULT_SET);
    }

  if (stmt_rs_info->rsmeta == NULL)
    {
      /* make rsmeta handle & structure */
      retval = stmt_bind_resultset_meta_handle (bh_interface,
						pstmt,
						pstmt->current_rs_idx,
						&prmeta);
      if (retval != NO_ERROR)
	{
	  ER_SET_AND_RETURN (retval);
	}
    }
  else
    {
      prmeta = stmt_rs_info->rsmeta;
    }

  retval = bh_interface->bind_to_handle (bh_interface, (BH_BIND *) prmeta,
					 (BIND_HANDLE *) r);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  return NO_ERROR;
}

/*
 * get_query_rmeta_ifs -
 *    return:
 */
static API_RESULTSET_META_IFS *
get_query_rmeta_ifs (void)
{
  return &RMETA_IFS_;
}

/*
 * get_query_rs_ifs -
 *    return:
 */
static API_RESULTSET_IFS *
get_query_rs_ifs (void)
{
  return &RS_IFS_;
}

/*
 * stmt_bind_resultset_meta_handle -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    stmt_idx():
 *    outmeta():
 */
static int
stmt_bind_resultset_meta_handle (BH_INTERFACE * bh_interface,
				 CI_STMT_STRUCTURE * pstmt,
				 int stmt_idx,
				 CI_RESULTSET_META_STRUCTURE ** outmeta)
{
  int retval;
  CI_RESULTSET_META_STRUCTURE *prsmeta;
  CI_RESULTSET_METADATA hrmeta;

  prsmeta = (CI_RESULTSET_META_STRUCTURE *) malloc
    (sizeof (CI_RESULTSET_META_STRUCTURE));

  if (prsmeta == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  init_rmeta_structure (prsmeta);
  prsmeta->stmt_idx = stmt_idx;
  prsmeta->query_type = db_get_query_type_list (pstmt->session, stmt_idx + 1);
  prsmeta->col_count = get_col_count (prsmeta->query_type);
  prsmeta->bh_interface = bh_interface;

  retval = bh_interface->alloc_handle (bh_interface,
				       (BH_BIND *) prsmeta,
				       (BIND_HANDLE *) & hrmeta);

  if (retval != NO_ERROR)
    {
      free (prsmeta);
      ER_SET_AND_RETURN (retval);
    }

  retval = bh_interface->bind_graft (bh_interface,
				     (BH_BIND *) prsmeta, (BH_BIND *) pstmt);

  if (retval != NO_ERROR)
    {
      bh_interface->destroy_handle (bh_interface, (BIND_HANDLE) hrmeta);
      ER_SET_AND_RETURN (retval);
    }

  if (pstmt->rs_info[stmt_idx].rs)
    {
      pstmt->rs_info[stmt_idx].rs->prsmeta = prsmeta;
      prsmeta->query_type = pstmt->rs_info[stmt_idx].rs->result->query_type;
    }

  pstmt->rs_info[stmt_idx].rsmeta = prsmeta;

  *outmeta = prsmeta;

  return NO_ERROR;
}

/*
 * res_fetch_internal -
 *    return:
 *    bh_interface():
 *    prs():
 *    offset():
 *    pos():
 */
static int
res_fetch_internal (BH_INTERFACE * bh_interface,
		    CI_RESULTSET_STRUCTURE * prs, int offset,
		    CI_FETCH_POSITION pos)
{
  int retval;
  DB_QUERY_RESULT *query_result;
  assert (prs);

  query_result = prs->result;

  retval = db_query_seek_tuple (query_result, offset, pos);

  if (retval == DB_CURSOR_END)
    return ER_INTERFACE_END_OF_CURSOR;

  if (retval != DB_CURSOR_SUCCESS)
    {
      return er_errid ();
    }


  return NO_ERROR;
}

/*
 * res_delete_row_internal -
 *    return:
 *    bh_interface():
 *    prs():
 */
static int
res_delete_row_internal (BH_INTERFACE * bh_interface,
			 CI_RESULTSET_STRUCTURE * prs)
{
  int retval;
  DB_VALUE tpl_oid;
  DB_OBJECT *obj;

  assert (bh_interface && prs);

  retval = cursor_get_current_oid (&(prs->result->res.s.cursor_id), &tpl_oid);

  if (retval != NO_ERROR)
    {
      return er_errid ();
    }

  if ((tpl_oid.domain.general_info.is_null)
      || (tpl_oid.domain.general_info.type != DB_TYPE_OBJECT))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_GENERIC);
    }

  obj = tpl_oid.data.op;

  retval = obj_delete (obj);

  return retval;
}

/*
 * get_col_count -
 *    return:
 *    query_type():
 */
static int
get_col_count (DB_QUERY_TYPE * query_type)
{
  int count;
  DB_QUERY_TYPE *tmp;

  tmp = query_type;
  count = 0;

  while (tmp)
    {
      count++;
      tmp = tmp->next;
    }

  return count;
}

/*
 * rs_get_index_by_name -
 *    return:
 *    impl():
 *    name():
 *    ri():
 */
static int
rs_get_index_by_name (void *impl, const char *name, int *ri)
{
  int i, col_count;
  CI_RESULTSET_STRUCTURE *prs;
  DB_QUERY_TYPE *query_type;

  assert (ri);

  prs = (CI_RESULTSET_STRUCTURE *) impl;

  col_count = prs->result->col_cnt;
  query_type = prs->result->query_type;
  for (i = 0; i < col_count && (query_type != NULL); i++)
    {
      if (query_type->attr_name != NULL)
	{
	  if (strcasecmp (name, query_type->attr_name) == 0)
	    {
	      *ri = i;
	      return NO_ERROR;
	    }
	  else
	    {
	      query_type = query_type->next;
	    }
	}
    }

  if (query_type == NULL)
    {
      ER_SET_AND_RETURN (ER_QPROC_INVALID_COLNAME);
    }

  return NO_ERROR;
}

/*
 * rs_get_db_value -
 *    return:
 *    impl():
 *    index():
 *    val():
 */
static int
rs_get_db_value (void *impl, int index, DB_VALUE * val)
{
  int retval;
  CI_RESULTSET_STRUCTURE *prs;

  assert (impl);

  prs = (CI_RESULTSET_STRUCTURE *) impl;

  retval = db_query_get_tuple_value (prs->result, index, val);

  return retval;
}

/*
 * rs_set_db_value -
 *    return:
 *    impl():
 *    index():
 *    val():
 */
static int
rs_set_db_value (void *impl, int index, DB_VALUE * val)
{
  int i, retval;
  DB_VALUE tpl_oid;
  DB_OBJECT *obj;
  CI_RESULTSET_STRUCTURE *prs;
  DB_QUERY_TYPE *query_type;
  const char *att_name;

  assert (impl);

  prs = (CI_RESULTSET_STRUCTURE *) impl;

  assert (prs->handle_type == HANDLE_TYPE_RESULTSET);

  retval = cursor_get_current_oid (&(prs->result->res.s.cursor_id), &tpl_oid);
  if (retval != NO_ERROR)
    {
      return er_errid ();
    }

  if ((tpl_oid.domain.general_info.is_null)
      || (tpl_oid.domain.general_info.type != DB_TYPE_OBJECT))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_GENERIC);
    }

  obj = tpl_oid.data.op;

  if (index < 0 || index > prs->result->col_cnt)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  if (prs->prsmeta != NULL)
    {
      query_type = prs->prsmeta->query_type;
    }
  else
    {
      query_type = prs->result->query_type;

      for (i = 0; i < index; i++)
	{
	  if (query_type == NULL)
	    {
	      ER_SET_AND_RETURN (ER_INTERFACE_GENERIC);
	    }

	  query_type = query_type->next;
	}
    }

  att_name = query_type->attr_name;

  if (att_name == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_AVAILABLE_INFORMATION);
    }

  retval = do_check_partitioned_class (db_get_class (obj),
				       CHECK_PARTITION_NONE,
				       (char *) att_name);

  if (retval != NO_ERROR)
    {
      return retval;
    }

  retval = obj_set (obj, att_name, val);

  return retval;
}

/*
 * rs_init_domain -
 *    return:
 *    impl():
 *    index():
 *    val():
 */
static int
rs_init_domain (void *impl, int index, DB_VALUE * val)
{
  int i, retval;
  CI_RESULTSET_STRUCTURE *prs;
  DB_QUERY_TYPE *query_type;
  int precision, scale;

  assert (impl && val);

  prs = (CI_RESULTSET_STRUCTURE *) impl;

  assert (prs->handle_type == HANDLE_TYPE_RESULTSET);

  if (index < 0 || index > prs->result->col_cnt)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  if (prs->prsmeta != NULL)
    {
      query_type = prs->prsmeta->query_type;
    }
  else
    {
      query_type = prs->result->query_type;

      for (i = 0; i < index; i++)
	{
	  if (query_type == NULL)
	    {
	      ER_SET_AND_RETURN (ER_INTERFACE_GENERIC);
	    }

	  query_type = query_type->next;
	}
    }

  if (query_type == NULL || query_type->domain == NULL)
    {
      precision = DB_DEFAULT_PRECISION;
      scale = DB_DEFAULT_SCALE;
    }
  else
    {
      precision = query_type->domain->precision;
      scale = query_type->domain->scale;
    }

  retval = db_value_domain_init (val, query_type->db_type, precision, scale);

  return retval;
}

/*
 * create_resultset_value_table -
 *    return:
 *    num_col():
 *    conn():
 *    prs():
 *    value_table():
 */
static int
create_resultset_value_table (int num_col, BIND_HANDLE conn,
			      CI_RESULTSET_STRUCTURE * prs,
			      VALUE_BIND_TABLE ** value_table)
{
  return create_db_value_bind_table (num_col,
				     (void *) prs,
				     0,
				     (BIND_HANDLE) conn,
				     rs_get_index_by_name,
				     rs_get_db_value,
				     rs_set_db_value,
				     rs_init_domain, &(prs->value_table));
}

/*
 * pmeta_get_info_internal -
 *    return:
 *    bh_interface():
 *    ppmeta():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
pmeta_get_info_internal (BH_INTERFACE * bh_interface,
			 CI_PARAM_META_STRUCTURE * ppmeta,
			 int index, CI_PMETA_INFO_TYPE type,
			 void *arg, size_t size)
{
  DB_MARKER *marker = NULL;
  assert (ppmeta && arg);
  DB_TYPE domain_type;
  int retval, i;

  if (index <= 0 || index > ppmeta->param_count)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  marker = ppmeta->marker;

  switch (type)
    {
    case CI_PMETA_INFO_MODE:
      {
	int *value;
	value = (int *) arg;
	*value = (ppmeta->is_out_param[index - 1]) ? 1 : 0;
	return NO_ERROR;
      }
      break;
    case CI_PMETA_INFO_COL_TYPE:
      {
	int *val;
	CI_TYPE xtype;
	val = (int *) arg;
	for (i = 1; i < index; i++)
	  {
	    marker = (DB_MARKER *) pt_node_next ((PT_NODE *) marker);
	    if (marker == NULL)
	      {
		ER_SET_AND_RETURN (ER_INTERFACE_GENERIC);
	      }
	  }

	domain_type = pt_type_enum_to_db (marker->type_enum);
	retval = db_type_to_type (domain_type, &xtype);
	if (retval < 0)
	  {
	    return retval;
	  }
	*val = xtype;
	return NO_ERROR;
      }
      break;
    case CI_PMETA_INFO_PRECISION:
    case CI_PMETA_INFO_SCALE:
      {
	int *val;
	val = (int *) arg;
	for (i = 1; i < index; i++)
	  {
	    marker = (DB_MARKER *) pt_node_next ((PT_NODE *) marker);
	    if (marker == NULL)
	      {
		ER_SET_AND_RETURN (ER_INTERFACE_GENERIC);
	      }
	  }
	if (marker->expected_domain)
	  {
	    *val = (type == CI_PMETA_INFO_PRECISION) ?
	      marker->expected_domain->precision :
	      marker->expected_domain->scale;
	  }
	else
	  {
	    ER_SET_AND_RETURN (ER_INTERFACE_NO_AVAILABLE_INFORMATION);
	  }

	return NO_ERROR;
      }
      break;
    case CI_PMETA_INFO_NULLABLE:
      {
	int *val;
	val = (int *) arg;
	*val = 1;
	return NO_ERROR;
      }
    default:
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  return NO_ERROR;
}

/*
 * stmt_add_batch_param -
 *    return:
 *    pstmt():
 *    val():
 *    num_val():
 */
static int
stmt_add_batch_param (CI_STMT_STRUCTURE * pstmt, DB_VALUE * val, int num_val)
{
  CI_BATCH_DATA *batch_data, *tmp;
  DB_VALUE *src_val, *dest_val;
  int i;

  batch_data = (CI_BATCH_DATA *) malloc (sizeof (CI_BATCH_DATA));

  if (!batch_data)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  memset (batch_data, '\0', sizeof (CI_BATCH_DATA));

  batch_data->data.val = (DB_VALUE *) malloc (sizeof (DB_VALUE) * num_val);
  if (batch_data->data.val == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  for (i = 0, src_val = pstmt->param_val, dest_val =
       batch_data->data.val; i < num_val; i++, src_val++, dest_val++)
    {
      pr_clone_value (src_val, dest_val);
    }

  tmp = pstmt->batch_data;
  if (pstmt->batch_data == NULL)
    {
      pstmt->batch_data = batch_data;
    }
  else
    {
      tmp = pstmt->batch_data;
      while (tmp->next != NULL)
	{
	  tmp = tmp->next;
	}

      tmp->next = batch_data;
    }

  return NO_ERROR;
}

/*
 * stmt_exec_prepared_batch_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    br():
 */
static int
stmt_exec_prepared_batch_internal (BH_INTERFACE * bh_interface,
				   CI_STMT_STRUCTURE * pstmt,
				   CI_BATCH_RESULT * br)
{
  DB_SESSION *session;
  int i, retval;
  bool has_result;
  CI_BATCH_RESULT_STRUCTURE *pbrs = NULL;
  CI_BATCH_DATA *batch_data, *tmp;

  session = pstmt->session;
  batch_data = pstmt->batch_data;

  retval = stmt_batch_result_free (bh_interface, pstmt);

  if (retval != NO_ERROR)
    return retval;

  retval =
    stmt_batch_alloc_and_bind_new_result (bh_interface, pstmt, &pbrs, br);

  if (retval != NO_ERROR)
    return retval;

  session = pstmt->session;

  has_result =
    (HAS_RESULT (db_get_statement_type (session, 1))) ? true : false;

  for (i = 0; i < pstmt->batch_count; i++)
    {
      DB_QUERY_RESULT *db_q_result;

      db_push_values (session, pstmt->session->parser->host_var_count,
		      batch_data->data.val);

      retval = db_execute_and_keep_statement (session, 1, &db_q_result);
      if (retval >= 0)
	{
	  pbrs->rs_info[i].metainfo.affected_row = retval;
	  pbrs->rs_info[i].err_code = NO_ERROR;
	}
      else
	{
	  pbrs->rs_info[i].err_code = retval;
	  pbrs->rs_info[i].err_msg = strdup (api_get_errmsg ());
	}

      db_query_end (db_q_result);
      batch_data = batch_data->next;
    }

  batch_data = pstmt->batch_data;
  for (i = 0; i < pstmt->batch_count && batch_data; i++)
    {
      if (batch_data)
	free (batch_data->data.query_string);

      tmp = batch_data;
      batch_data = tmp->next;
      free (tmp);
    }

  pstmt->batch_count = 0;
  pstmt->batch_data = NULL;

  return NO_ERROR;
}

/*
 * stmt_exec_batch_query_internal -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    br():
 */
static int
stmt_exec_batch_query_internal (BH_INTERFACE * bh_interface,
				CI_STMT_STRUCTURE * pstmt,
				CI_BATCH_RESULT * br)
{
  DB_SESSION *session;
  int i, retval;
  CI_BATCH_RESULT_STRUCTURE *pbrs = NULL;
  CI_BATCH_DATA *batch_data, *tmp;

  session = pstmt->session;
  batch_data = pstmt->batch_data;

  retval = stmt_batch_result_free (bh_interface, pstmt);

  if (retval != NO_ERROR)
    return retval;

  retval =
    stmt_batch_alloc_and_bind_new_result (bh_interface, pstmt, &pbrs, br);

  if (retval != NO_ERROR)
    return retval;

  for (i = 0; i < pstmt->batch_count; i++)
    {
      DB_SESSION *session;
      DB_QUERY_RESULT *db_q_result;
      int statement_id;
      char *query_string;
      size_t length;
      PT_NODE *pterror;

      session = (DB_SESSION *) malloc (sizeof (DB_SESSION));
      memset (session, '\0', sizeof (DB_SESSION));

      if (session == NULL)
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
	}

      session->parser = parser_create_parser ();
      if (session->parser == NULL)
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
	}

      session->include_oid = DB_NO_OIDS;

      query_string = batch_data->data.query_string;
      length = batch_data->query_length;

      session->statements =
	parser_parse_binary (session->parser, query_string, length);
      pterror = pt_get_errors (session->parser);

      if (pterror != NULL)
	{
	  int line, col;
	  const char *msg;

	  db_get_next_error (pterror, &line, &col);

	  pbrs->rs_info[i].err_code = api_get_errid ();
	  msg = api_get_errmsg ();

	  if (msg)
	    {
	      pbrs->rs_info[i].err_msg = strdup (msg);
	    }

	  db_close_session_local (session);
	  batch_data = batch_data->next;
	  continue;
	}

      if (session->statements)
	{
	  PT_NODE **tmp = session->statements;
	  if (tmp == NULL)
	    session->dimension = 0;
	  else
	    while (*tmp++)
	      (session->dimension)++;
	}

      statement_id = db_compile_statement_local (session);
      if (statement_id < 0)
	{
	  const char *err = api_get_errmsg ();

	  pbrs->rs_info[i].err_code = statement_id;
	  if (err)
	    pbrs->rs_info[i].err_msg = strdup (err);

	  db_close_session_local (session);
	  batch_data = batch_data->next;
	  continue;
	}

#if defined(CS_MODE)
      session->parser->exec_mode =
	(pstmt->opt.async_query) ? ASYNC_EXEC : SYNC_EXEC;
#else
      session->parser->exec_mode = SYNC_EXEC;
#endif

      retval =
	db_execute_statement_local (session, statement_id, &db_q_result);

      if (retval >= 0)
	{
	  pbrs->rs_info[i].metainfo.affected_row = retval;
	  pbrs->rs_info[i].err_code = NO_ERROR;
	}
      else
	{
	  pbrs->rs_info[i].err_code = retval;
	  pbrs->rs_info[i].err_msg = strdup (api_get_errmsg ());
	}

      db_query_end (db_q_result);
      db_close_session_local (session);
      batch_data = batch_data->next;
    }

  /* release batch data */
  batch_data = pstmt->batch_data;
  for (i = 0; i < pstmt->batch_count && batch_data; i++)
    {
      if (batch_data)
	free (batch_data->data.query_string);

      tmp = batch_data;
      batch_data = tmp->next;
      free (tmp);
    }
  pstmt->batch_count = 0;
  pstmt->batch_data = NULL;

  return NO_ERROR;
}

/*
 * conn_restart_client -
 *    return:
 *    pconn():
 *    program():
 *    print_version():
 *    volume():
 *    port():
 */
static int
conn_restart_client (CI_CONN_STRUCTURE * pconn,
		     const char *program, int print_version,
		     const char *volume, short port)
{
  int error = NO_ERROR;
  char port_string[8];

  strncpy (db_Program_name, program, PATH_MAX);
  db_Database_name[0] = '\0';
  db_Connect_status = 1;

  if (port > 0)
    {
      sprintf (port_string, "%d", port);
      sysprm_set_force ("cubrid_port_id", port_string);
    }
  error = boot_restart_client (program, (bool) print_version, volume);

  if (error != NO_ERROR)
    {
      db_Connect_status = 0;
    }
  else
    {
      db_Connect_status = 1;
      strcpy (db_Database_name, volume);
      au_link_static_methods ();
      esm_load_esm_classes ();

#if !defined(WINDOWS)
#if defined(SA_MODE) && defined(LINUX)
      if (!jsp_jvm_is_loaded ())
	{
	  prev_sigfpe_handler =
	    os_set_signal_handler (SIGFPE, sigfpe_handler);
	}
#else /* SA_MODE && (LINUX||X86_SOLARIS) */
      prev_sigfpe_handler = os_set_signal_handler (SIGFPE, sigfpe_handler);
#endif /* SA_MODE && (LINUX||X86_SOLARIS) */
#endif /* !WINDOWS */
    }

  return (error);
}

/*
 * conn_end_tran -
 *    return:
 *    bh_interface():
 *    pconn():
 *    commit():
 */
static int
conn_end_tran (BH_INTERFACE * bh_interface,
	       CI_CONN_STRUCTURE * pconn, bool commit)
{
  int retval, i;
  CI_STMT_STRUCTURE *pstmt, *pnextstmt;
  bool retain_lock = false;
  int query_count = 0;

  assert (pconn);

  if (commit == true)
    retval = tran_commit (retain_lock);
  else
    retval = tran_abort ();

  if (retval == NO_ERROR)
    {
      retval =
	bh_interface->bind_get_first_child (bh_interface,
					    (BH_BIND *) pconn,
					    (BH_BIND **) & pstmt);

      while (pstmt != NULL)
	{
	  retval = stmt_release_children (bh_interface, pstmt, true);
	  if (pstmt->rs_info)
	    {
	      query_count = pstmt->session->dimension;
	      for (i = 0; i < query_count; i++)
		{
		  pstmt->rs_info[i].rs = NULL;
		  pstmt->rs_info[i].rsmeta = NULL;
		}
	    }

	  retval =
	    bh_interface->bind_get_next_sibling (bh_interface,
						 (BH_BIND *) pstmt,
						 (BH_BIND **) & pnextstmt);
	  pstmt = pnextstmt;
	}

      if (retval != NO_ERROR)
	{
	  ER_SET_AND_RETURN (retval);
	}
    }

  return retval;
}


/*
 * stmt_release_children -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    result_set_only():
 */
static int
stmt_release_children (BH_INTERFACE * bh_interface,
		       CI_STMT_STRUCTURE * pstmt, bool result_set_only)
{
  CI_RESULTSET_STRUCTURE *resptr;
  BIND_HANDLE rs_handle;
  int retval;

  assert (bh_interface && pstmt);

  for (;;)
    {
      bool continue_work = false;
      CI_RESULTSET_STRUCTURE *nextresptr;

      bh_interface->bind_get_first_child (bh_interface,
					  (BH_BIND *) pstmt,
					  (BH_BIND **) & resptr);
      if (resptr == NULL)
	break;

      if (result_set_only == false)
	{
	  retval =
	    bh_interface->bind_to_handle (bh_interface,
					  (BH_BIND *) resptr, &rs_handle);
	  if (retval != NO_ERROR)
	    {
	      assert (1);
	      ER_SET_AND_RETURN (retval);
	    }

	  retval = bh_interface->destroy_handle (bh_interface, rs_handle);
	  if (retval != NO_ERROR)
	    {
	      assert (1);
	      ER_SET_AND_RETURN (retval);
	    }

	  continue;
	}
      else
	{
	  do
	    {
	      if (resptr->handle_type == HANDLE_TYPE_RESULTSET
		  || resptr->handle_type == HANDLE_TYPE_RMETA)
		{
		  retval = bh_interface->bind_to_handle (bh_interface,
							 (BH_BIND *) resptr,
							 &rs_handle);
		  if (retval != NO_ERROR)
		    {
		      ER_SET_AND_RETURN (retval);
		    }
		  retval =
		    bh_interface->destroy_handle (bh_interface, rs_handle);
		  if (retval != NO_ERROR)
		    {
		      ER_SET_AND_RETURN (retval);
		    }
		  continue_work = true;
		  break;
		}
	      else
		{
		  retval = bh_interface->bind_get_next_sibling (bh_interface,
								(BH_BIND *)
								resptr,
								(BH_BIND
								 **)
								(&nextresptr));
		  resptr = nextresptr;
		}
	    }
	  while (resptr != NULL);

	  if (continue_work == false)
	    break;
	}
    }

  return NO_ERROR;
}


/*
 * stmt_bind_pmeta_handle -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    outmeta():
 */
static int
stmt_bind_pmeta_handle (BH_INTERFACE * bh_interface,
			CI_STMT_STRUCTURE * pstmt,
			CI_PARAM_META_STRUCTURE ** outmeta)
{
  int retval, param_count;
  CI_PARAM_META_STRUCTURE *ppmeta;
  CI_PARAMETER_METADATA hpmeta;

  assert (pstmt && outmeta);

  ppmeta =
    (CI_PARAM_META_STRUCTURE *) malloc (sizeof (CI_PARAM_META_STRUCTURE));

  if (ppmeta == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  init_pmeta_structure (ppmeta);

  ppmeta->marker = db_get_input_markers (pstmt->session, 1);
  if (ppmeta->marker == NULL)
    {
      free (ppmeta);
      return er_errid ();
    }

  param_count = pstmt->session->parser->host_var_count;

  if (param_count > 0)
    {
      ppmeta->is_out_param = (bool *) malloc (sizeof (bool) * param_count);
      if (ppmeta->is_out_param == NULL)
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
	}
      memset (ppmeta->is_out_param, '\0', sizeof (ppmeta->is_out_param));
    }

  retval =
    bh_interface->alloc_handle (bh_interface, (BH_BIND *) ppmeta,
				(BIND_HANDLE *) & hpmeta);

  if (retval != NO_ERROR)
    {
      free (ppmeta);
      ER_SET_AND_RETURN (retval);
    }

  retval =
    bh_interface->bind_graft (bh_interface, (BH_BIND *) ppmeta,
			      (BH_BIND *) pstmt);

  ppmeta->param_count = param_count;
  ppmeta->bh_interface = bh_interface;

  if (retval != NO_ERROR)
    {
      free (ppmeta);
      ER_SET_AND_RETURN (retval);
    }

  *outmeta = ppmeta;

  return NO_ERROR;
}

/*
 * stmt_batch_result_free -
 *    return:
 *    bh_interface():
 *    pstmt():
 */
static int
stmt_batch_result_free (BH_INTERFACE * bh_interface,
			CI_STMT_STRUCTURE * pstmt)
{
  int retval;
  CI_BATCH_RESULT_STRUCTURE *pbrs, *tmp;
  CI_BATCH_RESULT hbrs;

  retval =
    bh_interface->bind_get_first_child (bh_interface, (BH_BIND *) pstmt,
					(BH_BIND **) & pbrs);

  while ((pbrs != NULL) && (retval == NO_ERROR))
    {
      if (pbrs->handle_type == HANDLE_TYPE_BATCH_RESULT)
	{
	  break;
	}

      tmp = pbrs;
      pbrs = NULL;
      retval =
	bh_interface->bind_get_next_sibling (bh_interface,
					     (BH_BIND *) tmp,
					     (BH_BIND **) & pbrs);
    }

  if (pbrs != NULL)
    {
      retval =
	bh_interface->bind_to_handle (bh_interface, (BH_BIND *) pbrs,
				      (BIND_HANDLE *) & hbrs);
      if (retval != NO_ERROR)
	{
	  ER_SET_AND_RETURN (retval);
	}

      retval = bh_interface->destroy_handle (bh_interface, hbrs);
      if (retval != NO_ERROR)
	{
	  ER_SET_AND_RETURN (retval);
	}
    }

  return NO_ERROR;
}

/*
 * stmt_batch_alloc_and_bind_new_result -
 *    return:
 *    bh_interface():
 *    pstmt():
 *    out_pbrs():
 *    outhbr():
 */
static int
stmt_batch_alloc_and_bind_new_result (BH_INTERFACE * bh_interface,
				      CI_STMT_STRUCTURE * pstmt,
				      CI_BATCH_RESULT_STRUCTURE **
				      out_pbrs, CI_BATCH_RESULT * outhbr)
{
  int retval;
  CI_BATCH_RESULT_STRUCTURE *pbrs;
  CI_BATCH_RESULT hbr;

  pbrs =
    (CI_BATCH_RESULT_STRUCTURE *) malloc (sizeof (CI_BATCH_RESULT_STRUCTURE));

  if (pbrs == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  init_batch_rs_structure (pbrs);

  pbrs->rs_count = pstmt->batch_count;

  pbrs->rs_info =
    (CI_BATCH_RESULT_INFO *) malloc (sizeof (CI_BATCH_RESULT_INFO)
				     * pstmt->batch_count);

  if (pbrs->rs_info == NULL)
    {
      free (pbrs);
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  retval = bh_interface->alloc_handle (bh_interface, (BH_BIND *) pbrs, &hbr);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  retval =
    bh_interface->bind_graft (bh_interface, (BH_BIND *) pbrs,
			      (BH_BIND *) pstmt);

  if (retval != NO_ERROR)
    {
      free (pbrs->rs_info);
      free (pbrs);
      ER_SET_AND_RETURN (retval);
    }

  memset (pbrs->rs_info, '\0',
	  sizeof (CI_BATCH_RESULT_INFO) * pstmt->batch_count);

  if (out_pbrs)
    *out_pbrs = pbrs;

  if (outhbr)
    *outhbr = hbr;

  return NO_ERROR;
}

/*
 * get_connection_opool -
 *    return:
 *    pst():
 *    opool():
 */
static int
get_connection_opool (COMMON_API_STRUCTURE * pst,
		      API_OBJECT_RESULTSET_POOL ** opool)
{
  CI_CONN_STRUCTURE *pconn;
  pconn = (CI_CONN_STRUCTURE *) pst;

  if (opool)
    {
      *opool = pconn->opool;
    }

  return NO_ERROR;
}

/* ------------------------------------------------------------------------- */
/* ci_api implementation function */
/* ------------------------------------------------------------------------- */

/*
 * ci_err_set -
 *    return:
 *    error_code():
 */
int
ci_err_set (int error_code)
{
  api_er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
  return NO_ERROR;
}

/*
 * ci_create_connection_impl -
 *    return:
 *    conn():
 */
static int
ci_create_connection_impl (CI_CONNECTION * conn)
{
  int rid, retval;
  CI_CONN_STRUCTURE *ptr;
  BH_INTERFACE *hd_ctx;

  ptr = (CI_CONN_STRUCTURE *) malloc (sizeof (CI_CONN_STRUCTURE));
  if (ptr == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  init_conn_structure (ptr);

  retval = bh_root_acquire (&rid, BH_ROOT_TYPE_STATIC_HASH_64);
  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  retval = bh_root_lock (rid, &hd_ctx);
  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  retval = hd_ctx->alloc_handle (hd_ctx, (BH_BIND *) ptr, conn);
  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  /* make object pool */
  retval =
    api_object_resultset_pool_create (hd_ctx, (BIND_HANDLE) * conn,
				      &(ptr->opool));
  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  ptr->bh_interface = hd_ctx;

  bh_root_unlock (rid);

  return NO_ERROR;
}

/*
 * ci_conn_connect_impl -
 *    return:
 *    pst():
 *    host():
 *    port():
 *    databasename():
 *    user_name():
 *    password():
 */
static int
ci_conn_connect_impl (COMMON_API_STRUCTURE * pst, const char *host,
		      unsigned short port,
		      const char *databasename,
		      const char *user_name, const char *password)
{
  int retval;
  CI_CONN_STRUCTURE *pconn;
  char dbname_host[1024];

  pconn = (CI_CONN_STRUCTURE *) pst;

  retval = au_login (user_name, password);

  if (retval != NO_ERROR)
    {
      return retval;
    }

  if (host != NULL)
    {
      memset (dbname_host, '\0', 1024);
      sprintf (dbname_host, "%s@%s", databasename, host);
      retval =
	conn_restart_client (pconn, API_PROGRAM_NAME, false,
			     dbname_host, port);
    }
  else
    {
      retval =
	conn_restart_client (pconn, API_PROGRAM_NAME, false,
			     databasename, port);
    }

  if (retval != NO_ERROR)
    {
      return retval;
    }

  pconn->conn_status = CI_CONN_STATUS_CONNECTED;

  return NO_ERROR;
}

/*
 * ci_conn_close_impl -
 *    return:
 *    pst():
 */
static int
ci_conn_close_impl (COMMON_API_STRUCTURE * pst)
{
  CI_CONN_STRUCTURE *pconn;

  pconn = (CI_CONN_STRUCTURE *) pst;

  boot_shutdown_client (true);
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGFPE, prev_sigfpe_handler);
#endif

  return NO_ERROR;
}

/*
 * ci_conn_create_statement_impl -
 *    return:
 *    pst():
 *    stmt():
 */
static int
ci_conn_create_statement_impl (COMMON_API_STRUCTURE * pst,
			       CI_STATEMENT * stmt)
{
  CI_CONN_STRUCTURE *pconn;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;
  int retval;

  pconn = (CI_CONN_STRUCTURE *) pst;
  bh_interface = pconn->bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) malloc (sizeof (CI_STMT_STRUCTURE));

  if (pstmt == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }

  init_stmt_structure (pstmt);

  retval = bh_interface->alloc_handle (bh_interface, (BH_BIND *) pstmt, stmt);
  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  retval =
    bh_interface->bind_graft (bh_interface, (BH_BIND *) pstmt,
			      (BH_BIND *) pconn);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  pstmt->pconn = pconn;

  return NO_ERROR;
}

/*
 * ci_conn_set_option_impl -
 *    return:
 *    pst():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_conn_set_option_impl (COMMON_API_STRUCTURE * pst,
			 CI_CONNECTION_OPTION option, void *arg, size_t size)
{
  int retval;
  int waitsecs, autocommit, isol_lv;
  CI_CONN_STRUCTURE *pconn;

  pconn = (CI_CONN_STRUCTURE *) pst;

  retval = NO_ERROR;

  switch (option)
    {
    case CI_CONNECTION_OPTION_LOCK_TIMEOUT:
      if (size != sizeof (int))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
	}
      memcpy (&waitsecs, arg, sizeof (int));
      tran_reset_wait_times ((float) (waitsecs / 1000));
      break;
    case CI_CONNECTION_OPTION_TRAN_ISOLATION_LV:
      if (size != sizeof (int))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
	}
      memcpy (&isol_lv, arg, sizeof (int));
      retval =
	tran_reset_isolation ((DB_TRAN_ISOLATION) isol_lv,
			      TM_TRAN_ASYNC_WS ());
      if (retval == NO_ERROR)
	{
	  pconn->opt.isolation = isol_lv;
	}
      break;
    case CI_CONNECTION_OPTION_AUTOCOMMIT:
      if (size != sizeof (int))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
	}
      memcpy (&autocommit, arg, sizeof (int));
      pconn->opt.autocommit = (autocommit) ? true : false;
      break;
    case CI_CONNECTION_OPTION_CLIENT_VERSION:
    case CI_CONNECTION_OPTION_SERVER_VERSION:
    default:
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_SUPPORTED_OPERATION);
    }

  return retval;
}

/*
 * ci_conn_get_option_impl -
 *    return:
 *    pst():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_conn_get_option_impl (COMMON_API_STRUCTURE * pst,
			 CI_CONNECTION_OPTION option, void *arg, size_t size)
{
  float waitsec;
  int intval;
  CI_CONN_STRUCTURE *pconn;
  DB_TRAN_ISOLATION isol_lv;
  bool dummy;

  pconn = (CI_CONN_STRUCTURE *) pst;

  switch (option)
    {
    case CI_CONNECTION_OPTION_LOCK_TIMEOUT:
      if (size != sizeof (int))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
	}
      tran_get_tran_settings (&waitsec, &isol_lv, &dummy);
      intval = (int) waitsec;
      if (intval > 0)
	{
	  intval *= 1000;
	}
      memcpy (arg, &intval, sizeof (int));
      break;
    case CI_CONNECTION_OPTION_TRAN_ISOLATION_LV:
      if (size != sizeof (int))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
	}
      tran_get_tran_settings (&waitsec, &isol_lv, &dummy);
      intval = (int) isol_lv;
      memcpy (arg, &intval, sizeof (int));
      pconn->opt.isolation = isol_lv;
      break;
    case CI_CONNECTION_OPTION_AUTOCOMMIT:
      if (size != sizeof (int))
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
	}
      intval = (pconn->opt.autocommit) ? (int) 1 : (int) 0;
      memcpy (arg, &intval, sizeof (int));
      break;
    case CI_CONNECTION_OPTION_CLIENT_VERSION:
      if (pconn->opt.cli_version[0] == '\0')
	{
	  strncpy (pconn->opt.cli_version, PACKAGE_STRING, VERSION_LENGTH);
	}
      strncpy ((char *) arg, pconn->opt.cli_version, size - 1);
      memset (arg + size - 1, '\0', 1);
      break;
    case CI_CONNECTION_OPTION_SERVER_VERSION:
      if (pconn->opt.srv_version[0] == '\0')
	{
	  strncpy (pconn->opt.srv_version, PACKAGE_STRING, VERSION_LENGTH);
	}
      strncpy ((char *) arg, pconn->opt.srv_version, size - 1);
      memset (arg + size - 1, '\0', 1);
      break;
    default:
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }
  return NO_ERROR;
}

/*
 * ci_conn_commit_impl -
 *    return:
 *    pst():
 */
static int
ci_conn_commit_impl (COMMON_API_STRUCTURE * pst)
{
  int retval;
  CI_CONN_STRUCTURE *pconn;

  pconn = (CI_CONN_STRUCTURE *) pst;

  retval = conn_end_tran (pconn->bh_interface, pconn, true);

  return retval;
}

/*
 * ci_conn_rollback_impl -
 *    return:
 *    pst():
 */
static int
ci_conn_rollback_impl (COMMON_API_STRUCTURE * pst)
{
  int retval;
  CI_CONN_STRUCTURE *pconn;

  pconn = (CI_CONN_STRUCTURE *) pst;

  retval = conn_end_tran (pconn->bh_interface, pconn, false);

  return retval;
}

/*
 * ci_conn_get_error_impl -
 *    return:
 *    pst():
 *    err():
 *    msg():
 *    size():
 */
static int
ci_conn_get_error_impl (COMMON_API_STRUCTURE * pst, int *err,
			char *msg, size_t size)
{
  assert (msg && err);
  *err = api_get_errid ();
  strncpy (msg, api_get_errmsg (), size);

  return NO_ERROR;
}

/*
 * ci_stmt_add_batch_query_impl -
 *    return:
 *    pst():
 *    sql():
 *    len():
 */
static int
ci_stmt_add_batch_query_impl (COMMON_API_STRUCTURE * pst,
			      const char *sql, size_t len)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_PREPARED_STATEMENT);
    }

  if (!(pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
      && (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_NOT_BATCH_STATEMENT);
    }

  retval = stmt_add_batch_string (pstmt, sql, len);

  if (retval == NO_ERROR)
    pstmt->batch_count++;

  pstmt->stmt_status |= CI_STMT_STATUS_BATCH_ADDED;

  return retval;
}

/*
 * stmt_add_batch_string -
 *    return:
 *    pstmt():
 *    sql():
 *    len():
 */
static int
stmt_add_batch_string (CI_STMT_STRUCTURE * pstmt, const char *sql, size_t len)
{
  CI_BATCH_DATA *batch_data, *tmp;

  assert (sql);

  batch_data = (CI_BATCH_DATA *) malloc (sizeof (CI_BATCH_DATA));
  if (!batch_data)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }
  memset (batch_data, '\0', sizeof (CI_BATCH_DATA));

  batch_data->data.query_string = (char *) malloc (len);
  if (batch_data->data.query_string == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_MEMORY);
    }
  memset (batch_data->data.query_string, '\0', len);

  strncpy (batch_data->data.query_string, sql, len);
  batch_data->query_length = len;

  tmp = pstmt->batch_data;
  if (pstmt->batch_data == NULL)
    {
      pstmt->batch_data = batch_data;
    }
  else
    {
      tmp = pstmt->batch_data;
      while (tmp->next != NULL)
	{
	  tmp = tmp->next;
	}

      tmp->next = batch_data;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_add_batch_impl -
 *    return:
 *    pst():
 */
static int
ci_stmt_add_batch_impl (COMMON_API_STRUCTURE * pst)
{
  int retval, i, param_count;
  CI_STMT_STRUCTURE *pstmt;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_NOT_PREPARED_STATEMENT);
    }

  if (!(pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
      && (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_NOT_BATCH_STATEMENT);
    }

  param_count = pstmt->session->parser->host_var_count;

  for (i = 0; i < param_count; i++)
    {
      if (pstmt->param_value_is_set[i] == false)
	{
	  ER_SET_AND_RETURN (ER_INTERFACE_PARAM_IS_NOT_SET);
	}
    }

  retval = stmt_add_batch_param (pstmt, pstmt->param_val, param_count);

  if (retval == NO_ERROR)
    pstmt->batch_count++;

  for (i = 0; i < param_count; i++)
    {
      pstmt->param_value_is_set[i] = false;
    }

  pstmt->stmt_status |= CI_STMT_STATUS_BATCH_ADDED;

  return retval;
}

/*
 * ci_stmt_clear_batch_impl -
 *    return:
 *    pst():
 */
static int
ci_stmt_clear_batch_impl (COMMON_API_STRUCTURE * pst)
{
  int param_count;
  CI_STMT_STRUCTURE *pstmt;
  CI_BATCH_DATA *batch_data, *tmp;
  bool is_prepared_batch;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if ((!(pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED) &&
       (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED)))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_CANNOT_CLEAR_BATCH);
    }

  param_count = pstmt->session->parser->host_var_count;

  is_prepared_batch =
    (pstmt->stmt_status & CI_STMT_STATUS_PREPARED) ? true : false;

  batch_data = pstmt->batch_data;

  while (batch_data)
    {
      if (is_prepared_batch)
	free (batch_data->data.query_string);
      else
	free (batch_data->data.val);

      tmp = batch_data;
      batch_data = tmp->next;
      free (tmp);
    }

  pstmt->batch_count = 0;
  pstmt->batch_data = NULL;

  return NO_ERROR;
}

/*
 * ci_stmt_execute_immediate_impl -
 *    return:
 *    pst():
 *    sql():
 *    len():
 *    rs():
 *    r():
 */
static int
ci_stmt_execute_immediate_impl (COMMON_API_STRUCTURE * pst,
				char *sql, size_t len,
				CI_RESULTSET * rs, int *r)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  bh_interface = pstmt->pconn->bh_interface;

  if (pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_PREPARED_STATEMENT);
    }

  if (pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_BATCH_STATEMENT);
    }

  if (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED)
    {
      retval = stmt_release_children (bh_interface, pstmt, false);

      if (retval != NO_ERROR)
	{
	  ER_SET_AND_RETURN (retval);
	}
    }

  if (pstmt->pconn->opt.autocommit && pstmt->pconn->need_defered_commit)
    {
      retval = conn_end_tran (bh_interface, pstmt->pconn, true);

      if (retval != NO_ERROR)
	{
	  return retval;
	}
      pstmt->pconn->need_defered_commit = false;
    }

  retval = stmt_exec_immediate_internal (bh_interface, pstmt, sql, len,
					 rs, r);

  pstmt->stmt_status |= CI_STMT_STATUS_EXECUTED;

  if (retval != NO_ERROR)
    {
      return retval;
    }

  if (pstmt->pconn->opt.autocommit && pstmt->pconn->need_immediate_commit)
    {
      retval = conn_end_tran (bh_interface, pstmt->pconn, true);

      if (retval != NO_ERROR)
	{
	  return retval;
	}
      pstmt->pconn->need_immediate_commit = false;
    }

  return retval;
}

/*
 * ci_stmt_execute_impl -
 *    return:
 *    pst():
 *    rs():
 *    r():
 */
static int
ci_stmt_execute_impl (COMMON_API_STRUCTURE * pst, CI_RESULTSET * rs, int *r)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  if (pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_BATCH_STATEMENT);
    }

  if (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED)
    {
      retval = stmt_release_children (bh_interface, pstmt, false);

      if (retval != NO_ERROR)
	{
	  return retval;
	}
    }

  if (pstmt->pconn->opt.autocommit && pstmt->pconn->need_defered_commit)
    {
      retval = conn_end_tran (bh_interface, pstmt->pconn, true);

      if (retval != NO_ERROR)
	{
	  return retval;
	}
      pstmt->pconn->need_defered_commit = false;
    }

  retval = stmt_exec_internal (bh_interface, pstmt, rs, r);

  if (retval != NO_ERROR)
    {
      return retval;
    }

  if (pstmt->pconn->opt.autocommit && pstmt->pconn->need_immediate_commit)
    {
      retval = conn_end_tran (bh_interface, pstmt->pconn, true);

      if (retval != NO_ERROR)
	{
	  return retval;
	}
      pstmt->pconn->need_immediate_commit = false;
    }

  pstmt->stmt_status |= CI_STMT_STATUS_EXECUTED;

  return retval;
}

/*
 * ci_stmt_execute_batch_impl -
 *    return:
 *    pst():
 *    br():
 */
static int
ci_stmt_execute_batch_impl (COMMON_API_STRUCTURE * pst, CI_BATCH_RESULT * br)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_CANNOT_BATCH_EXECUTE);
    }

  if (pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
    {
      retval = stmt_exec_prepared_batch_internal (bh_interface, pstmt, br);
    }
  else
    {
      retval = stmt_exec_batch_query_internal (bh_interface, pstmt, br);
    }

  if (retval != NO_ERROR)
    {
      return retval;
    }

  if (pstmt->pconn->opt.autocommit)
    {
      retval = conn_end_tran (bh_interface, pstmt->pconn, true);
      if (retval != NO_ERROR)
	{
	  return retval;
	}
    }

  return NO_ERROR;
}

/*
 * ci_stmt_get_option_impl -
 *    return:
 *    pst():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_stmt_get_option_impl (COMMON_API_STRUCTURE * pst,
			 CI_STATEMENT_OPTION option, void *arg, size_t size)
{
  CI_STMT_STRUCTURE *pstmt;
  int *data;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  switch (option)
    {
    case CI_STATEMENT_OPTION_HOLD_CURSORS_OVER_COMMIT:
      data = (int *) arg;
      *data = 0;		/* not implemeted */
      break;
    case CI_STATEMENT_OPTION_UPDATABLE_RESULT:
      data = (int *) arg;
      *data = (pstmt->opt.updatable_result) ? 1 : 0;
      break;
    case CI_STATEMENT_OPTION_GET_GENERATED_KEYS:
      data = (int *) arg;
      *data = 0;		/* not implemeted */
      break;
    case CI_STATEMENT_OPTION_EXEC_CONTINUE_ON_ERROR:
      data = (int *) arg;
      *data = (pstmt->opt.exec_continue_on_error) ? 1 : 0;
      break;
    case CI_STATEMENT_OPTION_ASYNC_QUERY:
      data = (int *) arg;
      *data = (pstmt->opt.async_query) ? 1 : 0;
      break;
    case CI_STATEMENT_OPTION_LAZY_EXEC:
      data = (int *) arg;
      *data = (pstmt->opt.lazy_exec) ? 1 : 0;
      break;
    default:
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
      break;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_set_option_impl -
 *    return:
 *    pst():
 *    option():
 *    arg():
 *    size():
 */
static int
ci_stmt_set_option_impl (COMMON_API_STRUCTURE * pst,
			 CI_STATEMENT_OPTION option, void *arg, size_t size)
{
  CI_STMT_STRUCTURE *pstmt;
  int data;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  switch (option)
    {
    case CI_STATEMENT_OPTION_HOLD_CURSORS_OVER_COMMIT:
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_SUPPORTED_OPERATION);
      break;

    case CI_STATEMENT_OPTION_UPDATABLE_RESULT:
      memcpy (&data, arg, sizeof (int));
      pstmt->opt.updatable_result = (data) ? true : false;
      break;

    case CI_STATEMENT_OPTION_GET_GENERATED_KEYS:
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_SUPPORTED_OPERATION);
      break;

    case CI_STATEMENT_OPTION_ASYNC_QUERY:
      memcpy (&data, arg, sizeof (int));
      pstmt->opt.async_query = (data) ? true : false;
      break;
    case CI_STATEMENT_OPTION_EXEC_CONTINUE_ON_ERROR:
      memcpy (&data, arg, sizeof (int));
      pstmt->opt.exec_continue_on_error = (data) ? true : false;
      break;
    case CI_STATEMENT_OPTION_LAZY_EXEC:
      memcpy (&data, arg, sizeof (int));
      pstmt->opt.lazy_exec = (data) ? true : false;
      break;
    default:
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
      break;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_prepare_impl -
 *    return:
 *    pst():
 *    sql():
 *    len():
 */
static int
ci_stmt_prepare_impl (COMMON_API_STRUCTURE * pst, const char *sql, size_t len)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
      && (pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      /* executed with exec_immediate before */
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_SUPPORTED_OPERATION);
    }

  if (pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
    {
      retval = stmt_release_children (bh_interface, pstmt, false);

      if (retval != NO_ERROR)
	{
	  return retval;
	}
    }

  retval = stmt_prepare_internal (bh_interface, pstmt, sql, len);

  if (retval != NO_ERROR)
    {
      return retval;
    }

  pstmt->stmt_status |= CI_STMT_STATUS_PREPARED;
  pstmt->stmt_status &= (~CI_STMT_STATUS_EXECUTED);

  return retval;
}

/*
 * ci_stmt_register_out_parameter_impl -
 *    return:
 *    pst():
 *    index():
 */
static int
ci_stmt_register_out_parameter_impl (COMMON_API_STRUCTURE * pst, int index)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  CI_PARAM_META_STRUCTURE *ppmeta;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  ppmeta = pstmt->ppmeta;

  if (ppmeta == NULL)
    {
      retval = stmt_bind_pmeta_handle (bh_interface, pstmt, &ppmeta);
      if (retval != NO_ERROR)
	{
	  return retval;
	}
    }

  if (ppmeta->param_count < index)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  if (pstmt->param_value_is_set[index - 1] == false)
    {
      retval =
	stmt_set_parameter_internal (bh_interface, pstmt, index,
				     CI_TYPE_NULL, NULL, 0);
      if (retval != NO_ERROR)
	{
	  return retval;
	}
    }

  ppmeta->is_out_param[index] = CI_PARAM_MODE_OUT;

  return NO_ERROR;
}

/*
 * ci_stmt_get_resultset_metadata_impl -
 *    return:
 *    pst():
 *    r():
 */
static int
ci_stmt_get_resultset_metadata_impl (COMMON_API_STRUCTURE * pst,
				     CI_RESULTSET_METADATA * r)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED)
      && !(pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  retval = stmt_get_resultset_metadata_internal (bh_interface, pstmt, r);

  if (retval != NO_ERROR)
    {
      return retval;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_get_parameter_metadata_impl -
 *    return:
 *    pst():
 *    r():
 */
static int
ci_stmt_get_parameter_metadata_impl (COMMON_API_STRUCTURE * pst,
				     CI_PARAMETER_METADATA * r)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  if (pstmt->ppmeta == NULL)
    {
      retval = stmt_bind_pmeta_handle (bh_interface, pstmt, &(pstmt->ppmeta));
      if (retval != NO_ERROR)
	{
	  return retval;
	}
    }

  retval = bh_interface->bind_to_handle (bh_interface,
					 (BH_BIND *) pstmt->ppmeta,
					 (BIND_HANDLE *) r);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  return NO_ERROR;
}

/*
 * ci_stmt_get_parameter_impl -
 *    return:
 *    pst():
 *    index():
 *    type():
 *    addr():
 *    len():
 *    outlen():
 *    isnull():
 */
static int
ci_stmt_get_parameter_impl (COMMON_API_STRUCTURE * pst, int index,
			    CI_TYPE type, void *addr,
			    size_t len, size_t * outlen, bool * isnull)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  retval =
    stmt_get_parameter_internal (bh_interface, pstmt, index, type, addr,
				 len, outlen, isnull);

  return retval;
}

/*
 * ci_stmt_set_parameter_impl -
 *    return:
 *    pst():
 *    index():
 *    type():
 *    val():
 *    size():
 */
static int
ci_stmt_set_parameter_impl (COMMON_API_STRUCTURE * pst,
			    int index, CI_TYPE type, void *val, size_t size)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_PREPARED);
    }

  retval =
    stmt_set_parameter_internal (bh_interface, pstmt, index, type, val, size);

  return retval;
}

/*
 * ci_stmt_get_resultset_impl -
 *    return:
 *    pst():
 *    res():
 */
static int
ci_stmt_get_resultset_impl (COMMON_API_STRUCTURE * pst, CI_RESULTSET * res)
{
  int retval;
  CI_STMT_STRUCTURE *pstmt;
  CI_RESULTSET_STRUCTURE *prs;
  BH_INTERFACE *bh_interface;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_EXECUTED);
    }

  if (pstmt->rs_info[pstmt->current_rs_idx].metainfo.has_result == false)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_HAS_NO_RESULT_SET);
    }

  prs = pstmt->rs_info[pstmt->current_rs_idx].rs;
  if (prs == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_RESULTSET_CLOSED);
    }

  retval =
    bh_interface->bind_to_handle (bh_interface, (BH_BIND *) prs,
				  (BIND_HANDLE *) res);

  if (retval != NO_ERROR)
    {
      ER_SET_AND_RETURN (retval);
    }

  return NO_ERROR;
}

/*
 * ci_stmt_affected_rows_impl -
 *    return:
 *    pst():
 *    out():
 */
static int
ci_stmt_affected_rows_impl (COMMON_API_STRUCTURE * pst, int *out)
{
  CI_STMT_STRUCTURE *pstmt;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_EXECUTED);
    }

  if (out)
    {
      *out = pstmt->rs_info[pstmt->current_rs_idx].metainfo.affected_row;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_next_result_impl -
 *    return:
 *    pst():
 *    exist_result():
 */
static int
ci_stmt_next_result_impl (COMMON_API_STRUCTURE * pst, bool * exist_result)
{
  CI_STMT_STRUCTURE *pstmt;
  BH_INTERFACE *bh_interface;
  CI_RESULTSET rs;
  int affected_row, retval;

  pstmt = (CI_STMT_STRUCTURE *) pst;
  bh_interface = pstmt->pconn->bh_interface;

  if (!(pstmt->stmt_status & CI_STMT_STATUS_EXECUTED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_EXECUTED);
    }

  if (pstmt->session->dimension <= pstmt->current_rs_idx + 1)
    {
      if (exist_result)
	*exist_result = false;

      return NO_ERROR;
    }

  pstmt->current_rs_idx += 1;

  if (exist_result)
    {
      *exist_result = true;
    }

  if (pstmt->opt.lazy_exec == true)
    {
      if (pstmt->pconn->opt.autocommit == true
	  && pstmt->pconn->need_defered_commit == true)
	{
	  retval = conn_end_tran (bh_interface, pstmt->pconn, true);

	  if (retval != NO_ERROR)
	    {
	      return retval;
	    }
	  pstmt->pconn->need_defered_commit = false;
	}

      retval =
	stmt_exec_one_statement (bh_interface, pstmt->current_rs_idx,
				 pstmt, &rs, &affected_row);
      if (retval != NO_ERROR)
	{
	  return retval;
	}

      if (pstmt->pconn->opt.autocommit == true
	  && pstmt->pconn->need_immediate_commit == true)
	{
	  retval = conn_end_tran (bh_interface, pstmt->pconn, true);

	  if (retval != NO_ERROR)
	    {
	      return retval;
	    }
	  pstmt->pconn->need_immediate_commit = false;
	}
    }

  return NO_ERROR;
}

/*
 * ci_batch_res_query_count_impl -
 *    return:
 *    pst():
 *    count():
 */
static int
ci_batch_res_query_count_impl (COMMON_API_STRUCTURE * pst, int *count)
{
  CI_BATCH_RESULT_STRUCTURE *pbrs;

  pbrs = (CI_BATCH_RESULT_STRUCTURE *) pst;

  *count = pbrs->rs_count;

  return NO_ERROR;
}

/*
 * ci_batch_res_get_result_impl -
 *    return:
 *    pst():
 *    index():
 *    ret():
 *    nr():
 */
static int
ci_batch_res_get_result_impl (COMMON_API_STRUCTURE * pst,
			      int index, int *ret, int *nr)
{
  CI_BATCH_RESULT_STRUCTURE *pbrs;

  pbrs = (CI_BATCH_RESULT_STRUCTURE *) pst;

  if (index > pbrs->rs_count)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  /* convert to zero based index */
  *ret = pbrs->rs_info[index - 1].err_code;
  *nr = pbrs->rs_info[index - 1].metainfo.affected_row;

  return NO_ERROR;
}

/*
 * ci_batch_res_get_error_impl -
 *    return:
 *    pst():
 *    index():
 *    err_code():
 *    err_msg():
 *    buf_size():
 */
static int
ci_batch_res_get_error_impl (COMMON_API_STRUCTURE * pst, int index,
			     int *err_code, char *err_msg, size_t buf_size)
{
  CI_BATCH_RESULT_STRUCTURE *pbrs;

  pbrs = (CI_BATCH_RESULT_STRUCTURE *) pst;

  if (index > pbrs->rs_count)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_INVALID_ARGUMENT);
    }

  *err_code = pbrs->rs_info[index].err_code;

  if (pbrs->rs_info[index].err_msg != NULL)
    strncpy (err_msg, pbrs->rs_info[index].err_msg, buf_size);
  else
    *err_msg = '\0';

  return NO_ERROR;
}

/*
 * ci_pmeta_get_count_impl -
 *    return:
 *    pst():
 *    count():
 */
static int
ci_pmeta_get_count_impl (COMMON_API_STRUCTURE * pst, int *count)
{
  CI_PARAM_META_STRUCTURE *ppmeta;

  ppmeta = (CI_PARAM_META_STRUCTURE *) pst;

  *count = ppmeta->param_count;

  return NO_ERROR;
}

/*
 * ci_pmeta_get_info_impl -
 *    return:
 *    pst():
 *    index():
 *    type():
 *    arg():
 *    size():
 */
static int
ci_pmeta_get_info_impl (COMMON_API_STRUCTURE * pst, int index,
			CI_PMETA_INFO_TYPE type, void *arg, size_t size)
{
  int retval;
  CI_PARAM_META_STRUCTURE *ppmeta;
  ppmeta = (CI_PARAM_META_STRUCTURE *) pst;



  retval = pmeta_get_info_internal (ppmeta->bh_interface, ppmeta, index,
				    type, arg, size);

  return retval;
}

/*
 * ci_stmt_get_first_error_impl -
 *    return:
 *    pst():
 *    line():
 *    col():
 *    errcode():
 *    err_msg():
 *    size():
 */
static int
ci_stmt_get_first_error_impl (COMMON_API_STRUCTURE * pst,
			      int *line, int *col, int *errcode,
			      char *err_msg, size_t size)
{
  CI_STMT_STRUCTURE *pstmt;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_BATCH_STATEMENT);
    }

  if (pstmt->err_info == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_ERROR);
    }

  *errcode = pstmt->err_info->err_code;
  if (pstmt->err_info->line >= 0)
    {
      *line = pstmt->err_info->line;
    }

  if (pstmt->err_info->column >= 0)
    {
      *col = pstmt->err_info->column;
    }

  strncpy (err_msg, pstmt->err_info->err_msg, size);

  pstmt->current_err_idx = 1;

  return NO_ERROR;
}

/*
 * ci_stmt_get_next_error_impl -
 *    return:
 *    pst():
 *    line():
 *    col():
 *    errcode():
 *    err_msg():
 *    size():
 */
static int
ci_stmt_get_next_error_impl (COMMON_API_STRUCTURE * pst, int *line,
			     int *col, int *errcode, char *err_msg,
			     size_t size)
{
  int i;
  CI_STMT_STRUCTURE *pstmt;
  STMT_ERROR_INFO *err_info;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_BATCH_STATEMENT);
    }

  err_info = pstmt->err_info;

  for (i = 0; i < pstmt->current_err_idx && err_info; i++)
    {
      err_info = err_info->next;
    }

  if (err_info == NULL)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NO_MORE_ERROR);
    }

  *errcode = err_info->err_code;

  if (err_info->line >= 0)
    {
      *line = err_info->line;
    }

  if (err_info->column >= 0)
    {
      *col = err_info->column;
    }

  strncpy (err_msg, err_info->err_msg, size);

  pstmt->current_err_idx++;

  return NO_ERROR;
}

/*
 * ci_stmt_get_query_type_impl -
 *    return:
 *    pst():
 *    type(out):
 */
static int
ci_stmt_get_query_type_impl (COMMON_API_STRUCTURE * pst,
			     CUBRID_STMT_TYPE * type)
{
  CI_STMT_STRUCTURE *pstmt;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_BATCH_STATEMENT);
    }

  if (!(pstmt->stmt_status & CI_STMT_STATUS_EXECUTED)
      && !(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_EXECUTED);
    }

  if (type)
    {
      *type = pstmt->rs_info[pstmt->current_rs_idx].metainfo.sql_type;
    }

  return NO_ERROR;
}

/*
 * ci_stmt_get_start_line_impl -
 *    return:
 *    pst():
 *    line(out):
 */
static int
ci_stmt_get_start_line_impl (COMMON_API_STRUCTURE * pst, int *line)
{
  CI_STMT_STRUCTURE *pstmt;

  pstmt = (CI_STMT_STRUCTURE *) pst;

  if (pstmt->stmt_status & CI_STMT_STATUS_BATCH_ADDED)
    {
      ER_SET_AND_RETURN (ER_INTERFACE_IS_BATCH_STATEMENT);
    }

  if (!(pstmt->stmt_status & CI_STMT_STATUS_EXECUTED)
      && !(pstmt->stmt_status & CI_STMT_STATUS_PREPARED))
    {
      ER_SET_AND_RETURN (ER_INTERFACE_NOT_EXECUTED);
    }

  if (line)
    {
      *line = db_get_start_line (pstmt->session, pstmt->current_rs_idx + 1);
    }

  return NO_ERROR;
}

/*
 * ci_collection_new_impl -
 *    return:
 *    conn():
 *    coll():
 */
static int
ci_collection_new_impl (CI_CONNECTION conn, CI_COLLECTION * coll)
{
  int res;
  API_COLLECTION *co;

  res = api_collection_create ((BIND_HANDLE) conn, &co);
  if (res == NO_ERROR)
    {
      *coll = co;
    }

  return res;
}

CUBRID_API_FUNCTION_TABLE Cubrid_api_function_table = {
  ci_create_connection_impl,
  ci_err_set,
  ci_conn_connect_impl,
  ci_conn_close_impl,
  ci_conn_create_statement_impl,
  ci_conn_set_option_impl,
  ci_conn_get_option_impl,
  ci_conn_commit_impl,
  ci_conn_rollback_impl,
  ci_conn_get_error_impl,
  ci_stmt_add_batch_query_impl,
  ci_stmt_add_batch_impl,
  ci_stmt_clear_batch_impl,
  ci_stmt_execute_immediate_impl,
  ci_stmt_execute_impl,
  ci_stmt_execute_batch_impl,
  ci_stmt_get_option_impl,
  ci_stmt_set_option_impl,
  ci_stmt_prepare_impl,
  ci_stmt_register_out_parameter_impl,
  ci_stmt_get_resultset_metadata_impl,
  ci_stmt_get_parameter_metadata_impl,
  ci_stmt_get_parameter_impl,
  ci_stmt_set_parameter_impl,
  ci_stmt_get_resultset_impl,
  ci_stmt_affected_rows_impl,
  ci_stmt_get_query_type_impl,
  ci_stmt_get_start_line_impl,
  ci_stmt_next_result_impl,
  ci_stmt_get_first_error_impl,
  ci_stmt_get_next_error_impl,
  ci_batch_res_query_count_impl,
  ci_batch_res_get_result_impl,
  ci_batch_res_get_error_impl,
  ci_pmeta_get_count_impl,
  ci_pmeta_get_info_impl,
  get_connection_opool,
  ci_collection_new_impl
};
