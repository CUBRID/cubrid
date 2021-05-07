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

#include <limits.h>

#include "connection_cl.h"
#include "cubrid_log.h"

typedef enum
{
  CLOG_STAGE_CONFIGURATION,
  CLOG_STAGE_PREPARATION,
  CLOG_STAGE_EXTRACTION,
  CLOG_STAGE_FINALIZATION
} CLOG_STAGE;

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

extern int css_Service_id;

CLOG_STAGE g_stage = CLOG_STAGE_CONFIGURATION;

CSS_CONN_ENTRY *g_conn_entry;

int g_connection_timeout = 300;	/* min/max: -1/360 (sec) */
int g_extraction_timeout = 300;	/* min/max: -1/360 (sec) */
int g_max_log_item = 512;	/* min/max: 1/1024 */
bool g_all_in_cond = false;

uint64_t *g_extraction_table;
int g_extraction_table_size;

char **g_extraction_user;
int g_extraction_user_size;

FILE *g_trace_log;
char g_trace_log_path[PATH_MAX + 1] = "./";
int g_trace_log_level = 0;
int g_trace_log_filesize = 10;	/* MB */

/*
 * cubrid_log_set_connection_timeout () -
 *   return:
 *   timeout(in):
 */
int
cubrid_log_set_connection_timeout (int timeout)
{
  if (g_stage != CLOG_STAGE_CONFIGURATION)
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
  if (g_stage != CLOG_STAGE_CONFIGURATION)
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
  if (g_stage != CLOG_STAGE_CONFIGURATION)
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
  if (g_stage != CLOG_STAGE_CONFIGURATION)
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
  if (g_stage != CLOG_STAGE_CONFIGURATION)
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
  if (g_stage != CLOG_STAGE_CONFIGURATION)
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
  g_extraction_table_size = arr_size;

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

  if (g_stage != CLOG_STAGE_CONFIGURATION)
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

  g_extraction_user_size = arr_size;

  return CUBRID_LOG_SUCCESS;
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
  if (g_stage != CLOG_STAGE_CONFIGURATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (host == NULL)
    {
      return CUBRID_LOG_INVALID_HOST;
    }

  if (port > USHRT_MAX)
    {
      return CUBRID_LOG_INVALID_PORT;
    }

  if (dbname == NULL)
    {
      return CUBRID_LOG_INVALID_DBNAME;
    }

  css_Service_id = port;

  // The system parameter 'tcp_connection_timeout' needs to set because it is used in the css_connect_to_cubrid_server() function call tree.
  // However, I am not sure if calling this function is the right choice. I think I have to use lower-level functions.
  // So, I will change this process which is connecting to the server after analysis.
  g_conn_entry = css_connect_to_cubrid_server (host, dbname);
  if (g_conn_entry == NULL)
    {
      goto cubrid_log_error;
    }

  g_stage = CLOG_STAGE_PREPARATION;

  return CUBRID_LOG_SUCCESS;

cubrid_log_error:

  if (g_conn_entry != NULL)
    {
      css_free_conn (g_conn_entry);
      g_conn_entry = NULL;
    }

  return CUBRID_LOG_FAILED_CONNECT;
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
  if (g_stage != CLOG_STAGE_PREPARATION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (lsa == NULL)
    {
      return CUBRID_LOG_INVALID_OUT_PARAM;
    }

  return CUBRID_LOG_SUCCESS;
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
  if (g_stage != CLOG_STAGE_PREPARATION && g_stage != CLOG_STAGE_EXTRACTION)
    {
      return CUBRID_LOG_INVALID_FUNC_CALL_STAGE;
    }

  if (lsa == NULL || log_item_list == NULL || list_size == NULL)
    {
      return CUBRID_LOG_INVALID_OUT_PARAM;
    }

  g_stage = CLOG_STAGE_EXTRACTION;

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
  if (g_stage != CLOG_STAGE_EXTRACTION)
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

  if (g_stage != CLOG_STAGE_PREPARATION && g_stage != CLOG_STAGE_EXTRACTION)
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

  g_extraction_table_size = 0;

  if (g_extraction_user != NULL)
    {
      for (i = 0; i < g_extraction_user_size; i++)
	{
	  free (g_extraction_user[i]);
	}

      free (g_extraction_user);
      g_extraction_user = NULL;
    }

  g_extraction_user_size = 0;

  snprintf (g_trace_log_path, PATH_MAX + 1, "%s", "./");
  g_trace_log_level = 0;
  g_trace_log_filesize = 10;

  g_stage = CLOG_STAGE_CONFIGURATION;

  return CUBRID_LOG_SUCCESS;
}

#endif /* CS_MODE */
