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
 * broker_changer.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(WINDOWS)
#include <process.h>
#endif /* WINDOWS */

#include "broker_admin_pub.h"
#include "broker_config.h"
#include "broker_util.h"

#if defined(WINDOWS)
#include "broker_wsa_init.h"
#endif /* WINDOWS */

int
main (int argc, char *argv[])
{
  char *br_name;
  char *conf_name;
  char *conf_value;
  T_BROKER_INFO br_info[MAX_BROKER_NUM];
  int num_broker, master_shm_id;
#if defined(CUBRID_SHARD)
  int proxy_number = -1;
#else
  int as_number = -1;
#endif

  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("VERSION %s\n", makestring (BUILD_NUMBER));
      return 0;
    }

#if defined(WINDOWS)
  wsa_initialize ();
#endif /* WINDOWS */

  if (argc < 4)
    {
#if defined(CUBRID_SHARD)
      printf ("%s <shard-name> [<proxy-number>] <conf-name> <conf-value>\n",
	      argv[0]);
#else /* CUBRID_SHARD */
      printf ("%s <broker-name> [<cas-number>] <conf-name> <conf-value>\n",
	      argv[0]);
#endif /* !CUBRID_SHARD */
      exit (0);
    }

  if (broker_config_read (NULL, br_info, &num_broker, &master_shm_id, NULL,
			  0, NULL, NULL, NULL) < 0)
    {
      printf ("config file error\n");
      exit (0);
    }

  ut_cd_work_dir ();

  br_name = argv[1];

  if (argc == 5)
    {
      char *p = NULL;

#if defined(CUBRID_SHARD)
      proxy_number = (int) strtol (argv[2], &p, 10);
      if ((errno == ERANGE) ||
	  (errno != 0 && proxy_number == 0) ||
	  (p && *p != '\0') || (proxy_number < 0))
	{
	  printf ("Invalid proxy number\n");
	  exit (0);
	}
#else /* CUBRID_SHARD */
      as_number = (int) strtol (argv[2], &p, 10);
      if ((errno == ERANGE) ||
	  (errno != 0 && as_number == 0) ||
	  (p && *p != '\0') || (as_number < 0))
	{
	  printf ("Invalid cas number\n");
	  exit (0);
	}
#endif /* !CUBRID_SHARD */

      conf_name = argv[3];
      conf_value = argv[4];
    }
  else
    {
      conf_name = argv[2];
      conf_value = argv[3];
    }

  admin_err_msg[0] = '\0';
#if defined(CUBRID_SHARD)
  if (admin_shard_conf_change (master_shm_id,
			       br_name, conf_name, conf_value,
			       proxy_number) < 0)
#else /* CUBRID_SHARD */
  if (admin_broker_conf_change (master_shm_id,
				br_name, conf_name, conf_value,
				as_number) < 0)
#endif /* !CUBRID_SHARD */
    {
      printf ("%s\n", admin_err_msg);
      exit (0);
    }
  printf ("OK\n");
  return 0;
}
