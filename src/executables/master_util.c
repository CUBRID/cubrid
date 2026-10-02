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
#include "environment_variable.h"

#include <string.h>
#include <limits.h>

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

/*
 * master_util_exec_path_is_trusted () - is exec_path a trusted CUBRID server binary path?
 *   return: true if exec_path has the form <CUBRID>/bin/<name> with no directory
 *           component of its own and no parent-directory traversal; else false.
 *   exec_path(in): the path a server registration asks the master to execv ()
 *
 * Note: a legitimate cub_server always registers the path built by
 *   envvar_bindir_file (basename (argv[0])), i.e. a file living directly in the
 *   installation bin directory. Confining revival to that directory keeps a
 *   caller-supplied exec_path from becoming arbitrary command execution.
 */
bool
master_util_exec_path_is_trusted (const char *exec_path)
{
  char bindir[PATH_MAX];
  size_t bindir_len;
  const char *base;

  if (exec_path == NULL || exec_path[0] == '\0')
    {
      return false;
    }

  /* reject any parent-directory traversal outright */
  if (strstr (exec_path, "..") != NULL)
    {
      return false;
    }

  /* expected trusted directory prefix: <CUBRID>/bin/ */
  bindir[0] = '\0';
  (void) envvar_bindir_file (bindir, sizeof (bindir), "");
  bindir_len = strlen (bindir);
  if (bindir_len == 0 || strncmp (exec_path, bindir, bindir_len) != 0)
    {
      return false;
    }

  /* the remainder must name a file directly in bin/ (single path component) */
  base = exec_path + bindir_len;
  if (base[0] == '\0' || strchr (base, '/') != NULL)
    {
      return false;
    }

  return true;
}
