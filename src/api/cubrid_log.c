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
 * cubrid_log.c -
 */

#ident "$Id$"

#if defined(CS_MODE)

#include <arpa/inet.h>
#include <limits.h>

#include "connection_cl.h"
#include "connection_list_cl.h"
#include "cubrid_log.h"
#include "log_lsa.hpp"
#include "network.h"
#include "object_representation.h"

#define CUBRID_LOG_ERROR_HANDLING(v) (err_code) = (v); goto cubrid_log_error

typedef enum
{
  CUBRID_LOG_STAGE_CONFIGURATION,
  CUBRID_LOG_STAGE_PREPARATION,
  CUBRID_LOG_STAGE_EXTRACTION,
  CUBRID_LOG_STAGE_FINALIZATION
} CUBRID_LOG_STAGE;

typedef enum
{
  CREATE = 0,
  ALTER,
  DROP,
  RENAME,
  TRUNCATE
} DDL_TYPE;

typedef enum
{
  INSERT = 0,
  UPDATE,
  DELETE
} DML_TYPE;

typedef enum
{
  COMMIT = 0,
  ROLLBACK
} DCL_TYPE;

typedef enum
{
  TABLE = 0,
  INDEX,
  SERIAL,
  VIEW,
  FUNCTION,
  PROCEDURE,
  TRIGGER
} OBJ_TYPE;

typedef enum
{
  DATA_ITEM_TYPE_DDL = 0,
  DATA_ITEM_TYPE_DML,
  DATA_ITEM_TYPE_DCL,
  DATA_ITEM_TYPE_TIMER
} DATA_ITEM_TYPE;

CUBRID_LOG_STAGE g_stage = CUBRID_LOG_STAGE_CONFIGURATION;

CSS_CONN_ENTRY *g_conn_entry;

int g_connection_timeout = 300;	/* min/max: -1/360 (sec) */
int g_extraction_timeout = 300;	/* min/max: -1/360 (sec) */
int g_max_log_item = 512;	/* min/max: 1/1024 */
bool g_all_in_cond = false;

uint64_t *g_extraction_table;
int g_extraction_table_count = 0;

char **g_extraction_user;
int g_extraction_user_count = 0;

FILE *g_trace_log;
char g_trace_log_path[PATH_MAX + 1] = "./";
int g_trace_log_level = 0;
int g_trace_log_filesize = 10;	/* MB */

LOG_LSA g_next_lsa = LSA_INITIALIZER;

/*
 * cubrid_log_set_connection_timeout () -
 *   return:
 *   timeout(in):
 */
int
cubrid_log_set_connection_timeout (int timeout)
{
  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (timeout < -1 || timeout > 360)
    {
      return CUBRID_LOG_INVALID_CONNECTION_TIMEOUT;
    }

  g_connection_timeout = timeout;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_set_extraction_timeout () -
 *   return:
 *   timeout(in):
 */
int
cubrid_log_set_extraction_timeout (int timeout)
{
  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (timeout < -1 || timeout > 360)
    {
      return CUBRID_LOG_INVALID_EXTRACTION_TIMEOUT;
    }

  g_extraction_timeout = timeout;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_set_tracelog () -
 *   return:
 *   path(in):
 *   level(in):
 *   filesize(in):
 */
int
cubrid_log_set_tracelog (char *path, int level, int filesize)
{
  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (strlen (path) > PATH_MAX)
    {
      return CUBRID_LOG_INVALID_PATH;
    }

  if (level < 0 || level > 2)
    {
      return CUBRID_LOG_INVALID_LEVEL;
    }

  if (filesize < 8 || filesize > 512)
    {
      if (filesize == -1)
	{
	  filesize = 10;
	}
      else
	{
	  return CUBRID_LOG_INVALID_FILESIZE;
	}
    }

  snprintf (g_trace_log_path, PATH_MAX + 1, "%s", path);
  g_trace_log_level = level;
  g_trace_log_filesize = filesize;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_set_max_log_item () -
 *   return:
 *   max_log_item(in):
 */
int
cubrid_log_set_max_log_item (int max_log_item)
{
  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (max_log_item < 1 || max_log_item > 1024)
    {
      return CUBRID_LOG_INVALID_MAX_LOG_ITEM;
    }

  g_max_log_item = max_log_item;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_set_all_in_cond () -
 *   return:
 *   retrieve_all(in):
 */
int
cubrid_log_set_all_in_cond (int retrieve_all)
{
  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (retrieve_all < 0 || retrieve_all > 1)
    {
      return CUBRID_LOG_INVALID_RETRIEVE_ALL;
    }

  g_all_in_cond = retrieve_all;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_set_extraction_table () -
 *   return:
 *   classoid_arr(in):
 *   arr_size(in):
 */
int
cubrid_log_set_extraction_table (uint64_t * classoid_arr, int arr_size)
{
  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if ((classoid_arr == NULL && arr_size != 0) || (classoid_arr != NULL && arr_size == 0))
    {
      return CUBRID_LOG_INVALID_CLASSOID_ARR_SIZE;
    }

  g_extraction_table = (uint64_t *) malloc (sizeof (uint64_t) * arr_size);
  if (g_extraction_table == NULL)
    {
      return CUBRID_LOG_FAILED_MALLOC;
    }

  memcpy (g_extraction_table, classoid_arr, arr_size);
  g_extraction_table_count = arr_size;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_set_extraction_user () -
 *   return:
 *   user_arr(in):
 *   arr_size(in):
 */
int
cubrid_log_set_extraction_user (char **user_arr, int arr_size)
{
  int i;

  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if ((user_arr == NULL && arr_size != 0) || (user_arr != NULL && arr_size == 0))
    {
      return CUBRID_LOG_INVALID_USER_ARR_SIZE;
    }

  g_extraction_user = (char **) malloc (sizeof (char *) * arr_size);
  if (g_extraction_user == NULL)
    {
      return CUBRID_LOG_FAILED_MALLOC;
    }

  for (i = 0; i < arr_size; i++)
    {
      g_extraction_user[i] = strdup (user_arr[i]);
    }

  g_extraction_user_count = arr_size;

  return CUBRID_LOG_SUCCESS;
}

static int
cubrid_log_connect_server_internal (char *host, int port, char *dbname)
{
  unsigned short rid = 0;

  char *recv_data = NULL;
  int recv_data_size;

  int reason;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  g_conn_entry = css_make_conn (INVALID_SOCKET);
  if (g_conn_entry == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  if (css_common_connect
      (host, g_conn_entry, DATA_REQUEST, dbname, strlen (dbname) + 1, port, g_connection_timeout, &rid, true) == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  css_queue_user_data_buffer (g_conn_entry, rid, sizeof (int), (char *) &reason);

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  if (recv_data == NULL || recv_data_size != sizeof (int))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  reason = ntohl (*(int *) recv_data);

  if (reason != SERVER_CONNECTED)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  if (recv_data != NULL && recv_data != (char *) &reason)
    {
      free_and_init (recv_data);
    }

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  if (recv_data != NULL && recv_data != (char *) &reason)
    {
      free_and_init (recv_data);
    }

  queue_entry = css_find_queue_entry (g_conn_entry->buffer_queue, rid);
  if (queue_entry != NULL)
    {
      queue_entry->buffer = NULL;
      css_queue_remove_header_entry_ptr (&g_conn_entry->buffer_queue, queue_entry);
    }

  if (g_conn_entry != NULL)
    {
      css_free_conn (g_conn_entry);
      g_conn_entry = NULL;
    }

  return err_code;
}

static int
cubrid_log_send_configurations (void)
{
  unsigned short rid = 0;

  OR_ALIGNED_BUF ((OR_INT_SIZE * 5)) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *request = OR_ALIGNED_BUF_START (a_request), *ptr;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int request_size = OR_ALIGNED_BUF_SIZE (a_request);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  ptr = or_pack_int (request, g_max_log_item);
  ptr = or_pack_int (ptr, g_extraction_timeout);
  ptr = or_pack_int (ptr, g_all_in_cond);
  ptr = or_pack_int (ptr, g_extraction_user_count);
  ptr = or_pack_int (ptr, g_extraction_table_count);

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_LOG_READER_SET_CONFIGURATION, &rid, request, request_size, reply,
       reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  if (recv_data == NULL || recv_data_size != sizeof (int))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  queue_entry = css_find_queue_entry (g_conn_entry->buffer_queue, rid);
  if (queue_entry != NULL)
    {
      queue_entry->buffer = NULL;
      css_queue_remove_header_entry_ptr (&g_conn_entry->buffer_queue, queue_entry);
    }

  return err_code;
}

/*
 * cubrid_log_connect_server () -
 *   return:
 *   host(in):
 *   port(in):
 *   dbname(in):
 */
int
cubrid_log_connect_server (char *host, int port, char *dbname)
{
  int err_code;

  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE);
    }

  if (host == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_HOST);
    }

  if (port < 0 || port > USHRT_MAX)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_PORT);
    }

  if (dbname == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_DBNAME);
    }

  if (cubrid_log_connect_server_internal (host, port, dbname) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  if (cubrid_log_send_configurations () != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT);
    }

  g_stage = CUBRID_LOG_STAGE_PREPARATION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_find_start_lsa (time_t timestamp, LOG_LSA * lsa)
{
  unsigned short rid = 0;

  OR_ALIGNED_BUF (OR_BIGINT_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *request = OR_ALIGNED_BUF_START (a_request), *ptr;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int request_size = OR_ALIGNED_BUF_SIZE (a_request);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  or_pack_int64 (request, (INT64) timestamp);

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_LOG_READER_GET_LSA, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND);
    }

  ptr = or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND);
    }

  or_unpack_log_lsa (ptr, lsa);

#if !defined (NDEBUG)
  printf ("start_time = %ld, start_lsa = %lld|%d\n", timestamp, LSA_AS_ARGS (lsa));
#endif

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  queue_entry = css_find_queue_entry (g_conn_entry->buffer_queue, rid);
  if (queue_entry != NULL)
    {
      queue_entry->buffer = NULL;
      css_queue_remove_header_entry_ptr (&g_conn_entry->buffer_queue, queue_entry);
    }

  return err_code;
}

/*
 * cubrid_log_find_lsa () -
 *   return:
 *   timestamp(in):
 *   lsa(out):
 */
int
cubrid_log_find_lsa (time_t timestamp, uint64_t * lsa)
{
  int err_code;

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE);
    }

  if (timestamp > time (NULL))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_TIMESTAMP);
    }

  if (lsa == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_OUT_PARAM);
    }

  if (LSA_ISNULL (&g_next_lsa))
    {
      if (cubrid_log_find_start_lsa (timestamp, &g_next_lsa) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND);
	}
    }

  memcpy (lsa, &g_next_lsa, sizeof (uint64_t));

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_extract_internal (LOG_LSA * next_lsa)
{
  unsigned short rid = 0;

  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE) a_reply;
  char *request = OR_ALIGNED_BUF_START (a_request), *ptr;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int request_size = OR_ALIGNED_BUF_SIZE (a_request);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  or_pack_log_lsa (request, next_lsa);

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_LOG_READER_GET_LOG_REFINED_INFO, &rid, request, request_size, reply,
       reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA);
    }

  ptr = or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA);
    }

#if !defined (NDEBUG)
  printf ("send_next_lsa = %lld|%d\n", LSA_AS_ARGS (next_lsa));
#endif

  or_unpack_log_lsa (ptr, next_lsa);

#if !defined (NDEBUG)
  printf ("recv_next_lsa = %lld|%d\n", LSA_AS_ARGS (next_lsa));
#endif

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  queue_entry = css_find_queue_entry (g_conn_entry->buffer_queue, rid);
  if (queue_entry != NULL)
    {
      queue_entry->buffer = NULL;
      css_queue_remove_header_entry_ptr (&g_conn_entry->buffer_queue, queue_entry);
    }

  return err_code;
}

/*
 * cubrid_log_extract () -
 *   return:
 *   lsa(in/out):
 *   log_item_list(out):
 *   list_size(out):
 */
int
cubrid_log_extract (uint64_t * lsa, CUBRID_LOG_ITEM ** log_item_list, int *list_size)
{
  int err_code;

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION && g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE);
    }

  if (lsa == NULL || log_item_list == NULL || list_size == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_OUT_PARAM);
    }

  memcpy (&g_next_lsa, lsa, sizeof (LOG_LSA));

  if (cubrid_log_extract_internal (&g_next_lsa) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA);
    }

  g_stage = CUBRID_LOG_STAGE_EXTRACTION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

/*
 * cubrid_log_clear_log_item () -
 *   return:
 *   log_item_list(in):
 */
int
cubrid_log_clear_log_item (CUBRID_LOG_ITEM * log_item_list)
{
  if (g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (log_item_list == NULL)
    {
      return CUBRID_LOG_INVALID_LOGITEM_LIST;
    }

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_finalize () -
 *   return:
 */
int
cubrid_log_finalize (void)
{
  int i = 0;

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION && g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  css_free_conn (g_conn_entry);
  g_conn_entry = NULL;

  g_connection_timeout = 300;
  g_extraction_timeout = 300;
  g_max_log_item = 512;
  g_all_in_cond = false;

  if (g_extraction_table != NULL)
    {
      free (g_extraction_table);
      g_extraction_table = NULL;
    }

  g_extraction_table_count = 0;

  if (g_extraction_user != NULL)
    {
      for (i = 0; i < g_extraction_user_count; i++)
	{
	  free (g_extraction_user[i]);
	}

      free (g_extraction_user);
      g_extraction_user = NULL;
    }

  g_extraction_user_count = 0;

  snprintf (g_trace_log_path, PATH_MAX + 1, "%s", "./");
  g_trace_log_level = 0;
  g_trace_log_filesize = 10;

  g_stage = CUBRID_LOG_STAGE_CONFIGURATION;

  return CUBRID_LOG_SUCCESS;
}

#endif /* CS_MODE */
