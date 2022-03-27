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

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "cas_db_inc.h"
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#include "cas_execute.h"

#include "cas.h"
#include "cas_common.h"
#include "cas_handle.h"
#include "cas_handle_procedure.hpp"
#include "cas_log.h"

#define SRV_HANDLE_ALLOC_SIZE		256

static void srv_handle_content_free (T_SRV_HANDLE * srv_handle);
static void col_update_info_free (T_QUERY_RESULT * q_result);
static void srv_handle_rm_tmp_file (int h_id, T_SRV_HANDLE * srv_handle);

static T_SRV_HANDLE **srv_handle_table = NULL;
static int max_srv_handle = 0;
static int max_handle_id = 0;
#if !defined(LIBCAS_FOR_JSP)
static int current_handle_count = 0;
#endif

/* implemented in transaction_cl.c */
extern bool tran_is_in_libcas (void);

static cas_procedure_handle_table procedure_handle_table;
static int current_handle_id = -1;	/* it is used for javasp */

int
hm_new_srv_handle (T_SRV_HANDLE ** new_handle, unsigned int seq_num)
{
  int i;
  int new_max_srv_handle;
  int new_handle_id = 0;
  T_SRV_HANDLE **new_srv_handle_table = NULL;
  T_SRV_HANDLE *srv_handle;

#if !defined(LIBCAS_FOR_JSP)
  if (cas_shard_flag == OFF && current_handle_count >= shm_appl->max_prepared_stmt_count)
    {
      return ERROR_INFO_SET (CAS_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED, CAS_ERROR_INDICATOR);
    }
#endif /* !LIBCAS_FOR_JSP */

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
#if defined(CAS_FOR_ORACLE)
  srv_handle->has_out_result = false;
#endif /* defined(CAS_FOR_ORACLE) */
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  srv_handle->send_metadata_before_execute = false;
  srv_handle->next_cursor_pos = 1;
#endif
#if !defined(LIBCAS_FOR_JSP)
  srv_handle->is_pooled = as_info->cur_statement_pooling;
#endif

#if defined(CAS_FOR_MYSQL)
  srv_handle->has_mysql_last_insert_id = false;
#endif /* CAS_FOR_MYSQL */

#if defined (CAS_FOR_CGW)
  srv_handle->cgw_handle = NULL;
  srv_handle->total_tuple_count = 0;
  srv_handle->stmt_type = CUBRID_STMT_NONE;
#endif /* CAS_FOR_CGW */

  *new_handle = srv_handle;
  srv_handle_table[new_handle_id - 1] = srv_handle;
  if (new_handle_id > max_handle_id)
    {
      max_handle_id = new_handle_id;
    }

#if !defined(LIBCAS_FOR_JSP)
  current_handle_count++;
#endif

  /* register handler id created from server-side JDBC */
  cas_procedure_handle_add (procedure_handle_table, current_handle_id, new_handle_id);

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

  cas_procedure_handle_free (procedure_handle_table, current_handle_id, h_id);
  srv_handle_content_free (srv_handle);
  srv_handle_rm_tmp_file (h_id, srv_handle);

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  FREE_MEM (srv_handle->classes);
  FREE_MEM (srv_handle->classes_chn);
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#if defined (CAS_FOR_CGW)
  srv_handle->cgw_handle = NULL;
#endif

  FREE_MEM (srv_handle);
  srv_handle_table[h_id - 1] = NULL;
#if !defined(LIBCAS_FOR_JSP)
  current_handle_count--;
#endif
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

      srv_handle_content_free (srv_handle);
      srv_handle_rm_tmp_file (i + 1, srv_handle);
#if defined (CAS_FOR_CGW)
      srv_handle->cgw_handle = NULL;
#endif /* CAS_FOR_CGW */
      FREE_MEM (srv_handle);
      srv_handle_table[i] = NULL;
#if !defined(LIBCAS_FOR_JSP)
      current_handle_count--;
#endif
    }

  max_handle_id = new_max_handle_id;
#if !defined(LIBCAS_FOR_JSP)
  if (free_holdable)
    {
      current_handle_count = 0;
      as_info->num_holdable_results = 0;
    }
#endif
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

#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
      hm_qresult_end (srv_handle, FALSE);
#else /* CAS_FOR_MYSQL */

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
	  || srv_handle->schema_type == CCI_SCH_DIRECT_SUPER_CLASS || srv_handle->schema_type == CCI_SCH_PRIMARY_KEY)
	{
	  hm_qresult_end (srv_handle, FALSE);
	}
#endif
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)

#if defined(CAS_FOR_MYSQL)
  if (srv_handle->session)
    {
      cas_mysql_stmt_free_result (srv_handle->session);
    }
#endif /* CAS_FOR_MYSQL */

  if (free_flag == TRUE)
    {
      ux_free_result (q_result);
      FREE_MEM (q_result);
      srv_handle->q_result = NULL;
    }
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  if (q_result)
    {
      for (i = 0; i < srv_handle->num_q_result; i++)
	{
	  if (q_result[i].copied != TRUE && q_result[i].result)
	    {
	      ux_free_result (q_result[i].result);

	      if (q_result[i].is_holdable == true)
		{
		  q_result[i].is_holdable = false;
#if !defined(LIBCAS_FOR_JSP)
		  as_info->num_holdable_results--;
#endif
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
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
}

void
hm_session_free (T_SRV_HANDLE * srv_handle)
{
  if (srv_handle->session)
    {
#if defined(CAS_FOR_ORACLE)
      cas_oracle_stmt_close (srv_handle->session);
#elif defined(CAS_FOR_MYSQL)
      cas_mysql_stmt_close (srv_handle->session);
#else /* CAS_FOR_ORACLE */
      db_close_session ((DB_SESSION *) (srv_handle->session));
#endif /* CAS_FOR_ORACLE */
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
#if defined(CAS_FOR_ORACLE) || defined(CAS_FOR_MYSQL)
  FREE_MEM (srv_handle->sql_stmt);
  ux_prepare_call_info_free (srv_handle->prepare_call_info);
  hm_qresult_end (srv_handle, TRUE);
  hm_session_free (srv_handle);
#else /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
  FREE_MEM (srv_handle->sql_stmt);
  ux_prepare_call_info_free (srv_handle->prepare_call_info);

  if (srv_handle->schema_type < 0 || srv_handle->schema_type == CCI_SCH_CLASS
      || srv_handle->schema_type == CCI_SCH_VCLASS || srv_handle->schema_type == CCI_SCH_ATTRIBUTE
      || srv_handle->schema_type == CCI_SCH_CLASS_ATTRIBUTE || srv_handle->schema_type == CCI_SCH_QUERY_SPEC
      || srv_handle->schema_type == CCI_SCH_DIRECT_SUPER_CLASS || srv_handle->schema_type == CCI_SCH_PRIMARY_KEY)
    {
      hm_qresult_end (srv_handle, TRUE);
      hm_session_free (srv_handle);
    }
  else if (srv_handle->schema_type == CCI_SCH_CLASS_PRIVILEGE || srv_handle->schema_type == CCI_SCH_ATTR_PRIVILEGE
	   || srv_handle->schema_type == CCI_SCH_SUPERCLASS || srv_handle->schema_type == CCI_SCH_SUBCLASS)
    {
      FREE_MEM (srv_handle->session);
      srv_handle->cur_result = NULL;
    }
  else if (srv_handle->schema_type == CCI_SCH_TRIGGER)
    {
      if (srv_handle->session)
	{
	  db_objlist_free ((DB_OBJLIST *) (srv_handle->session));
	}
      srv_handle->cur_result = NULL;
    }
  else if (srv_handle->schema_type == CCI_SCH_IMPORTED_KEYS || srv_handle->schema_type == CCI_SCH_EXPORTED_KEYS
	   || srv_handle->schema_type == CCI_SCH_CROSS_REFERENCE)
    {
      T_FK_INFO_RESULT *fk_res = (T_FK_INFO_RESULT *) srv_handle->session;

      if (fk_res != NULL)
	{
	  release_all_fk_info_results (fk_res);
	  srv_handle->session = NULL;
	}
      srv_handle->cur_result = NULL;
    }
#endif /* CAS_FOR_ORACLE || CAS_FOR_MYSQL */
}

static void
col_update_info_free (T_QUERY_RESULT * q_result)
{
  int i;

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
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
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
}

static void
srv_handle_rm_tmp_file (int h_id, T_SRV_HANDLE * srv_handle)
{
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  if (srv_handle->query_info_flag == TRUE)
    {
      char *p;

      p = cas_log_query_plan_file (h_id);
      if (p != NULL)
	{
	  unlink (p);
	}
    }
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
}

int
hm_srv_handle_get_current_count (void)
{
#if !defined(LIBCAS_FOR_JSP)
  return current_handle_count;
#else
  return 0;
#endif
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
