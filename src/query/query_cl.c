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
 * query_cl.c - Query processor main interface
 */

#ident "$Id$"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "query_cl.h"

#include "compile_context.h"
#include "db_elo.h"
#include "dbtype.h"
#include "internal_lob_marker.h"
#include "optimizer.h"
#include "network_interface_cl.h"
#include "internal_lob_stream_kind.h"
#include "transaction_cl.h"
#include "xasl.h"
#include "execute_statement.h"

/*
 * prepare_query () - Prepares a query for later (and repetitive)
 *                         execution
 *   return		 : Error code
 *   context (in)	 : query string; used for hash key of the XASL cache
 *   stream (in/out)	 : XASL stream, size, xasl_id & xasl_header;
 *                         set to NULL if you want to look up the XASL cache
 *
 *   NOTE: If stream->xasl_header is not NULL, also XASL node header will be
 *	   requested from server.
 */
int
prepare_query (COMPILE_CONTEXT * context, XASL_STREAM * stream)
{
  int ret = NO_ERROR;

  assert (context->sql_hash_text);

  /* if QO_PARAM_LEVEL indicate no execution, just return */
  if (qo_need_skip_execution ())
    {
      return NO_ERROR;
    }

  /* allocate XASL_ID, the caller is responsible to free this */
  stream->xasl_id = (XASL_ID *) malloc (sizeof (XASL_ID));
  if (stream->xasl_id == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (XASL_ID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* send XASL stream to the server and get XASL_ID */
  ret = qmgr_prepare_query (context, stream);
  if (ret != NO_ERROR)
    {
      free_and_init (stream->xasl_id);
      ASSERT_ERROR ();
      return ret;
    }

  /* if the query is not found in the cache */
  if (stream->buffer == NULL && stream->xasl_id && XASL_ID_IS_NULL (stream->xasl_id))
    {
      free_and_init (stream->xasl_id);
    }

  assert (ret == NO_ERROR);

  return ret;
}

/*
 * execute_query () - Execute a prepared query
 *   return: Error code
 *   xasl_id(in)        : XASL file id that was a result of prepare_query()
 *   query_idp(out)     : query id to be used for getting results
 *   var_cnt(in)        : number of host variables
 *   varptr(in) : array of host variables (query input parameters)
 *   list_idp(out)      : query result file id (QFILE_LIST_ID)
 *   flag(in)   : flag
 *   clt_cache_time(in) :
 *   srv_cache_time(in) :
 */
int
execute_query (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt, const DB_VALUE * varptr,
	       QFILE_LIST_ID ** list_idp, QUERY_FLAG flag, CACHE_TIME * clt_cache_time, CACHE_TIME * srv_cache_time)
{
  int query_timeout;
  int ret = NO_ERROR;

  *list_idp = NULL;

  /* if QO_PARAM_LEVEL indicate no execution, just return */
  if (qo_need_skip_execution ())
    {
      return NO_ERROR;
    }

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      cdc_Trigger_involved = false;
    }

  query_timeout = tran_get_query_timeout ();
  /* send XASL file id and host variables to the server and get QFILE_LIST_ID */
  *list_idp =
    qmgr_execute_query (xasl_id, query_idp, var_cnt, varptr, flag, clt_cache_time, srv_cache_time, query_timeout);

  if (*list_idp == NULL)
    {
      return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
    }

  assert (ret == NO_ERROR);

  return ret;
}

#if defined(CS_MODE)
enum internal_lob_dml_input_kind
{
  INTERNAL_LOB_DML_INPUT_MEMORY,
  INTERNAL_LOB_DML_INPUT_ELO
};
typedef enum internal_lob_dml_input_kind INTERNAL_LOB_DML_INPUT_KIND;

typedef struct internal_lob_dml_input INTERNAL_LOB_DML_INPUT;
struct internal_lob_dml_input
{
  DB_TYPE type;
  INTERNAL_LOB_DML_INPUT_KIND kind;
  const char *data;
  DB_BIGINT data_length;
  DB_BIGINT logical_length;
  DB_ELO elo;
  char locator[PATH_MAX + 16];
};

static bool
query_is_internal_lob_dml_source (const DB_VALUE * value)
{
  DB_TYPE type;
  int marker;

  if (value == NULL || DB_IS_NULL (value))
    {
      return false;
    }

  type = DB_VALUE_TYPE (value);
  if (type != DB_TYPE_BLOB && type != DB_TYPE_CLOB)
    {
      return false;
    }

  marker = db_value_get_internal_lob_marker (value);
  return marker == DB_VALUE_INTERNAL_LOB_MARKER_NONE
    || marker == DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE || marker == DB_VALUE_INTERNAL_LOB_MARKER_PENDING;
}

static bool
query_parse_internal_lob_file_source (const DB_VALUE * value, INTERNAL_LOB_DML_INPUT * input)
{
  const char *data = NULL;
  char marker[PATH_MAX + 128];
  char type_char;
  const char *path;
  long long data_length;
  long long logical_length;
  int size = 0;
  size_t locator_length;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE, NULL, &data, &size)
      || !internal_lob_marker_parse_file_source (data, size, marker, (int) sizeof (marker), &type_char, &data_length,
						 &logical_length, &path))
    {
      return false;
    }

  if ((type_char == 'C' && input->type != DB_TYPE_CLOB) || (type_char == 'B' && input->type != DB_TYPE_BLOB)
      || data_length < 0 || data_length > DB_MAX_INTERNAL_LOB_LENGTH)
    {
      return false;
    }
  if ((input->type == DB_TYPE_CLOB && logical_length != -1)
      || (input->type == DB_TYPE_BLOB
	  && (logical_length < 0 || data_length > DB_BIGINT_MAX / 8 || logical_length != data_length * 8)))
    {
      return false;
    }

  locator_length = strlen (path);
  if (locator_length == 0 || locator_length >= sizeof (input->locator))
    {
      return false;
    }

  memcpy (input->locator, path, locator_length + 1);
  input->data_length = (DB_BIGINT) data_length;
  input->logical_length = input->type == DB_TYPE_BLOB ? (DB_BIGINT) logical_length : (DB_BIGINT) data_length;
  return true;
}

static bool
query_parse_internal_lob_pending_source (const DB_VALUE * value, INTERNAL_LOB_DML_INPUT * input)
{
  const char *data = NULL;
  char marker[PATH_MAX + 64];
  char type_char;
  long long data_length;
  int size = 0;
  const char *locator;
  size_t locator_length;

  if (!db_get_internal_lob_marker_text (value, DB_VALUE_INTERNAL_LOB_MARKER_PENDING, NULL, &data, &size)
      || !internal_lob_marker_parse_pending (data, size, marker, (int) sizeof (marker), &type_char, &data_length,
					     &locator))
    {
      return false;
    }

  if (data_length < 0 || data_length > DB_MAX_INTERNAL_LOB_LENGTH
      || (type_char == 'C' && input->type != DB_TYPE_CLOB) || (type_char == 'B' && input->type != DB_TYPE_BLOB))
    {
      return false;
    }

  locator_length = strlen (locator);
  if (locator_length == 0 || locator_length >= sizeof (input->locator))
    {
      return false;
    }

  memcpy (input->locator, locator, locator_length + 1);
  input->data_length = (DB_BIGINT) data_length;
  input->logical_length = input->type == DB_TYPE_BLOB ? input->data_length * 8 : input->data_length;
  return true;
}

static int
query_initialize_internal_lob_dml_input (const DB_VALUE * value, INTERNAL_LOB_DML_INPUT * input)
{
  int marker;

  memset (input, 0, sizeof (*input));
  input->type = DB_VALUE_TYPE (value);
  marker = db_value_get_internal_lob_marker (value);

  if (marker == DB_VALUE_INTERNAL_LOB_MARKER_NONE)
    {
      input->kind = INTERNAL_LOB_DML_INPUT_MEMORY;
      if (input->type == DB_TYPE_BLOB)
	{
	  int bit_length = 0;

	  input->data = (const char *) db_get_bit (value, &bit_length);
	  input->logical_length = bit_length;
	  input->data_length = (bit_length + 7LL) / 8LL;
	}
      else
	{
	  input->data = db_get_string (value);
	  input->data_length = db_get_string_size (value);
	  input->logical_length = input->data_length;
	}

      if (input->data_length < 0 || input->data_length > DB_MAX_INTERNAL_LOB_LENGTH
	  || input->logical_length < 0 || (input->data_length > 0 && input->data == NULL))
	{
	  return stream_session_set_error ("invalid materialized internal LOB DML payload");
	}
      return NO_ERROR;
    }

  input->kind = INTERNAL_LOB_DML_INPUT_ELO;
  if ((marker == DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE
       && !query_parse_internal_lob_file_source (value, input))
      || (marker == DB_VALUE_INTERNAL_LOB_MARKER_PENDING && !query_parse_internal_lob_pending_source (value, input)))
    {
      return stream_session_set_error ("invalid external internal LOB DML source marker");
    }

  input->elo.size = -1;
  input->elo.locator = input->locator;
  input->elo.meta_data = NULL;
  input->elo.type = ELO_FBO;
  input->elo.es_type = 0;
  {
    DB_BIGINT current_size = db_elo_size (&input->elo);

    if (current_size < 0)
      {
	return er_errid () == NO_ERROR ? ER_FAILED : er_errid ();
      }
    if (current_size != input->data_length)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "LOB", "external LOB source size changed");
	return ER_ES_GENERAL;
      }
  }
  return NO_ERROR;
}

static int
query_read_internal_lob_dml_input (const INTERNAL_LOB_DML_INPUT * input, DB_BIGINT offset, char *buffer,
				   int request_size, const char **data_out)
{
  if (input->kind == INTERNAL_LOB_DML_INPUT_MEMORY)
    {
      *data_out = input->data + offset;
      return NO_ERROR;
    }

  DB_BIGINT read_bytes = 0;
  int error = db_elo_read (&input->elo, (off_t) offset, buffer, (size_t) request_size, &read_bytes);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (read_bytes != request_size)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_GENERAL, 2, "LOB", "unexpected end of external LOB source");
      return ER_ES_GENERAL;
    }

  *data_out = buffer;
  return NO_ERROR;
}

/*
 * query_free_internal_lob_dml_inputs () - Free what query_prepare_internal_lob_dml_inputs () built.
 */
static void
query_free_internal_lob_dml_inputs (int var_cnt, DB_VALUE * stream_values, INTERNAL_LOB_DML_INPUT * inputs,
				    internal_lob_dml_slot_config * slot_configs)
{
  if (stream_values != NULL)
    {
      for (int i = 0; i < var_cnt; i++)
	{
	  db_value_clear (&stream_values[i]);
	}
      free_and_init (stream_values);
    }
  free_and_init (inputs);
  free_and_init (slot_configs);
}

/* A slot no DML stream has: a skipped value that is used after all fails instead of being stored as NULL. */
#define QUERY_INTERNAL_LOB_DML_UNUSED_SLOT INT_MAX

/*
 * query_prepare_internal_lob_dml_inputs () - Turn the statement's Internal LOB parameters into stream slots.
 *   used_vars(in): flags of the host variables the statement uses, or NULL for all.  A LOB it does not use (one of an
 *                  earlier statement of the same buffer) is replaced by a slot marker no stream has, not sent.
 */
static int
query_prepare_internal_lob_dml_inputs (int var_cnt, const DB_VALUE * varptr, const bool * used_vars,
				       DB_VALUE ** stream_values_out, INTERNAL_LOB_DML_INPUT ** inputs_out,
				       internal_lob_dml_slot_config ** slot_configs_out, int *slot_count_out,
				       const OID * direct_class_oid)
{
  DB_VALUE *stream_values = NULL;
  INTERNAL_LOB_DML_INPUT *inputs = NULL;
  internal_lob_dml_slot_config *slot_configs = NULL;
  int slot_count = 0;
  int unused_count = 0;
  int error = NO_ERROR;

  *stream_values_out = NULL;
  *inputs_out = NULL;
  *slot_configs_out = NULL;
  *slot_count_out = 0;

  for (int i = 0; i < var_cnt; i++)
    {
      if (query_is_internal_lob_dml_source (&varptr[i]))
	{
	  if (used_vars == NULL || used_vars[i])
	    {
	      slot_count++;
	    }
	  else
	    {
	      unused_count++;
	    }
	}
    }

  if (slot_count == 0 && unused_count == 0)
    {
      return NO_ERROR;
    }
  if (slot_count > 1024)
    {
      return stream_session_set_error ("too many internal LOB DML payload slots");
    }

  stream_values = (DB_VALUE *) malloc ((size_t) var_cnt * sizeof (DB_VALUE));
  inputs = (INTERNAL_LOB_DML_INPUT *) malloc ((size_t) MAX (slot_count, 1) * sizeof (INTERNAL_LOB_DML_INPUT));
  slot_configs =
    (internal_lob_dml_slot_config *) malloc ((size_t) MAX (slot_count, 1) * sizeof (internal_lob_dml_slot_config));
  if (stream_values == NULL || inputs == NULL || slot_configs == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (size_t) var_cnt * sizeof (DB_VALUE) + (size_t) slot_count
	      * (sizeof (INTERNAL_LOB_DML_INPUT) + sizeof (internal_lob_dml_slot_config)));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  for (int i = 0; i < var_cnt; i++)
    {
      db_make_null (&stream_values[i]);
    }

  for (int i = 0, slot = 0; i < var_cnt; i++)
    {
      if (!query_is_internal_lob_dml_source (&varptr[i]))
	{
	  error = db_value_clone ((DB_VALUE *) & varptr[i], &stream_values[i]);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	  continue;
	}
      if (used_vars != NULL && !used_vars[i])
	{
	  error = internal_lob_dml_make_slot_value (&stream_values[i], DB_VALUE_TYPE (&varptr[i]),
						    QUERY_INTERNAL_LOB_DML_UNUSED_SLOT);
	  if (error != NO_ERROR)
	    {
	      goto exit;
	    }
	  continue;
	}

      error = query_initialize_internal_lob_dml_input (&varptr[i], &inputs[slot]);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      slot_configs[slot].type = inputs[slot].type;
      slot_configs[slot].data_length = inputs[slot].data_length;
      slot_configs[slot].logical_length = inputs[slot].logical_length;
      if (direct_class_oid != NULL && !OID_ISNULL (direct_class_oid))
	{
	  slot_configs[slot].flags = INTERNAL_LOB_DML_SLOT_FLAG_DIRECT_REVERSE;
	  slot_configs[slot].class_oid = *direct_class_oid;
	}
      else
	{
	  slot_configs[slot].flags = 0;
	  OID_SET_NULL (&slot_configs[slot].class_oid);
	}
      error = internal_lob_dml_make_slot_value (&stream_values[i], inputs[slot].type, slot);
      if (error != NO_ERROR)
	{
	  goto exit;
	}
      slot++;
    }

  *stream_values_out = stream_values;
  *inputs_out = inputs;
  *slot_configs_out = slot_configs;
  *slot_count_out = slot_count;
  return NO_ERROR;

exit:
  query_free_internal_lob_dml_inputs (var_cnt, stream_values, inputs, slot_configs);
  return error;
}

static int
query_send_internal_lob_dml_inputs (const INTERNAL_LOB_DML_INPUT * inputs,
				    const internal_lob_dml_slot_config * slot_configs, int slot_count)
{
  const int chunk_size = 1024 * 1024;
  char *chunk_buffer;
  int error = NO_ERROR;

  chunk_buffer = (char *) malloc (chunk_size);
  if (chunk_buffer == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, chunk_size);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (int slot = 0; slot < slot_count && error == NO_ERROR; slot++)
    {
      bool reverse = (slot_configs[slot].flags & INTERNAL_LOB_DML_SLOT_FLAG_DIRECT_REVERSE) != 0;
      DB_BIGINT cursor = reverse ? inputs[slot].data_length : 0;

      while ((reverse && cursor > 0) || (!reverse && cursor < inputs[slot].data_length))
	{
	  DB_BIGINT remaining = reverse ? cursor : inputs[slot].data_length - cursor;
	  int send_size = remaining < chunk_size ? (int) remaining : chunk_size;
	  DB_BIGINT offset = reverse ? cursor - send_size : cursor;
	  const char *send_data = NULL;

	  error = query_read_internal_lob_dml_input (&inputs[slot], offset, chunk_buffer, send_size, &send_data);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	  error = internal_lob_dml_from_send_data (slot, offset, send_data, send_size);
	  if (error != NO_ERROR)
	    {
	      /* The server drops the stream session on the first failed chunk, so every later send would only
	       * waste bandwidth and replace this error with "no active stream session". */
	      break;
	    }
	  cursor = reverse ? offset : cursor + send_size;
	}
    }

  free_and_init (chunk_buffer);
  return error;
}
#endif /* CS_MODE */

/*
 * check_client_internal_lob_dml_params () - Refuse Internal LOB parameters on a client-owned DML path.
 */
int
check_client_internal_lob_dml_params (int var_cnt, const DB_VALUE * varptr, const bool * used_vars)
{
#if defined(CS_MODE)
  DB_VALUE *stream_values = NULL;
  INTERNAL_LOB_DML_INPUT *inputs = NULL;
  internal_lob_dml_slot_config *slot_configs = NULL;
  int slot_count = 0;
  int error;

  if (var_cnt <= 0 || varptr == NULL)
    {
      return NO_ERROR;
    }

  error = query_prepare_internal_lob_dml_inputs (var_cnt, varptr, used_vars, &stream_values, &inputs, &slot_configs,
						 &slot_count, NULL);
  if (error == NO_ERROR && slot_count > 0)
    {
      /* The client object path (trigger, view or object-level DML) assigns through obj_assign_value (), which
       * refuses an unresolved Internal LOB envelope, so fail here before any payload is sent. */
      error = stream_session_set_error ("Internal LOB payload cannot be written through the client object path; a "
					"trigger, a view, or object-level DML forces that path");
    }

  query_free_internal_lob_dml_inputs (var_cnt, stream_values, inputs, slot_configs);
  return error;
#else /* CS_MODE */
  return NO_ERROR;
#endif /* !CS_MODE */
}

/*
 * execute_query_with_internal_lob_dml () - Execute INSERT/UPDATE with materialized Internal LOB parameters through
 *                                          the COPY-style, DML-owning stream session.
 *
 * Each BLOB/CLOB parameter becomes a typed slot marker and its bytes go as bounded slot frames; STREAM_END executes
 * the prepared XASL and returns the affected-row count.  Non-LOB statements and SA mode use plain execute_query.
 */
int
execute_query_with_internal_lob_dml (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt,
				     const DB_VALUE * varptr, QFILE_LIST_ID ** list_idp, QUERY_FLAG flag,
				     CACHE_TIME * clt_cache_time, CACHE_TIME * srv_cache_time,
				     const OID * direct_class_oid, const bool * used_vars)
{
#if defined(CS_MODE)
  DB_VALUE *stream_values = NULL;
  INTERNAL_LOB_DML_INPUT *inputs = NULL;
  internal_lob_dml_slot_config *slot_configs = NULL;
  QFILE_LIST_ID *result_list = NULL;
  int slot_count = 0;
  QUERY_FLAG stream_flag;
  CACHE_TIME local_cache_time;
  INT64 affected_rows = 0;
  bool execute_with_commit;
  int query_timeout;
  int error;

  *list_idp = NULL;
  if (query_idp != NULL)
    {
      *query_idp = NULL_QUERY_ID;
    }

  if (var_cnt <= 0 || varptr == NULL)
    {
      return execute_query (xasl_id, query_idp, var_cnt, varptr, list_idp, flag, clt_cache_time, srv_cache_time);
    }

  if ((flag & RETURN_GENERATED_KEYS) != 0)
    {
      /* The generated-keys path bypasses the DML-owning stream session. A direct Internal LOB file source
       * would then be shipped to the server as a FILE_SOURCE marker and opened with the server process's own
       * credentials (arbitrary server-side file read). Reject rather than silently fall back. */
      for (int i = 0; i < var_cnt; i++)
	{
	  if ((used_vars == NULL || used_vars[i]) && query_is_internal_lob_dml_source (&varptr[i]))
	    {
	      return stream_session_set_error ("generated keys are not supported with direct Internal LOB DML sources");
	    }
	}
      return execute_query (xasl_id, query_idp, var_cnt, varptr, list_idp, flag, clt_cache_time, srv_cache_time);
    }

  if (qo_need_skip_execution ())
    {
      return NO_ERROR;
    }

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      cdc_Trigger_involved = false;
    }

  error = query_prepare_internal_lob_dml_inputs (var_cnt, varptr, used_vars, &stream_values, &inputs, &slot_configs,
						 &slot_count, direct_class_oid);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (slot_count == 0)
    {
      /* stream_values, when built, holds only skipped LOBs of other statements */
      error = execute_query (xasl_id, query_idp, var_cnt, stream_values != NULL ? stream_values : varptr, list_idp,
			     flag, clt_cache_time, srv_cache_time);
      query_free_internal_lob_dml_inputs (var_cnt, stream_values, inputs, slot_configs);
      return error;
    }

  result_list = (QFILE_LIST_ID *) malloc (sizeof (QFILE_LIST_ID));
  if (result_list == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (QFILE_LIST_ID));
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }
  QFILE_CLEAR_LIST_ID (result_list);

  execute_with_commit = IS_QUERY_EXECUTE_WITH_COMMIT (flag);
  stream_flag = flag & ~(EXECUTE_QUERY_WITH_COMMIT | TRAN_AUTO_COMMIT | EXECUTE_QUERY_WITHOUT_DATA_BUFFERS);
  if (clt_cache_time == NULL)
    {
      CACHE_TIME_RESET (&local_cache_time);
      clt_cache_time = &local_cache_time;
    }
  if (srv_cache_time != NULL)
    {
      CACHE_TIME_RESET (srv_cache_time);
    }
  query_timeout = tran_get_query_timeout ();

  error = internal_lob_dml_from_init (xasl_id, var_cnt, stream_values, stream_flag, clt_cache_time, query_timeout,
				      slot_configs, slot_count);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = query_send_internal_lob_dml_inputs (inputs, slot_configs, slot_count);
  if (error == NO_ERROR)
    {
      error = stream_from_end (&affected_rows);
    }
  if (error != NO_ERROR)
    {
      int stream_error = error;

      er_stack_push ();
      (void) stream_from_abort ();
      er_stack_pop ();
      if (execute_with_commit)
	{
	  (void) tran_abort ();
	}
      error = stream_error;
      goto exit;
    }

  if (execute_with_commit)
    {
      error = tran_commit (false);
      if (error != NO_ERROR)
	{
	  int commit_error = error;
	  (void) tran_abort ();
	  error = commit_error;
	  goto exit;
	}
      tran_set_latest_query_status (NO_ERROR, TRAN_UNACTIVE_COMMITTED, false);
    }

  result_list->tuple_cnt = affected_rows;
  *list_idp = result_list;
  result_list = NULL;

exit:
  query_free_internal_lob_dml_inputs (var_cnt, stream_values, inputs, slot_configs);
  free_and_init (result_list);
  return error;
#else /* CS_MODE */
  return execute_query (xasl_id, query_idp, var_cnt, varptr, list_idp, flag, clt_cache_time, srv_cache_time);
#endif /* !CS_MODE */
}

/*
 * prepare_and_execute_query () -
 *   return:
 *   stream(in) : packed XASL tree
 *   stream_size(in)   : size of stream
 *   query_id(in)       :
 *   var_cnt(in)        : number of input values for positional variables
 *   varptr(in) : pointer to the array of input values
 *   result(out): pointer to result list id pointer
 *   flag(in)   : flag
 *
 * Note: Prepares and executes a query, and the result is returned
 *       through a list id (actually the list file).
 *       For csql, var_cnt must be 0 and varptr be NULL.
 *       It is the caller's responsibility to free result QFILE_LIST_ID by
 *       calling regu_free_listid.
 */
int
prepare_and_execute_query (char *stream, int stream_size, QUERY_ID * query_id, int var_cnt, DB_VALUE * varptr,
			   QFILE_LIST_ID ** result, QUERY_FLAG flag)
{
  QFILE_LIST_ID *list_idptr;
  int query_timeout;
  int ret = NO_ERROR;

  if (qo_need_skip_execution ())
    {
      *result = NULL;

      return NO_ERROR;
    }

  if (do_Trigger_involved && prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      cdc_Trigger_involved = true;
      flag |= TRIGGER_IS_INVOLVED;
    }
  else
    {
      cdc_Trigger_involved = false;
    }

  query_timeout = tran_get_query_timeout ();
  list_idptr = qmgr_prepare_and_execute_query (stream, stream_size, query_id, var_cnt, varptr, flag, query_timeout);
  if (list_idptr == NULL)
    {
      return ((ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
    }

  *result = list_idptr;

  assert (*result != NULL);
  assert (ret == NO_ERROR);

  return ret;
}
