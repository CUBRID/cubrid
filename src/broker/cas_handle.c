/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */


/*
 * cas_handle.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <unistd.h>
#endif /* WINDOWS */

#include "cas_log.h"
#include "dbtype.h"
#include "cas_common_execute.h"
#include "network_interface_cl.h"

#define SRV_HANDLE_ALLOC_SIZE		256

static void srv_handle_content_free (T_SRV_HANDLE * srv_handle);
static void srv_handle_release_internal_lob_uploads (int h_id, T_SRV_HANDLE * srv_handle);
static void col_update_info_free (T_QUERY_RESULT * q_result);
static void srv_handle_rm_tmp_file (int h_id, T_SRV_HANDLE * srv_handle);
extern bool tran_is_in_libcas (void);

static T_SRV_HANDLE **srv_handle_table = NULL;
static int max_srv_handle = 0;
static int max_handle_id = 0;
static int current_handle_count = 0;
static int current_handle_id = -1;	/* it is used for javasp */
static bool is_cgw_mode = false;
static cgw_free_stmt_func_t cgw_free_stmt_func = NULL;

int
hm_new_srv_handle (T_SRV_HANDLE ** new_handle, unsigned int seq_num)
{
  int i;
  int new_max_srv_handle;
  int new_handle_id = 0;
  T_SRV_HANDLE **new_srv_handle_table = NULL;
  T_SRV_HANDLE *srv_handle;

  if (cas_shard_flag == OFF && current_handle_count >= shm_appl->max_prepared_stmt_count)
    {
      return ERROR_INFO_SET (CAS_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED, CAS_ERROR_INDICATOR);
    }


  for (i = 0; i < max_srv_handle; i++)
    {
      if (srv_handle_table[i] == NULL)
	{
	  *new_handle = srv_handle_table[i];
	  new_handle_id = i + 1;
	  break;
	}
    }

  if (new_handle_id == 0)
    {
      new_max_srv_handle = max_srv_handle + SRV_HANDLE_ALLOC_SIZE;
      new_srv_handle_table = (T_SRV_HANDLE **) REALLOC (srv_handle_table, sizeof (T_SRV_HANDLE *) * new_max_srv_handle);
      if (new_srv_handle_table == NULL)
	{
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	}

      new_handle_id = max_srv_handle + 1;
      memset (new_srv_handle_table + max_srv_handle, 0, sizeof (T_SRV_HANDLE *) * SRV_HANDLE_ALLOC_SIZE);
      max_srv_handle = new_max_srv_handle;
      srv_handle_table = new_srv_handle_table;
    }

  srv_handle = (T_SRV_HANDLE *) MALLOC (sizeof (T_SRV_HANDLE));
  if (srv_handle == NULL)
    {
      return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }
  memset (srv_handle, 0, sizeof (T_SRV_HANDLE));
  srv_handle->id = new_handle_id;
  srv_handle->query_seq_num = seq_num;
  srv_handle->use_plan_cache = false;
  srv_handle->use_query_cache = false;
  srv_handle->is_holdable = false;
  srv_handle->is_from_current_transaction = true;
  srv_handle->is_pooled = as_info->cur_statement_pooling;
  srv_handle->replace_rule_idx = -1;
  srv_handle->num_orig_markers = 0;
  srv_handle->replace_fallback = 0;
  srv_handle->qr_demote_pending = 0;

  if (is_cgw_mode)
    {
      srv_handle->cgw_hstmt = NULL;
      srv_handle->cgw_col_binding = NULL;
      srv_handle->cgw_col_binding_buff = NULL;
      srv_handle->total_tuple_count = 0;
      srv_handle->stmt_type = CUBRID_STMT_NONE;
      srv_handle->is_cursor_open = false;
    }

  *new_handle = srv_handle;
  srv_handle_table[new_handle_id - 1] = srv_handle;
  if (new_handle_id > max_handle_id)
    {
      max_handle_id = new_handle_id;
    }

  current_handle_count++;

  return new_handle_id;
}

T_SRV_HANDLE *
hm_find_srv_handle (int h_id)
{
  if (h_id <= 0 || h_id > max_srv_handle)
    {
      return NULL;
    }

  return (srv_handle_table[h_id - 1]);
}

void
hm_srv_handle_free (int h_id)
{
  T_SRV_HANDLE *srv_handle;

  if (h_id <= 0 || h_id > max_srv_handle)
    {
      return;
    }

  srv_handle = srv_handle_table[h_id - 1];
  if (srv_handle == NULL)
    {
      return;
    }

  srv_handle_release_internal_lob_uploads (h_id, srv_handle);
  srv_handle_content_free (srv_handle);
  srv_handle_rm_tmp_file (h_id, srv_handle);

  FREE_MEM (srv_handle->classes);
  FREE_MEM (srv_handle->classes_chn);

  if (is_cgw_mode && cgw_free_stmt_func != NULL)
    {
      cgw_free_stmt_func (srv_handle);
    }

  FREE_MEM (srv_handle);
  srv_handle_table[h_id - 1] = NULL;
  current_handle_count--;
}

void
hm_srv_handle_free_all (bool free_holdable)
{
  T_SRV_HANDLE *srv_handle;
  int i;
  int new_max_handle_id = 0;

  for (i = 0; i < max_handle_id; i++)
    {
      srv_handle = srv_handle_table[i];
      if (srv_handle == NULL)
	{
	  continue;
	}

      if (srv_handle->is_holdable && !free_holdable)
	{
	  new_max_handle_id = i;
	  continue;
	}

      /* no release here: the driver may re-prepare and run the statement again with the same uploads */
      FREE_MEM (srv_handle->internal_lob_upload_tokens);
      srv_handle_content_free (srv_handle);
      srv_handle_rm_tmp_file (i + 1, srv_handle);

      if (is_cgw_mode && cgw_free_stmt_func != NULL)
	{
	  cgw_free_stmt_func (srv_handle);
	}

      FREE_MEM (srv_handle);
      srv_handle_table[i] = NULL;
      current_handle_count--;
    }

  max_handle_id = new_max_handle_id;
  if (free_holdable)
    {
      current_handle_count = 0;
      as_info->num_holdable_results = 0;
    }
}

void
hm_srv_handle_unset_prepare_flag_all (void)
{
  T_SRV_HANDLE *srv_handle;
  int i;

  for (i = 0; i < max_handle_id; i++)
    {
      srv_handle = srv_handle_table[i];
      if (srv_handle == NULL)
	{
	  continue;
	}

      srv_handle->is_prepared = FALSE;
    }

  hm_srv_handle_qresult_end_all (true);
}

void
hm_srv_handle_qresult_end_all (bool end_holdable)
{
  T_SRV_HANDLE *srv_handle;
  int i;

  for (i = 0; i < max_handle_id; i++)
    {
      srv_handle = srv_handle_table[i];
      if (srv_handle == NULL)
	{
	  continue;
	}

      if (srv_handle->is_holdable && !end_holdable)
	{
	  /* do not close holdable results */
	  srv_handle->is_from_current_transaction = false;
	  continue;
	}

      if (srv_handle->is_holdable && !srv_handle->is_from_current_transaction)
	{
	  /* end only holdable handles from the current transaction */
	  continue;
	}

      if (srv_handle->schema_type < 0 || srv_handle->schema_type == CCI_SCH_CLASS
	  || srv_handle->schema_type == CCI_SCH_VCLASS || srv_handle->schema_type == CCI_SCH_ATTRIBUTE
	  || srv_handle->schema_type == CCI_SCH_CLASS_ATTRIBUTE || srv_handle->schema_type == CCI_SCH_QUERY_SPEC
	  || srv_handle->schema_type == CCI_SCH_DIRECT_SUPER_CLASS || srv_handle->schema_type == CCI_SCH_PRIMARY_KEY
	  || srv_handle->schema_type == CCI_SCH_ATTR_WITH_SYNONYM)
	{
	  hm_qresult_end (srv_handle, FALSE);
	}
    }
}

/* true if a commit would end a result set still open (not holdable) or drop the locks it updates under */
bool
hm_has_result_tied_to_tran (void)
{
  /* [리뷰] holdable이고 updatable이 아닌 결과만 커밋에 살아남으므로 건너뛴다.
   * holdable+updatable 결과는 갱신 락을 지켜야 해서 커밋을 막는다. */
  T_SRV_HANDLE *srv_handle;
  int i, j;

  for (i = 0; i < max_handle_id; i++)
    {
      srv_handle = srv_handle_table[i];
      if (srv_handle == NULL || srv_handle->q_result == NULL
	  || (srv_handle->is_holdable && srv_handle->is_updatable != TRUE))
	{
	  continue;
	}

      for (j = 0; j < srv_handle->num_q_result; j++)
	{
	  if (srv_handle->q_result[j].result != NULL)
	    {
	      return true;
	    }
	}
    }

  return false;
}

#if defined(ENABLE_UNUSED_FUNCTION)
void
hm_srv_handle_set_pooled ()
{
  T_SRV_HANDLE *srv_handle;
  int i;

  for (i = 0; i < max_handle_id; i++)
    {
      srv_handle = srv_handle_table[i];
      if (srv_handle == NULL)
	{
	  continue;
	}

      srv_handle->is_pooled = 1;
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
hm_qresult_clear (T_QUERY_RESULT * q_result)
{
  memset (q_result, 0, sizeof (T_QUERY_RESULT));
}

void
hm_qresult_end (T_SRV_HANDLE * srv_handle, char free_flag)
{
  T_QUERY_RESULT *q_result;
  int i;

  q_result = srv_handle->q_result;
  if (q_result)
    {
      for (i = 0; i < srv_handle->num_q_result; i++)
	{
	  if (q_result[i].copied != TRUE && q_result[i].result)
	    {
	      hm_free_result (q_result[i].result);

	      if (q_result[i].is_holdable == true)
		{
		  q_result[i].is_holdable = false;
		  as_info->num_holdable_results--;
		}
	    }
	  q_result[i].result = NULL;

	  if (q_result[i].column_info)
	    {
	      db_query_format_free ((DB_QUERY_TYPE *) q_result[i].column_info);
	    }

	  q_result[i].column_info = NULL;
	  if (free_flag == TRUE)
	    {
	      col_update_info_free (&(q_result[i]));
	      FREE_MEM (q_result[i].null_type_column);
	    }
	}

      if (free_flag == TRUE)
	{
	  FREE_MEM (q_result);
	}
    }

  if (free_flag == TRUE)
    {
      srv_handle->q_result = NULL;
      srv_handle->num_q_result = 0;
      srv_handle->cur_result_index = 0;
    }

  srv_handle->cur_result = NULL;
  srv_handle->has_result_set = false;
}

void
hm_session_free (T_SRV_HANDLE * srv_handle)
{
  if (srv_handle->session)
    {
      db_close_session ((DB_SESSION *) (srv_handle->session));
    }
  srv_handle->session = NULL;
}

void
hm_col_update_info_clear (T_COL_UPDATE_INFO * col_update_info)
{
  memset (col_update_info, 0, sizeof (T_COL_UPDATE_INFO));
}

static void
srv_handle_content_free (T_SRV_HANDLE * srv_handle)
{
  FREE_MEM (srv_handle->sql_stmt);
  if (!is_cgw_mode)
    {
      hm_prepare_call_info_free (srv_handle->prepare_call_info);

      switch (srv_handle->schema_type)
	{
	case CCI_SCH_CLASS:
	case CCI_SCH_VCLASS:
	case CCI_SCH_ATTRIBUTE:
	case CCI_SCH_CLASS_ATTRIBUTE:
	case CCI_SCH_QUERY_SPEC:
	case CCI_SCH_DIRECT_SUPER_CLASS:
	case CCI_SCH_PRIMARY_KEY:
	case CCI_SCH_ATTR_WITH_SYNONYM:
	  hm_qresult_end (srv_handle, TRUE);
	  hm_session_free (srv_handle);
	  break;

	case CCI_SCH_CLASS_PRIVILEGE:
	case CCI_SCH_ATTR_PRIVILEGE:
	case CCI_SCH_SUPERCLASS:
	case CCI_SCH_SUBCLASS:
	  FREE_MEM (srv_handle->session);
	  srv_handle->cur_result = NULL;
	  break;

	case CCI_SCH_TRIGGER:
	  if (srv_handle->session)
	    {
	      db_objlist_free ((DB_OBJLIST *) (srv_handle->session));
	    }
	  srv_handle->cur_result = NULL;
	  break;

	case CCI_SCH_IMPORTED_KEYS:
	case CCI_SCH_EXPORTED_KEYS:
	case CCI_SCH_CROSS_REFERENCE:
	  {
	    T_FK_INFO_RESULT *fk_res = (T_FK_INFO_RESULT *) srv_handle->session;

	    if (fk_res != NULL)
	      {
		release_all_fk_info_results (fk_res);
		srv_handle->session = NULL;
	      }
	    srv_handle->cur_result = NULL;
	  }
	  break;

	default:
	  if (srv_handle->schema_type < 0)
	    {
	      hm_qresult_end (srv_handle, TRUE);
	      hm_session_free (srv_handle);
	    }
	  break;
	}
    }
  else
    {
      srv_handle->session = NULL;
    }
}

static void
col_update_info_free (T_QUERY_RESULT * q_result)
{
  int i;

  if (q_result->col_update_info)
    {
      for (i = 0; i < q_result->num_column; i++)
	{
	  FREE_MEM (q_result->col_update_info[i].attr_name);
	  FREE_MEM (q_result->col_update_info[i].class_name);
	}
      FREE_MEM (q_result->col_update_info);
    }
  q_result->col_updatable = FALSE;
  q_result->num_column = 0;
}

static void
srv_handle_rm_tmp_file (int h_id, T_SRV_HANDLE * srv_handle)
{
  if (srv_handle->query_info_flag == TRUE)
    {
      char *p;

      p = cas_log_query_plan_file (h_id);
      if (p != NULL)
	{
	  unlink (p);
	}
    }
}

int
hm_srv_handle_get_current_count (void)
{
  return current_handle_count;
}

void
hm_set_current_srv_handle (int h_id)
{
  if (tran_is_in_libcas ())
    {
      /* do nothing */
    }
  else
    {
      current_handle_id = h_id;
    }
}

static void
prepare_call_info_dbval_clear (T_PREPARE_CALL_INFO * call_info)
{
  DB_VALUE **args;
  int i = 0;

  if (call_info)
    {
      if (call_info->dbval_ret)
	{
	  db_value_clear ((DB_VALUE *) call_info->dbval_ret);
	  db_make_null ((DB_VALUE *) call_info->dbval_ret);
	}

      args = (DB_VALUE **) call_info->dbval_args;

      if (call_info->is_first_out)
	{
	  db_value_clear (args[0]);
	  i++;
	}

      for (; i < call_info->num_args; i++)
	{
	  if (args[i])
	    {
	      db_make_null (args[i]);
	    }
	}
    }
}

void
hm_free_result (void *res)
{
  db_query_end ((DB_QUERY_RESULT *) res);
}

void
hm_prepare_call_info_free (T_PREPARE_CALL_INFO * call_info)
{
  if (call_info)
    {
      int i;

      prepare_call_info_dbval_clear (call_info);
      FREE_MEM (call_info->dbval_ret);
      for (i = 0; i < call_info->num_args; i++)
	{
	  FREE_MEM (((DB_VALUE **) call_info->dbval_args)[i]);
	}
      FREE_MEM (call_info->dbval_args);
      FREE_MEM (call_info->param_mode);
      FREE_MEM (call_info);
    }
}

void
release_all_fk_info_results (T_FK_INFO_RESULT * fk_res)
{
  T_FK_INFO_RESULT *fk, *fk_release;

  fk = fk_res;
  while (fk != NULL)
    {
      fk_release = fk;
      fk = fk->next;

      FREE_MEM (fk_release->pktable_name);
      FREE_MEM (fk_release->pkcolumn_name);
      FREE_MEM (fk_release->fktable_name);
      FREE_MEM (fk_release->fkcolumn_name);
      FREE_MEM (fk_release->fk_name);
      FREE_MEM (fk_release->pk_name);
      FREE_MEM (fk_release);
    }
}

void
hm_set_cgw_mode (bool enabled)
{
  is_cgw_mode = enabled;
}

void
hm_set_cgw_free_stmt_func (cgw_free_stmt_func_t func)
{
  cgw_free_stmt_func = func;
}

/* Is the token bound by an open statement other than h_id? */
static bool
srv_handle_internal_lob_upload_shared (int h_id, INT64 token)
{
  int j, k;

  for (j = 0; j < max_handle_id; j++)
    {
      T_SRV_HANDLE *other = srv_handle_table[j];

      if (other == NULL || j + 1 == h_id)
	{
	  continue;
	}
      for (k = 0; k < other->internal_lob_upload_token_count; k++)
	{
	  if (other->internal_lob_upload_tokens[k] == token)
	    {
	      return true;
	    }
	}
    }
  return false;
}

/* The statement now binds these uploads: one it bound before and no longer does is released (unless another open
 * statement binds it); one it still binds stays for a re-execution. */
int
hm_srv_handle_set_internal_lob_uploads (T_SRV_HANDLE * srv_handle, const INT64 * tokens, int count)
{
  INT64 *kept = NULL;
  int i, j;

  if (count > 0)
    {
      kept = (INT64 *) MALLOC (sizeof (INT64) * count);
      if (kept == NULL)
	{
	  return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
	}
      memcpy (kept, tokens, sizeof (INT64) * count);
    }

  for (i = 0; i < srv_handle->internal_lob_upload_token_count; i++)
    {
      INT64 token = srv_handle->internal_lob_upload_tokens[i];

      for (j = 0; j < count && kept[j] != token; j++)
	{
	  ;
	}
      if (j == count && !srv_handle_internal_lob_upload_shared (srv_handle->id, token))
	{
	  (void) internal_lob_upload_release_from_server (token);
	}
    }
  FREE_MEM (srv_handle->internal_lob_upload_tokens);
  srv_handle->internal_lob_upload_tokens = kept;
  srv_handle->internal_lob_upload_token_count = count;
  return 0;
}

/* A token can be bound by more than one open statement (one Clob object bound twice): the last one releases it. */
static void
srv_handle_release_internal_lob_uploads (int h_id, T_SRV_HANDLE * srv_handle)
{
  int i;

  for (i = 0; i < srv_handle->internal_lob_upload_token_count; i++)
    {
      INT64 token = srv_handle->internal_lob_upload_tokens[i];
      bool shared = srv_handle_internal_lob_upload_shared (h_id, token);

      /* [리뷰] 문장 핸들 close 때 바로 해제한다. 다른 핸들도 쓰면 마지막 핸들이 해제한다.
       * hm_srv_handle_free_all은 해제 요청 없이 배열만 비운다. */
      if (!shared)
	{
	  (void) internal_lob_upload_release_from_server (token);
	}
    }
  FREE_MEM (srv_handle->internal_lob_upload_tokens);
  srv_handle->internal_lob_upload_token_count = 0;
}
