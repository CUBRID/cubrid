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

//
// server_revive.cpp - Server Revive monitoring module
//

#include "server_revive.hpp"
#include "util_func.h"
#include <thread>

static
  std::thread
  master_monitoring_thread;
static void
proc_make_arg (char **arg, char *args);
static void
css_monitor_worker ();

/*
 * proc_make_arg() -
 *   return: none
 *
 *   arg(out):
 *   argv(in):
 */
static void
proc_make_arg (char **arg, char *args)
{
  char *
  tok, *
    save;

  tok = strtok_r (args, " \t\n", &save);

  while (tok)
    {
      (*arg++) = tok;
      tok = strtok_r (NULL, " \t\n", &save);
    }

  return;
}

SERVER_ENTRY::SERVER_ENTRY (int pid, const char *server_name, const char *exec_path, char *args, CSS_CONN_ENTRY * conn)
{
  char *argv[HB_MAX_NUM_PROC_ARGV] = { NULL, };

  this->pid = pid;

  this->server_name = new char[std::strlen (server_name) + 1];
  std::strcpy (this->server_name, server_name);

  this->exec_path = new char[std::strlen (exec_path) + 1];
  std::strcpy (this->exec_path, exec_path);

  proc_make_arg (argv, args);
  this->argv = argv;
  this->conn = conn;

  // Set last_revive_time to 0
  this->last_revive_time.tv_sec = 0;
  this->last_revive_time.tv_usec = 0;

  this->need_revive = false;
}

SERVER_ENTRY::~SERVER_ENTRY ()
{
  delete[]server_name;
  delete[]exec_path;

  if (argv)
    {
      for (char **arg = argv; *arg; ++arg)
	{
	  delete[] * arg;
	}
      delete[]argv;
    }
}

MASTER_MONITOR_LIST::MASTER_MONITOR_LIST ()
{
  //need to change to Non-ha parameters (make new parameter)
  unacceptable_proc_restart_timediff = prm_get_integer_value (PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS);
  max_process_start_confirm = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM);
  PRINT_AND_LOG_ERR_MSG ("Master monitoring list is successfuly constructed.\n");
}

MASTER_MONITOR_LIST::~MASTER_MONITOR_LIST ()
{
for (SERVER_ENTRY * sentry:server_entry_list)
    {
      delete sentry;
    }
  server_entry_list.clear ();
  PRINT_AND_LOG_ERR_MSG ("Master monitoring list is successfuly destucted.\n");
}


/*
 * push_server_entry()
 *   return: none
 *   sentry(in): server_entry which wants to insert in master_monitoring_list.
 */
void
MASTER_MONITOR_LIST::push_server_entry (SERVER_ENTRY * sentry)
{
  server_entry_list.push_back (sentry);
}

/*
 * remove_server_entry()
 *   return: none
 *   sentry(in): server_entry which wants to remove from master_monitoring_list.
 */
void
MASTER_MONITOR_LIST::remove_server_entry (SERVER_ENTRY * sentry)
{
  auto it = std::find_if (server_entry_list.begin (), server_entry_list.end (),
			  [sentry] (SERVER_ENTRY * entry) {
			  return entry->conn->fd == sentry->conn->fd;}
  );
  if (it != server_entry_list.end ())
    {
      // Free the memory of the found entry
      delete *it;
      server_entry_list.erase (it);
      return;
    }
  return;
}

/*
 * get_server_entry_by_conn()
 *   return: server_entry
 *   conn(in): socket that corresoponds to server_entry that want to find.
 */
SERVER_ENTRY *
MASTER_MONITOR_LIST::get_server_entry_by_conn (CSS_CONN_ENTRY * conn)
{
for (SERVER_ENTRY * sentry:server_entry_list)
    {
      if (sentry->conn->fd == conn->fd)
	{
	  return sentry;
	}
    }
  return NULL;
}

/*
 * revive_proc()
 *   return: none
 *   sentry(in): server_entry which wants to revive the server.
 */
void
MASTER_MONITOR_LIST::revive_server (SERVER_ENTRY * sentry)
{
  // Revive routine for master monitoring thread. (TOBE)
  return;
}

void
create_monitor_thread ()
{
  master_monitoring_thread = std::thread (css_monitor_worker);
}

void
finalize_monitor_thread ()
{
  master_monitoring_thread.join ();
}

void
css_monitor_worker ()
{
  // TODO : revive routine for monitoring list (Will be done at CBRD-25438 issue)
}
