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
#include <assert.h>

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

#if defined(LINUX)
#include <sys/resource.h>
#endif /* LINUX */

#include "porting.h"
#include "cas_common.h"
#include "broker_shm.h"
#include "shard_metadata.h"
#include "shard_shm.h"
#include "shard_key_func.h"
#include "broker_util.h"
#include "broker_env_def.h"
#include "broker_process_size.h"
#include "broker_admin_pub.h"
#include "broker_filename.h"
#include "broker_error.h"
#include "cas_sql_log2.h"
#include "broker_acl.h"
#include "chartype.h"

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

#define MEMBER_SIZE(TYPE, MEMBER) ((int) sizeof(((TYPE *)0)->MEMBER))
#define NUM_OF_DIGITS(NUMBER) (int)log10(NUMBER) + 1

#define	ALL_PROXY	-1
#define	ALL_SHARD	-1
#define	ALL_AS		-1

T_SHM_PROXY *shm_proxy_p = NULL;

static int shard_shm_set_param_proxy (T_SHM_PROXY * proxy_p,
				      const char *param_name,
				      const char *param_value, int proxy_id);
static int shard_shm_set_param_proxy_internal (T_PROXY_INFO * proxy_info_p,
					       const char *param_name,
					       const char *param_value);
#if defined (ENABLE_UNUSED_FUNCTION)
static int shard_shm_set_param_shard (T_PROXY_INFO * proxy_info_p,
				      const char *param_name,
				      const char *param_value, int shard_id);
static int shard_shm_set_param_shard_internal (T_SHARD_CONN_INFO *
					       shard_conn_info_p,
					       const char *param_name,
					       const char *param_value);
static int shard_shm_set_param_shard_in_proxy (T_SHM_PROXY * proxy_p,
					       const char *param_name,
					       const char *param_value,
					       int proxy_id, int shard_id);
#endif /* ENABLE_UNUSED_FUNCTION */
static int shard_shm_set_param_as (T_PROXY_INFO * proxy_info_p,
				   T_SHARD_INFO * shard_info_p,
				   const char *param_name,
				   const char *param_value, int as_number);
static int shard_shm_set_param_as_internal (T_APPL_SERVER_INFO * as_info,
					    const char *param_name,
					    const char *param_value);
static int shard_shm_set_param_as_in_shard (T_PROXY_INFO * proxy_info_p,
					    const char *param_name,
					    const char *param_value,
					    int shard_id, int as_number);
static int shard_shm_set_param_as_in_proxy (T_SHM_PROXY * proxy_p,
					    const char *param_name,
					    const char *param_value,
					    int proxy_id, int shard_id,
					    int as_number);
static int shard_shm_check_max_file_open_limit (T_BROKER_INFO * br_info,
						T_SHM_PROXY * proxy_p);
static void get_shard_db_password (T_BROKER_INFO * br_info_p);
static void get_upper_str (char *upper_str, int len, char *value);

static void rename_error_log_file_name (char *error_log_file, struct tm *ct);

static int br_activate (T_BROKER_INFO * br_info, int master_shm_id,
			T_SHM_BROKER * shm_br);
static int br_inactivate (T_BROKER_INFO *);
static void as_activate (T_SHM_BROKER * shm_br, T_BROKER_INFO * br_info,
			 T_SHM_APPL_SERVER * shm_as_p,
			 T_APPL_SERVER_INFO * as_info_p, int as_idex,
			 char **env, int env_num);
static void as_inactivate (T_APPL_SERVER_INFO * as_info_p, char *broker_name,
			   int shard_flag);
static int check_shard_conn (T_SHM_APPL_SERVER * shm_as_p,
			     T_SHM_PROXY * shm_proxy_p);
static int check_shard_as_conn (T_SHM_APPL_SERVER * shm_as_p,
				T_SHARD_INFO * shard_info_p);

static void free_env (char **env, int env_num);
static char **make_env (char *env_file, int *env_num);

static int broker_create_dir (const char *new_dir);

static int
proxy_activate (T_BROKER_INFO * br_info_p, T_SHM_PROXY * shm_proxy_p,
		T_SHM_APPL_SERVER * shm_as_p, char **env, int env_num);
static int
proxy_activate_internal (int proxy_shm_id, T_SHM_APPL_SERVER * shm_as_p,
			 T_SHM_PROXY * shm_proxy_p, int proxy_id, char **env,
			 int env_num);
static void proxy_inactivate (T_BROKER_INFO * br_info_p,
			      T_PROXY_INFO * proxy_info_p);

#if !defined(WINDOWS)
#if defined (ENABLE_UNUSED_FUNCTION)
static int get_cubrid_version (void);
#endif
#endif /* !WINDOWS */

static char shard_db_password_env_str[MAX_BROKER_NUM][128];

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
  char *p, path[BROKER_PATH_MAX];

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
  return 0;
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
  int i;
  int res = 0;
  char path[BROKER_PATH_MAX];
  char upper_broker_name[BROKER_NAME_LEN];
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;

  if (br_num <= 0)
    {
      strcpy (admin_err_msg,
	      "Cannot start CUBRID Broker. (number of broker is 0)");
      return -1;
    }
  chdir ("..");
  broker_create_dir (get_cubrid_file (FID_VAR_DIR, path, BROKER_PATH_MAX));
  broker_create_dir (get_cubrid_file
		     (FID_CAS_TMP_DIR, path, BROKER_PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_AS_PID_DIR, path, BROKER_PATH_MAX));
  broker_create_dir (get_cubrid_file
		     (FID_SQL_LOG_DIR, path, BROKER_PATH_MAX));
  broker_create_dir (get_cubrid_file
		     (FID_SLOW_LOG_DIR, path, BROKER_PATH_MAX));
  broker_create_dir (get_cubrid_file
		     (FID_CUBRID_ERR_DIR, path, BROKER_PATH_MAX));
#if !defined(WINDOWS)
  broker_create_dir (get_cubrid_file
		     (FID_SQL_LOG2_DIR, path, BROKER_PATH_MAX));
  broker_create_dir (get_cubrid_file (FID_SOCK_DIR, path, BROKER_PATH_MAX));
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
		    "The socket path is too long (>%d): %s",
		    MEMBER_SIZE (struct sockaddr_un, sun_path), path);
	  return -1;
	}
#endif /* !WINDOWS */
      broker_create_dir (br_info[i].log_dir);
      broker_create_dir (br_info[i].slow_log_dir);
      broker_create_dir (br_info[i].err_log_dir);
      broker_create_dir (br_info[i].access_log_dir);

      if (br_info[i].shard_flag == ON)
	{
	  broker_create_dir (br_info[i].proxy_log_dir);
	}
    }
  chdir (envvar_bindir_file (path, BROKER_PATH_MAX, ""));

  /* create master shared memory */
  shm_br = broker_shm_initialize_shm_broker (master_shm_id, br_info, br_num,
					     acl_flag, acl_file);

  if (shm_br == NULL)
    {
      strcpy (admin_err_msg, "failed to initialize broker shared memory");
      return -1;
    }

  for (i = 0; i < br_num; i++)
    {
      if (br_info[i].shard_flag == OFF)
	{
	  continue;
	}

      if (br_info[i].shard_db_password[0] == '\0')
	{
	  get_shard_db_password (&shm_br->br_info[i]);
	}

      get_upper_str (upper_broker_name, BROKER_NAME_LEN, br_info[i].name);

      snprintf (shard_db_password_env_str[i],
		sizeof (shard_db_password_env_str[i]),
		"%s_SHARD_DB_PASSWORD=", upper_broker_name);
      putenv (shard_db_password_env_str[i]);
    }

  for (i = 0; i < br_num; i++)
    {
      shm_as_p = NULL;
      shm_proxy_p = NULL;

      if (br_info[i].service_flag == ON)
	{
	  if (br_info[i].shard_flag == ON)

	    {
	      shm_proxy_p =
		shard_shm_initialize_shm_proxy (&(shm_br->br_info[i]));

	      if (shm_proxy_p == NULL)
		{
		  sprintf (admin_err_msg,
			   "%s: failed to initialize proxy shared memory.",
			   br_info->name);

		  res = -1;
		  break;
		}

	      if (shard_shm_check_max_file_open_limit (&shm_br->br_info[i],
						       shm_proxy_p) < 0)
		{
		  res = -1;
		  break;
		}
	    }

	  shm_as_p =
	    broker_shm_initialize_shm_as (&(shm_br->br_info[i]), shm_proxy_p);
	  if (shm_as_p == NULL)
	    {
	      sprintf (admin_err_msg,
		       "%s: failed to initialize appl server shared memory.",
		       br_info->name);

	      res = -1;
	      break;
	    }

	  if (access_control_set_shm (shm_as_p, &shm_br->br_info[i],
				      shm_br, admin_err_msg) < 0)
	    {
	      res = -1;
	      break;
	    }

	  res = br_activate (&(shm_br->br_info[i]), master_shm_id, shm_br);

	  if (shm_br->br_info[i].shard_flag == ON && res == 0)
	    {
	      res = check_shard_conn (shm_as_p, shm_proxy_p);
	    }

	  if (res < 0)
	    {
	      break;
	    }

	  if (shm_as_p)
	    {
	      uw_shm_detach (shm_as_p);
	    }
	  if (shm_proxy_p)
	    {
	      uw_shm_detach (shm_proxy_p);
	    }

	}
    }

  if (res < 0)
    {
      char err_msg_backup[ADMIN_ERR_MSG_SIZE];
      memcpy (err_msg_backup, admin_err_msg, ADMIN_ERR_MSG_SIZE);

      /* if shm_as_p == NULL then, it is expected that failed creating shared memory */
      if (shm_as_p == NULL)
	{
	  if (shm_br->br_info[i].shard_flag == ON && shm_proxy_p)
	    {
	      uw_shm_detach (shm_proxy_p);
	      shm_proxy_p = NULL;

	      uw_shm_destroy (shm_br->br_info[i].proxy_shm_id);
	    }

	  --i;
	}

      for (; i >= 0; i--)
	{
	  br_inactivate (&(shm_br->br_info[i]));

	  uw_shm_destroy ((shm_br->br_info[i].appl_server_shm_id));
	  if (shm_br->br_info[i].shard_flag == ON)
	    {
	      uw_shm_destroy ((shm_br->br_info[i].proxy_shm_id));
	    }
	}

      memcpy (admin_err_msg, err_msg_backup, ADMIN_ERR_MSG_SIZE);
    }
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }

  if (res < 0)
    {
      uw_shm_destroy (master_shm_id);

      if (shm_as_p)
	{
	  uw_shm_detach (shm_as_p);
	}
      if (shm_proxy_p)
	{
	  uw_shm_detach (shm_proxy_p);
	}
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
  int i, res;
  char err_msg_backup[ADMIN_ERR_MSG_SIZE];

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

  memcpy (err_msg_backup, admin_err_msg, ADMIN_ERR_MSG_SIZE);

  for (i = 0; i < MAX_BROKER_NUM && i < shm_br->num_broker; i++)
    {
      if (shm_br->br_info[i].service_flag == ON)
	{
	  res = br_inactivate (&(shm_br->br_info[i]));
	  if (res < 0)
	    {
	      sprintf (admin_err_msg, "Cannot inactivate broker [%s]",
		       shm_br->br_info[i].name);
	      uw_shm_detach (shm_br);
	      return -1;
	    }
	  else
	    {
	      uw_shm_destroy ((shm_br->br_info[i].appl_server_shm_id));
	      if (shm_br->br_info[i].shard_flag == ON)
		{
		  uw_shm_destroy ((shm_br->br_info[i].proxy_shm_id));
		}
	    }
	}
    }

  memcpy (admin_err_msg, err_msg_backup, ADMIN_ERR_MSG_SIZE);

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
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl_server;
  int i, br_index;
  int appl_shm_key = 0, as_index;
  char **env = NULL;
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
  if (shm_br->br_info[br_index].shard_flag == ON)
    {
      uw_shm_detach (shm_br);
      return 0;
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

  as_activate (shm_br, &(shm_br->br_info[br_index]), shm_appl_server,
	       &(shm_appl_server->as_info[as_index]), as_index, env, env_num);

  shm_appl_server->as_info[as_index].service_flag = SERVICE_ON;

  uw_shm_detach (shm_appl_server);
  uw_shm_detach (shm_br);
  free_env (env, env_num);

  return 0;
}

int
admin_restart_cmd (int master_shm_id, const char *broker, int as_index)
{
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  int i, br_index, appl_shm_key;
  int pid;
  char **env = NULL;
  int env_num;
  char appl_server_shm_key_str[32];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
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
  if (shm_br->br_info[br_index].shard_flag == ON)
    {
      uw_shm_detach (shm_br);
      return 0;
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
      ut_kill_as_process (shm_appl->as_info[as_index].pid,
			  shm_br->br_info[br_index].name, as_index,
			  shm_appl->shard_flag);

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
      ut_kill_as_process (shm_appl->as_info[as_index].pid,
			  shm_br->br_info[br_index].name, as_index,
			  shm_appl->shard_flag);
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

      sprintf (appl_server_shm_key_str, "%s=%d", APPL_SERVER_SHM_KEY_STR,
	       shm_br->br_info[br_index].appl_server_shm_id);
      putenv (appl_server_shm_key_str);
      strcpy (appl_name, shm_appl->appl_server_name);

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

  if (ut_is_appl_server_ready
      (pid, &shm_appl->as_info[as_index].service_ready_flag) == false)
    {
      snprintf (admin_err_msg, ADMIN_ERR_MSG_SIZE,
		"Could not start the application server: %s\n",
		shm_appl->appl_server_name);
      goto restart_error;
    }

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
  shm_appl->as_info[as_index].driver_version[0] = '\0';

  /* mutex exit section */
  shm_appl->as_info[as_index].mutex_flag[SHM_MUTEX_ADMIN] = FALSE;

  uw_shm_detach (shm_appl);
  uw_shm_detach (shm_br);
  free_env (env, env_num);

  return 0;

restart_error:
  if (shm_appl)
    {
      uw_shm_detach (shm_appl);
    }
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }
  if (env)
    {
      free_env (env, env_num);
    }

  return -1;
}


int
admin_drop_cmd (int master_shm_id, const char *broker)
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
  if (shm_br->br_info[br_index].shard_flag == ON)
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

  as_inactivate (&shm_appl_server->as_info[as_index],
		 shm_br->br_info[br_index].name, shm_appl_server->shard_flag);

finale:
  uw_shm_detach (shm_br);
  uw_shm_detach (shm_appl_server);

  return 0;
}

int
admin_on_cmd (int master_shm_id, const char *broker_name)
{
  int i, res = 0;
  char upper_broker_name[BROKER_NAME_LEN];
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;

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
      if (shm_br->br_info[i].shard_flag == OFF)
	{
	  continue;
	}

      get_upper_str (upper_broker_name, BROKER_NAME_LEN,
		     shm_br->br_info[i].name);

      snprintf (shard_db_password_env_str[i],
		sizeof (shard_db_password_env_str[i]),
		"%s_SHARD_DB_PASSWORD=", upper_broker_name);
      putenv (shard_db_password_env_str[i]);
    }

  for (i = 0; i < shm_br->num_broker; i++)
    {
      shm_proxy_p = NULL;

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
	      if (shm_br->br_info[i].shard_flag == ON)
		{
		  shm_proxy_p =
		    shard_shm_initialize_shm_proxy (&(shm_br->br_info[i]));

		  if (shm_proxy_p == NULL)
		    {
		      sprintf (admin_err_msg,
			       "%s: failed to initialize proxy", broker_name);

		      res = -1;
		      break;
		    }

		  if (shard_shm_check_max_file_open_limit
		      (&shm_br->br_info[i], shm_proxy_p) < 0)
		    {
		      res = -1;
		      break;
		    }
		}

	      shm_as_p =
		broker_shm_initialize_shm_as (&(shm_br->br_info[i]),
					      shm_proxy_p);
	      if (shm_as_p == NULL)
		{
		  sprintf (admin_err_msg, "%s: cannot create shared memory",
			   broker_name);

		  res = -1;
		  break;
		}

	      if (access_control_set_shm (shm_as_p, &shm_br->br_info[i],
					  shm_br, admin_err_msg) < 0)
		{
		  res = -1;
		  break;
		}

	      res =
		br_activate (&(shm_br->br_info[i]), master_shm_id, shm_br);

	      if (shm_br->br_info[i].shard_flag == ON && res == 0)
		{
		  res = check_shard_conn (shm_as_p, shm_proxy_p);
		}
	    }
	  break;
	}
    }

  if (res < 0)
    {
      char err_msg_backup[ADMIN_ERR_MSG_SIZE];

      /* if shm_as_p == NULL then, it is expected that failed creating shared memory */
      if (shm_as_p == NULL)
	{
	  if (shm_br->br_info[i].shard_flag == ON && shm_proxy_p)
	    {
	      uw_shm_detach (shm_proxy_p);

	      uw_shm_destroy (shm_br->br_info[i].proxy_shm_id);
	    }

	  uw_shm_detach (shm_br);

	  return -1;
	}

      memcpy (err_msg_backup, admin_err_msg, ADMIN_ERR_MSG_SIZE);

      br_inactivate (&(shm_br->br_info[i]));

      uw_shm_destroy ((shm_br->br_info[i].appl_server_shm_id));
      if (shm_br->br_info[i].shard_flag == ON)
	{
	  uw_shm_destroy ((shm_br->br_info[i].proxy_shm_id));
	}

      memcpy (admin_err_msg, err_msg_backup, ADMIN_ERR_MSG_SIZE);
    }

  if (i >= shm_br->num_broker)
    {
      sprintf (admin_err_msg, "Cannot find broker [%s]", broker_name);

      res = -1;
    }

  uw_shm_detach (shm_br);
  if (shm_as_p)
    {
      uw_shm_detach (shm_as_p);
    }
  if (shm_proxy_p)
    {
      uw_shm_detach (shm_proxy_p);
    }

  return res;
}

int
admin_off_cmd (int master_shm_id, const char *broker_name)
{
  int i, res;
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
	      res = br_inactivate (&(shm_br->br_info[i]));
	      if (res < 0)
		{
		  sprintf (admin_err_msg, "Cannot inactivate broker [%s]",
			   broker_name);
		  uw_shm_detach (shm_br);
		  return -1;
		}
	      else
		{
		  uw_shm_destroy ((shm_br->br_info[i].appl_server_shm_id));
		  if (shm_br->br_info[i].shard_flag == ON)
		    {
		      uw_shm_destroy ((shm_br->br_info[i].proxy_shm_id));
		    }
		}
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
admin_reset_cmd (int master_shm_id, const char *broker_name)
{
  bool reset_next = FALSE;
  int i, j, k, as_index, br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_as_p;
  T_SHM_PROXY *proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p;
  T_SHARD_INFO *shard_info_p;

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

  shm_as_p =
    (T_SHM_APPL_SERVER *) uw_shm_open (shm_br->br_info[br_index].
				       appl_server_shm_id, SHM_APPL_SERVER,
				       SHM_MODE_ADMIN);
  if (shm_as_p == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      uw_shm_detach (shm_br);
      return -1;
    }
  assert (shm_as_p->num_appl_server <= APPL_SERVER_NUM_LIMIT);

  if (shm_br->br_info[br_index].shard_flag == ON)
    {
      proxy_p =
	(T_SHM_PROXY *) uw_shm_open (shm_br->br_info[br_index].proxy_shm_id,
				     SHM_PROXY, SHM_MODE_ADMIN);

      if (proxy_p == NULL)
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  uw_shm_detach (shm_br);
	  uw_shm_detach (shm_as_p);
	  return -1;
	}

      for (j = 0; j < proxy_p->num_proxy; j++)
	{
	  proxy_info_p = shard_shm_find_proxy_info (proxy_p, j);

	  for (k = 0; k < proxy_info_p->num_shard_conn; k++)
	    {
	      shard_info_p = shard_shm_find_shard_info (proxy_info_p, k);

	      as_index = shard_info_p->num_appl_server / 2;
	      as_index = MAX (1, as_index);

	      for (i = 0; i < as_index; i++)
		{
		  shm_as_p->as_info[i +
				    shard_info_p->as_info_index_base].
		    reset_flag = TRUE;
		}

	      while (1)
		{
		  for (i = 0; i < as_index; i++)
		    {
		      if (shm_as_p->
			  as_info[i +
				  shard_info_p->as_info_index_base].
			  reset_flag == FALSE)
			{
			  reset_next = TRUE;
			  break;
			}
		      SLEEP_MILISEC (0, 10);
		    }

		  if (reset_next)
		    {
		      break;
		    }
		}

	      for (i = as_index; i < shard_info_p->num_appl_server; i++)
		{
		  shm_as_p->as_info[i +
				    shard_info_p->as_info_index_base].
		    reset_flag = TRUE;
		}
	    }
	}
      uw_shm_detach (proxy_p);
    }
  else
    {
      int limit_appl_index = shm_br->br_info[br_index].appl_server_max_num;

      if (limit_appl_index > APPL_SERVER_NUM_LIMIT)
	{
	  limit_appl_index = APPL_SERVER_NUM_LIMIT;
	}

      for (i = 0; i < limit_appl_index; i++)
	{
	  shm_as_p->as_info[i].reset_flag = TRUE;
	}
    }

  uw_shm_detach (shm_as_p);
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

static bool
key_isdigit (const char *value)
{
  const char *p = value;

  while (char_isdigit (*p))
    {
      p++;
    }

  if (*p == '\0')
    {
      return true;
    }
  else
    {
      return false;
    }
}

static int
make_sp_value (SP_VALUE * value_p, char *shard_key)
{
  int length = strlen (shard_key);
  char *end;

  if (key_isdigit (shard_key))
    {
      errno = 0;
      value_p->integer = strtoll (shard_key, &end, 10);
      if (errno == ERANGE || *end != '\0')
	{
	  return -1;
	}

      value_p->type = VT_INTEGER;
    }
  else
    {
      value_p->string.value = (char *) malloc (sizeof (char) * (length + 1));
      if (value_p->string.value == NULL)
	{
	  return -1;
	}
      memcpy (value_p->string.value, shard_key, length);
      value_p->type = VT_STRING;
      value_p->string.length = length;
      value_p->string.value[length] = '\0';
    }

  return 0;
}

static void
print_usage (void)
{
  printf ("shard_getid -b <broker-name> [-f] shard-key\n");
  printf ("\t-b shard broker name\n");
  printf ("\t-f full information\n");
}

int
admin_getid_cmd (int master_shm_id, int argc, const char **argv)
{
  int i, error, optchar;
  int br_index, buf_len;
  int appl_server_shm_id;
  int shard_key_id;
  bool full_info_flag = false;
  const char *shard_key;
  const char *key_column;
  char buf[LINE_MAX];
  char line_buf[LINE_MAX];
  char broker_name[BROKER_NAME_LEN] = { 0 };
  T_BROKER_INFO *br_info_p;
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_SHARD_KEY *shm_key_p = NULL;
  T_SHM_SHARD_CONN *shm_conn_p = NULL;
  T_SHARD_KEY *key_p = NULL;
  T_SHARD_KEY_RANGE *range_p = NULL;
  SP_VALUE value;

  if (argc == 3 && strcmp (argv[2], "--version") == 0)
    {
      fprintf (stderr, "VERSION %s\n", makestring (BUILD_NUMBER));
      return 0;
    }

  while ((optchar = getopt (argc, (char *const *) argv, "b:fh?")) != EOF)
    {
      switch (optchar)
	{
	case 'b':
	  strncpy (broker_name, optarg, NAME_MAX);
	  break;
	case 'f':
	  full_info_flag = true;
	  break;
	case 'h':
	case '?':
	  print_usage ();
	  return 0;
	  break;
	}
    }

  if (argc < 5)
    {
      print_usage ();
      goto getid_error;
    }

  shard_key = argv[argc - 1];

  if (strcmp (broker_name, "") == 0)
    {
      sprintf (admin_err_msg, "Broker name is null.\n");
      print_usage ();
      goto getid_error;
    }

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      sprintf (admin_err_msg, "Failed to open master shared memory.\n");
      goto getid_error;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcmp (shm_br->br_info[i].name, broker_name) == 0)
	{
	  if (shm_br->br_info[i].service_flag == OFF)
	    {
	      sprintf (admin_err_msg, "Broker [%s] is not running.\n",
		       broker_name);
	      goto getid_error;
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
      sprintf (admin_err_msg, "Cannot find shard broker [%s].\n",
	       broker_name);
      goto getid_error;
    }

  br_info_p = &(shm_br->br_info[br_index]);

  if (br_info_p->shard_flag == OFF)
    {
      uw_shm_detach (shm_br);
      return 0;
    }

  appl_server_shm_id = br_info_p->appl_server_shm_id;

  shm_proxy_p =
    (T_SHM_PROXY *) uw_shm_open (br_info_p->proxy_shm_id, SHM_PROXY,
				 SHM_MODE_ADMIN);

  if (shm_proxy_p == NULL)
    {
      sprintf (admin_err_msg, "Failed to get shm proxy info.\n");
      goto getid_error;
    }

  shm_key_p = shard_metadata_get_key (shm_proxy_p);
  if (shm_key_p == NULL)
    {
      sprintf (admin_err_msg, "Failed to get shm metadata shard key info.\n");
      goto getid_error;
    }

  shm_conn_p = shard_metadata_get_conn (shm_proxy_p);
  if (shm_conn_p == NULL)
    {
      sprintf (admin_err_msg,
	       "Failed to get shm metadata connection info.\n");
      goto getid_error;
    }

  error = register_fn_get_shard_key ();
  if (error)
    {
      sprintf (admin_err_msg, "Failed to register shard hashing function.\n");
      goto getid_error;
    }

  error = make_sp_value (&value, (char *) shard_key);
  if (error)
    {
      sprintf (admin_err_msg, "Failed to make shard key value.\n");
      goto getid_error;
    }

  key_p = (T_SHARD_KEY *) (&(shm_key_p->shard_key[0]));
  key_column = key_p->key_column;

  shard_key_id = proxy_find_shard_id_by_hint_value (&value, key_column);
  if (shard_key_id < 0)
    {
      sprintf (admin_err_msg, "Failed to find shard key id.\n");
      goto getid_error;
    }

  range_p =
    shard_metadata_find_shard_range (shm_key_p, (char *) key_column,
				     shard_key_id);
  if (range_p == NULL)
    {
      sprintf (admin_err_msg,
	       "Unable to find shm shard range. (key:%s, key_id:%d).\n",
	       key_column, shard_key_id);
      goto getid_error;
    }

  /* SHARD ID INFORMATION */
  buf_len = 0;
  buf_len += sprintf (buf + buf_len, "%% %s\n", broker_name);
  buf_len += sprintf (buf + buf_len, " SHARD_ID : %d, ", range_p->shard_id);
  buf_len += sprintf (buf + buf_len, "SHARD_KEY : %s", shard_key);
  if (full_info_flag == true)
    {
      buf_len += sprintf (buf + buf_len, ", KEY_COLUMN : %s", key_column);
    }
  buf_len += sprintf (buf + buf_len, "\n");
  printf ("%s", buf);

  /* SHARD KEY CONFIG */
  if (full_info_flag == true)
    {
      buf_len = 0;
      buf_len +=
	sprintf (buf + buf_len, " MODULAR : %d, ",
		 shm_proxy_p->shard_key_modular);
      buf_len +=
	sprintf (buf + buf_len, "LIBRARY_NAME : %s, ",
		 (shm_proxy_p->shard_key_library_name[0] ==
		  0) ? "NOT DEFINED" : shm_proxy_p->shard_key_library_name);
      buf_len +=
	sprintf (buf + buf_len, "FUNCTION_NAME : %s ",
		 (shm_proxy_p->shard_key_function_name[0] ==
		  0) ? "NOT DEFINED" : shm_proxy_p->shard_key_function_name);
      buf_len += sprintf (buf + buf_len, "\n");
      printf ("%s", buf);
    }

  /* RANGE STATISTICS */
  if (full_info_flag == true)
    {
      buf_len = 0;
      buf_len +=
	sprintf (buf + buf_len, " RANGE STATISTICS : %s\n",
		 key_p->key_column);
      printf ("%s", buf);

      buf_len = 0;
      buf_len += sprintf (buf + buf_len, "%5s ~ ", "MIN");
      buf_len += sprintf (buf + buf_len, "%5s : ", "MAX");
      buf_len += sprintf (buf + buf_len, "%10s", "SHARD");
      for (i = 0; i < buf_len; i++)
	{
	  line_buf[i] = '-';
	}
      line_buf[i] = '\0';
      printf ("\t%s\n", buf);
      printf ("\t%s\n", line_buf);

      buf_len = 0;
      buf_len += sprintf (buf + buf_len, "\t%5d ~ %5d : %10d\n", range_p->min,
			  range_p->max, range_p->shard_id);
      printf ("%s\n", buf);
    }

  /* CONNECTION INFOMATION */
  if (full_info_flag == true)
    {
      buf_len = 0;
      buf_len += sprintf (buf + buf_len, " SHARD CONNECTION : \n");
      printf ("%s", buf);

      buf_len = 0;
      buf_len += sprintf (buf + buf_len, "%8s ", "SHARD_ID");
      buf_len += sprintf (buf + buf_len, "%16s ", "DB NAME");
      buf_len += sprintf (buf + buf_len, "%24s ", "CONNECTION_INFO");
      for (i = 0; i < buf_len; i++)
	{
	  line_buf[i] = '-';
	}
      line_buf[i] = '\0';
      printf ("\t%s\n", buf);
      printf ("\t%s\n", line_buf);

      for (i = 0; i < shm_conn_p->num_shard_conn; i++)
	{
	  if (range_p->shard_id == shm_conn_p->shard_conn[i].shard_id)
	    {
	      buf_len = 0;
	      buf_len +=
		sprintf (buf + buf_len, "%8d ",
			 shm_conn_p->shard_conn[i].shard_id);
	      buf_len +=
		sprintf (buf + buf_len, "%16s ",
			 shm_conn_p->shard_conn[i].db_name);
	      buf_len +=
		sprintf (buf + buf_len, "%24s ",
			 shm_conn_p->shard_conn[i].db_conn_info);
	      printf ("\t%s\n", buf);
	    }
	}
    }

  uw_shm_detach (shm_proxy_p);
  uw_shm_detach (shm_br);

  return 0;

getid_error:
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }
  if (shm_proxy_p)
    {
      uw_shm_detach (shm_proxy_p);
    }

  return -1;
}

int
admin_conf_change (int master_shm_id, const char *br_name,
		   const char *conf_name, const char *conf_value,
		   int as_number)
{
  int i, br_index;
  T_SHM_BROKER *shm_br = NULL;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_BROKER_INFO *br_info_p = NULL;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_USER *user_p = NULL;
  char path_org[BROKER_PATH_MAX] = { 0, };
  char path_new[BROKER_PATH_MAX] = { 0, };

  shm_br =
    (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_ADMIN);
  if (shm_br == NULL)
    {
      sprintf (admin_err_msg, "Broker is not started.");
      goto set_conf_error;
    }

  br_index = -1;
  for (i = 0; i < shm_br->num_broker; i++)
    {
      if (strcasecmp (br_name, shm_br->br_info[i].name) == 0)
	{
	  br_index = i;
	  break;
	}
    }
  if (br_index < 0)
    {
      sprintf (admin_err_msg, "Cannot find Broker [%s]", br_name);
      goto set_conf_error;
    }

  if (shm_br->br_info[br_index].service_flag == OFF)
    {
      sprintf (admin_err_msg, "Broker [%s] is not running.", br_name);
      goto set_conf_error;
    }

  br_info_p = &(shm_br->br_info[br_index]);
  shm_as_p =
    (T_SHM_APPL_SERVER *) uw_shm_open (br_info_p->appl_server_shm_id,
				       SHM_APPL_SERVER, SHM_MODE_ADMIN);

  if (shm_as_p == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      goto set_conf_error;
    }
  assert (shm_as_p->num_appl_server <= APPL_SERVER_NUM_LIMIT);

  if (br_info_p->shard_flag == ON)
    {
      shm_proxy_p =
	(T_SHM_PROXY *) uw_shm_open (br_info_p->proxy_shm_id, SHM_PROXY,
				     SHM_MODE_ADMIN);

      if (shm_proxy_p == NULL)
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  goto set_conf_error;
	}
    }

  if (strcasecmp (conf_name, "SQL_LOG") == 0)
    {
      char sql_log_mode;

      sql_log_mode = conf_get_value_sql_log_mode (conf_value);
      if (sql_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      if (br_info_p->shard_flag == ON)
	{
	  br_info_p->sql_log_mode = sql_log_mode;
	  shm_as_p->sql_log_mode = sql_log_mode;

	  if (shard_shm_set_param_as_in_proxy
	      (shm_proxy_p, conf_name, conf_value, ALL_PROXY, ALL_SHARD,
	       ALL_AS) < 0)
	    {
	      goto set_conf_error;
	    }
	}
      else
	{
	  if (as_number <= 0)
	    {
	      shm_br->br_info[br_index].sql_log_mode = sql_log_mode;
	      shm_as_p->sql_log_mode = sql_log_mode;
	      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num;
		   i++)
		{
		  shm_as_p->as_info[i].cur_sql_log_mode = sql_log_mode;
		  shm_as_p->as_info[i].cas_log_reset = CAS_LOG_RESET_REOPEN;
		}
	    }
	  else
	    {
	      if (as_number > shm_as_p->num_appl_server)
		{
		  sprintf (admin_err_msg, "Invalid cas number : %d",
			   as_number);
		  goto set_conf_error;
		}
	      shm_as_p->as_info[as_number - 1].cur_sql_log_mode =
		sql_log_mode;
	      shm_as_p->as_info[as_number - 1].cas_log_reset =
		CAS_LOG_RESET_REOPEN;
	    }
	}
    }
  else if (strcasecmp (conf_name, "SLOW_LOG") == 0)
    {
      char slow_log_mode;

      slow_log_mode = conf_get_value_table_on_off (conf_value);
      if (slow_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      if (br_info_p->shard_flag == ON)
	{
	  br_info_p->slow_log_mode = slow_log_mode;
	  shm_as_p->slow_log_mode = slow_log_mode;

	  if (shard_shm_set_param_as_in_proxy
	      (shm_proxy_p, conf_name, conf_value, ALL_PROXY, ALL_SHARD,
	       ALL_AS) < 0)
	    {
	      goto set_conf_error;
	    }
	}
      else
	{
	  if (as_number <= 0)
	    {
	      shm_br->br_info[br_index].slow_log_mode = slow_log_mode;
	      shm_as_p->slow_log_mode = slow_log_mode;
	      for (i = 0; i < shm_br->br_info[br_index].appl_server_max_num;
		   i++)
		{
		  shm_as_p->as_info[i].cur_slow_log_mode = slow_log_mode;
		  shm_as_p->as_info[i].cas_slow_log_reset =
		    CAS_LOG_RESET_REOPEN;
		}
	    }
	  else
	    {
	      if (as_number > shm_as_p->num_appl_server)
		{
		  sprintf (admin_err_msg, "Invalid cas number : %d",
			   as_number);
		  goto set_conf_error;
		}
	      shm_as_p->as_info[as_number - 1].cur_slow_log_mode =
		slow_log_mode;
	      shm_as_p->as_info[as_number - 1].cas_slow_log_reset =
		CAS_LOG_RESET_REOPEN;
	    }
	}
    }
  else if (strcasecmp (conf_name, "ACCESS_MODE") == 0)
    {
      char access_mode = conf_get_value_access_mode (conf_value);

      if (access_mode == -1)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      if (br_info_p->access_mode == access_mode)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->access_mode = access_mode;
      shm_as_p->access_mode = access_mode;

      if (br_info_p->shard_flag == ON)
	{
	  if (shard_shm_set_param_as_in_proxy
	      (shm_proxy_p, conf_name, conf_value, ALL_PROXY, ALL_SHARD,
	       ALL_AS) < 0)
	    {
	      goto set_conf_error;
	    }
	}
      else
	{
	  for (i = 0;
	       i < shm_as_p->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	       i++)
	    {
	      shm_as_p->as_info[i].reset_flag = TRUE;
	    }
	}
    }
  else if (strcasecmp (conf_name, "CONNECT_ORDER") == 0)
    {
      int connect_order;

      connect_order = conf_get_value_connect_order (conf_value);
      if (connect_order == -1)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      if (br_info_p->connect_order == connect_order)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->connect_order = connect_order;
      shm_as_p->connect_order = connect_order;

      if (br_info_p->shard_flag == ON)
	{
	  if (shard_shm_set_param_as_in_proxy
	      (shm_proxy_p, conf_name, conf_value, ALL_PROXY, ALL_SHARD,
	       ALL_AS) < 0)
	    {
	      goto set_conf_error;
	    }
	}
      else
	{
	  for (i = 0;
	       i < shm_as_p->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	       i++)
	    {
	      shm_as_p->as_info[i].reset_flag = TRUE;
	    }
	}
    }
  else if (strcasecmp (conf_name, "MAX_NUM_DELAYED_HOSTS_LOOKUP") == 0)
    {
      int max_num_delayed_hosts_lookup = 0, result;

      result = parse_int (&max_num_delayed_hosts_lookup, conf_value, 10);

      if (result != 0
	  || max_num_delayed_hosts_lookup <
	  DEFAULT_MAX_NUM_DELAYED_HOSTS_LOOKUP)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      if (br_info_p->max_num_delayed_hosts_lookup ==
	  max_num_delayed_hosts_lookup)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->max_num_delayed_hosts_lookup = max_num_delayed_hosts_lookup;
      shm_as_p->max_num_delayed_hosts_lookup = max_num_delayed_hosts_lookup;
    }
  else if (strcasecmp (conf_name, "RECONNECT_TIME") == 0)
    {
      int rctime;

      rctime = (int) ut_time_string_to_sec (conf_value, "sec");
      if (rctime < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->cas_rctime = rctime;
      shm_as_p->cas_rctime = rctime;
    }
  else if (strcasecmp (conf_name, "SQL_LOG_MAX_SIZE") == 0)
    {
      int sql_log_max_size;

      sql_log_max_size = (int) ut_size_string_to_kbyte (conf_value, "K");

      if (sql_log_max_size <= 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      else if (sql_log_max_size > MAX_SQL_LOG_MAX_SIZE)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->sql_log_max_size = sql_log_max_size;
      shm_as_p->sql_log_max_size = sql_log_max_size;
    }
  else if (strcasecmp (conf_name, "LONG_QUERY_TIME") == 0)
    {
      float long_query_time;

      long_query_time = (float) ut_time_string_to_sec (conf_value, "sec");

      if (long_query_time < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      else if (long_query_time > LONG_QUERY_TIME_LIMIT)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->long_query_time = (int) (long_query_time * 1000.0);
      shm_as_p->long_query_time = (int) (long_query_time * 1000.0);
    }
  else if (strcasecmp (conf_name, "LONG_TRANSACTION_TIME") == 0)
    {
      float long_transaction_time;

      long_transaction_time =
	(float) ut_time_string_to_sec (conf_value, "sec");

      if (long_transaction_time < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      else if (long_transaction_time > LONG_TRANSACTION_TIME_LIMIT)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->long_transaction_time =
	(int) (long_transaction_time * 1000.0);
      shm_as_p->long_transaction_time =
	(int) (long_transaction_time * 1000.0);
    }
  else if (strcasecmp (conf_name, "APPL_SERVER_MAX_SIZE") == 0)
    {
      int max_size;

      max_size = (int) ut_size_string_to_kbyte (conf_value, "M");

      if (max_size < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      if (max_size > 0
	  && max_size > (shm_br->br_info[br_index].appl_server_hard_limit))
	{
	  sprintf (admin_err_msg,
		   "CONFIGURATION WARNING - the APPL_SERVER_MAX_SIZE (%dM)"
		   " is greater than the APPL_SERVER_MAX_SIZE_HARD_LIMIT (%dM)",
		   max_size / ONE_K,
		   shm_as_p->appl_server_hard_limit / ONE_K);
	}

      br_info_p->appl_server_max_size = max_size;
      shm_as_p->appl_server_max_size = max_size;
    }
  else if (strcasecmp (conf_name, "APPL_SERVER_MAX_SIZE_HARD_LIMIT") == 0)
    {
      int hard_limit;
      int max_hard_limit;

      hard_limit = (int) ut_size_string_to_kbyte (conf_value, "M");

      max_hard_limit = INT_MAX;
      if (hard_limit <= 0)
	{
	  sprintf (admin_err_msg,
		   "APPL_SERVER_MAX_SIZE_HARD_LIMIT must be between 1 and %d",
		   max_hard_limit / ONE_K);
	  goto set_conf_error;
	}

      if (hard_limit < shm_br->br_info[br_index].appl_server_max_size)
	{
	  sprintf (admin_err_msg,
		   "CONFIGURATION WARNING - the APPL_SERVER_MAX_SIZE_HARD_LIMIT (%dM) "
		   "is smaller than the APPL_SERVER_MAX_SIZE (%dM)",
		   hard_limit / ONE_K,
		   shm_as_p->appl_server_max_size / ONE_K);
	}

      br_info_p->appl_server_hard_limit = hard_limit;
      shm_as_p->appl_server_hard_limit = hard_limit;
    }
  else if (strcasecmp (conf_name, "LOG_BACKUP") == 0)
    {
      int log_backup;

      log_backup = conf_get_value_table_on_off (conf_value);
      if (log_backup < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->log_backup = log_backup;
    }
  else if (strcasecmp (conf_name, "TIME_TO_KILL") == 0)
    {
      int time_to_kill;

      time_to_kill = (int) ut_time_string_to_sec (conf_value, "sec");
      if (time_to_kill <= 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->time_to_kill = time_to_kill;
    }
  else if (strcasecmp (conf_name, "ACCESS_LOG") == 0)
    {
      int access_log_flag;

      access_log_flag = conf_get_value_table_on_off (conf_value);
      if (access_log_flag < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->access_log = access_log_flag;
      shm_as_p->access_log = access_log_flag;
    }
  else if (strcasecmp (conf_name, "ACCESS_LOG_MAX_SIZE") == 0)
    {
      int size;

      /*
       * Use "KB" as unit, because MAX_ACCESS_LOG_MAX_SIZE uses this unit.
       * the range of the config value should be verified to avoid the invalid setting.
       */
      size = (int) ut_size_string_to_kbyte (conf_value, "K");

      if (size < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      else if (size > MAX_ACCESS_LOG_MAX_SIZE)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}


      br_info_p->access_log_max_size = size;
      shm_as_p->access_log_max_size = size;
    }
  else if (strcasecmp (conf_name, "KEEP_CONNECTION") == 0)
    {
      int keep_con;

      keep_con = conf_get_value_keep_con (conf_value);
      if (keep_con < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->keep_connection = keep_con;
      shm_as_p->keep_connection = keep_con;
    }
  else if (strcasecmp (conf_name, "CACHE_USER_INFO") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->cache_user_info = val;
      shm_as_p->cache_user_info = val;
    }
  else if (strcasecmp (conf_name, "SQL_LOG2") == 0)
    {
      int val, result;

      result = parse_int (&val, conf_value, 10);
      if (result != 0 || val < SQL_LOG2_NONE || val > SQL_LOG2_MAX)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->sql_log2 = val;
      shm_as_p->sql_log2 = val;
    }
  else if (strcasecmp (conf_name, "STATEMENT_POOLING") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->statement_pooling = val;
      shm_as_p->statement_pooling = val;
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->jdbc_cache = val;
      shm_as_p->jdbc_cache = val;
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE_ONLY_HINT") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->jdbc_cache_only_hint = val;
      shm_as_p->jdbc_cache_only_hint = val;
    }
  else if (strcasecmp (conf_name, "JDBC_CACHE_LIFE_TIME") == 0)
    {
      int val;

      val = atoi (conf_value);
      br_info_p->jdbc_cache_life_time = val;
      shm_as_p->jdbc_cache_life_time = val;
    }
  else if (strcasecmp (conf_name, "CCI_PCONNECT") == 0)
    {
      int val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->cci_pconnect = val;
      shm_as_p->cci_pconnect = val;
    }
  else if (strcasecmp (conf_name, "MAX_QUERY_TIMEOUT") == 0)
    {
      int val;

      val = (int) ut_time_string_to_sec (conf_value, "sec");

      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value: %s", conf_value);
	  goto set_conf_error;
	}
      else if (val > MAX_QUERY_TIMEOUT_LIMIT)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->query_timeout = val;
      shm_as_p->query_timeout = val;
    }
  else if (strcasecmp (conf_name, "MYSQL_READ_TIMEOUT") == 0)
    {
      int val;

      val = (int) ut_time_string_to_sec (conf_value, "sec");

      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value: %s", conf_value);
	  goto set_conf_error;
	}
      else if (val > MAX_QUERY_TIMEOUT_LIMIT)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->mysql_read_timeout = val;
      shm_as_p->mysql_read_timeout = val;
    }
  else if (strcasecmp (conf_name, "SHARD_PROXY_LOG") == 0)
    {
      char proxy_log_mode;

      if (br_info_p->shard_flag == OFF)
	{
	  sprintf (admin_err_msg, "%s is only supported on shard", conf_name);
	  goto set_conf_error;
	}

      assert (shm_proxy_p != NULL);

      proxy_log_mode = conf_get_value_proxy_log_mode (conf_value);
      if (proxy_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->proxy_log_mode = proxy_log_mode;

      if (shard_shm_set_param_proxy (shm_proxy_p, conf_name, conf_value,
				     ALL_PROXY) < 0)
	{
	  goto set_conf_error;
	}
    }
  else if (strcasecmp (conf_name, "SHARD_PROXY_LOG_MAX_SIZE") == 0)
    {
      int proxy_log_max_size;

      if (br_info_p->shard_flag == OFF)
	{
	  sprintf (admin_err_msg, "%s is only supported on shard", conf_name);
	  goto set_conf_error;
	}

      proxy_log_max_size = (int) ut_size_string_to_kbyte (conf_value, "K");

      if (proxy_log_max_size <= 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}
      else if (proxy_log_max_size > MAX_PROXY_LOG_MAX_SIZE)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}
      br_info_p->proxy_log_max_size = proxy_log_max_size;
      shm_as_p->proxy_log_max_size = proxy_log_max_size;
    }
  else if (strcasecmp (conf_name, "TRIGGER_ACTION") == 0)
    {
      int old_val, val;

      val = conf_get_value_table_on_off (conf_value);
      if (val < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      old_val = br_info_p->trigger_action_flag;

      br_info_p->trigger_action_flag = val;
      shm_as_p->trigger_action_flag = val;

      if (br_info_p->shard_flag == ON)
	{
	  if (shard_shm_set_param_as_in_proxy
	      (shm_proxy_p, conf_name, conf_value, ALL_PROXY, ALL_SHARD,
	       ALL_AS) < 0)
	    {
	      br_info_p->trigger_action_flag = old_val;
	      shm_as_p->trigger_action_flag = old_val;
	      goto set_conf_error;
	    }
	}
      else
	{
	  for (i = 0;
	       i < shm_as_p->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	       i++)
	    {
	      shm_as_p->as_info[i].reset_flag = TRUE;
	    }
	}
    }
  else if (strcasecmp (conf_name, "PREFERRED_HOSTS") == 0)
    {
      char *host_name = (char *) conf_value;
      int host_name_len = 0;

      host_name_len = strlen (host_name);

      if (host_name_len >= BROKER_INFO_NAME_MAX
	  || host_name_len >= SHM_APPL_SERVER_NAME_MAX)
	{
	  sprintf (admin_err_msg, "The length of the host name is too long.");
	  goto set_conf_error;
	}

      if (strncasecmp (br_info_p->preferred_hosts, host_name, host_name_len)
	  == 0)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", host_name);
	  goto set_conf_error;
	}

      strncpy (br_info_p->preferred_hosts, host_name, host_name_len);
      strncpy (shm_as_p->preferred_hosts, host_name, host_name_len);
    }
  else if (strcasecmp (conf_name, "MAX_PREPARED_STMT_COUNT") == 0)
    {
      int err_code = 0;
      int stmt_cnt = 0;

      err_code = parse_int (&stmt_cnt, conf_value, 10);
      if (err_code < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      if (stmt_cnt < 1)
	{
	  sprintf (admin_err_msg, "value is out of range : %s", conf_value);
	  goto set_conf_error;
	}

      if (br_info_p->max_prepared_stmt_count == stmt_cnt)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", conf_value);
	  goto set_conf_error;
	}

      if (br_info_p->max_prepared_stmt_count > stmt_cnt)
	{
	  sprintf (admin_err_msg,
		   "cannot be decreased below the previous value '%d' : %s",
		   br_info_p->max_prepared_stmt_count, conf_value);
	  goto set_conf_error;
	}

      br_info_p->max_prepared_stmt_count = stmt_cnt;
      shm_as_p->max_prepared_stmt_count = stmt_cnt;
    }
  else if (strcasecmp (conf_name, "SESSION_TIMEOUT") == 0)
    {
      int session_timeout = 0;

      session_timeout =
	(int) ut_time_string_to_sec ((char *) conf_value, "sec");
      if (session_timeout < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", conf_value);
	  goto set_conf_error;
	}

      if (br_info_p->session_timeout == session_timeout)
	{
	  sprintf (admin_err_msg, "same as previous value : %s", conf_value);
	  goto set_conf_error;
	}

      br_info_p->session_timeout = session_timeout;
      shm_as_p->session_timeout = session_timeout;
    }
  else if (strcasecmp (conf_name, "ERROR_LOG_DIR") == 0)
    {
      char *err_log_dir = (char *) conf_value;
      int err_log_dir_len = 0;

      err_log_dir_len = strlen (err_log_dir);

      if (err_log_dir_len >= CONF_LOG_FILE_LEN)
	{
	  sprintf (admin_err_msg, "The length of ERROR_LOG_DIR is too long.");
	  goto set_conf_error;
	}

      ut_cd_root_dir ();	/* change the working directory to $CUBRID */

      memset (path_org, 0x00, BROKER_PATH_MAX);
      memset (path_new, 0x00, BROKER_PATH_MAX);

      MAKE_FILEPATH (path_org, br_info_p->err_log_dir, BROKER_PATH_MAX);
      MAKE_FILEPATH (path_new, err_log_dir, BROKER_PATH_MAX);

#if defined(WINDOWS)
      if (strcasecmp (path_org, path_new) == 0)
#else
      if (strcmp (path_org, path_new) == 0)
#endif
	{
	  sprintf (admin_err_msg, "same as previous value : %s", err_log_dir);
	  goto set_conf_error;
	}

      broker_create_dir (path_new);
      if (access (path_new, F_OK) < 0)
	{
	  sprintf (admin_err_msg, "cannot access the path : %s", path_new);
	  goto set_conf_error;
	}

      ut_cd_work_dir ();	/* change the working directory to $CUBRID/bin */

      strcpy (br_info_p->err_log_dir, path_new);
      strcpy (shm_as_p->err_log_dir, path_new);

      for (i = 0; i < shm_as_p->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	   i++)
	{
	  shm_as_p->as_info[i].cas_err_log_reset = CAS_LOG_RESET_REOPEN;
	}
    }
  else if (strcasecmp (conf_name, "LOG_DIR") == 0)
    {
      char *log_dir = (char *) conf_value;
      int log_dir_len = 0;

      log_dir_len = strlen (log_dir);

      if (log_dir_len >= CONF_LOG_FILE_LEN)
	{
	  sprintf (admin_err_msg, "The length of LOG_DIR is too long.");
	  goto set_conf_error;
	}

      ut_cd_root_dir ();	/* change the working directory to $CUBRID */

      memset (path_org, 0x00, BROKER_PATH_MAX);
      memset (path_new, 0x00, BROKER_PATH_MAX);

      MAKE_FILEPATH (path_org, br_info_p->log_dir, BROKER_PATH_MAX);
      MAKE_FILEPATH (path_new, log_dir, BROKER_PATH_MAX);

#if defined(WINDOWS)
      if (strcasecmp (path_org, path_new) == 0)
#else
      if (strcmp (path_org, path_new) == 0)
#endif
	{
	  sprintf (admin_err_msg, "same as previous value : %s", log_dir);
	  goto set_conf_error;
	}

      broker_create_dir (path_new);
      if (access (path_new, F_OK) < 0)
	{
	  sprintf (admin_err_msg, "cannot access the path : %s", path_new);
	  goto set_conf_error;
	}

      ut_cd_work_dir ();	/* change the working directory to $CUBRID/bin */

      strcpy (br_info_p->log_dir, path_new);
      strcpy (shm_as_p->log_dir, path_new);

      for (i = 0; i < shm_as_p->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	   i++)
	{
	  shm_as_p->as_info[i].cas_log_reset = CAS_LOG_RESET_REOPEN;
	}
    }
  else if (strcasecmp (conf_name, "SLOW_LOG_DIR") == 0)
    {
      char *slow_log_dir = (char *) conf_value;
      int slow_log_dir_len = 0;

      slow_log_dir_len = strlen (slow_log_dir);

      if (slow_log_dir_len >= CONF_LOG_FILE_LEN)
	{
	  sprintf (admin_err_msg, "The length of SLOW_LOG_DIR is too long.");
	  goto set_conf_error;
	}

      ut_cd_root_dir ();	/* change the working directory to $CUBRID */

      memset (path_org, 0x00, BROKER_PATH_MAX);
      memset (path_new, 0x00, BROKER_PATH_MAX);

      MAKE_FILEPATH (path_org, br_info_p->slow_log_dir, BROKER_PATH_MAX);
      MAKE_FILEPATH (path_new, slow_log_dir, BROKER_PATH_MAX);

#if defined(WINDOWS)
      if (strcasecmp (path_org, path_new) == 0)
#else
      if (strcmp (path_org, path_new) == 0)
#endif
	{
	  sprintf (admin_err_msg, "same as previous value : %s",
		   slow_log_dir);
	  goto set_conf_error;
	}

      broker_create_dir (path_new);
      if (access (path_new, F_OK) < 0)
	{
	  sprintf (admin_err_msg, "cannot access the path : %s", path_new);
	  goto set_conf_error;
	}

      ut_cd_work_dir ();	/* change the working directory to $CUBRID/bin */

      strcpy (br_info_p->slow_log_dir, path_new);
      strcpy (shm_as_p->slow_log_dir, path_new);

      for (i = 0; i < shm_as_p->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	   i++)
	{
	  shm_as_p->as_info[i].cas_slow_log_reset = CAS_LOG_RESET_REOPEN;
	}
    }
  else
    {
      sprintf (admin_err_msg, "unknown keyword %s", conf_name);
      goto set_conf_error;
    }

  if (shm_proxy_p)
    {
      uw_shm_detach (shm_proxy_p);
    }
  if (shm_as_p)
    {
      uw_shm_detach (shm_as_p);
    }
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }
  return 0;

set_conf_error:
  if (shm_proxy_p)
    {
      uw_shm_detach (shm_proxy_p);
    }
  if (shm_as_p)
    {
      uw_shm_detach (shm_as_p);
    }
  if (shm_br)
    {
      uw_shm_detach (shm_br);
    }
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
    {
      goto error;
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
      goto error;
    }

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
admin_acl_status_cmd (int master_shm_id, const char *broker_name)
{
  int i, j, k, m;
  int br_index;
  T_SHM_BROKER *shm_br;
  T_SHM_APPL_SERVER *shm_appl;
  char line_buf[LINE_MAX];
  char str[32];
  int len = 0;

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

	      len = 0;
	      len += sprintf (line_buf + len, "%16s ", "CLIENT IP");
	      len += sprintf (line_buf + len, "%25s", "LAST ACCESS TIME");
	      fprintf (stdout, "%s\n", line_buf);

	      for (k = 0; k < len; k++)
		{
		  line_buf[k] = '=';
		}
	      line_buf[k] = '\0';
	      fprintf (stdout, "%s\n", line_buf);

	      for (k = 0; k < shm_appl->access_info[j].ip_info.num_list; k++)
		{
		  int address_index = k * IP_BYTE_COUNT;

		  len = 0;
		  for (m = 0;
		       m <
		       shm_appl->access_info[j].ip_info.
		       address_list[address_index]; m++)
		    {
		      len += sprintf (str + len, "%d%s",
				      shm_appl->access_info[j].ip_info.
				      address_list[address_index + m + 1],
				      ((m != 3) ? "." : ""));
		    }

		  if (m != 4)
		    {
		      len += sprintf (str + len, "*");
		    }

		  len = sprintf (line_buf, "%16.16s ", str);

		  if (shm_appl->access_info[j].ip_info.last_access_time[k] !=
		      0)
		    {
		      struct tm cur_tm;
		      localtime_r (&shm_appl->access_info[j].ip_info.
				   last_access_time[k], &cur_tm);
		      cur_tm.tm_year += 1900;

		      sprintf (str, "%02d-%02d-%02d %02d:%02d:%02d",
			       cur_tm.tm_year, cur_tm.tm_mon + 1,
			       cur_tm.tm_mday, cur_tm.tm_hour,
			       cur_tm.tm_min, cur_tm.tm_sec);
		      len += sprintf (line_buf + len, "%25.25s", str);
		    }

		  fprintf (stdout, "%s\n", line_buf);
		}
	      fprintf (stdout, "\n");
	    }
	  fprintf (stdout, "\n");
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

	  len = 0;
	  len += sprintf (line_buf + len, "%16s ", "CLIENT IP");
	  len += sprintf (line_buf + len, "%25s", "LAST ACCESS TIME");
	  fprintf (stdout, "%s\n", line_buf);

	  for (k = 0; k < len; k++)
	    {
	      line_buf[k] = '=';
	    }
	  line_buf[k] = '\0';
	  fprintf (stdout, "%s\n", line_buf);

	  for (k = 0; k < shm_appl->access_info[j].ip_info.num_list; k++)
	    {
	      int address_index = k * IP_BYTE_COUNT;

	      len = 0;
	      for (m = 0;
		   m <
		   shm_appl->access_info[j].ip_info.
		   address_list[address_index]; m++)
		{
		  len += sprintf (str + len, "%d%s",
				  shm_appl->access_info[j].ip_info.
				  address_list[address_index + m + 1],
				  ((m != 3) ? "." : ""));
		}

	      if (m != 4)
		{
		  len += sprintf (str + len, "*");
		}

	      len = sprintf (line_buf, "%16.16s ", str);

	      if (shm_appl->access_info[j].ip_info.last_access_time[k] != 0)
		{
		  struct tm cur_tm;
		  localtime_r (&shm_appl->access_info[j].ip_info.
			       last_access_time[k], &cur_tm);
		  cur_tm.tm_year += 1900;

		  sprintf (str, "%02d-%02d-%02d %02d:%02d:%02d",
			   cur_tm.tm_year, cur_tm.tm_mon + 1,
			   cur_tm.tm_mday, cur_tm.tm_hour,
			   cur_tm.tm_min, cur_tm.tm_sec);
		  len += sprintf (line_buf + len, "%25.25s", str);
		}

	      fprintf (stdout, "%s\n", line_buf);
	    }
	  fprintf (stdout, "\n");
	}
      fprintf (stdout, "\n");
      uw_shm_detach (shm_appl);
    }

  uw_shm_detach (shm_br);
  return 0;
}

int
admin_acl_reload_cmd (int master_shm_id, const char *broker_name)
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
	  if (access_control_read_config_file (shm_appl, access_file_name,
					       admin_err_msg) != 0)
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

      if (access_control_read_config_file (shm_appl, access_file_name,
					   admin_err_msg) != 0)
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

static int
br_activate (T_BROKER_INFO * br_info, int master_shm_id,
	     T_SHM_BROKER * shm_br)
{
  int pid, i, res = 0;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  T_APPL_SERVER_INFO *as_info_p = NULL;
  char **env = NULL;
  int env_num = 0;
  char port_str[BROKER_PATH_MAX];
  char master_shm_key_str[32];
  const char *broker_exe_name;
  int broker_check_loop_count = 30;
  T_SHM_PROXY *shm_proxy_p = NULL;

  if (br_info->shard_flag == ON)
    {

      shm_proxy_p = (T_SHM_PROXY *) uw_shm_open (br_info->proxy_shm_id,
						 SHM_PROXY, SHM_MODE_ADMIN);

      if (shm_proxy_p == NULL)
	{
	  sprintf (admin_err_msg, "%s: cannot open shared memory",
		   br_info->name);
	  res = -1;
	  goto end;
	}
    }

  br_info->err_code = UW_ER_NO_ERROR + 1;
  br_info->os_err_code = 0;
  br_info->num_busy_count = 0;
  br_info->reject_client_count = 0;

  shm_appl = (T_SHM_APPL_SERVER *) uw_shm_open (br_info->appl_server_shm_id,
						SHM_APPL_SERVER,
						SHM_MODE_ADMIN);
  if (shm_appl == NULL)
    {
      sprintf (admin_err_msg, "%s: cannot open shared memory", br_info->name);
      res = -1;
      goto end;
    }
  assert (shm_appl->num_appl_server <= APPL_SERVER_NUM_LIMIT);

#if defined(WINDOWS)
  shm_appl->use_pdh_flag = FALSE;
  br_info->pdh_workset = 0;
  br_info->pdh_pct_cpu = 0;
#endif /* WINDOWS */

  env = make_env (br_info->source_env, &env_num);

#if !defined(WINDOWS)
  if (br_info->shard_flag == ON)
    {
      unlink (shm_appl->port_name);
    }
  signal (SIGCHLD, SIG_IGN);
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  if ((pid = fork ()) < 0)
    {
      strcpy (admin_err_msg, "fork error");
      res = -1;
      goto end;
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

      if (IS_APPL_SERVER_TYPE_CAS (br_info->appl_server))
	{
	  broker_exe_name = NAME_CAS_BROKER;
	}
      else
	{
	  broker_exe_name = NAME_BROKER;
	}

#if defined(WINDOWS)
      if (IS_APPL_SERVER_TYPE_CAS (br_info->appl_server)
	  && br_info->appl_server_port < 0)
	broker_exe_name = NAME_CAS_BROKER2;
#endif /* WINDOWS */

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
  if (br_info->shard_flag == ON)
    {
      if (proxy_activate (br_info, shm_proxy_p, shm_appl, env, env_num) < 0)
	{
	  res = -1;
	  goto end;
	}
    }

  SLEEP_MILISEC (0, 200);

  br_info->pid = pid;

#if defined(WINDOWS)
  shm_appl->as_port = br_info->appl_server_port;
#endif /* WINDOWS */

  if (br_info->shard_flag == ON)
    {
      for (i = 0; i < br_info->appl_server_max_num; i++)
	{
	  as_info_p = &shm_appl->as_info[i];
	  if (as_info_p->advance_activate_flag)
	    {
	      as_activate (shm_br, br_info, shm_appl, as_info_p,
			   i, env, env_num);
	    }
	}

      for (i = 0; i < br_info->appl_server_max_num; i++)
	{
	  as_info_p = &shm_appl->as_info[i];
	  if (as_info_p->advance_activate_flag)
	    {
	      if (ut_is_appl_server_ready
		  (as_info_p->pid, &as_info_p->service_ready_flag) == false)
		{
		  sprintf (admin_err_msg,
			   "%s: failed to run appl server. \n",
			   br_info->name);
		  res = -1;
		  goto end;
		}
	    }
	}
    }
  else
    {
      for (i = 0; i < shm_appl->num_appl_server && i < APPL_SERVER_NUM_LIMIT;
	   i++)
	{
	  as_activate (shm_br, br_info, shm_appl, &shm_appl->as_info[i], i,
		       env, env_num);
	}
      for (; i < br_info->appl_server_max_num; i++)
	{
	  CON_STATUS_LOCK_INIT (&(shm_appl->as_info[i]));
	}
    }

  br_info->ready_to_service = true;
  br_info->service_flag = ON;

  for (i = 0; i < broker_check_loop_count; i++)
    {
      if (br_info->err_code > 0)
	{
	  SLEEP_MILISEC (0, 100);
	}
      else
	{
	  if (br_info->err_code < 0)
	    {
	      sprintf (admin_err_msg, "%s: %s", br_info->name,
		       uw_get_error_message (br_info->err_code,
					     br_info->os_err_code));
	      res = -1;
	    }
	  break;
	}
    }

  if (i == broker_check_loop_count)
    {
      sprintf (admin_err_msg, "%s: unknown error", br_info->name);
      res = -1;
    }

end:
  if (shm_appl)
    {
      uw_shm_detach (shm_appl);
    }
  if (shm_proxy_p)
    {
      uw_shm_detach (shm_proxy_p);
    }
  if (env)
    {
      free_env (env, env_num);
    }

  return res;
}

static int
br_inactivate (T_BROKER_INFO * br_info)
{
  int res = 0;
  int as_index;
  T_SHM_APPL_SERVER *shm_appl = NULL;
  time_t cur_time = time (NULL);
  struct tm ct;
  int proxy_index;
  T_SHM_PROXY *shm_proxy_p = NULL;
  T_PROXY_INFO *proxy_info_p = NULL;
#if defined(WINDOWS)
  char acl_sem_name[BROKER_NAME_LEN];
#endif /* WINDOWS */

  if (localtime_r (&cur_time, &ct) == NULL)
    {
      res = -1;
      goto end;
    }
  ct.tm_year += 1900;

  if (br_info->pid)
    {
      ut_kill_broker_process (br_info->pid, br_info->name);

      br_info->pid = 0;

      SLEEP_MILISEC (1, 0);
    }

  shm_appl = (T_SHM_APPL_SERVER *)
    uw_shm_open (br_info->appl_server_shm_id, SHM_APPL_SERVER,
		 SHM_MODE_ADMIN);

  if (shm_appl == NULL)
    {
      SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			uw_get_os_error_code ());
      res = -1;
      goto end;
    }

  if (br_info->shard_flag == ON)
    {

      shm_proxy_p =
	(T_SHM_PROXY *) uw_shm_open (br_info->proxy_shm_id,
				     SHM_PROXY, SHM_MODE_ADMIN);
      if (shm_proxy_p == NULL)
	{
	  SHM_OPEN_ERR_MSG (admin_err_msg, uw_get_error_code (),
			    uw_get_os_error_code ());
	  res = -1;
	  goto end;
	}

      for (proxy_index = 0; proxy_index < shm_proxy_p->num_proxy;
	   proxy_index++)
	{
	  proxy_info_p = shard_shm_find_proxy_info (shm_proxy_p, proxy_index);

	  proxy_inactivate (br_info, proxy_info_p);
	}
    }

  for (as_index = 0; as_index < br_info->appl_server_max_num; as_index++)
    {
      as_inactivate (&shm_appl->as_info[as_index], br_info->name,
		     shm_appl->shard_flag);
    }

  if (br_info->log_backup == ON)
    {
      rename_error_log_file_name (br_info->error_log_file, &ct);
    }
  else
    {
      unlink (br_info->error_log_file);
    }

  br_info->num_busy_count = 0;
  br_info->service_flag = OFF;

#if defined (WINDOWS)
  MAKE_ACL_SEM_NAME (acl_sem_name, shm_appl->broker_name);
  uw_sem_destroy (acl_sem_name);
#else
  uw_sem_destroy (&shm_appl->acl_sem);
#endif

end:
  if (shm_appl)
    {
      uw_shm_detach (shm_appl);
    }
  if (shm_proxy_p)
    {
      uw_shm_detach (shm_proxy_p);
    }
  return res;
}

static void
as_activate (T_SHM_BROKER * shm_br, T_BROKER_INFO * br_info,
	     T_SHM_APPL_SERVER * shm_appl, T_APPL_SERVER_INFO * as_info,
	     int as_index, char **env, int env_num)
{
  int pid;
  char appl_server_shm_key_env_str[32];
  char appl_name[APPL_SERVER_NAME_MAX_SIZE];
  int i;
  char as_id_env_str[32];

#if !defined(WINDOWS)
  char process_name[128];
  char port_name[BROKER_PATH_MAX];

  if (br_info->shard_flag == OFF)
    {
      ut_get_as_port_name (port_name, br_info->name, as_index,
			   BROKER_PATH_MAX);
      unlink (port_name);
    }
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
  as_info->driver_version[0] = '\0';

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

      sprintf (appl_server_shm_key_env_str, "%s=%d",
	       APPL_SERVER_SHM_KEY_STR, br_info->appl_server_shm_id);
      putenv (appl_server_shm_key_env_str);

      snprintf (as_id_env_str, sizeof (as_id_env_str), "%s=%d", AS_ID_ENV_STR,
		as_index);
      putenv (as_id_env_str);

      strcpy (appl_name, shm_appl->appl_server_name);


#if !defined(WINDOWS)

      if (br_info->shard_flag == ON)
	{
	  snprintf (process_name, sizeof (process_name) - 1,
		    "%s_%s_%d_%d_%d", shm_appl->broker_name, appl_name,
		    as_info->proxy_id + 1, as_info->shard_id,
		    as_info->shard_cas_id + 1);
	}
      else if (br_info->appl_server == APPL_SERVER_CAS_ORACLE)
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

  if (br_info->shard_flag == OFF)
    {
      (void) ut_is_appl_server_ready (pid, &as_info->service_ready_flag);
    }

  as_info->pid = pid;
  as_info->last_access_time = time (NULL);
  as_info->transaction_start_time = (time_t) 0;
  as_info->psize_time = time (NULL);
  as_info->psize = getsize (as_info->pid);

  if (br_info->shard_flag == ON)
    {
      as_info->uts_status = UTS_STATUS_CON_WAIT;
    }
  else
    {
      as_info->uts_status = UTS_STATUS_IDLE;
    }

  as_info->service_flag = SERVICE_ON;
}

static void
as_inactivate (T_APPL_SERVER_INFO * as_info_p, char *broker_name,
	       int shard_flag)
{
  if (as_info_p->pid <= 0)
    {
      return;
    }

  ut_kill_as_process (as_info_p->pid, broker_name, as_info_p->as_id,
		      shard_flag);

  as_info_p->pid = 0;
  as_info_p->service_flag = SERVICE_OFF;
  as_info_p->service_ready_flag = FALSE;

  /* initialize con / uts status */
  as_info_p->uts_status = UTS_STATUS_IDLE;
  as_info_p->con_status = CON_STATUS_CLOSE;

  CON_STATUS_LOCK_DESTROY (as_info_p);

  return;
}

static int
check_shard_conn (T_SHM_APPL_SERVER * shm_as_p, T_SHM_PROXY * shm_proxy_p)
{
  T_PROXY_INFO *proxy_info_p;
  T_SHARD_INFO *shard_info_p;
  int proxy_id, shard_id;
  int num_proxy;
  int res = 0;

  assert (shm_as_p != NULL);
  assert (shm_proxy_p != NULL);

  num_proxy = shm_proxy_p->num_proxy;

  assert (num_proxy <= MAX_PROXY_NUM);

  for (proxy_id = 0; proxy_id < num_proxy; proxy_id++)
    {
      proxy_info_p = shard_shm_find_proxy_info (shm_proxy_p, proxy_id);

      for (shard_id = 0; shard_id < proxy_info_p->num_shard_conn; shard_id++)
	{
	  shard_info_p = shard_shm_find_shard_info (proxy_info_p, shard_id);

	  res = check_shard_as_conn (shm_as_p, shard_info_p);

	  if (res < 0)
	    {
	      sprintf (admin_err_msg,
		       "failed to connect database. [%s]\n\n"
		       "please check your $CUBRID/conf/cubrid_broker.conf or database status.\n\n"
		       "[%%%s]\n"
		       "%-20s = %s\n"
		       "%-20s = %s\n",
		       shm_as_p->broker_name, shm_as_p->broker_name,
		       "SHARD_DB_NAME",
		       shm_as_p->shard_conn_info[shard_id].db_name,
		       "SHARD_DB_USER",
		       shm_as_p->shard_conn_info[shard_id].db_user);

	      return -1;
	    }
	}
    }

  return 0;
}

static int
check_shard_as_conn (T_SHM_APPL_SERVER * shm_as_p,
		     T_SHARD_INFO * shard_info_p)
{
  int i;
  int as_id;

  for (i = 0; i < SERVICE_READY_WAIT_COUNT; i++)
    {
      for (as_id = 0; as_id < shard_info_p->min_appl_server; as_id++)
	{
	  if (shm_as_p->as_info[as_id +
				shard_info_p->as_info_index_base].
	      uts_status != UTS_STATUS_CON_WAIT)
	    {
	      return 0;
	    }
	}

      SLEEP_MILISEC (0, 10);
    }

  return -1;
}

static int
proxy_activate (T_BROKER_INFO * br_info_p, T_SHM_PROXY * shm_proxy_p,
		T_SHM_APPL_SERVER * shm_as_p, char **env, int env_num)
{
  int i;
  T_PROXY_INFO *proxy_info_p = NULL;

  for (i = 0; i < shm_proxy_p->num_proxy; i++)
    {
      proxy_info_p = shard_shm_find_proxy_info (shm_proxy_p, i);

      snprintf (proxy_info_p->access_log_file, CONF_LOG_FILE_LEN - 1,
		"%s/%s_%d.access", CUBRID_BASE_DIR, br_info_p->name, i);
      dir_repath (proxy_info_p->access_log_file, CONF_LOG_FILE_LEN);

      proxy_info_p->cur_proxy_log_mode = br_info_p->proxy_log_mode;

      if (proxy_activate_internal (br_info_p->proxy_shm_id, shm_as_p,
				   shm_proxy_p, i, env, env_num) < 0)
	{
	  return -1;
	}
    }
  return 0;
}

static int
proxy_activate_internal (int proxy_shm_id, T_SHM_APPL_SERVER * shm_as_p,
			 T_SHM_PROXY * shm_proxy_p, int proxy_id, char **env,
			 int env_num)
{
  int pid = 0, i;
  const char *proxy_exe_name = NAME_PROXY;
  char proxy_shm_id_env_str[32];
  char proxy_id_env_str[32];

#if !defined (WINDOWS)
  char process_name[128];
#endif

  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;

  proxy_info_p = shard_shm_find_proxy_info (shm_proxy_p, proxy_id);
  if (proxy_info_p == NULL)
    {
      return -1;
    }

  proxy_info_p->cur_client = 0;

  shard_info_p = shard_shm_find_shard_info (proxy_info_p, 0);
  if (shard_info_p == NULL)
    {
      return -1;
    }

  /* max_context should be GE max_client */
  if (proxy_info_p->max_context < proxy_info_p->max_client)
    {
      sprintf (admin_err_msg,
	       "max_client %d is greater than %d [%s]\n\n"
	       "please check your $CUBRID/conf/cubrid_broker.conf\n\n"
	       "[%%%s]\n"
	       "%-20s = %d\n"
	       "%-20s = %d * %d\n", proxy_info_p->max_client,
	       proxy_info_p->max_context, shm_as_p->broker_name,
	       shm_as_p->broker_name, "SHARD_MAX_CLIENTS",
	       proxy_info_p->max_client, "MAX_NUM_APPL_SERVER",
	       shard_info_p->max_appl_server, proxy_info_p->max_shard);
      return -1;
    }

  ut_get_proxy_port_name (proxy_info_p->port_name, shm_as_p->broker_name,
			  proxy_info_p->proxy_id, SHM_PROXY_NAME_MAX);

#if !defined(WINDOWS)
  unlink (proxy_info_p->port_name);

  if ((pid = fork ()) < 0)
    {
      strcpy (admin_err_msg, "fork error");

      return -1;
    }
#endif /* WINDOWS */

  if (pid == 0)
    {
#if !defined(WINDOWS)
      signal (SIGCHLD, SIG_DFL);
#endif /* !WINDOWS */

      if (env != NULL)
	{
	  for (i = 0; i < env_num; i++)
	    {
	      putenv (env[i]);
	    }
	}
      snprintf (proxy_shm_id_env_str, sizeof (proxy_shm_id_env_str), "%s=%d",
		PROXY_SHM_KEY_STR, proxy_shm_id);
      snprintf (proxy_id_env_str, sizeof (proxy_id_env_str), "%s=%d",
		PROXY_ID_ENV_STR, proxy_id);

      putenv (proxy_shm_id_env_str);
      putenv (proxy_id_env_str);

#if !defined(WINDOWS)
      snprintf (process_name, sizeof (process_name) - 1, "%s_%s_%d",
		shm_as_p->broker_name, proxy_exe_name, proxy_id + 1);
#endif /* !WINDOWS */


#if defined(WINDOWS)
      pid = run_child (proxy_exe_name);
    }
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
proxy_inactivate (T_BROKER_INFO * br_info_p, T_PROXY_INFO * proxy_info_p)
{
  if (proxy_info_p->pid > 0)
    {
      ut_kill_proxy_process (proxy_info_p->pid, br_info_p->name,
			     proxy_info_p->proxy_id);
    }
  proxy_info_p->pid = 0;
  proxy_info_p->cur_client = 0;

  /* SHARD TODO : backup or remove access log file */

  return;
}

static void
free_env (char **env, int env_num)
{
  int i;

  if (env == NULL)
    {
      return;
    }

  for (i = 0; i < env_num; i++)
    {
      FREE_MEM (env[i]);
    }
  FREE_MEM (env);
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

static void
rename_error_log_file_name (char *error_log_file, struct tm *ct)
{
  char cmd_buf[BUFSIZ];

  sprintf (cmd_buf, "%s.%02d%02d%02d.%02d%02d",
	   error_log_file,
	   ct->tm_year, ct->tm_mon + 1, ct->tm_mday, ct->tm_hour, ct->tm_min);
  rename (error_log_file, cmd_buf);
}

static int
shard_shm_set_param_proxy (T_SHM_PROXY * proxy_p, const char *param_name,
			   const char *param_value, int proxy_id)
{
  int proxy_index;
  T_PROXY_INFO *proxy_info_p = NULL;

  if (proxy_id < 0)
    {
      for (proxy_index = 0; proxy_index < proxy_p->num_proxy; proxy_index++)
	{
	  proxy_info_p = shard_shm_find_proxy_info (proxy_p, proxy_index);

	  if (shard_shm_set_param_proxy_internal (proxy_info_p, param_name,
						  param_value) < 0)
	    {
	      return -1;
	    }
	}
    }
  else
    {
      proxy_info_p = shard_shm_find_proxy_info (proxy_p, proxy_id);

      if (proxy_info_p == NULL)
	{
	  sprintf (admin_err_msg, "Cannot find proxy info\n");
	  return -1;
	}

      if (shard_shm_set_param_proxy_internal (proxy_info_p, param_name,
					      param_value) < 0)
	{
	  return -1;
	}
    }

  return 0;
}

static int
shard_shm_set_param_proxy_internal (T_PROXY_INFO * proxy_info_p,
				    const char *param_name,
				    const char *param_value)
{
  if (strcasecmp (param_name, "SHARD_PROXY_LOG") == 0)
    {
      char proxy_log_mode;

      proxy_log_mode = conf_get_value_proxy_log_mode (param_value);

      if (proxy_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", param_value);
	  return -1;
	}

      proxy_info_p->cur_proxy_log_mode = proxy_log_mode;
    }

  return 0;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
shard_shm_set_param_shard (T_PROXY_INFO * proxy_info_p,
			   const char *param_name, const char *param_value,
			   int shard_id)
{
  int i;
  T_SHM_APPL_SERVER *shm_as_p = NULL;
  T_SHARD_CONN_INFO *shard_conn_info_p = NULL;

  shm_as_p =
    (T_SHM_APPL_SERVER *) uw_shm_open (proxy_info_p->appl_server_shm_id,
				       SHM_APPL_SERVER, SHM_MODE_ADMIN);
  if (shm_as_p == NULL)
    {
      return -1;
    }

  if (shard_id < 0)
    {
      for (i = 0; i < proxy_info_p->num_shard_conn; i++)
	{
	  shard_conn_info_p = &shm_as_p->shard_conn_info[i];

	  shard_shm_set_param_shard_internal (shard_conn_info_p, param_name,
					      param_value);
	}
    }
  else if (shard_id < proxy_info_p->num_shard_conn)
    {
      shard_conn_info_p = &shm_as_p->shard_conn_info[shard_id];

      shard_shm_set_param_shard_internal (shard_conn_info_p, param_name,
					  param_value);
    }
  else
    {
      uw_shm_detach (shm_as_p);
      return -1;
    }
  uw_shm_detach (shm_as_p);

  return 0;
}

static int
shard_shm_set_param_shard_internal (T_SHARD_CONN_INFO * shard_conn_info_p,
				    const char *param_name,
				    const char *param_value)
{
  if (strcasecmp (param_name, "SHARD_DB_USER") == 0)
    {
      strncpy (shard_conn_info_p->db_user, param_value,
	       sizeof (shard_conn_info_p->db_user) - 1);
    }
  else if (strcasecmp (param_name, "SHARD_DB_PASSWORD") == 0)
    {
      strncpy (shard_conn_info_p->db_password, param_value,
	       sizeof (shard_conn_info_p->db_password) - 1);
    }

  return 0;
}

static int
shard_shm_set_param_shard_in_proxy (T_SHM_PROXY * proxy_p,
				    const char *param_name,
				    const char *param_value, int proxy_id,
				    int shard_id)
{
  int proxy_index;
  T_PROXY_INFO *proxy_info_p = NULL;

  if (proxy_id < 0)
    {
      for (proxy_index = 0; proxy_index < proxy_p->num_proxy; proxy_index++)
	{
	  proxy_info_p = shard_shm_find_proxy_info (proxy_p, proxy_index);

	  if (shard_shm_set_param_shard
	      (proxy_info_p, param_name, param_value, shard_id) < 0)
	    {
	      return -1;
	    }
	}
    }
  else
    {
      proxy_info_p = shard_shm_find_proxy_info (proxy_p, proxy_id);

      if (proxy_info_p == NULL)
	{
	  sprintf (admin_err_msg, "Cannot find proxy info\n");
	  return -1;
	}

      if (shard_shm_set_param_shard (proxy_info_p, param_name, param_value,
				     shard_id) < 0)
	{
	  return -1;
	}
    }

  return 0;
}
#endif /* ENABLE_UNUSED_FUNCTION */

static int
shard_shm_set_param_as (T_PROXY_INFO * proxy_info_p,
			T_SHARD_INFO * shard_info_p, const char *param_name,
			const char *param_value, int as_number)
{
  int i;
  T_SHM_APPL_SERVER *shm_as_p = NULL;

  shm_as_p =
    (T_SHM_APPL_SERVER *) uw_shm_open (proxy_info_p->appl_server_shm_id,
				       SHM_APPL_SERVER, SHM_MODE_ADMIN);

  if (as_number < 0)
    {
      for (i = 0; i < shard_info_p->max_appl_server; i++)
	{
	  if (shard_shm_set_param_as_internal
	      (&shm_as_p->as_info[i + shard_info_p->as_info_index_base],
	       param_name, param_value) < 0)
	    {
	      uw_shm_detach (shm_as_p);
	      return -1;
	    }
	}
    }
  else
    {
      if (shard_shm_set_param_as_internal (&shm_as_p->as_info[as_number +
							      shard_info_p->
							      as_info_index_base],
					   param_name, param_value) < 0)
	{
	  uw_shm_detach (shm_as_p);
	  return -1;
	}
    }

  uw_shm_detach (shm_as_p);
  return 0;
}

static int
shard_shm_set_param_as_internal (T_APPL_SERVER_INFO * as_info,
				 const char *param_name,
				 const char *param_value)
{
  if (strcasecmp (param_name, "SQL_LOG") == 0)
    {
      char sql_log_mode;

      sql_log_mode = conf_get_value_sql_log_mode (param_value);

      if (sql_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", param_value);
	  return -1;
	}

      as_info->cur_sql_log_mode = sql_log_mode;
      as_info->cas_log_reset = CAS_LOG_RESET_REOPEN;
    }
  else if (strcasecmp (param_name, "SLOW_LOG") == 0)
    {
      char slow_log_mode;

      slow_log_mode = conf_get_value_table_on_off (param_value);

      if (slow_log_mode < 0)
	{
	  sprintf (admin_err_msg, "invalid value : %s", param_value);
	  return -1;
	}

      as_info->cur_slow_log_mode = slow_log_mode;
      as_info->cas_slow_log_reset = CAS_LOG_RESET_REOPEN;
    }
  else if (strcasecmp (param_name, "ACCESS_MODE") == 0
	   || strcasecmp (param_name, "CONNECT_ORDER") == 0
	   || strcasecmp (param_name, "TRIGGER_ACTION") == 0)
    {
      as_info->reset_flag = TRUE;
    }

  return 0;
}


static int
shard_shm_set_param_as_in_shard (T_PROXY_INFO * proxy_info_p,
				 const char *param_name,
				 const char *param_value, int shard_id,
				 int as_number)
{
  int shard_index;
  T_SHARD_INFO *shard_info_p = NULL;

  if (shard_id < 0)
    {
      for (shard_index = 0; shard_index < proxy_info_p->num_shard_conn;
	   shard_index++)
	{
	  shard_info_p =
	    shard_shm_find_shard_info (proxy_info_p, shard_index);

	  if (shard_shm_set_param_as
	      (proxy_info_p, shard_info_p, param_name, param_value,
	       as_number) < 0)
	    {
	      return -1;
	    }
	}
    }
  else
    {
      shard_info_p = shard_shm_find_shard_info (proxy_info_p, shard_id);

      if (shard_info_p == NULL)
	{
	  sprintf (admin_err_msg, "Cannot find shard info\n");
	  return -1;
	}

      if (shard_shm_set_param_as
	  (proxy_info_p, shard_info_p, param_name, param_value,
	   as_number) < 0)
	{
	  return -1;
	}
    }

  return 0;
}

static int
shard_shm_set_param_as_in_proxy (T_SHM_PROXY * proxy_p,
				 const char *param_name,
				 const char *param_value, int proxy_id,
				 int shard_id, int as_number)
{
  int proxy_index;
  T_PROXY_INFO *proxy_info_p = NULL;

  if (proxy_id < 0)
    {
      for (proxy_index = 0; proxy_index < proxy_p->num_proxy; proxy_index++)
	{
	  proxy_info_p = shard_shm_find_proxy_info (proxy_p, proxy_index);

	  if (shard_shm_set_param_as_in_shard (proxy_info_p, param_name,
					       param_value, shard_id,
					       as_number) < 0)
	    {
	      return -1;
	    }
	}
    }
  else
    {
      proxy_info_p = shard_shm_find_proxy_info (proxy_p, proxy_id);

      if (proxy_info_p == NULL)
	{
	  sprintf (admin_err_msg, "Cannot find proxy info\n");
	  return -1;
	}

      if (shard_shm_set_param_as_in_shard (proxy_info_p, param_name,
					   param_value, shard_id,
					   as_number) < 0)
	{
	  return -1;
	}
    }

  return 0;
}

static int
shard_shm_check_max_file_open_limit (T_BROKER_INFO * br_info,
				     T_SHM_PROXY * proxy_p)
{
#if defined(LINUX)
  int error = 0;
  unsigned int required_fd_num = 0;
  T_PROXY_INFO *proxy_info_p = NULL;
  T_SHARD_INFO *shard_info_p = NULL;
  struct rlimit sys_limit;

  proxy_info_p = shard_shm_find_proxy_info (proxy_p, 0);
  if (proxy_info_p == NULL)
    {
      sprintf (admin_err_msg, "Cannot find proxy info.\n");
      return -1;
    }

  shard_info_p = shard_shm_find_shard_info (proxy_info_p, 0);
  if (shard_info_p == NULL)
    {
      sprintf (admin_err_msg, "Cannot find shard info.\n");
      return -1;
    }

  required_fd_num += proxy_info_p->max_context;
  required_fd_num += proxy_info_p->max_shard * shard_info_p->max_appl_server;
  required_fd_num += PROXY_RESERVED_FD;

  error = getrlimit (RLIMIT_NOFILE, &sys_limit);
  if (error < 0)
    {
      sprintf (admin_err_msg, "Fail to get system limit.\n");
      return -1;
    }

  if (sys_limit.rlim_cur < required_fd_num)
    {
      sprintf (admin_err_msg,
	       "%s (current : %d, required : %d)\n",
	       "Maximum file descriptor number is less than the required value.",
	       (int) sys_limit.rlim_cur, required_fd_num);
      return -1;
    }

  if (sys_limit.rlim_max < required_fd_num)
    {
      sprintf (admin_err_msg,
	       "%s (current : %d, required : %d)\n",
	       "Maximum file descriptor number is less than the required value.",
	       (int) sys_limit.rlim_max, required_fd_num);
      return -1;
    }
#endif /* LINUX */
  return 0;
}

static void
get_shard_db_password (T_BROKER_INFO * br_info_p)
{
  char env_str[128];
  char upper_broker_name[BROKER_NAME_LEN];
  char *p = NULL;

  get_upper_str (upper_broker_name, BROKER_NAME_LEN, br_info_p->name);

  snprintf (env_str, sizeof (env_str), "%s_SHARD_DB_PASSWORD",
	    upper_broker_name);

  p = getenv (env_str);
  if (p != NULL)
    {
      strncpy (br_info_p->shard_db_password, p,
	       sizeof (br_info_p->shard_db_password) - 1);
    }

  return;
}

static void
get_upper_str (char *upper_str, int len, char *value)
{
  int i;

  for (i = 0; i < len - 1; i++)
    {
      upper_str[i] = (char) toupper (value[i]);
    }
  upper_str[i] = '\0';

  return;
}
