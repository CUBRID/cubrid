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
// master_server_monitor.cpp - Server Revive monitoring module
//

#include <sstream>

#include "master_server_monitor.hpp"

std::unique_ptr<server_monitor> master_Server_monitor = nullptr;

server_monitor::server_monitor ()
{
  m_server_entry_list = std::make_unique<std::vector<server_entry>> ();
  fprintf (stdout, "server_entry_list is created. \n");

  m_thread_shutdown = false;
  m_monitoring_thread = std::make_unique<std::thread> ([this]()
  {
    while (!m_thread_shutdown)
      {
	// TODO: select server_entry whose m_need_revive value is true. (Will be implemented in CBRD-25438 issue.)
      }
  });

  fprintf (stdout, "server_monitor_thread is created. \n");
  fflush (stdout);
}

// In server_monitor destructor, it should guarentee that
// m_monitoring_thread is terminated before m_monitor_list is deleted.
server_monitor::~server_monitor ()
{
  m_thread_shutdown = true;
  if (m_monitoring_thread->joinable())
    {
      m_monitoring_thread->join();
      fprintf (stdout, "server_monitor_thread is terminated. \n");
    }

  assert (m_server_entry_list->size () == 0);
  fprintf (stdout, "server_entry_list is deleted. \n");
  fflush (stdout);
}

server_monitor::server_entry::
server_entry (int pid, const char *server_name, const char *exec_path, char *args, CSS_CONN_ENTRY *conn)
  : m_pid {pid}
  , m_server_name {server_name}
  , m_exec_path {exec_path}
  , m_conn {conn}
  , m_last_revive_time {0, 0}
  , m_need_revive {false}
{
  if (args != nullptr)
    {
      proc_make_arg (args);
    }
}

void
server_monitor::server_entry::proc_make_arg (char *args)
{
  std::istringstream iss (args);
  std::string tok;
  while (iss >> tok)
    {
      m_argv.push_back (tok);
    }
}
