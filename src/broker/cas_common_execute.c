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
 * cas_common_execute.c - Shared execution functions used by both cas_execute.c and cas_cgw_execute.c
 */

#ident "$Id$"

#include "cas_common_execute.h"
#include "cas_net_buf.h"
#include "cas_util.h"
#include "perf_monitor.h"
#include "release_string.h"
#include "dbi.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>


/* Shared cas_u_type array for CAS and CGW */
static char cas_u_type[] = { 0,	/* 0 */
  CCI_U_TYPE_INT,		/* 1 */
  CCI_U_TYPE_FLOAT,		/* 2 */
  CCI_U_TYPE_DOUBLE,		/* 3 */
  CCI_U_TYPE_STRING,		/* 4 */
  CCI_U_TYPE_OBJECT,		/* 5 */
  CCI_U_TYPE_SET,		/* 6 */
  CCI_U_TYPE_MULTISET,		/* 7 */
  CCI_U_TYPE_SEQUENCE,		/* 8 */
  0,				/* 9 */
  CCI_U_TYPE_TIME,		/* 10 */
  CCI_U_TYPE_TIMESTAMP,		/* 11 */
  CCI_U_TYPE_DATE,		/* 12 */
  CCI_U_TYPE_MONETARY,		/* 13 */
  0, 0, 0, 0,			/* 14 - 17 */
  CCI_U_TYPE_SHORT,		/* 18 */
  0, 0, 0,			/* 19 - 21 */
  CCI_U_TYPE_NUMERIC,		/* 22 */
  CCI_U_TYPE_BIT,		/* 23 */
  CCI_U_TYPE_VARBIT,		/* 24 */
  CCI_U_TYPE_CHAR,		/* 25 */
  CCI_U_TYPE_NCHAR_DEPRECATED,	/* 26 */
  CCI_U_TYPE_VARNCHAR_DEPRECATED,	/* 27 */
  CCI_U_TYPE_RESULTSET,		/* 28 */
  0, 0,				/* 29 - 30 */
  CCI_U_TYPE_BIGINT,		/* 31 */
  CCI_U_TYPE_DATETIME,		/* 32 */
  CCI_U_TYPE_BLOB,		/* 33 */
  CCI_U_TYPE_CLOB,		/* 34 */
  CCI_U_TYPE_ENUM,		/* 35 */
  CCI_U_TYPE_TIMESTAMPTZ,	/* 36 */
  CCI_U_TYPE_TIMESTAMPLTZ,	/* 37 */
  CCI_U_TYPE_DATETIMETZ,	/* 38 */
  CCI_U_TYPE_DATETIMELTZ,	/* 39 */
  CCI_U_TYPE_JSON,		/* 40 */
};

static CAS_ERROR_LOG_HANDLE_CONTEXT *cas_EHCTX = NULL;

char
ux_db_type_to_cas_type (int db_type)
{
  /* todo: T_CCI_U_TYPE duplicates db types. */
  if (db_type < DB_TYPE_FIRST || db_type > DB_TYPE_LAST)
    {
      return CCI_U_TYPE_NULL;
    }

  return (cas_u_type[db_type]);
}

void
ux_set_utype_for_enum (char u_type)
{
  cas_u_type[DB_TYPE_ENUMERATION] = u_type;
}

void
ux_set_utype_for_timestamptz (char u_type)
{
  cas_u_type[DB_TYPE_TIMESTAMPTZ] = u_type;
}

void
ux_set_utype_for_datetimetz (char u_type)
{
  cas_u_type[DB_TYPE_DATETIMETZ] = u_type;
}

void
ux_set_utype_for_timestampltz (char u_type)
{
  cas_u_type[DB_TYPE_TIMESTAMPLTZ] = u_type;
}

void
ux_set_utype_for_datetimeltz (char u_type)
{
  cas_u_type[DB_TYPE_DATETIMELTZ] = u_type;
}

void
ux_set_utype_for_json (char u_type)
{
  cas_u_type[DB_TYPE_JSON] = u_type;
}

/* ========================================================================
 * SQL statement parsing functions
 * ======================================================================== */

char
get_stmt_type (char *stmt)
{
  if (strncasecmp (stmt, "insert", 6) == 0)
    {
      return CUBRID_STMT_INSERT;
    }
  else if (strncasecmp (stmt, "update", 6) == 0)
    {
      return CUBRID_STMT_UPDATE;
    }
  else if (strncasecmp (stmt, "delete", 6) == 0)
    {
      return CUBRID_STMT_DELETE;
    }
  else if (strncasecmp (stmt, "call", 4) == 0)
    {
      return CUBRID_STMT_CALL;
    }
  else if (strncasecmp (stmt, "evaluate", 8) == 0)
    {
      return CUBRID_STMT_EVALUATE;
    }
  else if (strncasecmp (stmt, "select", 6) == 0)
    {
      return CUBRID_STMT_SELECT;
    }
  else if (strncasecmp (stmt, "merge", 5) == 0)
    {
      return CUBRID_STMT_MERGE;
    }
  else
    {
      return CUBRID_MAX_STMT_TYPE;
    }
}

/* ========================================================================
 * Common execution functions
 * ======================================================================== */

int
ux_get_db_version (T_NET_BUF * net_buf, T_REQ_INFO * req_info)
{
  char *p;

  p = (char *) rel_build_number ();

  net_buf_cp_int (net_buf, 0, NULL);	/* result code */
  if (p == NULL)
    {
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      net_buf_cp_str (net_buf, p, strlen (p) + 1);
    }

  return 0;
}

char *
consume_tokens (char *stmt, STATEMENT_STATUS stmt_status)
{
  char *p = stmt;

  if (stmt_status == SQL_STYLE_COMMENT || stmt_status == CPP_STYLE_COMMENT)
    {
      for (; *p; p++)
	{
	  if (*p == '\n')
	    {
	      break;
	    }
	}
    }
  else if (stmt_status == C_STYLE_COMMENT)
    {
      for (; *p; p++)
	{
	  if (*p == '*' && *(p + 1) == '/')
	    {
	      p++;
	      break;
	    }
	}
    }
  else if (stmt_status == SINGLE_QUOTED_STRING)
    {
      for (; *p; p++)
	{
	  if (*p == '\'' && *(p + 1) == '\'')
	    {
	      p++;
	    }
	  else if (cas_default_no_backslash_escapes == false && *p == '\\')
	    {
	      p++;
	    }
	  else if (*p == '\'')
	    {
	      break;
	    }
	}
    }
  else if (stmt_status == DOUBLE_QUOTED_STRING)
    {
      for (; *p; p++)
	{
	  if (*p == '\"' && *(p + 1) == '\"')
	    {
	      p++;
	    }
	  else if (cas_default_no_backslash_escapes == false && *p == '\\')
	    {
	      p++;
	    }
	  else if (*p == '\"')
	    {
	      break;
	    }
	}
    }

  return p;
}

int
get_num_markers (char *stmt)
{
  char *p;
  int num_markers = 0;

  for (p = stmt; *p; p++)
    {
      if (*p == '?')
	{
	  num_markers++;
	}
      else if (*p == '-' && *(p + 1) == '-')
	{
	  p = consume_tokens (p + 2, SQL_STYLE_COMMENT);
	}
      else if (*p == '/' && *(p + 1) == '*')
	{
	  p = consume_tokens (p + 2, C_STYLE_COMMENT);
	}
      else if (*p == '/' && *(p + 1) == '/')
	{
	  p = consume_tokens (p + 2, CPP_STYLE_COMMENT);
	}
      else if (*p == '\'')
	{
	  p = consume_tokens (p + 1, SINGLE_QUOTED_STRING);
	}
      else if (cas_default_ansi_quotes == false && *p == '\"')
	{
	  p = consume_tokens (p + 1, DOUBLE_QUOTED_STRING);
	}

      if (*p == '\0')
	{
	  break;
	}
    }

  return num_markers;
}

void
ux_end_tran_cleanup (int tran_type)
{
  if (!as_info->cur_statement_pooling)
    {
      if (tran_type == CCI_TRAN_COMMIT)
	{
	  hm_srv_handle_free_all (false);
	}
      else
	{
	  hm_srv_handle_free_all (true);
	}
    }
  else
    {
      if (tran_type == CCI_TRAN_COMMIT)
	{
	  /* do not close holdable results on commit */
	  hm_srv_handle_qresult_end_all (false);
	}
      else
	{
	  /* clear all queries */
	  hm_srv_handle_qresult_end_all (true);
	}
    }
}

void
update_query_execution_count (T_APPL_SERVER_INFO * as_info_p, char stmt_type)
{
  assert (as_info_p != NULL);

  as_info_p->num_queries_processed %= MAX_DIAG_DATA_VALUE;
  as_info_p->num_queries_processed++;

  switch (stmt_type)
    {
    case CUBRID_STMT_SELECT:
      as_info_p->num_select_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_select_queries++;
      break;
    case CUBRID_STMT_INSERT:
      as_info_p->num_insert_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_insert_queries++;
      break;
    case CUBRID_STMT_UPDATE:
      as_info_p->num_update_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_update_queries++;
      break;
    case CUBRID_STMT_DELETE:
      as_info_p->num_delete_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_delete_queries++;
      break;
    default:
      break;
    }
}

void
update_error_query_count (T_APPL_SERVER_INFO * as_info_p, const T_ERROR_INFO * err_info_p)
{
  assert (as_info_p != NULL);
  assert (err_info_p != NULL);

  if (err_info_p->err_number != ER_QPROC_INVALID_XASLNODE)
    {
      as_info_p->num_error_queries %= MAX_DIAG_DATA_VALUE;
      as_info_p->num_error_queries++;
    }

  if (err_info_p->err_indicator == DBMS_ERROR_INDICATOR)
    {
      if (err_info_p->err_number == ER_BTREE_UNIQUE_FAILED || err_info_p->err_number == ER_UNIQUE_VIOLATION_WITHKEY)
	{
	  as_info_p->num_unique_error_queries %= MAX_DIAG_DATA_VALUE;
	  as_info_p->num_unique_error_queries++;
	}
    }
}

bool
check_auto_commit_after_getting_result (T_SRV_HANDLE * srv_handle)
{
  // To close an updatable cursor is dangerous since it lose locks and updating cursor is allowed before closing it.

  if (srv_handle->auto_commit_mode == TRUE && srv_handle->cur_result_index == srv_handle->num_q_result
      && srv_handle->forward_only_cursor == TRUE && srv_handle->is_updatable == FALSE)
    {
      return true;
    }

  return false;
}

void
prepare_column_info_set (T_NET_BUF * net_buf, char ut, short scale, int prec, char charset, const char *col_name,
			 const char *default_value, char auto_increment, char unique_key, char primary_key,
			 char reverse_index, char reverse_unique, char foreign_key, char shared, const char *attr_name,
			 const char *class_name, char is_non_null, T_BROKER_VERSION client_version)
{
  const char *attr_name_p, *class_name_p;
  int attr_name_len, class_name_len;

  net_buf_column_info_set (net_buf, ut, scale, prec, charset, col_name);

  attr_name_p = (attr_name != NULL) ? attr_name : "";
  attr_name_len = strlen (attr_name_p);

  class_name_p = (class_name != NULL) ? class_name : "";
  class_name_len = strlen (class_name_p);

  net_buf_cp_int (net_buf, attr_name_len + 1, NULL);
  net_buf_cp_str (net_buf, attr_name_p, attr_name_len + 1);

  net_buf_cp_int (net_buf, class_name_len + 1, NULL);
  net_buf_cp_str (net_buf, class_name_p, class_name_len + 1);

  if (is_non_null >= 1)
    {
      is_non_null = 1;
    }
  else if (is_non_null < 0)
    {
      is_non_null = 0;
    }

  net_buf_cp_byte (net_buf, is_non_null);

  if (client_version < CAS_MAKE_VER (8, 3, 0))
    {
      return;
    }

  if (default_value == NULL)
    {
      net_buf_cp_int (net_buf, 1, NULL);
      net_buf_cp_byte (net_buf, '\0');
    }
  else
    {
      int len = strlen (default_value) + 1;

      net_buf_cp_int (net_buf, len, NULL);
      net_buf_cp_str (net_buf, default_value, len);
    }

  net_buf_cp_byte (net_buf, auto_increment);
  net_buf_cp_byte (net_buf, unique_key);
  net_buf_cp_byte (net_buf, primary_key);
  net_buf_cp_byte (net_buf, reverse_index);
  net_buf_cp_byte (net_buf, reverse_unique);
  net_buf_cp_byte (net_buf, foreign_key);
  net_buf_cp_byte (net_buf, shared);
}

bool
has_stmt_result_set (char stmt_type)
{
  switch (stmt_type)
    {
    case CUBRID_STMT_SELECT:
    case CUBRID_STMT_CALL:
    case CUBRID_STMT_GET_STATS:
    case CUBRID_STMT_EVALUATE:
      return true;

    default:
      break;
    }

  return false;
}

/* ========================================================================
 * Error log handler functions for execution
 * ======================================================================== */

char *
cas_log_error_handler_asprint (char *buf, size_t bufsz, bool clear)
{
  char *buf_p;
  unsigned int from, to;

  if (buf == NULL || bufsz <= 0)
    {
      return NULL;
    }

  if (cas_EHCTX == NULL || cas_EHCTX->from == 0)
    {
      buf[0] = '\0';
      return buf;
    }

  from = cas_EHCTX->from;
  to = cas_EHCTX->to;

  if (clear)
    {
      cas_EHCTX->from = 0;
      cas_EHCTX->to = 0;
    }

  /* ", EID = <int> ~ <int>" : 32 bytes suffice */
  if (bufsz < 32)
    {
      buf_p = (char *) malloc (32);

      if (buf_p == NULL)
	{
	  return NULL;
	}
    }
  else
    {
      buf_p = buf;
    }

  /* actual print */
  if (to != 0)
    {
      snprintf (buf_p, 32, ", EID = %u ~ %u", from, to);
    }
  else
    {
      snprintf (buf_p, 32, ", EID = %u", from);
    }

  return buf_p;
}

/*****************************
  move from cas_log.c
 *****************************/
void
cas_log_error_handler (unsigned int eid)
{
  if (cas_EHCTX == NULL)
    {
      return;
    }

  if (cas_EHCTX->from == 0)
    {
      cas_EHCTX->from = eid;
    }
  else
    {
      cas_EHCTX->to = eid;
    }
}

/*
 * get_error_log_eids - get error identifier string
 *    return: pointer to internal buffer
 * NOTE:
 * this function is not MT safe. Returned address is guaranteed to be valid
 * until next get_error_log_eids() call.
 */
char *
get_error_log_eids (int err)
{
  static char *pending_alloc = NULL;
  static char buffer[512];
  char *buf;

  if (err >= 0)
    {
      return (char *) "";
    }

  if (pending_alloc != NULL)
    {
      free (pending_alloc);
      pending_alloc = NULL;
    }

  buf = cas_log_error_handler_asprint (buffer, sizeof (buffer), true);
  if (buf != buffer)
    {
      pending_alloc = buf;
    }

  return buf;
}

void
cas_log_error_handler_begin (void)
{
  CAS_ERROR_LOG_HANDLE_CONTEXT *ectx;

  ectx = (CAS_ERROR_LOG_HANDLE_CONTEXT *) malloc (sizeof (*ectx));
  if (ectx == NULL)
    {
      return;
    }

  ectx->from = 0;
  ectx->to = 0;

  if (cas_EHCTX != NULL)
    {
      free (cas_EHCTX);
    }

  cas_EHCTX = ectx;
  (void) db_register_error_log_handler (cas_log_error_handler);
}

void
cas_log_error_handler_end (void)
{
  if (cas_EHCTX != NULL)
    {
      free (cas_EHCTX);
      cas_EHCTX = NULL;
      (void) db_register_error_log_handler (NULL);
    }
}

void
cas_log_error_handler_clear (void)
{
  if (cas_EHCTX == NULL)
    {
      return;
    }

  cas_EHCTX->from = 0;
  cas_EHCTX->to = 0;
}
