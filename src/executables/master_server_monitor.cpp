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
#include <unistd.h>
#include <signal.h>
#include <system_parameter.h>
#include "master_server_monitor.hpp"

#define SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM)
#define SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_MSECS prm_get_integer_value (PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS)
std::unique_ptr<server_monitor> master_Server_monitor = nullptr;

server_monitor::server_monitor ()
{
  m_server_entry_list = std::make_unique<std::list<server_entry>> ();
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_list is created. \n");

  m_thread_shutdown = false;
  m_revive_entry_count.store (0);
  m_monitoring_thread = std::make_unique<std::thread> ([this]()
  {
    int error_code;
    std::chrono::steady_clock::time_point tv;
    int pid = 0;
    while (!m_thread_shutdown)
      {
	std::unique_lock<std::mutex> lock (m_monitor_mutex);

	constexpr int period_in_secs = 1;
	while (!m_monitor_cv.wait_for (lock, std::chrono::seconds (period_in_secs), [this] ()
	{
	  return m_revive_entry_count.load () > 0 || m_thread_shutdown;
	  }));

	while (m_revive_entry_count.load () > 0)
	  {
	    fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is running. Number of server need revive : %d\n",
		     m_revive_entry_count.load());
	    for (auto it = m_server_entry_list->begin(); it != m_server_entry_list->end();)
	      {
		if (it->get_need_revive ())
		  {
		    //it->set_need_revive (false);

		    tv = std::chrono::steady_clock::now ();
		    auto timediff = std::chrono::duration_cast<std::chrono::milliseconds> (tv - it->get_last_revive_time()).count();
		    if (timediff > SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_MSECS)
		      {
			while (++ (it->m_revive_count) < SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES)
			  {
			    //it->set_need_revive (true);

			    server_monitor_try_revive_server (it->get_exec_path(), it->get_argv(), &pid);
			    assert (pid > 0);
			    error_code = kill (pid, 0);
			    if (error_code == ESRCH)
			      {
				// If there is no process with the given pid, fork and exectute the server again.
				continue;
			      }

			    error_code = server_monitor_check_server_revived (*it);
			    if (error_code != NO_ERROR)
			      {
				assert (it->m_revive_count >= SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES);
				fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server has been failed to revive\n");
				m_revive_entry_count--;
				it = m_server_entry_list->erase (it);
				break;
			      }
			    else
			      {
				m_revive_entry_count--;
				it++;
				break;
			      }
			  }
		      }
		    else
		      {
			fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Process failure repeated within a short period of time.\n");
			m_revive_entry_count--;
			it = m_server_entry_list->erase (it);
		      }
		  }
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
      m_monitor_cv.notify_all();
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
  bool success = false;
  for (auto &entry : *m_server_entry_list)
    {
      if (entry.server_entry_compare_args_and_argv (args))
	{
	  entry.set_pid (pid);
	  entry.set_exec_path (exec_path);
	  entry.set_conn (conn);
	  entry.set_need_revive (false);
	  entry.set_last_revive_time ();
	  entry.m_revive_count = 0;
	  success = true;
	  fprintf (stdout,
		   "[SERVER_REVIVE_DEBUG] : server entry has been updated in master_Server_monitor : pid : %d, exec_path : %s, args : %s\n",
		   pid,
		   exec_path, args);
	  break;
	}
    }
  if (!success)
    {
      m_server_entry_list->emplace_back (pid, exec_path, args, conn);
      fprintf (stdout,
	       "[SERVER_REVIVE_DEBUG] : server entry has been registered into master_Server_monitor : pid : %d, exec_path : %s, args : %s\n",
	       pid,
	       exec_path, args);
    }
}

void
server_monitor::remove_server_entry_by_conn (CSS_CONN_ENTRY *conn)
{
  std::unique_lock<std::mutex> lock (m_monitor_mutex);
  const auto result = std::remove_if (m_server_entry_list->begin(), m_server_entry_list->end(),
				      [conn] (auto& e) -> bool
  {
    return e.get_conn() == conn;
  });
  assert (result != m_server_entry_list->end ());
  m_server_entry_list->erase (result, m_server_entry_list->end());
  fprintf (stdout,
	   "[SERVER_REVIVE_DEBUG] : server has been removed from master_Server_monitor. Number of server in master_Server_monitor: %d\n",
	   m_server_entry_list->size ());
}

void
server_monitor::find_set_entry_to_revive (CSS_CONN_ENTRY *conn)
{
  for (auto &entry : *m_server_entry_list)
    {
      if (entry.get_conn() == conn)
	{
	  entry.set_need_revive (true);
	  m_revive_entry_count++;
	  m_monitor_cv.notify_all();
	  return;
	}
    }
}

server_monitor::server_entry::
server_entry (int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn)
  : m_pid {pid}
  , m_exec_path {exec_path}
  , m_conn {conn}
  , m_need_revive {false}
  , m_revive_count {0}
{
  set_last_revive_time ();
  if (args != nullptr)
    {
      proc_make_arg (args);
    }
}

int
server_monitor::server_entry::get_pid () const
{
  return m_pid;
}

std::string
server_monitor::server_entry::get_exec_path () const
{
  return m_exec_path;
}

std::vector<std::string>
server_monitor::server_entry::get_argv () const
{
  return m_argv;
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

std::chrono::steady_clock::time_point
server_monitor::server_entry::get_last_revive_time () const
{
  return m_last_revive_time;
}

void
server_monitor::server_entry::set_pid (int pid)
{
  m_pid = pid;
}

void
server_monitor::server_entry::set_exec_path (const char *exec_path)
{
  m_exec_path = exec_path;
}

void
server_monitor::server_entry::set_conn (CSS_CONN_ENTRY *conn)
{
  m_conn = conn;
}

void
server_monitor::server_entry::set_need_revive (bool need_revive)
{
  std::unique_lock<std::mutex> lock (m_entry_mutex);
  m_need_revive = need_revive;
}

void
server_monitor::server_entry::set_last_revive_time ()
{
  m_last_revive_time = std::chrono::steady_clock::now ();
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

bool
server_monitor::server_entry::server_entry_compare_args_and_argv (const char *args)
{
  std::istringstream iss (args);
  std::string tok;
  for (size_t i = 0; i < m_argv.size(); i++)
    {
      if (! (iss >> tok))
	{
	  return false;
	}
      if (m_argv[i] != tok)
	{
	  return false;
	}
    }
  return true;
}

void
server_monitor::server_monitor_try_revive_server (std::string exec_path, std::vector<std::string> argv, int *out_pid)
{
  pid_t pid;
  int error_code;

  if (out_pid)
    {
      *out_pid = 0;
    }

  pid = fork ();
  if (pid < 0)
    {
      return;
    }
  else if (pid == 0)
    {
      const char **argv_ptr = new const char *[argv.size() + 1];
      for (size_t i = 0; i < argv.size(); i++)
	{
	  argv_ptr[i] = argv[i].c_str();
	}
      argv_ptr[argv.size()] = nullptr;
      error_code = execv (exec_path.c_str(), (char *const *)argv_ptr);
    }
  else
    {
      *out_pid = pid;
    }
}

int
server_monitor::server_monitor_check_server_revived (server_monitor::server_entry &sentry)
{

  while (++sentry.m_revive_count < SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES)
    {
      std::this_thread::sleep_for (std::chrono::milliseconds (1000));
      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_name : %s, pid : %d, revive_count : %d\n",
	       sentry.get_argv()[1].c_str(), sentry.get_pid(), sentry.m_revive_count);
      if (!sentry.get_need_revive())
	{
	  return NO_ERROR;
	}
    }
  return ER_FAILED;
}