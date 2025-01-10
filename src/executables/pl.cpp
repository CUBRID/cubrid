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
 * pl.cpp - utility PL JVM server main routine
 *
 */

#ident "$Id$"

#include "config.h"

#if !defined(WINDOWS)
#include <dlfcn.h>
#include <execinfo.h>
#endif
#include <string.h>
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
#include <tlhelp32.h>
#endif /* not WINDOWS */

#include "process_util.h"
#include "environment_variable.h"
#include "system_parameter.h"
#include "error_code.h"
#include "error_manager.h"
#include "message_catalog.h"
#include "utility.h"
#include "databases_file.h"
#include "object_representation.h"
#include "method_struct_invoke.hpp"
#include "pl_connection.hpp"
#include "pl_comm.h"
#include "pl_file.h"
#include "pl_sr.h"

#include "packer.hpp"

#include <string>
#include <algorithm>
#include <array>
#include <atomic>

#define PL_PING_LEN   PATH_MAX

#define PL_PRINT_ERR_MSG(...) \
  do {\
      fprintf (stderr, __VA_ARGS__);\
  }while (0)

#if defined(WINDOWS)
#define NULL_DEVICE "NUL:"
#else
#define NULL_DEVICE "/dev/null"
#endif

static int pl_start_server (const PL_SERVER_INFO pl_info, const std::string &db_name, const std::string &path);
static int pl_stop_server (const PL_SERVER_INFO pl_info, const std::string &db_name);
static int pl_status_server (const PL_SERVER_INFO pl_info, const std::string &db_name);

static void pl_dump_status (FILE *fp, PL_STATUS_INFO status_info);
static int pl_ping_server (const PL_SERVER_INFO pl_info, const char *db_name, char *buf);
static bool pl_is_running (const PL_SERVER_INFO pl_info, const std::string &db_name);

static int pl_get_server_info (const std::string &db_name, PL_SERVER_INFO &info);
static int pl_check_argument (int argc, char *argv[], std::string &command, std::string &db_name);
static int pl_check_database (const std::string &db_name, std::string &db_path);

static int pl_get_port_param ();

#if !defined(WINDOWS)
static void pl_signal_handler (int sig);
#endif

static bool is_signal_handling = false;
static char executable_path[PATH_MAX];

static std::string command;
static std::string db_name;
static PL_SERVER_INFO running_info = PL_SERVER_INFO_INITIALIZER;

#if defined(WINDOWS)
static bool
get_ppid (DWORD &ppid)
{
  HANDLE h_proc_snap;
  PROCESSENTRY32 pe32;

  /* Take a snapshot of all processes in the system. */
  h_proc_snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
  if (h_proc_snap == INVALID_HANDLE_VALUE)
    {
      return false;
    }

  pe32.dwSize = sizeof (PROCESSENTRY32);
  if (Process32First (h_proc_snap, &pe32))
    {
      DWORD pid = GetCurrentProcessId();
      do
	{
	  if (pe32.th32ProcessID != pid)
	    {
	      continue;
	    }
	  CloseHandle (h_proc_snap);

	  ppid = pe32.th32ParentProcessID;
	  return true;
	}
      while (Process32Next (h_proc_snap, &pe32));
    }

  CloseHandle (h_proc_snap);
  return false;
}
#endif

/*
 * main() - javasp main function
 */

int
main (int argc, char *argv[])
{
  int status = NO_ERROR;
  FILE *redirect = NULL; /* for ping */

#if defined(WINDOWS)
  FARPROC pl_old_hook = NULL;
#else
  if (os_set_signal_handler (SIGPIPE, SIG_IGN) == SIG_ERR)
    {
      return ER_GENERIC_ERROR;
    }

  os_set_signal_handler (SIGABRT, pl_signal_handler);
  os_set_signal_handler (SIGILL, pl_signal_handler);
  os_set_signal_handler (SIGFPE, pl_signal_handler);
  os_set_signal_handler (SIGBUS, pl_signal_handler);
  os_set_signal_handler (SIGSEGV, pl_signal_handler);
  os_set_signal_handler (SIGSYS, pl_signal_handler);

#endif /* WINDOWS */
  {
    /*
    * COMMON PART FOR PING AND OTHER COMMANDS
    */

    // supress error message
    er_init (NULL_DEVICE, ER_NEVER_EXIT);

    /* check arguments, get command and database name */
    status = pl_check_argument (argc, argv, command, db_name);
    if (status != NO_ERROR)
      {
	return ER_GENERIC_ERROR;
      }

    /* check database exists and get path name of database */
    std::string pathname;
    status = pl_check_database (db_name, pathname);
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

    /* try to create info dir and get absolute path for info file; $CUBRID/var/pl_<db_name>.info */
    PL_SERVER_INFO pl_info = PL_SERVER_INFO_INITIALIZER;
    status = pl_get_server_info (db_name, pl_info);
    if (status != NO_ERROR && command.compare ("start") != 0)
      {
	char info_path[PATH_MAX], err_msg[PATH_MAX + 32];
	pl_get_info_file (info_path, PATH_MAX, db_name.c_str ());
	snprintf (err_msg, sizeof (err_msg), "Error while opening file (%s)", info_path);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, err_msg);
	goto exit;
      }

#if defined(WINDOWS)
    // socket startup for windows
    windows_socket_startup (pl_old_hook);
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
	    goto exit;
	  }

	// check process is running
	if (pl_info.pid == PL_PID_DISABLED || is_terminated_process (pl_info.pid) == true)
	  {
	    fprintf (stdout, "NO_PROCESS");
	    goto exit;
	  }

	char buffer[PL_PING_LEN] = {0};
	if (status == NO_ERROR)
	  {
	    status = pl_ping_server (pl_info, db_name.c_str (), buffer);
	  }

	if (status == NO_ERROR)
	  {
	    std::string ping_db_name;
	    packing_unpacker unpacker (buffer, PL_PING_LEN);
	    unpacker.unpack_string (ping_db_name);

	    fprintf (stdout, "%s", ping_db_name.c_str ());
	  }
	else
	  {
	    fprintf (stdout, "NO_CONNECTION");
	    status = NO_ERROR;
	    goto exit;
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
	(void) pl_start_server (pl_info, db_name, pathname);

	command = "running";

	pl_read_info (db_name.c_str(), running_info);
	do
	  {
#if defined (WINDOWS)
	    DWORD parent_ppid = 0;
	    HANDLE hParent;
	    DWORD result;

	    if (get_ppid (parent_ppid) == false)
	      {
		break;// parent process is terminated
	      }

	    hParent = OpenProcess (SYNCHRONIZE, FALSE, parent_ppid);
	    result = WaitForSingleObject (hParent, INFINITE);
	    CloseHandle (hParent);
	    if (result == WAIT_OBJECT_0)
	      {
		break;// parent process is terminated
	      }
#else
	    if (getppid () == 1)
	      {
		// parent process is terminated
		break;
	      }
#endif
	    sleep (1);
	  }
	while (true);
      }
    else if (command.compare ("stop") == 0)
      {
	status = pl_stop_server (pl_info, db_name);
      }
    else if (command.compare ("status") == 0)
      {
	status = pl_status_server (pl_info, db_name);
      }
    else
      {
	PL_PRINT_ERR_MSG ("Invalid command: %s\n", command.c_str ());
	status = ER_GENERIC_ERROR;
      }

#if defined(WINDOWS)
    // socket shutdown for windows
    windows_socket_shutdown (pl_old_hook);
#endif /* WINDOWS */
  }

exit:

  if (command.compare ("ping") == 0)
    {
      if (status != NO_ERROR)
	{
	  fprintf (stdout, "ERROR");
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
	  PL_PRINT_ERR_MSG ("%s\n", er_msg ());
	}
    }

  return status;
}

#if !defined(WINDOWS)
static void pl_signal_handler (int sig)
{
  PL_SERVER_INFO pl_info = PL_SERVER_INFO_INITIALIZER;

  if (os_set_signal_handler (sig, SIG_DFL) == SIG_ERR)
    {
      return;
    }

  if (is_signal_handling == true)
    {
      return;
    }

  int status = pl_get_server_info (db_name, pl_info); // if failed,
  if (status == NO_ERROR && pl_info.pid != PL_PID_DISABLED)
    {
      if (command.compare ("running") != 0 || db_name.empty ())
	{
	  return;
	}

      if (running_info.pid == pl_info.pid && running_info.port == pl_info.port)
	{
	  is_signal_handling = true;
	}
      else
	{
	  return;
	}

      // error handling in parent
      std::string err_msg;

      void *addresses[64];
      int nn_addresses = backtrace (addresses, sizeof (addresses) / sizeof (void *));
      char **symbols = backtrace_symbols (addresses, nn_addresses);

      err_msg += "pid (";
      err_msg += std::to_string (getpid ());
      err_msg += ")\n";

      for (int i = 0; i < nn_addresses; i++)
	{
	  err_msg += symbols[i];
	  if (i < nn_addresses - 1)
	    {
	      err_msg += "\n";
	    }
	}
      free (symbols);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_SERVER_CRASHED, 1, err_msg.c_str ());

      exit (1);
    }
  else
    {
      // resume signal hanlding
      os_set_signal_handler (sig, pl_signal_handler);
      is_signal_handling = false;
    }
}
#endif

static int
pl_get_port_param ()
{
  int prm_port = 0;
#if defined (WINDOWS)
  const bool is_uds_mode = false;
#else
  const bool is_uds_mode = prm_get_bool_value (PRM_ID_STORED_PROCEDURE_UDS);
#endif
  prm_port = (is_uds_mode) ? PL_PORT_UDS_MODE : prm_get_integer_value (PRM_ID_STORED_PROCEDURE_PORT);
  return prm_port;
}

static int
pl_start_server (const PL_SERVER_INFO pl_info, const std::string &db_name, const std::string &path)
{
  int status = NO_ERROR;

  if (pl_info.pid != PL_PID_DISABLED && pl_is_running (pl_info, db_name))
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
      status = pl_start_jvm_server (db_name.c_str (), path.c_str (), pl_get_port_param ());

      int port = (status == NO_ERROR) ? pl_server_port () : PL_PORT_DISABLED;
      PL_SERVER_INFO pl_new_info { getpid(), pl_server_port () };

      pl_unlink_info (db_name.c_str ());
      if ((pl_open_info_dir () && pl_write_info (db_name.c_str (), &pl_new_info)))
	{
	  /* succeed */
	}
      else
	{
	  /* failed to write info file */
	  char info_path[PATH_MAX], err_msg[PATH_MAX + 32];
	  pl_get_info_file (info_path, PATH_MAX, db_name.c_str ());
	  snprintf (err_msg, sizeof (err_msg), "Error while writing to file: (%s)", info_path);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_CANNOT_START_JVM, 1, err_msg);
	  status = ER_SP_CANNOT_START_JVM;
	}
    }

  return status;
}

static int
pl_stop_server (const PL_SERVER_INFO pl_info, const std::string &db_name)
{
#define MAX_RETRY_COUNT 10
  int status = ER_FAILED;
  int retry_count = 0;

  if (pl_info.pid != -1 && !is_terminated_process (pl_info.pid))
    {
      terminate_process (pl_info.pid);

      while (retry_count < MAX_RETRY_COUNT)
	{
	  if (is_terminated_process (pl_info.pid) == true)
	    {
	      status = NO_ERROR;
	      break;
	    }
	  usleep (10);
	  retry_count++;
	}
    }

  return status;
}

static int
pl_status_server (const PL_SERVER_INFO pl_info, const std::string &db_name)
{
  int status = NO_ERROR;
  cubmem::block buffer;

  if (pl_info.pid == -1 || is_terminated_process (pl_info.pid))
    {
      goto exit;
    }

  {
    cubpl::connection_pool connection_pool (1, db_name, pl_info.port, true);
    cubpl::connection_view cv = connection_pool.claim ();
    if (cv->is_valid())
      {
	cubmethod::header header (DB_EMPTY_SESSION, SP_CODE_UTIL_STATUS, 0);
	status = cv->send_buffer_args (header);
	if (status != NO_ERROR)
	  {
	    goto exit;
	  }

	status = cv->receive_buffer (buffer);
	if (status != NO_ERROR)
	  {
	    goto exit;
	  }

	if (status == NO_ERROR)
	  {
	    int num_args = 0;
	    PL_STATUS_INFO status_info;

	    status_info.pid = pl_info.pid;

	    packing_unpacker unpacker (buffer.ptr, buffer.dim);

	    unpacker.unpack_int (status_info.port);
	    unpacker.unpack_string (status_info.db_name);
	    unpacker.unpack_int (num_args);
	    std::string arg;
	    for (int i = 0; i < num_args; i++)
	      {
		unpacker.unpack_string (arg);
		status_info.vm_args.push_back (arg);
	      }

	    pl_dump_status (stdout, status_info);
	  }
      }
    else
      {
	status = ER_GENERIC_ERROR;
      }
  }

exit:
  if (status != NO_ERROR)
    {
      fprintf (stdout, "Java Stored Procedure Server (%s, pid %d) - Abnormal State \n", db_name.c_str (), pl_info.pid);
    }

  if (buffer.ptr)
    {
      free_and_init (buffer.ptr);
    }

  return status;
}

static int
pl_ping_server (const PL_SERVER_INFO pl_info, const char *db_name, char *buf)
{
  int status = NO_ERROR;
  cubmem::block ping_blk {0, NULL};

  cubpl::connection_pool connection_pool (1, db_name, pl_info.port, true);
  cubpl::connection_view cv = connection_pool.claim ();

  if (cv->is_valid())
    {
      cubmethod::header header (DB_EMPTY_SESSION, SP_CODE_UTIL_PING, 0);
      status = cv->send_buffer_args (header);
      if (status != NO_ERROR)
	{
	  goto exit;
	}

      status = cv->receive_buffer (ping_blk);
      if (status != NO_ERROR)
	{
	  goto exit;
	}
      memcpy (buf, ping_blk.ptr, ping_blk.dim);
    }

exit:
  if (ping_blk.is_valid ())
    {
      delete [] ping_blk.ptr;
    }

  return er_errid ();
}

static void
pl_dump_status (FILE *fp, PL_STATUS_INFO status_info)
{
  if (status_info.port == PL_PORT_UDS_MODE)
    {
      fprintf (fp, "Java Stored Procedure Server (%s, pid %d, UDS)\n", status_info.db_name.c_str (), status_info.pid);
    }
  else
    {
      fprintf (fp, "Java Stored Procedure Server (%s, pid %d, port %d)\n", status_info.db_name.c_str (), status_info.pid,
	       status_info.port);
    }
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
pl_is_running (const PL_SERVER_INFO pl_info, const std::string &db_name)
{
  // check server running
  bool result = false;
  char buffer[PL_PING_LEN] = {0};
  if (pl_ping_server (pl_info, db_name.c_str (), buffer) == NO_ERROR)
    {
      if (db_name.compare (0, db_name.size (), buffer) == 0)
	{
	  result = true;
	}
    }
  return result;
}

static int
pl_get_server_info (const std::string &db_name, PL_SERVER_INFO &info)
{
  if (pl_open_info_dir ()
      && pl_read_info (db_name.c_str(), info))
    {
      return NO_ERROR;
    }
  else
    {
      return ER_GENERIC_ERROR;
    }
}

static int
pl_check_database (const std::string &db_name, std::string &path)
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
pl_check_argument (int argc, char *argv[], std::string &command, std::string &db_name)
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
      PL_PRINT_ERR_MSG ("Invalid number of arguments: %d\n", argc);
    }

  if (status == NO_ERROR)
    {
      /* check command */
      std::array<std::string, 5> commands = {"start", "stop", "restart", "status", "ping"};
      auto it = find (commands.begin(), commands.end(), command);
      if (it == commands.end())
	{
	  status = ER_GENERIC_ERROR;
	  PL_PRINT_ERR_MSG ("Invalid command: %s\n", command.c_str ());
	}
    }

  return status;
}
