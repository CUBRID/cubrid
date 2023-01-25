/*
 *
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
 * plcsql.cpp - utility for plcsql registration
 *
 */

#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cwctype>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <iterator>
#include <filesystem>

#include "dbi.h"
#include "db_client_type.hpp"
#include "error_manager.h"
#include "network_interface_cl.h"
#include "porting.h"
#include "utility.h"
#include "util_support.h"

namespace fs = std::filesystem;

static std::string input_string;
static std::string output_string;
static std::string sql;
static bool verbose = false;

#define DB_PLCSQL_AS_ARGS(arg) \
  arg.db_name.data(), arg.user_name.data(), arg.passwd.data(), NULL, DB_CLIENT_TYPE_PLCSQL_HELPER

#define PLCSQL_LOG_FORCE(str) \
  do { \
      std::cout << str << std::endl; \
  } while(0)

#define PLCSQL_LOG(str) \
  do { \
    if (verbose) { \
      std::cout << str << std::endl; \
    } \
  } while(0)

struct plcsql_argument
{
  std::string db_name;
  std::string user_name;
  std::string passwd;
  std::string in_file;

  plcsql_argument ()
  {
    //
  }

  void print (void);
};

void
plcsql_argument::print (void)
{
  if (verbose)
    {
      fprintf (stdout, "db name = %s, user name = %s, password = %s, input file = %s\n"
	       , db_name.c_str()
	       , user_name.c_str()
	       , passwd.c_str()
	       , fs::canonical (fs::path (in_file)).c_str());
    }
}

static void
utility_plcsql_usage (void)
{
  fprintf (stdout,
	   "Usage : plcsql_helper [OPTION] database-name\n" "\n" "valid options:\n"
	   "-u, --user=ID                 user ID for database access;\n"
	   "-p, --password=PASS           user password; default: none\n"
	   "-i, --input-file              path for input PL/CSQL file\n"
	   "-v, --verbose                 verbose mode to print logs\n"
	   "-h, --help                    show usage\n");
}

static void
utility_plcsql_print (int message_num, ...)
{
  va_list ap;

  va_start (ap, message_num);
  vfprintf (stderr, utility_get_generic_message (message_num), ap);
  va_end (ap);
}

/*
 * plcsql_read_file() - read a file into command editor
 *   return: none
 *   file_name(in): input file name
 */
static int
plcsql_read_file (const std::string &file_name)
{
  fs::path src_path = fs::path (file_name);
  if (fs::exists (src_path) == true)
    {
      std::ifstream infile (fs::canonical (src_path), std::ios_base::binary);

      if (infile)
	{
	  std::vector<char> buf (
		  (std::istreambuf_iterator<char> (infile)),
		  (std::istreambuf_iterator<char>()));

	  input_string.assign (buf.data(), buf.size ());
	  infile.close ();
	  return NO_ERROR;
	}
      else
	{
	  PLCSQL_LOG ("RR_OS_ERROR");
	}
    }
  return ER_FAILED;
}

int
parse_options (int argc, char *argv[], plcsql_argument *pl_args)
{
  char option_string[64];
  GETOPT_LONG opts[] =
  {
    {"user", 1, 0, 'u'},
    {"password", 1, 0, 'p'},
    {"input-file", 1, 0, 'i'},
    {"verbose", 0, 0, 'v'},
    {0, 0, 0, 0}
  };

  utility_make_getopt_optstring (opts, option_string);

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "u:p:i:vh", opts, &option_index);
      if (option_key == -1)
	{
	  break;
	}

      switch (option_key)
	{
	case 'u':
	  pl_args->user_name.assign (optarg ? optarg : "");
	  break;

	case 'p':
	  pl_args->passwd.assign (optarg ? optarg : "");
	  break;

	case 'i':
	  pl_args->in_file.assign (optarg ? optarg : "");
	  break;

	case 'v':
	  verbose = true;
	  break;

	default:
	  return ER_FAILED;
	}
    }

  if (argc - optind == 1)
    {
      pl_args->db_name = argv[optind];
    }
  else
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
main (int argc, char *argv[])
{
  int error = EXIT_FAILURE;
  plcsql_argument plcsql_arg;
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result = NULL;

  {
    if (parse_options (argc, argv, &plcsql_arg) != NO_ERROR)
      {
	goto print_usage;
      }

    PLCSQL_LOG ("[Arguments]");
    plcsql_arg.print ();

    if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
      {
	PLCSQL_LOG_FORCE ("Initializing error manager is failed");
	goto exit_on_end;
      }

    if (db_restart_ex ("PLCSQL Helper", DB_PLCSQL_AS_ARGS (plcsql_arg)) != NO_ERROR)
      {
	PLCSQL_LOG_FORCE ("Connecting with cub_server is failed");
	goto exit_on_end;
      }

    if (plcsql_read_file (plcsql_arg.in_file) != NO_ERROR)
      {
	PLCSQL_LOG_FORCE ("Reading PL/CSQL program is failed");
	goto exit_on_end;
      }

    PLCSQL_LOG ("[Input File]");
    PLCSQL_LOG (input_string);

    PLCSQL_LOG ("[Compile PL/CSQL]");
    /* Call network interface API to send a input file (PL/CSQL program) */
    if (plcsql_transfer_file (input_string, verbose, output_string, sql) != NO_ERROR)
      {
	PLCSQL_LOG ("Transferring PL/CSQL program is failed");
	goto exit_on_end;
      }

    PLCSQL_LOG ("[Output File]");
    PLCSQL_LOG (output_string);

    PLCSQL_LOG ("[Stored Routine Definition]");
    PLCSQL_LOG (sql);

    // Execute SQL
    {
      if (sql.empty ())
	{
	  PLCSQL_LOG_FORCE ("Invalid SQL string");
	  goto exit_on_end;
	}

      session = db_open_buffer (sql.c_str ());
      if (!session)
	{
	  PLCSQL_LOG_FORCE ("Parsing SQL is failed");
	  goto exit_on_end;
	}

      int stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  PLCSQL_LOG_FORCE ("Compiling SQL is failed");
	  goto exit_on_end;
	}

      int db_error = db_execute_statement (session, stmt_id, &result);
      if (db_error < 0)
	{
	  PLCSQL_LOG_FORCE ("Executing SQL is failed");
	  goto exit_on_end;
	}
    }

    error = NO_ERROR;
    goto exit_on_end;
  }

print_usage:
  utility_plcsql_usage ();
  error = EXIT_FAILURE;
  /* fall through */

exit_on_end:
  if (result != NULL)
    {
      db_query_end (result);
    }

  if (session != NULL)
    {
      if (error == NO_ERROR)
	{
	  PLCSQL_LOG_FORCE ("Registering PL/CSQL procedure/function has been completed successfully");
	  (void) db_commit_transaction ();
	}
      else
	{
	  PLCSQL_LOG_FORCE ("Registering PL/CSQL procedure/function has been failed");
	  (void) db_abort_transaction ();
	}
      db_close_session (session);
    }

  return error;
}
