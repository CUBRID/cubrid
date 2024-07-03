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

//
// server_revive.cpp - Server Revive monitoring module
//

#include "server_revive.hpp"
#include "util_func.h"

master_monitor::master_monitor()
{
  m_monitoring_thread = std::make_unique<std::thread> ([this]()
  {
    master_monitor_worker();
  });
}


// When master_monitor is deleted, it should guerentee that
// monitoring_thread is terminated before monitor_list is deleted.
master_monitor::~master_monitor()
{
  if (m_monitoring_thread->joinable())
    {
      m_monitoring_thread->join();
    }
  m_monitor_list.reset();
}

void
master_monitor::make_and_insert_server_entry (int pid, const char *server_name, const char *exec_path, char *args,
    CSS_CONN_ENTRY *conn)
{
  auto temp = std::make_unique<server_entry> (pid, server_name, exec_path, args, conn);
  m_monitor_list->server_entry_list.push_back (std::move (temp));
}

void
master_monitor::master_monitor_worker (void)
{
  while (true)
    {
      // TODO: Server_entry selection which need_revive value is true. (Will be implemented in CBRD-25438 issue.)
    }
}

void master_monitor::remove_server_entry (std::unique_ptr<server_entry> entry)
{
  auto it = std::find_if (m_monitor_list->server_entry_list.begin(), m_monitor_list->server_entry_list.end(),
			  [&entry] (const std::unique_ptr<server_entry> &e)
  {
    return e.get() == entry.get();
  });

  if (it != m_monitor_list->server_entry_list.end())
    {
      m_monitor_list->server_entry_list.erase (it);
    }
}

std::unique_ptr<master_monitor::server_entry> master_monitor::get_server_entry_by_conn (CSS_CONN_ENTRY *conn)
{
  auto it = std::find_if (m_monitor_list->server_entry_list.begin(), m_monitor_list->server_entry_list.end(),
			  [conn] (const std::unique_ptr<server_entry> &e)
  {
    return e->conn == conn;
  });

  if (it != m_monitor_list->server_entry_list.end())
    {
      return std::move (*it);
    }

  return nullptr;
}

void
master_monitor::revive_server (std::unique_ptr<server_entry> entry)
{
  // TODO: Server reviving routine for abnormal terminated server. (Will be mplemented in CBRD-25438 issue.)
}

master_monitor::server_entry::
server_entry (int pid, const char *server_name, const char *exec_path, char *args, CSS_CONN_ENTRY *conn)
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

master_monitor::server_entry::~server_entry()
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

master_monitor::master_monitor_list::master_monitor_list()
{
  unacceptable_proc_restart_timediff = prm_get_integer_value (PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS);
  max_process_start_confirm = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM);
}

/*
 * proc_make_arg() -
 *   return: none
 *
 *   arg(out):
 *   argv(in):
 */
void
proc_make_arg (char **arg, char *args)
{
  char *tok, *save;

  tok = strtok_r (args, " \t\n", &save);

  while (tok)
    {
      (*arg++) = tok;
      tok = strtok_r (NULL, " \t\n", &save);
    }

  return;
}
