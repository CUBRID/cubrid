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
  bool delete_after_read;
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
query_get_internal_lob_marker_data (const DB_VALUE * value, const char **data_out, int *size_out)
{
  DB_TYPE type;

  assert (value != NULL && data_out != NULL && size_out != NULL);

  type = DB_VALUE_TYPE (value);
  if (type == DB_TYPE_CLOB)
    {
      *data_out = db_get_string (value);
      *size_out = db_get_string_size (value);
      return *data_out != NULL && *size_out > 0;
    }
  if (type == DB_TYPE_BLOB)
    {
      int bit_length = 0;

      *data_out = (const char *) db_get_bit (value, &bit_length);
      if (*data_out == NULL || bit_length <= 0 || bit_length % 8 != 0)
	{
	  return false;
	}
      *size_out = bit_length / 8;
      return true;
    }
  return false;
}

static bool
query_parse_internal_lob_file_source (const DB_VALUE * value, INTERNAL_LOB_DML_INPUT * input)
{
  const char *data = NULL;
  const int prefix_length = (int) strlen (INTERNAL_LOB_FILE_SOURCE_PREFIX);
  char marker[PATH_MAX + 128];
  char *p;
  char *endptr;
  long long data_length;
  long long logical_length;
  int size = 0;
  size_t locator_length;

  if (!query_get_internal_lob_marker_data (value, &data, &size) || size <= prefix_length
      || size >= (int) sizeof (marker) || memcmp (data, INTERNAL_LOB_FILE_SOURCE_PREFIX, prefix_length) != 0)
    {
      return false;
    }

  memcpy (marker, data, size);
  marker[size] = '\0';
  p = marker + prefix_length;
  if ((p[0] == 'C' && input->type != DB_TYPE_CLOB) || (p[0] == 'B' && input->type != DB_TYPE_BLOB)
      || (p[0] != 'C' && p[0] != 'B') || p[1] != ':')
    {
      return false;
    }
  p += 2;

  data_length = strtoll (p, &endptr, 10);
  if (endptr == p || *endptr != ':' || data_length < 0 || data_length > DB_MAX_INTERNAL_LOB_LENGTH)
    {
      return false;
    }
  p = endptr + 1;

  logical_length = strtoll (p, &endptr, 10);
  if (endptr == p || *endptr != ':')
    {
      return false;
    }
  p = endptr + 1;
  if ((input->type == DB_TYPE_CLOB && logical_length != -1)
      || (input->type == DB_TYPE_BLOB
	  && (logical_length < 0 || data_length > DB_BIGINT_MAX / 8 || logical_length != data_length * 8)))
    {
      return false;
    }

  locator_length = strlen (p);
  if (locator_length == 0 || locator_length >= sizeof (input->locator))
    {
      return false;
    }

  memcpy (input->locator, p, locator_length + 1);
  input->data_length = (DB_BIGINT) data_length;
  input->logical_length = input->type == DB_TYPE_BLOB ? (DB_BIGINT) logical_length : (DB_BIGINT) data_length;
  return true;
}

static bool
query_parse_internal_lob_pending_source (const DB_VALUE * value, INTERNAL_LOB_DML_INPUT * input)
{
  const char *data = NULL;
  const int prefix_length = (int) strlen (INTERNAL_LOB_PENDING_PREFIX);
  char marker[PATH_MAX + 64];
  char type_char;
  long long data_length;
  int delete_after_read;
  int locator_offset = 0;
  int size = 0;
  const char *locator;
  size_t locator_length;

  if (!query_get_internal_lob_marker_data (value, &data, &size) || size <= prefix_length
      || size >= (int) sizeof (marker) || memcmp (data, INTERNAL_LOB_PENDING_PREFIX, prefix_length) != 0)
    {
      return false;
    }

  memcpy (marker, data, size);
  marker[size] = '\0';
  if (sscanf (marker + prefix_length, "%c:%lld:%d:%n", &type_char, &data_length, &delete_after_read,
	      &locator_offset) < 3
      || locator_offset <= 0 || data_length < 0 || data_length > DB_MAX_INTERNAL_LOB_LENGTH
      || (delete_after_read != 0 && delete_after_read != 1)
      || (type_char == 'C' && input->type != DB_TYPE_CLOB) || (type_char == 'B' && input->type != DB_TYPE_BLOB)
      || (type_char != 'C' && type_char != 'B'))
    {
      return false;
    }

  locator = marker + prefix_length + locator_offset;
  locator_length = strlen (locator);
  if (locator_length == 0 || locator_length >= sizeof (input->locator))
    {
      return false;
    }

  memcpy (input->locator, locator, locator_length + 1);
  input->data_length = (DB_BIGINT) data_length;
  input->logical_length = input->type == DB_TYPE_BLOB ? input->data_length * 8 : input->data_length;
  input->delete_after_read = delete_after_read != 0;
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1,
		  "invalid materialized internal LOB DML payload");
	  return ER_STREAM_SESSION_ERROR;
	}
      return NO_ERROR;
    }

  input->kind = INTERNAL_LOB_DML_INPUT_ELO;
  if ((marker == DB_VALUE_INTERNAL_LOB_MARKER_FILE_SOURCE
       && !query_parse_internal_lob_file_source (value, input))
      || (marker == DB_VALUE_INTERNAL_LOB_MARKER_PENDING && !query_parse_internal_lob_pending_source (value, input)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1,
	      "invalid external internal LOB DML source marker");
      return ER_STREAM_SESSION_ERROR;
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

static int
query_prepare_internal_lob_dml_inputs (int var_cnt, const DB_VALUE * varptr, DB_VALUE ** stream_values_out,
				       INTERNAL_LOB_DML_INPUT ** inputs_out,
				       internal_lob_dml_slot_config ** slot_configs_out, int *slot_count_out,
				       const OID * direct_class_oid)
{
  DB_VALUE *stream_values = NULL;
  INTERNAL_LOB_DML_INPUT *inputs = NULL;
  internal_lob_dml_slot_config *slot_configs = NULL;
  int slot_count = 0;
  int error = NO_ERROR;

  *stream_values_out = NULL;
  *inputs_out = NULL;
  *slot_configs_out = NULL;
  *slot_count_out = 0;

  for (int i = 0; i < var_cnt; i++)
    {
      if (query_is_internal_lob_dml_source (&varptr[i]))
	{
	  slot_count++;
	}
    }

  if (slot_count == 0)
    {
      return NO_ERROR;
    }
  if (slot_count > 1024)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, "too many internal LOB DML payload slots");
      return ER_STREAM_SESSION_ERROR;
    }

  stream_values = (DB_VALUE *) malloc ((size_t) var_cnt * sizeof (DB_VALUE));
  inputs = (INTERNAL_LOB_DML_INPUT *) malloc ((size_t) slot_count * sizeof (INTERNAL_LOB_DML_INPUT));
  slot_configs = (internal_lob_dml_slot_config *) malloc ((size_t) slot_count * sizeof (internal_lob_dml_slot_config));
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
  return error;
}

static void
query_clear_internal_lob_dml_stream_values (int var_cnt, DB_VALUE * stream_values)
{
  if (stream_values == NULL)
    {
      return;
    }

  for (int i = 0; i < var_cnt; i++)
    {
      db_value_clear (&stream_values[i]);
    }
  free_and_init (stream_values);
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
 * begin_client_internal_lob_dml_stream () - Stage CLOB/BLOB parameters for a client-owned DML path.
 *
 * Trigger and workspace DML cannot be executed by the prepared XASL owned by STREAM_END.  Keep the same COPY-style
 * payload session active while the legacy locator force performs the heap change instead.  Heap Internal LOB handling
 * consumes the same typed slot values from the active session, so no upload token or detached payload is introduced.
 */
int
begin_client_internal_lob_dml_stream (int var_cnt, const DB_VALUE * varptr, DB_VALUE ** stream_values_out,
				      const OID * direct_class_oid)
{
#if defined(CS_MODE)
  DB_VALUE *stream_values = NULL;
  INTERNAL_LOB_DML_INPUT *inputs = NULL;
  internal_lob_dml_slot_config *slot_configs = NULL;
  XASL_ID null_xasl_id;
  CACHE_TIME cache_time;
  int slot_count = 0;
  int error;
  bool stream_open = false;

  if (stream_values_out == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }
  *stream_values_out = NULL;
  if (var_cnt <= 0 || varptr == NULL)
    {
      return NO_ERROR;
    }

  error = query_prepare_internal_lob_dml_inputs (var_cnt, varptr, &stream_values, &inputs, &slot_configs,
						 &slot_count, direct_class_oid);
  if (error != NO_ERROR || slot_count == 0)
    {
      return error;
    }

  XASL_ID_SET_NULL (&null_xasl_id);
  CACHE_TIME_RESET (&cache_time);
  error = internal_lob_dml_from_init (&null_xasl_id, 0, NULL, (QUERY_FLAG) 0, &cache_time, 0, slot_configs, slot_count);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  stream_open = true;

  error = query_send_internal_lob_dml_inputs (inputs, slot_configs, slot_count);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  *stream_values_out = stream_values;
  stream_values = NULL;

exit:
  if (error != NO_ERROR && stream_open)
    {
      er_stack_push ();
      (void) stream_from_abort ();
      er_stack_pop ();
    }
  query_clear_internal_lob_dml_stream_values (var_cnt, stream_values);
  free_and_init (inputs);
  free_and_init (slot_configs);
  return error;
#else /* CS_MODE */
  *stream_values_out = NULL;
  return NO_ERROR;
#endif /* !CS_MODE */
}

int
end_client_internal_lob_dml_stream (int var_cnt, DB_VALUE * stream_values)
{
#if defined(CS_MODE)
  INT64 ignored_count = 0;
  int error = NO_ERROR;

  if (stream_values != NULL)
    {
      error = stream_from_end (&ignored_count);
    }
  query_clear_internal_lob_dml_stream_values (var_cnt, stream_values);
  return error;
#else /* CS_MODE */
  return NO_ERROR;
#endif /* !CS_MODE */
}

void
abort_client_internal_lob_dml_stream (int var_cnt, DB_VALUE * stream_values)
{
#if defined(CS_MODE)
  if (stream_values != NULL)
    {
      (void) stream_from_abort ();
    }
  query_clear_internal_lob_dml_stream_values (var_cnt, stream_values);
#endif /* CS_MODE */
}

/*
 * execute_query_with_internal_lob_dml () - Execute INSERT/UPDATE with materialized Internal LOB parameters through
 *                                          the COPY-style, DML-owning stream session.
 *
 * Non-LOB statements and standalone mode retain the ordinary execute_query path. In client/server mode every
 * materialized BLOB/CLOB parameter is replaced with a typed slot marker in the packed query parameters. The original
 * bytes are then sent as bounded slot frames. STREAM_END executes the prepared XASL in the same active server session
 * and returns the affected-row count; no upload token and no second DML request are used.
 */
int
execute_query_with_internal_lob_dml (const XASL_ID * xasl_id, QUERY_ID * query_idp, int var_cnt,
				     const DB_VALUE * varptr, QFILE_LIST_ID ** list_idp, QUERY_FLAG flag,
				     CACHE_TIME * clt_cache_time, CACHE_TIME * srv_cache_time,
				     const OID * direct_class_oid)
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
  bool stream_open = false;
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
	  if (query_is_internal_lob_dml_source (&varptr[i]))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1,
		      "generated keys are not supported with direct Internal LOB DML sources");
	      return ER_STREAM_SESSION_ERROR;
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

  error = query_prepare_internal_lob_dml_inputs (var_cnt, varptr, &stream_values, &inputs, &slot_configs,
						 &slot_count, direct_class_oid);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (slot_count == 0)
    {
      return execute_query (xasl_id, query_idp, var_cnt, varptr, list_idp, flag, clt_cache_time, srv_cache_time);
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
  stream_open = true;

  error = query_send_internal_lob_dml_inputs (inputs, slot_configs, slot_count);

  if (error == NO_ERROR)
    {
      error = stream_from_end (&affected_rows);
      if (error == NO_ERROR)
	{
	  stream_open = false;
	}
    }
  if (error != NO_ERROR)
    {
      int stream_error = error;

      if (stream_open)
	{
	  er_stack_push ();
	  (void) stream_from_abort ();
	  er_stack_pop ();
	  stream_open = false;
	}
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
  if (stream_open)
    {
      (void) stream_from_abort ();
    }
  query_clear_internal_lob_dml_stream_values (var_cnt, stream_values);
  free_and_init (inputs);
  free_and_init (slot_configs);
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
