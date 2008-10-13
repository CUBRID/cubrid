/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * master_util.c - common module for commdb and master
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "system_parameter.h"

/*
 * master_util_config_startup() - get port id and service name from parameters
 *   return: true if port id and service name are valid,
 *           otherwise false
 *   db_name(in)
 *   port_id(out)
 */
bool
master_util_config_startup (const char *db_name, int *port_id)
{
  sysprm_load_and_init (db_name, NULL);
  *port_id = PRM_TCP_PORT_ID;

  /*
   * Must give either port_id or service_name
   * if port == 0, nothing special use port number of service
   *    port < 0, bind a local reserved port
   *    port > 0, it is the port number of service
   */
  if (*port_id <= 0)
    {
      return false;
    }
  else
    {
      return true;
    }
}

/*
 * master_util_wait_proc_terminate()
 *   return: none
 *   pid(in)
 */
void
master_util_wait_proc_terminate (int pid)
{
#if defined(WINDOWS)
  HANDLE h;
  h = OpenProcess (SYNCHRONIZE, FALSE, pid);
  if (h)
    {
      WaitForSingleObject (h, INFINITE);
      CloseHandle (h);
    }
#else /* ! WINDOWS */
  while (1)
    {
      if (kill (pid, 0) < 0)
	break;
      sleep (1);
    }
#endif /* ! WINDOWS */
}
