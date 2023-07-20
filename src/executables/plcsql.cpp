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
#include "db_value_printer.hpp"
#include "memory_private_allocator.hpp"
#include "string_buffer.hpp"
#include "optimizer.h" /* qo_get_optimization_param, qo_set_optimization_param */

#include "byte_order.h"
#include "db.h"
#include "parser.h"
#include "api_compat.h"
#include "method_query_handler.hpp"

#define CS_MODE
#include "network_interface_cl.h"

#include "porting.h"
#include "utility.h"

namespace fs = std::filesystem;

// input
static std::string input_string;
static bool verbose = false;

// output
static PLCSQL_COMPILE_INFO compile_info;

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
  std::string query;

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
	       , in_file.c_str());
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
	   "-q, --query                   test and get parameterized query string for static sql\n"
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

static std::vector <std::string> queries;

static bool dbvalue_to_str (DB_VALUE *value, std::string &out)
{
  bool valid = true;
  size_t BUFFER_SIZE = 1024;

  switch (value->domain.general_info.type)
    {
    case DB_TYPE_INTEGER:
      out += "$int";
      break;
    case DB_TYPE_BIGINT:
      out += "$bigint";
      break;
    case DB_TYPE_FLOAT:
      out += "$float";
      break;
    case DB_TYPE_DOUBLE:
      out += "$double";
      break;
    case DB_TYPE_VARNCHAR:
      value->domain.general_info.type = DB_TYPE_VARCHAR; // force casting
    // break through
    case DB_TYPE_VARCHAR:
      out += "$varchar";
      BUFFER_SIZE = db_get_string_size (value) > 0 ? db_get_string_size (value) : 1024;
      break;
    case DB_TYPE_TIME:
      out += "$time";
      break;
    case DB_TYPE_TIMESTAMP:
      out += "$timestamp";
      break;
    case DB_TYPE_TIMESTAMPTZ:
      out += "$timestamptz";
      break;
    case DB_TYPE_TIMESTAMPLTZ:
      out += "$timestampltz";
      break;
    case DB_TYPE_DATETIME:
      out += "$datetime";
      break;
    case DB_TYPE_DATETIMETZ:
      out += "$datetimetz";
      break;
    case DB_TYPE_DATETIMELTZ:
      out += "$datetimeltz";
      break;
    case DB_TYPE_DATE:
      out += "$date";
      break;
    case DB_TYPE_MONETARY:
      out += "$monetary";
      break;
    case DB_TYPE_SMALLINT:
      out += "$smallint";
      break;
    case DB_TYPE_NUMERIC:
      out += "$numeric";
      break;
    case DB_TYPE_BIT:
      out += "$bit";
      break;
    case DB_TYPE_VARBIT:
      out += "$varbit";
      break;
    case DB_TYPE_NCHAR:
      value->domain.general_info.type = DB_TYPE_CHAR; // force casting
    // break through
    case DB_TYPE_CHAR:
      out += "$char";
      BUFFER_SIZE = db_get_string_size (value) > 0 ? db_get_string_size (value) : 1024;
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
    case DB_TYPE_OID:
    case DB_TYPE_OBJECT:
    case DB_TYPE_NULL:
    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_POINTER:
    case DB_TYPE_ERROR:
    case DB_TYPE_VOBJ:
    case DB_TYPE_DB_VALUE:
    case DB_TYPE_RESULTSET:
    case DB_TYPE_MIDXKEY:
    case DB_TYPE_TABLE:
    case DB_TYPE_ENUMERATION:
    case DB_TYPE_JSON:
    default:
      valid = false;
      out.clear ();
    }

  if (valid)
    {
      string_buffer sb (cubmem::PRIVATE_BLOCK_ALLOCATOR, BUFFER_SIZE);

      db_value_printer printer (sb);
      printer.describe_value (value);

      out.append (", $");
      out.append (sb.release_ptr ());
    }

  return valid;
}

static int
plcsql_test_query (const std::string &query_str)
{
  int error = NO_ERROR;

  cubmethod::error_context error_ctx;
  cubmethod::query_handler *handler = new (std::nothrow) cubmethod::query_handler (error_ctx, 0);

  er_clear ();
  error_ctx.clear ();

  std::string hostvars_string;

  // see handler->prepare_compile ()
  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, 2);
  error = handler->prepare (query_str, cubmethod::PREPARE_TEST_MODE);

  if (error == NO_ERROR && error_ctx.has_error () == false)
    {
      DB_SESSION *db_session = handler->get_db_session ();
      // cubmethod::prepare_info &prepare_info = handler->get_prepare_info ();

      PARSER_CONTEXT *parser = db_get_parser (db_session);
      PT_NODE *stmt = db_get_statement (db_session, 0);

      parser->custom_print |= PT_CONVERT_RANGE;
      std::string query = parser_print_tree (parser, stmt);

      int markers_cnt = parser->host_var_count + parser->auto_param_count;
      DB_MARKER *marker = db_get_input_markers (db_session, 1);

      bool isValid = true;
      while (marker)
	{
	  int idx = marker->info.host_var.index;
	  DB_VALUE *marker_val = NULL;
	  if (idx >= parser->host_var_count)
	    {
	      // auto parameterized
	      marker_val = &db_session->parser->host_variables[idx];
	    }
	  else
	    {
	      marker_val = &db_session->parser->host_variables[idx];
	    }

	  isValid = dbvalue_to_str (marker_val, hostvars_string);
	  if (!isValid)
	    {
	      hostvars_string.clear ();
	      error = ER_FAILED;
	      break;
	    }

	  if (marker->next)
	    {
	      hostvars_string += ",";
	    }
	  else
	    {
	      hostvars_string += ";";
	    }

	  marker = marker->next;
	}

      if (isValid)
	{
	  if (hostvars_string.empty ())
	    {
	      // hostvars_string = "// " + query_str + "\n" + query;
	      hostvars_string = query + ";";
	    }
	  else
	    {
	      std::string tmp = hostvars_string + "\n" + query + ";";
	      hostvars_string = tmp;
	      // hostvars_string = "// " + query_str + "\n" + hostvars_string + "\n" + query;
	    }
	}
    }

  if (hostvars_string.empty ())
    {
      std::string err_template = "/* [error]" + error_ctx.get_error_msg () + "*/\n";
      hostvars_string = err_template + query_str;
      error = ER_FAILED;
    }

  fprintf (stdout, "%s", hostvars_string.c_str ());

  delete handler;

  return error;
}

/*
 * plcsql_read_file() - read a file into command editor
 *   return: none
 *   file_name(in): input file name
 */
static int
plcsql_read_file (const std::string &file_name)
{
  try
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
    }
  catch (fs::filesystem_error &e)
    {
      PLCSQL_LOG (e.what ());
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
    {"input-file", 0, 0, 'i'},
    {"query", 0, 0, 'q'},
    {"verbose", 0, 0, 'v'},
    {0, 0, 0, 0}
  };

  while (1)
    {
      int option_index = 0;
      int option_key;

      option_key = getopt_long (argc, argv, "u:p:i:q:vh", opts, &option_index);
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

	case 'q':
	  pl_args->query.assign (optarg ? optarg : "");
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
    if (er_init (NULL, ER_NEVER_EXIT) != NO_ERROR)
      {
	PLCSQL_LOG_FORCE ("Initializing error manager is failed");
	goto exit_on_end;
      }

    if (parse_options (argc, argv, &plcsql_arg) != NO_ERROR)
      {
	goto print_usage;
      }

    if (db_restart_ex ("PLCSQL Helper", DB_PLCSQL_AS_ARGS (plcsql_arg)) != NO_ERROR)
      {
	PLCSQL_LOG_FORCE ("Connecting with cub_server is failed");
	goto exit_on_end;
      }

    if (!plcsql_arg.query.empty ())
      {
	// query testing mode
	error = plcsql_test_query (plcsql_arg.query) == NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
	goto exit_on_end;
      }
    else
      {
	// DDL (CREATE PROCEDURE, FUNCTION) testing mode


	PLCSQL_LOG ("[Arguments]");
	plcsql_arg.print ();



	if (plcsql_read_file (plcsql_arg.in_file) != NO_ERROR)
	  {
	    PLCSQL_LOG_FORCE ("Reading PL/CSQL program is failed");
	    goto exit_on_end;
	  }

	PLCSQL_LOG ("[Input File]");
	PLCSQL_LOG (input_string);

	PLCSQL_LOG ("[Compile PL/CSQL]");
	/* Call network interface API to send a input file (PL/CSQL program) */
	if (plcsql_transfer_file (input_string, verbose, compile_info) != NO_ERROR)
	  {
	    PLCSQL_LOG ("Transferring or Translating PL/CSQL program is failed");
	    goto exit_on_end;
	  }

	if (compile_info.err_code == 0)
	  {
	    PLCSQL_LOG ("[Output File]");
	    PLCSQL_LOG (compile_info.translated_code);

	    PLCSQL_LOG ("[Stored Routine Definition]");
	    PLCSQL_LOG (compile_info.register_stmt);
	  }
	else
	  {
	    PLCSQL_LOG ("[Compile Error Info]");
	    PLCSQL_LOG ("error at : " << compile_info.err_line);
	    PLCSQL_LOG ("error message : " << compile_info.err_msg);
	    goto exit_on_end;
	  }

	// Execute SQL
	{
	  const std::string &sql = compile_info.register_stmt;
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
