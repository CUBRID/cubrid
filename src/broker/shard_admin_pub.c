/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * shard_admin_pub.c -
 */

#ident "$Id$"

#if defined(WINDOWS)
#include <winsock2.h>
#include <windows.h>
#endif /* WINDOWS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#if defined(WINDOWS)
#include <direct.h>
#include <process.h>
#include <io.h>
#else /* WINDOWS */
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif /* WINDOWS */

#include "porting.h"
#include "cas_common.h"
#include "broker_shm.h"
#include "broker_util.h"
#include "broker_env_def.h"
#include "broker_process_size.h"
#include "broker_admin_pub.h"
#include "broker_filename.h"
#include "broker_error.h"
#include "cas_sql_log2.h"
#include "shard_proxy_common.h"
#include "shard_shm.h"
#include "shard_metadata.h"
#include "shard_admin_pub.h"

#define ADMIN_ERR_MSG_SIZE	1024

char admin_err_msg[ADMIN_ERR_MSG_SIZE];

#if !defined(WINDOWS) && !defined(LINUX)
extern char **environ;
#endif

static int shard_proxy_activate (int as_shm_id, int proxy_id,
				 char *shm_as_cp);
static void shard_proxy_inactivate (T_BROKER_INFO * br_info_p,
				    T_PROXY_INFO * proxy_info_p);

int
shard_broker_activate (int master_shm_id, T_BROKER_INFO * br_info_p,
		       char *shm_as_cp)
{
  int pid, i, env_num;
  int broker_check_loop_count = 30;
  char err_flag = FALSE;
  char **env;

  char port_env_str[PATH_MAX];
  char appl_name_env_str[32];
  char master_shm_key_env_str[32];
  char appl_server_shm_key_env_str[32];
  char error_log_lock_env_file[128];
  const char *broker_exe_name;

  char dirname[PATH_MAX];

  T_SHM_APPL_SERVER *shm_as_p = NULL;

  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p == NULL)
    {
      return -1;
    }

  /* "shard/shard_broker_admin_pub.c" 2787 */
  br_info_p->err_code = -1;
  br_info_p->os_err_code = 0;
  br_info_p->num_busy_count = 0;

  /* SHARD TODO : move this code to shard_shm.[ch] */
  get_cubrid_file (FID_SOCK_DIR, dirname, PATH_MAX);
  snprintf (shm_as_p->port_name,
	    sizeof (shm_as_p->port_name) - 1, "%s/%s.B", dirname,
	    br_info_p->name);
#if !defined(WINDOWS)
  unlink (shm_as_p->port_name);
#endif /* !WINDOWS */

  env = make_env (br_info_p->source_env, &env_num);

#if !defined(WINDOWS)
  signal (SIGCHLD, SIG_IGN);
#endif

#if !defined(WINDOWS)
  if ((pid = fork ()) < 0)
    {
      strcpy (admin_err_msg, "fork error");

      FREE_MEM (env);
      return -1;
    }
#endif /* WINDOWS */

  br_info_p->ready_to_service = false;
#if !defined(WINDOWS)
  if (pid == 0)
    {
      signal (SIGCHLD, SIG_DFL);

      /* SHARD TODO : not implemented yet */
#if 0
#if defined(V3_ADMIN_D)
      if (admin_clt_sock_fd > 0)
	CLOSE_SOCKET (admin_clt_sock_fd);
      if (admin_srv_sock_fd > 0)
	CLOSE_SOCKET (admin_srv_sock_fd);
#endif /* V3_ADMIN_D */
#endif
#endif /* WINDOWS */

      if (env != NULL)
	{
	  for (i = 0; i < env_num; i++)
	    putenv (env[i]);
	}

      sprintf (port_env_str, "%s=%d", PORT_NUMBER_ENV_STR, br_info_p->port);
      sprintf (master_shm_key_env_str, "%s=%d", MASTER_SHM_KEY_ENV_STR,
	       master_shm_id);
      sprintf (appl_server_shm_key_env_str, "%s=%d", APPL_SERVER_SHM_KEY_STR,
	       br_info_p->appl_server_shm_id);

      putenv (port_env_str);
      putenv (master_shm_key_env_str);
      putenv (appl_server_shm_key_env_str);

      if (IS_APPL_SERVER_TYPE_CAS (br_info_p->appl_server))
	{
	  broker_exe_name = NAME_CAS_BROKER;
	}
      else
	{
	  broker_exe_name = NAME_BROKER;
	}

#if defined(WINDOWS)
      if (IS_APPL_SERVER_TYPE_CAS (br_info_p->appl_server)
	  && br_info_p->appl_server_port < 0)
	{
	  broker_exe_name = NAME_CAS_BROKER2;
	}
#endif /* WINDOWS */

      sprintf (appl_name_env_str, "%s=%s", APPL_NAME_ENV_STR,
	       broker_exe_name);
      putenv (appl_name_env_str);

      sprintf (error_log_lock_env_file, "%s=%s.log.lock",
	       ERROR_LOG_LOCK_FILE_ENV_STR, br_info_p->name);
      putenv (error_log_lock_env_file);

#if defined(WINDOWS)
      pid = run_child (broker_exe_name);
#else /* WINDOWS */
      if (execle (broker_exe_name, broker_exe_name, NULL, environ) < 0)
	{
	  perror (broker_exe_name);
	  exit (0);
	}
      exit (0);
    }
#endif /* WINDOWS */

  SLEEP_MILISEC (0, 200);

  br_info_p->pid = pid;

  br_info_p->ready_to_service = true;
  br_info_p->service_flag = ON;

  for (i = 0; i < broker_check_loop_count; i++)
    {
      if (br_info_p->err_code < 0)
	{
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  if (br_info_p->err_code > 0)
	    {
	      sprintf (admin_err_msg, "%s: %s", br_info_p->name,
		       uw_get_error_message (br_info_p->err_code,
					     br_info_p->os_err_code));
	      err_flag = TRUE;
	    }
	  break;
	}
    }

  if (i == broker_check_loop_count)
    {
      sprintf (admin_err_msg, "%s: unknown error", br_info_p->name);
      err_flag = TRUE;
    }

  if (err_flag)
    {
      FREE_MEM (env);
      return -1;
    }

  FREE_MEM (env);
  return 0;
}

void
shard_broker_inactivate (T_BROKER_INFO * br_info_p)
{
  int i;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  char *shm_as_cp = NULL;
  time_t cur_time = time (NULL);
  struct tm ct;
  char cmd_buf[BUFSIZ];

#if defined(WINDOWS)
  if (localtime_r (&cur_time, &ct) < 0)
    {
      return -1;
    }
#else /* WINDOWS */
  if (localtime_r (&cur_time, &ct) == NULL)
    {
      return -1;
    }
#endif /* !WINDOWS */
  ct.tm_year += 1900;

  if (br_info_p->pid)
    {
      SHARD_ERR ("<KILL BROKER> PID:[%d]\n", br_info_p->pid);
      ut_kill_process (br_info_p->pid, br_info_p->name, PROXY_INVALID_ID,
		       SHARD_INVALID_ID, CAS_INVALID_ID);
      SLEEP_MILISEC (1, 0);
    }

  shm_as_cp =
    (char *) uw_shm_open (br_info_p->appl_server_shm_id, SHM_APPL_SERVER,
			  SHM_MODE_ADMIN);
  if (shm_as_cp == NULL)
    {
      SHARD_ERR ("failed to uw_shm_open. (shmid:%08x).\n",
		 br_info_p->appl_server_shm_id);
      goto end;
    }

  shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (shm_proxy_p == NULL)
    {
      SHARD_ERR ("failed to shard_shm_get_proxy. \n");
      goto end;
    }

  for (proxy_info_p = shard_shm_get_first_proxy_info (shm_proxy_p);
       proxy_info_p;
       proxy_info_p = shard_shm_get_next_proxy_info (proxy_info_p))
    {
      shard_proxy_inactivate (br_info_p, proxy_info_p);
    }

  if (br_info_p->log_backup == ON)
    {
      sprintf (cmd_buf, "%s.%02d%02d%02d.%02d%02d",
	       br_info_p->error_log_file,
	       ct.tm_year, ct.tm_mon + 1, ct.tm_mday, ct.tm_hour, ct.tm_min);
      rename (br_info_p->error_log_file, cmd_buf);
    }
  else
    {
      unlink (br_info_p->error_log_file);
    }

end:
  br_info_p->appl_server_num = br_info_p->appl_server_min_num;
  br_info_p->num_busy_count = 0;
  br_info_p->service_flag = OFF;

  /* SHARD TODO : not implemented yet - acl */
#if 0
  /* "broker/broker_admin_pub.c" 2885 lines */
#if defined (WINDOWS)
  MAKE_ACL_SEM_NAME (acl_sem_name, shm_appl->broker_name);
  uw_sem_destroy (acl_sem_name);
#else
  uw_sem_destroy (&shm_appl->acl_sem);
#endif
#endif

  if (shm_as_cp)
    {
      uw_shm_detach (shm_as_cp);
    }
  uw_shm_destroy (br_info_p->appl_server_shm_id);
  uw_shm_destroy (br_info_p->metadata_shm_id);

  return;
}

static int
shard_proxy_activate (int as_shm_id, int proxy_id, char *shm_as_cp)
{
  int pid, i, env_num;
  int fd_cnt;
  char **env;
  const char *proxy_exe_name = NAME_PROXY;
  char port_env_str[PATH_MAX];
  char access_log_env_str[256];
  char as_shm_id_env_str[32];
  char proxy_id_env_str[32];

  char dirname[PATH_MAX];
  char process_name[128];

  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;

  assert (proxy_id >= 0);
  assert (shm_as_cp);

  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p == NULL)
    {
      return -1;
    }

  proxy_info_p = shard_shm_get_proxy_info (shm_as_cp, proxy_id);
  if (proxy_info_p == NULL)
    {
      return -1;
    }

  proxy_info_p->cur_client = 0;

  shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
  if (shard_info_p == NULL)
    {
      return -1;
    }

  /* Check the fd count of proxy is greater than MAX_FD. */
  /* MAX_FD >= broker + max_client + (max_num_appl_server * each shard) + RESERVED_FD */
  fd_cnt = 1 + proxy_info_p->max_client;
  fd_cnt += shard_info_p->max_appl_server * proxy_info_p->max_shard;

  if (MAX_FD - RESERVED_FD < fd_cnt)
    {

      sprintf (admin_err_msg,
	       "fd set count %d is greater than %d [%s]\n\n"
	       "please check your $CUBRID/conf/shard.conf\n\n"
	       "[%%%s]\n"
	       "%-20s = %d\n"
	       "%-20s = %d * %d\n", fd_cnt, MAX_FD - RESERVED_FD,
	       shm_as_p->broker_name, shm_as_p->broker_name, "MAX_CLIENT",
	       proxy_info_p->max_client, "MAX_NUM_APPL_SERVER",
	       shard_info_p->max_appl_server, proxy_info_p->max_shard);
      return -1;
    }

  get_cubrid_file (FID_SOCK_DIR, dirname, PATH_MAX);
  snprintf (proxy_info_p->port_name,
	    sizeof (proxy_info_p->port_name) - 1, "%s/%s.P%d", dirname,
	    shm_as_p->broker_name, proxy_id + 1);
#if !defined(WINDOWS)
  unlink (proxy_info_p->port_name);
#endif /* !WINDOWS */

  env = make_env (shm_as_p->source_env, &env_num);

#if !defined(WINDOWS)
  if ((pid = fork ()) < 0)
    {
      strcpy (admin_err_msg, "fork error");

      /* SHARD TODO : will delete */
#if 0
      uw_shm_detach (shm_as_p);
      uw_shm_destroy (br_info_p->appl_server_shm_id);
#endif
      FREE_MEM (env);
      return -1;
    }
#endif /* WINDOWS */

  if (pid == 0)
    {
      signal (SIGCHLD, SIG_DFL);

      if (env != NULL)
	{
	  for (i = 0; i < env_num; i++)
	    putenv (env[i]);
	}
      sprintf (port_env_str, "%s=%s", PORT_NAME_ENV_STR, shm_as_p->port_name);
      snprintf (as_shm_id_env_str, sizeof (as_shm_id_env_str), "%s=%d",
		APPL_SERVER_SHM_KEY_STR, as_shm_id);
      snprintf (proxy_id_env_str, sizeof (proxy_id_env_str), "%s=%d",
		PROXY_ID_ENV_STR, proxy_id);

      putenv (port_env_str);
      putenv (as_shm_id_env_str);
      putenv (proxy_id_env_str);
      /* SHARD TODO : will delete */
#if 0
      putenv (access_log_env_str);
#endif

#if !defined(WINDOWS)
      snprintf (process_name, sizeof (process_name) - 1, "%s_%s_%d",
		shm_as_p->broker_name, proxy_exe_name, proxy_id + 1);
#endif /* !WINDOWS */


#if defined(WINDOWS)
      pid = run_child (proxy_exe_name);
#else /* WINDOWS */
      if (execle (proxy_exe_name, process_name, NULL, environ) < 0)
	{
	  perror (process_name);
	  exit (0);
	}
      exit (0);
    }
#endif /* WINDOWS */

  SLEEP_MILISEC (0, 200);

  proxy_info_p->pid = pid;

  return 0;
}

static void
shard_proxy_inactivate (T_BROKER_INFO * br_info_p,
			T_PROXY_INFO * proxy_info_p)
{
  int i;
  T_SHARD_INFO *shard_info_p;
  T_APPL_SERVER_INFO *as_info_p;

  assert (proxy_info_p);

  if (proxy_info_p->pid > 0)
    {
      SHARD_ERR ("<KILL PROXY> PID:[%d]\n", proxy_info_p->pid);
      ut_kill_process (proxy_info_p->pid, br_info_p->name,
		       proxy_info_p->proxy_id, SHARD_INVALID_ID,
		       CAS_INVALID_ID);
    }
  proxy_info_p->pid = 0;
  proxy_info_p->cur_client = 0;

  /* SHARD TODO : backup or remove access log file */

  for (shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
       shard_info_p;
       shard_info_p = shard_shm_get_next_shard_info (shard_info_p))
    {
      for (i = 0; i < shard_info_p->num_appl_server; i++)
	{
	  as_info_p = &(shard_info_p->as_info[i]);
	  if (as_info_p->pid)
	    {
	      shard_as_inactivate (br_info_p, as_info_p,
				   proxy_info_p->proxy_id,
				   shard_info_p->shard_id, i);
	    }
	}
    }

  return;
}

int
shard_as_activate (int as_shm_id,
		   int proxy_id, int shard_id, int as_id, char *shm_as_cp)
{
  /* as_activate() 
     "shard/shard_broker_admin_pub.c" 2790
   */
  int pid, i, env_num;
  char **env;
  char port_env_str[PATH_MAX];
  char appl_name_env_str[64];
  char appl_server_shm_key_env_str[32];
  char error_log_env_str[256];
  char error_log_lock_env_file[128];

  char proxy_id_env_str[32];
  char shard_id_env_str[32];
  char as_id_env_str[32];

  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
  char port_name[PATH_MAX], dirname[PATH_MAX];
#if !defined(WINDOWS)
  char process_name[128];
#endif /* !WINDOWS */

  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_APPL_SERVER_INFO *as_info_p = NULL;

  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p == NULL)
    {
      return -1;
    }

  proxy_info_p = shard_shm_get_proxy_info (shm_as_cp, proxy_id);
  if (proxy_info_p == NULL)
    {
      return -1;
    }

  as_info_p = shard_shm_get_as_info (proxy_info_p, shard_id, as_id);
  if (as_info_p == NULL)
    {
      return -1;
    }

  env = make_env (shm_as_p->source_env, &env_num);

  /* SHARD TODO : will delete, not used */
#if 0
  get_cubrid_file (FID_SOCK_DIR, dirname);
  snprintf (port_name, sizeof (port_name) - 1, "%s/%s.%d", dirname,
	    br_info_p->name, as_id);
#if !defined(WINDOWS)
  unlink (port_name);
#endif /* !WINDOWS */
#endif

  as_info_p->num_request = 0;
  as_info_p->session_id = 0;
  as_info_p->uts_status = UTS_STATUS_START;
  as_info_p->reset_flag = FALSE;
  /* SHARD TODO : not implemented yet - acl */
#if 0
  as_info_p->clt_appl_name[0] = '\0';
  as_info_p->clt_req_path_info[0] = '\0';
  as_info_p->clt_ip_addr[0] = '\0';
#endif
  as_info_p->cur_sql_log_mode = shm_as_p->sql_log_mode;

#if defined(WINDOWS)
  as_info_p->pdh_pid = 0;
  as_info_p->pdh_workset = 0;
  as_info_p->pdh_pct_cpu = 0;
#endif /* WINDOWS */

  as_info_p->service_ready_flag = FALSE;

  CON_STATUS_LOCK_INIT (as_info_p);

#if defined(WINDOWS)
  pid = 0;
#else /* WINDOWS */
  pid = fork ();
  if (pid < 0)
    {
      perror ("fork");
    }
#endif /* !WINDOWS */

  if (pid == 0)
    {
      if (env != NULL)
	{
	  for (i = 0; i < env_num; i++)
	    putenv (env[i]);
	}

      strcpy (appl_name, shm_as_p->appl_server_name);

      sprintf (port_env_str, "%s=%s", PORT_NAME_ENV_STR,
	       proxy_info_p->port_name);
      sprintf (appl_server_shm_key_env_str, "%s=%d", APPL_SERVER_SHM_KEY_STR,
	       as_shm_id);
      sprintf (appl_name_env_str, "%s=%s", APPL_NAME_ENV_STR, appl_name);

      sprintf (error_log_env_str, "%s=%s",
	       ERROR_LOG_ENV_STR, shm_as_p->error_log_file);

      sprintf (error_log_lock_env_file, "%s=%s.log.lock",
	       ERROR_LOG_LOCK_FILE_ENV_STR, shm_as_p->broker_name);

      snprintf (proxy_id_env_str, sizeof (proxy_id_env_str), "%s=%d",
		PROXY_ID_ENV_STR, proxy_id);
      snprintf (shard_id_env_str, sizeof (shard_id_env_str), "%s=%d",
		SHARD_ID_ENV_STR, shard_id);
      snprintf (as_id_env_str, sizeof (as_id_env_str), "%s=%d", AS_ID_ENV_STR,
		as_id);

      putenv (port_env_str);
      putenv (appl_server_shm_key_env_str);
      putenv (appl_name_env_str);
      putenv (error_log_env_str);
      putenv (error_log_lock_env_file);

      putenv (proxy_id_env_str);
      putenv (shard_id_env_str);
      putenv (as_id_env_str);

#if !defined(WINDOWS)
      snprintf (process_name, sizeof (process_name) - 1, "%s_%s_%d_%d_%d",
		shm_as_p->broker_name, appl_name, proxy_id + 1, shard_id,
		as_id + 1);
#endif /* !WINDOWS */

      SHARD_ERR ("<START AS> process_name:[%s|%s]\n", appl_name,
		 process_name);

#if defined(WINDOWS)
      pid = run_child (appl_name);
#else /* WINDOWS */
      if (execle (appl_name, process_name, NULL, environ) < 0)
	{
	  perror (appl_name);
	  SHARD_ERR ("<START AS> failed. process_name:[%s|%s]\n", appl_name,
		     process_name);
	}
      exit (0);
#endif /* WINDOWS */
    }

  /* SHARD TODO : not inplemented  yet */
#if 0
  SERVICE_READY_WAIT (as_info_p->service_ready_flag);
#endif

  as_info_p->pid = pid;
  as_info_p->last_access_time = time (NULL);
  as_info_p->psize_time = time (NULL);
  as_info_p->psize = getsize (as_info_p->pid);
  as_info_p->uts_status = UTS_STATUS_CON_WAIT;
  as_info_p->service_flag = SERVICE_ON;

  FREE_MEM (env);
  return 0;
}

void
shard_as_inactivate (T_BROKER_INFO * br_info_p,
		     T_APPL_SERVER_INFO * as_info_p, int proxy_index,
		     int shard_index, int as_index)
{
  if (as_info_p->pid > 0)
    {
      SHARD_ERR ("<KILL AS> PID:[%d]\n", as_info_p->pid);
      ut_kill_process (as_info_p->pid, br_info_p->name, proxy_index,
		       shard_index, as_index);
    }
  as_info_p->pid = 0;
  as_info_p->last_access_time = time (0);
  as_info_p->service_flag = SERVICE_OFF;
  as_info_p->service_ready_flag = FALSE;
  CON_STATUS_LOCK_DESTROY (as_info_p);

  /* 
   * shard_cas does not have unix-domain socket and pid lock file.
   * so, we need not delete socket and lock file.
   */

  return;
}

int
shard_process_activate (int master_shm_id, T_BROKER_INFO * br_info_p,
			T_SHM_BROKER * shm_br_p, char *shm_as_cp)
{
  int i, j, k, error;
  T_SHM_APPL_SERVER *shm_as_p;

  T_SHM_PROXY *proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;

  assert (master_shm_id > 0);
  assert (br_info_p);
  assert (shm_br_p);
  assert (shm_as_cp);

  /* start broker */
  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p == NULL)
    {
      goto error_return;
    }
  error = shard_broker_activate (master_shm_id, br_info_p, shm_as_cp);
  if (error)
    {
      goto error_return;
    }

  /* start proxy */
  proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (proxy_p == NULL)
    {
      goto error_return;
    }
  for (i = 0, proxy_info_p = shard_shm_get_first_proxy_info (proxy_p);
       proxy_info_p;
       i++, proxy_info_p = shard_shm_get_next_proxy_info (proxy_info_p))
    {
      proxy_info_p->cur_proxy_log_mode = br_info_p->proxy_log_mode;

      error =
	shard_proxy_activate (br_info_p->appl_server_shm_id, i, shm_as_cp);
      if (error)
	{
	  SHARD_ERR ("<PROXY START FAILED> PROXY:[%d].\n",
		     proxy_info_p->proxy_id);
	  goto error_return;
	}

      for (j = 0, shard_info_p =
	   shard_shm_get_first_shard_info (proxy_info_p); shard_info_p;
	   j++, shard_info_p = shard_shm_get_next_shard_info (shard_info_p))
	{
	  /* start shard (c)as */
	  /* SHARD TODO : min_appl_server? num_appl_server? */
	  for (k = 0; k < shard_info_p->num_appl_server; k++)
	    {
	      error =
		shard_as_activate (br_info_p->appl_server_shm_id,
				   proxy_info_p->proxy_id /*proxy_id */ ,
				   shard_info_p->shard_id /*shard_id */ ,
				   k /*as_index */ ,
				   shm_as_cp);
	      if (error)
		{
		  SHARD_ERR
		    ("<AS START FAILED> PROXY:[%d], SHARD:[%d], AS:[%d].\n",
		     proxy_info_p->proxy_id, shard_info_p->shard_id, k);
		  goto error_return;
		}
	    }

	  SERVICE_READY_WAIT (shard_info_p->service_ready_flag);
	  if (shard_info_p->service_ready_flag == false)
	    {
	      sprintf (admin_err_msg,
		       "failed to connect database. [%s]\n\n"
		       "please check your $CUBRID/conf/shard.conf or database status.\n\n"
		       "[%%%s]\n"
		       "%-20s = %s\n"
		       "%-20s = %s\n",
		       br_info_p->name, br_info_p->name, "SHARD_DB_NAME",
		       shard_info_p->db_name, "SHARD_DB_USER",
		       shard_info_p->db_user);
	      goto error_return;
	    }
	}
    }


  return 0;

error_return:

  shard_process_inactivate (br_info_p);
  return -1;
}

void
shard_process_inactivate (T_BROKER_INFO * br_info_p)
{
  assert (br_info_p);

  shard_broker_inactivate (br_info_p);

  return;
}
