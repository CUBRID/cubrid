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

#include "master_server_monitor.hpp"

#include "util_func.h"

server_monitor::server_monitor ()
{
  m_thread_shutdown = false;
  m_monitor_list = std::make_unique<server_monitor_list>();
  fprintf (stdout, "server_monitor_list is created.\n");

  m_monitoring_thread = std::make_unique<std::thread> ([this]()
  {
    while (m_thread_shutdown)
      {
	// TODO: Server_entry selection which need_revive value is true. (Will be implemented in CBRD-25438 issue.)
      }
  });
  fprintf (stdout, "server_monitor_thread is created.\n");
}


// When server_monitor is deleted, it should guerentee that
// monitoring_thread is terminated before monitor_list is deleted.
server_monitor::~server_monitor ()
{
  m_thread_shutdown = true;
  if (m_monitoring_thread->joinable())
    {
      m_monitoring_thread->join();
    }
  fprintf (stdout, "server_monitor_thread is deleted.\n");
  m_monitor_list.reset();
  fprintf (stdout, "server_monitor_list is deleted.\n");
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
  this->m_argv = new char *[HB_MAX_NUM_PROC_ARGV];
  proc_make_arg (this->m_argv, args);
}

server_monitor::server_entry::~server_entry()
{
  if (m_argv)
    {
      for (char **arg = m_argv; *arg; ++arg)
	{
	  delete[] * arg;
	}
      delete[] m_argv;
    }
}


void
server_monitor::server_entry::proc_make_arg (char **arg, char *args)
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


server_monitor::server_monitor_list::server_monitor_list()
{
  m_unacceptable_proc_restart_timediff = prm_get_integer_value (PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS);
  m_max_process_start_confirm = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM);
}