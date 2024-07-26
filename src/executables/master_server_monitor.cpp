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
#include <algorithm>

#include "master_server_monitor.hpp"

std::unique_ptr<server_monitor> master_Server_monitor = nullptr;

server_monitor::server_monitor ()
{
  m_server_entry_list = std::make_unique<std::list<server_entry>> ();
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_list is created. \n");

  m_thread_shutdown = false;
  m_monitoring_thread = std::make_unique<std::thread> ([this]()
  {
    while (!m_thread_shutdown)
      {
	// TODO: select server_entry whose m_need_revive value is true. (Will be implemented in CBRD-25438 issue.)
      }
  });

  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is created. \n");
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
      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is terminated. \n");
    }

  assert (m_server_entry_list->size () == 0);
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_list is deleted. \n");
  fflush (stdout);
}

void
server_monitor::make_and_insert_server_entry (int pid, const char *exec_path, char *args,
    CSS_CONN_ENTRY *conn)
{
  m_server_entry_list->emplace_back (pid, exec_path, args, conn);
  fprintf (stdout,
	   "[SERVER_REVIVE_DEBUG] : server has been registered into master_Server_monitor : pid : %d, exec_path : %s, args : %s\n",
	   pid,
	   exec_path, args);
}

void
server_monitor::remove_server_entry_by_conn (CSS_CONN_ENTRY *conn)
{
  const auto result = std::remove_if (m_server_entry_list->begin(), m_server_entry_list->end(),
				      [conn] (auto& e) -> bool {return e.get_conn() == conn;});
  assert (result != m_server_entry_list->end ());

  m_server_entry_list->erase (result, m_server_entry_list->end());
  fprintf (stdout,
	   "[SERVER_REVIVE_DEBUG] : server has been removed from master_Server_monitor. Number of server in master_Server_monitor: %d\n",
	   m_server_entry_list->size ());
}

server_monitor::server_entry::
server_entry (int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn)
  : m_pid {pid}
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

CSS_CONN_ENTRY *
server_monitor::server_entry::get_conn () const
{
  return m_conn;
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
