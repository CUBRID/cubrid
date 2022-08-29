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
 * broker_admin.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(WINDOWS)
#include <direct.h>
#endif /* WINDOWS */

#include "cas_common.h"
#include "broker_admin_pub.h"
#include "broker_config.h"
#include "broker_shm.h"
#include "broker_error.h"

#include "broker_util.h"
#include "util_func.h"
#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

#define	TRUE			1
#define	FALSE			0

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
  else
    {
      printf ("cannot open admin log file [%s]\n", log_file);
    }
}

int
main (int argc, char **argv)
{
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  char admin_log_file[BROKER_PATH_MAX];
  char acl_file[BROKER_PATH_MAX];
  bool acl_flag;
  int num_broker, master_shm_id;
  int err;
  char msg_buf[256];

  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("VERSION %s\n", makestring (BUILD_NUMBER));
      return 0;
    }

  /* change the working directory to $CUBRID */
  ut_cd_root_dir ();

  err = broker_config_read (NULL, br_info, &num_broker, &master_shm_id, admin_log_file, 0, &acl_flag, acl_file, NULL);
  if (err < 0)
    {
#if defined (FOR_ODBC_GATEWAY)
      util_log_write_errstr ("gateway config read error.\n");
#else
      util_log_write_errstr ("broker config read error.\n");
#endif
      return -1;
    }

  /* change the working directory to $CUBRID/bin */
  ut_cd_work_dir ();

  if (argc < 2)
    {
      goto usage;
    }

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      PRINT_AND_LOG_ERR_MSG ("WSA init error\n");
      return 0;
    }
#endif /* WINDOWS */

#if 0
  if (admin_get_host_ip ())
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
      return -1;
    }
#endif
  admin_err_msg[0] = '\0';

  admin_init_env ();

  if (strcasecmp (argv[1], "start") == 0)
    {
      T_SHM_BROKER *shm_br;

#ifdef ORACLE
      if (check_key () == FALSE)
	{
	  return -1;
	}
#endif

      shm_br = (T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER, SHM_MODE_MONITOR);

      if (shm_br == NULL && uw_get_error_code () != UW_ER_SHM_OPEN_MAGIC)
	{
	  if (admin_start_cmd (br_info, num_broker, master_shm_id, acl_flag, acl_file, admin_log_file) < 0)
	    {
	      PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	      return -1;
	    }
	  else
	    {
	      admin_log_write (admin_log_file, "start");
	    }
	}
      else
	{
	  PRINT_AND_LOG_ERR_MSG ("Error: CUBRID Broker is already running " "with shared memory key '%x'.\n",
				 master_shm_id);
	  uw_shm_detach (shm_br);
	}
    }
  else if (strcasecmp (argv[1], "stop") == 0)
    {
      if (admin_stop_cmd (master_shm_id))
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  admin_log_write (admin_log_file, "stop");
	}
    }
  else if (strcasecmp (argv[1], "add") == 0)
    {
      if (argc < 3)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s add <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_add_cmd (master_shm_id, argv[2]) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  sprintf (msg_buf, "add %s", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "restart") == 0)
    {
      if (argc < 4)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s restart <broker-name> <appl_server_index>\n", argv[0]);
	  return -1;
	}
      if (admin_restart_cmd (master_shm_id, argv[2], atoi (argv[3])) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  sprintf (msg_buf, "restart %s %s", argv[2], argv[3]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "drop") == 0)
    {
      if (argc < 3)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s drop <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_drop_cmd (master_shm_id, argv[2]) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  sprintf (msg_buf, "drop %s", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "on") == 0)
    {
      if (argc < 3)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s on <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_on_cmd (master_shm_id, argv[2]) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  sprintf (msg_buf, "%s on", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "off") == 0)
    {
      if (argc < 3)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s on <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_off_cmd (master_shm_id, argv[2]) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  sprintf (msg_buf, "%s off", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "reset") == 0)
    {
      if (argc < 3)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s reset <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_reset_cmd (master_shm_id, argv[2]) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  sprintf (msg_buf, "%s reset", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "info") == 0)
    {
      if (admin_info_cmd (master_shm_id) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
      else
	{
	  admin_log_write (admin_log_file, "info");
	}
    }
  else if (strcasecmp (argv[1], "acl") == 0)
    {
      char *br_name = NULL;
      int err_code;

      if (argc < 3)
	{
#if defined (FOR_ODBC_GATEWAY)
	  PRINT_AND_LOG_ERR_MSG ("%s acl <reload|status> <gateway-name>\n", argv[0]);
#else
	  PRINT_AND_LOG_ERR_MSG ("%s acl <reload|status> <broker-name>\n", argv[0]);
#endif
	  return -1;
	}

      if (argc > 3)
	{
	  br_name = argv[3];
	}

      if (strcasecmp (argv[2], "reload") == 0)
	{
	  err_code = admin_acl_reload_cmd (master_shm_id, br_name);
	}
      else if (strcasecmp (argv[2], "status") == 0)
	{
	  err_code = admin_acl_status_cmd (master_shm_id, br_name);
	}
      else
	{
#if defined (FOR_ODBC_GATEWAY)
	  PRINT_AND_LOG_ERR_MSG ("%s acl <reload|status> <gateway-name>\n", argv[0]);
#else
	  PRINT_AND_LOG_ERR_MSG ("%s acl <reload|status> <broker-name>\n", argv[0]);
#endif
	  return -1;
	}
      if (err_code < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
    }
  else if (strcasecmp (argv[1], "getid") == 0)
    {
      if (admin_getid_cmd (master_shm_id, argc, (const char **) argv) < 0)
	{
	  PRINT_AND_LOG_ERR_MSG ("%s\n", admin_err_msg);
	  return -1;
	}
    }
  else
    {
      goto usage;
    }

  return 0;

usage:
  printf ("%s (start | stop | add | drop | restart \
	    | on | off | reset | info | acl | getid)\n", argv[0]);
  return -1;
}
