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

#if defined(WINDOWS)
#include <io.h>
#else /* WINDOWS */
#include <unistd.h>
#endif /* WINDOWS */

#include <assert.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "authenticate.h"
#include "connection_cl.h"
#include "connection_list_cl.h"
#include "cubrid_log.h"
#include "log_lsa.hpp"
#include "network.h"
#include "object_representation.h"
#include "dbi.h"
#include "dbtype_def.h"
#include "porting.h"

#if defined(WINDOWS)
#include "wintcp.h"
#endif

#define CUBRID_LOG_WRITE_TRACELOG(msg, ...) \
  do\
    {\
      if (msg) \
        { \
          cubrid_log_tracelog (__FILE__, __LINE__, __func__, false, 0, msg, ##__VA_ARGS__); \
        } \
    }\
  while(0)

#define CUBRID_LOG_ERROR_HANDLING(e, msg, ...) \
  do\
    {\
      (err_code) = (e); \
      if (msg) \
        { \
          cubrid_log_tracelog (__FILE__, __LINE__, __func__, true, e, msg, ##__VA_ARGS__); \
        } \
      goto cubrid_log_error; \
    }\
  while(0)

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif

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

char g_dbname[CUBRID_LOG_MAX_DBNAME_LEN + 1] = "";

FILE *g_trace_log = NULL;
char g_trace_log_base[PATH_MAX + 1] = ".";
char g_trace_log_path[PATH_MAX + 1] = "";
char g_trace_log_path_old[PATH_MAX + 1] = "";
int g_trace_log_level = 0;
int g_num_trace_log = 0;
int64_t g_trace_log_filesize = 10 * 1024 * 1024;	/* 10 MB */

LOG_LSA g_next_lsa = LSA_INITIALIZER;

char *g_log_infos = NULL;
int g_log_infos_size = 0;

CUBRID_LOG_ITEM *g_log_items = NULL;
int g_log_items_count = 0;

const char *
data_item_type_to_string (int data_item_type)
{
  switch (data_item_type)
    {
    case 0:
      return "DDL";
    case 1:
      return "DML";
    case 2:
      return "DCL";
    case 3:
      return "TIMER";
    default:
      assert (0);
    }
}

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

  if (timeout < 0 || timeout > 360)
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

  if (timeout < 0 || timeout > 360)
    {
      return CUBRID_LOG_INVALID_EXTRACTION_TIMEOUT;
    }

  g_extraction_timeout = timeout;

  return CUBRID_LOG_SUCCESS;
}

static void
cubrid_log_reset_tracelog ()
{
  if (g_trace_log != NULL)
    {
      fflush (g_trace_log);
      fclose (g_trace_log);
      g_trace_log = NULL;
    }

  memset (g_trace_log_base, 0, PATH_MAX + 1);
  memset (g_trace_log_path, 0, PATH_MAX + 1);
  memset (g_trace_log_path_old, 0, PATH_MAX + 1);
  memset (g_dbname, 0, CUBRID_LOG_MAX_DBNAME_LEN + 1);

  strcpy (g_trace_log_base, ".");
  g_num_trace_log = 0;
  g_trace_log_level = 0;
  g_trace_log_filesize = 10 * 1024 * 1024;
}

static int
cubrid_log_make_new_tracelog ()
{
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  struct timeval tv;

  int len = 0;

  char curr_time[14];

  if (g_dbname[0] == '\0')
    {
      return -1;
    }

  er_time = time (NULL);

  er_tm_p = localtime_r (&er_time, &er_tm);
  if (er_tm_p == NULL)
    {
      strcpy (curr_time, "00000000_0000");
    }
  else
    {
      strftime (curr_time, sizeof (curr_time), "%Y%m%d_%H%M", er_tm_p);
    }

  if (g_trace_log != NULL)
    {
      fclose (g_trace_log);
      g_trace_log = NULL;
    }

  if (g_num_trace_log == 2)
    {
      if (remove (g_trace_log_path_old) != 0)
	{
	  return -1;
	}
      memcpy (g_trace_log_path_old, g_trace_log_path, PATH_MAX + 1);
    }
  else
    {
      if (g_num_trace_log != 0)
	{
	  memcpy (g_trace_log_path_old, g_trace_log_path, PATH_MAX + 1);
	}
    }

  len = (int) (strlen (g_trace_log_base) + strlen (g_dbname) + strlen (curr_time)) + 17;
  if (len > PATH_MAX)
    {
      return -1;
    }

  snprintf (g_trace_log_path, len, "%s%c%s_cubridlog_%s.err", g_trace_log_base, PATH_SEPARATOR, g_dbname, curr_time);

  g_trace_log = fopen (g_trace_log_path, "a+");
  if (g_trace_log == NULL)
    {
      return -1;
    }

  if (g_num_trace_log < 2)
    {
      g_num_trace_log++;
    }

  assert (g_num_trace_log <= 2);

  return CUBRID_LOG_SUCCESS;
}

static int
cubrid_log_check_tracelog ()
{
  int64_t curr_size = 0;

  if (g_trace_log == NULL)
    {
      return cubrid_log_make_new_tracelog ();
    }
  else
    {
      curr_size = ftell (g_trace_log);
      if (curr_size >= g_trace_log_filesize)
	{
	  return cubrid_log_make_new_tracelog ();
	}
    }

  return CUBRID_LOG_SUCCESS;
}

static int
cubrid_log_check_tracelog_path (char *path)
{
  struct stat statbuf;

  if (access (path, F_OK) == 0)
    {
      /* Check if it is directory */
      stat (path, &statbuf);
      if (!S_ISDIR (statbuf.st_mode))
	{
	  return CUBRID_LOG_INVALID_PATH;
	}

      /* Directory exists */
      if (access (path, W_OK) < 0)
	{
	  /* Write permission */
	  return CUBRID_LOG_NO_FILE_PERMISSION;
	}
    }
  else
    {
      /* Directory not exist, then make directory */
      if (mkdir (path, 0777) < 0)
	{
	  return CUBRID_LOG_NO_FILE_PERMISSION;
	}
    }

  return CUBRID_LOG_SUCCESS;
}

void
cubrid_log_tracelog (const char *filename, const int line_no, const char *function, bool is_error, int error,
		     const char *fmt, ...)
{
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  struct timeval tv;
  char curr_time[256];

  va_list arg_ptr;

  if (cubrid_log_check_tracelog () != CUBRID_LOG_SUCCESS)
    {
      return;
    }

  er_time = time (NULL);

  er_tm_p = localtime_r (&er_time, &er_tm);
  if (er_tm_p == NULL)
    {
      strcpy (curr_time, "00/00/00 00:00:00.000");
    }
  else
    {
      gettimeofday (&tv, NULL);
      snprintf (curr_time + strftime (curr_time, 128, "%m/%d/%y %H:%M:%S", er_tm_p), 255, ".%03ld", tv.tv_usec / 1000);
    }

  if (!is_error)
    {
      fprintf (g_trace_log, "[%s][FILE:%s][LINE:%d][FUNC:%s]\n", curr_time, filename, line_no, function);
    }
  else
    {
      fprintf (g_trace_log, "[%s][FILE:%s][LINE:%d][FUNC:%s][ERROR:%d]\n", curr_time, filename, line_no, function,
	       error);
    }

  va_start (arg_ptr, fmt);
  vfprintf (g_trace_log, fmt, arg_ptr);
  va_end (arg_ptr);
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
  int err_code;

  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (path == NULL || strlen (path) > PATH_MAX)
    {
      return CUBRID_LOG_INVALID_PATH;
    }
  else
    {
      if ((err_code = cubrid_log_check_tracelog_path (path)) != CUBRID_LOG_SUCCESS)
	{
	  return err_code;
	}
    }

  if (level < 0 || level > 1)
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

  snprintf (g_trace_log_base, PATH_MAX + 1, "%s", path);
  g_trace_log_level = level;
  g_trace_log_filesize = filesize * 1024 * 1024;

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

#if defined (WINDOWS)
  if (css_windows_startup () < 0)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "Failed to startup Windows socket\n");
    }
#endif

  g_conn_entry = css_make_conn (INVALID_SOCKET);
  if (g_conn_entry == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "Failed to make css_conn_entry to connect to the server\n");
    }

  if (css_common_connect
      (host, g_conn_entry, DATA_REQUEST, dbname, (int) strlen (dbname) + 1, port, g_connection_timeout, &rid,
       true) == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Failed to connect to the server. host (%s), dbname (%s), port (%d), timeout (%d sec)\n",
				 host, dbname, port, g_connection_timeout);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "Failed to receive data from server. (timeout : %d sec)\n",
				 g_connection_timeout);
    }

  if (recv_data == NULL || recv_data_size != sizeof (int))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Failed to receive data from server. recv_data is %s, recv_data_size : %d (should be %d)\n",
				 recv_data ? "not null" : "null", recv_data_size, sizeof (int));
    }

  reason = ntohl (*(int *) recv_data);

  if (recv_data != NULL)
    {
      free_and_init (recv_data);
    }

#if defined (WINDOWS)
  if (reason == SERVER_CONNECTED_NEW)
    {
      if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				     "Failed to receive the server port id from the master.\n");
	}

      if (recv_data != NULL)
	{
	  assert (recv_data_size == sizeof (int));

	  int port_id = ntohl (*((int *) recv_data));

	  css_close_conn (g_conn_entry);

	  g_conn_entry = css_server_connect_part_two (host, g_conn_entry, port_id, &rid);
	  if (g_conn_entry == NULL)
	    {
	      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
					 "Failed to connect to the server with new port id (%d)\n", port_id);
	    }

	  free_and_init (recv_data);
	}
      else
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				     "Failed to get server port id (recv_data = %s , recv_data_size = %d) \n",
				     recv_data, recv_data_size);
	}
    }
  else
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "Failed to connect to the server (reason : %d) \n", reason);
    }
#else
  if (reason != SERVER_CONNECTED)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "Failed to connect to the server (reason : %d) \n", reason);
    }
#endif

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  if (recv_data != NULL)
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

#if defined (WINDOWS)
  (void) css_windows_shutdown ();
#endif

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

  request_size = OR_INT_SIZE * 5;

  for (i = 0; i < g_extraction_user_count; i++)
    {
      request_size += or_packed_string_length (g_extraction_user[i], NULL);
    }

  request_size += (OR_BIGINT_SIZE * g_extraction_table_count);

  a_request = (char *) malloc (request_size + MAX_ALIGNMENT);
  if (a_request == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, "Failed to malloc for request. (size : %d)\n",
				 request_size + MAX_ALIGNMENT);
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

  request_size = (int) (ptr - request);

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_START_SESSION, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Request(NET_SERVER_CDC_START_SESSION) failed. request size (%d), reply_sze (%d)\n",
				 request_size, reply_size);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_connection_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "receive data from the request(NET_SERVER_CDC_START_SESSION) failed. (timeout : %d sec)\n",
				 g_connection_timeout);
    }

  if (recv_data == NULL || recv_data_size != sizeof (int))
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "recv_data is %s, receive data size : %d (should be %d)\n",
				 recv_data ? "not null" : "null", recv_data_size, sizeof (int));
    }

  or_unpack_int (recv_data, &reply_code);

  if (reply_code == ER_CDC_NOT_AVAILABLE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_UNAVAILABLE_CDC_SERVER,
				 "Failed to connect to the server. Please check 'supplemental_log' parameter\n");
    }
  else if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Failed to connect to the server. reply code from server is %d\n", reply_code);
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
cubrid_log_db_login (char *hostname, char *dbname, char *username, char *password)
{
  MOP user;
  char dbname_at_hostname[CUB_MAXHOSTNAMELEN + CUBRID_LOG_MAX_DBNAME_LEN + 2] = { '\0', };

  snprintf (dbname_at_hostname, sizeof (dbname_at_hostname), "%s@%s", dbname, hostname);

  if (au_login (username, password, true) != NO_ERROR)
    {
      cubrid_log_tracelog (__FILE__, __LINE__, __func__, true, CUBRID_LOG_FAILED_LOGIN, "Failed to login\n");
      goto error;
    }

  if (db_restart ("cubrid_log_api", 0, dbname_at_hostname) != NO_ERROR)
    {
      cubrid_log_tracelog (__FILE__, __LINE__, __func__, true, CUBRID_LOG_FAILED_LOGIN,
			   "db_restart failed to connect to %s\n", dbname_at_hostname);
      return CUBRID_LOG_FAILED_LOGIN;
    }

  user = au_find_user (username);
  if (user == NULL)
    {
      cubrid_log_tracelog (__FILE__, __LINE__, __func__, true, CUBRID_LOG_FAILED_LOGIN, "can not find user %s\n",
			   username);
      goto error;
    }

  if (!au_is_dba_group_member (user))
    {
      cubrid_log_tracelog (__FILE__, __LINE__, __func__, true, CUBRID_LOG_FAILED_LOGIN,
			   "DBA authorization failed. %s is not a member of DBA group\n", username);
      goto error;
    }

  db_shutdown ();

  return CUBRID_LOG_SUCCESS;

error:

  db_shutdown ();

  return CUBRID_LOG_FAILED_LOGIN;
}

/*
 * cubrid_log_connect_server () -
 *   return:
 *   host(in):
 *   port(in):
 *   dbname(in):
 */
int
cubrid_log_connect_server (char *host, int port, char *dbname, char *user, char *password)
{
  int err_code;

  if (dbname != NULL)
    {
      strncpy (g_dbname, dbname, CUBRID_LOG_MAX_DBNAME_LEN);
    }
  else
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_DBNAME, "dbname must not be null\n");
    }

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] stage (%d), host (%s), port (%d), dbname (%s), user (%s)\n", g_stage, host,
				 port, dbname, user);
    }

  if (g_stage != CUBRID_LOG_STAGE_CONFIGURATION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE,
				 "stage required for this step : %d, but current stage : %d\n",
				 CUBRID_LOG_STAGE_CONFIGURATION, g_stage);
    }

  if (host == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_HOST, "host must not be null\n");
    }

  if (port < 0 || port > USHRT_MAX)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_PORT,
				 "invalid port number : %d, port must be greater than 0 and less than %d\n", port,
				 USHRT_MAX);
    }

  if (user == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_USER, "user must not be null\n");
    }

  if (password == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_PASSWORD, "password must not be null\n");
    }

  if (cubrid_log_db_login (host, dbname, user, password) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_LOGIN, NULL);
    }

  if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "error initialization failed\n");
    }

  if (cubrid_log_connect_server_internal (host, port, dbname) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, NULL);
    }

  if (cubrid_log_send_configurations () != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, NULL);
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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] timestamp (%lld)\n", *timestamp);
    }

  or_pack_int64 (request, (INT64) (*timestamp));

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_FIND_LSA, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Request(NET_SERVER_CDC_FIND_LSA) failed. request size (%d), reply_sze (%d)\n",
				 request_size, reply_size);
    }

  /* extraction timeout will be replaced when it is defined */
  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "receive data from the request(NET_SERVER_CDC_FIND_LSA) failed. (timeout : %d sec)\n",
				 g_connection_timeout);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT64_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "recv_data is %s | recv_data_size = %d (should be %d)\n",
				 recv_data ? "not null" : "null", recv_data_size,
				 OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT64_SIZE);
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
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND, "LOG LSA is not found at the time %lld\n", *timestamp);
    }

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[OUTPUT] timestamp (%lld), lsa (%lld | %d)\n", *timestamp, lsa->pageid, lsa->offset);
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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] stage (%d), timestamp (%lld)\n", g_stage, *timestamp);
    }

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE,
				 "stage required for this step : %d, but current stage : %d\n",
				 CUBRID_LOG_STAGE_PREPARATION, g_stage);
    }

  if (timestamp == NULL || *timestamp < 0)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_TIMESTAMP,
				 "timestamp must be greater or equal than 0. Input timestamp is %s, and value is %lld\n",
				 timestamp ? "not null" : "null", *timestamp);
    }

  if (lsa == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_OUT_PARAM, "Input lsa must not be null\n");
    }

  if (LSA_ISNULL (&g_next_lsa))
    {
      if (cubrid_log_find_start_lsa (timestamp, &g_next_lsa) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_LSA_NOT_FOUND, NULL);
	}
    }

  memcpy (lsa, &g_next_lsa, sizeof (uint64_t));

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[OUTPUT] timestamp (%lld), lsa (%lld)\n", *timestamp, g_next_lsa);
    }

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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] lsa (%lld | %d)\n", next_lsa->pageid, next_lsa->offset);
    }

  or_pack_log_lsa (request, next_lsa);

  /* protocol name will be modified */
  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_GET_LOGINFO_METADATA, &rid, request, request_size, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Request(NET_SERVER_CDC_GET_LOGINFO_METADATA) failed. request size (%d), reply_sze (%d)\n",
				 request_size, reply_size);
    }

  /* extraction timeout will be modified when it is defined */
  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "receive data from the request(NET_SERVER_CDC_GET_LOGINFO_METADATA) failed. (timeout : %d sec)\n",
				 g_connection_timeout);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT_SIZE + OR_INT_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "recv_data is %s | recv_data_size = %d (should be %d)\n",
				 recv_data ? "not null" : "null", recv_data_size,
				 OR_INT_SIZE + OR_LOG_LSA_ALIGNED_SIZE + OR_INT_SIZE + OR_INT_SIZE);
    }

  ptr = or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      if (reply_code == ER_CDC_EXTRACTION_TIMEOUT)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_EXTRACTION_TIMEOUT, "Extraction timeout (timeout : %d sec)\n",
				     g_extraction_timeout);
	}
      else if (reply_code == ER_CDC_INVALID_LOG_LSA)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LSA, "Input lsa is not valid (%lld|%d)\n", next_lsa->pageid,
				     next_lsa->offset);
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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[MID] extract infos are next_lsa (%lld | %d), num_infos (%d), total_length (%d)\n",
				 next_lsa->pageid, next_lsa->offset, *num_infos, *total_length);
    }

  if (g_log_infos_size < *total_length)
    {
      char *tmp_log_infos = NULL;

      tmp_log_infos = (char *) realloc ((void *) g_log_infos, *total_length + MAX_ALIGNMENT);
      if (tmp_log_infos == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC,
				     "Malloc failed for storing temporary log infos (size : %d)\n",
				     *total_length + MAX_ALIGNMENT);
	}
      else
	{
	  g_log_infos = tmp_log_infos;
	}

      g_log_infos_size = *total_length;
    }

  reply = PTR_ALIGN (g_log_infos, MAX_ALIGNMENT);
  reply_size = *total_length;
  recv_data = NULL;

  if (*total_length > 0)
    {
      if (css_send_request_with_data_buffer
	  (g_conn_entry, NET_SERVER_CDC_GET_LOGINFO, &rid, NULL, 0, reply, reply_size) != NO_ERRORS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				     "Request(NET_SERVER_CDC_GET_LOGINFO) failed. reply_sze (%d)\n", reply_size);
	}

      /* extraction timeout will be modified when it is defined */
      if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				     "receive data from the request(NET_SERVER_CDC_GET_LOGINFO) failed. (timeout : %d sec)\n",
				     g_extraction_timeout);
	}

      if (recv_data == NULL || recv_data_size != *total_length)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				     "recv_data is %s | recv_data_size = %d (should be %d)\n",
				     recv_data ? "not null" : "null", recv_data_size, *total_length);
	}
    }

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[END]\n");
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
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC,
				     "Malloc failed for dml->changed_column_index (size : %d)\n",
				     sizeof (int) * dml->num_changed_column);
	}

      for (i = 0; i < dml->num_changed_column; i++)
	{
	  ptr = or_unpack_int (ptr, &dml->changed_column_index[i]);
	}

      dml->changed_column_data = (char **) malloc (sizeof (char *) * dml->num_changed_column);
      if (dml->changed_column_data == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC,
				     "Malloc failed for dml->changed_column_data (size : %d)\n",
				     sizeof (char *) * dml->num_changed_column);
	}

      dml->changed_column_data_len = (int *) malloc (sizeof (int) * dml->num_changed_column);
      if (dml->changed_column_data_len == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC,
				     "Malloc failed for dml->changed_column_data_len (size : %d)\n",
				     sizeof (int) * dml->num_changed_column);
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
	      dml->changed_column_data_len[i] = (int) strlen (dml->changed_column_data[i]);
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
		  dml->changed_column_data_len[i] = (int) strlen (dml->changed_column_data[i]);
		}
	      break;

	    case 8:
	      dml->changed_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->changed_column_data[i]);
	      dml->changed_column_data_len[i] = (int) strlen (dml->changed_column_data[i]);
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
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, "Malloc failed for dml->cond_column_index (size : %d)\n",
				     sizeof (int) * dml->num_cond_column);
	}

      for (i = 0; i < dml->num_cond_column; i++)
	{
	  ptr = or_unpack_int (ptr, &dml->cond_column_index[i]);
	}

      dml->cond_column_data = (char **) malloc (sizeof (char *) * dml->num_cond_column);
      if (dml->cond_column_data == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC, "Malloc failed for dml->cond_column_data (size : %d)\n",
				     sizeof (char *) * dml->num_cond_column);
	}

      dml->cond_column_data_len = (int *) malloc (sizeof (int) * dml->num_cond_column);
      if (dml->cond_column_data_len == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC,
				     "Malloc failed for dml->cond_column_data_len (size : %d)\n",
				     sizeof (int) * dml->num_cond_column);
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
	      dml->cond_column_data_len[i] = (int) strlen (dml->cond_column_data[i]);
	      break;
	    case 6:
	      assert (0);	// unused pack func code: or_pack_stream()
	      break;

	    case 7:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->cond_column_data[i]);
	      if (dml->cond_column_data[i] == NULL)
		{
		  dml->cond_column_data_len[i] = 0;
		}
	      else
		{
		  dml->cond_column_data_len[i] = (int) strlen (dml->cond_column_data[i]);
		}

	      break;

	    case 8:
	      dml->cond_column_data[i] = ptr;
	      ptr = or_unpack_string_nocopy (ptr, &dml->cond_column_data[i]);
	      dml->cond_column_data_len[i] = (int) strlen (dml->cond_column_data[i]);
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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] data item type (%s)\n", data_item_type_to_string (data_item_type));
    }

  switch (data_item_type)
    {
    case DATA_ITEM_TYPE_DDL:
      if ((err_code = cubrid_log_make_ddl (data_info, &data_item->ddl)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, NULL);
	}

      break;

    case DATA_ITEM_TYPE_DML:
      if ((err_code = cubrid_log_make_dml (data_info, &data_item->dml)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, NULL);
	}

      break;

    case DATA_ITEM_TYPE_DCL:
      if ((err_code = cubrid_log_make_dcl (data_info, &data_item->dcl)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, NULL);
	}

      break;

    case DATA_ITEM_TYPE_TIMER:
      if ((err_code = cubrid_log_make_timer (data_info, &data_item->timer)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (err_code, NULL);
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

  ptr = *log_info;

  ptr = or_unpack_int (ptr, &log_info_len);

  ptr = or_unpack_int (ptr, &log_item->transaction_id);
  ptr = or_unpack_string_nocopy (ptr, &log_item->user);
  ptr = or_unpack_int (ptr, &log_item->data_item_type);

  if ((rc = cubrid_log_make_data_item (&ptr, (DATA_ITEM_TYPE) log_item->data_item_type, &log_item->data_item)) !=
      CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (rc, NULL);
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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] num_infos (%d), total_length (%d)\n", num_infos, total_length);
    }

  if (g_log_items_count < num_infos)
    {
      CUBRID_LOG_ITEM *tmp_log_items = NULL;

      tmp_log_items = (CUBRID_LOG_ITEM *) realloc ((void *) g_log_items, sizeof (CUBRID_LOG_ITEM) * num_infos);
      if (tmp_log_items == NULL)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_MALLOC,
				     "Malloc failed for storing temporary log items (size : %d)\n",
				     sizeof (CUBRID_LOG_ITEM) * num_infos);
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
	  CUBRID_LOG_ERROR_HANDLING (rc, NULL);
	}

      g_log_items[i].next = &g_log_items[i + 1];

      ptr = PTR_ALIGN (ptr, MAX_ALIGNMENT);
    }

  g_log_items[num_infos - 1].next = NULL;

  *log_item_list = g_log_items;
  *list_size = num_infos;

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[OUTPUT] list_size (%d)\n", *list_size);
    }

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

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] current stage (%d), lsa (%lld)\n", g_stage, *lsa);
    }

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION && g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE,
				 "stage required for this step : %d, but current stage : %d\n",
				 CUBRID_LOG_STAGE_EXTRACTION, g_stage);
    }

  if (lsa == NULL || log_item_list == NULL || list_size == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_OUT_PARAM,
				 "lsa (%s), log_item_list(%s), list_size(%s) must not be null\n",
				 lsa ? "not null" : "null", log_item_list ? "not null" : "null",
				 list_size ? "not null" : "null");
    }

  memcpy (&g_next_lsa, lsa, sizeof (LOG_LSA));

  rc = cubrid_log_extract_internal (&g_next_lsa, &num_infos, &total_length);

  if (rc != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (rc, NULL);
    }

  if ((rc = cubrid_log_make_log_item_list (num_infos, total_length, log_item_list, list_size)) != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (rc, NULL);
    }

  memcpy (lsa, &g_next_lsa, sizeof (uint64_t));

  g_stage = CUBRID_LOG_STAGE_EXTRACTION;

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[OUTPUT] lsa (%lld), list_size (%d)\n", *lsa, *list_size);
    }

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  return err_code;
}

static int
cubrid_log_clear_data_item (DATA_ITEM_TYPE data_item_type, CUBRID_DATA_ITEM * data_item)
{
  int err_code;

  if (g_trace_log_level == 1)
    {
      CUBRID_LOG_WRITE_TRACELOG ("[INPUT] data item type (%s)\n", data_item_type_to_string (data_item_type));
    }

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

  if (g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE,
				 "stage required for this step : %d, but current stage : %d\n",
				 CUBRID_LOG_STAGE_EXTRACTION, g_stage);
    }

  if (log_item_list == NULL)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_LOGITEM_LIST, "log_item_list must not be null\n");
    }

  for (i = 0; i < g_log_items_count; i++)	// if g_log_items_count == 0 then nothing to do
    {
      if ((rc =
	   cubrid_log_clear_data_item ((DATA_ITEM_TYPE) g_log_items[i].data_item_type,
				       &g_log_items[i].data_item)) != CUBRID_LOG_SUCCESS)
	{
	  CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_DEALLOC, NULL);
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

  if (css_send_request_with_data_buffer
      (g_conn_entry, NET_SERVER_CDC_END_SESSION, &rid, NULL, 0, reply, reply_size) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "Request(NET_SERVER_CDC_END_SESSION) failed. reply_sze (%d)\n", reply_size);
    }

  if (css_receive_data (g_conn_entry, rid, &recv_data, &recv_data_size, g_extraction_timeout * 1000) != NO_ERRORS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT,
				 "receive data from the request(NET_SERVER_CDC_END_SESSION) failed. (timeout : %d sec)\n",
				 g_extraction_timeout);
    }

  if (recv_data == NULL || recv_data_size != OR_INT_SIZE)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_CONNECT, "recv_data is %s | recv_data_size = %d (should be %d)\n",
				 recv_data ? "not null" : "null", recv_data_size, OR_INT_SIZE);
    }

  or_unpack_int (recv_data, &reply_code);

  if (reply_code != NO_ERROR)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_DISCONNECT, "Failed to disconnect. reply code : %d\n", reply_code);
    }

  if (recv_data != NULL && recv_data != reply)
    {
      free_and_init (recv_data);
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

  if (g_conn_entry != NULL)
    {
      css_free_conn (g_conn_entry);
      g_conn_entry = NULL;
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

  cubrid_log_reset_tracelog ();

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

  if (g_stage != CUBRID_LOG_STAGE_PREPARATION && g_stage != CUBRID_LOG_STAGE_EXTRACTION)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_INVALID_FUNC_CALL_STAGE,
				 "stage required for this step : %d or %d, but current stage : %d\n",
				 CUBRID_LOG_STAGE_PREPARATION, CUBRID_LOG_STAGE_EXTRACTION, g_stage);
    }

  if (cubrid_log_disconnect_server () != CUBRID_LOG_SUCCESS)
    {
      CUBRID_LOG_ERROR_HANDLING (CUBRID_LOG_FAILED_DISCONNECT, NULL);
    }

#if defined (WINDOWS)
  (void) css_windows_shutdown ();
#endif

  (void) cubrid_log_reset_globals ();

  g_stage = CUBRID_LOG_STAGE_CONFIGURATION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

#if defined (WINDOWS)
  (void) css_windows_shutdown ();
#endif

  (void) cubrid_log_reset_globals ();

  g_stage = CUBRID_LOG_STAGE_CONFIGURATION;

  return err_code;
}

#endif /* CS_MODE */
