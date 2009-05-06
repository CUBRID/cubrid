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
  char admin_log_file[256];
  int num_broker, master_shm_id;

  if (argc == 2 && strcmp (argv[1], "--version") == 0)
    {
      printf ("VERSION %s\n", makestring (BUILD_NUMBER));
      return 0;
    }

#if defined(WINDOWS)
  wsa_initialize ();
#endif /* WINDOWS */

  ut_cd_work_dir ();

  if (argc < 4)
    {
      printf ("%s <br-name> <conf-name> <conf-value>\n", argv[0]);
      exit (0);
    }

  if (broker_config_read
      (br_info, &num_broker, &master_shm_id, admin_log_file) < 0)
    {
      printf ("config file error\n");
      exit (0);
    }

  br_name = argv[1];
  conf_name = argv[2];
  conf_value = argv[3];

  admin_err_msg[0] = '\0';
  if (admin_broker_conf_change (master_shm_id, br_name, conf_name, conf_value)
      < 0)
    {
      printf ("%s\n", admin_err_msg);
      exit (0);
    }
  printf ("OK\n");
  return 0;
}
