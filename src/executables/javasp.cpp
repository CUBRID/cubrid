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
 * javasp.cpp - utility java stored procedure server main routine
 *
 */

#ident "$Id$"

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#else /* not WINDOWS */
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <io.h>
#endif /* not WINDOWS */

#include "environment_variable.h"
#include "system_parameter.h"
#include "error_code.h"
#include "error_manager.h"
#include "message_catalog.h"
#include "utility.h"
#include "databases_file.h"
#include "object_representation.h"

#include "jsp_comm.h"
#include "jsp_file.h"
#include "jsp_sr.h"

#include <string>
#include <algorithm>
#include <array>

#define JAVASP_PING_LEN   PATH_MAX

#define JAVASP_PRINT_ERR_MSG(...) \
  do {\
      fprintf (stderr, __VA_ARGS__);\
  }while (0)

#if defined(WINDOWS)
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif

static int javasp_start_server (const JAVASP_SERVER_INFO jsp_info, const std::string &db_name, const std::string &path);
static int javasp_stop_server (const JAVASP_SERVER_INFO jsp_info, const std::string &db_name);
static int javasp_status_server (const JAVASP_SERVER_INFO jsp_info);

static void javasp_dump_status (FILE *fp, JAVASP_STATUS_INFO status_info);
static int javasp_ping_server (const int server_port, char *buf);
static bool javasp_is_running (const int server_port, const std::string &db_name);

static bool javasp_is_terminated_process (int pid);
static void javasp_terminate_process (int pid);

static int javasp_get_server_info (const std::string &db_name, JAVASP_SERVER_INFO &info);
static int javasp_check_argument (int argc, char *argv[], std::string &command, std::string &db_name);
static int javasp_check_database (const std::string &db_name, std::string &db_path);

/*
 * main() - javasp main function
 */

int
main (int argc, char *argv[])
{
  int status = NO_ERROR;
  FILE *redirect = NULL; /* for ping */
  std::string command, db_name;

#if defined(WINDOWS)
  FARPROC jsp_old_hook = NULL;
#else
  if (os_set_signal_handler (SIGPIPE, SIG_IGN) == SIG_ERR)
    {
      return ER_GENERIC_ERROR;
    }
#endif /* WINDOWS */
  {
    /*
    * COMMON PART FOR PING AND OTHER COMMANDS
    */

    // supress error message
    er_init (NULL_DEVICE, ER_NEVER_EXIT);

    /* check arguments, get command and database name */
    status = javasp_check_argument (argc, argv, command, db_name);
    if (status != NO_ERROR)
      {
	return ER_GENERIC_ERROR;
      }

    /* check database exists and get path name of database */
    std::string pathname;
    status = javasp_check_database (db_name, pathname);
    if (status != NO_ERROR)
      {
	goto exit;
      }

    /* initialize error manager for command */
    if (command.compare ("ping") != 0)
      {
	/* finalize supressing error message for ping */
	er_final (ER_ALL_FINAL);

	/* error message log file */
	char er_msg_file[PATH_MAX];
	snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_java.err", db_name.c_str ());
	er_init (er_msg_file, ER_NEVER_EXIT);
      }

    /* try to create info dir and get absolute path for info file; $CUBRID/var/javasp_<db_name>.info */
    JAVASP_SERVER_INFO jsp_info = {-1, -1};
    status = javasp_get_server_info (db_name, jsp_info);
    if (status != NO_ERROR && command.compare ("start") != 0)
      {
	char info_path[PATH_MAX], err_msg[PATH_MAX + 32];
	javasp_get_info_file (info_path, PATH_MAX, db_name.c_str ());
	snprintf (err_msg, sizeof (err_msg), "Error while opening file (%s)", info_path);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, err_msg);
	goto exit;
      }

#if defined(WINDOWS)
    // socket startup for windows
    windows_socket_startup (jsp_old_hook);
#endif /* WINDOWS */

    /*
    * PROCESS PING
    */
    if (command.compare ("ping") == 0)
      {
	// redirect stderr
	if ((redirect = freopen (NULL_DEVICE, "w", stderr)) == NULL)
	  {
	    assert (false);
	    return ER_GENERIC_ERROR;
	  }

	char buffer[JAVASP_PING_LEN] = {0};
	if ((status = javasp_ping_server (jsp_info.port, buffer)) == NO_ERROR)
	  {
	    fprintf (stdout, "%s", buffer);
	  }
	else
	  {
	    fprintf (stdout, "NO_CONNECTION");
	  }
	return status;
      }

    /*
    * BEGIN TO PROCESS FOR OTHER COMMANDS
    */

    // load system parameter
    sysprm_load_and_init (db_name.c_str (), NULL, SYSPRM_IGNORE_INTL_PARAMS);

    /* javasp command main routine */
    if (command.compare ("start") == 0)
      {
	// check java stored procedure is not enabled
	if (prm_get_bool_value (PRM_ID_JAVA_STORED_PROCEDURE) == false)
	  {
	    char err_msg[PATH_MAX];
	    snprintf (err_msg, PATH_MAX, "%s system parameter is not enabled", prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE));
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, err_msg);
	    status = ER_SP_CANNOT_START_JVM;
	    goto exit;
	  }

	status = javasp_start_server (jsp_info, db_name, pathname);
	if (status == NO_ERROR)
	  {
	    while (true)
	      {
		SLEEP_MILISEC (0, 100);
	      }
	  }
      }
    else if (command.compare ("stop") == 0)
      {
	status = javasp_stop_server (jsp_info, db_name);
      }
    else if (command.compare ("status") == 0)
      {
	status = javasp_status_server (jsp_info);
      }
    else
      {
	JAVASP_PRINT_ERR_MSG ("Invalid command: %s\n", command.c_str ());
	status = ER_GENERIC_ERROR;
      }

#if defined(WINDOWS)
    // socket shutdown for windows
    windows_socket_shutdown (jsp_old_hook);
#endif /* WINDOWS */
  }

exit:

  if (command.compare ("ping") == 0)
    {
      if (status != NO_ERROR)
	{
	  fprintf (stdout, "ERROR");
	}
      else
	{
	  fprintf (stdout, "NO_CONNECTION");
	}

      if (redirect)
	{
	  fclose (redirect);
	}
    }
  else
    {
      if (er_has_error ())
	{
	  JAVASP_PRINT_ERR_MSG ("%s\n", er_msg ());
	}
    }

  return status;
}

static int
javasp_start_server (const JAVASP_SERVER_INFO jsp_info, const std::string &db_name, const std::string &path)
{
  int status = NO_ERROR;
  int check_port = -1;
  int prm_port = prm_get_integer_value (PRM_ID_JAVA_STORED_PROCEDURE_PORT);

  /* trying to start javasp server for new port */
  if (prm_port != jsp_info.port)
    {
      /* check javasp server is running with previously configured port number */
      check_port = jsp_info.port;
    }
  else
    {
      /* check javasp server is running for the port number written in configuration file */
      check_port = prm_port;
    }

  if (javasp_is_running (check_port, db_name))
    {
      status = ER_GENERIC_ERROR;
    }
  else
    {
#if !defined(WINDOWS)
      /* create a new session */
      setsid ();
#endif
      er_clear (); // clear error before string JVM
      status = jsp_start_server (db_name.c_str (), path.c_str (), prm_port);

      if (status == NO_ERROR)
	{
	  JAVASP_SERVER_INFO jsp_new_info { getpid(), jsp_server_port () };

	  if ((javasp_open_info_dir () && javasp_write_info (db_name.c_str (), jsp_new_info)))
	    {
	      /* succeed */
	    }
	  else
	    {
	      /* failed to write info file */
	      char info_path[PATH_MAX], err_msg[PATH_MAX + 32];
	      javasp_get_info_file (info_path, PATH_MAX, db_name.c_str ());
	      snprintf (err_msg, sizeof (err_msg), "Error while writing to file: (%s)", info_path);
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, err_msg);
	      status = ER_SP_CANNOT_START_JVM;
	    }
	}
    }

  return status;
}

static int
javasp_stop_server (const JAVASP_SERVER_INFO jsp_info, const std::string &db_name)
{
  SOCKET socket = INVALID_SOCKET;
  int status = NO_ERROR;

  socket = jsp_connect_server (jsp_info.port);
  if (socket != INVALID_SOCKET)
    {
      char *buffer = NULL;
      int req_size = (int) sizeof (int);
      int nbytes;

      int stop_code = 0xFF;
      nbytes = jsp_writen (socket, (void *) &stop_code, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  status = er_errid ();
	}
      jsp_disconnect_server (socket);

      if (!javasp_is_terminated_process (jsp_info.pid))
	{
	  javasp_terminate_process (jsp_info.pid);
	}

      javasp_reset_info (db_name.c_str ());
    }

  return status;
}

static int
javasp_status_server (const JAVASP_SERVER_INFO jsp_info)
{
  int status = NO_ERROR;
  char *buffer = NULL;
  SOCKET socket = INVALID_SOCKET;

  socket = jsp_connect_server (jsp_info.port);
  if (socket != INVALID_SOCKET)
    {
      char *ptr = NULL;
      OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
      char *request = OR_ALIGNED_BUF_START (a_request);

      ptr = or_pack_int (request, SP_CODE_UTIL_STATUS);
      ptr = or_pack_int (ptr, SP_CODE_UTIL_TERMINATE_THREAD);

      int nbytes = jsp_writen (socket, request, (int) sizeof (int) * 2);
      if (nbytes != (int) sizeof (int) * 2)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  status = er_errid ();
	  goto exit;
	}

      int res_size = 0;
      nbytes = jsp_readn (socket, (char *) &res_size, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  status = er_errid ();
	  goto exit;
	}
      res_size = ntohl (res_size);

      buffer = (char *) malloc (res_size);
      if (buffer == NULL)
	{
	  status = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto exit;
	}

      nbytes = jsp_readn (socket, buffer, res_size);
      if (nbytes != res_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  status = er_errid ();
	  goto exit;
	}

      int num_args = 0;
      JAVASP_STATUS_INFO status_info;

      status_info.pid = jsp_info.pid;
      ptr = or_unpack_int (buffer, &status_info.port);
      ptr = or_unpack_string_nocopy (ptr, &status_info.db_name);
      ptr = or_unpack_int (ptr, &num_args);
      for (int i = 0; i < num_args; i++)
	{
	  char *arg = NULL;
	  ptr = or_unpack_string_nocopy (ptr, &arg);
	  status_info.vm_args.push_back (std::string (arg));
	}
      jsp_disconnect_server (socket);
      javasp_dump_status (stdout, status_info);
    }

exit:
  if (buffer)
    {
      free_and_init (buffer);
    }

  return status;
}

static int
javasp_ping_server (const int server_port, char *buf)
{
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  char *ptr = NULL;
  SOCKET socket = INVALID_SOCKET;

  socket = jsp_connect_server (server_port);
  if (socket != INVALID_SOCKET)
    {
      ptr = or_pack_int (request, SP_CODE_UTIL_PING);
      ptr = or_pack_int (ptr, SP_CODE_UTIL_TERMINATE_THREAD);

      int nbytes = jsp_writen (socket, request, (int) sizeof (int) * 2);
      if (nbytes != (int) sizeof (int) * 2)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  goto exit;
	}

      int res_size = 0;
      nbytes = jsp_readn (socket, (char *) &res_size, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  goto exit;
	}
      res_size = ntohl (res_size);

      nbytes = jsp_readn (socket, buf, res_size);
      if (nbytes != res_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  goto exit;
	}
    }

exit:
  if (socket != INVALID_SOCKET)
    {
      jsp_disconnect_server (socket);
    }
  return er_errid ();
}

static void
javasp_dump_status (FILE *fp, JAVASP_STATUS_INFO status_info)
{
  fprintf (fp, "Java Stored Procedure Server (%s, pid %d, port %d)\n", status_info.db_name, status_info.pid,
	   status_info.port);
  auto vm_args_len = status_info.vm_args.size();
  if (vm_args_len > 0)
    {
      fprintf (fp, "Java VM arguments :\n");
      fprintf (fp, " -------------------------------------------------\n");
      for (int i = 0; i < (int) vm_args_len; i++)
	{
	  fprintf (fp, "  %s\n", status_info.vm_args[i].c_str());
	}
      fprintf (fp, " -------------------------------------------------\n");
    }
}

static bool
javasp_is_running (const int server_port, const std::string &db_name)
{
  // check server running
  bool result = false;
  char buffer[JAVASP_PING_LEN] = {0};
  if (javasp_ping_server (server_port, buffer) == NO_ERROR)
    {
      if (db_name.compare (0, db_name.size (), buffer) == 0)
	{
	  result = true;
	}
    }
  return result;
}

/*
 * javasp_is_terminated_process () -
 *   return:
 *   pid(in):
 *   TODO there exists same function in file_io.c and util_service.c
 */
static bool
javasp_is_terminated_process (int pid)
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

static void
javasp_terminate_process (int pid)
{
#if defined(WINDOWS)
  HANDLE phandle;

  phandle = OpenProcess (PROCESS_TERMINATE, FALSE, pid);
  if (phandle)
    {
      TerminateProcess (phandle, 0);
      CloseHandle (phandle);
    }
#else /* ! WINDOWS */
  kill (pid, SIGTERM);
#endif /* ! WINDOWS */
}

static int
javasp_get_server_info (const std::string &db_name, JAVASP_SERVER_INFO &info)
{
  if (javasp_open_info_dir ()
      && javasp_read_info (db_name.c_str(), info))
    {
      return NO_ERROR;
    }
  else
    {
      return ER_GENERIC_ERROR;
    }
}

static int
javasp_check_database (const std::string &db_name, std::string &path)
{
  int status = NO_ERROR;

  /* check database exists */
  DB_INFO *db = cfg_find_db (db_name.c_str ());
  if (db == NULL)
    {
      status = ER_GENERIC_ERROR;
    }
  else
    {
      path.assign (db->pathname);
      cfg_free_directory (db);
    }

  return status;
}

static int
javasp_check_argument (int argc, char *argv[], std::string &command, std::string &db_name)
{
  int status = NO_ERROR;

  /* check argument number */
  if (argc == 3)
    {
      command.assign (argv[1]);
      db_name.assign (argv[2]);
    }
  else if (argc == 2)
    {
      command.assign ("start");
      db_name.assign (argv[1]);
    }
  else
    {
      status = ER_GENERIC_ERROR;
      JAVASP_PRINT_ERR_MSG ("Invalid number of arguments: %d\n", argc);
    }

  if (status == NO_ERROR)
    {
      /* check command */
      std::array<std::string, 5> commands = {"start", "stop", "restart", "status", "ping"};
      auto it = find (commands.begin(), commands.end(), command);
      if (it == commands.end())
	{
	  status = ER_GENERIC_ERROR;
	  JAVASP_PRINT_ERR_MSG ("Invalid command: %s\n", command.c_str ());
	}
    }

  return status;
}
