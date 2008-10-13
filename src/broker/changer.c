/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * changer.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef WIN32
#include <process.h>
#endif

#include "admin_pub.h"
#include "br_config.h"
#include "util.h"

#ifdef WIN32
#include "wsa_init.h"
#endif

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

#ifdef WIN32
  wsa_initialize ();
#endif

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

