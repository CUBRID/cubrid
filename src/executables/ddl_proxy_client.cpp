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
 * ddl_proxy_client.cpp - client that executes ddl queries
 */

#include <stdio.h>
#include <stdarg.h>

#include "message_catalog.h"
#include "environment_variable.h"
#include "intl_support.h"
#include "utility.h"
#include "util_support.h"
#include "cubrid_getopt.h"
#include "db_admin.h"
#include "dbi.h"
#include "authenticate.h"

static void
ddl_proxy_print_msg (int message_num, ...)
{
  const char *message = utility_get_generic_message (message_num);
  if (message != NULL)
    {
      va_list ap;

      va_start (ap, message_num);
      vfprintf (stderr, message, ap);
      va_end (ap);
    }
}

int
main (int argc, char *argv[])
{
  char arguments_string[64];
  int error = 0;
  DDL_CLIENT_ARGUMENT arguments;

  GETOPT_LONG possible_arguments[] =
  {
    {DDL_PROXY_USER_L, 1, 0, DDL_PROXY_USER_S},
    {DDL_PROXY_PASSWORD_L, 1, 0, DDL_PROXY_PASSWORD_S},
    {DDL_PROXY_OUTPUT_FILE_L, 1, 0, DDL_PROXY_OUTPUT_FILE_S},
    {DDL_PROXY_COMMAND_L, 1, 0, DDL_PROXY_COMMAND_S},
    {DDL_PROXY_REQUEST_L, 1, 0, DDL_PROXY_REQUEST_S },
    {DDL_PROXY_TRAN_INDEX_L, 1, 0, DDL_PROXY_TRAN_INDEX_S},
    {DDL_PROXY_SYS_PARAM_L, 1, 0, DDL_PROXY_SYS_PARAM_S},
    {VERSION_L, 0, 0, VERSION_S},
    {0, 0, 0, 0}
  };

  memset (&arguments, 0, sizeof (DDL_CLIENT_ARGUMENT));
  utility_make_getopt_optstring (possible_arguments, arguments_string);

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, arguments_string, possible_arguments, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case DDL_PROXY_USER_S:
	  if (arguments.user_name != NULL)
	    {
	      free ((void *) arguments.user_name);
	    }
	  arguments.user_name = strdup (optarg);
	  break;

	case DDL_PROXY_PASSWORD_S:
	  if (arguments.passwd != NULL)
	    {
	      free ((void *) arguments.passwd);
	    }
	  arguments.passwd = strdup (optarg);
	  util_hide_password (optarg);
	  break;

	case DDL_PROXY_OUTPUT_FILE_S:
	  if (arguments.out_file_name != NULL)
	    {
	      free ((void *) arguments.out_file_name);
	    }
	  arguments.out_file_name = strdup (optarg);
	  break;

	case DDL_PROXY_COMMAND_S:
	  if (arguments.command != NULL)
	    {
	      free ((void *) arguments.command);
	    }
	  arguments.command = strdup (optarg);
	  break;

	case DDL_PROXY_REQUEST_S:
	  if (arguments.request != NULL)
	    {
	      free ((void *) arguments.request);
	    }
	  arguments.request = strdup (optarg);
	  break;

	case DDL_PROXY_TRAN_INDEX_S:
	  if (arguments.tran_index != NULL)
	    {
	      free ((void *) arguments.tran_index);
	    }
	  arguments.tran_index = strdup (optarg);
	  break;

	case DDL_PROXY_SYS_PARAM_S:
	  if (arguments.sys_param != NULL)
	    {
	      free ((void *) arguments.sys_param);
	    }
	  arguments.sys_param = strdup (optarg);
	  break;

	case VERSION_S:
	  ddl_proxy_print_msg (MSGCAT_UTIL_GENERIC_VERSION, UTIL_DDL_PROXY_CLIENT, PRODUCT_STRING);
	  goto exit_on_end;

	default:
	  assert (false);
	  // TODO
	  // goto print_usage;
	}
    }

  if (argc - optind == 1)
    {
      arguments.db_name = argv[optind];
    }
  else if (argc > optind)
    {
      ddl_proxy_print_msg (MSGCAT_UTIL_GENERIC_ARGS_OVER, argv[optind + 1]);
      assert (false);
      // TODO
      // goto print_usage;
    }
  else
    {
      ddl_proxy_print_msg (MSGCAT_UTIL_GENERIC_MISS_DBNAME);
      assert (false);
      // TODO
      // goto print_usage;
    }

  error = start_ddl_proxy_client (argv[0], &arguments);

exit_on_end:
  if (arguments.user_name != NULL)
    {
      free ((void *) arguments.user_name);
    }
  if (arguments.passwd != NULL)
    {
      free ((void *) arguments.passwd);
    }
  if (arguments.out_file_name != NULL)
    {
      free ((void *) arguments.out_file_name);
    }
  if (arguments.command != NULL)
    {
      free ((void *) arguments.command);
    }
  if (arguments.request != NULL)
    {
      free ((void *) arguments.request);
    }
  if (arguments.tran_index != NULL)
    {
      free ((void *) arguments.tran_index);
    }
  if (arguments.sys_param != NULL)
    {
      free ((void *) arguments.sys_param);
    }

  return error;
}
