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

#include "util_support.h"
#include "cubrid_getopt.h"
#include "utility.h"
#include "csql.h"
#include "db.h"
#include "dbi.h"

static void
utility_print (int message_num, ...)
{
  typedef const char *(*GET_MESSAGE) (int message_index);

  DSO_HANDLE util_sa_library;
  DSO_HANDLE symbol;
  GET_MESSAGE get_message_fn;

  utility_load_library (&util_sa_library, LIB_UTIL_SA_NAME);
  if (util_sa_library == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  utility_load_symbol (util_sa_library, &symbol, UTILITY_GENERIC_MSG_FUNC_NAME);
  if (symbol == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }

  get_message_fn = (GET_MESSAGE) symbol;

  {
    va_list ap;

    va_start (ap, message_num);
    vfprintf (stderr, get_message_fn (message_num), ap);
    va_end (ap);
  }
}

static int start_ddl_proxy_client(const char *program_name, CSQL_ARGUMENT *args)
{
  DB_SESSION *session = NULL;
  int rc = NO_ERROR;

  rc = db_restart_ex (program_name, args->db_name, args->user_name, args->passwd, NULL, DB_CLIENT_TYPE_ADMIN_CSQL);

  if (rc != NO_ERROR)
    {
      return rc;
    }

  if (args->command != NULL)
    {
      int total_stmts, stmt_id, i, num_of_rows;
      DB_QUERY_RESULT *result = NULL;

      session = db_open_buffer ((const char *) args->command);
      if (session == NULL)
	{
          rc = er_errid ();
	  goto error;
	}

        if (db_get_errors (session) || er_errid () != NO_ERROR)
          {
            rc = er_errid ();
            goto error;
          }

        total_stmts = db_statement_count (session);
        for (i = 0; i < total_stmts; i++)
          {
            stmt_id = db_compile_statement (session);
            if (stmt_id < 0)
              {
                rc = er_errid ();
                db_abort_transaction ();
                goto error;
              }

            if (stmt_id == 0)
              goto error;

            num_of_rows = db_execute_statement (session, stmt_id, &result);
            if (num_of_rows < 0)
              {
                rc = er_errid ();
                db_abort_transaction ();
                goto error;
              }

            if (result != NULL)
              {
                db_query_end (result);
                result = NULL;
              }
            else
              {
                db_free_query (session);
              }

            db_drop_statement (session, stmt_id);
          }
    }

error:
  if (session != NULL)
    {
      db_close_session (session);
    }
  db_commit_transaction ();
  db_shutdown ();

  return rc;
}

int
main (int argc, char *argv[])
{
  char arguments_string[64];
  int error = 0;
  CSQL_ARGUMENT arguments;

  GETOPT_LONG possible_arguments[] = {
    {CSQL_USER_L, 1, 0, CSQL_USER_S},
    {CSQL_PASSWORD_L, 1, 0, CSQL_PASSWORD_S},
    {CSQL_OUTPUT_FILE_L, 1, 0, CSQL_OUTPUT_FILE_S},
    {CSQL_COMMAND_L, 1, 0, CSQL_COMMAND_S},
    {VERSION_L, 0, 0, VERSION_S},
    {0, 0, 0, 0}
  };

  memset (&arguments, 0, sizeof (CSQL_ARGUMENT));
  arguments.auto_commit = true;
  arguments.single_line_execution = true;
  arguments.string_width = 0;
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
	case CSQL_USER_S:
	  if (arguments.user_name != NULL)
	    {
	      free ((void *) arguments.user_name);
	    }
	  arguments.user_name = strdup (optarg);
	  break;

	case CSQL_PASSWORD_S:
	  if (arguments.passwd != NULL)
	    {
	      free ((void *) arguments.passwd);
	    }
	  arguments.passwd = strdup (optarg);
	  util_hide_password (optarg);
	  break;

	case CSQL_OUTPUT_FILE_S:
	  if (arguments.out_file_name != NULL)
	    {
	      free ((void *) arguments.out_file_name);
	    }
	  arguments.out_file_name = strdup (optarg);
	  break;

	case CSQL_COMMAND_S:
	  if (arguments.command != NULL)
	    {
	      free ((void *) arguments.command);
	    }
	  arguments.command = strdup (optarg);
	  break;

	case VERSION_S:
	  utility_print (MSGCAT_UTIL_GENERIC_VERSION, UTIL_DDL_PROXY_CLIENT, PRODUCT_STRING);
	  goto exit_on_end;

	default:
	  assert(false);
	}
    }

  if (argc - optind == 1)
    {
      arguments.db_name = argv[optind];
    }
  else if (argc > optind)
    {
      utility_print (MSGCAT_UTIL_GENERIC_ARGS_OVER, argv[optind + 1]);
      assert(false);
    }
  else
    {
      utility_print (MSGCAT_UTIL_GENERIC_MISS_DBNAME);
      assert(false);
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

  return error;
}
