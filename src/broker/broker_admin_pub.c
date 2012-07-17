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
 * broker_admin_pub.c -
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

#if defined(WINDOWS)
#include <direct.h>
#include <process.h>
#include <io.h>
#else /* WINDOWS */
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <math.h>
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

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#include "dbdef.h"
#else /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */
#define DB_EMPTY_SESSION        (0)
#endif /* !CAS_FOR_ORACLE && !CAS_FOR_MYSQL */

#define ADMIN_ERR_MSG_SIZE	1024

#define MAKE_VERSION(MAJOR, MINOR)	(((MAJOR) << 8) | (MINOR))

#define SHM_OPEN_ERR_MSG(BUF, ERRCODE, OSERRCODE)			\
	do {								\
	  int	_err = ERRCODE, _os_err = OSERRCODE;			\
	  int	_msglen = 0;						\
	  char	*_msg = NULL;						\
	  if (_err == UW_ER_SHM_OPEN_MAGIC) {				\
	    sprintf(BUF, "Cannot open shared memory (Version mismatched)"); \
	  }								\
	  else {							\
	    if (_os_err > 0)						\
	      _msg = strerror(_os_err);					\
	    _msglen = sprintf(BUF, "Cannot open shared memory");	\
	    if (_msg != NULL)						\
	      _msglen += sprintf(BUF + _msglen, " (os error = %s)", _msg); \
	  }								\
	} while (0)

#define MEMBER_SIZE(TYPE, MEMBER) sizeof(((TYPE *)0)->MEMBER)
#define NUM_OF_DIGITS(NUMBER) (int)log10(NUMBER) + 1

#define ACCESS_FILE_DELIMITER ":"
#define IP_FILE_DELIMITER ","

#if !defined(CUBRID_SHARD)
static int br_activate (T_BROKER_INFO *, int, T_SHM_BROKER *);
static int br_inactivate (T_BROKER_INFO *);
static void as_activate (T_APPL_SERVER_INFO *, int, T_BROKER_INFO *, char **,
			 int, T_SHM_APPL_SERVER *, T_SHM_BROKER *);
static void as_inactivate (int, char *br_name, int as_index);
static char **make_env (char *env_file, int *env_num);
static const char *get_appl_server_name (int appl_server_type, char **env,
					 int env_num);
#endif /* !CUBRID_SHARD */
static int broker_create_dir (const char *new_dir);
static int read_from_access_control_file (T_SHM_APPL_SERVER * shm_appl,
					  char *filename);
static int read_ip_info (IP_INFO * ip_info, char *filename);
static void access_control_file_repath (char *path);
#if !defined(WINDOWS)
#if defined (ENABLE_UNUSED_FUNCTION)
static int get_cubrid_version (void);
#endif
#endif /* !WINDOWS */

#if defined(WINDOWS)
static int admin_get_host_ip (unsigned char *ip_addr);
#endif /* WINDOWS */

char admin_err_msg[ADMIN_ERR_MSG_SIZE];

#if !defined(WINDOWS) && !defined(LINUX)
extern char **environ;
#endif

#if defined(WINDOWS)
void
unix_style_path (char *path)
{
  char *p;
  for (p = path; *p; p++)
    {
      if (*p == '\\')
	*p = '/';
    }
}
#endif /* WINDOWS */

static int
broker_create_dir (const char *new_dir)
{
  char *p, path[PATH_MAX];

  if (new_dir == NULL)
    return -1;

  strcpy (path, new_dir);
  trim (path);

#if defined(WINDOWS)
  unix_style_path (path);
#endif /* WINDOWS */

  p = path;
#if defined(WINDOWS)
  if (path[0] == '/')
    p = path + 1;
  else if (strlen (path) > 3 && path[2] == '/')
    p = path + 3;
#else /* WINDOWS */
  if (path[0] == '/')
    {
      p = path + 1;
    }
#endif /* WINDOWS */

  while (p != NULL)
    {
      p = strchr (p, '/');
      if (p != NULL)
	*p = '\0';
      if (access (path, F_OK) < 0)
	{
	  if (mkdir (path, 0777) < 0)
	    {
	      return -1;
	    }
	}
      if (p != NULL)
	{
	  *p = '/';
	  p++;
	}
    }
  return -1;
}

#if defined (ENABLE_UNUSED_FUNCTION)
int
admin_isstarted_cmd (int master_shm_id)
{
  T_SHM_BROKER *shm_br;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    return 0;
  uw_shm_detach (shm_br);
  return 1;
}
#endif /* ENABLE_UNUSED_FUNCTION */

int
admin_start_cmd (T_BROKER_INFO * br_info, int br_num, int master_shm_id,
		 bool acl_flag, char *acl_file)
{
  T_SHM_BROKER *shm_br;
  int shm_size, i, j;
  int res = 0;
#if defined(WINDOWS)
  unsigned char ip_addr[4];
#endif /* WINDOWS */
  char path[PATH_MAX];

#if defined(CUBRID_SHARD)
  char *shm_metadata_p = NULL;
  char *shm_appl_svr_p = NULL;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHM_PROXY *proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p;
#endif /* CUBRID_SHARD */

#if defined(WINDOWS)
  if (admin_get_host_ip (ip_addr) < 0)
    return -1;
#endif /* WINDOWS */

  if (br_num <= 0)
    {
      strcpy (admin_err_msg,
	      "Cannot start CUBRID Broker. (number of broker is 0)");
      return -1;
    }
  chdir ("..");
  broker_create_dir (get_cubrid_file (FID_VAR_DIR, path, PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_CAS_TMP_DIR, path, PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_AS_PID_DIR, path, PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_SQL_LOG_DIR, path, PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_SLOW_LOG_DIR, path, PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_CUBRID_ERR_DIR, path, PATH_MAX));
#if !defined(WINDOWS)
  broker_create_dir (get_cubrid_file (FID_SQL_LOG2_DIR, path, PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_SOCK_DIR, path, PATH_MAX));
#endif /* !WINDOWS */


  for (i = 0; i < br_num; i++)
    {
#if !defined(WINDOWS)
      /*prevent the broker from hanging due to an excessively long path
       * socket path length = sock_path[broker_name].[as_index]
       */
      if (strlen (path) + strlen (br_info[i].name) + 1 +
	  NUM_OF_DIGITS (br_info[i].appl_server_max_num) >
	  MEMBER_SIZE (struct sockaddr_un, sun_path) - 1)
	{
	  snprintf (admin_err_msg, sizeof (admin_err_msg) - 1,
		    "The socket path is too long (>%u): %s",
		    MEMBER_SIZE (struct sockaddr_un, sun_path), path);
	  return -1;
	}
#endif /* !WINDOWS */
      broker_create_dir (br_info[i].log_dir);
      broker_create_dir (br_info[i].slow_log_dir);
      broker_create_dir (br_info[i].err_log_dir);
#if defined(CUBRID_SHARD)
      broker_create_dir (br_info[i].proxy_log_dir);
#endif /* CUBRID_SHARD */
    }
  chdir (envvar_bindir_file (path, PATH_MAX, ""));

  /* create master shared memory */
  shm_size = sizeof (T_SHM_BROKER) + (br_num - 1) * sizeof (T_BROKER_INFO);

  shm_br =
    (T_SHM_BROKER *) uw_shm_create (master_shm_id, shm_size, SHM_BROKER);

  if (shm_br == NULL)
    {
      strcpy (admin_err_msg, "Cannot create shared memory");
      return -1;
    }

#if defined(WINDOWS)
  shm_br->magic = uw_shm_get_magic_number ();
  memcpy (shm_br->my_ip_addr, ip_addr, 4);
#else /* WINDOWS */
  shm_br->owner_uid = getuid ();

  /* create a new session */
  setsid ();
#endif /* WINDOWS */

  shm_br->num_broker = br_num;
  /* create appl server shared memory */
  for (i = 0; i < br_num; i++)
    {
#if defined(CUBRID_SHARD)
      shm_as_p = NULL;
      proxy_p = NULL;
#endif
      shm_br->br_info[i] = br_info[i];
#if !defined(CUBRID_SHARD)
      snprintf (shm_br->br_info[i].access_log_file, CONF_LOG_FILE_LEN - 1,
		"%s/%s.access", br_info[i].access_log_file, br_info[i].name);
#endif /* !CUBRID_SHARD */
      snprintf (shm_br->br_info[i].error_log_file, CONF_LOG_FILE_LEN - 1,
		"%s/%s.err", br_info[i].error_log_file, br_info[i].name);

      shm_br->access_control = acl_flag;

      if (acl_file != NULL)
	{
	  strncpy (shm_br->access_control_file, acl_file, PATH_MAX - 1);
	}

      if (shm_br->br_info[i].service_flag == ON)
	{
#if defined(CUBRID_SHARD)
	  shm_metadata_p = shard_metadata_initialize (&(shm_br->br_info[i]));
	  if (shm_metadata_p)
	    {
	      shm_appl_svr_p =
		shard_shm_initialize (&(shm_br->br_info[i]), shm_metadata_p);
	      if (shm_appl_svr_p)
		{
		  shm_as_p = shard_shm_get_appl_server (shm_appl_svr_p);
		  proxy_p = shard_shm_get_proxy (shm_appl_svr_p);
		}
	    }

	  if (shm_as_p == NULL || proxy_p == NULL)
	    {
	      uw_shm_destroy (shm_br->br_info[i].appl_server_shm_id);
	      uw_shm_destroy (shm_br->br_info[i].metadata_shm_id);

	      res = -1;
	      break;
	    }

	  for (j = 0, proxy_info_p = shard_shm_get_first_proxy_info (proxy_p);
	       proxy_info_p;
	       j++, proxy_info_p =
	       shard_shm_get_next_proxy_info (proxy_info_p))
	    {
	      snprintf (proxy_info_p->access_log_file, CONF_LOG_FILE_LEN - 1,
			"%s/%s_%d.access", CUBRID_BASE_DIR, br_info[i].name,
			j);
	      dir_repath (proxy_info_p->access_log_file);
	    }

	  res =
	    shard_process_activate (master_shm_id, &(shm_br->br_info[i]),
				    shm_br, shm_appl_svr_p);
#else
	  res = br_activate (&(shm_br->br_info[i]), master_shm_id, shm_br);
#endif
	  if (res < 0)
	    {
	      break;
	    }
	}
    }

  if (res < 0)
    {
      char err_msg_backup[ADMIN_ERR_MSG_SIZE];
      memcpy (err_msg_backup, admin_err_msg, ADMIN_ERR_MSG_SIZE);

      for (--i; i >= 0; i--)
	{
#if defined(CUBRID_SHARD)
	  shard_process_inactivate (&(shm_br->br_info[i]));
#else
	  br_inactivate (&(shm_br->br_info[i]));
#endif /* CUBRID_SHARD */
	}
      memcpy (admin_err_msg, err_msg_backup, ADMIN_ERR_MSG_SIZE);
    }

  uw_shm_detach (shm_br);
#if defined(CUBRID_SHARD)
  if (shm_appl_svr_p)
    {
      uw_shm_detach (shm_appl_svr_p);
    }
  if (shm_metadata_p)
    {
      uw_shm_detach (shm_metadata_p);
    }
#endif /* CUBRID_SHARD */

  if (res < 0)
    {
      uw_shm_destroy (master_shm_id);
    }
#if defined(WINDOWS)
  else
    {
      char shm_id_env_str[128];
      sprintf (shm_id_env_str, "%s=%d", MASTER_SHM_KEY_ENV_STR,
	       master_shm_id);
      putenv (shm_id_env_str);
      run_child (NAME_UC_SHM);
    }
#endif /* WINDOWS */

  return res;
}

int
admin_stop_cmd (int master_shm_id)
{
  T_SHM_BROKER *shm_br;
  int i;

  shm_br = (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
					 SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

#if !defined(WINDOWS)
  if (shm_br->owner_uid != getuid ())
    {
      strcpy (admin_err_msg, "Cannot stop CUBRID Broker. (Not owner)\n");
      return -1;
    }
#endif /* WINDOWS */
  for (i = 0; i < MAX_BROKER_NUM && i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].service_flag == ON)
	{
#if defined(CUBRID_SHARD)
	  shard_process_inactivate (&shm_br->br_info[i]);
#else
	  br_inactivate (&(shm_br->br_info[i]));
#endif /* CUBRID_SHARD */
	}
    }

  shm_br->magic = 0;
#if defined(WINDOWS)
  uw_shm_detach (shm_br);
#endif /* WINDOWS */
  uw_shm_destroy (master_shm_id);

  return 0;
}

int
admin_add_cmd (int master_shm_id, const char *broker)
{
#if defined(CUBRID_SHARD)
  /* SHARD TODO : not implemented yet */
  return 0;
#else
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl_server;
  int i, br_index;
  int appl_shm_key = 0, as_index;
  char **env;
  int env_num;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }
  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (broker, shm_br->br_info[i].name) == 0)
	{
	  appl_shm_key = shm_br->br_info[i].appl_server_shm_id;
	  br_index = i;
	  break;
	}
    }
  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker);
      uw_shm_detach (shm_br);
      return -1;
    }

  if (shm_br->br_info[br_index].auto_add_appl_server == ON)
    {
      uw_shm_detach (shm_br);
      return 0;
    }

  shm_appl_server =
    (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl_server == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  if (shm_br->br_info[br_index].appl_server_num >=
      shm_br->br_info[br_index].appl_server_max_num)
    {
      strcpy (admin_err_msg, "Cannot add appl server\n");
      uw_shm_detach (shm_br);
      uw_shm_detach (shm_appl_server);
      return -1;
    }

  as_index = shm_br->br_info[br_index].appl_server_num;

  (shm_br->br_info[br_index].appl_server_num)++;
  (shm_appl_server->num_appl_server)++;

  env = make_env (shm_br->br_info[br_index].source_env, &env_num);

  as_activate (&(shm_appl_server->as_info[as_index]),
	       as_index, &(shm_br->br_info[br_index]),
	       env, env_num, shm_appl_server, shm_br);

  shm_appl_server->as_info[as_index].service_flag = SERVICE_ON;

  uw_shm_detach (shm_appl_server);
  uw_shm_detach (shm_br);
  FREE_MEM (env);

  return 0;
#endif /* CUBRID_SHARD */
}

int
admin_restart_cmd (int master_shm_id, const char *broker, int as_index)
{
#if defined(CUBRID_SHARD)
  /* SHARD TODO : not implemented yet */
  return 0;
#else
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, br_index, appl_shm_key;
  int pid;
  char **env;
  int env_num;
  char port_str[PATH_MAX], appl_name_str[64];
  char access_log_env_str[256], error_log_env_str[256];
  char appl_server_shm_key_str[32];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
  char buf[PATH_MAX];
#if !defined(WINDOWS)
  char argv0[128];
#endif /* !WINDOWS */

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      goto restart_error;
    }
  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (broker, shm_br->br_info[i].name) == 0)
	{
	  appl_shm_key = shm_br->br_info[i].appl_server_shm_id;
	  br_index = i;
	  break;
	}
    }
  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]\n", broker);
      goto restart_error;
    }
  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      goto restart_error;
    }

  if (as_index < 1
      || as_index > shm_br->br_info[br_index].appl_server_max_num)
    {
      strcpy (admin_err_msg, "Cannot restart appl server\n");
      goto restart_error;
    }
  as_index--;

  if (shm_appl->as_info[as_index].service_flag != SERVICE_ON)
    {
      uw_shm_detach (shm_appl);
      uw_shm_detach (shm_br);
      return 0;
    }

  if (IS_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server))
    {
      /* proxy_id and shard_id argument is only use in CUBRID SHARD */
      /* so, please set PROXY_INVALID_ID and SHARD_INVALID_ID in normal Broker */
      ut_kill_process (shm_appl->as_info[as_index].pid,
		       shm_br->br_info[br_index].name, PROXY_INVALID_ID,
		       SHARD_INVALID_ID, as_index);
      shm_appl->as_info[as_index].uts_status = UTS_STATUS_BUSY;
      uw_shm_detach (shm_appl);
      uw_shm_detach (shm_br);
      return 0;
    }

  /* mutex entry section */
  shm_appl->as_info[as_index].mutex_flag[SHM_MUTEX_ADMIN] = TRUE;
  shm_appl->as_info[as_index].mutex_turn = SHM_MUTEX_BROKER;
  while ((shm_appl->as_info[as_index].mutex_flag[SHM_MUTEX_BROKER] == TRUE)
	 && (shm_appl->as_info[as_index].mutex_turn == SHM_MUTEX_BROKER))
    {				/* no-op */
#if defined(WINDOWS)
      int a;
      a = 0;
#endif /* WINDOWS */
    }

  shm_appl->as_info[as_index].uts_status = UTS_STATUS_BUSY;

  if (shm_appl->as_info[as_index].pid)
    {
      /* proxy_id and shard_id argument is only use in CUBRID SHARD */
      /* so, please set PROXY_INVALID_ID and SHARD_INVALID_ID in normal Broker */
      ut_kill_process (shm_appl->as_info[as_index].pid,
		       shm_br->br_info[br_index].name, PROXY_INVALID_ID,
		       SHARD_INVALID_ID, as_index);
    }

  SLEEP_SEC (1);

  env = make_env (shm_br->br_info[br_index].source_env, &env_num);

  shm_appl->as_info[as_index].service_ready_flag = FALSE;

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

      sprintf (port_str, "%s=%s%s.%d", PORT_NAME_ENV_STR,
	       get_cubrid_file (FID_SOCK_DIR, buf, PATH_MAX),
	       shm_br->br_info[br_index].name, as_index + 1);
      putenv (port_str);
      sprintf (appl_server_shm_key_str, "%s=%d", APPL_SERVER_SHM_KEY_STR,
	       shm_br->br_info[br_index].appl_server_shm_id);
      putenv (appl_server_shm_key_str);
      strcpy (appl_name, shm_appl->appl_server_name);
      sprintf (appl_name_str, "%s=%s", APPL_NAME_ENV_STR, appl_name);
      putenv (appl_name_str);
      sprintf (access_log_env_str, "%s=%s",
	       ACCESS_LOG_ENV_STR, shm_br->br_info[br_index].access_log_file);
      putenv (access_log_env_str);
      sprintf (error_log_env_str, "%s=%s",
	       ERROR_LOG_ENV_STR, shm_br->br_info[br_index].error_log_file);
      putenv (error_log_env_str);

#if !defined(WINDOWS)
      snprintf (argv0, sizeof (argv0) - 1, "%s_%s_%d",
		shm_br->br_info[br_index].name, appl_name, as_index + 1);
      uw_shm_detach (shm_br);
      uw_shm_detach (shm_appl);
#endif /* !WINDOWS */

#if defined(WINDOWS)
      pid = run_child (appl_name);
#else /* WINDOWS */
      if (execle (appl_name, argv0, NULL, environ) < 0)
	{
	  perror ("execle");
	}
      exit (0);
#endif /* !WINDOWS */
    }

  SERVICE_READY_WAIT (shm_appl->as_info[as_index].service_ready_flag);

  shm_appl->as_info[as_index].uts_status = UTS_STATUS_IDLE;
  shm_appl->as_info[as_index].pid = pid;

  shm_appl->as_info[as_index].reset_flag = FALSE;
  shm_appl->as_info[as_index].psize =
    getsize (shm_appl->as_info[as_index].pid);
  shm_appl->as_info[as_index].psize_time = time (NULL);
  shm_appl->as_info[as_index].last_access_time = time (NULL);
  shm_appl->as_info[as_index].transaction_start_time = (time_t) 0;
  shm_appl->as_info[as_index].clt_appl_name[0] = '\0';
  shm_appl->as_info[as_index].clt_req_path_info[0] = '\0';
  shm_appl->as_info[as_index].database_name[0] = '\0';
  shm_appl->as_info[as_index].database_host[0] = '\0';
  shm_appl->as_info[as_index].last_connect_time = 0;

  memset (&shm_appl->as_info[as_index].cas_clt_ip[0], 0x0,
	  sizeof (shm_appl->as_info[as_index].cas_clt_ip));
  shm_appl->as_info[as_index].cas_clt_port = 0;

  /* mutex exit section */
  shm_appl->as_info[as_index].mutex_flag[SHM_MUTEX_ADMIN] = FALSE;

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  FREE_MEM (env);

  return 0;

restart_error:
  if (shm_appl)
    uw_shm_detach (shm_appl);
  if (shm_br)
    uw_shm_detach (shm_br);
  return -1;
#endif /* CUBRID_SHARD */
}


int
admin_drop_cmd (int master_shm_id, const char *broker)
{
#if defined(CUBRID_SHARD)
  /* SHARD TODO : not implemented yet */
  return 0;
#else
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl_server;
  int br_index, i, appl_shm_key, as_index;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }
  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (broker, shm_br->br_info[i].name) == 0)
	{
	  appl_shm_key = shm_br->br_info[i].appl_server_shm_id;
	  br_index = i;
	  break;
	}
    }
  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker);
      uw_shm_detach (shm_br);
      return -1;
    }
  shm_appl_server =
    (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl_server == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  if (shm_br->br_info[br_index].appl_server_num <= 0)
    {
      strcpy (admin_err_msg, "Cannot drop appl server");
      uw_shm_detach (shm_br);
      uw_shm_detach (shm_appl_server);
      return -1;
    }

  if (shm_br->br_info[br_index].auto_add_appl_server == ON)
    goto finale;

  as_index = shm_br->br_info[br_index].appl_server_num - 1;
  shm_appl_server->as_info[as_index].service_flag = SERVICE_OFF;

  while (1)
    {
      if (shm_appl_server->as_info[as_index].service_flag == SERVICE_OFF_ACK)
	break;
      SLEEP_MILISEC (0, 500);
    }

  (shm_br->br_info[br_index].appl_server_num)--;
  (shm_appl_server->num_appl_server)--;

  as_inactivate (shm_appl_server->as_info[as_index].pid,
		 shm_br->br_info[br_index].name, as_index);
  shm_appl_server->as_info[as_index].pid = 0;

finale:
  uw_shm_detach (shm_br);
  uw_shm_detach (shm_appl_server);

  return 0;
#endif /* CUBRID_SHARD */
}

int
admin_on_cmd (int master_shm_id, const char *broker_name)
{
  int i;
  T_SHM_BROKER *shm_br;
  int res = 0;
#if defined(CUBRID_SHARD)
  int j;
  char *shm_metadata_p = NULL;
  char *shm_appl_svr_p = NULL;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHM_PROXY *proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p;
#endif /* CUBRID_SHARD */

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == ON)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is already running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
#if defined(CUBRID_SHARD)
	      shm_metadata_p =
		shard_metadata_initialize (&(shm_br->br_info[i]));
	      if (shm_metadata_p)
		{
		  shm_appl_svr_p =
		    shard_shm_initialize (&(shm_br->br_info[i]),
					  shm_metadata_p);
		  if (shm_appl_svr_p)
		    {
		      shm_as_p = shard_shm_get_appl_server (shm_appl_svr_p);
		      proxy_p = shard_shm_get_proxy (shm_appl_svr_p);
		    }
		}

	      if (shm_as_p == NULL || proxy_p == NULL)
		{
		  uw_shm_destroy (shm_br->br_info[i].appl_server_shm_id);
		  uw_shm_destroy (shm_br->br_info[i].metadata_shm_id);

		  res = -1;
		  break;
		}

	      for (j = 0, proxy_info_p =
		   shard_shm_get_first_proxy_info (proxy_p); proxy_info_p;
		   j++, proxy_info_p =
		   shard_shm_get_next_proxy_info (proxy_info_p))
		{
		  snprintf (proxy_info_p->access_log_file,
			    CONF_LOG_FILE_LEN - 1, "%s/%s_%d.access",
			    CUBRID_BASE_DIR, shm_br->br_info[i].name, j);
		  dir_repath (proxy_info_p->access_log_file);
		}

	      res =
		shard_process_activate (master_shm_id, &(shm_br->br_info[i]),
					shm_br, shm_appl_svr_p);
#else
	      res =
		br_activate (&(shm_br->br_info[i]), master_shm_id, shm_br);
#endif /* CUBRID_SHARD */
	    }
	  break;
	}
    }
  if (i >= shm_br->num_broker)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
#if defined(CUBRID_SHARD)
      if (shm_appl_svr_p)
	{
	  uw_shm_detach (shm_appl_svr_p);
	}
      if (shm_metadata_p)
	{
	  uw_shm_detach (shm_metadata_p);
	}
#endif /* CUBRID_SHARD */

      return -1;
    }

  uw_shm_detach (shm_br);
#if defined(CUBRID_SHARD)
  if (shm_appl_svr_p)
    {
      uw_shm_detach (shm_appl_svr_p);
    }
  if (shm_metadata_p)
    {
      uw_shm_detach (shm_metadata_p);
    }
#endif /* CUBRID_SHARD */


  return res;
}

int
admin_off_cmd (int master_shm_id, const char *broker_name)
{
  int i;
  T_SHM_BROKER *shm_br;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

#if !defined(WINDOWS)
  if (shm_br->owner_uid != getuid ())
    {
      strcpy (admin_err_msg, "Cannot stop broker. (Not owner)");
      return -1;
    }
#endif /* !WINDOWS */

  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is not running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
#if defined(CUBRID_SHARD)
	      shard_process_inactivate (&shm_br->br_info[i]);
#else
	      br_inactivate (&(shm_br->br_info[i]));
#endif /* CUBRID_SHARD */
	    }
	  break;
	}
    }
  if (i >= shm_br->num_broker)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

  uw_shm_detach (shm_br);
  return 0;
}

int
admin_broker_suspend_cmd (int master_shm_id, const char *broker_name)
{
#if defined(CUBRID_SHARD)
  /* SHARD TODO : not implemented yet */
  return 0;
#else
  int i, br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is not running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
	      br_index = i;
	    }
	  break;
	}
    }

  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
				       appl_server_shm_id, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl->suspend_mode = SUSPEND_REQ;
  while (1)
    {
      SLEEP_MILISEC (0, 100);
      if (shm_appl->suspend_mode == SUSPEND)
	{
	  break;
	}
    }

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  return 0;
#endif /* CUBRID_SHARD */
}

int
admin_broker_resume_cmd (int master_shm_id, const char *broker_name)
{
#if defined(CUBRID_SHARD)
  /* SHARD TODO : not implemented yet */
  return 0;
#else
  int i, br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is not running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
	      br_index = i;
	    }
	  break;
	}
    }

  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
				       appl_server_shm_id, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl->suspend_mode = SUSPEND_NONE;

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  return 0;
#endif /* CUBRID_SHARD */
}

int
admin_reset_cmd (int master_shm_id, const char *broker_name)
{
  int i, br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;
#if defined(CUBRID_SHARD)
  T_BROKER_INFO *br_info_p;
  T_SHM_PROXY *proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p;
  T_SHARD_INFO *shard_info_p;
  char *shm_as_cp = NULL;
#endif /* CUBRID_SHARD */

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is not running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
	      br_index = i;
	    }
	  break;
	}
    }

  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

#if defined(CUBRID_SHARD)
  br_info_p = &(shm_br->br_info[br_index]);
  shm_as_cp =
    (char *) uw_shm_open (br_info_p->appl_server_shm_id, SHM_APPL_SERVER,
			  SHM_MODE_ADMIN);
  if (shm_as_cp == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }
  proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (proxy_p == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      uw_shm_detach (shm_as_cp);
      return -1;
    }

  for (proxy_info_p = shard_shm_get_first_proxy_info (proxy_p);
       proxy_info_p;
       proxy_info_p = shard_shm_get_next_proxy_info (proxy_info_p))
    {
      for (shard_info_p = shard_shm_get_first_shard_info (proxy_info_p);
	   shard_info_p;
	   shard_info_p = shard_shm_get_next_shard_info (shard_info_p))
	{
	  for (i = 0; i < shard_info_p->num_appl_server; i++)
	    {
	      shard_info_p->as_info[i].reset_flag = TRUE;
	    }
	}
    }
  uw_shm_detach (shm_as_cp);
#else
  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
				       appl_server_shm_id, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  for (i = 0; i < shm_appl->num_appl_server; i++)
    {
      shm_appl->as_info[i].reset_flag = TRUE;
    }
  uw_shm_detach (shm_appl);
#endif /* CUBRID_SHARD */

  uw_shm_detach (shm_br);
  return 0;
}

int
admin_info_cmd (int master_shm_id)
{
  T_SHM_BROKER *shm_br;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				  SHM_MODE_MONITOR);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  broker_config_dump (stdout, shm_br->br_info, shm_br->num_broker,
		      master_shm_id);

  uw_shm_detach (shm_br);
  return 0;
}

int
admin_get_broker_status (int master_shm_id, const char *broker_name)
{
  int i, br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;
  int br_status;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is not running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
	      br_index = i;
	    }
	  break;
	}
    }

  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
				       appl_server_shm_id, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  br_status = shm_appl->suspend_mode;

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  return br_status;
}

int
admin_broker_job_first_cmd (int master_shm_id, const char *broker_name,
			    int job_id)
{
  int i, br_index;
  int ret_value = 0;
  int new_priority;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker[%s] is not running",
		       broker_name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
	      br_index = i;
	    }
	  break;
	}
    }

  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
				       appl_server_shm_id, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }

  shm_appl->suspend_mode = SUSPEND_CHANGE_PRIORITY_REQ;
  while (1)
    {
      SLEEP_MILISEC (0, 100);
      if (shm_appl->suspend_mode == SUSPEND_CHANGE_PRIORITY)
	break;
    }

  new_priority = shm_appl->job_queue[1].priority + 1;
  if (new_priority < 40)
    new_priority = 40;

  if (max_heap_change_priority (shm_appl->job_queue, job_id, new_priority) <
      0)
    {
      sprintf (admin_err_msg, "Job id (%d)  not found", job_id);
      ret_value = -1;
    }
  shm_appl->suspend_mode = SUSPEND_END_CHANGE_PRIORITY;
  while (1)
    {
      SLEEP_MILISEC (0, 100);
      if (shm_appl->suspend_mode == SUSPEND)
	break;
    }

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  return ret_value;
}

int
admin_broker_conf_change (int master_shm_id, const char *br_name,
			  const char *conf_name, const char *conf_value,
			  int as_number)
{
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, br_index;
  int appl_shm_key;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      sprintf (admin_err_msg, "Broker is not started.");
      goto set_broker_conf_error;
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
      sprintf (admin_err_msg, "Cannot find broker [%s]", br_name);
      goto set_broker_conf_error;
    }

  if (shm_br->br_info[br_index].service_flag == OFF)
    {
      sprintf (admin_err_msg, "Broker[%s] is not running.", br_name);
      goto set_broker_conf_error;
    }

  shm_appl = (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key,
						SHM_APPL_SERVER,
						SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      goto set_broker_conf_error;
    }

  if (strcasecmp (conf_name, "SQL_LOG") == 0)
    {
      char sql_log_mode;

      sql_log_mode = conf_get_value_sql_log_mode (conf_value);
      if (sql_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}

      if (as_number <= 0)
	{
	  shm_br->br_info[br_index].sql_log_mode = sql_log_mode;
	  shm_appl->sql_log_mode = sql_log_mode;
	  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	    {
	      shm_appl->as_info[i].cur_sql_log_mode = sql_log_mode;
	    }
	}
      else
	{
	  if (as_number > shm_appl->num_appl_server)
	    {
	      sprintf (admin_err_msg, "Invalid cas number : %d", as_number);
	      goto set_broker_conf_error;
	    }
	  shm_appl->as_info[as_number - 1].cur_sql_log_mode = sql_log_mode;
	}
    }
  else if (strcasecmp (conf_name, "SLOW_LOG") == 0)
    {
      int slow_log_mode;

      slow_log_mode = conf_get_value_table_on_off (conf_value);
      if (slow_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}

      if (as_number <= 0)
	{
	  shm_br->br_info[br_index].slow_log_mode = slow_log_mode;
	  shm_appl->slow_log_mode = slow_log_mode;
	  for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num; i++)
	    {
	      shm_appl->as_info[i].cur_slow_log_mode = slow_log_mode;
	    }
	}
      else
	{
	  if (as_number > shm_appl->num_appl_server)
	    {
	      sprintf (admin_err_msg, "Invalid cas number : %d", as_number);
	      goto set_broker_conf_error;
	    }
	  shm_appl->as_info[as_number - 1].cur_slow_log_mode = slow_log_mode;
	}
    }
  else if (strcasecmp (conf_name, "ACCESS_MODE") == 0)
    {
      char access_mode = conf_get_value_access_mode (conf_value);

      if (access_mode == -1)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      if (shm_br->br_info[br_index].access_mode == access_mode)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].access_mode = access_mode;
      shm_appl->access_mode = access_mode;
      for (i = 0; i < shm_appl->num_appl_server; i++)
	{
	  shm_appl->as_info[i].reset_flag = TRUE;
	}
    }
  else if (strcasecmp (conf_name, "SQL_LOG_MAX_SIZE") == 0)
    {
      int sql_log_max_size;

      sql_log_max_size = atoi (conf_value);
      if (sql_log_max_size <= 0)
	{
	  sql_log_max_size = DEFAULT_SQL_LOG_MAX_SIZE;
	}
      if (sql_log_max_size > MAX_SQL_LOG_MAX_SIZE)
	{
	  sql_log_max_size = MAX_SQL_LOG_MAX_SIZE;
	}
      shm_br->br_info[br_index].sql_log_max_size = sql_log_max_size;
      shm_appl->sql_log_max_size = sql_log_max_size;
    }
  else if (strcasecmp (conf_name, "LONG_QUERY_TIME") == 0)
    {
      float long_query_time;

      long_query_time = (float) strtod (conf_value, NULL);
      if (long_query_time <= 0)
	{
	  long_query_time = (float) DEFAULT_LONG_QUERY_TIME;
	}
      shm_br->br_info[br_index].long_query_time =
	(int) (long_query_time * 1000.0);
      shm_appl->long_query_time = (int) (long_query_time * 1000.0);
    }
  else if (strcasecmp (conf_name, "LONG_TRANSACTION_TIME") == 0)
    {
      float long_transaction_time;

      long_transaction_time = (float) strtod (conf_value, NULL);
      if (long_transaction_time <= 0)
	{
	  long_transaction_time = (float) DEFAULT_LONG_TRANSACTION_TIME;
	}
      shm_br->br_info[br_index].long_transaction_time =
	(int) (long_transaction_time * 1000.0);
      shm_appl->long_transaction_time =
	(int) (long_transaction_time * 1000.0);
    }
  else if (strcasecmp (conf_name, "APPL_SERVER_MAX_SIZE") == 0)
    {
      int max_size;

      max_size = atoi (conf_value);
      max_size *= ONE_K;
      shm_br->br_info[br_index].appl_server_max_size = max_size;
      shm_appl->appl_server_max_size = max_size;
    }
  else if (strcasecmp (conf_name, "APPL_SERVER_MAX_SIZE_HARD_LIMIT") == 0)
    {
      int hard_limit;

      hard_limit = atoi (conf_value);
      hard_limit *= ONE_K;
      shm_br->br_info[br_index].appl_server_hard_limit = hard_limit;
      shm_appl->appl_server_hard_limit = hard_limit;
    }
  else if (strcasecmp (conf_name, "LOG_BACKUP") == 0)
    {
      int log_backup;

      log_backup = conf_get_value_table_on_off (conf_value);
      if (log_backup < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].log_backup = log_backup;
    }
  else if (strcasecmp (conf_name, "TIME_TO_KILL") == 0)
    {
      int time_to_kill;

      time_to_kill = atoi (conf_value);
      if (time_to_kill <= 0)
	{
	  time_to_kill = DEFAULT_TIME_TO_KILL;
	}
      shm_br->br_info[br_index].time_to_kill = time_to_kill;
    }
  else if (strcasecmp (conf_name, "ACCESS_LOG") == 0)
    {
      int access_log_flag;

      access_log_flag = conf_get_value_table_on_off (conf_value);
      if (access_log_flag < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].access_log = access_log_flag;
      shm_appl->access_log = access_log_flag;
    }
  else if (strcasecmp (conf_name, "KEEP_CONNECTION") == 0)
    {
      int keep_con;

      keep_con = conf_get_value_keep_con (conf_value);
      if (keep_con < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].keep_connection = keep_con;
      shm_appl->keep_connection = keep_con;
    }
  else if (strcasecmp (conf_name, "CACHE_USER_INFO") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].cache_user_info = val;
      shm_appl->cache_user_info = val;
    }
  else if (strcasecmp (conf_name, "SQL_LOG2") == 0)
    {
      int val;

      val = atoi (conf_value);
      if (val < SQL_LOG2_NONE || val > SQL_LOG2_MAX)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].sql_log2 = val;
      shm_appl->sql_log2 = val;
    }
  else if (strcasecmp (conf_name, "STATEMENT_POOLING") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].statement_pooling = val;
      shm_appl->statement_pooling = val;
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].jdbc_cache = val;
      shm_appl->jdbc_cache = val;
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE_ONLY_HINT") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].jdbc_cache_only_hint = val;
      shm_appl->jdbc_cache_only_hint = val;
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE_LIFE_TIME") == 0)
    {
      int val;

      val = atoi (conf_value);
      shm_br->br_info[br_index].jdbc_cache_life_time = val;
      shm_appl->jdbc_cache_life_time = val;
    }
  else if (strcasecmp (conf_name, "CCI_PCONNECT") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].cci_pconnect = val;
      shm_appl->cci_pconnect = val;
    }
  else if (strcasecmp (conf_name, "SELECT_AUTO_COMMIT") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].select_auto_commit = val;
      shm_appl->select_auto_commit = val;
    }
  else if (strcasecmp (conf_name, "MAX_QUERY_TIMEOUT") == 0)
    {
      int val;

      val = atoi (conf_value);
      if (val < 0 || val > MAX_QUERY_TIMEOUT_LIMIT)
	{
	  sprintf (admin_err_msg, "invalid value: %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].query_timeout = val;
      shm_appl->query_timeout = val;
    }
  else
    {
      sprintf (admin_err_msg, "unknown keyword %s", conf_name);
      goto set_broker_conf_error;
    }

  if (shm_appl)
    uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  return 0;

set_broker_conf_error:
  if (shm_appl)
    uw_shm_detach (shm_appl);
  if (shm_br)
    uw_shm_detach (shm_br);
  return -1;
}

int
admin_del_cas_log (int master_shmid, const char *broker, int asid)
{
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, br_index;
  int appl_shm_key;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shmid, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    goto error;

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (broker, shm_br->br_info[i].name) == 0)
	{
	  appl_shm_key = shm_br->br_info[i].appl_server_shm_id;
	  br_index = i;
	  break;
	}
    }
  if (br_index < 0)
    goto error;

  shm_appl =
    (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    goto error;

  if (asid < 1 || asid > shm_br->br_info[br_index].appl_server_max_num)
    goto error;
  asid--;

  if (shm_appl->as_info[asid].service_flag != SERVICE_ON
      ||
      (IS_NOT_APPL_SERVER_TYPE_CAS (shm_br->br_info[br_index].appl_server)))
    {
      goto error;
    }

  shm_appl->as_info[asid].cas_log_reset = CAS_LOG_RESET_REMOVE;

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  return 0;

error:
  if (shm_appl)
    uw_shm_detach (shm_appl);
  if (shm_br)
    uw_shm_detach (shm_br);
  return -1;
}

void
admin_init_env ()
{
  int i, j;
  char *p;
  const char *clt_envs[] = { SID_ENV_STR,
    PATH_INFO_ENV_STR,
    REQUEST_METHOD_ENV_STR,
    CONTENT_LENGTH_ENV_STR,
    DELIMITER_ENV_STR,
    OUT_FILE_NAME_ENV_STR,
    CLT_APPL_NAME_ENV_STR,
    SESSION_REQUEST_ENV_STR,
    QUERY_STRING_ENV_STR,
    REMOTE_ADDR_ENV_STR,
    NULL
  };

  for (i = 0; environ[i] != NULL; i++)
    {
      p = strchr (environ[i], '=');
      if (p == NULL)
	{
	  environ[i] = (char *) "DUMMY_ENV=VISION_THREE";
	  continue;
	}
      for (j = 0; clt_envs[j] != NULL; j++)
	{
	  if (strncmp (environ[i], clt_envs[j], strlen (clt_envs[j])) == 0)
	    {
	      environ[i] = (char *) "DUMMY_ENV=VISION_THREE";
	      break;
	    }
	}
    }
}

int
admin_broker_acl_status_cmd (int master_shm_id, const char *broker_name)
{
  int i, j, k, m;
  int br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				  SHM_MODE_MONITOR);

  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  if (broker_name != NULL)
    {
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	    {
	      br_index = i;
	      break;
	    }
	}
      if (br_index == -1)
	{
	  sprintf (admin_err_msg, "Cannot find broker [%s]\n", broker_name);
	  uw_shm_detach (shm_br);
	  return -1;
	}
    }

  fprintf (stdout, "ACCESS_CONTROL=%s\n",
	   (shm_br->access_control) ? "ON" : "OFF");
  fprintf (stdout, "ACCESS_CONTROL_FILE=%s\n\n", shm_br->access_control_file);

  if (shm_br->access_control == false ||
      shm_br->access_control_file[0] == '\0')
    {
      uw_shm_detach (shm_br);
      return 0;
    }

  if (br_index < 0)
    {
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      continue;
	    }

	  shm_appl =
	    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[i].
					       appl_server_shm_id,
					       SHM_APPL_SERVER,
					       SHM_MODE_MONITOR);
	  if (shm_appl == NULL)
	    {
	      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
				uw_get_os_error_code ());
	      uw_shm_detach (shm_br);
	      return -1;
	    }

	  fprintf (stdout, "[%%%s]\n", shm_appl->broker_name);

	  for (j = 0; j < shm_appl->num_access_info; j++)
	    {
	      fprintf (stdout, "%s:%s:%s\n", shm_appl->access_info[j].dbname,
		       shm_appl->access_info[j].dbuser,
		       shm_appl->access_info[j].ip_files);
	      for (k = 0; k < shm_appl->access_info[j].ip_info.num_list; k++)
		{
		  int address_index = k * IP_BYTE_COUNT;

		  for (m = 0;
		       m <
		       shm_appl->access_info[j].ip_info.
		       address_list[address_index]; m++)
		    {
		      fprintf (stdout, "%d%s",
			       shm_appl->access_info[j].ip_info.
			       address_list[address_index + m + 1],
			       ((m != 3) ? "." : ""));
		    }

		  if (m != 4)
		    {
		      fprintf (stdout, "*");
		    }

		  fprintf (stdout, "\n");
		}
	      fprintf (stdout, "\n");
	    }
	  uw_shm_detach (shm_appl);
	}
    }
  else
    {
      shm_appl =
	(T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
					   appl_server_shm_id,
					   SHM_APPL_SERVER, SHM_MODE_MONITOR);
      if (shm_appl == NULL)
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  uw_shm_detach (shm_br);
	  return -1;
	}

      fprintf (stdout, "[%%%s]\n", shm_appl->broker_name);

      for (j = 0; j < shm_appl->num_access_info; j++)
	{
	  fprintf (stdout, "%s:%s:%s\n", shm_appl->access_info[j].dbname,
		   shm_appl->access_info[j].dbuser,
		   shm_appl->access_info[j].ip_files);
	  for (k = 0; k < shm_appl->access_info[j].ip_info.num_list; k++)
	    {
	      int address_index = k * IP_BYTE_COUNT;

	      for (m = 0;
		   m <
		   shm_appl->access_info[j].ip_info.
		   address_list[address_index]; m++)
		{
		  fprintf (stdout, "%d%s",
			   shm_appl->access_info[j].ip_info.
			   address_list[address_index + m + 1],
			   ((m != 3) ? "." : ""));
		}

	      if (m != 4)
		{
		  fprintf (stdout, "*");
		}
	      fprintf (stdout, "\n");
	    }
	  fprintf (stdout, "\n");
	}
      uw_shm_detach (shm_appl);
    }

  uw_shm_detach (shm_br);
  return 0;
}

int
admin_broker_acl_reload_cmd (int master_shm_id, const char *broker_name)
{
  int i;
  int br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;
  char *access_file_name;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);

  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  br_index = -1;
  if (broker_name != NULL)
    {
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	    {
	      br_index = i;
	      break;
	    }
	}
      if (br_index == -1)
	{
	  sprintf (admin_err_msg, "Cannot find broker [%s]\n", broker_name);
	  uw_shm_detach (shm_br);
	  return -1;
	}
    }

  if (shm_br->access_control == false ||
      shm_br->access_control_file[0] == '\0')
    {
      uw_shm_detach (shm_br);
      return 0;
    }

  set_cubrid_file (FID_ACCESS_CONTROL_FILE, shm_br->access_control_file);
  access_file_name = get_cubrid_file_ptr (FID_ACCESS_CONTROL_FILE);

  if (br_index < 0)
    {
      for (i = 0; i < shm_br->num_broker; i++)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      continue;
	    }

	  shm_appl =
	    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[i].
					       appl_server_shm_id,
					       SHM_APPL_SERVER,
					       SHM_MODE_ADMIN);
	  if (shm_appl == NULL)
	    {
	      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
				uw_get_os_error_code ());
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  if (read_from_access_control_file (shm_appl, access_file_name) != 0)
	    {
	      uw_shm_detach (shm_appl);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  uw_shm_detach (shm_appl);
	}
    }
  else
    {
      shm_appl =
	(T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
					   appl_server_shm_id,
					   SHM_APPL_SERVER, SHM_MODE_ADMIN);
      if (shm_appl == NULL)
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  uw_shm_detach (shm_br);
	  return -1;
	}

      if (read_from_access_control_file (shm_appl, access_file_name) != 0)
	{
	  uw_shm_detach (shm_appl);
	  uw_shm_detach (shm_br);
	  return -1;
	}

      uw_shm_detach (shm_appl);
    }

  uw_shm_detach (shm_br);
  return 0;
}

#if defined(WINDOWS)
static int
admin_get_host_ip (unsigned char *ip_addr)
{
  char hostname[64];
  struct hostent *hp;

  if (gethostname (hostname, sizeof (hostname)) < 0)
    {
      strcpy (admin_err_msg, "gethostname error");
      return -1;
    }
  if ((hp = gethostbyname (hostname)) == NULL)
    {
      sprintf (admin_err_msg, "unknown host : %s", hostname);
      return -1;
    }
  memcpy (ip_addr, hp->h_addr_list[0], 4);

  return 0;
}
#endif /* WINDOWS */

#if !defined(CUBRID_SHARD)
static int
br_activate (T_BROKER_INFO * br_info, int master_shm_id,
	     T_SHM_BROKER * shm_br)
{
  int shm_size, pid, i;
  T_SHM_APPL_SERVER *shm_appl;
  char **env;
  int env_num;
  char port_str[PATH_MAX];
  char appl_name_str[32];
  char master_shm_key_str[32];
  char appl_server_shm_key_str[32];
  char error_log_lock_file[128];
  const char *broker_exe_name;
  char err_flag = FALSE;
  int broker_check_loop_count = 30;

  br_info->err_code = -1;
  br_info->os_err_code = 0;
  br_info->num_busy_count = 0;

  shm_size = sizeof (T_SHM_APPL_SERVER) +
    (br_info->appl_server_max_num - 1) * sizeof (T_APPL_SERVER_INFO);
  shm_appl = (T_SHM_APPL_SERVER *) uw_shm_create (br_info->appl_server_shm_id,
						  shm_size, SHM_APPL_SERVER);

  if (shm_appl == NULL)
    {
      sprintf (admin_err_msg, "%s: cannot create shared memory",
	       br_info->name);
      return -1;
    }

  shm_appl->cci_default_autocommit = br_info->cci_default_autocommit;
  shm_appl->suspend_mode = SUSPEND_NONE;
  shm_appl->job_queue_size = br_info->job_queue_size;
  shm_appl->job_queue[0].id = 0;	/* initialize max heap */
  shm_appl->max_prepared_stmt_count = br_info->max_prepared_stmt_count;
  strcpy (shm_appl->log_dir, br_info->log_dir);
  strcpy (shm_appl->slow_log_dir, br_info->slow_log_dir);
  strcpy (shm_appl->err_log_dir, br_info->err_log_dir);
  strcpy (shm_appl->broker_name, br_info->name);
  shm_appl->access_control = shm_br->access_control;
  shm_appl->acl_chn = 0;

  if (shm_br->access_control && shm_br->access_control_file[0] != '\0')
    {
      char *access_file_name;
#if defined (WINDOWS)
      char sem_name[BROKER_NAME_LEN];

      MAKE_ACL_SEM_NAME (sem_name, br_info->name);

      if (uw_sem_init (sem_name) < 0)
	{
	  sprintf (admin_err_msg, "%s: cannot initialize acl semaphore",
		   br_info->name);
	  uw_shm_detach (shm_appl);
	  uw_shm_destroy (br_info->appl_server_shm_id);
	  return -1;
	}
#else
      if (uw_sem_init (&shm_appl->acl_sem) < 0)
	{
	  sprintf (admin_err_msg, "%s: cannot initialize acl semaphore",
		   br_info->name);
	  uw_shm_detach (shm_appl);
	  uw_shm_destroy (br_info->appl_server_shm_id);
	  return -1;
	}
#endif
      if (shm_br->access_control_file[0] != '\0')
	{
	  set_cubrid_file (FID_ACCESS_CONTROL_FILE,
			   shm_br->access_control_file);
	  access_file_name = get_cubrid_file_ptr (FID_ACCESS_CONTROL_FILE);
	  if (read_from_access_control_file (shm_appl, access_file_name) != 0)
	    {
	      uw_shm_detach (shm_appl);
	      uw_shm_destroy (br_info->appl_server_shm_id);
	      return -1;
	    }
	}
    }
#if defined(WINDOWS)
  shm_appl->use_pdh_flag = FALSE;
  br_info->pdh_workset = 0;
  br_info->pdh_pct_cpu = 0;
#endif /* WINDOWS */

  env = make_env (br_info->source_env, &env_num);

  strncpy (shm_appl->appl_server_name,
	   get_appl_server_name (br_info->appl_server, env, env_num),
	   APPL_SERVER_NAME_MAX_SIZE);
  shm_appl->appl_server_name[APPL_SERVER_NAME_MAX_SIZE - 1] = '\0';

#if !defined(WINDOWS)
  signal (SIGCHLD, SIG_IGN);
#endif

#if !defined(WINDOWS)
  if ((pid = fork ()) < 0)
    {
      strcpy (admin_err_msg, "fork error");
      uw_shm_detach (shm_appl);
      uw_shm_destroy (br_info->appl_server_shm_id);
      FREE_MEM (env);
      return -1;
    }
#endif /* WINDOWS */

  br_info->ready_to_service = false;
#if !defined(WINDOWS)
  if (pid == 0)
    {
      signal (SIGCHLD, SIG_DFL);
#if defined(V3_ADMIN_D)
      if (admin_clt_sock_fd > 0)
	CLOSE_SOCKET (admin_clt_sock_fd);
      if (admin_srv_sock_fd > 0)
	CLOSE_SOCKET (admin_srv_sock_fd);
#endif /* V3_ADMIN_D */
#endif /* WINDOWS */

      if (env != NULL)
	{
	  for (i = 0; i < env_num; i++)
	    putenv (env[i]);
	}

      sprintf (port_str, "%s=%d", PORT_NUMBER_ENV_STR, br_info->port);
      putenv (port_str);
      sprintf (master_shm_key_str, "%s=%d", MASTER_SHM_KEY_ENV_STR,
	       master_shm_id);
      putenv (master_shm_key_str);
      sprintf (appl_server_shm_key_str, "%s=%d", APPL_SERVER_SHM_KEY_STR,
	       br_info->appl_server_shm_id);
      putenv (appl_server_shm_key_str);

      if (IS_APPL_SERVER_TYPE_CAS (br_info->appl_server))
	broker_exe_name = NAME_CAS_BROKER;
      else
	broker_exe_name = NAME_BROKER;

#if defined(WINDOWS)
      if (IS_APPL_SERVER_TYPE_CAS (br_info->appl_server)
	  && br_info->appl_server_port < 0)
	broker_exe_name = NAME_CAS_BROKER2;
#endif /* WINDOWS */

      sprintf (appl_name_str, "%s=%s", APPL_NAME_ENV_STR, broker_exe_name);
      putenv (appl_name_str);

      sprintf (error_log_lock_file, "%s=%s.log.lock",
	       ERROR_LOG_LOCK_FILE_ENV_STR, br_info->name);
      putenv (error_log_lock_file);

#if !defined(WINDOWS)
      uw_shm_detach (shm_appl);
      uw_shm_detach (shm_br);
#endif /* !WINDOWS */

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

  br_info->pid = pid;

  shm_appl->num_appl_server = br_info->appl_server_num;
  shm_appl->sql_log_mode = br_info->sql_log_mode;
  shm_appl->slow_log_mode = br_info->slow_log_mode;
  shm_appl->sql_log_max_size = br_info->sql_log_max_size;
  shm_appl->long_query_time = br_info->long_query_time;
  shm_appl->long_transaction_time = br_info->long_transaction_time;
  shm_appl->appl_server_max_size = br_info->appl_server_max_size;
  shm_appl->appl_server_hard_limit = br_info->appl_server_hard_limit;
  shm_appl->session_timeout = br_info->session_timeout;
  shm_appl->sql_log2 = br_info->sql_log2;
#if defined(WINDOWS)
  shm_appl->as_port = br_info->appl_server_port;
#endif /* WINDOWS */
  shm_appl->max_string_length = br_info->max_string_length;
  shm_appl->stripped_column_name = br_info->stripped_column_name;
  shm_appl->keep_connection = br_info->keep_connection;
  shm_appl->cache_user_info = br_info->cache_user_info;
  shm_appl->statement_pooling = br_info->statement_pooling;
  shm_appl->access_mode = br_info->access_mode;
  shm_appl->cci_pconnect = br_info->cci_pconnect;
  shm_appl->select_auto_commit = br_info->select_auto_commit;

  shm_appl->access_log = br_info->access_log;

  shm_appl->jdbc_cache = br_info->jdbc_cache;
  shm_appl->jdbc_cache_only_hint = br_info->jdbc_cache_only_hint;
  shm_appl->jdbc_cache_life_time = br_info->jdbc_cache_life_time;

  strcpy (shm_appl->preferred_hosts, br_info->preferred_hosts);

  shm_appl->query_timeout = br_info->query_timeout;

  for (i = 0; i < shm_appl->num_appl_server; i++)
    {
      shm_appl->as_info[i].last_access_time = time (NULL);
      shm_appl->as_info[i].transaction_start_time = (time_t) 0;
      as_activate (&(shm_appl->as_info[i]),
		   i, br_info, env, env_num, shm_appl, shm_br);
    }
  for (; i < br_info->appl_server_max_num; i++)
    {
      shm_appl->as_info[i].service_flag = SERVICE_OFF;
      shm_appl->as_info[i].last_access_time = time (NULL);
      shm_appl->as_info[i].transaction_start_time = (time_t) 0;
      shm_appl->as_info[i].mutex_flag[SHM_MUTEX_BROKER] = FALSE;
      shm_appl->as_info[i].mutex_flag[SHM_MUTEX_ADMIN] = FALSE;
      shm_appl->as_info[i].mutex_turn = SHM_MUTEX_BROKER;
      shm_appl->as_info[i].num_request = 0;
      shm_appl->as_info[i].num_requests_received = 0;
      shm_appl->as_info[i].num_transactions_processed = 0;
      shm_appl->as_info[i].num_queries_processed = 0;
      shm_appl->as_info[i].num_long_queries = 0;
      shm_appl->as_info[i].num_long_transactions = 0;
      shm_appl->as_info[i].num_error_queries = 0;
      shm_appl->as_info[i].num_interrupts = 0;
      shm_appl->as_info[i].num_connect_requests = 0;
      shm_appl->as_info[i].num_restarts = 0;
      shm_appl->as_info[i].auto_commit_mode = FALSE;
      shm_appl->as_info[i].database_name[0] = '\0';
      shm_appl->as_info[i].database_host[0] = '\0';
      shm_appl->as_info[i].last_connect_time = 0;
      shm_appl->as_info[i].cur_sql_log_mode = br_info->sql_log_mode;
      shm_appl->as_info[i].cur_slow_log_mode = br_info->slow_log_mode;
      CON_STATUS_LOCK_INIT (&(shm_appl->as_info[i]));
    }

  br_info->ready_to_service = true;
  br_info->service_flag = ON;

  uw_shm_detach (shm_appl);

  for (i = 0; i < broker_check_loop_count; i++)
    {
      if (br_info->err_code < 0)
	{
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  if (br_info->err_code > 0)
	    {
	      sprintf (admin_err_msg, "%s: %s", br_info->name,
		       uw_get_error_message (br_info->err_code,
					     br_info->os_err_code));
	      err_flag = TRUE;
	    }
	  break;
	}
    }

  if (i == broker_check_loop_count)
    {
      sprintf (admin_err_msg, "%s: unknown error", br_info->name);
      err_flag = TRUE;
    }

  if (err_flag)
    {
      char err_msg_backup[ADMIN_ERR_MSG_SIZE];
      memcpy (err_msg_backup, admin_err_msg, ADMIN_ERR_MSG_SIZE);
      br_inactivate (br_info);
      memcpy (admin_err_msg, err_msg_backup, ADMIN_ERR_MSG_SIZE);
      FREE_MEM (env);
      return -1;
    }

  return 0;
}

static int
br_inactivate (T_BROKER_INFO * br_info)
{
  T_SHM_APPL_SERVER *shm_appl;
  time_t cur_time = time (NULL);
  struct tm ct;
  int i;
  char cmd_buf[BUFSIZ];
#if defined(WINDOWS)
  char acl_sem_name[BROKER_NAME_LEN];
#endif

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

  if (br_info->pid)
    {
      /* proxy_id and shard_id argument is only use in CUBRID SHARD */
      /* so, please set PROXY_INVALID_ID and SHARD_INVALID_ID in normal Broker */
      ut_kill_process (br_info->pid, NULL, PROXY_INVALID_ID, SHARD_INVALID_ID,
		       CAS_INVALID_ID);
      SLEEP_MILISEC (1, 0);
    }

  shm_appl = (T_SHM_APPL_SERVER *)
    uw_shm_open (br_info->appl_server_shm_id, SHM_APPL_SERVER,
		 SHM_MODE_ADMIN);

  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

  for (i = 0; i < br_info->appl_server_max_num; i++)
    {
      as_inactivate (shm_appl->as_info[i].pid, br_info->name, i);
      shm_appl->as_info[i].pid = 0;
      CON_STATUS_LOCK_DESTROY (&(shm_appl->as_info[i]));
    }
  br_info->appl_server_num = br_info->appl_server_min_num;

  if (br_info->log_backup == ON)
    {
      sprintf (cmd_buf, "%s.%02d%02d%02d.%02d%02d",
	       br_info->access_log_file,
	       ct.tm_year, ct.tm_mon + 1, ct.tm_mday, ct.tm_hour, ct.tm_min);
      rename (br_info->access_log_file, cmd_buf);

      sprintf (cmd_buf, "%s.%02d%02d%02d.%02d%02d",
	       br_info->error_log_file,
	       ct.tm_year, ct.tm_mon + 1, ct.tm_mday, ct.tm_hour, ct.tm_min);
      rename (br_info->error_log_file, cmd_buf);
    }
  else
    {
      unlink (br_info->access_log_file);
      unlink (br_info->error_log_file);
    }

  br_info->num_busy_count = 0;

#if defined (WINDOWS)
  MAKE_ACL_SEM_NAME (acl_sem_name, shm_appl->broker_name);
  uw_sem_destroy (acl_sem_name);
#else
  uw_sem_destroy (&shm_appl->acl_sem);
#endif

  uw_shm_detach (shm_appl);
  uw_shm_destroy (br_info->appl_server_shm_id);

  br_info->service_flag = OFF;

  return 0;
}

static void
as_activate (T_APPL_SERVER_INFO * as_info, int as_index,
	     T_BROKER_INFO * br_info, char **env, int env_num,
	     T_SHM_APPL_SERVER * shm_appl, T_SHM_BROKER * shm_br)
{
  int pid;
  char port_str[PATH_MAX];
  char appl_name_str[64];
  char appl_server_shm_key_str[32];
  char access_log_env_str[256];
  char error_log_env_str[256];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
  char error_log_lock_file[128];
  int i;
  char port_name[PATH_MAX], dirname[PATH_MAX];
#if !defined(WINDOWS)
  char process_name[128];
#endif /* !WINDOWS */

  get_cubrid_file (FID_SOCK_DIR, dirname, PATH_MAX);
  snprintf (port_name, sizeof (port_name) - 1, "%s%s.%d", dirname,
	    br_info->name, as_index + 1);
#if !defined(WINDOWS)
  unlink (port_name);
#endif /* !WINDOWS */

  /* mutex variable initialize */
  as_info->mutex_flag[SHM_MUTEX_BROKER] = FALSE;
  as_info->mutex_flag[SHM_MUTEX_ADMIN] = FALSE;
  as_info->mutex_turn = SHM_MUTEX_BROKER;
  CON_STATUS_LOCK_INIT (as_info);

  as_info->num_request = 0;
  as_info->uts_status = UTS_STATUS_START;
  as_info->reset_flag = FALSE;
  as_info->clt_appl_name[0] = '\0';
  as_info->clt_req_path_info[0] = '\0';
  as_info->cur_sql_log_mode = shm_appl->sql_log_mode;
  as_info->cur_slow_log_mode = shm_appl->slow_log_mode;

  memset (&as_info->cas_clt_ip[0], 0x0, sizeof (as_info->cas_clt_ip));
  as_info->cas_clt_port = 0;

#if defined(WINDOWS)
  as_info->pdh_pid = 0;
  as_info->pdh_workset = 0;
  as_info->pdh_pct_cpu = 0;
#endif /* WINDOWS */

  as_info->service_ready_flag = FALSE;

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

      sprintf (port_str, "%s=%s", PORT_NAME_ENV_STR, port_name);
      putenv (port_str);
      sprintf (appl_server_shm_key_str, "%s=%d", APPL_SERVER_SHM_KEY_STR,
	       br_info->appl_server_shm_id);
      putenv (appl_server_shm_key_str);
      strcpy (appl_name, shm_appl->appl_server_name);

      sprintf (appl_name_str, "%s=%s", APPL_NAME_ENV_STR, appl_name);
      putenv (appl_name_str);
      sprintf (access_log_env_str, "%s=%s",
	       ACCESS_LOG_ENV_STR, br_info->access_log_file);
      putenv (access_log_env_str);
      sprintf (error_log_env_str, "%s=%s",
	       ERROR_LOG_ENV_STR, br_info->error_log_file);
      putenv (error_log_env_str);
      sprintf (error_log_lock_file, "%s=%s.log.lock",
	       ERROR_LOG_LOCK_FILE_ENV_STR, br_info->name);
      putenv (error_log_lock_file);

#if !defined(WINDOWS)
      if (br_info->appl_server == APPL_SERVER_CAS_ORACLE)
	{
	  snprintf (process_name, sizeof (process_name) - 1, "%s", appl_name);
	}
      else
	{
	  snprintf (process_name, sizeof (process_name) - 1, "%s_%s_%d",
		    br_info->name, appl_name, as_index + 1);
	}
      uw_shm_detach (shm_appl);
      uw_shm_detach (shm_br);
#endif /* !WINDOWS */

#if defined(WINDOWS)
      pid = run_child (appl_name);
#else /* WINDOWS */
      if (execle (appl_name, process_name, NULL, environ) < 0)
	{
	  perror (appl_name);
	}
      exit (0);
#endif /* WINDOWS */
    }

  SERVICE_READY_WAIT (as_info->service_ready_flag);

  as_info->pid = pid;
  as_info->last_access_time = time (NULL);
  as_info->transaction_start_time = (time_t) 0;
  as_info->psize_time = time (NULL);
  as_info->psize = getsize (as_info->pid);
  as_info->uts_status = UTS_STATUS_IDLE;
  as_info->service_flag = SERVICE_ON;
}

static void
as_inactivate (int as_pid, char *br_name, int as_index)
{
  if (as_pid)
    {
      /* proxy_id and shard_id argument is only use in CUBRID SHARD */
      /* so, please set PROXY_INVALID_ID and SHARD_INVALID_ID in normal Broker */
      ut_kill_process (as_pid, br_name, PROXY_INVALID_ID, SHARD_INVALID_ID,
		       as_index);
    }
}

static char **
make_env (char *env_file, int *env_num)
{
  char **env = NULL;
  int num, read_num;
  FILE *env_fp;
  char read_buf[BUFSIZ], col1[128], col2[128];

  *env_num = 0;

  if (env_file[0] == '\0')
    return NULL;

  env_fp = fopen (env_file, "r");
  if (env_fp == NULL)
    return NULL;

  num = 0;

  while (fgets (read_buf, BUFSIZ, env_fp) != NULL)
    {
      if (read_buf[0] == '#')
	continue;
      read_num = sscanf (read_buf, "%127s%127s", col1, col2);
      if (read_num != 2)
	continue;

      if (env == NULL)
	env = (char **) malloc (sizeof (char *));
      else
	env = (char **) realloc (env, sizeof (char *) * (num + 1));
      if (env == NULL)
	break;

      env[num] = (char *) malloc (strlen (col1) + strlen (col2) + 2);
      if (env[num] == NULL)
	{
	  for (num--; num >= 0; num--)
	    FREE_MEM (env[num]);
	  FREE_MEM (env);
	  env = NULL;
	  break;
	}

      sprintf (env[num], "%s=%s", col1, col2);
      num++;
    }

  fclose (env_fp);

  *env_num = num;
  return env;
}

static const char *
get_appl_server_name (int appl_server_type, char **env, int env_num)
{
#if !defined(WINDOWS)
  int dbms_version;
#endif /* !WINDOWS */

  if (env != NULL)
    {
      const char *p = "UC_APPL_SERVER_EXE_NAME=";
      int i;
      for (i = 0; i < env_num; i++)
	{
	  if (strncmp (env[i], p, strlen (p)) == 0)
	    {
	      return (env[i] + strlen (p));
	    }
	}
    }
  if (appl_server_type == APPL_SERVER_CAS_ORACLE)
    return APPL_SERVER_CAS_ORACLE_NAME;
  if (appl_server_type == APPL_SERVER_CAS_MYSQL)
    return APPL_SERVER_CAS_MYSQL_NAME;
  return APPL_SERVER_CAS_NAME;
}
#endif /* !CUBRID_SHARD */

static ACCESS_INFO *
find_access_info (ACCESS_INFO ai[], int size, char *dbname, char *dbuser)
{
  int i;

  for (i = 0; i < size; i++)
    {
      if (strcmp (ai[i].dbname, dbname) == 0
	  && strcmp (ai[i].dbuser, dbuser) == 0)
	{
	  return &ai[i];
	}
    }

  return NULL;
}

static int
read_from_access_control_file (T_SHM_APPL_SERVER * shm_appl, char *filename)
{
  char buf[1024], path_buf[256], *files, *token, *save;
  FILE *fd_access_list;
  int num_access_list = 0, line = 0;
  ACCESS_INFO new_access_info[ACL_MAX_ITEM_COUNT];
  ACCESS_INFO *access_info;
  bool is_current_broker_section;
#if defined(WINDOWS)
  char acl_sem_name[BROKER_NAME_LEN];
#endif

  fd_access_list = fopen (filename, "r");

  if (fd_access_list == NULL)
    {
      sprintf (admin_err_msg,
	       "%s: error while loading access control file(%s)",
	       shm_appl->broker_name, filename);
      return -1;
    }

  is_current_broker_section = false;

  memset (new_access_info, '\0', sizeof (new_access_info));

  while (fgets (buf, 1024, fd_access_list))
    {
      char *dbname, *dbuser, *ip_file, *p;

      line++;
      p = strchr (buf, '#');
      if (p != NULL)
	{
	  *p = '\0';
	}

      trim (buf);

      if (buf[0] == '\0')
	{
	  continue;
	}

      if (is_current_broker_section == false &&
	  strncmp (buf, "[%", 2) == 0 && buf[strlen (buf) - 1] == ']')
	{
	  buf[strlen (buf) - 1] = '\0';
	  if (strcasecmp (shm_appl->broker_name, buf + 2) == 0)
	    {
	      is_current_broker_section = true;
	      continue;
	    }
	}
      if (is_current_broker_section == false)
	{
	  continue;
	}

      if (strncmp (buf, "[%", 2) == 0 && buf[strlen (buf) - 1] == ']')
	{
	  buf[strlen (buf) - 1] = '\0';
	  if (strcasecmp (shm_appl->broker_name, buf + 2) != 0)
	    {
	      break;
	    }
	}

      if (num_access_list >= ACL_MAX_ITEM_COUNT)
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s)"
		   " - max item count(%d) exceeded.",
		   shm_appl->broker_name, filename, ACL_MAX_ITEM_COUNT);
	  goto error;
	}

      dbname = strtok (buf, ACCESS_FILE_DELIMITER);
      if (dbname == NULL || strlen (dbname) > (ACL_MAX_DBNAME_LENGTH - 1))
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s:%d)"
		   " - Database name is empty or too long.",
		   shm_appl->broker_name, filename, line);
	  goto error;
	}

      dbuser = strtok (NULL, ACCESS_FILE_DELIMITER);
      if (dbuser == NULL || strlen (dbuser) > (ACL_MAX_DBUSER_LENGTH - 1))
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s:%d)"
		   " - Database user is empty or too long.",
		   shm_appl->broker_name, filename, line);
	  goto error;
	}

      ip_file = strtok (NULL, ACCESS_FILE_DELIMITER);
      if (ip_file == NULL)
	{
	  sprintf (admin_err_msg,
		   "%s: error while loading access control file(%s:%d)"
		   " - IP list file paths are empty.",
		   shm_appl->broker_name, filename, line);
	  goto error;
	}

      access_info = find_access_info (new_access_info, num_access_list,
				      dbname, dbuser);
      if (access_info == NULL)
	{
	  access_info = &new_access_info[num_access_list];
	  strncpy (access_info->dbname, dbname, ACL_MAX_DBNAME_LENGTH);
	  strncpy (access_info->dbuser, dbuser, ACL_MAX_DBUSER_LENGTH);
	  num_access_list++;
	}

      if (access_info->ip_files[0] != '\0')
	{
	  strncat (access_info->ip_files, ",", LINE_MAX);
	}
      strncat (access_info->ip_files, ip_file, LINE_MAX);
      for (files = ip_file;; files = NULL)
	{
	  token = strtok_r (files, IP_FILE_DELIMITER, &save);
	  if (token == NULL)
	    {
	      break;
	    }

	  if (strlen (token) > 255)
	    {
	      snprintf (admin_err_msg, ADMIN_ERR_MSG_SIZE,
			"%s: error while loading access control file(%s)"
			" - a IP file path(%s) is too long",
			shm_appl->broker_name, filename, token);
	      goto error;
	    }

	  strncpy (path_buf, token, 256);
	  access_control_file_repath (path_buf);
	  if (read_ip_info (&(access_info->ip_info), path_buf) < 0)
	    {
	      goto error;
	    }
	}
    }

  fclose (fd_access_list);

#if defined (WINDOWS)
  MAKE_ACL_SEM_NAME (acl_sem_name, shm_appl->broker_name);
  uw_sem_wait (acl_sem_name);
#else
  uw_sem_wait (&shm_appl->acl_sem);
#endif

  memcpy (shm_appl->access_info, new_access_info, sizeof (new_access_info));
  shm_appl->num_access_info = num_access_list;
  shm_appl->acl_chn++;

#if defined(WINDOWS)
  uw_sem_post (acl_sem_name);
#else
  uw_sem_post (&shm_appl->acl_sem);
#endif

  return 0;

error:
  fclose (fd_access_list);

  return -1;
}

static void
access_control_file_repath (char *path)
{
  char tmp_str[PATH_MAX];

  trim (path);

  if (IS_ABS_PATH (path))
    {
      return;
    }

#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
  envvar_confdir_file (tmp_str, PATH_MAX, path);
  strcpy (path, tmp_str);
#endif

  return;
}

static int
read_ip_info (IP_INFO * ip_info, char *filename)
{
  char buf[LINE_MAX];
  FILE *fd_ip_list;
  unsigned char i, ln = 0;

  fd_ip_list = fopen (filename, "r");

  if (fd_ip_list == NULL)
    {
      sprintf (admin_err_msg, "Could not open ip info file(%s)", filename);
      return -1;
    }

  buf[LINE_MAX - 2] = 0;
  while (fgets (buf, LINE_MAX, fd_ip_list))
    {
      char *token, *p;
      int address_index;

      ln++;
      if (buf[LINE_MAX - 2] != 0 && buf[LINE_MAX - 2] != '\n')
	{
	  sprintf (admin_err_msg, "Error while loading ip info file(%s)"
		   " - %d line is too long", filename, ln);
	  goto error;
	}

      p = strchr (buf, '#');
      if (p != NULL)
	{
	  *p = '\0';
	}

      trim (buf);
      if (buf[0] == '\0')
	{
	  continue;
	}

      if (ip_info->num_list >= ACL_MAX_IP_COUNT)
	{
	  sprintf (admin_err_msg,
		   "Error while loading ip info file(%s)"
		   " - max ip count(%d) exceeded.",
		   filename, ACL_MAX_IP_COUNT);
	  goto error;
	}

      token = strtok (buf, ".");

      address_index = ip_info->num_list * IP_BYTE_COUNT;
      for (i = 0; i < 4; i++)
	{
	  if (token == NULL)
	    {
	      sprintf (admin_err_msg,
		       "Error while loading ip info file(%s)", filename);
	      goto error;
	    }

	  if (strcmp (token, "*") == 0)
	    {
	      break;
	    }
	  else
	    {
	      long adr;
	      char *p = NULL;

	      adr = strtol (token, &p, 10);
	      if ((errno == ERANGE) ||
		  (errno != 0 && adr == 0) ||
		  (p && *p != '\0') || (adr > 255 || adr < 0))
		{
		  sprintf (admin_err_msg,
			   "Error while loading ip info file(%s)", filename);
		  goto error;
		}

	      ip_info->address_list[address_index + 1 + i] =
		(unsigned char) adr;
	    }

	  token = strtok (NULL, ".");
	  if (i == 3 && token != NULL)
	    {
	      sprintf (admin_err_msg,
		       "Error while loading ip info file(%s)", filename);
	      goto error;
	    }
	}
      ip_info->address_list[address_index] = i;
      ip_info->num_list++;
    }

  fclose (fd_ip_list);
  return 0;

error:
  fclose (fd_ip_list);
  return -1;
}

#if !defined(WINDOWS)
#if defined (ENABLE_UNUSED_FUNCTION)
static int
get_cubrid_version ()
{
  FILE *fp;
  char res_file[16];
  char cmd[32];
  int version = 0;

  strcpy (res_file, "ux_ver.uc_tmp");
  unlink (res_file);

  sprintf (cmd, "cubrid_rel > %s", res_file);
  system (cmd);

  fp = fopen (res_file, "r");
  if (fp != NULL)
    {
      char buf[1024];
      char *p;
      int major, minor;
      size_t n;

      n = fread (buf, 1, sizeof (buf) - 1, fp);
      if (n > 0 && n < sizeof (buf))
	{
	  buf[n] = '\0';
	  p = strstr (buf, "Release");
	  if (p != NULL)
	    {
	      p += 7;
	      if (sscanf (p, "%d%*c%d", &major, &minor) == 2)
		{
		  version = MAKE_VERSION (major, minor);
		}
	    }
	}
      fclose (fp);
    }
  unlink (res_file);

  return version;
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* !WINDOWS */
