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
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <system_parameter.h>

#include "master_server_monitor.hpp"

#define GET_ELAPSED_TIME(end_time, start_time) \
            ((double)(end_time.tv_sec - start_time.tv_sec) * 1000 + \
             (end_time.tv_usec - start_time.tv_usec)/1000.0)

std::unique_ptr<server_monitor> master_Server_monitor = nullptr;

server_monitor::server_monitor ()
{
  m_server_entry_list = std::make_unique<std::list<server_entry>> ();
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_list is created. \n");

  m_thread_shutdown = false;
  m_monitoring_thread = std::make_unique<std::thread> ([this]()
  {
    int error_code;
    pid_t pid;
    struct timeval tv;
    int tries, max_retries;
    while (!m_thread_shutdown)
      {
	for (auto &entry : *m_server_entry_list)
	  {
	    if (entry.m_need_revive)
	      {
                fprintf (stdout, "Server argv : %s needs revive.\n", entry.get_argv()[1]);
		gettimeofday (&tv, nullptr);
		if (GET_ELAPSED_TIME (tv,entry.get_last_revive_time ()
				      ) > prm_get_integer_value (PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS))
		  {
		    tries = 0;
		    max_retries = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM);
		    while (++tries < max_retries)
		      {
                        fprintf(stdout, "tries : %d, max_retries : %d\n", tries, max_retries);
                        pid = fork ();
                        if (pid < 0)
                        {
                          //MASTER_ER_SET (ER_WARNING_SEVERITY, ARG_FILE_LINE, ERR_CSS_CANNOT_FORK, 0);
                          //sleep (1);
                          continue;
                        }
                        else if (pid == 0)
                        {
                          error_code = execv(entry.get_exec_path(), entry.get_argv());
                        }
                        else
                        {
                          if (kill (pid, 0) < 0)
                          {
                            if (errno != ESRCH)
                            {
                              break;
                            }
                          }
                          else
                          {
                            fprintf (stdout, "Server is revived. \n");
                          }
                        }
		      }
		  }
		master_Server_monitor->remove_server_entry_by_conn (entry.get_conn());
	      }
	  }
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

void
server_monitor::find_set_entry_to_revive (CSS_CONN_ENTRY *conn)
{
  fprintf (stdout, "Server will be checked. \n");
  for (auto &entry : *m_server_entry_list)
    {
      fprintf (stdout, "Server is being checked. \n");
      if (entry.get_conn() == conn)
	{
          fprintf(stdout, "Server is found. \n");
	  entry.m_need_revive = true;
	  return;
	}
    }
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

bool
server_monitor::server_entry::get_need_revive () const
{
  return m_need_revive;
}

void
server_monitor::server_entry::set_need_revive (bool need_revive)
{
  m_need_revive = need_revive;
}

char *const *
server_monitor::server_entry::get_argv () const
{
  char * argv[16];
  for (int i = 0; i < m_argv.size (); i++)
    {
      argv[i] = (char *) m_argv[i].c_str ();
    }
  argv[m_argv.size ()] = nullptr;
  return argv;
}

const char *
server_monitor::server_entry::get_exec_path () const
{
  return m_exec_path.c_str ();
}

struct timeval
server_monitor::server_entry::get_last_revive_time () const
{
  return m_last_revive_time;
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
