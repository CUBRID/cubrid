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
 * broker_admin_so.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#endif /* WINDOWS */

#include "porting.h"
#include "cas_common.h"
#include "broker_admin_so.h"
#include "broker_filename.h"

#include "broker_config.h"
#include "broker_util.h"
#include "broker_admin_pub.h"
#include "broker_shm.h"
#include "broker_process_size.h"
#include "broker_process_info.h"
#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

#define UC_CONF_PARAM_MASTER_SHM_ID		"MASTER_SHM_ID"
#define UC_CONF_PARAM_ADMIN_LOG_FILE		"ADMIN_LOG_FILE"

#define UC_CONF_PARAM_BROKER_NAME		"%"
#define UC_CONF_PARAM_SERVICE			"SERVICE"
#define UC_CONF_PARAM_APPL_SERVER		"APPL_SERVER"
#define UC_CONF_PARAM_BROKER_PORT		"BROKER_PORT"
#define UC_CONF_PARAM_MIN_NUM_APPL_SERVER	"MIN_NUM_APPL_SERVER"
#define UC_CONF_PARAM_MAX_NUM_APPL_SERVER	"MAX_NUM_APPL_SERVER"
#define UC_CONF_PARAM_AUTO_ADD_APPL_SERVER	"AUTO_ADD_APPL_SERVER"
#define UC_CONF_PARAM_APPL_SERVER_SHM_ID	"APPL_SERVER_SHM_ID"
#define UC_CONF_PARAM_APPL_SERVER_MAX_SIZE	"APPL_SERVER_MAX_SIZE"
#define UC_CONF_PARAM_APPL_SERVER_HARD_LIMIT	"APPL_SERVER_MAX_SIZE_HARD_LIMIT"
#define UC_CONF_PARAM_LOG_DIR			"LOG_DIR"
#define UC_CONF_PARAM_SLOW_LOG_DIR		"SLOW_LOG_DIR"
#define UC_CONF_PARAM_ERROR_LOG_DIR		"ERROR_LOG_DIR"
#define UC_CONF_PARAM_LOG_BACKUP		"LOG_BACKUP"
#define UC_CONF_PARAM_SOURCE_ENV		"SOURCE_ENV"
#define UC_CONF_PARAM_ACCESS_LOG		"ACCESS_LOG"
#define UC_CONF_PARAM_SQL_LOG			"SQL_LOG"
#define UC_CONF_PARAM_SLOW_LOG			"SLOW_LOG"
#define UC_CONF_PARAM_LONG_QUERY_TIME           "LONG_QUERY_TIME"
#define UC_CONF_PARAM_LONG_TRANSACTION_TIME     "LONG_TRANSACTION_TIME"
#define UC_CONF_PARAM_TIME_TO_KILL		"TIME_TO_KILL"
#define UC_CONF_PARAM_SESSION_TIMEOUT		"SESSION_TIMEOUT"
#define UC_CONF_PARAM_JOB_QUEUE_SIZE		"JOB_QUEUE_SIZE"
#define UC_CONF_PARAM_ACCESS_LIST		"ACCESS_LIST"
#define UC_CONF_PARAM_SQL_LOG2			"SQL_LOG2"
#define UC_CONF_PARAM_MAX_STRING_LENGTH		"MAX_STRING_LENGTH"
#define UC_CONF_PARAM_STRIPPED_COLUMN_NAME	"STRIPPED_COLUMN_NAME"
#define UC_CONF_PARAM_KEEP_CONNECTION		"KEEP_CONNECTION"
#define UC_CONF_PARAM_SQL_LOG_MAX_SIZE		"SQL_LOG_MAX_SIZE"

#define MAX_NUM_CONF	35

#define SET_CONF_ITEM(CONF_ITEM, IDX, NAME, VALUE)	\
	do {						\
	  (CONF_ITEM)[IDX].name = NAME;			\
	  (CONF_ITEM)[IDX].value = VALUE;		\
	  (IDX)++;					\
	} while (0)

#define SET_CONF_ITEM_ONOFF(CONF_ITEM, IDX, NAME, VALUE)	\
	do {							\
	  if ((VALUE) == ON)					\
	    SET_CONF_ITEM(CONF_ITEM, IDX, NAME, strdup("ON"));	\
	  else							\
	    SET_CONF_ITEM(CONF_ITEM, IDX, NAME, strdup("OFF"));	\
	} while (0)

#define SET_CONF_ITEM_SQL_LOG_MODE(CONF_ITEM, IDX, NAME, VALUE) \
        do {                                                    \
          const char *_macro_tmp_ptr;                           \
          if ((VALUE) & SQL_LOG_MODE_ALL)                        \
            _macro_tmp_ptr = "ALL";                              \
          else if ((VALUE) & SQL_LOG_MODE_ERROR)                \
            _macro_tmp_ptr = "ERROR";                           \
          else /*((VALUE) == SQL_LOG_MODE_NONE)*/                \
            _macro_tmp_ptr = "NONE";                             \
          SET_CONF_ITEM(CONF_ITEM, IDX, NAME, strdup(_macro_tmp_ptr));  \
        } while (0)

#define SET_CONF_ITEM_KEEP_CON(CONF_ITEM, IDX, NAME, VALUE)             \
        do {								\
          const char *_macro_tmp_ptr;						\
          if ((VALUE) == KEEP_CON_AUTO)					\
            _macro_tmp_ptr = "AUTO";					\
          else								\
            _macro_tmp_ptr = "ON";					\
	  SET_CONF_ITEM(CONF_ITEM, IDX, NAME, strdup(_macro_tmp_ptr));	\
        } while (0)

#define FMT_D "%d"
#define FMT_X "%X"
#define SET_CONF_ITEM_INT(CONF_ITEM, IDX, NAME, VALUE, FMT)	\
	do {						\
	  char	_macro_tmp_buf[32];			\
	  sprintf(_macro_tmp_buf, FMT, (VALUE));	\
	  SET_CONF_ITEM(CONF_ITEM, IDX, NAME, strdup(_macro_tmp_buf));	\
	} while (0)

#define SET_CONF_ITEM_STR(CONF_ITEM, IDX, NAME, VALUE)		\
	do {							\
	  SET_CONF_ITEM(CONF_ITEM, IDX, NAME, strdup(VALUE));	\
	} while (0)

#define SET_FLAG(ONOFF)		((ONOFF) == ON ? FLAG_ON : FLAG_OFF)

#define CP_ADMIN_ERR_MSG(BUF)	sprintf(BUF, "Error: %s", admin_err_msg)

static void admin_log_write (const char *log_file, const char *msg);
static int admin_common (T_BROKER_INFO * br_info, int *num_broker, int *master_shm_id, char *admin_log_file,
			 char *err_msg, char admin_flag, bool * acl_flag, char *acl_file);
static int copy_job_info (T_JOB_INFO ** job_info, T_MAX_HEAP_NODE * job_q);
static int conf_copy_header (T_UC_CONF * unicas_conf, int master_shm_id, char *admin_log_file, char *err_msg);
static int conf_copy_broker (T_UC_CONF * unicas_conf, T_BROKER_INFO * br_conf, int num_br, char *err_msg);
static void conf_item_free (T_UC_CONF_ITEM * conf_item, int num);
static char *get_broker_name (T_BR_CONF * br_conf);
static const char *get_as_type_str (char as_type);
static void change_conf_as_type (T_BR_CONF * br_conf, int old_as_type, int new_as_type);
static void reset_conf_value (int num_item, T_UC_CONF_ITEM * item, const char *name);
static int get_as_type (const char *type_str);
static int uc_changer_internal (const char *br_name, const char *name, const char *value, int as_number, char *err_msg);

#define CHECK_SHARED_MEMORY(p_shm, err_msg)             \
    do {                                                \
        if (!p_shm) {                                   \
            if (err_msg)  strcpy(err_msg, "Shared memory is not opened."); \
            return -1;                                  \
        }                                               \
    } while (0)

DLL_EXPORT void *
uc_broker_shm_open (char *err_msg)
{
  T_BROKER_INFO br_conf[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  void *ret_val;

  if (admin_common (br_conf, &num_broker, &master_shm_id, admin_log_file, err_msg, 0, NULL, NULL) < 0)
    {
      return NULL;
    }
  ret_val = uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (!ret_val && err_msg)
    {
      strcpy (err_msg, strerror (errno));
    }
  return ret_val;
}

DLL_EXPORT int
uc_get_br_num_with_opened_shm (void *shm_br, char *err_msg)
{
  CHECK_SHARED_MEMORY (shm_br, err_msg);
  return ((T_SHM_BROKER *) shm_br)->num_broker;
}

DLL_EXPORT int
uc_get_br_name_with_opened_shm (void *shm_br, int br_index, char *name, int buffer_size, char *err_msg)
{
  CHECK_SHARED_MEMORY (shm_br, err_msg);
  if (name == NULL)
    {
      if (err_msg)
	{
	  strcpy (err_msg, "Invalid name buffer.");
	}
      return -1;
    }

  strncpy (name, ((T_SHM_BROKER *) shm_br)->br_info[br_index].name, buffer_size);
  return 1;
}

DLL_EXPORT void *
uc_as_shm_open (void *shm_br, int br_index, char *err_msg)
{
  void *ret_val;

  if (shm_br == NULL)
    {
      if (err_msg)
	{
	  strcpy (err_msg, "Shared memory is not opened.");
	}
      return NULL;
    }

  ret_val = uw_shm_open (((T_SHM_BROKER *) shm_br)->br_info[br_index].appl_server_shm_id, SHM_APPL_SERVER,
			 SHM_MODE_MONITOR);
  if (!ret_val && err_msg)
    {
      strcpy (err_msg, strerror (errno));
    }
  return ret_val;
}

DLL_EXPORT int
uc_get_as_num_with_opened_shm (void *shm_br, int br_index, char *err_msg)
{
  CHECK_SHARED_MEMORY (shm_br, err_msg);
  return ((T_SHM_BROKER *) shm_br)->br_info[br_index].appl_server_max_num;
}

DLL_EXPORT int
uc_get_as_reqs_received_with_opened_shm (void *shm_as, long long array[], int array_size, char *err_msg)
{
  /* return value -2 means that array_size is different from real as num (as add or remove ...) so coller must reset
   * array[] info and after, recall this function */
  int i;

  CHECK_SHARED_MEMORY (shm_as, err_msg);
/*
  if (array_size != ((T_SHM_APPL_SERVER*)shm_as)->num_appl_server) {
    if (err_msg) strcpy(err_msg, "Invalid array_size.");
    return -2;
  }
*/
  for (i = 0; i < array_size; i++)
    {
      array[i] = ((T_SHM_APPL_SERVER *) shm_as)->as_info[i].num_requests_received;
    }

  return 1;
}

DLL_EXPORT int
uc_get_active_session_with_opened_shm (void *shm_p, char *err_msg)
{
  T_SHM_BROKER *shm_br = (T_SHM_BROKER *) shm_p;
  int num_ses = 0, i;

  CHECK_SHARED_MEMORY (shm_br, err_msg);

  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].service_flag == ON)
	{
	  num_ses += shm_br->br_info[i].num_busy_count;
	}
    }

  return num_ses;
}

DLL_EXPORT int
uc_get_as_tran_processed_with_opened_shm (void *shm_as, long long array[], int array_size, char *err_msg)
{
  int i;

  CHECK_SHARED_MEMORY (shm_as, err_msg);

  for (i = 0; i < array_size; i++)
    {
      array[i] = ((T_SHM_APPL_SERVER *) shm_as)->as_info[i].num_transactions_processed;
    }

  return 1;
}

DLL_EXPORT int
uc_get_as_query_processed_with_opened_shm (void *shm_as, long long array[], int array_size, char *err_msg)
{
  int i;

  CHECK_SHARED_MEMORY (shm_as, err_msg);

  for (i = 0; i < array_size; i++)
    {
      array[i] = ((T_SHM_APPL_SERVER *) shm_as)->as_info[i].num_queries_processed;
    }

  return 1;
}

DLL_EXPORT void
uc_shm_detach (void *p)
{
  uw_shm_detach (p);
}

DLL_EXPORT const char *
uc_version ()
{
  return makestring (BUILD_NUMBER);
}

DLL_EXPORT int
uc_start (char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  char acl_file[BROKER_PATH_MAX];
  bool acl_flag;

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, &acl_flag, acl_file) < 0)
    {
      return -1;
    }

  if (admin_start_cmd (br_info, num_broker, master_shm_id, acl_flag, acl_file, admin_log_file) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  admin_log_write (admin_log_file, "start");

  return 0;
}

DLL_EXPORT int
uc_stop (char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_stop_cmd (master_shm_id) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  admin_log_write (admin_log_file, "stop");

  return 0;
}

DLL_EXPORT int
uc_add (const char *br_name, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  char msg_buf[256];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_add_cmd (master_shm_id, br_name) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  sprintf (msg_buf, "add %s", br_name);
  admin_log_write (admin_log_file, msg_buf);

  return 0;
}

DLL_EXPORT int
uc_restart (const char *br_name, int as_index, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  char msg_buf[256];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_restart_cmd (master_shm_id, br_name, as_index) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  sprintf (msg_buf, "restart %s %d", br_name, as_index);
  admin_log_write (admin_log_file, msg_buf);

  return 0;
}

DLL_EXPORT int
uc_drop (const char *br_name, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[256];
  char msg_buf[256];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_drop_cmd (master_shm_id, br_name) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  sprintf (msg_buf, "drop %s", br_name);
  admin_log_write (admin_log_file, msg_buf);

  return 0;
}

DLL_EXPORT int
uc_on (const char *br_name, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  char msg_buf[256];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_on_cmd (master_shm_id, br_name) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  sprintf (msg_buf, "%s on", br_name);
  admin_log_write (admin_log_file, msg_buf);

  return 0;
}

DLL_EXPORT int
uc_off (const char *br_name, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  char msg_buf[256];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 1, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_off_cmd (master_shm_id, br_name) < 0)
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }

  sprintf (msg_buf, "%s off", br_name);
  admin_log_write (admin_log_file, msg_buf);

  return 0;
}

DLL_EXPORT int
uc_as_info (const char *br_name, T_AS_INFO ** ret_as_info, T_JOB_INFO ** job_info, int *num_job, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int br_index, i, appl_shm_key;
  int num_as;
  T_AS_INFO *as_info = NULL;
  char client_ip_str[16];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 0, NULL, NULL) < 0)
    {
      return -1;
    }

  shm_br = (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      strcpy (err_msg, "Error: shared memory open error");
      return -1;
    }
  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (br_name, shm_br->br_info[i].name) == 0)
	{
	  appl_shm_key = shm_br->br_info[i].appl_server_shm_id;
	  br_index = i;
	  break;
	}
    }
  if (br_index < 0)
    {
      sprintf (err_msg, "Error: cannot find broker [%s]", br_name);
      goto as_info_error;
    }
  shm_appl = (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key, SHM_APPL_SERVER, SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      strcpy (err_msg, "Error: shared memory open error");
      goto as_info_error;
    }

  num_as = shm_br->br_info[br_index].appl_server_max_num;
  as_info = (T_AS_INFO *) malloc (sizeof (T_AS_INFO) * num_as);
  if (as_info == NULL)
    {
      strcpy (err_msg, strerror (errno));
      goto as_info_error;
    }
  memset (as_info, 0, sizeof (T_AS_INFO) * num_as);

  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
    {
      T_PSINFO proc_info;

      as_info[i].service_flag = shm_appl->as_info[i].service_flag;
      if (shm_appl->as_info[i].service_flag != SERVICE_ON)
	{
	  continue;
	}

      as_info[i].pid = shm_appl->as_info[i].pid;
      as_info[i].psize = getsize (as_info[i].pid);
      memset (&proc_info, 0, sizeof (T_PSINFO));
      get_psinfo (as_info[i].pid, &proc_info);
      as_info[i].num_thr = proc_info.num_thr;
      as_info[i].cpu_time = proc_info.cpu_time;
      as_info[i].pcpu = proc_info.pcpu;
      as_info[i].num_request = shm_appl->as_info[i].num_request;
#if defined(WINDOWS)
      if (shm_appl->use_pdh_flag == TRUE)
	{
	  float pct_cpu;

	  pct_cpu = shm_appl->as_info[i].pdh_pct_cpu;
	  if (pct_cpu >= 0)
	    {
	      as_info[i].pcpu = pct_cpu;
	    }
	  as_info[i].psize = shm_appl->as_info[i].pdh_workset;
	  as_info[i].cpu_time = shm_appl->as_info[i].cpu_time;
	}
#endif /* WINDOWS */
#if defined(WINDOWS)
      as_info[i].as_port = shm_appl->as_info[i].as_port;
#else /* WINDOWS */
      as_info[i].as_port = 0;
#endif /* WINDOWS */
      if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY)
	{
	  if (IS_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server))
	    {
	      if (shm_appl->as_info[i].con_status == CON_STATUS_OUT_TRAN)
		{
		  as_info[i].status = AS_STATUS_CLOSE_WAIT;
		}
	      else if (shm_appl->as_info[i].log_msg[0] == '\0')
		{
		  as_info[i].status = AS_STATUS_CLIENT_WAIT;
		}
	      else
		{
		  as_info[i].status = AS_STATUS_BUSY;
		}
	    }
	  else
	    {
	      as_info[i].status = AS_STATUS_BUSY;
	    }
	}
#if defined(WINDOWS)
      else if (shm_appl->as_info[i].uts_status == UTS_STATUS_BUSY_WAIT)
	{
	  as_info[i].status = AS_STATUS_BUSY;
	}
#endif /* WINDOWS */
      else if (shm_appl->as_info[i].uts_status == UTS_STATUS_RESTART)
	{
	  as_info[i].status = AS_STATUS_RESTART;
	}
      else
	{
	  as_info[i].status = AS_STATUS_IDLE;
	}

      as_info[i].last_access_time = shm_appl->as_info[i].last_access_time;
      as_info[i].last_connect_time = shm_appl->as_info[i].last_connect_time;
      as_info[i].num_requests_received = shm_appl->as_info[i].num_requests_received;
      as_info[i].num_queries_processed = shm_appl->as_info[i].num_queries_processed;
      as_info[i].num_transactions_processed = shm_appl->as_info[i].num_transactions_processed;
      as_info[i].num_long_queries = shm_appl->as_info[i].num_long_queries;
      as_info[i].num_long_transactions = shm_appl->as_info[i].num_long_transactions;
      as_info[i].num_error_queries = shm_appl->as_info[i].num_error_queries;
      as_info[i].num_interrupts = shm_appl->as_info[i].num_interrupts;

      ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str), shm_appl->as_info[i].cas_clt_ip);
      strncpy (as_info[i].clt_ip_addr, client_ip_str, sizeof (as_info[i].clt_ip_addr) - 1);

      strncpy (as_info[i].database_host, shm_appl->as_info[i].database_host, sizeof (as_info[i].database_host) - 1);
      strncpy (as_info[i].database_name, shm_appl->as_info[i].database_name, sizeof (as_info[i].database_name) - 1);

      strncpy (as_info[i].clt_appl_name, shm_appl->as_info[i].clt_appl_name, sizeof (as_info[i].clt_appl_name) - 1);
      strncpy (as_info[i].request_file, shm_appl->as_info[i].clt_req_path_info, sizeof (as_info[i].request_file) - 1);

      strncpy (as_info[i].log_msg, shm_appl->as_info[i].log_msg, sizeof (as_info[i].log_msg) - 1);
    }

  if (job_info)
    {
      T_MAX_HEAP_NODE job_q[JOB_QUEUE_MAX_SIZE + 1];

      memcpy (job_q, shm_appl->job_queue, sizeof (T_MAX_HEAP_NODE) * (JOB_QUEUE_MAX_SIZE + 1));
      *num_job = copy_job_info (job_info, job_q);
    }
  else if (num_job)
    {
      *num_job = shm_appl->job_queue[0].id;
    }

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);

  *ret_as_info = as_info;
  return (num_as);

as_info_error:
  if (shm_appl)
    {
      uw_shm_detach (shm_appl);
    }
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }
  uc_info_free (as_info);
  return -1;
}

DLL_EXPORT void
uc_info_free (void *info)
{
  FREE_MEM (info);
}

DLL_EXPORT int
uc_br_info (T_BR_INFO ** ret_br_info, char *err_msg)
{
  T_BROKER_INFO br_conf[MAX_BROKER_NUM];
  T_SHM_BROKER *shm_br;
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];
  int i, num_br;
  T_BR_INFO *br_info = NULL;
  char *p;

  if (admin_common (br_conf, &num_broker, &master_shm_id, admin_log_file, err_msg, 0, NULL, NULL) < 0)
    {
      return -1;
    }

  shm_br = (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      strcpy (err_msg, "ERROR: shared memory open error");
      return -1;
    }
  num_br = shm_br->num_broker;
  if (num_br < 1 || num_br > MAX_BROKER_NUM)
    {
      goto br_info_error;
    }
  br_info = (T_BR_INFO *) malloc (sizeof (T_BR_INFO) * num_br);
  if (br_info == NULL)
    {
      goto br_info_error;
    }
  memset (br_info, 0, sizeof (T_BR_INFO) * num_br);
  for (i = 0; i < num_br; i++)
    {
      strncpy (br_info[i].name, shm_br->br_info[i].name, sizeof (br_info[i].name) - 1);
      strcpy (br_info[i].as_type, get_as_type_str (shm_br->br_info[i].appl_server));
      br_info[i].status = SET_FLAG (shm_br->br_info[i].service_flag);
      if (shm_br->br_info[i].service_flag == ON)
	{
#if !defined(WINDOWS)
	  T_PSINFO proc_info;
#endif /* !WINDOWS */
	  int num_req, j;
	  INT64 num_tran, num_query, num_long_query, num_long_tran, num_error_query, num_interrupts;
	  T_SHM_APPL_SERVER *shm_appl;

	  shm_appl =
	    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[i].appl_server_shm_id, SHM_APPL_SERVER, SHM_MODE_ADMIN);
	  if (shm_appl == NULL)
	    {
	      strcpy (err_msg, "ERROR: shared memory open error");
	      goto br_info_error;
	    }

	  br_info[i].pid = shm_br->br_info[i].pid;
	  br_info[i].port = shm_br->br_info[i].port;
	  br_info[i].num_as = shm_br->br_info[i].appl_server_num;
	  br_info[i].max_as = shm_br->br_info[i].appl_server_max_num;
	  br_info[i].min_as = shm_br->br_info[i].appl_server_min_num;
	  br_info[i].num_job_q = shm_appl->job_queue[0].id;
	  br_info[i].num_busy_count = shm_br->br_info[i].num_busy_count;
#if defined(WINDOWS)
	  br_info[i].num_thr = shm_br->br_info[i].pdh_num_thr;
	  br_info[i].pcpu = shm_br->br_info[i].pdh_pct_cpu;
	  br_info[i].cpu_time = shm_br->br_info[i].cpu_time;
#else /* WINDOWS */
	  memset (&proc_info, 0, sizeof (proc_info));
	  get_psinfo (br_info[i].pid, &proc_info);
	  br_info[i].num_thr = proc_info.num_thr;
	  br_info[i].pcpu = proc_info.pcpu;
	  br_info[i].cpu_time = proc_info.cpu_time;
#endif /* WINDOWS */

	  num_req = 0;
	  num_tran = 0;
	  num_query = 0;
	  num_long_tran = 0;
	  num_long_query = 0;
	  num_error_query = 0;
	  num_interrupts = 0;
	  for (j = 0; j < shm_br->br_info[i].appl_server_max_num; j++)
	    {
	      num_req += shm_appl->as_info[j].num_request;
	      num_tran += shm_appl->as_info[j].num_transactions_processed;
	      num_query += shm_appl->as_info[j].num_queries_processed;
	      num_long_query += shm_appl->as_info[j].num_long_queries;
	      num_long_tran += shm_appl->as_info[j].num_long_transactions;
	      num_error_query += shm_appl->as_info[j].num_error_queries;
	      num_interrupts += shm_appl->as_info[j].num_interrupts;
	    }

	  br_info[i].num_req = num_req;
	  br_info[i].num_tran = num_tran;
	  br_info[i].num_query = num_query;
	  br_info[i].num_long_query = num_long_query;
	  br_info[i].num_long_tran = num_long_tran;
	  br_info[i].num_error_query = num_error_query;
	  br_info[i].num_interrupts = num_interrupts;

	  br_info[i].keep_connection = shm_appl->keep_connection;

	  br_info[i].shm_id = shm_br->br_info[i].appl_server_shm_id;
	  br_info[i].session_timeout = shm_br->br_info[i].session_timeout;
	  br_info[i].auto_add_flag = SET_FLAG (shm_br->br_info[i].auto_add_appl_server);
	  br_info[i].sql_log_mode = shm_br->br_info[i].sql_log_mode;
	  br_info[i].slow_log_mode = shm_br->br_info[i].slow_log_mode;
	  br_info[i].access_mode = shm_br->br_info[i].access_mode;
	  br_info[i].long_query_time = shm_br->br_info[i].long_query_time;
	  br_info[i].long_transaction_time = shm_br->br_info[i].long_transaction_time;
	  strncpy (br_info[i].log_dir, shm_br->br_info[i].access_log_file, sizeof (br_info[i].log_dir) - 1);
	  p = strrchr (br_info[i].log_dir, '/');
	  if (p != NULL)
	    {
	      *p = '\0';
	    }
	  br_info[i].as_max_size = shm_br->br_info[i].appl_server_max_size;
	  br_info[i].as_hard_limit = shm_br->br_info[i].appl_server_hard_limit;
	  br_info[i].log_backup_flag = shm_br->br_info[i].log_backup;
	  br_info[i].time_to_kill = shm_br->br_info[i].time_to_kill;
	  uw_shm_detach (shm_appl);
	}
      if (shm_br->br_info[i].acl_file[0] == '\0')
	{
	  br_info[i].access_list_flag = 0;
	}
      else
	{
	  br_info[i].access_list_flag = 1;
	}

      if (shm_br->br_info[i].source_env[0] == '\0')
	{
	  br_info[i].source_env_flag = 0;
	}
      else
	{
	  br_info[i].source_env_flag = 1;
	}
    }
  uw_shm_detach (shm_br);

  *ret_br_info = br_info;

  return (num_br);

br_info_error:
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }
  if (br_info)
    {
      uc_info_free (br_info);
    }
  return -1;
}

DLL_EXPORT int
uc_unicas_conf (T_UC_CONF * unicas_conf, int *ret_mst_shmid, char *err_msg)
{
  T_BROKER_INFO br_conf[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];

  memset (unicas_conf, 0, sizeof (T_UC_CONF));

  if (admin_common (br_conf, &num_broker, &master_shm_id, admin_log_file, err_msg, 0, NULL, NULL) < 0)
    {
      return -1;
    }

  if (conf_copy_header (unicas_conf, master_shm_id, admin_log_file, err_msg) < 0)
    {
      return -1;
    }

  if (conf_copy_broker (unicas_conf, br_conf, num_broker, err_msg) < 0)
    {
      return -1;
    }

  if (ret_mst_shmid)
    *ret_mst_shmid = master_shm_id;

  return 0;
}

DLL_EXPORT void
uc_unicas_conf_free (T_UC_CONF * unicas_conf)
{
  int i;

  conf_item_free (unicas_conf->header_conf, unicas_conf->num_header);
  if (unicas_conf->br_conf)
    {
      for (i = 0; i < unicas_conf->num_broker; i++)
	{
	  conf_item_free (unicas_conf->br_conf[i].item, unicas_conf->br_conf[i].num);
	}
      FREE_MEM (unicas_conf->br_conf);
    }
  memset (unicas_conf, 0, sizeof (T_UC_CONF));
}

DLL_EXPORT int
uc_conf_broker_add (T_UC_CONF * unicas_conf, const char *br_name, char *err_msg)
{
  int num_br, n;
  T_UC_CONF_ITEM *conf_item;

  num_br = unicas_conf->num_broker + 1;
  unicas_conf->br_conf = (T_BR_CONF *) realloc (unicas_conf->br_conf, sizeof (T_BR_CONF) * num_br);
  if (unicas_conf->br_conf == NULL)
    {
      strcpy (err_msg, strerror (errno));
      return -1;
    }
  memset (&(unicas_conf->br_conf[num_br - 1]), 0, sizeof (T_BR_CONF));
  unicas_conf->num_broker = num_br;

  conf_item = (T_UC_CONF_ITEM *) malloc (sizeof (T_UC_CONF_ITEM) * MAX_NUM_CONF);
  if (conf_item == NULL)
    {
      strcpy (err_msg, strerror (errno));
      return -1;
    }
  memset (conf_item, 0, sizeof (T_UC_CONF_ITEM) * MAX_NUM_CONF);

  n = 0;
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_BROKER_NAME, br_name);
  SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_SERVICE, ON);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_APPL_SERVER, get_as_type_str (APPL_SERVER_CAS));
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_BROKER_PORT, 0, FMT_D);
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MIN_NUM_APPL_SERVER, 2, FMT_D);
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MAX_NUM_APPL_SERVER, 10, FMT_D);
  SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_AUTO_ADD_APPL_SERVER, OFF);
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_APPL_SERVER_SHM_ID, 0, FMT_X);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_APPL_SERVER_MAX_SIZE, DEFAULT_SERVER_MAX_SIZE);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_LOG_DIR, DEFAULT_LOG_DIR);
  SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_LOG_BACKUP, OFF);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_SOURCE_ENV, "");
  SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_ACCESS_LOG, ON);
  SET_CONF_ITEM_SQL_LOG_MODE (conf_item, n, UC_CONF_PARAM_SQL_LOG, SQL_LOG_MODE_ERROR);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_LONG_QUERY_TIME, DEFAULT_LONG_QUERY_TIME);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_LONG_TRANSACTION_TIME, DEFAULT_LONG_TRANSACTION_TIME);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_TIME_TO_KILL, DEFAULT_TIME_TO_KILL);
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_SESSION_TIMEOUT, 30, FMT_D);
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_JOB_QUEUE_SIZE, 30, FMT_D);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_ACCESS_LIST, "");
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MAX_STRING_LENGTH, -1, FMT_D);
  SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_STRIPPED_COLUMN_NAME, ON);
  SET_CONF_ITEM_KEEP_CON (conf_item, n, UC_CONF_PARAM_KEEP_CONNECTION, KEEP_CON_DEFAULT);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_SQL_LOG_MAX_SIZE, DEFAULT_SQL_LOG_MAX_SIZE);

  unicas_conf->br_conf[num_br - 1].num = n;
  unicas_conf->br_conf[num_br - 1].item = conf_item;
  return 0;
}

DLL_EXPORT void
uc_change_config (T_UC_CONF * unicas_conf, const char *br_name, const char *name, const char *value)
{
  T_BR_CONF *br_conf = NULL;
  int i;

  for (i = 0; i < unicas_conf->num_broker; i++)
    {
      br_conf = &(unicas_conf->br_conf[i]);
      if (strcasecmp (br_name, get_broker_name (br_conf)) == 0)
	{
	  break;
	}
      br_conf = NULL;
    }
  if (br_conf == NULL)
    {
      return;
    }

  for (i = 0; i < br_conf->num; i++)
    {
      if (strcasecmp (name, br_conf->item[i].name) == 0)
	{
	  if (strcasecmp (name, UC_CONF_PARAM_APPL_SERVER) == 0)
	    {
	      change_conf_as_type (br_conf, get_as_type (br_conf->item[i].value), get_as_type (value));
	    }
	  FREE_MEM (br_conf->item[i].value);
	  br_conf->item[i].value = strdup (value);
	  break;
	}
    }
}

DLL_EXPORT int
uc_changer (const char *br_name, const char *name, const char *value, char *err_msg)
{
  return uc_changer_internal (br_name, name, value, -1, err_msg);
}

DLL_EXPORT int
uc_cas_changer (const char *br_name, const char *name, const char *value, int as_number, char *err_msg)
{
  return uc_changer_internal (br_name, name, value, as_number, err_msg);
}

DLL_EXPORT int
uc_del_cas_log (const char *br_name, int asid, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];

  if (err_msg == NULL)
    {
      return -1;
    }

  err_msg[0] = '\0';

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 0, NULL, NULL) < 0)
    {
      return -1;
    }

  if (admin_del_cas_log (master_shm_id, br_name, asid) < 0)
    {
      return -1;
    }

  return 0;
}

/******************************************************************
  private functions
 ******************************************************************/

static char *
get_broker_name (T_BR_CONF * br_conf)
{
  int i, num;
  T_UC_CONF_ITEM *conf_item;

  conf_item = br_conf->item;
  num = br_conf->num;

  for (i = 0; i < num; i++)
    {
      if (strcmp (conf_item[i].name, UC_CONF_PARAM_BROKER_NAME) == 0)
	{
	  if (conf_item[i].value == NULL)
	    {
	      return (char *) "";
	    }
	  return conf_item[i].value;
	}
    }
  return (char *) "";
}

static void
conf_item_free (T_UC_CONF_ITEM * conf_item, int num)
{
  int i;

  if (conf_item == NULL)
    {
      return;
    }
  for (i = 0; i < num; i++)
    {
      FREE_MEM (conf_item[i].value);
    }
  FREE_MEM (conf_item);
}

static int
conf_copy_broker (T_UC_CONF * unicas_conf, T_BROKER_INFO * br_conf, int num_br, char *err_msg)
{
  int i, n;
  T_UC_CONF_ITEM *conf_item;

  unicas_conf->br_conf = (T_BR_CONF *) malloc (sizeof (T_BR_CONF) * num_br);
  if (unicas_conf->br_conf == NULL)
    {
      strcpy (err_msg, strerror (errno));
      return -1;
    }
  memset (unicas_conf->br_conf, 0, sizeof (T_BR_CONF) * num_br);
  unicas_conf->num_broker = num_br;

  for (i = 0; i < num_br; i++)
    {
      char as_type;
      conf_item = (T_UC_CONF_ITEM *) malloc (sizeof (T_UC_CONF_ITEM) * MAX_NUM_CONF);
      if (conf_item == NULL)
	{
	  strcpy (err_msg, strerror (errno));
	  return -1;
	}
      memset (conf_item, 0, sizeof (T_UC_CONF_ITEM) * MAX_NUM_CONF);
      as_type = br_conf[i].appl_server;
      n = 0;
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_BROKER_NAME, br_conf[i].name);
      SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_SERVICE, br_conf[i].service_flag);
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_APPL_SERVER, get_as_type_str (br_conf[i].appl_server));
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_BROKER_PORT, br_conf[i].port, FMT_D);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MIN_NUM_APPL_SERVER, br_conf[i].appl_server_min_num, FMT_D);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MAX_NUM_APPL_SERVER, br_conf[i].appl_server_max_num, FMT_D);
      SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_AUTO_ADD_APPL_SERVER, br_conf[i].auto_add_appl_server);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_APPL_SERVER_SHM_ID, br_conf[i].appl_server_shm_id, FMT_X);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_APPL_SERVER_MAX_SIZE, br_conf[i].appl_server_max_size / ONE_K,
			 FMT_D);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_APPL_SERVER_HARD_LIMIT, br_conf[i].appl_server_hard_limit / ONE_K,
			 FMT_D);
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_LOG_DIR, br_conf[i].log_dir);
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_SLOW_LOG_DIR, br_conf[i].slow_log_dir);
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_ERROR_LOG_DIR, br_conf[i].err_log_dir);
      SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_LOG_BACKUP, br_conf[i].log_backup);
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_SOURCE_ENV, br_conf[i].source_env);
      SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_ACCESS_LOG, br_conf[i].access_log);
      SET_CONF_ITEM_SQL_LOG_MODE (conf_item, n, UC_CONF_PARAM_SQL_LOG, br_conf[i].sql_log_mode);
      SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_SLOW_LOG, br_conf[i].slow_log_mode);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_SESSION_TIMEOUT, br_conf[i].session_timeout, FMT_D);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_SQL_LOG_MAX_SIZE, br_conf[i].sql_log_max_size, FMT_D);
      SET_CONF_ITEM_KEEP_CON (conf_item, n, UC_CONF_PARAM_KEEP_CONNECTION, br_conf[i].keep_connection);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_TIME_TO_KILL, br_conf[i].time_to_kill, FMT_D);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_SESSION_TIMEOUT, br_conf[i].session_timeout, FMT_D);
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_JOB_QUEUE_SIZE, br_conf[i].job_queue_size, FMT_D);
      if (IS_APPL_SERVER_TYPE_CAS (as_type))
	{
	  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MAX_STRING_LENGTH, br_conf[i].max_string_length, FMT_D);
	  SET_CONF_ITEM_ONOFF (conf_item, n, UC_CONF_PARAM_STRIPPED_COLUMN_NAME, br_conf[i].stripped_column_name);
	}
      SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_ACCESS_LIST, br_conf[i].acl_file);
#if 0
      SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_SQL_LOG2, br_conf[i].sql_log2, FMT_D);
#endif

      unicas_conf->br_conf[i].num = n;
      unicas_conf->br_conf[i].item = conf_item;
    }
  return 0;
}

static int
conf_copy_header (T_UC_CONF * unicas_conf, int master_shm_id, char *admin_log_file, char *err_msg)
{
  T_UC_CONF_ITEM *conf_item;
  int n;

  conf_item = (T_UC_CONF_ITEM *) malloc (sizeof (T_UC_CONF_ITEM) * 2);
  if (conf_item == NULL)
    {
      strcpy (err_msg, strerror (errno));
      return -1;
    }
  n = 0;
  SET_CONF_ITEM_INT (conf_item, n, UC_CONF_PARAM_MASTER_SHM_ID, master_shm_id, FMT_X);
  SET_CONF_ITEM_STR (conf_item, n, UC_CONF_PARAM_ADMIN_LOG_FILE, admin_log_file);

  unicas_conf->num_header = n;
  unicas_conf->header_conf = conf_item;
  return 0;
}

static int
admin_common (T_BROKER_INFO * br_info, int *num_broker, int *master_shm_id, char *admin_log_file, char *err_msg,
	      char admin_flag, bool * acl_flag, char *acl_file)
{
  if (broker_config_read (NULL, br_info, num_broker, master_shm_id, admin_log_file, admin_flag, acl_flag, acl_file,
			  err_msg) < 0)
    {
      return -1;
    }

  ut_cd_work_dir ();

  if (!admin_flag)
    {
      return 0;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      strcpy (err_msg, "Error: WSAinit");
      return -1;
    }
#endif /* WINDOWS */

#if 0
  if (admin_get_host_ip ())
    {
      CP_ADMIN_ERR_MSG (err_msg);
      return -1;
    }
#endif

  admin_init_env ();

  return 0;
}

static int
copy_job_info (T_JOB_INFO ** ret_job_info, T_MAX_HEAP_NODE * job_q)
{
  int num_job = job_q[0].id;
  int i;
  T_MAX_HEAP_NODE item;
  T_JOB_INFO *job_info;

  job_info = (T_JOB_INFO *) malloc (sizeof (T_JOB_INFO) * num_job);
  if (job_info == NULL)
    {
      return 0;
    }
  memset (job_info, 0, sizeof (T_JOB_INFO) * num_job);

  for (i = 0; i < num_job; i++)
    {
      if (max_heap_delete (job_q, &item) < 0)
	{
	  num_job = i;
	  break;
	}
      job_info[i].id = item.id;
      job_info[i].priority = item.priority;
      memcpy (job_info[i].ip, item.ip_addr, 4);
      job_info[i].recv_time = item.recv_time;
      strncpy (job_info[i].script, item.script, sizeof (job_info[i].script) - 1);
      strncpy (job_info[i].prgname, item.prg_name, sizeof (job_info[i].prgname) - 1);
    }

  *ret_job_info = job_info;
  return num_job;
}

static const char *
get_as_type_str (char as_type)
{
  if (as_type == APPL_SERVER_CAS_ORACLE)
    {
      return APPL_SERVER_CAS_ORACLE_TYPE_NAME;
    }
  if (as_type == APPL_SERVER_CAS_MYSQL51)
    {
      return APPL_SERVER_CAS_MYSQL51_TYPE_NAME;
    }
  if (as_type == APPL_SERVER_CAS_MYSQL)
    {
      return APPL_SERVER_CAS_MYSQL_TYPE_NAME;
    }
  if (as_type == APPL_SERVER_CAS_CGW)
    {
      return APPL_SERVER_CAS_CGW_TYPE_NAME;
    }
  return APPL_SERVER_CAS_TYPE_NAME;
}

static int
get_as_type (const char *type_str)
{
  if (strcasecmp (type_str, APPL_SERVER_CAS_ORACLE_TYPE_NAME) == 0)
    {
      return APPL_SERVER_CAS_ORACLE;
    }
  if (strcasecmp (type_str, APPL_SERVER_CAS_MYSQL51_TYPE_NAME) == 0)
    {
      return APPL_SERVER_CAS_MYSQL51;
    }
  if (strcasecmp (type_str, APPL_SERVER_CAS_MYSQL_TYPE_NAME) == 0)
    {
      return APPL_SERVER_CAS_MYSQL;
    }
  if (strcasecmp (type_str, APPL_SERVER_CAS_CGW_TYPE_NAME) == 0)
    {
      return APPL_SERVER_CAS_CGW;
    }
  return APPL_SERVER_CAS;
}

static void
change_conf_as_type (T_BR_CONF * br_conf, int old_as_type, int new_as_type)
{
  int num;
  T_UC_CONF_ITEM *item;

  if (old_as_type == new_as_type)
    return;

  num = br_conf->num;
  item = br_conf->item;
  if (IS_APPL_SERVER_TYPE_CAS (old_as_type))
    {
      reset_conf_value (num, item, UC_CONF_PARAM_MAX_STRING_LENGTH);
      reset_conf_value (num, item, UC_CONF_PARAM_STRIPPED_COLUMN_NAME);
    }
  num = br_conf->num - 1;
  br_conf->num = num;
}

static void
reset_conf_value (int num_item, T_UC_CONF_ITEM * item, const char *name)
{
  int i;

  for (i = 0; i < num_item; i++)
    {
      if (strcasecmp (item[i].name, name) == 0)
	{
	  if (item[i].value)
	    {
	      item[i].value[0] = '\0';
	    }
	  return;
	}
    }
}

static void
admin_log_write (const char *log_file, const char *msg)
{
  FILE *fp;
  struct tm ct;
  time_t ts;

  fp = fopen (log_file, "a");
  if (fp != NULL)
    {
      ts = time (NULL);
      memset (&ct, 0x0, sizeof (struct tm));
      localtime_r (&ts, &ct);
      fprintf (fp, "%d/%02d/%02d %02d:%02d:%02d %s\n", ct.tm_year + 1900, ct.tm_mon + 1, ct.tm_mday, ct.tm_hour,
	       ct.tm_min, ct.tm_sec, msg);
      fclose (fp);
    }
}

static int
uc_changer_internal (const char *br_name, const char *name, const char *value, int as_number, char *err_msg)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
  char admin_log_file[BROKER_PATH_MAX];

  if (admin_common (br_info, &num_broker, &master_shm_id, admin_log_file, err_msg, 0, NULL, NULL) < 0)
    {
      return -1;
    }
  if (admin_conf_change (master_shm_id, br_name, name, value, as_number) < 0)
    {
      strcpy (err_msg, "ERROR : changer");
      return -1;
    }
  return 0;
}
