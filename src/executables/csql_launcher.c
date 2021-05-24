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
 * csql_launcher.c : csql invocation program
 */

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>

#include "csql.h"
#include "message_catalog.h"
#include "environment_variable.h"
#include "intl_support.h"
#include "utility.h"
#include "util_support.h"
#include "cubrid_getopt.h"

typedef const char *(*CSQL_GET_MESSAGE) (int message_index);
typedef int (*CSQL) (const char *argv0, CSQL_ARGUMENT * csql_arg);

static void utility_csql_usage (void);
static void utility_csql_print (void);

/*
 * utility_csql_usage() - display csql usage
 */
static void
utility_csql_usage (void)
{
  DSO_HANDLE util_sa_library;
  CSQL_GET_MESSAGE csql_get_message;
  const char *message;

  utility_load_library (&util_sa_library, LIB_UTIL_SA_NAME);
  if (util_sa_library == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  utility_load_symbol (util_sa_library, (DSO_HANDLE *) (&csql_get_message), "csql_get_message");
  if (csql_get_message == NULL)
    {
      utility_load_print_error (stderr);
      return;
    }
  message = (*csql_get_message) (CSQL_MSG_USAGE);
  fprintf (stderr, message, PRODUCT_STRING, UTIL_CSQL_NAME);
}

/*
 * utility_csql_print - display a version of this utility
 *
 * return:
 *
 * NOTE:
 */
static void
utility_csql_print (int message_num, ...)
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

/*
 * main() - csql main module.
 *   return: no return if no error,
 *           EXIT_FAILURE otherwise.
 */
int
main (int argc, char *argv[])
{
  char option_string[64];
  int error = 0;
  CSQL_ARGUMENT csql_arg;
  DSO_HANDLE util_library;
  CSQL csql;
  int check_output_style = 0;
  bool explicit_single_line = false;

  GETOPT_LONG csql_option[] = {
    {CSQL_SA_MODE_L, 0, 0, CSQL_SA_MODE_S},
    {CSQL_CS_MODE_L, 0, 0, CSQL_CS_MODE_S},
    {CSQL_USER_L, 1, 0, CSQL_USER_S},
    {CSQL_PASSWORD_L, 1, 0, CSQL_PASSWORD_S},
    {CSQL_ERROR_CONTINUE_L, 0, 0, CSQL_ERROR_CONTINUE_S},
    {CSQL_INPUT_FILE_L, 1, 0, CSQL_INPUT_FILE_S},
    {CSQL_OUTPUT_FILE_L, 1, 0, CSQL_OUTPUT_FILE_S},
    {CSQL_SINGLE_LINE_L, 0, 0, CSQL_SINGLE_LINE_S},
    {CSQL_COMMAND_L, 1, 0, CSQL_COMMAND_S},
    {CSQL_LINE_OUTPUT_L, 0, 0, CSQL_LINE_OUTPUT_S},
    {CSQL_READ_ONLY_L, 0, 0, CSQL_READ_ONLY_S},
    {CSQL_NO_AUTO_COMMIT_L, 0, 0, CSQL_NO_AUTO_COMMIT_S},
    {CSQL_NO_PAGER_L, 0, 0, CSQL_NO_PAGER_S},
    {CSQL_NO_SINGLE_LINE_L, 0, 0, CSQL_NO_SINGLE_LINE_S},
    {CSQL_SYSADM_L, 0, 0, CSQL_SYSADM_S},
    {CSQL_WRITE_ON_STANDBY_L, 0, 0, CSQL_WRITE_ON_STANDBY_S},
    {CSQL_STRING_WIDTH_L, 1, 0, CSQL_STRING_WIDTH_S},
    {CSQL_NO_TRIGGER_ACTION_L, 0, 0, CSQL_NO_TRIGGER_ACTION_S},
    {CSQL_PLAIN_OUTPUT_L, 0, 0, CSQL_PLAIN_OUTPUT_S},
    {CSQL_SKIP_COL_NAMES_L, 0, 0, CSQL_SKIP_COL_NAMES_S},
    {CSQL_SKIP_VACUUM_L, 0, 0, CSQL_SKIP_VACUUM_S},
    {CSQL_QUERY_OUTPUT_L, 0, 0, CSQL_QUERY_OUTPUT_S},
    {CSQL_QUERY_COLUMN_DELIMITER_L, 1, 0, CSQL_QUERY_COLUMN_DELIMITER_S},
    {CSQL_QUERY_COLUMN_ENCLOSURE_L, 1, 0, CSQL_QUERY_COLUMN_ENCLOSURE_S},
    {CSQL_LOADDB_OUTPUT_L, 0, 0, CSQL_LOADDB_OUTPUT_S},
    {VERSION_L, 0, 0, VERSION_S},
    {0, 0, 0, 0}
  };

  memset (&csql_arg, 0, sizeof (CSQL_ARGUMENT));
  csql_arg.auto_commit = true;
  csql_arg.single_line_execution = true;
  csql_arg.string_width = 0;
  csql_arg.trigger_action_flag = true;
  csql_arg.plain_output = false;
  csql_arg.query_output = false;
  csql_arg.loaddb_output = false;
  csql_arg.column_delimiter = -1;
  csql_arg.column_enclosure = -1;
  utility_make_getopt_optstring (csql_option, option_string);

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, option_string, csql_option, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case CSQL_SA_MODE_S:
	  csql_arg.sa_mode = true;
	  break;

	case CSQL_CS_MODE_S:
	  csql_arg.cs_mode = true;
	  break;

	case CSQL_USER_S:
	  if (csql_arg.user_name != NULL)
	    {
	      free ((void *) csql_arg.user_name);
	    }
	  csql_arg.user_name = strdup (optarg);
	  break;

	case CSQL_PASSWORD_S:
	  if (csql_arg.passwd != NULL)
	    {
	      free ((void *) csql_arg.passwd);
	    }
	  csql_arg.passwd = strdup (optarg);
	  util_hide_password (optarg);
	  break;

	case CSQL_ERROR_CONTINUE_S:
	  csql_arg.continue_on_error = true;
	  break;

	case CSQL_INPUT_FILE_S:
	  if (csql_arg.in_file_name != NULL)
	    {
	      free ((void *) csql_arg.in_file_name);
	    }
	  csql_arg.in_file_name = strdup (optarg);
	  break;

	case CSQL_OUTPUT_FILE_S:
	  if (csql_arg.out_file_name != NULL)
	    {
	      free ((void *) csql_arg.out_file_name);
	    }
	  csql_arg.out_file_name = strdup (optarg);
	  break;

	case CSQL_SINGLE_LINE_S:
	  explicit_single_line = true;
	  break;

	case CSQL_NO_SINGLE_LINE_S:
	  csql_arg.single_line_execution = false;
	  break;

	case CSQL_COMMAND_S:
	  if (csql_arg.command != NULL)
	    {
	      free ((void *) csql_arg.command);
	    }
	  csql_arg.command = strdup (optarg);
	  break;

	case CSQL_LINE_OUTPUT_S:
	  csql_arg.line_output = true;
	  break;

	case CSQL_READ_ONLY_S:
	  csql_arg.read_only = true;
	  break;

	case CSQL_NO_AUTO_COMMIT_S:
	  csql_arg.auto_commit = false;
	  break;

	case CSQL_NO_PAGER_S:
	  csql_arg.nopager = true;
	  break;

	case CSQL_SYSADM_S:
	  csql_arg.sysadm = true;
	  break;

	case CSQL_WRITE_ON_STANDBY_S:
	  csql_arg.write_on_standby = true;
	  break;

	case CSQL_STRING_WIDTH_S:
	  {
	    int string_width = 0, result;

	    result = parse_int (&string_width, optarg, 10);

	    if (result != 0 || string_width < 0)
	      {
		goto print_usage;
	      }

	    csql_arg.string_width = string_width;
	  }
	  break;

	case CSQL_NO_TRIGGER_ACTION_S:
	  csql_arg.trigger_action_flag = false;
	  break;

	case CSQL_PLAIN_OUTPUT_S:
	  csql_arg.plain_output = true;
	  break;

	case CSQL_SKIP_COL_NAMES_S:
	  csql_arg.skip_column_names = true;
	  break;

	case CSQL_SKIP_VACUUM_S:
	  csql_arg.skip_vacuum = true;
	  break;

	case CSQL_QUERY_OUTPUT_S:
	  csql_arg.query_output = true;
	  break;

	case CSQL_QUERY_COLUMN_DELIMITER_S:
	  {
	    int len = strlen (optarg);

	    if (len == 1)
	      {
		csql_arg.column_delimiter = optarg[0];
	      }
	    else if (len >= 2 && optarg[0] == '\\')
	      {
		if (optarg[1] == 't')
		  {
		    csql_arg.column_delimiter = '\t';
		  }
		else if (optarg[1] == 'n')
		  {
		    csql_arg.column_delimiter = '\n';
		  }
		else
		  {
		    csql_arg.column_delimiter = optarg[1];
		  }
	      }
	    else
	      {
		csql_arg.column_delimiter = ',';
	      }
	  }
	  break;

	case CSQL_QUERY_COLUMN_ENCLOSURE_S:
	  if (strlen (optarg) >= 1)
	    {
	      csql_arg.column_enclosure = optarg[0];
	    }
	  else
	    {
	      csql_arg.column_enclosure = '\'';
	    }
	  break;

	case CSQL_LOADDB_OUTPUT_S:
	  csql_arg.loaddb_output = true;
	  break;

	case VERSION_S:
	  utility_csql_print (MSGCAT_UTIL_GENERIC_VERSION, UTIL_CSQL_NAME, PRODUCT_STRING);
	  goto exit_on_end;

	default:
	  goto print_usage;
	}
    }

  if (argc - optind == 1)
    {
      csql_arg.db_name = argv[optind];
    }
  else if (argc > optind)
    {
      utility_csql_print (MSGCAT_UTIL_GENERIC_ARGS_OVER, argv[optind + 1]);
      goto print_usage;
    }
  else
    {
      utility_csql_print (MSGCAT_UTIL_GENERIC_MISS_DBNAME);
      goto print_usage;
    }

  if ((csql_arg.command == NULL && csql_arg.in_file_name == NULL) || csql_arg.line_output == true)
    {
      csql_arg.skip_column_names = false;
      csql_arg.plain_output = false;
      csql_arg.query_output = false;
      csql_arg.loaddb_output = false;
    }

  if (csql_arg.plain_output == true)
    {
      check_output_style++;
    }

  if (csql_arg.query_output == true)
    {
      if (csql_arg.column_delimiter == -1)
	{
	  csql_arg.column_delimiter = ',';
	}

      if (csql_arg.column_enclosure == -1)
	{
	  csql_arg.column_enclosure = '\'';
	}
      check_output_style++;
    }
  else if (csql_arg.column_delimiter != -1 || csql_arg.column_enclosure != -1)
    {
      /* delimiter and enclosure can only use with query_output option */
      goto print_usage;
    }

  if (csql_arg.loaddb_output == true)
    {
      csql_arg.column_delimiter = ' ';
      csql_arg.column_enclosure = '\'';
      check_output_style++;
    }

  if (check_output_style > 1)
    {
      /* can't use -p, -q, and --loaddb-output together */
      goto print_usage;
    }

  if (csql_arg.sysadm && (csql_arg.user_name == NULL || strcasecmp (csql_arg.user_name, "DBA")))
    {
      /* sysadm is allowed only to DBA */
      goto print_usage;
    }

  if (csql_arg.sysadm == false && csql_arg.write_on_standby == true)
    {
      /* write_on_standby must come with sysadm */
      goto print_usage;
    }

  if (csql_arg.sa_mode && (csql_arg.cs_mode || csql_arg.write_on_standby))
    {
      /* Don't allow both at once. */
      goto print_usage;
    }
  else if (!csql_arg.sa_mode && csql_arg.skip_vacuum)
    {
      /* Don't allow to skip vacuum on CS mode */
      goto print_usage;
    }
  else if (explicit_single_line && csql_arg.single_line_execution == false)
    {
      /* Don't allow both at once. */
      goto print_usage;
    }
  else if (csql_arg.sa_mode)
    {
      utility_load_library (&util_library, LIB_UTIL_SA_NAME);
    }
  else
    {
      utility_load_library (&util_library, LIB_UTIL_CS_NAME);
    }

  if (util_library == NULL)
    {
      utility_load_print_error (stderr);
      goto exit_on_error;
    }

  utility_load_symbol (util_library, (DSO_HANDLE *) (&csql), "csql");
  if (csql == NULL)
    {
      utility_load_print_error (stderr);
      goto exit_on_error;
    }

  error = (*csql) (argv[0], &csql_arg);

exit_on_end:
  if (csql_arg.user_name != NULL)
    {
      free ((void *) csql_arg.user_name);
    }
  if (csql_arg.passwd != NULL)
    {
      free ((void *) csql_arg.passwd);
    }
  if (csql_arg.in_file_name != NULL)
    {
      free ((void *) csql_arg.in_file_name);
    }
  if (csql_arg.out_file_name != NULL)
    {
      free ((void *) csql_arg.out_file_name);
    }
  if (csql_arg.command != NULL)
    {
      free ((void *) csql_arg.command);
    }

  return error;

print_usage:
  utility_csql_usage ();
  error = EXIT_FAILURE;
  goto exit_on_end;

exit_on_error:
  error = ER_GENERIC_ERROR;
  goto exit_on_end;
}
