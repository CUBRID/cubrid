/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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

#include "jsp_common.h"
#include "jsp_file.h"
#include "jsp_sr.h"

#include <string>

#define JAVASP_PING_LEN   PATH_MAX

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
static void javasp_reset_file (const std::string &db_name);

static int javasp_check_argument (int argc, char *argv[], std::string &command, std::string &db_name);
static int javasp_check_database (const std::string &db_name, std::string &db_path);

/*
 * main() - javasp main function
 */

int
main (int argc, char *argv[])
{
  int status = NO_ERROR;
#if defined(WINDOWS)
  FARPROC jsp_old_hook = NULL;
#else
  if (os_set_signal_handler (SIGPIPE, SIG_IGN) == SIG_ERR)
    {
      status = ER_GENERIC_ERROR;
      goto exit;
    }
#endif /* WINDOWS */
  {
    /*
    * COMMON PART FOR PING AND OTHER COMMANDS
    */

    if (utility_initialize () != NO_ERROR)
      {
	return EXIT_FAILURE;
      }

    /* check arguments, get command and database name */
    std::string command, db_name;
    status = javasp_check_argument (argc, argv, command, db_name);
    if (status != NO_ERROR)
      {
	goto exit;
      }

    if (command.compare ("ping") == 0)
      {
	// redirect stderr
	FILE *f = NULL;
	if ((f = freopen (NULL_DEVICE, "w", stderr)) == NULL)
	  {
	    assert (false);
	  }

	// supress error message
	er_init (NULL_DEVICE, ER_NEVER_EXIT);
      }
    else
      {
	/* error message log file */
	char er_msg_file[PATH_MAX];
	snprintf (er_msg_file, sizeof (er_msg_file) - 1, "./%s_java.err", db_name.c_str ());
	if (er_init (er_msg_file, ER_NEVER_EXIT) != NO_ERROR)
	  {
	    PRINT_AND_LOG_ERR_MSG ("Failed to initialize error manager.\n");
	  }
      }

    /* check database exists and get path name of database */
    std::string pathname;
    status = javasp_check_database (db_name, pathname);
    if (status != NO_ERROR)
      {
	goto exit;
      }

    /* try to create info dir and get absolute path for info file; $CUBRID/var/javasp_<db_name>.info */
    JAVASP_SERVER_INFO jsp_info = {-1, -1};
    status = javasp_get_server_info (db_name, jsp_info);
    if (status != NO_ERROR)
      {
	goto exit;
      }

    /*
    * PROCESS PING
    */
    if (command.compare ("ping") == 0)
      {
	char buffer[JAVASP_PING_LEN] = {0};
	if (javasp_ping_server (jsp_info.port, buffer) == NO_ERROR)
	  {
	    fprintf (stdout, "%s", buffer);
	  }
	return NO_ERROR;
      }

    /*
    * BEGIN TO PROCESS FOR OTHER COMMANDS
    */

    // load system parameter
    status = sysprm_load_and_init (db_name.c_str (), NULL, SYSPRM_IGNORE_INTL_PARAMS);
    if (status != NO_ERROR)
      {
	PRINT_AND_LOG_ERR_MSG ("Failed to load system paramter");
	goto exit;
      }

    // check java stored procedure is not enabled
    if (prm_get_bool_value (PRM_ID_JAVA_STORED_PROCEDURE) == false)
      {
	PRINT_AND_LOG_ERR_MSG ("%s parameter is not enabled\n", prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE));
	status = ER_GENERIC_ERROR;
	goto exit;
      }

#if defined(WINDOWS)
    // socket startup for windows
    windows_socket_startup (jsp_old_hook);
#endif /* WINDOWS */

    /* javasp command main routine */
    if (command.compare ("start") == 0)
      {
	status = javasp_start_server (jsp_info, db_name, pathname.c_str ());
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
	assert (false);
	status = ER_GENERIC_ERROR;
	goto exit;
      }
  }

exit:

#if defined(WINDOWS)
  windows_socket_shutdown (jsp_old_hook);
#endif /* WINDOWS */

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

      status = jsp_start_server (db_name.c_str (), path.c_str (), prm_port);

      if (status == NO_ERROR)
	{
	  char info_path[PATH_MAX] = {0};
	  JAVASP_SERVER_INFO jsp_new_info { getpid(), jsp_server_port () };
	  javasp_get_info_file (info_path, sizeof (info_path), db_name.c_str ());
	  javasp_write_info (info_path, jsp_new_info);
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
  if (socket == INVALID_SOCKET)
    {
      status = ER_SP_CANNOT_CONNECT_JVM;
    }
  else
    {
      char *buffer = NULL;
      int req_size = (int) sizeof (int);
      int nbytes;

      int stop_code = 0xFF;
      nbytes = jsp_writen (socket, (void *) &stop_code, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  status = ER_SP_NETWORK_ERROR;
	}
      jsp_disconnect_server (socket);

      if (!javasp_is_terminated_process (jsp_info.pid))
	{
	  javasp_terminate_process (jsp_info.pid);
	}

      javasp_reset_file (db_name);
    }

  return status;
}

static int
javasp_status_server (const JAVASP_SERVER_INFO jsp_info)
{
  int status = NO_ERROR;
  char *buffer = NULL;

  SOCKET socket = jsp_connect_server (jsp_info.port);
  if (socket == INVALID_SOCKET)
    {
      status = ER_SP_CANNOT_CONNECT_JVM;
      goto exit;
    }
  else
    {
      char *ptr = NULL;
      OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
      char *request = OR_ALIGNED_BUF_START (a_request);

      ptr = or_pack_int (request, SP_CODE_UTIL_STATUS);
      ptr = or_pack_int (ptr, SP_CODE_UTIL_TERMINATE_THREAD);

      int nbytes = jsp_writen (socket, request, (int) sizeof (int) * 2);
      if (nbytes != (int) sizeof (int) * 2)
	{
	  // stopping failed
	  status = ER_SP_NOT_RUNNING_JVM;
	  goto exit;
	}

      int res_size = 0;
      nbytes = jsp_readn (socket, (char *) &res_size, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  status = ER_SP_NETWORK_ERROR;
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
	  status = ER_SP_NETWORK_ERROR;
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
  int status = NO_ERROR;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_request;
  char *request = OR_ALIGNED_BUF_START (a_request);
  char *ptr = NULL;
  SOCKET socket = INVALID_SOCKET;

  {
    socket = jsp_connect_server (server_port);
    if (socket == INVALID_SOCKET)
      {
	// server is not running
	status = ER_SP_CANNOT_CONNECT_JVM;
	goto exit;
      }

    ptr = or_pack_int (request, SP_CODE_UTIL_PING);
    ptr = or_pack_int (ptr, SP_CODE_UTIL_TERMINATE_THREAD);

    int nbytes = jsp_writen (socket, request, (int) sizeof (int) * 2);
    if (nbytes != (int) sizeof (int) * 2)
      {
	// stopping failed
	status = ER_SP_NOT_RUNNING_JVM;
	goto exit;
      }

    int res_size = 0;
    nbytes = jsp_readn (socket, (char *) &res_size, (int) sizeof (int));
    if (nbytes != (int) sizeof (int))
      {
	status = ER_SP_NETWORK_ERROR;
	goto exit;
      }
    res_size = ntohl (res_size);

    nbytes = jsp_readn (socket, buf, res_size);
    if (nbytes != res_size)
      {
	status = ER_SP_NETWORK_ERROR;
	goto exit;
      }
  }

exit:
  jsp_disconnect_server (socket);
  return status;
}

static void javasp_dump_status (FILE *fp, JAVASP_STATUS_INFO status_info)
{
  fprintf (stdout, "Java Stored Procedure Server (%s, pid %d, port %d)\n", status_info.db_name, status_info.pid,
	   status_info.port);
  auto vm_args_len = status_info.vm_args.size();
  if (vm_args_len > 0)
    {
      fprintf (stdout, "Java VM arguments :\n");
      fprintf (fp, " -------------------------------------------------\n");
      for (int i = 0; i < (int) vm_args_len; i++)
	{
	  fprintf (stdout, "  %s\n", status_info.vm_args[i].c_str());
	}
      fprintf (fp, " -------------------------------------------------\n");
    }
}

static bool javasp_is_running (const int server_port, const std::string &db_name)
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

static void
javasp_reset_file (const std::string &db_name)
{
  char info_path[PATH_MAX] = {0};
  JAVASP_SERVER_INFO reset_info {-1, -1};
  javasp_get_info_file (info_path, sizeof (info_path), db_name.c_str ());
  javasp_write_info (info_path, reset_info);
}

static int
javasp_get_server_info (const std::string &db_name, JAVASP_SERVER_INFO &info)
{
  int status = NO_ERROR;
  char info_path[PATH_MAX];
  if (javasp_get_info_dir () && javasp_get_info_file (info_path, sizeof (info_path), db_name.c_str ()))
    {
      info = javasp_read_info (info_path);
    }
  else
    {
      PRINT_AND_LOG_ERR_MSG ("Error while opening file: %s\n", info_path);
      status = MSGCAT_UTIL_GENERIC_FILEOPEN_ERROR;
    }

  return status;
}

static int
javasp_check_database (const std::string &db_name, std::string &path)
{
  int status = NO_ERROR;

  /* check database exists */
  DB_INFO *db = cfg_find_db (db_name.c_str ());
  if (db == NULL)
    {
      PRINT_AND_LOG_ERR_MSG ("database '%s' does not exist.\n", db_name.c_str ());
      status = ER_FAILED;
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
      status = MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT;
    }

  return status;
}