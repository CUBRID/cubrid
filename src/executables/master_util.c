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
  sysprm_load_and_init (db_name, NULL);
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
