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
 * broker_changer.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(WINDOWS)
#include <process.h>
#include "porting.h"
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
  int as_number = -1;

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
      printf ("%s <broker-name> [<cas-number>] <conf-name> <conf-value>\n", argv[0]);
      exit (0);
    }

  if (broker_config_read (NULL, br_info, &num_broker, &master_shm_id, NULL, 0, NULL, NULL, NULL) < 0)
    {
      printf ("config file error\n");
      exit (0);
    }

  ut_cd_work_dir ();

  br_name = argv[1];

  if (argc == 5)
    {
      int result;

      result = parse_int (&as_number, argv[2], 10);

      if (result != 0 || as_number < 0)
	{
	  printf ("Invalid cas number\n");
	  exit (0);
	}

      conf_name = argv[3];
      conf_value = argv[4];
    }
  else
    {
      conf_name = argv[2];
      conf_value = argv[3];
    }

  admin_err_msg[0] = '\0';

  if (admin_conf_change (master_shm_id, br_name, conf_name, conf_value, as_number) < 0)
    {
      printf ("%s\n", admin_err_msg);
      exit (0);
    }

  if (admin_err_msg[0] != '\0')
    {
      printf ("%s\n", admin_err_msg);
    }

  printf ("OK\n");
  return 0;
}
