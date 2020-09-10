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
 * javasp.c - utility java stored procedure server main routine
 *
 */

#ident "$Id$"

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "environment_variable.h"
#include "system_parameter.h"
#include "error_code.h"
#include "message_catalog.h"
#include "utility.h"
#include "databases_file.h"
#include "jsp_sr.h"

static bool
is_javasp_server_running (int port_number)
{
  return false;
}

static char executable_path[PATH_MAX];
/*
 * main() - javasp main function
 *   return: EXIT_SUCCESS/EXIT_FAILURE
 */
int
main (int argc, char *argv[])
{
  int status = EXIT_SUCCESS;
  int command;
  char *db_name = NULL;
  char *binary_name;

  DB_INFO *db;
  {
    /* save executable path */
    binary_name = basename (argv[0]);
    (void) envvar_bindir_file (executable_path, PATH_MAX, binary_name);

#if !defined(WINDOWS)
    /* create a new session */
    setsid ();
#endif

    /* initialize message catalog for argument parsing and usage() */
    if (utility_initialize () != NO_ERROR)
      {
	return EXIT_FAILURE;
      }

    /* save database name */
    db_name = argv[2];

    printf ("javasp main\n");

    if ((db = cfg_find_db (db_name)) == NULL)
      {
	fprintf (stderr, "database '%s' does not exist.\n", db_name);
	goto exit;
      }

    status = sysprm_load_and_init (db_name, NULL, SYSPRM_IGNORE_INTL_PARAMS);
    if (status != NO_ERROR)
      {
	util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
	goto exit;
      }

    int port_number = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_PORT);

    /*
     * check javasp server is running on port number
     */
    if (is_javasp_server_running (port_number))
      {
	goto exit;
      }

    /* create a new session */
    setsid ();

    status = jsp_start_server (db_name, db->pathname, port_number);
    if (status != NO_ERROR)
      {
	// failed to start javasp server
	goto exit;
      }
  }

exit:
  return status;
}
