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
 * util_service.c - a front end of service utilities
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#if !defined(WINDOWS)
#include <sys/wait.h>
#endif
#if defined(WINDOWS)
#include <io.h>
#endif
#include "porting.h"
#include "utility.h"
#include "error_code.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "connection_cl.h"
#include "util_func.h"
#include "util_support.h"

#if defined(WINDOWS)
#include "wintcp.h"
#endif
#include "environment_variable.h"
#include "release_string.h"
#include "dynamic_array.h"
#include "heartbeat.h"

#include <string>

#if defined(WINDOWS)
typedef int pid_t;
#endif

typedef enum
{
  SERVICE = 0,
  SERVER = 1,
  BROKER = 2,
  MANAGER = 3,
  HEARTBEAT = 4,
  UTIL_HELP = 6,
  UTIL_VERSION = 7,
  ADMIN = 8,
  JAVASP_UTIL = 20,
  GATEWAY = 21
} UTIL_SERVICE_INDEX_E;

typedef enum
{
  START,
  STOP,
  RESTART,
  STATUS,
  DEREGISTER,
  LIST,
  RELOAD,
  ON,
  OFF,
  ACCESS_CONTROL,
  RESET,
  INFO,
  SC_COPYLOGDB,
  SC_APPLYLOGDB,
  GET_SHARID,
  TEST,
  REPLICATION
} UTIL_SERVICE_COMMAND_E;

typedef enum
{
  SERVICE_START_SERVER,
  SERVICE_START_BROKER,
  SERVICE_START_MANAGER,
  SERVER_START_LIST,
  SERVICE_START_HEARTBEAT,
  SERVICE_START_GATEWAY
} UTIL_SERVICE_PROPERTY_E;

typedef enum
{
  ALL_SERVICES_RUNNING,
  ALL_SERVICES_STOPPED
} UTIL_ALL_SERVICES_STATUS;

typedef enum
{
  MANAGER_SERVER_RUNNING = 0,
  MANAGER_SERVER_STOPPED,
  MANAGER_SERVER_STATUS_ERROR
} UTIL_MANAGER_SERVER_STATUS_E;

typedef enum
{
  JAVASP_SERVER_RUNNING = 0,
  JAVASP_SERVER_STOPPED,
  JAVASP_SERVER_STATUS_ERROR
} UTIL_JAVASP_SERVER_STATUS_E;

typedef struct
{
  int option_type;
  const char *option_name;
  int option_mask;
} UTIL_SERVICE_OPTION_MAP_T;

typedef struct
{
  int property_index;
  const char *property_value;
} UTIL_SERVICE_PROPERTY_T;

#define UTIL_TYPE_SERVICE       "service"
#define UTIL_TYPE_SERVER        "server"
#define UTIL_TYPE_BROKER        "broker"
#define UTIL_TYPE_MANAGER       "manager"
#define UTIL_TYPE_HEARTBEAT     "heartbeat"
#define UTIL_TYPE_HB_SHORT      "hb"
#define UTIL_TYPE_JAVASP        "javasp"
#define UTIL_TYPE_GATEWAY       "gateway"

static UTIL_SERVICE_OPTION_MAP_T us_Service_map[] = {
  {SERVICE, UTIL_TYPE_SERVICE, MASK_SERVICE},
  {SERVER, UTIL_TYPE_SERVER, MASK_SERVER},
  {BROKER, UTIL_TYPE_BROKER, MASK_BROKER},
  {MANAGER, UTIL_TYPE_MANAGER, MASK_MANAGER},
  {HEARTBEAT, UTIL_TYPE_HEARTBEAT, MASK_HEARTBEAT},
  {HEARTBEAT, UTIL_TYPE_HB_SHORT, MASK_HEARTBEAT},
  {JAVASP_UTIL, UTIL_TYPE_JAVASP, MASK_JAVASP},
  {GATEWAY, UTIL_TYPE_GATEWAY, MASK_GATEWAY},
  {UTIL_HELP, "--help", MASK_ALL},
  {UTIL_VERSION, "--version", MASK_ALL},
  {ADMIN, UTIL_OPTION_CREATEDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_RENAMEDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_COPYDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_DELETEDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_BACKUPDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_RESTOREDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_ADDVOLDB, MASK_ADMIN},
#if 0
  {ADMIN, UTIL_OPTION_DELVOLDB, MASK_ADMIN},
#endif /* ` */
  {ADMIN, UTIL_OPTION_SPACEDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_LOCKDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_KILLTRAN, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_OPTIMIZEDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_INSTALLDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_DIAGDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_PATCHDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_CHECKDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_ALTERDBHOST, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_PLANDUMP, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_ESTIMATE_DATA, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_ESTIMATE_INDEX, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_LOADDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_UNLOADDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_COMPACTDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_PARAMDUMP, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_STATDUMP, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_CHANGEMODE, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_APPLYINFO, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_GENERATE_LOCALE, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_DUMP_LOCALE, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_TRANLIST, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_GEN_TZ, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_DUMP_TZ, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_SYNCCOLLDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_RESTORESLAVE, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_VACUUMDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_CHECKSUMDB, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_TDE, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_FLASHBACK, MASK_ADMIN},
  {ADMIN, UTIL_OPTION_MEMMON, MASK_ADMIN},
  {-1, "", MASK_ADMIN}
};

#define COMMAND_TYPE_START      "start"
#define COMMAND_TYPE_STOP       "stop"
#define COMMAND_TYPE_RESTART    "restart"
#define COMMAND_TYPE_STATUS     "status"
#define COMMAND_TYPE_DEREG      "deregister"
#define COMMAND_TYPE_LIST       "list"
#define COMMAND_TYPE_RELOAD     "reload"
#define COMMAND_TYPE_ON         "on"
#define COMMAND_TYPE_OFF        "off"
#define COMMAND_TYPE_ACL        "acl"
#define COMMAND_TYPE_RESET      "reset"
#define COMMAND_TYPE_INFO       "info"
#define COMMAND_TYPE_COPYLOGDB  "copylogdb"
#define COMMAND_TYPE_APPLYLOGDB "applylogdb"
#define COMMAND_TYPE_GETID      "getid"
#define COMMAND_TYPE_TEST       "test"
#define COMMAND_TYPE_REPLICATION	"replication"
#define COMMAND_TYPE_REPLICATION_SHORT	"repl"

static UTIL_SERVICE_OPTION_MAP_T us_Command_map[] = {
  {START, COMMAND_TYPE_START, MASK_ALL},
  {STOP, COMMAND_TYPE_STOP, MASK_ALL},
  {RESTART, COMMAND_TYPE_RESTART, MASK_SERVICE | MASK_SERVER | MASK_BROKER | MASK_GATEWAY | MASK_JAVASP},
  {STATUS, COMMAND_TYPE_STATUS, MASK_ALL},
  {DEREGISTER, COMMAND_TYPE_DEREG, MASK_HEARTBEAT},
  {LIST, COMMAND_TYPE_LIST, MASK_HEARTBEAT},
  {RELOAD, COMMAND_TYPE_RELOAD, MASK_HEARTBEAT},
  {ON, COMMAND_TYPE_ON, MASK_BROKER | MASK_GATEWAY},
  {OFF, COMMAND_TYPE_OFF, MASK_BROKER | MASK_GATEWAY},
  {ACCESS_CONTROL, COMMAND_TYPE_ACL, MASK_SERVER | MASK_BROKER | MASK_GATEWAY},
  {RESET, COMMAND_TYPE_RESET, MASK_BROKER | MASK_GATEWAY},
  {INFO, COMMAND_TYPE_INFO, MASK_BROKER | MASK_GATEWAY},
  {SC_COPYLOGDB, COMMAND_TYPE_COPYLOGDB, MASK_HEARTBEAT},
  {SC_APPLYLOGDB, COMMAND_TYPE_APPLYLOGDB, MASK_HEARTBEAT},
  {GET_SHARID, COMMAND_TYPE_GETID, MASK_BROKER},
  {TEST, COMMAND_TYPE_TEST, MASK_BROKER},
  {REPLICATION, COMMAND_TYPE_REPLICATION, MASK_HEARTBEAT},
  {REPLICATION, COMMAND_TYPE_REPLICATION_SHORT, MASK_HEARTBEAT},
  {-1, "", MASK_ALL}
};

static UTIL_SERVICE_PROPERTY_T us_Property_map[] = {
  {SERVICE_START_SERVER, NULL},
  {SERVICE_START_BROKER, NULL},
  {SERVICE_START_MANAGER, NULL},
  {SERVER_START_LIST, NULL},
  {SERVICE_START_HEARTBEAT, NULL},
  {SERVICE_START_GATEWAY, NULL},
  {-1, NULL}
};


static const char **Argv;
static int ha_mode_in_common;

static int util_get_service_option_mask (int util_type);
static int util_get_command_option_mask (int command_type);
static void util_service_usage (int util_type);
static void util_service_version (const char *argv0);
static int load_properties (void);
static void finalize_properties (void);
static const char *get_property (int property_type);
static int parse_arg (UTIL_SERVICE_OPTION_MAP_T * option, const char *arg);
static int process_service (int command_type, bool process_window_service);
static int process_server (int command_type, int argc, char **argv, bool show_usage, bool check_ha_mode,
			   bool process_window_service);
static int process_broker (int command_type, int argc, const char **argv, bool process_window_service);
static int process_gateway (int command_type, int argc, const char **argv, bool process_window_service);
static int process_manager (int command_type, bool process_window_service);
static int process_javasp (int command_type, int argc, const char **argv, bool show_usage, bool suppress_message,
			   bool process_window_service, bool ha_mode);
static int process_javasp_start (const char *db_name, bool suppress_message, bool process_window_service);
static int process_javasp_stop (const char *db_name, bool suppress_message, bool process_window_service);
static int process_javasp_status (const char *db_name, bool suppress_message);
static int process_heartbeat (int command_type, int argc, const char **argv);
static int process_heartbeat_start (HA_CONF * ha_conf, int argc, const char **argv);
static int process_heartbeat_stop (HA_CONF * ha_conf, int argc, const char **argv);
static int process_heartbeat_deregister (int argc, const char **argv);
static int process_heartbeat_status (int argc, const char **argv);
static int process_heartbeat_reload (int argc, const char **argv);
static int process_heartbeat_util (HA_CONF * ha_conf, int command_type, int argc, const char **argv);
static int process_heartbeat_replication (HA_CONF * ha_conf, int argc, const char **argv);

static int proc_execute_internal (const char *file, const char *args[], bool wait_child, bool close_output,
				  bool close_err, bool hide_cmd_args, int *pid);
static int proc_execute (const char *file, const char *args[], bool wait_child, bool close_output, bool close_err,
			 int *pid);
static int proc_execute_hide_cmd_args (const char *file, const char *args[], bool wait_child, bool close_output,
				       bool close_err, int *pid);
static void hide_cmd_line_args (char **args);
static int process_master (int command_type);
static void print_message (FILE * output, int message_id, ...);
static void print_result (const char *util_name, int status, int command_type);
static bool is_terminated_process (const int pid);
static char *make_exec_abspath (char *buf, int buf_len, char *cmd);
static const char *command_string (int command_type);
static bool is_server_running (const char *type, const char *server_name, int pid);
static int is_broker_running (void);
static int is_gateway_running (void);
static UTIL_MANAGER_SERVER_STATUS_E is_manager_running (unsigned int sleep_time);
static UTIL_JAVASP_SERVER_STATUS_E is_javasp_running (const char *server_name);
static void get_server_names (const char *type, char **name_buffer);

#if defined(WINDOWS)
static bool is_windows_service_running (unsigned int sleep_time);
#endif
static bool are_all_services_running (unsigned int sleep_time, bool check_win_service);
static bool are_all_services_stopped (unsigned int sleep_time, bool check_win_service);
static bool check_all_services_status (unsigned int sleep_time, UTIL_ALL_SERVICES_STATUS expected_status,
				       bool check_win_service);

static bool ha_make_mem_size (char *mem_size, int size, int value);
static bool ha_is_registered (const char *args, const char *hostname);

#if !defined(WINDOWS)
static int us_hb_copylogdb_start (dynamic_array * out_ap, HA_CONF * ha_conf, const char *db_name, const char *node_name,
				  const char *remote_host);
static int us_hb_copylogdb_stop (HA_CONF * ha_conf, const char *db_name, const char *node_name,
				 const char *remote_host);

static int us_hb_applylogdb_start (dynamic_array * out_ap, HA_CONF * ha_conf, const char *db_name,
				   const char *node_name, const char *remote_host);
static int us_hb_applylogdb_stop (HA_CONF * ha_conf, const char *db_name, const char *node_name,
				  const char *remote_host);

#if defined (ENABLE_UNUSED_FUNCTION)
static int us_hb_utils_start (dynamic_array * pids, HA_CONF * ha_conf, const char *db_name, const char *node_name);
static int us_hb_utils_stop (HA_CONF * ha_conf, const char *db_name, const char *node_name);
#endif /* ENABLE_UNUSED_FUNCTION */

static int us_hb_server_start (HA_CONF * ha_conf, const char *db_name);
static int us_hb_server_stop (HA_CONF * ha_conf, const char *db_name);

static int us_hb_process_start (HA_CONF * ha_conf, const char *db_name, bool check_result);
static int us_hb_process_stop (HA_CONF * ha_conf, const char *db_name);

static int us_hb_process_copylogdb (int command_type, HA_CONF * ha_conf, const char *db_name, const char *node_name,
				    const char *remote_host);
static int us_hb_process_applylogdb (int command_type, HA_CONF * ha_conf, const char *db_name, const char *node_name,
				     const char *remote_host);
#if defined (ENABLE_UNUSED_FUNCTION)
static int us_hb_process_server (int command_type, HA_CONF * ha_conf, const char *db_name);
#endif /* ENABLE_UNUSED_FUNCTION */

static int us_hb_stop_get_options (char *db_name, int db_name_size, char *host_name, int host_name_size,
				   bool * immediate_stop, int argc, const char **argv);
static int us_hb_status_get_options (bool * verbose, char *remote_host_name, int remote_host_name_size, int argc,
				     const char **argv);
static int us_hb_util_get_options (char *db_name, int db_name_size, char *node_name, int node_name_size,
				   char *remote_host_name, int remote_host_name_size, int argc, const char **argv);


#define US_HB_DEREG_WAIT_TIME_IN_SEC	100
#endif /* !WINDOWS */

static char *
make_exec_abspath (char *buf, int buf_len, char *cmd)
{
  buf[0] = '\0';

  (void) envvar_bindir_file (buf, buf_len, cmd);

  return buf;
}

static const char *
command_string (int command_type)
{
  const char *command;

  switch (command_type)
    {
    case START:
      command = PRINT_CMD_START;
      break;
    case STATUS:
      command = PRINT_CMD_STATUS;
      break;
    case DEREGISTER:
      command = PRINT_CMD_DEREG;
      break;
    case LIST:
      command = PRINT_CMD_LIST;
      break;
    case RELOAD:
      command = PRINT_CMD_RELOAD;
      break;
    case ACCESS_CONTROL:
      command = PRINT_CMD_ACL;
      break;
    case SC_COPYLOGDB:
      command = PRINT_CMD_COPYLOGDB;
      break;
    case SC_APPLYLOGDB:
      command = PRINT_CMD_APPLYLOGDB;
      break;
    case TEST:
      command = PRINT_CMD_TEST;
      break;
    case REPLICATION:
      command = PRINT_CMD_REPLICATION;
      break;
    case STOP:
    default:
      command = PRINT_CMD_STOP;
      break;
    }

  return command;
}

static void
print_result (const char *util_name, int status, int command_type)
{
  const char *result;

  if (status != NO_ERROR)
    {
      result = PRINT_RESULT_FAIL;
    }
  else
    {
      result = PRINT_RESULT_SUCCESS;
    }

  print_message (stdout, MSGCAT_UTIL_GENERIC_RESULT, util_name, command_string (command_type), result);
}

/*
 * print_message() -
 *
 * return:
 *
 */
static void
print_message (FILE * output, int message_id, ...)
{
  va_list arg_list;
  const char *format;

  format = utility_get_generic_message (message_id);
  va_start (arg_list, message_id);
  vfprintf (output, format, arg_list);
  va_end (arg_list);
}

/*
 * process_admin() - process admin utility
 *
 * return:
 *
 */
static int
process_admin (int argc, char **argv)
{
  char **copy_argv;
  int status;

  /* execv expects NULL terminated arguments vector */
  copy_argv = (char **) malloc (sizeof (char *) * (argc + 1));
  if (copy_argv == NULL)
    {
      return ER_GENERIC_ERROR;
    }

  memcpy (copy_argv, argv, sizeof (char *) * argc);
  copy_argv[argc] = NULL;

  status = proc_execute_hide_cmd_args (UTIL_ADMIN_NAME, (const char **) copy_argv, true, false, false, NULL);

  free (copy_argv);

  return status;
}

/*
 * util_get_service_option_mask () -
 *
 */
static int
util_get_service_option_mask (int util_type)
{
  int i;

  assert (util_type != ADMIN);

  for (i = 0; us_Service_map[i].option_type != -1; i++)
    {
      if (us_Service_map[i].option_type == util_type)
	{
	  return us_Service_map[i].option_mask;
	}
    }
  return 0;			/* NULL mask */
}

/*
 * util_get_command_option_mask () -
 *
 */
static int
util_get_command_option_mask (int command_type)
{
  int i;

  for (i = 0; us_Command_map[i].option_type != -1; i++)
    {
      if (us_Command_map[i].option_type == command_type)
	{
	  return us_Command_map[i].option_mask;
	}
    }
  return 0;			/* NULL mask */
}

/*
 * main() - a service utility's entry point
 *
 * return:
 *
 * NOTE:
 */
int
main (int argc, char *argv[])
{
  int util_type, command_type;
  int status;
  bool process_window_service = false;
  pid_t pid = getpid ();
  char env_buf[16];

#if defined (DO_NOT_USE_CUBRIDENV)
  char *envval;
  char path[PATH_MAX];

  envval = getenv (envvar_prefix ());
  if (envval != NULL)
    {
      fprintf (stderr,
	       "CAUTION : " "The environment variable $%s is set to %s.\n"
	       "          But, built-in prefix (%s) will be used.\n\n", envvar_prefix (), envval, envvar_root ());
    }

  envval = envvar_get ("DATABASES");
  if (envval != NULL)
    {
      fprintf (stderr,
	       "CAUTION : " "The environment variable $%s_%s is set to %s.\n"
	       "          But, built-in prefix (%s) will be used.\n\n", envvar_prefix (), "DATABASES", envval,
	       envvar_vardir_file (path, PATH_MAX, ""));
    }
#endif

  sprintf (env_buf, "%d", pid);
  envvar_set (UTIL_PID_ENVVAR_NAME, env_buf);

  ER_SAFE_INIT (NULL, ER_NEVER_EXIT);

  Argv = (const char **) argv;
  if (argc == 2)
    {
      if (parse_arg (us_Service_map, (char *) argv[1]) == UTIL_VERSION)
	{
	  util_service_version (argv[0]);
	  return EXIT_SUCCESS;
	}
    }

  /* validate the number of arguments to avoid Klockwork's error message */
  if (argc < 2 || argc > 1024)
    {
      util_type = -1;
      goto usage;
    }

  util_type = parse_arg (us_Service_map, (char *) argv[1]);
  if (util_type == ER_GENERIC_ERROR)
    {
      util_type = parse_arg (us_Service_map, (char *) argv[2]);
      if (util_type == ER_GENERIC_ERROR)
	{
	  print_message (stderr, MSGCAT_UTIL_GENERIC_SERVICE_INVALID_NAME, argv[1]);
	  goto error;
	}
    }

  if (load_properties () != NO_ERROR)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);

      util_log_write_command (argc, argv);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);

      return EXIT_FAILURE;
    }

  ha_mode_in_common = prm_get_integer_value (PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY);

  if (util_type == ADMIN)
    {
      util_log_write_command (argc, argv);
      status = process_admin (argc, argv);
      util_log_write_result (status);

      return status;
    }

  if (util_type == UTIL_HELP)
    {
      util_type = -1;
      goto usage;
    }

  if (argc < 3)
    {
      goto usage;
    }

  command_type = parse_arg (us_Command_map, (char *) argv[2]);
  if (command_type == ER_GENERIC_ERROR)
    {
      command_type = parse_arg (us_Command_map, (char *) argv[1]);
      if (command_type == ER_GENERIC_ERROR)
	{
	  print_message (stderr, MSGCAT_UTIL_GENERIC_SERVICE_INVALID_CMD, argv[2]);
	  goto error;
	}
    }
  else
    {
      int util_mask = util_get_service_option_mask (util_type);
      int command_mask = util_get_command_option_mask (command_type);

      if ((util_mask & command_mask) == 0)
	{
	  print_message (stderr, MSGCAT_UTIL_GENERIC_SERVICE_INVALID_CMD, argv[2]);
	  goto error;
	}
    }

  util_log_write_command (argc, argv);

#if defined(WINDOWS)
  if (css_windows_startup () < 0)
    {
      util_log_write_errstr ("Unable to initialize Winsock.\n");
      goto error;
    }

  process_window_service = true;

  if ((util_type == SERVICE || util_type == BROKER || util_type == GATEWAY || util_type == MANAGER) && (argc > 3)
      && strcmp ((char *) argv[3], "--for-windows-service") == 0)
    {
      process_window_service = false;
    }
  else if ((util_type == SERVER || util_type == BROKER || util_type == GATEWAY || util_type == JAVASP_UTIL)
	   && (argc > 4) && strcmp ((char *) argv[4], "--for-windows-service") == 0)
    {
      process_window_service = false;
    }
#else
  assert (process_window_service == false);
#endif

  switch (util_type)
    {
    case SERVICE:
      {
	status = process_service (command_type, process_window_service);
      }
      break;
    case SERVER:
      status = process_server (command_type, argc - 3, &argv[3], true, true, process_window_service);
      break;
    case BROKER:
      status = process_broker (command_type, argc - 3, (const char **) &argv[3], process_window_service);
      break;
    case MANAGER:
      status = process_manager (command_type, process_window_service);
      break;
    case HEARTBEAT:
#if defined(WINDOWS)
      /* TODO : define message catalog for heartbeat */
      return EXIT_FAILURE;
#else
      status = process_heartbeat (command_type, argc - 3, (const char **) &argv[3]);
#endif /* !WINDOWs */
      break;
    case JAVASP_UTIL:
      status =
	process_javasp (command_type, argc - 3, (const char **) &argv[3], true, false, process_window_service, false);
      break;
    case GATEWAY:
      status = process_gateway (command_type, argc - 3, (const char **) &argv[3], process_window_service);
      break;
    default:
      goto usage;
    }

  util_log_write_result (status);

  return ((status == NO_ERROR) ? EXIT_SUCCESS : EXIT_FAILURE);

usage:
  util_service_usage (util_type);
error:
  finalize_properties ();
#if defined(WINDOWS)
  css_windows_shutdown ();
#endif /* WINDOWS */
  return EXIT_FAILURE;
}

/*
 * util_service_usage - display an usage of this utility
 *
 * return:
 *
 * NOTE:
 */
static void
util_service_usage (int util_type)
{
  char *exec_name;

  if (util_type < 0)
    {
      util_type = 0;
    }
  else
    {
      util_type++;
    }

  exec_name = basename ((char *) Argv[0]);
  print_message (stdout, MSGCAT_UTIL_GENERIC_CUBRID_USAGE + util_type, PRODUCT_STRING, exec_name, exec_name, exec_name);
}

/*
 * util_service_version - display a version of this utility
 *
 * return:
 *
 * NOTE:
 */
static void
util_service_version (const char *argv0)
{
  const char *exec_name;
  char buf[REL_MAX_VERSION_LENGTH];

  exec_name = basename ((char *) argv0);
  rel_copy_version_string (buf, REL_MAX_VERSION_LENGTH);
  print_message (stdout, MSGCAT_UTIL_GENERIC_VERSION, exec_name, buf);
}

/*
 * proc_execute - to fork/exec internal service exes
 */
static int
proc_execute (const char *file, const char *args[], bool wait_child, bool close_output, bool close_err, int *out_pid)
{
  return proc_execute_internal (file, args, wait_child, close_output, close_err, false, out_pid);
}

/*
 * proc_execute_hide_cmd_args - to fork/exec cub_admin for command line
 *
 * It will hide commandline arguments.
 */
static int
proc_execute_hide_cmd_args (const char *file, const char *args[], bool wait_child, bool close_output, bool close_err,
			    int *out_pid)
{
  return proc_execute_internal (file, args, wait_child, close_output, close_err, true, out_pid);
}

#if defined(WINDOWS)
static int
proc_execute_internal (const char *file, const char *args[], bool wait_child, bool close_output, bool close_err,
		       bool hide_cmd_args, int *out_pid)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  int i, j, k, cmd_arg_len;
  char cmd_arg[1024];
  char executable_path[PATH_MAX];
  int ret_code = NO_ERROR;
  bool inherited_handle = TRUE;
  char fixed_arg[1024];		/* replace " with "" */

  if (out_pid)
    {
      *out_pid = 0;
    }

  (void) envvar_bindir_file (executable_path, PATH_MAX, file);

  for (i = 0, cmd_arg_len = 0; args[i]; i++)
    {
      if (strchr (args[i], '"') == NULL)
	{
	  cmd_arg_len += sprintf (cmd_arg + cmd_arg_len, "\"%s\" ", args[i]);
	}
      else
	{
	  k = 0;
	  for (j = 0; j < strlen (args[i]); j++)
	    {
	      if (args[i][j] == '"')
		{
		  fixed_arg[k++] = '"';
		}
	      fixed_arg[k++] = args[i][j];
	    }
	  fixed_arg[k] = '\0';

	  cmd_arg_len += sprintf (cmd_arg + cmd_arg_len, "\"%s\" ", fixed_arg);
	}
    }

  GetStartupInfo (&si);
  si.dwFlags = si.dwFlags | STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle (STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle (STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle (STD_ERROR_HANDLE);
  inherited_handle = TRUE;

  if (close_output)
    {
      si.hStdOutput = NULL;
    }

  if (close_err)
    {
      si.hStdError = NULL;
    }

  if (!CreateProcess (executable_path, cmd_arg, NULL, NULL, inherited_handle, 0, NULL, NULL, &si, &pi))
    {
      return ER_FAILED;
    }

  if (wait_child)
    {
      DWORD status = 0;
      WaitForSingleObject (pi.hProcess, INFINITE);
      GetExitCodeProcess (pi.hProcess, &status);
      ret_code = status;
    }
  else
    {
      if (out_pid)
	{
	  *out_pid = pi.dwProcessId;
	}
    }

  CloseHandle (pi.hProcess);
  CloseHandle (pi.hThread);
  return ret_code;
}

#else
static int
proc_execute_internal (const char *file, const char *args[], bool wait_child, bool close_output, bool close_err,
		       bool hide_cmd_args, int *out_pid)
{
  pid_t pid, tmp;
  char executable_path[PATH_MAX];

  if (out_pid)
    {
      *out_pid = 0;
    }

  (void) envvar_bindir_file (executable_path, PATH_MAX, file);

  /* do not process SIGCHLD, a child process will be defunct */
  if (wait_child)
    {
      signal (SIGCHLD, SIG_DFL);
    }
  else
    {
      signal (SIGCHLD, SIG_IGN);
    }

  pid = fork ();
  if (pid == -1)
    {
      perror ("fork");
      return ER_GENERIC_ERROR;
    }
  else if (pid == 0)
    {
      /* a child process handle SIGCHLD to SIG_DFL */
      signal (SIGCHLD, SIG_DFL);
      if (close_output)
	{
	  fclose (stdout);
	}

      if (close_err)
	{
	  fclose (stderr);
	}

      if (execv (executable_path, (char *const *) args) == -1)
	{
	  perror ("execv");
	  return ER_GENERIC_ERROR;
	}
    }
  else
    {
      int status = 0;

      if (hide_cmd_args == true)
	{
	  /* for hide password */
	  hide_cmd_line_args ((char **) args);
	}

      /* sleep (0); */
      if (wait_child)
	{
	  do
	    {
	      tmp = waitpid (-1, &status, 0);
	      if (tmp == -1)
		{
		  perror ("waitpid");
		  return ER_GENERIC_ERROR;
		}
	    }
	  while (tmp != pid);
	}
      else
	{
	  /* sleep (3); */
	  if (out_pid)
	    {
	      *out_pid = pid;
	    }
	  return NO_ERROR;
	}

      if (WIFEXITED (status))
	{
	  return WEXITSTATUS (status);
	}
    }
  return ER_GENERIC_ERROR;
}
#endif

/*
 * hide_cmd_line_args -
 *
 * return:
 *
 */
static void
hide_cmd_line_args (char **args)
{
#if defined (LINUX)
  int i;

  assert (args[0] != NULL && args[1] != NULL);

  for (i = 2; args[i] != NULL; i++)
    {
      memset (args[i], '\0', strlen (args[i]));
    }
#endif /* LINUX */
}

/*
 * process_master -
 *
 * return:
 *
 *      command_type(in):
 */
static int
process_master (int command_type)
{
  int status = NO_ERROR;
  int pid = 0;
  int waited_seconds = 0;

  int master_port = prm_get_master_port_id ();

  switch (command_type)
    {
    case START:
      {
	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_MASTER_NAME, PRINT_CMD_START);
	if (!css_does_master_exist (master_port))
	  {
	    const char *args[] = { UTIL_MASTER_NAME, NULL };

	    status = ER_GENERIC_ERROR;
	    while (status != NO_ERROR && waited_seconds < 180)
	      {
		if (pid == 0 || (pid != 0 && is_terminated_process (pid)))
		  {
		    if (pid != 0)
		      {
			util_log_write_errstr ("Master does not exist. Try to start it again.\n");
		      }

#if !defined(WINDOWS)
		    envvar_set ("NO_DAEMON", "true");
#endif
		    status = proc_execute (UTIL_MASTER_NAME, args, false, false, false, &pid);
		    if (status != NO_ERROR)
		      {
			util_log_write_errstr ("Could not start master process.\n");
			break;
		      }
		  }

		/* The master process needs a few seconds to bind port */
		sleep (1);
		waited_seconds++;

		status = css_does_master_exist (master_port) ? NO_ERROR : ER_GENERIC_ERROR;
	      }

	    if (status != NO_ERROR)
	      {
		/* The master process failed to start or could not connected within 3 minutes */
		util_log_write_errid (MSGCAT_UTIL_GENERIC_RESULT, PRINT_MASTER_NAME, command_string (command_type),
				      PRINT_RESULT_FAIL);
	      }

	    print_result (PRINT_MASTER_NAME, status, command_type);
	  }
	else
	  {
	    status = ER_GENERIC_ERROR;
	    print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_MASTER_NAME);
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_MASTER_NAME);
	  }
      }
      break;
    case STOP:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_MASTER_NAME, PRINT_CMD_STOP);
      if (css_does_master_exist (master_port))
	{
	  const char *args[] = { UTIL_COMMDB_NAME, COMMDB_ALL_STOP, NULL };
	  status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);

	  status = css_does_master_exist (master_port) ? ER_GENERIC_ERROR : NO_ERROR;

	  print_result (PRINT_MASTER_NAME, status, command_type);
	}
      else
	{
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
	}
      break;
    }
  return status;
}

#if defined(WINDOWS)
/*
 * is_windwos_service_running -
 *
 * return:
 *
 *      sleep_time(in):
 *
 * NOTE:
 */
static bool
is_windows_service_running (unsigned int sleep_time)
{
  FILE *input;
  char buf[32], cmd[PATH_MAX];

  sleep (sleep_time);

  make_exec_abspath (cmd, PATH_MAX, (char *) UTIL_WIN_SERVICE_CONTROLLER_NAME " " "-status");

  input = popen (cmd, "r");
  if (input == NULL)
    {
      return false;
    }

  memset (buf, '\0', sizeof (buf));

  if ((fgets (buf, 32, input) == NULL) || strncmp (buf, "SERVICE_RUNNING", 15) != 0)
    {
      pclose (input);
      return false;
    }

  pclose (input);

  return true;
}
#endif
/*
 * are_all_services_running - are all of services running
 *
 * return:
 *
 *      sleep_time(in):
 *
 * NOTE:
 */
static bool
are_all_services_running (unsigned int sleep_time, bool check_win_service)
{
  return check_all_services_status (sleep_time, ALL_SERVICES_RUNNING, check_win_service);
}

/*
 * are_all_services_stopped - are all of services stopped
 *
 * return:
 *
 *      sleep_time(in):
 *
 * NOTE:
 */
static bool
are_all_services_stopped (unsigned int sleep_time, bool check_win_service)
{
  return check_all_services_status (sleep_time, ALL_SERVICES_STOPPED, check_win_service);
}


/*
 * check_all_services_status - check all service status and compare with
			      expected_status, if not meet return false.
 *
 * return:
 *
 *      sleep_time(in):
 *      expected_status(in);
 * NOTE:
 */
static bool
check_all_services_status (unsigned int sleep_time, UTIL_ALL_SERVICES_STATUS expected_status, bool check_win_service)
{
  bool ret;
  int master_port;

  if (check_win_service)
    {
#if defined (WINDOWS)
      /* check whether CUBRIDService is running */
      ret = is_windows_service_running (sleep_time);
      if ((expected_status == ALL_SERVICES_RUNNING && !ret) || (expected_status == ALL_SERVICES_STOPPED && ret))
	{
	  return false;
	}
#else
      assert (0);
      return false;
#endif
    }

  master_port = prm_get_master_port_id ();
  /* check whether cub_master is running */
  ret = css_does_master_exist (master_port);
  if ((expected_status == ALL_SERVICES_RUNNING && !ret) || (expected_status == ALL_SERVICES_STOPPED && ret))
    {
      return false;
    }

  if (strcmp (get_property (SERVICE_START_SERVER), PROPERTY_ON) == 0
      && us_Property_map[SERVER_START_LIST].property_value != NULL)
    {
      char buf[4096];
      char *list, *token, *save;
      const char *delim = " ,:";

      memset (buf, '\0', sizeof (buf));

      strncpy (buf, us_Property_map[SERVER_START_LIST].property_value, sizeof (buf) - 1);

      for (list = buf;; list = NULL)
	{
	  token = strtok_r (list, delim, &save);
	  if (token == NULL)
	    {
	      break;
	    }
	  /* check whether cub_server is running */
	  ret = is_server_running (CHECK_SERVER, token, 0);
	  if ((expected_status == ALL_SERVICES_RUNNING && !ret) || (expected_status == ALL_SERVICES_STOPPED && ret))
	    {
	      return false;
	    }
	}
    }

  /* check whether cub_broker is running */
  if (strcmp (get_property (SERVICE_START_BROKER), PROPERTY_ON) == 0)
    {
      int broker_status;

      /* broker_status may be 0, 1, 2. */
      broker_status = is_broker_running ();
      if ((expected_status == ALL_SERVICES_RUNNING && broker_status != 0)
	  || (expected_status == ALL_SERVICES_STOPPED && broker_status != 1))
	{
	  return false;
	}
    }

  /* check whether cub_gateway is running */
  if (strcmp (get_property (SERVICE_START_GATEWAY), PROPERTY_ON) == 0)
    {
      int gateway_status;

      /* gateway_status may be 0, 1, 2. */
      gateway_status = is_gateway_running ();
      if ((expected_status == ALL_SERVICES_RUNNING && gateway_status != 0)
	  || (expected_status == ALL_SERVICES_STOPPED && gateway_status != 1))
	{
	  return false;
	}
    }

  /* check whether cub_manager is running */
  if (strcmp (get_property (SERVICE_START_MANAGER), PROPERTY_ON) == 0)
    {
      UTIL_MANAGER_SERVER_STATUS_E manager_status;
      manager_status = is_manager_running (0);

      if ((expected_status == ALL_SERVICES_RUNNING && manager_status != MANAGER_SERVER_RUNNING)
	  || (expected_status == ALL_SERVICES_STOPPED && manager_status != MANAGER_SERVER_STOPPED))
	{
	  return false;
	}
    }

  /* do not check heartbeat in windows */

  return true;
}


/*
 * process_service -
 *
 * return:
 *
 *      command_type(in):
 *      process_window_service(in):
 *
 * NOTE:
 */
static int
process_service (int command_type, bool process_window_service)
{
  int status = NO_ERROR;

  switch (command_type)
    {
    case START:
      if (process_window_service)
	{
#if defined(WINDOWS)
	  if (!are_all_services_running (0, process_window_service))
	    {
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_SERVICE,
		PRINT_CMD_START, NULL
	      };

	      proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
	      status = are_all_services_running (0, process_window_service) ? NO_ERROR : ER_GENERIC_ERROR;
	      print_result (PRINT_SERVICE_NAME, status, command_type);
	    }
	  else
	    {
	      print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_SERVICE_NAME);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_SERVICE_NAME);
	      status = ER_GENERIC_ERROR;
	    }
#endif
	}
      else
	{
	  if (!are_all_services_running (0, process_window_service))
	    {
	      (void) process_master (command_type);

	      if (strcmp (get_property (SERVICE_START_SERVER), PROPERTY_ON) == 0
		  && us_Property_map[SERVER_START_LIST].property_value != NULL
		  && us_Property_map[SERVER_START_LIST].property_value[0] != '\0')
		{
		  (void) process_server (command_type, 0, NULL, false, true, false);
		}
	      if (strcmp (get_property (SERVICE_START_BROKER), PROPERTY_ON) == 0)
		{
		  (void) process_broker (command_type, 0, NULL, false);
		}
	      if (strcmp (get_property (SERVICE_START_GATEWAY), PROPERTY_ON) == 0)
		{
		  (void) process_gateway (command_type, 0, NULL, false);
		}
	      if (strcmp (get_property (SERVICE_START_MANAGER), PROPERTY_ON) == 0)
		{
		  (void) process_manager (command_type, false);
		}
	      if (strcmp (get_property (SERVICE_START_HEARTBEAT), PROPERTY_ON) == 0)
		{
		  (void) process_heartbeat (command_type, 0, NULL);
		}
	      status = are_all_services_running (0, process_window_service) ? NO_ERROR : ER_GENERIC_ERROR;
	    }
	  else
	    {
	      print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_SERVICE_NAME);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_SERVICE_NAME);
	      status = ER_GENERIC_ERROR;
	    }
	}
      break;
    case STOP:
      if (process_window_service)
	{
#if defined(WINDOWS)
	  if (is_windows_service_running (0))
	    {
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_SERVICE,
		PRINT_CMD_STOP, NULL
	      };

	      proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
	      status = are_all_services_stopped (0, process_window_service) ? NO_ERROR : ER_GENERIC_ERROR;

	      print_result (PRINT_SERVICE_NAME, status, command_type);
	    }
	  else
	    {
	      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_SERVICE_NAME);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_SERVICE_NAME);
	      status = ER_GENERIC_ERROR;
	    }
#endif
	}
      else
	{
	  if (!are_all_services_stopped (0, process_window_service))
	    {
	      (void) process_javasp (command_type, 0, NULL, false, false, process_window_service, false);

	      if (strcmp (get_property (SERVICE_START_SERVER), PROPERTY_ON) == 0
		  && us_Property_map[SERVER_START_LIST].property_value != NULL
		  && us_Property_map[SERVER_START_LIST].property_value[0] != '\0')
		{
		  (void) process_server (command_type, 0, NULL, false, true, false);
		}
	      if (strcmp (get_property (SERVICE_START_BROKER), PROPERTY_ON) == 0)
		{
		  (void) process_broker (command_type, 0, NULL, false);
		}
	      if (strcmp (get_property (SERVICE_START_GATEWAY), PROPERTY_ON) == 0)
		{
		  (void) process_gateway (command_type, 0, NULL, false);
		}
	      if (strcmp (get_property (SERVICE_START_MANAGER), PROPERTY_ON) == 0)
		{
		  (void) process_manager (command_type, false);
		}

	      if (strcmp (get_property (SERVICE_START_HEARTBEAT), PROPERTY_ON) == 0)
		{
		  (void) process_heartbeat (command_type, 0, NULL);
		}

	      (void) process_master (command_type);

	      status = are_all_services_stopped (0, process_window_service) ? NO_ERROR : ER_GENERIC_ERROR;
	    }
	  else
	    {
	      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_SERVICE_NAME);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_SERVICE_NAME);
	      status = ER_GENERIC_ERROR;
	    }
	}
      break;
    case RESTART:
      status = process_service (STOP, process_window_service);
      status = process_service (START, process_window_service);
      break;
    case STATUS:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_MASTER_NAME, PRINT_CMD_STATUS);
      if (css_does_master_exist (prm_get_master_port_id ()))
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_MASTER_NAME);
	}
      else
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
	}

      {
	const char *args[] = { "-b" };

	(void) process_server (command_type, 0, NULL, false, true, false);
	(void) process_javasp (command_type, 0, NULL, true, false, false, false);
	(void) process_broker (command_type, 1, args, false);
	(void) process_gateway (command_type, 1, args, false);
	(void) process_manager (command_type, false);
	if (strcmp (get_property (SERVICE_START_HEARTBEAT), PROPERTY_ON) == 0)
	  {
	    (void) process_heartbeat (command_type, 0, NULL);
	  }
      }
      break;
    default:
      status = ER_GENERIC_ERROR;
    }

  return status;
}

/*
 * get_server_names -
 *
 * return:
 *
 *      out_buffer (out):
 */
static void
get_server_names (const char *type, char **name_buffer)
{
  FILE *input;
  char buf[4096];
  char cmd[PATH_MAX];

  assert (strcmp (type, CHECK_SERVER) == 0 || strcmp (type, CHECK_HA_SERVER) == 0);
  assert (name_buffer != NULL);

  *name_buffer = NULL;

  make_exec_abspath (cmd, PATH_MAX, (char *) UTIL_COMMDB_NAME " " COMMDB_ALL_STATUS);
  input = popen (cmd, "r");
  if (input == NULL)
    {
      return;
    }

  int offset = 0;
  /* *INDENT-OFF* */
  std::string delimiter = " ";
  std::string result;
  /* *INDENT-ON* */
  while (fgets (buf, 4096, input) != NULL)
    {
      /* *INDENT-OFF* */
      std::string s (buf);

      /*
      * ignore HA-applylogdb and HA-copylogdb
      * check Server and HA-Server
      */
      std::string::size_type server_pos = s.find (type);
      if (server_pos == std::string::npos)
      {
        continue;
      }

      /* find Server */
      std::string::size_type start = s.find (delimiter, 1) + 1;
      std::string::size_type end = s.find (delimiter, start);

      std::string server_name = s.substr (start, end - start);

      result.append (server_name);
      result.append (",");
      /* *INDENT-ON* */
    }

  /* allocate buffer, it should be freed */
  if (!result.empty ())
    {
      int name_length = result.size ();
      *name_buffer = (char *) malloc (name_length + 1);
      if (*name_buffer)
	{
	  strncpy (*name_buffer, result.c_str (), name_length);
	  (*name_buffer)[name_length] = '\0';
	}
    }

  pclose (input);
}

/*
 * check_server -
 *
 * return:
 *
 *      type(in):
 *      server_name(in):
 */
static bool
check_server (const char *type, const char *server_name)
{
  FILE *input;
  char buf[4096], *token, *save_ptr, *delim = (char *) " ";
  char cmd[PATH_MAX];

  make_exec_abspath (cmd, PATH_MAX, (char *) UTIL_COMMDB_NAME " " COMMDB_ALL_STATUS);
  input = popen (cmd, "r");
  if (input == NULL)
    {
      return false;
    }

  while (fgets (buf, 4096, input) != NULL)
    {
      token = strtok_r (buf, delim, &save_ptr);
      if (token == NULL)
	{
	  continue;
	}

      if (strcmp (type, CHECK_SERVER) == 0)
	{
	  if (strcmp (token, CHECK_SERVER) != 0 && strcmp (token, CHECK_HA_SERVER) != 0)
	    {
	      continue;
	    }
	}
      else
	{
	  if (strcmp (token, type) != 0)
	    {
	      continue;
	    }
	}

      token = strtok_r (NULL, delim, &save_ptr);
      if (token != NULL && strcmp (token, server_name) == 0)
	{
	  pclose (input);
	  return true;
	}
    }
  pclose (input);
  return false;
}

/*
 * is_server_running -
 *
 * return:
 *
 *      type(in):
 *      server_name(in):
 *      pid(in):
 */
static bool
is_server_running (const char *type, const char *server_name, int pid)
{
  if (!css_does_master_exist (prm_get_master_port_id ()))
    {
      return false;
    }

  if (pid <= 0)
    {
      return check_server (type, server_name);
    }

  while (true)
    {
      if (!is_terminated_process (pid))
	{
	  if (check_server (type, server_name))
	    {
	      return true;
	    }
	  sleep (1);

	  /* A child process is defunct because the SIGCHLD signal ignores. */
	  /*
	   * if (waitpid (pid, &status, WNOHANG) == -1) { perror ("waitpid"); } */
	}
      else
	{
	  return false;
	}
    }
}

/*
 * process_server -
 *
 * return:
 *
 *      command_type(in):
 *      argc(in):
 *      argv(in) :
 *      show_usage(in):
 *
 * NOTE:
 */
static int
process_server (int command_type, int argc, char **argv, bool show_usage, bool check_ha_mode,
		bool process_window_service)
{
  char buf[4096];
  char *list, *token, *save;
  const char *delim = " ,:";
  int status = NO_ERROR;
  int master_port = prm_get_master_port_id ();

  memset (buf, '\0', sizeof (buf));

  /* A string is copyed because strtok_r() modify an original string. */
  if (argc == 0)
    {
      if (us_Property_map[SERVER_START_LIST].property_value != NULL)
	{
	  strncpy (buf, us_Property_map[SERVER_START_LIST].property_value, sizeof (buf) - 1);
	}
    }
  else
    {
      strncpy (buf, argv[0], sizeof (buf) - 1);
    }

  if (command_type != STATUS && strlen (buf) == 0)
    {
      if (show_usage)
	{
	  util_service_usage (SERVER);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_CMD);
	}
      return ER_GENERIC_ERROR;
    }

  switch (command_type)
    {
    case START:
      if (process_window_service)
	{
#if defined(WINDOWS)
	  const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_SERVER,
	    COMMAND_TYPE_START, NULL, NULL
	  };

	  for (list = buf;; list = NULL)
	    {
	      token = strtok_r (list, delim, &save);
	      if (token == NULL)
		{
		  break;
		}

	      args[3] = token;

	      /* load parameters for [@database] section */
	      if (check_ha_mode == true)
		{
		  status = sysprm_load_and_init (token, NULL, SYSPRM_IGNORE_INTL_PARAMS);
		  if (status != NO_ERROR)
		    {
		      print_result (PRINT_SERVER_NAME, status, command_type);
		      break;
		    }

		  if (util_get_ha_mode_for_sa_utils () != HA_MODE_OFF)
		    {
		      status = ER_GENERIC_ERROR;
		      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE);
		      print_result (PRINT_SERVER_NAME, status, command_type);
		      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE);
		      continue;
		    }
		}
	      if (is_server_running (CHECK_SERVER, token, 0))
		{
		  status = ER_GENERIC_ERROR;
		  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_SERVER_NAME, token);
		  util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_SERVER_NAME, token);
		  continue;
		}
	      else
		{
		  status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);

		  if (status == NO_ERROR && !is_server_running (CHECK_SERVER, token, 0))
		    {
		      status = ER_GENERIC_ERROR;
		    }
		  print_result (PRINT_SERVER_NAME, status, command_type);
		}
	    }
#endif
	}
      else
	{
	  if (!css_does_master_exist (master_port))
	    {
	      status = process_master (command_type);
	      if (status != NO_ERROR)
		{
		  break;	/* escape switch */
		}
	    }

	  for (list = buf;; list = NULL)
	    {
	      token = strtok_r (list, delim, &save);
	      if (token == NULL)
		{
		  break;
		}
	      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_3S, PRINT_SERVER_NAME, PRINT_CMD_START, token);

#if !defined(WINDOWS)
	      if (check_ha_mode == true)
		{
		  status = sysprm_load_and_init (token, NULL, SYSPRM_IGNORE_INTL_PARAMS);
		  if (status != NO_ERROR)
		    {
		      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
		      print_result (PRINT_SERVER_NAME, status, command_type);
		      break;
		    }

		  if (util_get_ha_mode_for_sa_utils () != HA_MODE_OFF)
		    {
		      status = ER_GENERIC_ERROR;
		      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE);
		      print_result (PRINT_SERVER_NAME, status, command_type);
		      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE);
		      continue;
		    }
		}
#endif /* !WINDOWS */

	      if (is_server_running (CHECK_SERVER, token, 0))
		{
		  status = ER_GENERIC_ERROR;
		  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_SERVER_NAME, token);
		  util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_SERVER_NAME, token);
		  continue;
		}
	      else
		{
		  int pid;
		  const char *args[] = { UTIL_CUBRID_NAME, token, NULL };
		  status = proc_execute (UTIL_CUBRID_NAME, args, false, false, false, &pid);

		  if (status == NO_ERROR && !is_server_running (CHECK_SERVER, token, pid))
		    {
		      status = ER_GENERIC_ERROR;
		    }
		  print_result (PRINT_SERVER_NAME, status, command_type);

		  /* run javasp server if DB server is started successfully */
		  if (status == NO_ERROR)
		    {
		      (void) process_javasp (command_type, 1, (const char **) &token, false, true,
					     process_window_service, false);
		    }
		}
	    }
	}
      break;
    case STOP:
      for (list = buf;; list = NULL)
	{
	  token = strtok_r (list, delim, &save);
	  if (token == NULL)
	    {
	      break;
	    }

	  /* try to stop javasp server first */
	  if (is_javasp_running (token) == JAVASP_SERVER_RUNNING)
	    {
	      (void) process_javasp (command_type, 1, (const char **) &token, false, true, process_window_service,
				     false);
	    }

	  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_3S, PRINT_SERVER_NAME, PRINT_CMD_STOP, token);

	  if (is_server_running (CHECK_SERVER, token, 0))
	    {
	      if (process_window_service)
		{
#if defined(WINDOWS)
		  const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_SERVER,
		    COMMAND_TYPE_STOP, NULL, NULL
		  };

		  args[3] = token;
		  status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
		}
	      else
		{
		  const char *args[] = { UTIL_COMMDB_NAME, COMMDB_SERVER_STOP, token, NULL };
#if !defined(WINDOWS)
		  if (check_ha_mode)
		    {
		      status = sysprm_load_and_init (token, NULL, SYSPRM_IGNORE_INTL_PARAMS);
		      if (status != NO_ERROR)
			{
			  print_result (PRINT_SERVER_NAME, status, command_type);
			  break;
			}

		      if (util_get_ha_mode_for_sa_utils () != HA_MODE_OFF)
			{
			  status = ER_GENERIC_ERROR;
			  print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE);
			  util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE);
			  print_result (PRINT_SERVER_NAME, status, command_type);
			  continue;
			}
		    }
#endif /* !WINDOWS */
		  status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
		}
	      print_result (PRINT_SERVER_NAME, status, command_type);
	    }
	  else
	    {
	      status = ER_GENERIC_ERROR;
	      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_SERVER_NAME, token);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_SERVER_NAME, token);
	    }
	}
      break;
    case RESTART:
      status = process_server (STOP, argc, argv, show_usage, check_ha_mode, process_window_service);
      status = process_server (START, argc, argv, show_usage, check_ha_mode, process_window_service);
      break;
    case STATUS:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_SERVER_NAME, PRINT_CMD_STATUS);
      if (css_does_master_exist (master_port))
	{
	  const char *args[] = { UTIL_COMMDB_NAME, COMMDB_SERVER_STATUS, NULL };
	  status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
	}
      else
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
	}
      break;
    case ACCESS_CONTROL:
      {
	if (argc != 2)
	  {
	    status = ER_GENERIC_ERROR;

	    if (show_usage)
	      {
		util_service_usage (SERVER);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_CMD);
	      }
	    break;
	  }

	if (strcasecmp (argv[0], "reload") == 0)
	  {
	    const char *args[] = { UTIL_ADMIN_NAME, UTIL_OPTION_ACLDB, ACLDB_RELOAD, argv[1],
	      NULL
	    };

	    status = proc_execute (UTIL_ADMIN_NAME, args, true, false, false, NULL);
	    print_result (PRINT_SERVER_NAME, status, command_type);
	  }
	else if (strcasecmp (argv[0], "status") == 0)
	  {
	    const char *args[] = { UTIL_ADMIN_NAME, UTIL_OPTION_ACLDB, argv[1], NULL };

	    status = proc_execute (UTIL_ADMIN_NAME, args, true, false, false, NULL);
	  }
	else
	  {
	    status = ER_GENERIC_ERROR;
	    if (show_usage)
	      {
		util_service_usage (SERVER);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_CMD);
	      }

	    break;
	  }
      }

      break;
    default:
      status = ER_GENERIC_ERROR;
      break;
    }

  return status;
}

/*
 * is_broker_running -
 *
 * return:
 *
 */
static int
is_broker_running (void)
{
  const char *args[] = { UTIL_MONITOR_NAME, 0 };
  int status = proc_execute (UTIL_MONITOR_NAME, args, true, true, false, NULL);
  return status;
}

/*
 * process_broker -
 *
 * return:
 *
 *      command_type(in):
 *      name(in):
 *
 */
static int
process_broker (int command_type, int argc, const char **argv, bool process_window_service)
{
  int status = NO_ERROR;

  switch (command_type)
    {
    case START:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_BROKER_NAME, PRINT_CMD_START);
      switch (is_broker_running ())
	{
	case 0:		/* no error */
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_BROKER_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_BROKER_NAME);
	  break;
	case 1:		/* shm_open error */
	  if (process_window_service)
	    {
#if defined(WINDOWS)
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_BROKER,
		COMMAND_TYPE_START, NULL
	      };

	      status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
	      status = (is_broker_running () == 0) ? NO_ERROR : ER_GENERIC_ERROR;
#endif
	    }
	  else
	    {
	      const char *args[] = { UTIL_BROKER_NAME, COMMAND_TYPE_START, NULL };
	      status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
	      status = (is_broker_running () == 0) ? NO_ERROR : ER_GENERIC_ERROR;
	    }
	  print_result (PRINT_BROKER_NAME, status, command_type);

	  break;
	case 2:		/* no conf file or parameter error */
	default:
	  status = ER_GENERIC_ERROR;
	  print_result (PRINT_BROKER_NAME, ER_GENERIC_ERROR, command_type);
	  break;
	}
      break;
    case STOP:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_BROKER_NAME, PRINT_CMD_STOP);
      switch (is_broker_running ())
	{
	case 0:		/* no error */
	  if (process_window_service)
	    {
#if defined(WINDOWS)
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_BROKER,
		COMMAND_TYPE_STOP, NULL
	      };

	      status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
	      status = (is_broker_running () == 0) ? ER_GENERIC_ERROR : NO_ERROR;
#endif
	    }
	  else
	    {
	      const char *args[] = { UTIL_BROKER_NAME, COMMAND_TYPE_STOP, NULL };
	      status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
	      status = (is_broker_running () == 0) ? ER_GENERIC_ERROR : NO_ERROR;
	    }
	  print_result (PRINT_BROKER_NAME, status, command_type);
	  break;
	case 1:		/* shm_open error */
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_BROKER_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_BROKER_NAME);
	  break;
	case 2:		/* no conf file or parameter error */
	default:		/* other error */
	  status = ER_GENERIC_ERROR;
	  print_result (PRINT_BROKER_NAME, ER_GENERIC_ERROR, command_type);
	  break;
	}
      break;
    case RESTART:
      status = process_broker (STOP, 0, NULL, process_window_service);
#if defined (WINDOWS)
      Sleep (500);
#endif
      status = process_broker (START, 0, NULL, process_window_service);
      break;
    case STATUS:
      {
	int i;
	const char **args;

	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_BROKER_NAME, PRINT_CMD_STATUS);
	switch (is_broker_running ())
	  {
	  case 0:		/* no error */
	    args = (const char **) malloc (sizeof (char *) * (argc + 2));
	    if (args == NULL)
	      {
		status = ER_GENERIC_ERROR;
		util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
		break;
	      }

	    args[0] = PRINT_BROKER_NAME " " PRINT_CMD_STATUS;
	    for (i = 0; i < argc; i++)
	      {
		args[i + 1] = argv[i];
	      }
	    args[argc + 1] = NULL;
	    status = proc_execute (UTIL_MONITOR_NAME, args, true, false, false, NULL);

	    free (args);
	    break;

	  case 1:		/* shm_open error */
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_BROKER_NAME);
	    break;

	  case 2:		/* no conf file */
	  default:		/* other error */
	    status = ER_GENERIC_ERROR;
	    print_result (PRINT_BROKER_NAME, ER_GENERIC_ERROR, command_type);
	    break;
	  }
      }
      break;
    case ON:
      {
	if (process_window_service)
	  {
#if defined(WINDOWS)
	    const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_BROKER,
	      COMMAND_TYPE_ON, argv[0], NULL
	    };

	    status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	  }
	else
	  {
	    const char *args[] = { UTIL_BROKER_NAME, COMMAND_TYPE_ON, argv[0], NULL };
	    if (argc <= 0)
	      {
		status = ER_GENERIC_ERROR;
		print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		break;
	      }
	    status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
	  }
      }
      break;
    case OFF:
      {
	if (process_window_service)
	  {
#if defined(WINDOWS)
	    const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_BROKER,
	      COMMAND_TYPE_OFF, argv[0], NULL
	    };

	    status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	  }
	else
	  {
	    const char *args[] = { UTIL_BROKER_NAME, COMMAND_TYPE_OFF, argv[0], NULL };
	    if (argc <= 0)
	      {
		status = ER_GENERIC_ERROR;
		print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		break;
	      }
	    status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
	  }
      }
      break;
    case ACCESS_CONTROL:
      {
	const char *args[5];

	args[0] = UTIL_BROKER_NAME;
	args[1] = COMMAND_TYPE_ACL;
	args[2] = argv[0];

	if (argc == 1)
	  {
	    args[3] = NULL;
	  }
	else if (argc == 2)
	  {
	    args[3] = argv[1];
	    args[4] = NULL;
	  }
	else
	  {
	    status = ER_GENERIC_ERROR;
	    util_service_usage (BROKER);
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_CMD);
	    break;
	  }
	status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
	print_result (PRINT_BROKER_NAME, status, command_type);
	break;
      }
    case RESET:
      {
	if (process_window_service)
	  {
#if defined(WINDOWS)
	    const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_BROKER,
	      COMMAND_TYPE_RESET, argv[0], NULL
	    };

	    status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	  }
	else
	  {
	    const char *args[] = { UTIL_BROKER_NAME, COMMAND_TYPE_RESET, argv[0], NULL };
	    if (argc <= 0)
	      {
		status = ER_GENERIC_ERROR;
		print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		break;
	      }
	    status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
	  }
      }
      break;

    case INFO:
      {
	const char *args[] = { UTIL_BROKER_NAME, COMMAND_TYPE_INFO, NULL };

	status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);
      }
      break;

    case GET_SHARID:
      {
	int i;
	const char **args;
	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_BROKER_NAME, PRINT_CMD_GETID);

	if (is_broker_running ())
	  {
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_BROKER_NAME);
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_BROKER_NAME);
	    status = ER_GENERIC_ERROR;
	    break;
	  }

	args = (const char **) malloc (sizeof (char *) * (argc + 3));
	if (args == NULL)
	  {
	    status = ER_GENERIC_ERROR;
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	    break;
	  }

	args[0] = UTIL_BROKER_NAME;
	args[1] = PRINT_CMD_GETID;

	for (i = 0; i < argc; i++)
	  {
	    args[i + 2] = argv[i];
	  }
	args[argc + 2] = NULL;

	status = proc_execute (UTIL_BROKER_NAME, args, true, false, false, NULL);

	free (args);
      }
      break;

    case TEST:
      {
	int i;
	const char **args;

	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_BROKER_NAME, PRINT_CMD_TEST);
	switch (is_broker_running ())
	  {
	  case 0:		/* no error */
	    args = (const char **) malloc (sizeof (char *) * (argc + 2));
	    if (args == NULL)
	      {
		status = ER_GENERIC_ERROR;
		util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
		break;
	      }

	    args[0] = PRINT_BROKER_NAME " " PRINT_CMD_TEST;
	    for (i = 0; i < argc; i++)
	      {
		args[i + 1] = argv[i];
	      }
	    args[argc + 1] = NULL;
	    status = proc_execute (UTIL_TESTER_NAME, args, true, false, false, NULL);

	    free (args);
	    break;

	  case 1:		/* shm_open error */
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_BROKER_NAME);
	    break;

	  case 2:		/* no conf file */
	  default:		/* other error */
	    status = ER_GENERIC_ERROR;
	    print_result (PRINT_BROKER_NAME, ER_GENERIC_ERROR, command_type);
	    break;
	  }
      }
      break;

    default:
      status = ER_GENERIC_ERROR;
      break;
    }

  return status;
}

/*
 * is_gateway_running -
 *
 * return:
 *
 */
static int
is_gateway_running (void)
{
  const char *args[] = { UTIL_GATEWAY_MONITOR_NAME, 0 };
  int status = proc_execute (UTIL_GATEWAY_MONITOR_NAME, args, true, true, false, NULL);
  return status;
}

/*
 * process_gateway -
 *
 * return:
 *
 *      command_type(in):
 *      name(in):
 *
 */
static int
process_gateway (int command_type, int argc, const char **argv, bool process_window_service)
{
  int status = NO_ERROR;
  switch (command_type)
    {
    case START:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_GATEWAY_NAME, PRINT_CMD_START);
      switch (is_gateway_running ())
	{
	case 0:		/* no error */
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_GATEWAY_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_GATEWAY_NAME);
	  break;
	case 1:		/* shm_open error */
	  if (process_window_service)
	    {
#if defined(WINDOWS)
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_GATEWAY,
		COMMAND_TYPE_START, NULL
	      };

	      status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
	      status = (is_gateway_running () == 0) ? NO_ERROR : ER_GENERIC_ERROR;
#endif
	    }
	  else
	    {
	      const char *args[] = { UTIL_GATEWAY_NAME, COMMAND_TYPE_START, NULL };
	      status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
	      status = (is_gateway_running () == 0) ? NO_ERROR : ER_GENERIC_ERROR;
	    }
	  print_result (PRINT_GATEWAY_NAME, status, command_type);

	  break;
	case 2:		/* no conf file or parameter error */
	default:
	  status = ER_GENERIC_ERROR;
	  print_result (PRINT_GATEWAY_NAME, ER_GENERIC_ERROR, command_type);
	  break;
	}
      break;
    case STOP:
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_GATEWAY_NAME, PRINT_CMD_STOP);
      switch (is_gateway_running ())
	{
	case 0:		/* no error */
	  if (process_window_service)
	    {
#if defined(WINDOWS)
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_GATEWAY,
		COMMAND_TYPE_STOP, NULL
	      };

	      status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
	      status = (is_gateway_running () == 0) ? ER_GENERIC_ERROR : NO_ERROR;
#endif
	    }
	  else
	    {
	      const char *args[] = { UTIL_GATEWAY_NAME, COMMAND_TYPE_STOP, NULL };
	      status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
	      status = (is_gateway_running () == 0) ? ER_GENERIC_ERROR : NO_ERROR;
	    }
	  print_result (PRINT_GATEWAY_NAME, status, command_type);
	  break;
	case 1:		/* shm_open error */
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_GATEWAY_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_GATEWAY_NAME);
	  break;
	case 2:		/* no conf file or parameter error */
	default:		/* other error */
	  status = ER_GENERIC_ERROR;
	  print_result (PRINT_GATEWAY_NAME, ER_GENERIC_ERROR, command_type);
	  break;
	}
      break;
    case RESTART:
      status = process_gateway (STOP, 0, NULL, process_window_service);
#if defined (WINDOWS)
      Sleep (500);
#endif
      status = process_gateway (START, 0, NULL, process_window_service);
      break;
    case STATUS:
      {
	int i;
	const char **args;

	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_GATEWAY_NAME, PRINT_CMD_STATUS);
	switch (is_gateway_running ())
	  {
	  case 0:		/* no error */
	    args = (const char **) malloc (sizeof (char *) * (argc + 2));
	    if (args == NULL)
	      {
		status = ER_GENERIC_ERROR;
		util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
		break;
	      }

	    args[0] = PRINT_GATEWAY_NAME " " PRINT_CMD_STATUS;
	    for (i = 0; i < argc; i++)
	      {
		args[i + 1] = argv[i];
	      }
	    args[argc + 1] = NULL;
	    status = proc_execute (UTIL_GATEWAY_MONITOR_NAME, args, true, false, false, NULL);

	    free (args);
	    break;

	  case 1:		/* shm_open error */
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_GATEWAY_NAME);
	    break;

	  case 2:		/* no conf file */
	  default:		/* other error */
	    status = ER_GENERIC_ERROR;
	    print_result (PRINT_GATEWAY_NAME, ER_GENERIC_ERROR, command_type);
	    break;
	  }
      }
      break;
    case ON:
      {
	if (process_window_service)
	  {
#if defined(WINDOWS)
	    const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_GATEWAY,
	      COMMAND_TYPE_ON, argv[0], NULL
	    };

	    status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	  }
	else
	  {
	    const char *args[] = { UTIL_GATEWAY_NAME, COMMAND_TYPE_ON, argv[0], NULL };
	    if (argc <= 0)
	      {
		status = ER_GENERIC_ERROR;
		print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		break;
	      }
	    status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
	  }
      }
      break;
    case OFF:
      {
	if (process_window_service)
	  {
#if defined(WINDOWS)
	    const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_GATEWAY,
	      COMMAND_TYPE_OFF, argv[0], NULL
	    };
	    status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	  }
	else
	  {

	    const char *args[] = { UTIL_GATEWAY_NAME, COMMAND_TYPE_OFF, argv[0], NULL };
	    if (argc <= 0)
	      {
		status = ER_GENERIC_ERROR;
		print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		break;
	      }
	    status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
	  }
      }
      break;
    case ACCESS_CONTROL:
      {
	const char *args[5];

	args[0] = UTIL_GATEWAY_NAME;
	args[1] = COMMAND_TYPE_ACL;
	args[2] = argv[0];

	if (argc == 1)
	  {
	    args[3] = NULL;
	  }
	else if (argc == 2)
	  {
	    args[3] = argv[1];
	    args[4] = NULL;
	  }
	else
	  {
	    status = ER_GENERIC_ERROR;
	    util_service_usage (GATEWAY);
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_CMD);
	    break;
	  }
	status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
	print_result (PRINT_GATEWAY_NAME, status, command_type);
	break;
      }
    case RESET:
      {
	if (process_window_service)
	  {
#if defined(WINDOWS)
	    const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_GATEWAY,
	      COMMAND_TYPE_RESET, argv[0], NULL
	    };

	    status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	  }
	else
	  {
	    const char *args[] = { UTIL_GATEWAY_NAME, COMMAND_TYPE_RESET, argv[0], NULL };
	    if (argc <= 0)
	      {
		status = ER_GENERIC_ERROR;
		print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
		break;
	      }
	    status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
	  }
      }
      break;

    case INFO:
      {
	const char *args[] = { UTIL_GATEWAY_NAME, COMMAND_TYPE_INFO, NULL };

	status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);
      }
      break;

    case GET_SHARID:
      {
	int i;
	const char **args;
	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_GATEWAY_NAME, PRINT_CMD_GETID);

	if (is_gateway_running ())
	  {
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_GATEWAY_NAME);
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_GATEWAY_NAME);
	    status = ER_GENERIC_ERROR;
	    break;
	  }

	args = (const char **) malloc (sizeof (char *) * (argc + 3));
	if (args == NULL)
	  {
	    status = ER_GENERIC_ERROR;
	    util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
	    break;
	  }

	args[0] = UTIL_GATEWAY_NAME;
	args[1] = PRINT_CMD_GETID;

	for (i = 0; i < argc; i++)
	  {
	    args[i + 2] = argv[i];
	  }
	args[argc + 2] = NULL;

	status = proc_execute (UTIL_GATEWAY_NAME, args, true, false, false, NULL);

	free (args);
      }
      break;

    case TEST:
      {
	int i;
	const char **args;

	print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_GATEWAY_NAME, PRINT_CMD_TEST);
	switch (is_gateway_running ())
	  {
	  case 0:		/* no error */
	    args = (const char **) malloc (sizeof (char *) * (argc + 2));
	    if (args == NULL)
	      {
		status = ER_GENERIC_ERROR;
		util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
		break;
	      }

	    args[0] = PRINT_GATEWAY_NAME " " PRINT_CMD_TEST;
	    for (i = 0; i < argc; i++)
	      {
		args[i + 1] = argv[i];
	      }
	    args[argc + 1] = NULL;
	    status = proc_execute (UTIL_TESTER_NAME, args, true, false, false, NULL);

	    free (args);
	    break;

	  case 1:		/* shm_open error */
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_GATEWAY_NAME);
	    break;

	  case 2:		/* no conf file */
	  default:		/* other error */
	    status = ER_GENERIC_ERROR;
	    print_result (PRINT_GATEWAY_NAME, ER_GENERIC_ERROR, command_type);
	    break;
	  }
      }
      break;

    default:
      status = ER_GENERIC_ERROR;
      break;
    }

  return status;
}

static UTIL_MANAGER_SERVER_STATUS_E
is_manager_running (unsigned int sleep_time)
{
  FILE *input;
  char buf[16], cmd[PATH_MAX];
  struct stat stbuf;

  sleep (sleep_time);

  /* check cub_manager */
  (void) envvar_bindir_file (cmd, PATH_MAX, UTIL_CUB_MANAGER_NAME);
  if (stat (cmd, &stbuf) == -1)
    {
      /* Not install manager server */
      return MANAGER_SERVER_STATUS_ERROR;
    }

  strcat (cmd, " getpid");
  input = popen (cmd, "r");
  if (input == NULL)
    {
      /* Failed to run cub_manager process */
      return MANAGER_SERVER_STATUS_ERROR;
    }

  memset (buf, '\0', sizeof (buf));
  if (fgets (buf, 16, input) == NULL)
    {
      pclose (input);
      return MANAGER_SERVER_STOPPED;
    }
  if (atoi (buf) > 0)
    {
      pclose (input);
      return MANAGER_SERVER_RUNNING;
    }
  else
    {
      pclose (input);
      return MANAGER_SERVER_STATUS_ERROR;
    }
}

/*
 * process_manager -
 *
 * return:
 *
 *      command_type(in):
 *      cms_port(in):
 *
 */
static int
process_manager (int command_type, bool process_window_service)
{
  int cub_manager, status = NO_ERROR;
  char cub_manager_path[PATH_MAX];
  UTIL_MANAGER_SERVER_STATUS_E manager_status;
  struct stat stbuf;

  cub_manager_path[0] = '\0';

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_MANAGER_NAME, command_string (command_type));

  (void) envvar_bindir_file (cub_manager_path, PATH_MAX, UTIL_CUB_MANAGER_NAME);
  if (stat (cub_manager_path, &stbuf) == -1)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_MANAGER_NOT_INSTALLED);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_MANAGER_NOT_INSTALLED);
      return ER_GENERIC_ERROR;
    }
  manager_status = is_manager_running (0);
  if (manager_status == MANAGER_SERVER_STATUS_ERROR)
    {
      status = ER_GENERIC_ERROR;
      print_result (PRINT_MANAGER_NAME, status, command_type);
      return status;
    }

  switch (command_type)
    {
    case START:
      if (manager_status == MANAGER_SERVER_STOPPED)
	{
	  if (process_window_service)
	    {
#if defined(WINDOWS)
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_MANAGER,
		COMMAND_TYPE_START, NULL
	      };

	      status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	    }
	  else
	    {
	      const char *args[] = { UTIL_CUB_MANAGER_NAME, COMMAND_TYPE_START, NULL };
	      cub_manager = proc_execute (UTIL_CUB_MANAGER_NAME, args, false, false, false, NULL);
	    }

	  status = (is_manager_running (1) == MANAGER_SERVER_RUNNING) ? NO_ERROR : ER_GENERIC_ERROR;
	  print_result (PRINT_MANAGER_NAME, status, command_type);
	}
      else
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_MANAGER_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_MANAGER_NAME);
	  status = ER_GENERIC_ERROR;
	}
      break;
    case STOP:
      if (manager_status == MANAGER_SERVER_RUNNING)
	{
	  if (process_window_service)
	    {
#if defined(WINDOWS)
	      const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_MANAGER,
		COMMAND_TYPE_STOP, NULL
	      };

	      status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	    }
	  else
	    {
	      const char *args[] = { UTIL_CUB_MANAGER_NAME, COMMAND_TYPE_STOP, NULL };
	      status = proc_execute (UTIL_CUB_MANAGER_NAME, args, true, false, false, NULL);
	    }
	  status = (is_manager_running (1) == MANAGER_SERVER_STOPPED) ? NO_ERROR : ER_GENERIC_ERROR;
	  print_result (PRINT_MANAGER_NAME, status, command_type);
	}
      else
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MANAGER_NAME);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MANAGER_NAME);
	  status = ER_GENERIC_ERROR;
	}
      break;
    case STATUS:
      {
	switch (manager_status)
	  {
	  case MANAGER_SERVER_RUNNING:
	    print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_1S, PRINT_MANAGER_NAME);
	    break;
	  case MANAGER_SERVER_STOPPED:
	  default:
	    print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MANAGER_NAME);
	    break;
	  }
      }
      break;
    default:
      status = ER_GENERIC_ERROR;
      break;
    }

  return status;
}

/*
 * is_javasp_running -
 *
 * return:
 *
 */

static UTIL_JAVASP_SERVER_STATUS_E
is_javasp_running (const char *server_name)
{
  static const char PING_CMD[] = " ping ";
  static const int PING_CMD_LEN = sizeof (PING_CMD);

  FILE *input = NULL;
  char buf[PATH_MAX] = { 0 };
  char cmd[PATH_MAX] = { 0 };

  (void) envvar_bindir_file (cmd, PATH_MAX, UTIL_JAVASP_NAME);

  strcat (cmd, PING_CMD);
  strcat (cmd, server_name);

  input = popen (cmd, "r");
  if (input == NULL)
    {
      return JAVASP_SERVER_STATUS_ERROR;
    }

  memset (buf, '\0', sizeof (buf));
  if (fgets (buf, PATH_MAX, input) == NULL)
    {
      pclose (input);
      return JAVASP_SERVER_STATUS_ERROR;
    }

  if (strcmp (buf, server_name) == 0)
    {
      pclose (input);
      return JAVASP_SERVER_RUNNING;
    }
  else if (strcmp (buf, "NO_CONNECTION") == 0)
    {
      pclose (input);
      return JAVASP_SERVER_STOPPED;
    }
  else
    {
      pclose (input);
      return JAVASP_SERVER_STATUS_ERROR;
    }
}

static int
process_javasp_start (const char *db_name, bool suppress_message, bool process_window_service)
{
  static const int wait_timeout = 30;
  int waited_secs = 0;
  int pid = 0;
  int status = ER_GENERIC_ERROR;

  if (!suppress_message)
    {
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_3S, PRINT_JAVASP_NAME, PRINT_CMD_START, db_name);
    }

  UTIL_JAVASP_SERVER_STATUS_E javasp_status = is_javasp_running (db_name);
  if (javasp_status == JAVASP_SERVER_RUNNING)
    {
      if (!suppress_message)
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_JAVASP_NAME, db_name);
	}
      util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_JAVASP_NAME, db_name);
    }
  else
    {
      if (process_window_service)
	{
#if defined(WINDOWS)
	  const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_JAVASP,
	    COMMAND_TYPE_START, db_name, NULL
	  };
	  status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	}
      else
	{
	  const char *args[] = { UTIL_JAVASP_NAME, db_name, NULL };
	  status = proc_execute (UTIL_JAVASP_NAME, args, false, false, false, &pid);
	}

      status = ER_GENERIC_ERROR;
      while (status != NO_ERROR && waited_secs < wait_timeout)
	{
	  if (pid != 0 && is_terminated_process (pid))
	    {
	      break;
	    }

	  javasp_status = is_javasp_running (db_name);
	  if (javasp_status == JAVASP_SERVER_RUNNING)
	    {
	      status = NO_ERROR;
	      break;
	    }
	  else
	    {
	      sleep (1);	/* wait to start */
	      waited_secs++;

	      if (javasp_status == JAVASP_SERVER_STOPPED)
		{
		  util_log_write_errstr ("Waiting for javasp server to start... (%d / %d)\n", waited_secs,
					 wait_timeout);
		}
	      else
		{
		  if (waited_secs > 3)
		    {
		      /* invalid database name or failed to open info file, wait upto 3 seconds */
		      break;
		    }
		}
	    }
	}
      if (!suppress_message)
	{
	  print_result (PRINT_JAVASP_NAME, status, START);
	}
      else
	{
	  fprintf (stdout, "Calling java stored procedure %s allowed\n", (status == NO_ERROR) ? "is" : "is not");
	}
    }
  return status;
}

static int
process_javasp_stop (const char *db_name, bool suppress_message, bool process_window_service)
{
  int status = NO_ERROR;
  static const int wait_timeout = 5;
  int waited_secs = 0;

  if (!suppress_message)
    {
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_3S, PRINT_JAVASP_NAME, PRINT_CMD_STOP, db_name);
    }
  UTIL_JAVASP_SERVER_STATUS_E javasp_status = is_javasp_running (db_name);
  if (javasp_status == JAVASP_SERVER_RUNNING)
    {
      if (process_window_service)
	{
#if defined(WINDOWS)
	  const char *args[] = { UTIL_WIN_SERVICE_CONTROLLER_NAME, PRINT_CMD_JAVASP,
	    COMMAND_TYPE_STOP, db_name, NULL
	  };

	  status = proc_execute (UTIL_WIN_SERVICE_CONTROLLER_NAME, args, true, false, false, NULL);
#endif
	}
      else
	{
	  const char *args[] = { UTIL_JAVASP_NAME, COMMAND_TYPE_STOP, db_name, NULL };
	  status = proc_execute (UTIL_JAVASP_NAME, args, true, false, false, NULL);
	  do
	    {
	      status = (is_javasp_running (db_name) == JAVASP_SERVER_RUNNING) ? ER_GENERIC_ERROR : NO_ERROR;
	      sleep (1);	/* wait to stop */
	      waited_secs++;
	    }
	  while (status != NO_ERROR && waited_secs < wait_timeout);
	}
      if (!suppress_message)
	{
	  print_result (PRINT_JAVASP_NAME, status, STOP);
	}
    }
  else
    {
      status = ER_GENERIC_ERROR;
      if (!suppress_message)
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_JAVASP_NAME, db_name);
	}
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_JAVASP_NAME, db_name);
    }

  return status;
}

static int
process_javasp_status (const char *db_name)
{
  int status = NO_ERROR;
  UTIL_JAVASP_SERVER_STATUS_E javasp_status = is_javasp_running (db_name);
  if (javasp_status == JAVASP_SERVER_RUNNING)
    {
      const char *args[] = { UTIL_JAVASP_NAME, COMMAND_TYPE_STATUS, db_name, NULL };
      status = proc_execute (UTIL_JAVASP_NAME, args, true, false, false, NULL);
    }
  else
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_JAVASP_NAME, db_name);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_JAVASP_NAME, db_name);
    }

  return status;
}

static int
process_javasp (int command_type, int argc, const char **argv, bool show_usage, bool suppress_message,
		bool process_window_service, bool ha_mode)
{
  const int buf_size = 4096;
  char *buf = NULL;
  char *list = NULL, *save = NULL;
  const char *delim = " ,:";
  int status = NO_ERROR;
  char *db_name = NULL;
  int master_port = prm_get_master_port_id ();


  if (argc == 0)		/* cubrid service command */
    {
      /* get all server names from master request */
      if (css_does_master_exist (master_port))
	{
	  const char *server_type = (ha_mode) ? CHECK_HA_SERVER : CHECK_SERVER;
	  get_server_names (server_type, &buf);
	}
    }
  else				/* cubrid javasp command */
    {
      buf = (char *) calloc (sizeof (char), buf_size);
      strncpy (buf, argv[0], buf_size);
    }

  if (command_type != STATUS && buf == NULL)
    {
      if (show_usage)
	{
	  util_service_usage (JAVASP_UTIL);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_CMD);
	}
      status = ER_GENERIC_ERROR;
      goto exit;
    }

  if (command_type == STATUS && !suppress_message)
    {
      print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_JAVASP_NAME, PRINT_CMD_STATUS);
    }

  list = buf;
  while (buf)
    {
      db_name = strtok_r (list, delim, &save);
      if (db_name == NULL)
	{
	  break;
	}
      switch (command_type)
	{
	case START:
	  status = process_javasp_start (db_name, suppress_message, process_window_service);
	  break;
	case STOP:
	  status = process_javasp_stop (db_name, suppress_message, process_window_service);
	  break;
	case RESTART:
	  status = process_javasp_stop (db_name, suppress_message, process_window_service);
	  status = process_javasp_start (db_name, suppress_message, process_window_service);
	  break;
	case STATUS:
	  status = process_javasp_status (db_name);
	  break;
	default:
	  status = ER_GENERIC_ERROR;
	  break;
	}

      list = NULL;
    }

exit:
  if (buf)
    {
      free (buf);
    }

  return status;
}

static bool
ha_make_log_path (char *path, int size, char *base, char *db, char *node)
{
  return snprintf (path, size, "%s/%s_%s", base, db, node) >= 0;
}

static bool
ha_concat_db_and_host (char *db_host, int size, const char *db, const char *host)
{
  return snprintf (db_host, size, "%s@%s", db, host) >= 0;
}

#if defined(WINDOWS)
static bool
ha_mkdir (const char *path, int dummy)
{
  return false;
}
#else
static bool
ha_mkdir (const char *path, mode_t mode)
{
  char dir[PATH_MAX];
  struct stat statbuf;

  if (stat (path, &statbuf) == 0 && S_ISDIR (statbuf.st_mode))
    {
      return true;
    }

  cub_dirname_r (path, dir, PATH_MAX);
  if (stat (dir, &statbuf) == -1)
    {
      if (errno == ENOENT && ha_mkdir (dir, mode))
	{
	  return mkdir (path, mode) == 0;
	}
      else
	{
	  return false;
	}
    }
  else if (S_ISDIR (statbuf.st_mode))
    {
      return mkdir (path, mode) == 0;
    }

  return false;
}
#endif /* WINDOWS */

static bool
ha_make_mem_size (char *mem_size, int size, int value)
{
  return snprintf (mem_size, size, "--max-mem-size=%d", value) >= 0;
}

static bool
ha_is_registered (const char *args, const char *hostname)
{
  int status;
  const char *commdb_argv[] = {
    UTIL_COMMDB_NAME,		/* argv[0] */
    COMMDB_IS_REG,		/* argv[1] */
    args,			/* argv[2] : process args */
    NULL, NULL,			/* argv[3~4] : -h hostname */
    NULL			/* argv[5] */
  };

  if (args == NULL)
    {
      return false;
    }

  if (hostname)
    {
      commdb_argv[3] = COMMDB_HOST;	/* -h */
      commdb_argv[4] = hostname;	/* hostname */
      commdb_argv[5] = NULL;
    }

  status = proc_execute (UTIL_COMMDB_NAME, commdb_argv, true, false, false, NULL);
  if (status == NO_ERROR)
    {
      return true;
    }

  return false;
}

static int
ha_argv_to_args (char *args, int size, const char **argv, HB_PROC_TYPE type)
{
  int status = NO_ERROR;
  int ret = 0;

  if (type == HB_PTYPE_COPYLOGDB)
    {
      ret = snprintf (args, size, "%s %s %s %s %s %s %s ",	/* copylogdb */
		      argv[0],	/* UTIL_ADMIN_NAME : cub_admin */
		      argv[1],	/* UTIL_COPYLOGDB : copylogdb */
		      argv[2],	/* -L, --log-path=PATH */
		      argv[3],	/* PATH */
		      argv[4],	/* -m, --mode=MODE */
		      argv[5],	/* MODE */
		      argv[6]);	/* database-name */
    }
  else if (type == HB_PTYPE_APPLYLOGDB)
    {
      ret = snprintf (args, size, "%s %s %s %s %s %s ",	/* applylogdb */
		      argv[0],	/* UTIL_ADMIN_NAME : cub_admin */
		      argv[1],	/* UTIL_APPLYLOGDB : applylogdb */
		      argv[2],	/* -L, --log-path=PATH */
		      argv[3],	/* PATH */
		      argv[4],	/* --max-mem-size=SIZE */
		      argv[5]);	/* database-name */
    }
  else
    {
      assert (false);
      status = ER_GENERIC_ERROR;
    }
  if (ret < 0)
    {
      assert (false);
      status = ER_GENERIC_ERROR;
    }

  return status;
}

#if !defined(WINDOWS)
static int
us_hb_copylogdb_start (dynamic_array * out_ap, HA_CONF * ha_conf, const char *db_name, const char *node_name,
		       const char *remote_host)
{
  int status = NO_ERROR;
  int pid;
  int i, j, num_nodes;
  int num_db_found = 0, num_node_found = 0;
  HA_NODE_CONF *nc;
  char db_host[PATH_MAX], log_path[PATH_MAX];
  char **dbs;

  num_nodes = ha_conf->num_node_conf;
  dbs = ha_conf->db_names;
  nc = ha_conf->node_conf;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, UTIL_COPYLOGDB, PRINT_CMD_START);

  for (i = 0; dbs[i] != NULL; i++)
    {
      if (db_name != NULL && strcmp (dbs[i], db_name) != 0)
	{
	  continue;
	}
      num_db_found++;

      prm_set_integer_value (PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY, HA_MODE_FAIL_BACK);
      status = sysprm_load_and_init (dbs[i], NULL, SYSPRM_IGNORE_INTL_PARAMS);
      if (status != NO_ERROR)
	{
	  goto ret;
	}

      if (util_get_ha_mode_for_sa_utils () == HA_MODE_OFF)
	{
	  continue;
	}

      for (j = 0; j < num_nodes; j++)
	{
	  if (node_name != NULL && strcmp (nc[j].node_name, node_name) != 0)
	    {
	      continue;
	    }

	  if (remote_host == NULL)
	    {
	      if (util_is_localhost (nc[j].node_name))
		{
		  continue;
		}
	    }
	  else
	    {
	      if (node_name != NULL && strcmp (node_name, remote_host) == 0)
		{
		  continue;
		}
	    }
	  num_node_found++;

	  db_host[0] = log_path[0] = '\0';
	  (void) ha_concat_db_and_host (db_host, PATH_MAX, dbs[i], nc[j].node_name);
	  (void) ha_make_log_path (log_path, PATH_MAX, nc[j].copy_log_base, dbs[i], nc[j].node_name);
	  if (db_host[0] != '\0' && log_path[0] != '\0')
	    {
	      char lw_args[HB_MAX_SZ_PROC_ARGS];
	      const char *lw_argv[] = { UTIL_ADMIN_NAME, UTIL_COPYLOGDB,
		"-L", log_path,
		"-m", nc[j].copy_sync_mode,
		db_host,
		NULL
	      };

	      status = ha_argv_to_args (lw_args, sizeof (lw_args), lw_argv, HB_PTYPE_COPYLOGDB);
	      if (status != NO_ERROR)
		{
		  goto ret;
		}

	      if (remote_host == NULL)
		{
		  if (ha_is_registered (lw_args, NULL))
		    {
		      continue;
		    }

		  if (ha_mkdir (log_path, 0755))
		    {
		      status = proc_execute (UTIL_ADMIN_NAME, lw_argv, false, false, false, &pid);

		      if (status != NO_ERROR)
			{
			  goto ret;
			}

		      if (out_ap && da_add (out_ap, &pid) != NO_ERROR)
			{
			  status = ER_GENERIC_ERROR;
			  goto ret;
			}
		    }
		  else
		    {
		      status = ER_GENERIC_ERROR;
		      goto ret;
		    }
		}
	      else
		{
		  const char *commdb_argv[] = {
		    UTIL_COMMDB_NAME,
		    COMMDB_HA_START_UTIL_PROCESS, lw_args,
		    COMMDB_HOST, remote_host,
		    NULL,
		  };
		  status = proc_execute (UTIL_COMMDB_NAME, commdb_argv, true, false, false, &pid);
		  if (status != NO_ERROR)
		    {
		      goto ret;
		    }

		  if (out_ap && (da_add (out_ap, lw_args) != NO_ERROR))
		    {
		      status = ER_GENERIC_ERROR;
		      goto ret;
		    }
		}
	    }
	}
    }

  if (db_name != NULL && num_db_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      status = ER_GENERIC_ERROR;
      goto ret;
    }

  if (node_name != NULL && num_node_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      status = ER_GENERIC_ERROR;
    }

ret:
  print_result (UTIL_COPYLOGDB, status, START);
  return status;
}

static int
us_hb_copylogdb_stop (HA_CONF * ha_conf, const char *db_name, const char *node_name, const char *remote_host)
{
  int status = NO_ERROR;
  int i, j, num_nodes;
  int num_db_found = 0, num_node_found = 0;
  HA_NODE_CONF *nc;
  char db_host[PATH_MAX], log_path[PATH_MAX];
  char **dbs;
  int wait_time = 0;

  num_nodes = ha_conf->num_node_conf;
  dbs = ha_conf->db_names;
  nc = ha_conf->node_conf;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, UTIL_COPYLOGDB, PRINT_CMD_STOP);

  for (i = 0; dbs[i] != NULL; i++)
    {
      if (db_name != NULL && strcmp (dbs[i], db_name) != 0)
	{
	  continue;
	}
      num_db_found++;

      for (j = 0; j < num_nodes; j++)
	{
	  if (node_name != NULL && strcmp (nc[j].node_name, node_name) != 0)
	    {
	      continue;
	    }

	  if (remote_host == NULL)
	    {
	      if (util_is_localhost (nc[j].node_name))
		{
		  continue;
		}
	    }
	  else
	    {
	      if (node_name != NULL && strcmp (node_name, remote_host) == 0)
		{
		  continue;
		}
	    }
	  num_node_found++;

	  db_host[0] = log_path[0] = '\0';
	  (void) ha_concat_db_and_host (db_host, PATH_MAX, dbs[i], nc[j].node_name);
	  (void) ha_make_log_path (log_path, PATH_MAX, nc[j].copy_log_base, dbs[i], nc[j].node_name);
	  if (db_host[0] != '\0' && log_path[0] != '\0')
	    {
	      char lw_args[HB_MAX_SZ_PROC_ARGS];
	      const char *lw_argv[] = {
		UTIL_ADMIN_NAME, UTIL_COPYLOGDB,
		"-L", log_path,
		"-m", nc[j].copy_sync_mode, db_host, NULL
	      };

	      status = ha_argv_to_args (lw_args, sizeof (lw_args), lw_argv, HB_PTYPE_COPYLOGDB);
	      if (status != NO_ERROR)
		{
		  goto ret;
		}

	      if (remote_host != NULL || ha_is_registered (lw_args, remote_host))
		{
		  const char *commdb_argv[HB_MAX_SZ_PROC_ARGS] = {
		    UTIL_COMMDB_NAME, COMMDB_HA_DEREG_BY_ARGS, lw_args,
		    NULL, NULL,	/* -h hostname */
		    NULL
		  };

		  if (remote_host != NULL)
		    {
		      commdb_argv[3] = COMMDB_HOST;	/* -h */
		      commdb_argv[4] = remote_host;	/* hostname */
		      commdb_argv[5] = NULL;
		    }

		  status = proc_execute (UTIL_COMMDB_NAME, commdb_argv, true, false, false, NULL);
		  if (remote_host != NULL && status != NO_ERROR)
		    {
		      goto ret;
		    }

		  wait_time = 0;
		  do
		    {
		      if (ha_is_registered (lw_args, remote_host) == false)
			{
			  status = NO_ERROR;
			  break;
			}

		      sleep (3);
		      wait_time += 3;
		    }
		  while (wait_time < US_HB_DEREG_WAIT_TIME_IN_SEC);
		}
	    }
	  else
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }
	}
    }

  if (db_name != NULL && num_db_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      status = ER_GENERIC_ERROR;
      goto ret;
    }

  if (node_name != NULL && num_node_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      status = ER_GENERIC_ERROR;
    }

ret:
  print_result (UTIL_COPYLOGDB, status, STOP);
  return status;
}

static int
us_hb_applylogdb_start (dynamic_array * out_ap, HA_CONF * ha_conf, const char *db_name, const char *node_name,
			const char *remote_host)
{
  int status = NO_ERROR;
  int pid;
  int i, j, num_nodes;
  int num_db_found = 0, num_node_found = 0;
  HA_NODE_CONF *nc;
  char log_path[PATH_MAX], db_host[PATH_MAX], mem_size[PATH_MAX];
  char **dbs;

  num_nodes = ha_conf->num_node_conf;
  dbs = ha_conf->db_names;
  nc = ha_conf->node_conf;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, UTIL_APPLYLOGDB, PRINT_CMD_START);

  for (i = 0; dbs[i] != NULL; i++)
    {
      if (db_name != NULL && strcmp (dbs[i], db_name) != 0)
	{
	  continue;
	}
      num_db_found++;

      prm_set_integer_value (PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY, HA_MODE_FAIL_BACK);
      status = sysprm_load_and_init (dbs[i], NULL, SYSPRM_IGNORE_INTL_PARAMS);
      if (status != NO_ERROR)
	{
	  goto ret;
	}

      if (util_get_ha_mode_for_sa_utils () == HA_MODE_OFF)
	{
	  continue;
	}

      for (j = 0; j < num_nodes; j++)
	{
	  if (node_name != NULL && strcmp (nc[j].node_name, node_name) != 0)
	    {
	      continue;
	    }

	  if (remote_host == NULL)
	    {
	      if (util_is_localhost (nc[j].node_name))
		{
		  continue;
		}
	    }
	  else
	    {
	      if (node_name != NULL && strcmp (node_name, remote_host) == 0)
		{
		  continue;
		}
	    }

	  num_node_found++;

	  log_path[0] = db_host[0] = mem_size[0] = '\0';
	  (void) ha_concat_db_and_host (db_host, PATH_MAX, dbs[i], "localhost");
	  (void) ha_make_log_path (log_path, PATH_MAX, nc[j].copy_log_base, dbs[i], nc[j].node_name);
	  (void) ha_make_mem_size (mem_size, PATH_MAX, nc[j].apply_max_mem_size);

	  if (log_path[0] != '\0' && db_host[0] != '\0' && mem_size[0] != '\0')
	    {
	      char la_args[HB_MAX_SZ_PROC_ARGS];
	      const char *la_argv[] = {
		UTIL_ADMIN_NAME, UTIL_APPLYLOGDB,
		"-L", log_path, mem_size, db_host, NULL
	      };

	      status = ha_argv_to_args (la_args, sizeof (la_args), la_argv, HB_PTYPE_APPLYLOGDB);
	      if (status != NO_ERROR)
		{
		  goto ret;
		}

	      if (remote_host == NULL)
		{
		  if (ha_is_registered (la_args, NULL))
		    {
		      continue;
		    }

		  status = proc_execute (UTIL_ADMIN_NAME, la_argv, false, false, false, &pid);
		  if (status != NO_ERROR)
		    {
		      goto ret;
		    }

		  if (out_ap && da_add (out_ap, &pid) != NO_ERROR)
		    {
		      status = ER_GENERIC_ERROR;
		      goto ret;
		    }
		}
	      else
		{
		  const char *commdb_argv[] = {
		    UTIL_COMMDB_NAME,
		    COMMDB_HA_START_UTIL_PROCESS, la_args,
		    COMMDB_HOST, remote_host,
		    NULL,
		  };
		  status = proc_execute (UTIL_COMMDB_NAME, commdb_argv, true, false, false, &pid);
		  if (status != NO_ERROR)
		    {
		      goto ret;
		    }

		  if (out_ap && (da_add (out_ap, la_args) != NO_ERROR))
		    {
		      status = ER_GENERIC_ERROR;
		      goto ret;
		    }
		}
	    }
	  else
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }
	}
    }

  if (db_name != NULL && num_db_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      status = ER_GENERIC_ERROR;
      goto ret;
    }

  if (node_name != NULL && num_node_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      status = ER_GENERIC_ERROR;
    }

ret:
  print_result (UTIL_APPLYLOGDB, status, START);
  return status;
}

static int
us_hb_applylogdb_stop (HA_CONF * ha_conf, const char *db_name, const char *node_name, const char *remote_host)
{
  int status = NO_ERROR;
  int i, j, num_nodes;
  int num_db_found = 0, num_node_found = 0;
  HA_NODE_CONF *nc;
  char log_path[PATH_MAX], db_host[PATH_MAX], mem_size[PATH_MAX];
  char **dbs;
  int wait_time = 0;

  num_nodes = ha_conf->num_node_conf;
  dbs = ha_conf->db_names;
  nc = ha_conf->node_conf;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, UTIL_APPLYLOGDB, PRINT_CMD_STOP);

  for (i = 0; dbs[i] != NULL; i++)
    {
      if (db_name != NULL && strcmp (dbs[i], db_name) != 0)
	{
	  continue;
	}
      num_db_found++;

      for (j = 0; j < num_nodes; j++)
	{
	  if (node_name != NULL && strcmp (nc[j].node_name, node_name) != 0)
	    {
	      continue;
	    }

	  if (remote_host == NULL)
	    {
	      if (util_is_localhost (nc[j].node_name))
		{
		  continue;
		}
	    }
	  else
	    {
	      if (node_name != NULL && strcmp (node_name, remote_host) == 0)
		{
		  continue;
		}
	    }
	  num_node_found++;

	  log_path[0] = db_host[0] = mem_size[0] = '\0';
	  (void) ha_concat_db_and_host (db_host, PATH_MAX, dbs[i], "localhost");
	  (void) ha_make_log_path (log_path, PATH_MAX, nc[j].copy_log_base, dbs[i], nc[j].node_name);
	  (void) ha_make_mem_size (mem_size, PATH_MAX, nc[j].apply_max_mem_size);

	  if (log_path[0] != '\0' && db_host[0] != '\0' && mem_size[0] != '\0')
	    {
	      char la_args[HB_MAX_SZ_PROC_ARGS];
	      const char *la_argv[] = {
		UTIL_ADMIN_NAME, UTIL_APPLYLOGDB,
		"-L", log_path, mem_size, db_host, NULL
	      };

	      status = ha_argv_to_args (la_args, sizeof (la_args), la_argv, HB_PTYPE_APPLYLOGDB);
	      if (status != NO_ERROR)
		{
		  goto ret;
		}

	      if (remote_host != NULL || ha_is_registered (la_args, remote_host))
		{
		  const char *commdb_argv[HB_MAX_SZ_PROC_ARGS] = {
		    UTIL_COMMDB_NAME, COMMDB_HA_DEREG_BY_ARGS, la_args,
		    NULL, NULL,	/* -h hostname */
		    NULL
		  };

		  if (remote_host != NULL)
		    {
		      commdb_argv[3] = COMMDB_HOST;	/* -h */
		      commdb_argv[4] = remote_host;	/* hostname */
		      commdb_argv[5] = NULL;
		    }

		  status = proc_execute (UTIL_COMMDB_NAME, commdb_argv, true, false, false, NULL);
		  if (remote_host != NULL && status != NO_ERROR)
		    {
		      goto ret;
		    }

		  wait_time = 0;
		  do
		    {
		      if (ha_is_registered (la_args, remote_host) == false)
			{
			  status = NO_ERROR;
			  break;
			}

		      sleep (3);
		      wait_time += 3;
		    }
		  while (wait_time < US_HB_DEREG_WAIT_TIME_IN_SEC);
		}
	    }
	  else
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }
	}
    }

  if (db_name != NULL && num_db_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      status = ER_GENERIC_ERROR;
      goto ret;
    }

  if (node_name != NULL && num_node_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_NODE);
      status = ER_GENERIC_ERROR;
    }

ret:
  print_result (UTIL_APPLYLOGDB, status, STOP);
  return status;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
us_hb_utils_start (dynamic_array * pids, HA_CONF * ha_conf, const char *db_name, const char *node_name)
{
  int status = NO_ERROR;

  status = us_hb_copylogdb_start (pids, ha_conf, db_name, node_name, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  status = us_hb_applylogdb_start (pids, ha_conf, db_name, node_name, NULL);
  return status;
}

static int
us_hb_utils_stop (HA_CONF * ha_conf, const char *db_name, const char *node_name)
{
  int status = NO_ERROR;

  status = us_hb_copylogdb_stop (ha_conf, db_name, node_name, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  status = us_hb_applylogdb_stop (ha_conf, db_name, node_name, NULL);
  return status;
}
#endif /* ENABLE_UNUSED_FUNCTION */

static int
us_hb_server_start (HA_CONF * ha_conf, const char *db_name)
{
  int status = NO_ERROR;
  int i, num_db_found = 0;
  char **dbs;

  dbs = ha_conf->db_names;
  for (i = 0; dbs[i] != NULL; i++)
    {
      if (db_name != NULL && strcmp (db_name, dbs[i]))
	{
	  continue;
	}
      num_db_found++;

      if (!is_server_running (CHECK_SERVER, dbs[i], 0))
	{
	  prm_set_integer_value (PRM_ID_HA_MODE_FOR_SA_UTILS_ONLY, HA_MODE_FAIL_BACK);
	  status = sysprm_load_and_init (dbs[i], NULL, SYSPRM_IGNORE_INTL_PARAMS);
	  if (status != NO_ERROR)
	    {
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
	      print_result (PRINT_SERVER_NAME, status, START);
	      break;
	    }

	  if (util_get_ha_mode_for_sa_utils () == HA_MODE_OFF)
	    {
	      print_message (stderr, MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
	      print_result (PRINT_SERVER_NAME, status, START);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
	      continue;
	    }

	  status = process_server (START, 1, &(dbs[i]), true, false, false);
	  if (status != NO_ERROR)
	    {
	      return status;
	    }
	}
      else
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_SERVER_NAME, dbs[i]);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_ALREADY_RUNNING_2S, PRINT_SERVER_NAME, dbs[i]);
	}
    }

  if (db_name != NULL && num_db_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      status = ER_GENERIC_ERROR;
    }

  return status;
}

static int
us_hb_server_stop (HA_CONF * ha_conf, const char *db_name)
{
  int status = NO_ERROR;
  int i, num_db_found = 0;
  char **dbs;

  dbs = ha_conf->db_names;
  for (i = 0; dbs[i] != NULL; i++)
    {
      if (db_name != NULL && strcmp (db_name, dbs[i]))
	{
	  continue;
	}
      num_db_found++;

      if (is_server_running (CHECK_SERVER, dbs[i], 0))
	{
	  status = process_server (STOP, 1, &(dbs[i]), true, false, false);
	  if (status != NO_ERROR)
	    {
	      return status;
	    }
	}
      else
	{
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_SERVER_NAME, dbs[i]);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_2S, PRINT_SERVER_NAME, dbs[i]);
	}
    }

  if (db_name != NULL && num_db_found == 0)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_HA_MODE_NOT_LISTED_HA_DB);
      status = ER_GENERIC_ERROR;
    }

  return status;
}

static int
us_hb_process_start (HA_CONF * ha_conf, const char *db_name, bool check_result)
{
  int status = NO_ERROR;
  int i;
  int pid;
  dynamic_array *pids = NULL;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HA_PROCS_NAME, PRINT_CMD_START);

  pids = da_create (100, sizeof (int));
  if (pids == NULL)
    {
      status = ER_GENERIC_ERROR;
      goto ret;
    }

  status = us_hb_server_start (ha_conf, db_name);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  status = us_hb_copylogdb_start (pids, ha_conf, db_name, NULL, NULL);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  status = us_hb_applylogdb_start (pids, ha_conf, db_name, NULL, NULL);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  sleep (HB_START_WAITING_TIME_IN_SECS);
  if (check_result == true)
    {
      for (i = 0; i < da_size (pids); i++)
	{
	  da_get (pids, i, &pid);
	  if (is_terminated_process (pid))
	    {
	      status = ER_GENERIC_ERROR;
	      break;
	    }
	}
    }

ret:
  if (pids)
    {
      da_destroy (pids);
    }

  print_result (PRINT_HA_PROCS_NAME, status, START);
  return status;
}

/*
 * us_hb_deactivate - deactivate CUBRID heartbeat
 *    return:
 *
 *    hostname(in): target hostname
 *    immediate_stop(in): whether to stop immediately or not
 */
static int
us_hb_deactivate (const char *hostname, bool immediate_stop)
{
  int status = NO_ERROR;
  int opt_idx = 1;

  const char *args[] = {
    UTIL_COMMDB_NAME, NULL, NULL, NULL, NULL, NULL
  };

  if (hostname != NULL && hostname[0] != '\0')
    {
      args[opt_idx++] = COMMDB_HOST;
      args[opt_idx++] = hostname;
    }

  if (immediate_stop == true)
    {
      args[opt_idx++] = COMMDB_HB_DEACT_IMMEDIATELY;
    }

  /* stop javasp server */
  (void) process_javasp (STOP, 0, NULL, false, true, false, true);

  /* stop all HA processes including cub_server */
  args[opt_idx] = COMMDB_HA_DEACT_STOP_ALL;
  status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  /* wait until all processes are shutdown */
  args[opt_idx] = COMMDB_HA_DEACT_CONFIRM_STOP_ALL;
  while ((status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL)) != NO_ERROR)
    {
      if (status == EXIT_FAILURE)
	{
	  return status;
	}
      sleep (1);
    }

  /* start deactivation */
  args[opt_idx] = COMMDB_HA_DEACTIVATE;
  status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  /* wait until no cub_server processes are running */
  args[opt_idx] = COMMDB_HA_DEACT_CONFIRM_NO_SERVER;
  while ((status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL)) != NO_ERROR)
    {
      if (status == EXIT_FAILURE)
	{
	  return status;
	}
      sleep (1);
    }

  return NO_ERROR;
}

static int
us_hb_process_stop (HA_CONF * ha_conf, const char *db_name)
{
  int status = NO_ERROR;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HA_PROCS_NAME, PRINT_CMD_STOP);

  /* stop javasp server */
  (void) process_javasp (STOP, 1, (const char **) &db_name, false, true, false, true);

  status = us_hb_copylogdb_stop (ha_conf, db_name, NULL, NULL);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  status = us_hb_applylogdb_stop (ha_conf, db_name, NULL, NULL);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  status = us_hb_server_stop (ha_conf, db_name);

ret:
  print_result (PRINT_HA_PROCS_NAME, status, STOP);
  return status;
}

static int
us_hb_process_copylogdb (int command_type, HA_CONF * ha_conf, const char *db_name, const char *node_name,
			 const char *remote_host)
{
  int status = NO_ERROR;
  int i;
  int pid;
  dynamic_array *pids = NULL;
  dynamic_array *args = NULL;
  char arg[PATH_MAX];

  switch (command_type)
    {
    case START:
      if (remote_host == NULL)
	{
	  pids = da_create (100, sizeof (int));
	  if (pids == NULL)
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }

	  status = us_hb_copylogdb_start (pids, ha_conf, db_name, node_name, remote_host);

	  sleep (HB_START_WAITING_TIME_IN_SECS);
	  for (i = 0; i < da_size (pids); i++)
	    {
	      da_get (pids, i, &pid);
	      if (is_terminated_process (pid))
		{
		  (void) us_hb_copylogdb_stop (ha_conf, db_name, node_name, remote_host);
		  status = ER_GENERIC_ERROR;
		  break;
		}
	    }
	}
      else
	{
	  args = da_create (100, sizeof (arg));
	  if (args == NULL)
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }

	  status = us_hb_copylogdb_start (args, ha_conf, db_name, node_name, remote_host);

	  sleep (HB_START_WAITING_TIME_IN_SECS);
	  for (i = 0; i < da_size (args); i++)
	    {
	      da_get (args, i, arg);
	      if (ha_is_registered (arg, remote_host) == false)
		{
		  (void) us_hb_copylogdb_stop (ha_conf, db_name, node_name, remote_host);
		  status = ER_GENERIC_ERROR;
		  break;
		}
	    }
	}

      break;

    case STOP:
      status = us_hb_copylogdb_stop (ha_conf, db_name, node_name, remote_host);

      break;

    default:
      status = ER_GENERIC_ERROR;
      break;
    }

ret:
  if (pids)
    {
      da_destroy (pids);
    }
  if (args)
    {
      da_destroy (args);
    }

  return status;
}

static int
us_hb_process_applylogdb (int command_type, HA_CONF * ha_conf, const char *db_name, const char *node_name,
			  const char *remote_host)
{
  int status = NO_ERROR;
  int i, pid;
  dynamic_array *pids = NULL;
  dynamic_array *args = NULL;
  char arg[PATH_MAX];

  switch (command_type)
    {
    case START:
      if (remote_host == NULL)
	{
	  pids = da_create (100, sizeof (int));
	  if (pids == NULL)
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }

	  status = us_hb_applylogdb_start (pids, ha_conf, db_name, node_name, remote_host);

	  sleep (HB_START_WAITING_TIME_IN_SECS);
	  for (i = 0; i < da_size (pids); i++)
	    {
	      da_get (pids, i, &pid);
	      if (is_terminated_process (pid))
		{
		  (void) us_hb_applylogdb_stop (ha_conf, db_name, node_name, remote_host);
		  status = ER_GENERIC_ERROR;
		  break;
		}
	    }
	}
      else
	{
	  args = da_create (100, sizeof (arg));
	  if (args == NULL)
	    {
	      status = ER_GENERIC_ERROR;
	      goto ret;
	    }

	  status = us_hb_applylogdb_start (args, ha_conf, db_name, node_name, remote_host);

	  sleep (HB_START_WAITING_TIME_IN_SECS);
	  for (i = 0; i < da_size (args); i++)
	    {
	      da_get (args, i, arg);
	      if (ha_is_registered (arg, remote_host) == false)
		{
		  (void) us_hb_applylogdb_stop (ha_conf, db_name, node_name, remote_host);
		  status = ER_GENERIC_ERROR;
		  break;
		}
	    }
	}
      break;

    case STOP:
      status = us_hb_applylogdb_stop (ha_conf, db_name, node_name, remote_host);
      break;

    default:
      status = ER_GENERIC_ERROR;
      break;
    }

ret:
  if (pids)
    {
      da_destroy (pids);
    }
  if (args)
    {
      da_destroy (args);
    }

  return status;
}

#if defined (ENABLE_UNUSED_FUNCTION)
static int
us_hb_process_server (int command_type, HA_CONF * ha_conf, const char *db_name)
{
  int status = NO_ERROR;

  switch (command_type)
    {
    case START:
      status = us_hb_server_start (ha_conf, db_name);
      break;

    case STOP:
      status = us_hb_server_stop (ha_conf, db_name);
      break;

    default:
      status = ER_GENERIC_ERROR;
      break;
    }

  return status;
}
#endif /* !ENABLE_UNUSED_FUNCTION */
#endif /* !WINDOWS */


/*
 * us_hb_stop_get_options -
 * return: NO_ERROR or error code
 *
 *      db_name(out):
 *      db_name_size(in):
 *      remote_host_name(out):
 *      remote_host_name_size(in):
 *      immediate_stop(out):
 *      argc(in):
 *      argv(in):
 *
 */
static int
us_hb_stop_get_options (char *db_name, int db_name_size, char *remote_host_name, int remote_host_name_size,
			bool * immediate_stop, int argc, const char **argv)
{
  int status = NO_ERROR;
  int opt, opt_idx;
  char opt_str[64];
  int tmp_argc;
  const char **tmp_argv = NULL;
  size_t copy_len;

  const struct option hb_stop_opts[] = {
    {COMMDB_HB_DEACT_IMMEDIATELY_L, 0, 0, COMMDB_HB_DEACT_IMMEDIATELY_S},
    {COMMDB_HOST_L, 1, 0, COMMDB_HOST_S},
    {0, 0, 0, 0}
  };

  *db_name = *remote_host_name = '\0';
  *immediate_stop = false;

  /* dummy program name + user given argv */
  tmp_argc = argc + 1;

  /* +1 for null termination */
  tmp_argv = (const char **) malloc ((tmp_argc + 1) * sizeof (char *));
  if (tmp_argv == NULL)
    {
      status = ER_GENERIC_ERROR;
      print_message (stderr, MSGCAT_UTIL_GENERIC_NO_MEM);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto ret;
    }
  memset (tmp_argv, 0, (tmp_argc + 1) * sizeof (char *));

  tmp_argv[0] = PRINT_HEARTBEAT_NAME " " PRINT_CMD_STOP;
  memcpy (&tmp_argv[1], argv, argc * sizeof (char *));

  utility_make_getopt_optstring (hb_stop_opts, opt_str);
  while (1)
    {
      opt = getopt_long (tmp_argc, (char *const *) tmp_argv, opt_str, hb_stop_opts, &opt_idx);
      if (opt == -1)
	{
	  break;
	}

      switch (opt)
	{
	case COMMDB_HOST_S:
	  copy_len = strnlen (optarg, remote_host_name_size - 1);
	  memcpy (remote_host_name, optarg, copy_len);
	  remote_host_name[copy_len] = '\0';
	  break;
	case COMMDB_HB_DEACT_IMMEDIATELY_S:
	  *immediate_stop = true;
	  break;
	default:
	  status = ER_GENERIC_ERROR;
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  break;
	}
    }

  /* process non optional arguments */
  if (status == NO_ERROR && tmp_argc > optind)
    {
      if ((tmp_argc - optind) > 1)
	{
	  status = ER_GENERIC_ERROR;
	  print_message (stderr, MSGCAT_UTIL_GENERIC_ARGS_OVER, tmp_argv[optind + 1]);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_ARGS_OVER, tmp_argv[optind + 1]);

	  goto ret;
	}

      if ((tmp_argc - optind) == 1)
	{
	  copy_len = strnlen (tmp_argv[optind], db_name_size);
	  memcpy (db_name, tmp_argv[optind], copy_len);
	  db_name[copy_len] = '\0';
	}
    }

ret:
  if (tmp_argv)
    {
      free_and_init (tmp_argv);
    }

  return status;
}

/*
 * us_hb_status_get_options -
 * return: NO_ERROR or error code
 *
 *      verbose(out):
 *      remote_host_name(out):
 *      remote_host_name_size(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
us_hb_status_get_options (bool * verbose, char *remote_host_name, int remote_host_name_size, int argc,
			  const char **argv)
{
  int status = NO_ERROR;
  int opt, opt_idx;
  char opt_str[64];
  int tmp_argc;
  const char **tmp_argv = NULL;
  int copy_len;

  const struct option hb_status_opts[] = {
    {"verbose", 0, 0, 'v'},
    {COMMDB_HOST_L, 1, 0, COMMDB_HOST_S},
    {0, 0, 0, 0}
  };

  *verbose = false;
  *remote_host_name = '\0';

  /* dummy program name + user given argv */
  tmp_argc = argc + 1;

  /* +1 for null termination */
  tmp_argv = (const char **) malloc ((tmp_argc + 1) * sizeof (char *));
  if (tmp_argv == NULL)
    {
      status = ER_GENERIC_ERROR;
      print_message (stderr, MSGCAT_UTIL_GENERIC_NO_MEM);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto ret;
    }
  memset (tmp_argv, 0, (tmp_argc + 1) * sizeof (char *));

  tmp_argv[0] = PRINT_HEARTBEAT_NAME " " PRINT_CMD_STATUS;
  memcpy (&tmp_argv[1], argv, argc * sizeof (char *));

  utility_make_getopt_optstring (hb_status_opts, opt_str);
  while (1)
    {
      opt = getopt_long (tmp_argc, (char *const *) tmp_argv, opt_str, hb_status_opts, &opt_idx);
      if (opt == -1)
	{
	  break;
	}

      switch (opt)
	{
	case 'v':
	  *verbose = true;
	  break;
	case COMMDB_HOST_S:
	  copy_len = (int) strnlen (optarg, (size_t) (remote_host_name_size - 1));
	  memcpy (remote_host_name, optarg, copy_len);
	  remote_host_name[copy_len] = '\0';
	  break;
	default:
	  status = ER_GENERIC_ERROR;
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  break;
	}
    }

  /* process non optional arguments */
  if (status == NO_ERROR && tmp_argc > optind)
    {
      status = ER_GENERIC_ERROR;
      print_message (stderr, MSGCAT_UTIL_GENERIC_ARGS_OVER, tmp_argv[optind]);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_ARGS_OVER, tmp_argv[optind]);
      goto ret;
    }

ret:
  if (tmp_argv)
    {
      free_and_init (tmp_argv);
    }

  return status;
}

/*
 * us_hb_util_get_options -
 * return: NO_ERROR or error code
 *
 *      db_name(out):
 *      db_name_size(in):
 *      node_name(out):
 *      node_name_size(in):
 *      remote_host_name(out):
 *      remote_host_name_size(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
us_hb_util_get_options (char *db_name, int db_name_size, char *node_name, int node_name_size, char *remote_host_name,
			int remote_host_name_size, int argc, const char **argv)
{
  int status = NO_ERROR;
  int opt, opt_idx;
  char opt_str[64];
  int tmp_argc;
  const char **tmp_argv = NULL;
  size_t copy_len;

  const struct option hb_util_opts[] = {
    {COMMDB_HOST_L, 1, 0, COMMDB_HOST_S},
    {0, 0, 0, 0}
  };

  if (argc < 0)
    {
      util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      return ER_GENERIC_ERROR;
    }

  *db_name = *node_name = *remote_host_name = '\0';

  /* dummy program name + user given argv */
  tmp_argc = argc + 1;

  /* +1 for null termination */
  tmp_argv = (const char **) malloc ((tmp_argc + 1) * sizeof (char *));
  if (tmp_argv == NULL)
    {
      status = ER_GENERIC_ERROR;
      print_message (stderr, MSGCAT_UTIL_GENERIC_NO_MEM);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NO_MEM);
      goto ret;
    }
  memset (tmp_argv, 0, (tmp_argc + 1) * sizeof (char *));

  tmp_argv[0] = PRINT_HEARTBEAT_NAME " " "copylogdb/applylogdb/prefetchlogdb";
  memcpy (&tmp_argv[1], argv, argc * sizeof (char *));

  utility_make_getopt_optstring (hb_util_opts, opt_str);
  while (1)
    {
      opt = getopt_long (tmp_argc, (char *const *) tmp_argv, opt_str, hb_util_opts, &opt_idx);
      if (opt == -1)
	{
	  break;
	}

      switch (opt)
	{
	case COMMDB_HOST_S:
	  copy_len = strnlen (optarg, remote_host_name_size);
	  memcpy (remote_host_name, optarg, copy_len);
	  remote_host_name[copy_len] = '\0';
	  break;
	default:
	  status = ER_GENERIC_ERROR;
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
	  break;
	}
    }

  /* process non optional arguments */
  if (status == NO_ERROR)
    {
      if ((tmp_argc - optind) > 2)
	{
	  status = ER_GENERIC_ERROR;
	  print_message (stderr, MSGCAT_UTIL_GENERIC_ARGS_OVER, tmp_argv[tmp_argc - 1]);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_ARGS_OVER, tmp_argv[tmp_argc - 1]);
	  goto ret;
	}
      else if ((tmp_argc - optind) < 2)
	{
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
	  goto ret;
	}

      copy_len = strnlen (tmp_argv[optind], db_name_size - 1);
      memcpy (db_name, tmp_argv[optind], copy_len);
      db_name[copy_len] = '\0';

      copy_len = strnlen (tmp_argv[optind + 1], node_name_size - 1);
      memcpy (node_name, tmp_argv[optind + 1], copy_len);
      node_name[copy_len] = '\0';
    }

ret:
  if (tmp_argv)
    {
      free_and_init (tmp_argv);
    }

  return status;
}


#if !defined(WINDOWS)

/*
 * process_heartbeat_start -
 *
 * return:
 *
 *      ha_conf(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_start (HA_CONF * ha_conf, int argc, const char **argv)
{
  int status = NO_ERROR;
  int master_port;
  const char *db_name = NULL;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HEARTBEAT_NAME, PRINT_CMD_START);

  master_port = prm_get_master_port_id ();
  if (!css_does_master_exist (master_port))
    {
      status = process_master (START);

      if (status != NO_ERROR)
	{
	  goto ret;
	}
    }

  if (css_does_master_exist (master_port))
    {
      const char *args[] = {
	UTIL_COMMDB_NAME, COMMDB_HA_ACTIVATE, NULL
      };
      status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
    }

  if (status != NO_ERROR)
    {
      goto ret;
    }

  db_name = (argc >= 1) ? argv[0] : NULL;
  if (db_name != NULL)
    {
      status = sysprm_load_and_init (db_name, NULL, SYSPRM_IGNORE_INTL_PARAMS);
      if (status != NO_ERROR)
	{
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
	  goto ret;
	}

      if (util_get_ha_mode_for_sa_utils () == HA_MODE_OFF)
	{
	  status = ER_GENERIC_ERROR;
	  print_message (stderr, MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
	  util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
	  goto ret;
	}
    }

  status = us_hb_process_start (ha_conf, db_name, true);
  if (status != NO_ERROR)
    {
      if (db_name == NULL)
	{
	  (void) us_hb_deactivate (NULL, false);
	}
      else
	{
	  (void) us_hb_process_stop (ha_conf, db_name);
	}
    }

ret:
  print_result (PRINT_HEARTBEAT_NAME, status, START);
  return status;
}

/*
 * process_heartbeat_stop -
 *
 * return:
 *
 *      ha_conf(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_stop (HA_CONF * ha_conf, int argc, const char **argv)
{
  int status = NO_ERROR;
  int master_port;
  char db_name[64] = "";	/* DBNAME_LEN */
  char remote_host_name[CUB_MAXHOSTNAMELEN] = "";
  bool immediate_stop = false;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HEARTBEAT_NAME, PRINT_CMD_STOP);

  status =
    us_hb_stop_get_options (db_name, sizeof (db_name), remote_host_name, sizeof (remote_host_name), &immediate_stop,
			    argc, argv);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  /* validate options */
  if ((db_name[0] != '\0') && (remote_host_name[0] != '\0' || immediate_stop == true))
    {
      status = ER_GENERIC_ERROR;
      print_message (stderr, MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      goto ret;
    }

  master_port = prm_get_master_port_id ();
  if (remote_host_name[0] != '\0' || css_does_master_exist (master_port) == true)
    {
      if (db_name[0] != '\0')
	{
	  status = sysprm_load_and_init (db_name, NULL, SYSPRM_IGNORE_INTL_PARAMS);
	  if (status != NO_ERROR)
	    {
	      goto ret;
	    }

	  if (util_get_ha_mode_for_sa_utils () == HA_MODE_OFF)
	    {
	      status = ER_GENERIC_ERROR;
	      print_message (stderr, MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
	      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
	      goto ret;
	    }

	  status = us_hb_process_stop (ha_conf, db_name);
	}
      else
	{
	  status = us_hb_deactivate (remote_host_name, immediate_stop);
	}
    }
  else
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
    }

  if (status == NO_ERROR)
    {
      /* wait for cub_master to clean up its internal resources */
      sleep (HB_STOP_WAITING_TIME_IN_SECS);
    }

ret:
  print_result (PRINT_HEARTBEAT_NAME, status, STOP);
  return status;
}
#endif

/*
 * process_heartbeat_deregister -
 *
 * return:
 *
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_deregister (int argc, const char **argv)
{
  int status = NO_ERROR;
  int master_port;
  const char *pid = NULL;

  if (argc < 1)
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      goto ret;
    }

  pid = (char *) argv[0];
  if (pid == NULL)
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      goto ret;
    }

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_3S, PRINT_HEARTBEAT_NAME, PRINT_CMD_DEREG, pid);

  master_port = prm_get_master_port_id ();
  if (css_does_master_exist (master_port))
    {
      const char *commdb_argv[] = {
	UTIL_COMMDB_NAME, COMMDB_HA_DEREG_BY_PID, pid, NULL
      };
      status = proc_execute (UTIL_COMMDB_NAME, commdb_argv, true, false, false, NULL);
    }
  else
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
    }

ret:
  print_result (PRINT_HEARTBEAT_NAME, status, DEREGISTER);
  return status;
}

/*
 * process_heartbeat_status -
 *
 * return:
 *
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_status (int argc, const char **argv)
{
  int status = NO_ERROR;
  int master_port;
  bool verbose;
  char remote_host_name[CUB_MAXHOSTNAMELEN];
  int ext_opt_offset;

  const char *node_list_argv[] = {
    UTIL_COMMDB_NAME, COMMDB_HA_NODE_LIST,
    NULL, NULL, NULL,		/* -v and/or -h hostname */
    NULL
  };
  const char *proc_list_argv[] = {
    UTIL_COMMDB_NAME, COMMDB_HA_PROC_LIST,
    NULL, NULL, NULL,		/* -v and/or -h hostname */
    NULL
  };
  const char *ping_host_list_argv[] = {
    UTIL_COMMDB_NAME, COMMDB_HA_PING_HOST_LIST,
    NULL, NULL, NULL,		/* -v and/or -h hostname */
    NULL
  };
  const char *admin_info_argv[] = {
    UTIL_COMMDB_NAME, COMMDB_HA_ADMIN_INFO,
    NULL, NULL, NULL,		/* -v and/or -h hostname */
    NULL
  };

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HEARTBEAT_NAME, PRINT_CMD_STATUS);

  verbose = false;
  memset (remote_host_name, 0, sizeof (remote_host_name));
  status = us_hb_status_get_options (&verbose, remote_host_name, sizeof (remote_host_name), argc, argv);
  if (status != NO_ERROR)
    {
      return status;
    }

  ext_opt_offset = 2;

  if (remote_host_name[0] != '\0')
    {
      node_list_argv[ext_opt_offset] = COMMDB_HOST;	/* -h */
      proc_list_argv[ext_opt_offset] = COMMDB_HOST;	/* -h */
      ping_host_list_argv[ext_opt_offset] = COMMDB_HOST;	/* -h */
      admin_info_argv[ext_opt_offset] = COMMDB_HOST;	/* -h */
      ext_opt_offset++;

      node_list_argv[ext_opt_offset] = remote_host_name;	/* hostname */
      proc_list_argv[ext_opt_offset] = remote_host_name;	/* hostname */
      ping_host_list_argv[ext_opt_offset] = remote_host_name;	/* hostname */
      admin_info_argv[ext_opt_offset] = remote_host_name;	/* hostname */
      ext_opt_offset++;
    }
  else
    {
      master_port = prm_get_master_port_id ();
      if (css_does_master_exist (master_port) == false)
	{
	  status = ER_GENERIC_ERROR;
	  print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
	  return status;
	}
    }

  if (verbose == true)
    {
      node_list_argv[ext_opt_offset] = COMMDB_VERBOSE_OUTPUT;
      proc_list_argv[ext_opt_offset] = COMMDB_VERBOSE_OUTPUT;
    }

  status = proc_execute (UTIL_COMMDB_NAME, node_list_argv, true, false, false, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  status = proc_execute (UTIL_COMMDB_NAME, proc_list_argv, true, false, false, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  status = proc_execute (UTIL_COMMDB_NAME, ping_host_list_argv, true, false, false, NULL);
  if (status != NO_ERROR)
    {
      return status;
    }

  if (verbose == true)
    {
      status = proc_execute (UTIL_COMMDB_NAME, admin_info_argv, true, false, false, NULL);
    }

  return status;
}

/*
 * process_heartbeat_reload -
 *
 * return:
 *
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_reload (int argc, const char **argv)
{
  int status = NO_ERROR;
  int master_port;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HEARTBEAT_NAME, PRINT_CMD_RELOAD);

  master_port = prm_get_master_port_id ();
  if (css_does_master_exist (master_port))
    {
      const char *args[] = {
	UTIL_COMMDB_NAME, COMMDB_HA_RELOAD, NULL
      };
      status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
    }
  else
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
    }

  print_result (PRINT_HEARTBEAT_NAME, status, RELOAD);
  return status;
}

#if !defined(WINDOWS)
/*
 * process_heartbeat_util -
 *
 * return:
 *
 *      ha_conf(in):
 *      command_type(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_util (HA_CONF * ha_conf, int command_type, int argc, const char **argv)
{
  int status = NO_ERROR;
  int sub_command_type;

  char db_name[64];
  char node_name[CUB_MAXHOSTNAMELEN];
  char host_name[CUB_MAXHOSTNAMELEN];
  char *db_name_p, *node_name_p, *host_name_p;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HEARTBEAT_NAME, command_string (command_type));

  sub_command_type = parse_arg (us_Command_map, (char *) argv[0]);

  db_name[0] = node_name[0] = host_name[0] = '\0';
  status =
    us_hb_util_get_options (db_name, sizeof (db_name), node_name, sizeof (node_name), host_name, sizeof (host_name),
			    (argc - 1), (argv + 1));
  if (status != NO_ERROR)
    {
      goto ret;
    }

  db_name_p = (db_name[0] == '\0') ? NULL : db_name;
  node_name_p = (node_name[0] == '\0') ? NULL : node_name;
  host_name_p = (host_name[0] == '\0') ? NULL : host_name;

  assert (db_name_p != NULL);

  status = sysprm_load_and_init (db_name_p, NULL, SYSPRM_IGNORE_INTL_PARAMS);
  if (status != NO_ERROR)
    {
      goto ret;
    }

  if (util_get_ha_mode_for_sa_utils () == HA_MODE_OFF)
    {
      status = ER_GENERIC_ERROR;
      print_message (stderr, MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
      goto ret;
    }

  switch (command_type)
    {
    case SC_COPYLOGDB:
      status = us_hb_process_copylogdb (sub_command_type, ha_conf, db_name_p, node_name_p, host_name_p);
      break;
    case SC_APPLYLOGDB:
      status = us_hb_process_applylogdb (sub_command_type, ha_conf, db_name_p, node_name_p, host_name_p);
      break;
    }

ret:
  print_result (PRINT_HEARTBEAT_NAME, status, command_type);
  return status;
}

/*
 * process_heartbeat_replication -
 *
 * return:
 *
 *      ha_conf(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat_replication (HA_CONF * ha_conf, int argc, const char **argv)
{
  int status = NO_ERROR;
  int sub_command_type;
  int master_port;
  const char *node_name = NULL;

  print_message (stdout, MSGCAT_UTIL_GENERIC_START_STOP_2S, PRINT_HEARTBEAT_NAME, PRINT_CMD_REPLICATION);

  if (argc < 2)
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_MISS_ARGUMENT);
      goto ret;
    }

  sub_command_type = parse_arg (us_Command_map, (char *) argv[0]);
  node_name = argv[1];

  if ((sub_command_type != START && sub_command_type != STOP) || node_name == NULL || node_name[0] == '\0')
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);
      goto ret;
    }

  master_port = prm_get_master_port_id ();
  if (css_does_master_exist (master_port))
    {
      const char *args[] = {
	UTIL_COMMDB_NAME, COMMDB_HA_ACTIVATE, NULL
      };
      status = proc_execute (UTIL_COMMDB_NAME, args, true, false, false, NULL);
      if (status == NO_ERROR)
	{
	  if (sub_command_type == START)
	    {
	      status = us_hb_process_copylogdb (START, ha_conf, NULL, node_name, NULL);
	      if (status == NO_ERROR)
		{
		  status = us_hb_process_applylogdb (START, ha_conf, NULL, node_name, NULL);
		  if (status != NO_ERROR)
		    {
		      (void) us_hb_process_copylogdb (STOP, ha_conf, NULL, node_name, NULL);
		    }
		}
	    }
	  else
	    {
	      (void) us_hb_process_copylogdb (STOP, ha_conf, NULL, node_name, NULL);
	      (void) us_hb_process_applylogdb (STOP, ha_conf, NULL, node_name, NULL);
	      status = NO_ERROR;
	    }
	}
    }
  else
    {
      status = ER_GENERIC_ERROR;
      print_message (stdout, MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_RUNNING_1S, PRINT_MASTER_NAME);
    }

ret:
  print_result (PRINT_HEARTBEAT_NAME, status, REPLICATION);
  return status;
}
#endif

/*
 * process_heartbeat -
 *
 * return:
 *
 *      command_type(in):
 *      argc(in):
 *      argv(in):
 *
 */
static int
process_heartbeat (int command_type, int argc, const char **argv)
{
  int status = NO_ERROR;
#if !defined(WINDOWS)
  HA_CONF ha_conf;

  if (ha_mode_in_common == HA_MODE_OFF)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_NOT_HA_MODE);
      print_result (PRINT_HEARTBEAT_NAME, ER_FAILED, command_type);
      return ER_FAILED;
    }

  memset ((void *) &ha_conf, 0, sizeof (HA_CONF));
  status = util_make_ha_conf (&ha_conf);
  if (status != NO_ERROR)
    {
      print_message (stderr, MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      util_log_write_errid (MSGCAT_UTIL_GENERIC_SERVICE_PROPERTY_FAIL);
      print_result (PRINT_HEARTBEAT_NAME, ER_FAILED, command_type);
      return ER_FAILED;
    }

  switch (command_type)
    {
    case START:
      status = process_heartbeat_start (&ha_conf, argc, argv);
      break;
    case STOP:
      status = process_heartbeat_stop (&ha_conf, argc, argv);
      break;
    case DEREGISTER:
      status = process_heartbeat_deregister (argc, argv);
      break;
    case STATUS:
    case LIST:
      status = process_heartbeat_status (argc, argv);
      break;
    case RELOAD:
      status = process_heartbeat_reload (argc, argv);
      break;
    case SC_COPYLOGDB:
    case SC_APPLYLOGDB:
      status = process_heartbeat_util (&ha_conf, command_type, argc, argv);
      break;
    case REPLICATION:
      status = process_heartbeat_replication (&ha_conf, argc, argv);
      break;
    default:
      status = ER_GENERIC_ERROR;
      break;
    }

ret:
  util_free_ha_conf (&ha_conf);
  return status;
#else /* !WINDOWS */

  status = ER_FAILED;
  print_result (PRINT_HEARTBEAT_NAME, status, command_type);
  return status;
#endif /* WINDOWS */
}

/*
 * parse_arg -
 *
 * return:
 *
 *      option(in):
 *      arg(in):
 *
 */
static int
parse_arg (UTIL_SERVICE_OPTION_MAP_T * option, const char *arg)
{
  int i;

  if (arg == NULL || arg[0] == 0)
    {
      return ER_GENERIC_ERROR;
    }
  for (i = 0; option[i].option_type != -1; i++)
    {
      if (strcasecmp (option[i].option_name, arg) == 0)
	{
	  return option[i].option_type;
	}
    }
  return ER_GENERIC_ERROR;
}

/*
 * load_properties -
 *
 * return:
 *
 * NOTE:
 */
static int
load_properties (void)
{
  bool server_flag = false;
  bool broker_flag = false;
  bool gateway_flag = false;
  bool manager_flag = false;
  bool heartbeat_flag = false;
  bool javasp_flag = false;

  char *value = NULL;

  if (sysprm_load_and_init (NULL, NULL, SYSPRM_IGNORE_INTL_PARAMS) != NO_ERROR)
    {
      return ER_GENERIC_ERROR;
    }

  /* get service::service list */
  value = prm_get_string_value (PRM_ID_SERVICE_SERVICE_LIST);
  if (value != NULL)
    {
      const char *error_msg;
      char *util = NULL, *save_ptr = NULL;
      for (util = value;; util = NULL)
	{
	  util = strtok_r (util, " \t,", &save_ptr);
	  if (util == NULL)
	    {
	      break;
	    }

	  if (strcmp (util, UTIL_TYPE_SERVER) == 0)
	    {
	      server_flag = true;
	    }
	  else if (strcmp (util, UTIL_TYPE_BROKER) == 0)
	    {
	      broker_flag = true;
	    }
	  else if (strcmp (util, UTIL_TYPE_MANAGER) == 0)
	    {
	      manager_flag = true;
	    }
	  else if (strcmp (util, UTIL_TYPE_HEARTBEAT) == 0)
	    {
	      heartbeat_flag = true;
	    }
	  else if (strcmp (util, UTIL_TYPE_GATEWAY) == 0)
	    {
	      gateway_flag = true;
	    }
	  else
	    {
	      error_msg = utility_get_generic_message (MSGCAT_UTIL_GENERIC_INVALID_PARAMETER);
	      fprintf (stderr, error_msg, "service", util);
	      return ER_GENERIC_ERROR;
	    }
	}
    }

  us_Property_map[SERVICE_START_SERVER].property_value = strdup (server_flag ? PROPERTY_ON : PROPERTY_OFF);
  us_Property_map[SERVICE_START_BROKER].property_value = strdup (broker_flag ? PROPERTY_ON : PROPERTY_OFF);
  us_Property_map[SERVICE_START_GATEWAY].property_value = strdup (gateway_flag ? PROPERTY_ON : PROPERTY_OFF);
  us_Property_map[SERVICE_START_MANAGER].property_value = strdup (manager_flag ? PROPERTY_ON : PROPERTY_OFF);
  us_Property_map[SERVICE_START_HEARTBEAT].property_value = strdup (heartbeat_flag ? PROPERTY_ON : PROPERTY_OFF);

  /* get service::server list */
  value = prm_get_string_value (PRM_ID_SERVICE_SERVER_LIST);
  if (value != NULL)
    {
      us_Property_map[SERVER_START_LIST].property_value = strdup (value);
      if (us_Property_map[SERVER_START_LIST].property_value == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) (strlen (value) + 1));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }
  else
    {
      us_Property_map[SERVER_START_LIST].property_value = NULL;
    }

  return NO_ERROR;
}

/*
 * finalize_properties - free allocated memory by strdup ()
 *
 * return:
 *
 */
static void
finalize_properties (void)
{
  int i;

  for (i = 0; us_Property_map[i].property_index != -1; i++)
    {
      if (us_Property_map[i].property_value != NULL)
	{
	  free_and_init (us_Property_map[i].property_value);
	}
    }
}

/*
 * get_property -
 *
 * return:
 *
 *      property_type(in):
 *
 * NOTE:
 */
static const char *
get_property (int property_type)
{
  return us_Property_map[property_type].property_value;
}

/*
 * is_terminated_process() - test if the process is terminated
 *   return: true if the process is terminated, otherwise false
 *   pid(in): process id
 */
static bool
is_terminated_process (const int pid)
{
#if defined(WINDOWS)
  HANDLE h_process;

  h_process = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (h_process == NULL)
    {
      return true;
    }
  else
    {
      CloseHandle (h_process);
      return false;
    }
#else /* WINDOWS */
  if (kill (pid, 0) == -1)
    {
      return true;
    }
  else
    {
      return false;
    }
#endif /* WINDOWS */
}
