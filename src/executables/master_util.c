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
 * master_util.c - common module for commdb and master
 */

#ident "$Id$"

#include "config.h"

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>

#include "system_parameter.h"
#include "master_util.h"

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
  if (sysprm_load_and_init (db_name, NULL, SYSPRM_IGNORE_INTL_PARAMS) != NO_ERROR)
    {
      return false;
    }
  *port_id = prm_get_master_port_id ();

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
