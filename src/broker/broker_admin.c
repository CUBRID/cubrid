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
      fprintf (fp, "%d/%02d/%02d %02d:%02d:%02d %s\n",
	       ct.tm_year + 1900, ct.tm_mon + 1, ct.tm_mday,
	       ct.tm_hour, ct.tm_min, ct.tm_sec, msg);
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
  char admin_log_file[PATH_MAX];
  char acl_file[PATH_MAX];
  bool acl_flag;
  int num_broker, master_shm_id;
  int err;
  char msg_buf[256];

  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("VERSION %s\n", makestring (BUILD_NUMBER));
      return 0;
    }

  err = broker_config_read (NULL, br_info, &num_broker, &master_shm_id,
			    admin_log_file, 0, &acl_flag, acl_file, NULL);
  if (err < 0)
    return -1;

  /* change the working directory to $CUBRID/bin */
  ut_cd_work_dir ();

  if (argc < 2)
    goto usage;

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      printf ("WSA init error\n");
      return 0;
    }
#endif /* WINDOWS */

#if 0
  if (admin_get_host_ip ())
    {
      printf ("%s\n", admin_err_msg);
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

      shm_br =
	(T_SHM_BROKER *) uw_shm_open (master_shm_id, SHM_BROKER,
				      SHM_MODE_MONITOR);

      if (shm_br == NULL && uw_get_error_code () != UW_ER_SHM_OPEN_MAGIC)
	{
	  if (admin_start_cmd (br_info, num_broker, master_shm_id,
			       acl_flag, acl_file) < 0)
	    {
	      printf ("%s\n", admin_err_msg);
	      return -1;
	    }
	  else
	    {
	      admin_log_write (admin_log_file, "start");
	    }
	}
      else
	{
	  printf ("Error: CUBRID Broker is already running "
		  "with shared memory key '%x'.\n", master_shm_id);
	  uw_shm_detach (shm_br);
	}
    }
  else if (strcasecmp (argv[1], "stop") == 0)
    {
      if (admin_stop_cmd (master_shm_id))
	{
	  printf ("%s\n", admin_err_msg);
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
	  printf ("%s add <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_add_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
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
	  printf ("%s restart <broker-name> <appl_server_index>\n", argv[0]);
	  return -1;
	}
      if (admin_restart_cmd (master_shm_id, argv[2], atoi (argv[3])) < 0)
	{
	  printf ("%s\n", admin_err_msg);
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
	  printf ("%s drop <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_drop_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
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
	  printf ("%s on <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_broker_on_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
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
	  printf ("%s on <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_broker_off_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
	}
      else
	{
	  sprintf (msg_buf, "%s off", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "suspend") == 0)
    {
      if (argc < 3)
	{
	  printf ("%s suspend <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_broker_suspend_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
	}
      else
	{
	  sprintf (msg_buf, "%s suspend", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "resume") == 0)
    {
      if (argc < 3)
	{
	  printf ("%s resume <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_broker_resume_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
	}
      else
	{
	  sprintf (msg_buf, "%s resume", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "reset") == 0)
    {
      if (argc < 3)
	{
	  printf ("%s reset <broker-name>\n", argv[0]);
	  return -1;
	}
      if (admin_broker_reset_cmd (master_shm_id, argv[2]) < 0)
	{
	  printf ("%s\n", admin_err_msg);
	}
      else
	{
	  sprintf (msg_buf, "%s reset", argv[2]);
	  admin_log_write (admin_log_file, msg_buf);
	}
    }
  else if (strcasecmp (argv[1], "job_first") == 0)
    {
      int broker_status, i;

      if (argc < 4)
	{
	  printf ("%s job_first <broker-name> <job-id>\n", argv[0]);
	  return -1;
	}
      broker_status = admin_get_broker_status (master_shm_id, argv[2]);
      if (broker_status < 0)
	{
	  printf ("%s\n", admin_err_msg);
	  return -1;
	}

      if (broker_status == SUSPEND_NONE)
	{
	  if (admin_broker_suspend_cmd (master_shm_id, argv[2]) < 0)
	    {
	      printf ("%s\n", admin_err_msg);
	      return -1;
	    }
	}
      for (i = argc - 1; i >= 3; i--)
	{
	  if (admin_broker_job_first_cmd
	      (master_shm_id, argv[2], atoi (argv[i])) < 0)
	    {
	      printf ("%s\n", admin_err_msg);
	    }
	}
      if (broker_status == SUSPEND_NONE)
	{
	  if (admin_broker_resume_cmd (master_shm_id, argv[2]) < 0)
	    {
	      printf ("%s\n", admin_err_msg);
	      printf ("broker[%s] SUSPENDED\n", argv[2]);
	    }
	}
    }
  else if (strcasecmp (argv[1], "info") == 0)
    {
      if (admin_broker_info_cmd (master_shm_id) < 0)
	{
	  printf ("%s\n", admin_err_msg);
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
	  printf ("%s acl <reload|status> <broker-name>\n", argv[0]);
	  return -1;
	}

      if (argc > 3)
	{
	  br_name = argv[3];
	}

      if (strcasecmp (argv[2], "reload") == 0)
	{
	  err_code = admin_broker_acl_reload_cmd (master_shm_id, br_name);
	}
      else if (strcasecmp (argv[2], "status") == 0)
	{
	  err_code = admin_broker_acl_status_cmd (master_shm_id, br_name);
	}
      else
	{
	  printf ("%s acl <reload|status> <broker-name>\n", argv[0]);
	  return -1;
	}
      if (err_code < 0)
	{
	  printf ("%s\n", admin_err_msg);
	  return -1;
	}
    }
  else
    {
      goto usage;
    }

  return 0;

usage:
  printf ("%s (start | stop | add | drop | restart"
	  " | on | off | suspend | resume | reset | job_first | info | acl)\n",
	  argv[0]);
  return -1;
}
