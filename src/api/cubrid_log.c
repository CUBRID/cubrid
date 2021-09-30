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

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <arpa/inet.h>
#endif /* WINDOWS */

#include <assert.h>
#include <limits.h>

#include "authenticate.h"
#include "connection_cl.h"
#include "connection_list_cl.h"
#include "cubrid_log.h"
#include "log_lsa.hpp"
#include "network.h"
#include "object_representation.h"
#include "dbi.h"
#include "dbtype_def.h"

#define CUBRID_LOG_ERROR_HANDLING(e, v) \
  do\
    {\
      (err_code) = (e); \
      if(g_trace_log)\
        {\
          sprintf(v, "FILE : %s, LINE : %d \n", __FILE__, __LINE__); \
          cubrid_log_set_tracelog_pointer (v); \
          fprintf (g_trace_log, v); \
        }\
      goto cubrid_log_error; \
    }\
  while(0)

typedef enum
{
  CUBRID_LOG_STAGE_CONFIGURATION,
  CUBRID_LOG_STAGE_PREPARATION,
  CUBRID_LOG_STAGE_EXTRACTION,
  CUBRID_LOG_STAGE_FINALIZATION
} CUBRID_LOG_STAGE;

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

FILE *g_trace_log = NULL;
char g_trace_log_path[PATH_MAX + 1] = "./cubrid_tracelog.err";
int g_trace_log_level = 0;
int64_t g_trace_log_filesize = 10 * 1024 * 1024;	/* 10 MB */

LOG_LSA g_next_lsa = LSA_INITIALIZER;

char *g_log_infos = NULL;
int g_log_infos_size = 0;

CUBRID_LOG_ITEM *g_log_items = NULL;
int g_log_items_count = 0;

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
 * cubrid_log_set_tracelog_pointer () -
 *   return:
 *   logsize(in):
 */
static void
cubrid_log_set_tracelog_pointer (char buf[])
{
  int64_t curr_size = 0;
  int64_t tracelog_size = 0;

  curr_size = ftell (g_trace_log);
  tracelog_size = strlen (buf);

  if (curr_size + tracelog_size > g_trace_log_filesize)
    {
      fseek (g_trace_log, 0, SEEK_SET);
    }
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
  g_trace_log_filesize = filesize * 1024 * 1024;
  g_trace_log = fopen (path, "a+");
  if (g_trace_log == NULL)
    {
      return CUBRID_LOG_INVALID_PATH;
    }

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

  memcpy (g_extraction_table, classoid_arr, arr_size * sizeof (uint64_t));
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

  char trace_errbuf[1024];

  g_conn_entry = css_make_conn (INVALID_SOCKET);
  if (g_conn_entry == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (css_common_connect
      (host, g_conn_entry, DATA_REQUEST, dbname, strlen (dbname) + 1, port, g_connection_timeout, &rid, true) == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  css_queue_user_data_buffer (g_conn_entry, rid, sizeof (int), (char *) &reason);

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (recv_data == NULL || recv_data_size != sizeof (int))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  reason = ntohl (*(int *) recv_data);

  if (reason != SERVER_CONNECTED)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
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

  char *a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *request, *ptr;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int request_size, i;
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  char trace_errbuf[1024];

  request_size = OR_INT_SIZE * 5;

  for (i = 0; i < g_extraction_user_count; i++)
    {
      request_size += or_packed_string_length (g_extraction_user[i], NULL);
    }

  request_size += (OR_BIGINT_SIZE * g_extraction_table_count);

  a_request = (char *) malloc (request_size + MAX_ALIGNMENT);
  if (a_request == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
    }

  request = PTR_ALIGN (a_request, MAX_ALIGNMENT);

  ptr = or_pack_int (request, g_max_log_item);
  ptr = or_pack_int (ptr, g_extraction_timeout);
  ptr = or_pack_int (ptr, g_all_in_cond);
  ptr = or_pack_int (ptr, g_extraction_user_count);

  for (i = 0; i < g_extraction_user_count; i++)
    {
      ptr = or_pack_string (ptr, g_extraction_user[i]);
    }

  ptr = or_pack_int (ptr, g_extraction_table_count);

  for (i = 0; i < g_extraction_table_count; i++)
    {
      ptr = or_pack_int64 (ptr, (INT64) g_extraction_table[i]);
    }

  request_size = ptr - request;

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_START_SESSION, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (recv_data == NULL || recv_data_size != sizeof (int))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  or_unpack_int (recv_data, &reply_code);

  if (reply_code == ER_CDC_NOT_AVAILABLE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_UNAVAILABLE_CDC_SERVER, trace_errbuf);
    }
  else if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  free_and_init (a_request);

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

  if (a_request != NULL)
    {
      free_and_init (a_request);
    }

  return err_code;
}

static int
cubrid_log_db_login (char *dbname, char *id, char *password)
{
  MOP user;
  int error = NO_ERROR;

  error = db_restart ("cubrid_log_api", TRUE, dbname);
  if (error != NO_ERROR)
    {
      return CUBRID_LOG_FAILED_LOGIN;
    }

  user = au_find_user (id);
  if (user == NULL)
    {
      error = CUBRID_LOG_FAILED_LOGIN;
      goto error;
    }

  if (!au_is_dba_group_member (user))
    {
      error = CUBRID_LOG_FAILED_LOGIN;
      goto error;
    }

  if (db_login (id, password) != NO_ERROR)
    {
      error = CUBRID_LOG_FAILED_LOGIN;
      goto error;
    }

  db_shutdown ();

  return CUBRID_LOG_SUCCESS;

error:

  db_shutdown ();

  return error;
}

/*
 * cubrid_log_connect_server () -
 *   return:
 *   host(in):
 *   port(in):
 *   dbname(in):
 */
int
cubrid_log_connect_server (char *host, int port, char *dbname, char *id, char *password)
{
  int err_code;

  char trace_errbuf[1024];

  if (g_trace_log == NULL)
    {
      g_trace_log = fopen (g_trace_log_path, "a+");
      if (g_trace_log == NULL)
	{
	  err_code = CUBRID_LOG_FAILED_CONNECT;
	  goto cubrid_log_error;
	}
    }

  if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE, trace_errbuf);
    }

  if (host == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_HOST, trace_errbuf);
    }

  if (port < 0 || port > USHRT_MAX)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_PORT, trace_errbuf);
    }

  if (dbname == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_DBNAME, trace_errbuf);
    }

  if (id == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_ID, trace_errbuf);
    }

  if (password == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_PASSWORD, trace_errbuf);
    }

  if (cubrid_log_db_login (dbname, id, password) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_LOGIN, trace_errbuf);
    }

  if (cubrid_log_connect_server_internal (host, port, dbname) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (cubrid_log_send_configurations () != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  g_stage = CUBRID_LOG_STAGE_PREPARATION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_find_start_lsa (time_t * timestamp, LOG_LSA * lsa)
{
  unsigned short rid = 0;

  OR_ALIGNED_BUF (OR_BIGINT_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT64_SIZE) a_reply;
  char *request = OR_ALIGNED_BUF_START (a_request), *ptr;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int request_size = OR_ALIGNED_BUF_SIZE (a_request);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  char trace_errbuf[1024];

  or_pack_int64 (request, (INT64) (*timestamp));

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_FIND_LSA, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  /* extraction timeout will be replaced when it is defined */
  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT64_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  ptr = or_unpack_int (recv_data, &reply_code);

  if (reply_code == NO_ERROR || reply_code == ER_CDC_ADJUSTED_LSA)
    {
      ptr = or_unpack_log_lsa (ptr, lsa);
      or_unpack_int64 (ptr, timestamp);

      if (recv_data != NULL && recv_data != reply)
	{
	  free_and_init (recv_data);
	}

      return CUBRID_LOG_SUCCESS;
    }
  else if (reply_code == ER_CDC_ADJUSTED_LSA)
    {
      ptr = or_unpack_log_lsa (ptr, lsa);
      or_unpack_int64 (ptr, timestamp);

      if (recv_data != NULL && recv_data != reply)
	{
	  free_and_init (recv_data);
	}

      return CUBRID_LOG_SUCCESS_WITH_ADJUSTED_LSA;
    }
  else
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND, trace_errbuf);
    }

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
cubrid_log_find_lsa (time_t * timestamp, uint64_t * lsa)
{
  int err_code;

  char trace_errbuf[1024];

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE, trace_errbuf);
    }

  if (timestamp <= 0)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_TIMESTAMP, trace_errbuf);
    }

  if (lsa == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_OUT_PARAM, trace_errbuf);
    }

  if (LSA_ISNULL (&g_next_lsa))
    {
      if (cubrid_log_find_start_lsa (timestamp, &g_next_lsa) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND, trace_errbuf);
	}
    }

  memcpy (lsa, &g_next_lsa, sizeof (uint64_t));

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_extract_internal (LOG_LSA * next_lsa, int *num_infos, int *total_length)
{
  unsigned short rid = 0;

  OR_ALIGNED_BUF (OR_LOG_LSA_ALIGNED_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT_SIZE + OR_INT_SIZE) a_reply;
  char *request = OR_ALIGNED_BUF_START (a_request), *ptr;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int request_size = OR_ALIGNED_BUF_SIZE (a_request);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;
  int rc = NO_ERROR;

  char trace_errbuf[1024];

  or_pack_log_lsa (request, next_lsa);

  /* protocol name will be modified */
  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_GET_LOGINFO_METADATA, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  /* extraction timeout will be modified when it is defined */
  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT_SIZE + OR_INT_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  ptr = or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      if (reply_code == ER_CDC_NOTHING_TO_RETURN)
	{
	  rc = CUBRID_LOG_SUCCESS_WITH_NO_LOGITEM;
	}
      else if (reply_code == ER_CDC_EXTRACTION_TIMEOUT)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_EXTRACTION_TIMEOUT, trace_errbuf);
	}
      else if (reply_code == ER_CDC_INVALID_LOG_LSA)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA, trace_errbuf);
	}
    }

  ptr = or_unpack_log_lsa (ptr, next_lsa);

  ptr = or_unpack_int (ptr, num_infos);
  or_unpack_int (ptr, total_length);

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  if (rc == CUBRID_LOG_SUCCESS_WITH_NO_LOGITEM)
    {
      goto cubrid_log_end;
    }

  if (g_log_infos_size < *total_length)
    {
      char *tmp_log_infos = NULL;

      tmp_log_infos = (char *) realloc ((void *) g_log_infos, *total_length + MAX_ALIGNMENT);
      if (tmp_log_infos == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}
      else
	{
	  g_log_infos = tmp_log_infos;
	}

      g_log_infos_size = *total_length;
    }

  reply = PTR_ALIGN (g_log_infos, MAX_ALIGNMENT);
  reply_size = *total_length;

  if (*total_length > 0)
    {
      if (css_send_request_with_data_buffer
	  (g_conn_entry, NET_SERVER_CDC_GET_LOGINFO, &rid, NULL, 0, reply, reply_size) != NO_ERRORS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
	}

      /* extraction timeout will be modified when it is defined */
      if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
	}

      if (recv_data == NULL || recv_data_size != *total_length)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_EXTRACT, trace_errbuf);
	}
    }
  return CUBRID_LOG_SUCCESS;

cubrid_log_end:

  return rc;

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

inline static int
cubrid_log_make_ddl (char **data_info, DDL * ddl)
{
  char *ptr;

  char trace_errbuf[1024];

  ptr = *data_info;

  ptr = or_unpack_int (ptr, &ddl->ddl_type);
  ptr = or_unpack_int (ptr, &ddl->object_type);
  ptr = or_unpack_int64 (ptr, (INT64 *) & ddl->oid);
  ptr = or_unpack_int64 (ptr, (INT64 *) & ddl->classoid);
  ptr = or_unpack_int (ptr, &ddl->statement_length);
  ptr = or_unpack_string_nocopy (ptr, &ddl->statement);

  *data_info = ptr;

  return CUBRID_LOG_SUCCESS;
}

inline static int
cubrid_log_make_dml (char **data_info, DML * dml)
{
  char *ptr;

  int i, pack_func_code;
  int err_code;

  char trace_errbuf[1024];

  ptr = *data_info;

  ptr = or_unpack_int (ptr, &dml->dml_type);
  ptr = or_unpack_int64 (ptr, (INT64 *) & dml->classoid);
  ptr = or_unpack_int (ptr, &dml->num_changed_column);

  if (dml->num_changed_column)
    {
      // now, just malloc for validation and later, optimize it.
      dml->changed_column_index = (int *) malloc (sizeof (int) * dml->num_changed_column);
      if (dml->changed_column_index == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}

      for (i = 0; i < dml->num_changed_column; i++)
	{
	  ptr = or_unpack_int (ptr, &dml->changed_column_index[i]);
	}

      dml->changed_column_data = (char **) malloc (sizeof (char *) * dml->num_changed_column);
      if (dml->changed_column_data == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}

      dml->changed_column_data_len = (int *) malloc (sizeof (int) * dml->num_changed_column);
      if (dml->changed_column_data_len == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}

      for (i = 0; i < dml->num_changed_column; i++)
	{
	  ptr = or_unpack_int (ptr, &pack_func_code);

	  switch (pack_func_code)
	    {
	    case 0:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_int (ptr, (int *) dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = OR_INT_SIZE;
	      break;

	    case 1:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_int64 (ptr, (INT64 *) dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = OR_INT64_SIZE;
	      break;

	    case 2:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_float (ptr, (float *) dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = OR_FLOAT_SIZE;
	      break;

	    case 3:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_double (ptr, (double *) dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = OR_DOUBLE_SIZE;
	      break;

	    case 4:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_short (ptr, (short *) dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = OR_SHORT_SIZE;
	      break;

	    case 5:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = strlen (dml->changed_column_data[i]);
	      break;
	    case 6:
	      assert (0);	// unused pack func code: or_pack_stream()
	      break;

	    case 7:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->changed_column_data[i]);
	      if (dml->changed_column_data[i] == NULL)
		{
		  dml->changed_column_data_len[i] = 0;
		}
	      else
		{
		  dml->changed_column_data_len[i] = strlen (dml->changed_column_data[i]);
		}
	      break;

	    case 8:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = strlen (dml->changed_column_data[i]);
	      break;

	    default:
	      assert (0);

	    }
	}
    }

  ptr = or_unpack_int (ptr, &dml->num_cond_column);

  if (dml->num_cond_column)
    {
      // now, just malloc for validation and later, optimize it.
      dml->cond_column_index = (int *) malloc (sizeof (int) * dml->num_cond_column);
      if (dml->cond_column_index == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}

      for (i = 0; i < dml->num_cond_column; i++)
	{
	  ptr = or_unpack_int (ptr, &dml->cond_column_index[i]);
	}

      dml->cond_column_data = (char **) malloc (sizeof (char *) * dml->num_cond_column);
      if (dml->cond_column_data == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}

      dml->cond_column_data_len = (int *) malloc (sizeof (int) * dml->num_cond_column);
      if (dml->cond_column_data_len == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}

      for (i = 0; i < dml->num_cond_column; i++)
	{
	  ptr = or_unpack_int (ptr, &pack_func_code);

	  switch (pack_func_code)
	    {
	    case 0:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_int (ptr, (int *) dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = OR_INT_SIZE;
	      break;

	    case 1:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_int64 (ptr, (INT64 *) dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = OR_BIGINT_SIZE;
	      break;

	    case 2:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_float (ptr, (float *) dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = OR_FLOAT_SIZE;
	      break;

	    case 3:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_double (ptr, (double *) dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = OR_DOUBLE_SIZE;
	      break;

	    case 4:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_short (ptr, (short *) dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = OR_SHORT_SIZE;
	      break;

	    case 5:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = strlen (dml->cond_column_data[i]);
	      break;
	    case 6:
	      assert (0);	// unused pack func code: or_pack_stream()
	      break;

	    case 7:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = strlen (dml->cond_column_data[i]);
	      break;

	    case 8:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = strlen (dml->cond_column_data[i]);
	      break;

	    default:
	      assert (0);

	    }
	}
    }

  *data_info = ptr;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

inline static int
cubrid_log_make_dcl (char **data_info, DCL * dcl)
{
  char *ptr;

  ptr = *data_info;

  ptr = or_unpack_int (ptr, &dcl->dcl_type);
  ptr = or_unpack_int64 (ptr, &dcl->timestamp);

  *data_info = ptr;

  return CUBRID_LOG_SUCCESS;
}

inline static int
cubrid_log_make_timer (char **data_info, TIMER * timer)
{
  char *ptr;

  ptr = *data_info;

  ptr = or_unpack_int64 (ptr, &timer->timestamp);

  *data_info = ptr;

  return CUBRID_LOG_SUCCESS;
}

static int
cubrid_log_make_data_item (char **data_info, DATA_ITEM_TYPE data_item_type, CUBRID_DATA_ITEM * data_item)
{
  int err_code;

  char trace_errbuf[1024];

  switch (data_item_type)
    {
    case DATA_ITEM_TYPE_DDL:
      if ((err_code = cubrid_log_make_ddl (data_info, &data_item->ddl)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, trace_errbuf);
	}

      break;

    case DATA_ITEM_TYPE_DML:
      if ((err_code = cubrid_log_make_dml (data_info, &data_item->dml)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, trace_errbuf);
	}

      break;

    case DATA_ITEM_TYPE_DCL:
      if ((err_code = cubrid_log_make_dcl (data_info, &data_item->dcl)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, trace_errbuf);
	}

      break;

    case DATA_ITEM_TYPE_TIMER:
      if ((err_code = cubrid_log_make_timer (data_info, &data_item->timer)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, trace_errbuf);
	}

      break;

    default:
      assert (0);
    }

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_make_log_item (char **log_info, CUBRID_LOG_ITEM * log_item)
{
  char *ptr;

  int log_info_len;
  int err_code;
  int rc;

  char trace_errbuf[1024];

  ptr = *log_info;

  ptr = or_unpack_int (ptr, &log_info_len);

  ptr = or_unpack_int (ptr, &log_item->transaction_id);
  ptr = or_unpack_string_nocopy (ptr, &log_item->user);
  ptr = or_unpack_int (ptr, &log_item->data_item_type);

  if ((rc = cubrid_log_make_data_item (&ptr, (DATA_ITEM_TYPE) log_item->data_item_type, &log_item->data_item)) !=
      CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (rc, trace_errbuf);
    }

  *log_info = ptr;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_make_log_item_list (int num_infos, int total_length, CUBRID_LOG_ITEM ** log_item_list, int *list_size)
{
  char *ptr;

  int i;
  int err_code;
  int rc;

  char trace_errbuf[1024];

  if (g_log_items_count < num_infos)
    {
      CUBRID_LOG_ITEM *tmp_log_items = NULL;

      tmp_log_items = (CUBRID_LOG_ITEM *) realloc ((void *) g_log_items, sizeof (CUBRID_LOG_ITEM) * num_infos);
      if (tmp_log_items == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, trace_errbuf);
	}
      else
	{
	  g_log_items = tmp_log_items;
	}

      g_log_items_count = num_infos;
    }

  ptr = PTR_ALIGN (g_log_infos, MAX_ALIGNMENT);

  for (i = 0; i < num_infos; i++)
    {
      if ((rc = cubrid_log_make_log_item (&ptr, &g_log_items[i]) != CUBRID_LOG_SUCCESS))
	{
	  CUBRID_LOG_ERROR_HANDLING (rc, trace_errbuf);
	}

      g_log_items[i].next = &g_log_items[i + 1];

      ptr = PTR_ALIGN (ptr, MAX_ALIGNMENT);
    }

  g_log_items[num_infos - 1].next = NULL;

  *log_item_list = g_log_items;
  *list_size = num_infos;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

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
  int num_infos, total_length;
  int err_code;
  int rc;

  char trace_errbuf[1024];

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION && g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE, trace_errbuf);
    }

  if (lsa == NULL || log_item_list == NULL || list_size == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_OUT_PARAM, trace_errbuf);
    }

  memcpy (&g_next_lsa, lsa, sizeof (LOG_LSA));

  rc = cubrid_log_extract_internal (&g_next_lsa, &num_infos, &total_length);

  if (rc != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (rc, trace_errbuf);
    }

  if ((rc = cubrid_log_make_log_item_list (num_infos, total_length, log_item_list, list_size)) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (rc, trace_errbuf);
    }

  memcpy (lsa, &g_next_lsa, sizeof (uint64_t));

  g_stage = CUBRID_LOG_STAGE_EXTRACTION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_clear_data_item (DATA_ITEM_TYPE data_item_type, CUBRID_DATA_ITEM * data_item)
{
  int err_code;

  switch (data_item_type)
    {
    case DATA_ITEM_TYPE_DDL:
      /* nothing to do */
      break;

    case DATA_ITEM_TYPE_DML:
      if (data_item->dml.num_changed_column > 0)
	{
	  free_and_init (data_item->dml.changed_column_index);
	  free_and_init (data_item->dml.changed_column_data);
	  free_and_init (data_item->dml.changed_column_data_len);
	  data_item->dml.num_changed_column = 0;
	}

      if (data_item->dml.num_cond_column > 0)
	{
	  free_and_init (data_item->dml.cond_column_index);
	  free_and_init (data_item->dml.cond_column_data);
	  free_and_init (data_item->dml.cond_column_data_len);
	  data_item->dml.num_cond_column = 0;
	}

      break;

    case DATA_ITEM_TYPE_DCL:
      /* nothing to do */
      break;

    case DATA_ITEM_TYPE_TIMER:
      /* nothing to do */
      break;

    default:
      assert (0);
    }

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
  int i;
  int err_code;
  int rc;

  char trace_errbuf[1024];

  if (g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE, trace_errbuf);
    }

  if (log_item_list == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LOGITEM_LIST, trace_errbuf);
    }

  for (i = 0; i < g_log_items_count; i++)	// if g_log_items_count == 0 then nothing to do
    {
      if ((rc =
	   cubrid_log_clear_data_item ((DATA_ITEM_TYPE) g_log_items[i].data_item_type,
				       &g_log_items[i].data_item)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_DEALLOC, trace_errbuf);
	}
    }

  g_log_items_count = 0;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_disconnect_server (void)
{
  unsigned short rid = 0;

  OR_ALIGNED_BUF (OR_INT_SIZE) a_reply;
  char *reply = OR_ALIGNED_BUF_START (a_reply);
  int reply_size = OR_ALIGNED_BUF_SIZE (a_reply), reply_code;

  char *recv_data = NULL;
  int recv_data_size;

  CSS_QUEUE_ENTRY *queue_entry;
  int err_code;

  char trace_errbuf[1024];

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_END_SESSION, &rid, NULL, 0, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, trace_errbuf);
    }

  or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_DISCONNECT, trace_errbuf);
    }

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
    }

  if (g_trace_log != NULL)
    {
      fclose (g_trace_log);
      g_trace_log = NULL;
    }

  css_free_conn (g_conn_entry);
  g_conn_entry = NULL;

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

static int
cubrid_log_reset_globals (void)
{
  int i;

  g_connection_timeout = 300;
  g_extraction_timeout = 300;
  g_max_log_item = 512;
  g_all_in_cond = false;

  if (g_extraction_table != NULL)
    {
      free_and_init (g_extraction_table);
      g_extraction_table = NULL;
    }

  g_extraction_table_count = 0;

  if (g_extraction_user != NULL)
    {
      for (i = 0; i < g_extraction_user_count; i++)
	{
	  free_and_init (g_extraction_user[i]);
	}

      free_and_init (g_extraction_user);
      g_extraction_user = NULL;
    }

  g_extraction_user_count = 0;

  snprintf (g_trace_log_path, PATH_MAX + 1, "%s", "./cubrid_tracelog.err");
  g_trace_log_level = 0;
  g_trace_log_filesize = 10 * 1024 * 1024;

  g_next_lsa = LSA_INITIALIZER;

  if (g_log_infos != NULL)
    {
//      free (g_log_infos);
      g_log_infos = NULL;
    }

  g_log_infos_size = 0;

  return CUBRID_LOG_SUCCESS;
}

/*
 * cubrid_log_finalize () -
 *   return:
 */
int
cubrid_log_finalize (void)
{
  int err_code;

  char trace_errbuf[1024];

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION && g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE, trace_errbuf);
    }

  if (cubrid_log_disconnect_server () != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_DISCONNECT, trace_errbuf);
    }

  (void) cubrid_log_reset_globals ();

  g_stage = CUBRID_LOG_STAGE_CONFIGURATION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

#endif /* CS_MODE */
