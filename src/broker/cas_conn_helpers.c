/*
 *
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
 * cas_conn_helpers.c - per-connection loop helpers, extracted VERBATIM from
 *                      cas_common_main.c (stage B1, #117)
 *
 * These are the pieces of the CAS main file that the request loop needs but
 * that carry no process lifecycle: the keep-connection header readers, the
 * restart probe, the query sequence counter, and the small as_info/db status
 * accessors.  They stay in cas_common_lib for cub_cas/cub_cas_cgw and are
 * also folded into the server library for the in-server CAS speaker.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "cas_common.h"
#include "cas_common_main.h"
#include "cas_common_vars.h"
#include "broker_shm.h"
#include "cas_log.h"
#include "cas_network.h"
#include "cas_net_buf.h"
#include "broker_process_size.h"
#include "ddl_log.h"		/* logddl_set_start_time */
#include "cas_db_inc.h"		/* db_set/get_connect_status */
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
restart_is_needed (void)
{
  if (as_info->num_holdable_results > 0 || as_info->cas_change_mode == CAS_CHANGE_MODE_KEEP)
    {
      /* we do not want to restart the CAS when there are open holdable results or cas_change_mode is
       * CAS_CHANGE_MODE_KEEP */
      return 0;
    }
#if defined(WINDOWS)
  if (shm_appl->use_pdh_flag == TRUE)
    {
      if ((as_info->pid == as_info->pdh_pid) && (as_info->pdh_workset > shm_appl->appl_server_max_size))
	{
	  return 1;
	}
      else
	{
	  return 0;
	}
    }
  else
    {
      if (cas_req_count > 500)
	return 1;
      else
	return 0;
    }
#else /* WINDOWS */
  int max_process_size;

#if defined(AIX)
  /* In linux, getsize() returns VSM(55M). but in AIX, getsize() returns vritual meory size for data(900K). so, the
   * size of cub_cas process exceeds 'psize_at_start * 2' very easily. the linux's rule to restart cub_cas is not suit
   * for AIX. In AIX, we use 20M as max_process_size. */
  max_process_size = (shm_appl->appl_server_max_size > 0) ? shm_appl->appl_server_max_size : 20 * ONE_K;
#else
  max_process_size = (shm_appl->appl_server_max_size > 0) ? shm_appl->appl_server_max_size : (psize_at_start * 10);
#endif

  if (as_info->psize > max_process_size)
    {
      return 1;
    }
  else
    {
      return 0;
    }
#endif /* !WINDOWS */
}

int
net_read_header_keep_con_on (SOCKET clt_sock_fd, MSG_HEADER * client_msg_header)
{
  int ret_value = 0;
  int timeout = 0, remained_timeout = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      net_timeout_set (CAS_SHM_CFG (session_timeout));
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);
      timeout = CAS_SHM_CFG (session_timeout);
      remained_timeout = timeout;
    }

  do
    {
      if (as_info->con_status == CON_STATUS_OUT_TRAN)
	{
	  remained_timeout -= DEFAULT_CHECK_INTERVAL;
	}

      if (net_read_header (clt_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN && is_net_timed_out ())
	    {
	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}

	      if (timeout > 0 && remained_timeout <= 0)
		{
		  ret_value = -1;
		  break;
		}
	    }
	}
      else
	{
	  break;
	}
    }
  while (1);

  return ret_value;
}

int
net_read_int_keep_con_auto (SOCKET clt_sock_fd, MSG_HEADER * client_msg_header, T_REQ_INFO * req_info,
			    SOCKET srv_sock_fd)
{
  int ret_value = 0;

  if (as_info->con_status == CON_STATUS_IN_TRAN)
    {
      /* holdable results have the same lifespan of a normal session */
      net_timeout_set (CAS_SHM_CFG (session_timeout));
    }
  else
    {
      net_timeout_set (DEFAULT_CHECK_INTERVAL);

      new_req_sock_fd = srv_sock_fd;
    }

  do
    {
      if (as_info->cas_log_reset)
	{
	  cas_log_reset (broker_name);
	}
      if (as_info->cas_slow_log_reset)
	{
	  cas_slow_log_reset (broker_name);
	}

      if (as_info->con_status != CON_STATUS_IN_TRAN && as_info->reset_flag == TRUE)
	{
	  return -1;
	}

      if (as_info->con_status == CON_STATUS_CLOSE || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
	{
	  break;
	}

      if (net_read_header (clt_sock_fd, client_msg_header) < 0)
	{
	  /* if in-transaction state, return network error */
	  if (as_info->con_status == CON_STATUS_IN_TRAN || !is_net_timed_out ())
	    {
	      ret_value = -1;
	      break;
	    }
	  /* if out-of-transaction state, check whether restart is needed */
	  if (as_info->con_status == CON_STATUS_OUT_TRAN && is_net_timed_out ())
	    {
	      if (restart_is_needed ())
		{
		  cas_log_debug (ARG_FILE_LINE, "net_read_int_keep_con_auto: " "restart_is_needed()");
		  ret_value = -1;
		  break;
		}

	      if (as_info->reset_flag == TRUE)
		{
		  ret_value = -1;
		  break;
		}
	    }
	}
      else
	{
	  break;
	}
    }
  while (1);

  new_req_sock_fd = INVALID_SOCKET;

  CON_STATUS_LOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  if (as_info->con_status == CON_STATUS_OUT_TRAN)
    {
      as_info->num_request++;
      gettimeofday (&tran_start_time, NULL);
    }
  logddl_set_start_time (&tran_start_time);

  if (as_info->con_status == CON_STATUS_CLOSE || as_info->con_status == CON_STATUS_CLOSE_AND_CONNECT)
    {
      ret_value = -1;
    }
  else
    {
      if (as_info->con_status != CON_STATUS_IN_TRAN)
	{
	  as_info->con_status = CON_STATUS_IN_TRAN;
	  as_info->transaction_start_time = time (0);
	  errors_in_transaction = 0;
	}
    }

  CON_STATUS_UNLOCK (&(shm_appl->as_info[shm_as_index]), CON_STATUS_LOCK_CAS);

  return ret_value;
}


/* small per-connection helpers (see the file header) */

static CAS_TLS int query_sequence_num;

int
cas_get_graceful_down_timeout (void)
{
  if (as_info->advance_activate_flag)
    {
      return -1;
    }

  return 1 * 60;		/* 1 min */
}

/*
 * set_hang_check_time() -
 *   Mark the current time so that cas hang checker thread
 *   in broker can monitor the status of the cas.
 *   If the time is set, ALWAYS unset it
 *   before meeting indefinite blocking operation.
 */
void
set_hang_check_time (void)
{
  if (cas_shard_flag == OFF && as_info != NULL && shm_appl != NULL && shm_appl->monitor_hang_flag)
    {
      as_info->claimed_alive_time = time (NULL);
    }
  return;
}

/*
 * unset_hang_check_time -
 *   Clear the time and the cas is free from being monitored
 *   by hang checker in broker.
 */
void
unset_hang_check_time (void)
{
  if (cas_shard_flag == OFF && as_info != NULL && shm_appl != NULL && shm_appl->monitor_hang_flag)
    {
      as_info->claimed_alive_time = (time_t) 0;
    }
  return;
}

int
query_seq_num_next_value (void)
{
  return ++query_sequence_num;
}

int
query_seq_num_current_value (void)
{
  return query_sequence_num;
}

T_BROKER_VERSION
cas_get_client_version (void)
{
  return req_info.client_version;
}

void
cas_set_db_connect_status (int status)
{
  db_set_connect_status (status);
}

int
cas_get_db_connect_status (void)
{
  return db_get_connect_status ();
}
