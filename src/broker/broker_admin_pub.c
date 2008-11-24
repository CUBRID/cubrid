/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * broker_admin_pub.c -
 */

#ident "$Id$"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>

#ifdef WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "cas_common.h"
#include "broker_shm.h"
#include "broker_util.h"
#include "broker_env_def.h"
#include "broker_process_size.h"
#include "broker_admin_pub.h"
#include "broker_filename.h"
#include "broker_error.h"
#include "cas_sql_log2.h"

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

static int br_activate (T_BROKER_INFO *, int, T_SHM_BROKER *);
static int br_inactivate (T_BROKER_INFO *);
static void as_activate (T_APPL_SERVER_INFO *, int, T_BROKER_INFO *, char **,
			 int, T_SHM_APPL_SERVER *, T_SHM_BROKER *);
static void as_inactivate (int, char *br_name, int as_index);
static char **make_env (char *env_file, int *env_num);
static char *get_appl_server_name (int appl_server_type, char **env,
				   int env_num);
#ifndef WIN32
static int get_cubrid_version (void);
#endif

#ifdef WIN32
static int admin_get_host_ip (unsigned char *ip_addr);
#endif

char admin_err_msg[ADMIN_ERR_MSG_SIZE];

#ifndef WIN32
extern char **environ;
#endif

#ifdef V3_ADMIN_D
extern int admin_clt_sock_fd;	/* in admin_d.c */
extern int admin_srv_sock_fd;	/* in admin_d.c */
#endif

#ifdef WIN32
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
#endif

int
broker_create_dir (char *new_dir)
{
  char *p, path[1024];

  if (new_dir == NULL)
    return -1;

  strcpy (path, new_dir);
  trim (path);

#ifdef WIN32
  unix_style_path (path);
#endif

  p = path;
#ifdef WIN32
  if (path[0] == '/')
    p = path + 1;
  else if (strlen (path) > 3 && path[2] == '/')
    p = path + 3;
#else
  if (path[0] == '/')
    {
      p = path + 1;
    }
#endif

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

int
admin_start_cmd (T_BROKER_INFO * br_info, int br_num, int master_shm_id)
{
  T_SHM_BROKER *shm_br;
  int shm_size, i;
  int res = 0;
#ifdef WIN32
  unsigned char ip_addr[4];
#endif

#ifdef WIN32
  if (admin_get_host_ip (ip_addr) < 0)
    return -1;
#endif

  chdir ("..");
  broker_create_dir (CUBRID_VAR_DIR);
  broker_create_dir (CUBRID_TMP_DIR);
  broker_create_dir (CUBRID_BASE_DIR);
  broker_create_dir (CUBRID_ASPID_DIR);
  broker_create_dir (CUBRID_LOG_DIR);
  broker_create_dir (CUBRID_ERR_DIR);
#ifndef WIN32
  broker_create_dir (CUBRID_LOG_DIR "/" SQL_LOG2_DIR);
  broker_create_dir (CUBRID_SOCK_DIR);
#endif

  if (br_num <= 0)
    {
      strcpy (admin_err_msg,
	      "Cannot start CUBRID Broker. (number of broker is 0)");
      return -1;
    }

  for (i = 0; i < br_num; i++)
    {
      broker_create_dir (br_info[i].log_dir);
      broker_create_dir (br_info[i].err_log_dir);
    }
  chdir ("bin");

  /* create master shared memory */
  shm_size = sizeof (T_SHM_BROKER) + (br_num - 1) * sizeof (T_BROKER_INFO);

  shm_br =
    (T_SHM_BROKER *) uw_shm_create (master_shm_id, shm_size, SHM_BROKER);

  if (shm_br == NULL)
    {
      strcpy (admin_err_msg, "Cannot create shared memory");
      return -1;
    }

#ifdef WIN32
  shm_br->magic = uw_shm_get_magic_number ();
  memcpy (shm_br->my_ip_addr, ip_addr, 4);
#else
  shm_br->owner_uid = getuid ();
#endif

  shm_br->num_broker = br_num;
  /* create appl server shared memory */
  for (i = 0; i < br_num; i++)
    {
      shm_br->br_info[i] = br_info[i];
      sprintf (shm_br->br_info[i].access_log_file, "%s/%s.access",
	       br_info[i].access_log_file, br_info[i].name);
      sprintf (shm_br->br_info[i].error_log_file, "%s/%s.error",
	       br_info[i].error_log_file, br_info[i].name);
      if (shm_br->br_info[i].service_flag == ON)
	{
	  res = br_activate (&(shm_br->br_info[i]), master_shm_id, shm_br);
	  if (res < 0)
	    break;
	}
    }
  if (res < 0)
    {
      char err_msg_backup[ADMIN_ERR_MSG_SIZE];
      memcpy (err_msg_backup, admin_err_msg, ADMIN_ERR_MSG_SIZE);
      for (--i; i >= 0; i--)
	{
	  br_inactivate (&(shm_br->br_info[i]));
	}
      memcpy (admin_err_msg, err_msg_backup, ADMIN_ERR_MSG_SIZE);
    }

  uw_shm_detach (shm_br);

  if (res < 0)
    {
      uw_shm_destroy (master_shm_id);
    }
#ifdef WIN32
  else
    {
      char shm_id_env_str[128];
      sprintf (shm_id_env_str, "%s=%d", MASTER_SHM_KEY_ENV_STR,
	       master_shm_id);
      putenv (shm_id_env_str);
      run_child (NAME_UC_SHM);
    }
#endif


  return res;
}

int
admin_stop_cmd (int master_shm_id)
{
  T_SHM_BROKER *shm_br;
  int i;

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      return -1;
    }

#ifndef WIN32
  if (shm_br->owner_uid != getuid ())
    {
      strcpy (admin_err_msg, "Cannot stop CUBRID Broker. (Not owner)\n");
      return -1;
    }
#endif
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].service_flag == ON)
	br_inactivate (&(shm_br->br_info[i]));
    }

  shm_br->magic = 0;
#ifdef WIN32
  uw_shm_detach (shm_br);
#endif
  uw_shm_destroy (master_shm_id);

  return 0;
}

int
admin_add_cmd (int master_shm_id, char *broker)
{
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
	       as_index + 1,
	       &(shm_br->br_info[br_index]),
	       env, env_num, shm_appl_server, shm_br);

  shm_appl_server->as_info[as_index].service_flag = SERVICE_ON;

  uw_shm_detach (shm_appl_server);
  uw_shm_detach (shm_br);

  return 0;
}

int
admin_restart_cmd (int master_shm_id, char *broker, int as_index)
{
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, br_index, appl_shm_key;
  int pid;
  char **env;
  int env_num;
  char port_str[AS_PORT_STR_SIZE], appl_name_str[64];
  char access_log_env_str[256], error_log_env_str[256];
  char appl_server_shm_key_str[32];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
  char buf[PATH_MAX];
#ifndef WIN32
  char argv0[64];
#endif

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

  if (shm_br->br_info[br_index].appl_server == APPL_SERVER_CAS)
    {
      ut_kill_process (shm_appl->as_info[as_index].pid,
		       shm_br->br_info[br_index].name, as_index);
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
#ifdef WIN32
      int a;
      a = 0;
#endif
    }

  shm_appl->as_info[as_index].uts_status = UTS_STATUS_BUSY;

  if (shm_appl->as_info[as_index].pid)
    {
      ut_kill_process (shm_appl->as_info[as_index].pid,
		       shm_br->br_info[br_index].name, as_index);
    }

  SLEEP_SEC (1);

  env = make_env (shm_br->br_info[br_index].source_env, &env_num);

  shm_appl->as_info[as_index].service_ready_flag = FALSE;

#ifndef WIN32
  if ((pid = fork ()) != 0)
    {
    }
  else
    {
#ifdef V3_ADMIN_D
      if (admin_clt_sock_fd > 0)
	CLOSE_SOCKET (admin_clt_sock_fd);
      if (admin_srv_sock_fd > 0)
	CLOSE_SOCKET (admin_srv_sock_fd);
#endif
#endif

      if (env != NULL)
	{
	  for (i = 0; i < env_num; i++)
	    putenv (env[i]);
	}

      sprintf (port_str, "%s=%s%s.%d", PORT_NAME_ENV_STR,
	       get_cubrid_file (FID_SOCK_DIR, buf),
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

#ifndef WIN32
      sprintf (argv0, "%s_%s_%d", shm_br->br_info[br_index].name, appl_name,
	       as_index + 1);
      uw_shm_detach (shm_br);
      uw_shm_detach (shm_appl);
#endif

#ifdef WIN32
      pid = run_child (appl_name);
#else
      if (execle (appl_name, argv0, NULL, environ) < 0)
	perror ("execle");
      exit (0);
    }
#endif

  SERVICE_READY_WAIT (shm_appl->as_info[as_index].service_ready_flag);

  shm_appl->as_info[as_index].uts_status = UTS_STATUS_IDLE;
  shm_appl->as_info[as_index].pid = pid;

  shm_appl->as_info[as_index].session_id = 0;
  shm_appl->as_info[as_index].session_keep = FALSE;
  shm_appl->as_info[as_index].psize =
    getsize (shm_appl->as_info[as_index].pid);
  shm_appl->as_info[as_index].psize_time = time (NULL);
  shm_appl->as_info[as_index].last_access_time = time (NULL);
  shm_appl->as_info[as_index].clt_appl_name[0] = '\0';
  shm_appl->as_info[as_index].clt_req_path_info[0] = '\0';
  shm_appl->as_info[as_index].clt_ip_addr[0] = '\0';

  /* mutex exit section */
  shm_appl->as_info[as_index].mutex_flag[SHM_MUTEX_ADMIN] = FALSE;

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);

  return 0;

restart_error:
  if (shm_appl)
    uw_shm_detach (shm_appl);
  if (shm_br)
    uw_shm_detach (shm_br);
  return -1;
}

int
admin_drop_cmd (int master_shm_id, char *broker)
{
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

  shm_appl_server->as_info[as_index].session_keep = FALSE;

  as_inactivate (shm_appl_server->as_info[as_index].pid,
		 shm_br->br_info[br_index].name, as_index);
  shm_appl_server->as_info[as_index].pid = 0;

finale:
  uw_shm_detach (shm_br);
  uw_shm_detach (shm_appl_server);

  return 0;
}

int
admin_broker_on_cmd (int master_shm_id, char *broker_name)
{
  int i;
  T_SHM_BROKER *shm_br;
  int res = 0;

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
	      res =
		br_activate (&(shm_br->br_info[i]), master_shm_id, shm_br);
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

  return res;
}

int
admin_broker_off_cmd (int master_shm_id, char *broker_name)
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

#ifndef WIN32
  if (shm_br->owner_uid != getuid ())
    {
      strcpy (admin_err_msg, "Cannot stop broker. (Not owner)");
      return -1;
    }
#endif

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
	      br_inactivate (&(shm_br->br_info[i]));
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
admin_broker_suspend_cmd (int master_shm_id, char *broker_name)
{
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
}

int
admin_broker_resume_cmd (int master_shm_id, char *broker_name)
{
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
}

int
admin_get_broker_status (int master_shm_id, char *broker_name)
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

int admin_broker_job_first_cmd
  (int master_shm_id, char *broker_name, int job_id)
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
admin_broker_conf_change (int master_shm_id, char *br_name, char *conf_name,
			  char *conf_value)
{
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, br_index;
  int appl_shm_key;

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

  shm_appl = (T_SHM_APPL_SERVER *) uw_shm_open (appl_shm_key,
						SHM_APPL_SERVER,
						SHM_MODE_ADMIN);

  if ((strcasecmp (conf_name, "SQL_LOG") == 0)
      || (strcasecmp (conf_name, "SQL_LOG_APPEND_MODE") == 0)
      || (strcasecmp (conf_name, "SQL_LOG_BIND_VALUE") == 0))
    {
      if (shm_appl)
	{
	  char sql_log_mode, sql_log_mode_value;

	  if (strcasecmp (conf_name, "SQL_LOG") == 0)
	    {
	      sql_log_mode_value = SQL_LOG_MODE_ON;
	    }
	  else if (strcasecmp (conf_name, "SQL_LOG_APPEND_MODE") == 0)
	    {
	      sql_log_mode_value = SQL_LOG_MODE_APPEND;
	    }
	  else
	    {
	      sql_log_mode_value = SQL_LOG_MODE_BIND_VALUE;
	    }

	  sql_log_mode = shm_br->br_info[br_index].sql_log_mode;
	  CONF_GET_VALUE_SQL_LOG (sql_log_mode, conf_value,
				  sql_log_mode_value);
	  if (sql_log_mode < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].sql_log_mode = sql_log_mode;
	  shm_appl->sql_log_mode = sql_log_mode;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "SQL_LOG_TIME") == 0)
    {
      if (shm_appl)
	{
	  int log_time;

	  CONF_GET_VALUE_POSITIVE_INT (log_time, conf_value,
				       SQL_LOG_TIME_MAX);
	  shm_appl->sql_log_time = log_time;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "SQL_LOG_MAX_SIZE") == 0)
    {
      if (shm_appl)
	{
	  int sql_log_max_size;

	  CONF_GET_VALUE_POSITIVE_INT (sql_log_max_size, conf_value,
				       DEFAULT_SQL_LOG_MAX_SIZE);
	  if (sql_log_max_size > MAX_SQL_LOG_MAX_SIZE)
	    sql_log_max_size = MAX_SQL_LOG_MAX_SIZE;
	  shm_br->br_info[br_index].sql_log_max_size = sql_log_max_size;
	  shm_appl->sql_log_max_size = sql_log_max_size;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "APPL_SERVER_MAX_SIZE") == 0)
    {
      if (shm_appl)
	{
	  int max_size;

	  CONF_GET_VALUE_POSITIVE_INT (max_size, conf_value,
				       DEFAULT_SERVER_MAX_SIZE);
	  max_size *= 1024;
	  shm_br->br_info[br_index].appl_server_max_size = max_size;
	  shm_appl->appl_server_max_size = max_size;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "COMPRESS_SIZE") == 0)
    {
      if (shm_appl)
	{
	  int comp_size;

	  CONF_GET_VALUE_POSITIVE_INT (comp_size, conf_value,
				       DEFAULT_COMPRESS_SIZE);
	  comp_size *= 1024;	/* bytes */
	  shm_br->br_info[br_index].compress_size = comp_size;
	  shm_appl->compress_size = comp_size;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "LOG_BACKUP") == 0)
    {
      int log_backup;

      CONF_GET_VALUE_ON_OFF (log_backup, conf_value);
      if (log_backup < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_broker_conf_error;
	}
      shm_br->br_info[br_index].log_backup = log_backup;
    }
  else if (strcasecmp (conf_name, "PRIORITY_GAP") == 0)
    {
      int priority_gap;

      CONF_GET_VALUE_POSITIVE_INT (priority_gap, conf_value,
				   DEFAULT_PRIORITY_GAP);
      shm_br->br_info[br_index].priority_gap = priority_gap;
    }
  else if (strcasecmp (conf_name, "TIME_TO_KILL") == 0)
    {
      int time_to_kill;

      CONF_GET_VALUE_POSITIVE_INT (time_to_kill, conf_value,
				   DEFAULT_TIME_TO_KILL);
      shm_br->br_info[br_index].time_to_kill = time_to_kill;
    }
  else if (strcasecmp (conf_name, "ACCESS_LOG") == 0)
    {
      if (shm_appl)
	{
	  int access_log_flag;

	  CONF_GET_VALUE_ON_OFF (access_log_flag, conf_value);
	  if (access_log_flag < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].access_log = access_log_flag;
	  shm_appl->access_log = access_log_flag;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "KEEP_CONNECTION") == 0)
    {
      if (shm_appl)
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
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "CACHE_USER_INFO") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_ON_OFF (val, conf_value);
	  if (val < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].cache_user_info = val;
	  shm_appl->cache_user_info = val;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "SQL_LOG2") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_INT (val, conf_value);
	  if (val < SQL_LOG2_NONE || val > SQL_LOG2_MAX)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].sql_log2 = val;
	  shm_appl->sql_log2 = val;
	}
    }
  else if (strcasecmp (conf_name, "STATEMENT_POOLING") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_ON_OFF (val, conf_value);
	  if (val < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].statement_pooling = val;
	  shm_appl->statement_pooling = val;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_ON_OFF (val, conf_value);
	  if (val < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].jdbc_cache = val;
	  shm_appl->jdbc_cache = val;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE_ONLY_HINT") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_ON_OFF (val, conf_value);
	  if (val < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].jdbc_cache_only_hint = val;
	  shm_appl->jdbc_cache_only_hint = val;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE_LIFE_TIME") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_INT (val, conf_value);
	  shm_br->br_info[br_index].jdbc_cache_life_time = val;
	  shm_appl->jdbc_cache_life_time = val;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "SQL_LOG_SINGLE_LINE") == 0)
    {
      if (shm_appl)
	{
	  int val;

	  CONF_GET_VALUE_ON_OFF (val, conf_value);
	  if (val < 0)
	    {
	      sprintf (admin_err_msg, "invalid value : %s", conf_value);
	      goto set_broker_conf_error;
	    }
	  shm_br->br_info[br_index].sql_log_single_line = val;
	  shm_appl->sql_log_single_line = val;
	}
      else
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_broker_conf_error;
	}
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
admin_del_cas_log (int master_shmid, char *broker, int asid)
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
      || shm_br->br_info[br_index].appl_server != APPL_SERVER_CAS)
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
  char *clt_envs[] = { SID_ENV_STR,
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
	  environ[i] = "DUMMY_ENV=VISION_THREE";
	  continue;
	}
      for (j = 0; clt_envs[j] != NULL; j++)
	{
	  if (strncmp (environ[i], clt_envs[j], strlen (clt_envs[j])) == 0)
	    {
	      environ[i] = "DUMMY_ENV=VISION_THREE";
	      break;
	    }
	}
    }
}

#ifdef WIN32
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
#endif

static int
  br_activate
  (T_BROKER_INFO * br_info, int master_shm_id, T_SHM_BROKER * shm_br)
{
  int shm_size, pid, i;
  T_SHM_APPL_SERVER *shm_appl;
  char **env;
  int env_num;
  char port_str[AS_PORT_STR_SIZE];
  char appl_name_str[32];
  char master_shm_key_str[32];
  char appl_server_shm_key_str[32];
  char error_log_lock_file[128];
  char *broker_exe_name;
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

  shm_appl->suspend_mode = SUSPEND_NONE;
  shm_appl->job_queue_size = br_info->job_queue_size;
  shm_appl->job_queue[0].id = 0;	/* initialize max heap */
  strcpy (shm_appl->log_dir, br_info->log_dir);
  strcpy (shm_appl->err_log_dir, br_info->err_log_dir);

#ifdef WIN32
  shm_appl->use_pdh_flag = FALSE;
  br_info->pdh_workset = 0;
  br_info->pdh_pct_cpu = 0;
#endif

  env = make_env (br_info->source_env, &env_num);

  strncpy (shm_appl->appl_server_name,
	   get_appl_server_name (br_info->appl_server, env, env_num),
	   APPL_SERVER_NAME_MAX_SIZE);
  shm_appl->appl_server_name[APPL_SERVER_NAME_MAX_SIZE - 1] = '\0';

#ifndef WIN32
  if ((pid = fork ()) < 0)
    {
      strcpy (admin_err_msg, "fork error");
      uw_shm_detach (shm_appl);
      uw_shm_destroy (br_info->appl_server_shm_id);
      return -1;
    }
#endif

#ifndef WIN32
  if (pid == 0)
    {

#ifdef V3_ADMIN_D
      if (admin_clt_sock_fd > 0)
	CLOSE_SOCKET (admin_clt_sock_fd);
      if (admin_srv_sock_fd > 0)
	CLOSE_SOCKET (admin_srv_sock_fd);
#endif
#endif

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

      if (br_info->appl_server == APPL_SERVER_CAS)
	broker_exe_name = NAME_CAS_BROKER;
      else
	broker_exe_name = NAME_BROKER;

#ifdef WIN32
      if (br_info->appl_server == APPL_SERVER_CAS
	  && br_info->appl_server_port < 0)
	broker_exe_name = NAME_CAS_BROKER2;
#endif

      sprintf (appl_name_str, "%s=%s", APPL_NAME_ENV_STR, broker_exe_name);
      putenv (appl_name_str);

      sprintf (error_log_lock_file, "%s=%s.log.lock",
	       ERROR_LOG_LOCK_FILE_ENV_STR, br_info->name);
      putenv (error_log_lock_file);

#ifndef WIN32
      uw_shm_detach (shm_appl);
      uw_shm_detach (shm_br);
#endif

#ifdef WIN32
      pid = run_child (broker_exe_name);
#else
      if (execle (broker_exe_name, broker_exe_name, NULL, environ) < 0)
	{
	  perror (broker_exe_name);
	  exit (0);
	}
      exit (0);
    }
#endif

  SLEEP_MILISEC (0, 200);

  br_info->pid = pid;

  shm_appl->num_appl_server = br_info->appl_server_num;
  shm_appl->sql_log_mode = br_info->sql_log_mode;
  shm_appl->sql_log_time = br_info->sql_log_time;
  shm_appl->sql_log_max_size = br_info->sql_log_max_size;
  shm_appl->compress_size = br_info->compress_size;
  shm_appl->appl_server_max_size = br_info->appl_server_max_size;
  shm_appl->session_timeout = br_info->session_timeout;
  shm_appl->sql_log2 = br_info->sql_log2;
#ifdef WIN32
  shm_appl->as_port = br_info->appl_server_port;
#endif
  shm_appl->max_string_length = br_info->max_string_length;
  shm_appl->stripped_column_name = br_info->stripped_column_name;
  shm_appl->keep_connection = br_info->keep_connection;
  shm_appl->cache_user_info = br_info->cache_user_info;
  shm_appl->statement_pooling = br_info->statement_pooling;
  shm_appl->sql_log_single_line = br_info->sql_log_single_line;

  shm_appl->session_flag = br_info->session_flag;
  shm_appl->set_cookie = br_info->set_cookie;
  shm_appl->error_log = br_info->error_log;
  shm_appl->entry_value_trim = br_info->entry_value_trim;
  shm_appl->oid_check = br_info->oid_check;
  shm_appl->access_log = br_info->access_log;
  shm_appl->enc_appl_flag = br_info->enc_appl_flag;

  shm_appl->jdbc_cache = br_info->jdbc_cache;
  shm_appl->jdbc_cache_only_hint = br_info->jdbc_cache_only_hint;
  shm_appl->jdbc_cache_life_time = br_info->jdbc_cache_life_time;

  strcpy (shm_appl->doc_root, br_info->doc_root);
  strcpy (shm_appl->file_upload_temp_dir, br_info->file_upload_temp_dir);
  strcpy (shm_appl->file_upload_delimiter, br_info->file_upload_delimiter);
  strcpy (shm_appl->broker_name, br_info->name);

  for (i = 0; i < shm_appl->num_appl_server; i++)
    {
      shm_appl->as_info[i].last_access_time = time (NULL);
      as_activate (&(shm_appl->as_info[i]),
		   i + 1, br_info, env, env_num, shm_appl, shm_br);
    }
  for (; i < br_info->appl_server_max_num; i++)
    {
      shm_appl->as_info[i].service_flag = SERVICE_OFF;
      shm_appl->as_info[i].last_access_time = time (NULL);
      shm_appl->as_info[i].mutex_flag[SHM_MUTEX_BROKER] = FALSE;
      shm_appl->as_info[i].mutex_flag[SHM_MUTEX_ADMIN] = FALSE;
      shm_appl->as_info[i].mutex_turn = SHM_MUTEX_BROKER;
      shm_appl->as_info[i].num_request = 0;
#ifdef DIAG_DEVEL
      shm_appl->as_info[i].num_requests_received = 0;
      shm_appl->as_info[i].num_transactions_processed = 0;
#endif
      CON_STATUS_LOCK_INIT (&(shm_appl->as_info[i]));
    }

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
      return -1;
    }

  return 0;
}

static int
br_inactivate (T_BROKER_INFO * br_info)
{
  T_SHM_APPL_SERVER *shm_appl;
  time_t cur_time = time (NULL);
  struct tm *ct;
  int i;
  char cmd_buf[BUFSIZ];

  ct = localtime (&cur_time);
  ct->tm_year += 1900;

  if (br_info->pid)
    {
      ut_kill_process (br_info->pid, NULL, 0);
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
    }
  br_info->appl_server_num = br_info->appl_server_min_num;

  if (br_info->log_backup == ON)
    {
      sprintf (cmd_buf, "%s.%02d%02d%02d.%02d%02d",
	       br_info->access_log_file,
	       ct->tm_year, ct->tm_mon + 1, ct->tm_mday,
	       ct->tm_hour, ct->tm_min);
      rename (br_info->access_log_file, cmd_buf);

      sprintf (cmd_buf, "%s.%02d%02d%02d.%02d%02d",
	       br_info->error_log_file,
	       ct->tm_year, ct->tm_mon + 1, ct->tm_mday,
	       ct->tm_hour, ct->tm_min);
      rename (br_info->error_log_file, cmd_buf);
    }
  else
    {
      unlink (br_info->access_log_file);
      unlink (br_info->error_log_file);
    }

  br_info->num_busy_count = 0;

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
  char port_str[AS_PORT_STR_SIZE];
  char appl_name_str[64];
  char appl_server_shm_key_str[32];
  char access_log_env_str[256];
  char error_log_env_str[256];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
  char error_log_lock_file[128];
  int i;
  char port_name[AS_PORT_STR_SIZE];
#ifndef WIN32
  char process_name[64];
#endif

  get_cubrid_file (FID_SOCK_DIR, port_name);
  sprintf (port_name, "%s/%s.%d", port_name, br_info->name, as_index);
#ifndef WIN32
  unlink (port_name);
#endif

  /* mutex variable initialize */
  as_info->mutex_flag[SHM_MUTEX_BROKER] = FALSE;
  as_info->mutex_flag[SHM_MUTEX_ADMIN] = FALSE;
  as_info->mutex_turn = SHM_MUTEX_BROKER;
  CON_STATUS_LOCK_INIT (as_info);

  as_info->num_request = 0;
  as_info->session_id = 0;
  as_info->session_keep = FALSE;
  as_info->uts_status = UTS_STATUS_START;
  as_info->clt_appl_name[0] = '\0';
  as_info->clt_req_path_info[0] = '\0';
  as_info->clt_ip_addr[0] = '\0';

#ifdef WIN32
  as_info->pdh_pid = 0;
  as_info->pdh_workset = 0;
  as_info->pdh_pct_cpu = 0;
#endif

  as_info->service_ready_flag = FALSE;

#ifndef WIN32
  if ((pid = fork ()) < 0)
    {
      perror ("fork");
    }
#endif

#ifndef WIN32
  if (pid == 0)
    {

#ifdef V3_ADMIN_D
      if (admin_clt_sock_fd > 0)
	CLOSE_SOCKET (admin_clt_sock_fd);
      if (admin_srv_sock_fd > 0)
	CLOSE_SOCKET (admin_srv_sock_fd);
#endif
#endif

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

#ifdef RUN_PURIFY
      if (br_info->appl_server == APPL_SERVER_CAS)
	putenv ("PUREOPTIONS=-program-name=cas");
#endif

#ifndef WIN32
      sprintf (process_name, "%s_%s_%d", br_info->name, appl_name, as_index);
      uw_shm_detach (shm_appl);
      uw_shm_detach (shm_br);
#endif

#ifdef WIN32
      pid = run_child (appl_name);
#else
      if (execle (appl_name, process_name, NULL, environ) < 0)
	perror (appl_name);
      exit (0);
    }
#endif

  SERVICE_READY_WAIT (as_info->service_ready_flag);

  as_info->pid = pid;
  as_info->last_access_time = time (NULL);
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
      ut_kill_process (as_pid, br_name, as_index);
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
      read_num = sscanf (read_buf, "%s%s", col1, col2);
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


static char *
get_appl_server_name (int appl_server_type, char **env, int env_num)
{
  char *appl_name;
#ifndef WIN32
  int dbms_version;
#endif

  if (env != NULL)
    {
      char *p = "UC_APPL_SERVER_EXE_NAME=";
      int i;
      for (i = 0; i < env_num; i++)
	{
	  if (strncmp (env[i], p, strlen (p)) == 0)
	    {
	      return (env[i] + strlen (p));
	    }
	}
    }

  if (appl_server_type == APPL_SERVER_UTS_C)
    return APPL_SERVER_UTS_C_NAME;
  if (appl_server_type == APPL_SERVER_UTS_W)
    return APPL_SERVER_UTS_W_NAME;
  if (appl_server_type == APPL_SERVER_UPLOAD)
    return APPL_SERVER_UPLOAD_NAME;
  if (appl_server_type == APPL_SERVER_AM)
    return APPL_SERVER_AM_NAME;

  /* appl_server_type == APPL_SERVER_CAS */
#ifdef WIN32
  appl_name = APPL_SERVER_CAS_NAME;
#else
  dbms_version = get_cubrid_version ();

  if (dbms_version >= MAKE_VERSION (5, 2)
      && dbms_version < MAKE_VERSION (6, 0))
    appl_name = APPL_SERVER_CAS_U52_NAME;
  else
    appl_name = APPL_SERVER_CAS_NAME;
#endif

  return appl_name;
}

#ifndef WIN32
static int
get_cubrid_version ()
{
  FILE *fp;
  char res_file[16];
  char cmd[32];
  int version;

  strcpy (res_file, "ux_ver.uc_tmp");
  unlink (res_file);

  sprintf (cmd, "cubrid_rel > %s", res_file);
  system (cmd);

  fp = fopen (res_file, "r");
  if (fp == NULL)
    {
      version = 0;
    }
  else
    {
      char buf[1024];
      char *p;
      int major, minor;

      memset (buf, 0, sizeof (buf));
      fread (buf, 1, sizeof (buf) - 1, fp);
      p = strstr (buf, "Release");
      if (p == NULL)
	{
	  version = 0;
	}
      else
	{
	  p += 7;
	  if (sscanf (p, "%d%*c%d", &major, &minor) < 2)
	    version = 0;
	  else
	    version = MAKE_VERSION (major, minor);
	}
      fclose (fp);
    }
  unlink (res_file);

  return version;
}
#endif
