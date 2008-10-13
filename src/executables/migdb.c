/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * load_noopt.c 
 */

#ident "$Id$"

#include "config.h"

#include "utility.h"
#include "dbu_misc.h"
#include "error_code.h"
#include "message_catalog.h"

static UTIL_ARG_MAP ua_Load_Option_Map[] = {
  {OPTION_STRING_TABLE, 0, 0},
  {LOAD_USER_S, ARG_STRING, 0},
  {LOAD_PASSWORD_S, ARG_STRING, 0},
  {LOAD_CHECK_ONLY_S, ARG_BOOLEAN, 0},
  {LOAD_LOAD_ONLY_S, ARG_BOOLEAN, 0},
  {LOAD_ESTIMATED_SIZE_S, ARG_INTEGER, 0},
  {LOAD_VERBOSE_S, ARG_BOOLEAN, 0},
  {LOAD_NO_STATISTICS_S, ARG_BOOLEAN, 0},
  {LOAD_PERIODIC_COMMIT_S, ARG_INTEGER, 0},
  {LOAD_NO_OID_S, ARG_BOOLEAN, 0},
  {LOAD_SCHEMA_FILE_S, ARG_STRING, 0},
  {LOAD_INDEX_FILE_S, ARG_STRING, 0},
  {LOAD_DATA_FILE_S, ARG_STRING, 0},
  {LOAD_ERROR_CONTROL_FILE_S, ARG_STRING, 0},
  {0, 0, 0}
};

static GETOPT_LONG ua_Load_Option[] = {
  {LOAD_USER_L, 1, 0, LOAD_USER_S},
  {LOAD_PASSWORD_L, 1, 0, LOAD_PASSWORD_S},
  {LOAD_CHECK_ONLY_L, 0, 0, LOAD_CHECK_ONLY_S},
  {LOAD_LOAD_ONLY_L, 0, 0, LOAD_LOAD_ONLY_S},
  {LOAD_ESTIMATED_SIZE_L, 1, 0, LOAD_ESTIMATED_SIZE_S},
  {LOAD_VERBOSE_L, 0, 0, LOAD_VERBOSE_S},
  {LOAD_NO_STATISTICS_L, 0, 0, LOAD_NO_STATISTICS_S},
  {LOAD_PERIODIC_COMMIT_L, 1, 0, LOAD_PERIODIC_COMMIT_S},
  {LOAD_NO_OID_L, 0, 0, LOAD_NO_OID_S},
  {LOAD_SCHEMA_FILE_L, 1, 0, LOAD_SCHEMA_FILE_S},
  {LOAD_INDEX_FILE_L, 1, 0, LOAD_INDEX_FILE_S},
  {LOAD_DATA_FILE_L, 1, 0, LOAD_DATA_FILE_S},
  {LOAD_ERROR_CONTROL_FILE_L, 1, 0, LOAD_ERROR_CONTROL_FILE_S},
  {0, 0, 0, 0}
};

static UTIL_MAP utility_Map[] = {
  {LOADDB, SA_ONLY, "load", "loaddb_user",
   ua_Load_Option, ua_Load_Option_Map},
};

int
main (int argc, char *argv[])
{
  int status;
  UTIL_FUNCTION_ARG util_func_arg;
  
  if (utility_initialize () != NO_ERROR)
    {
      return EXIT_FAILURE;
    }

  if (util_parse_argument (&utility_Map[0], argc, argv) != NO_ERROR)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_LOADDB,
				       LOADDB_MSG_USAGE),
	       utility_Map[0].utility_name);
      msgcat_final();
      return EXIT_FAILURE;
    }

  util_func_arg.arg_map = utility_Map[0].arg_map;
  util_func_arg.command_name = utility_Map[0].utility_name;
  util_func_arg.argv0 = argv[0];
  status = loaddb_user (&util_func_arg);
  msgcat_final();
  return status;
}
