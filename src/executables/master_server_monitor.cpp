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

std::unique_ptr<server_monitor> master_Server_monitor = nullptr;

server_monitor::server_monitor ()
{
  constexpr int SERVER_MONITOR_JOB_QUEUE_SIZE = 64;
  m_job_queue = new lockfree::circular_queue<server_monitor_job> (SERVER_MONITOR_JOB_QUEUE_SIZE);
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : job_queue is created. \n");
  m_server_entry_list = std::make_unique<std::list<server_entry>> ();
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_list is created. \n");

  start_monitoring_thread ();
  m_monitoring_thread = std::make_unique<std::thread> (&server_monitor::server_monitor_thread_worker, this);
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is created. \n");
  fflush (stdout);
}

// In server_monitor destructor, it should guarentee that
// m_monitoring_thread is terminated before m_monitor_list is deleted.
server_monitor::~server_monitor ()
{
  stop_monitoring_thread ();
  if (m_monitoring_thread->joinable())
    {
      m_monitor_cv.notify_all();
      m_monitoring_thread->join();
      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is terminated. \n");
    }

  assert (m_job_queue->is_empty ());
  if (m_job_queue)
    {
      delete m_job_queue;
      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : job_queue is deleted. \n");
    }

  assert (m_server_entry_list->size () == 0);
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_list is deleted. \n");

  fflush (stdout);
}

void
server_monitor::start_monitoring_thread ()
{
  m_thread_shutdown = false;
}

void
server_monitor::stop_monitoring_thread ()
{
  m_thread_shutdown = true;
}

void
server_monitor::server_monitor_thread_worker ()
{
  while (!m_thread_shutdown)
    {
      std::unique_lock<std::mutex> lock (m_monitor_mutex);

      constexpr int period_in_secs = 1;
      while (!m_monitor_cv.wait_for (lock, std::chrono::seconds (period_in_secs), [this] ()
      {
	return !m_job_queue->is_empty () || m_thread_shutdown;
	}));

      while (!m_job_queue->is_empty ())
	{
	  server_monitor_job consume_job;
	  m_job_queue->consume (consume_job);

	  switch (consume_job.m_job_type)
	    {
	    case SERVER_MONITOR_NO_JOB:
	      break;
	    case SERVER_MONITOR_REGISTER_ENTRY:
	      make_and_insert_server_entry (consume_job.m_pid, consume_job.m_exec_path, consume_job.m_args,
					    consume_job.m_conn, consume_job.m_produce_time);
	      break;
	    case SERVER_MONITOR_REMOVE_ENTRY:
	      remove_server_entry_by_pid (consume_job.m_pid);
	      break;
	    case SERVER_MONITOR_REVIVE_ENTRY:
	      revive_server_with_pid (consume_job.m_pid);
	      break;
	    default:
	      break;
	    }
	}
    }
}

void
server_monitor::make_and_insert_server_entry (int pid, const char *exec_path, char *args,
    CSS_CONN_ENTRY *conn, std::chrono::steady_clock::time_point revive_time)
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
	  entry.set_last_revive_time (revive_time);
	  entry.m_retries = 0;
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
      m_server_entry_list->emplace_back (pid, exec_path, args, conn, revive_time);
      fprintf (stdout,
	       "[SERVER_REVIVE_DEBUG] : server entry has been registered into master_Server_monitor : pid : %d, exec_path : %s, args : %s\n",
	       pid,
	       exec_path, args);
    }
}

void
server_monitor::remove_server_entry_by_pid (int pid)
{
  const auto result = std::remove_if (m_server_entry_list->begin(), m_server_entry_list->end(),
				      [pid] (auto& e) -> bool
  {
    return e.get_pid() == pid;
  });
  assert (result != m_server_entry_list->end ());
  m_server_entry_list->erase (result, m_server_entry_list->end());
  fprintf (stdout,
	   "[SERVER_REVIVE_DEBUG] : server has been removed from master_Server_monitor. Number of server in master_Server_monitor: %d\n",
	   m_server_entry_list->size ());
}

void
server_monitor::revive_server_with_pid (int pid)
{
  int error_code;
  const int SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES = prm_get_integer_value (PRM_ID_HA_MAX_PROCESS_START_CONFIRM);
  const int SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_MSECS = prm_get_integer_value (
	      PRM_ID_HA_UNACCEPTABLE_PROC_RESTART_TIMEDIFF_IN_MSECS);
  std::chrono::steady_clock::time_point tv;
  int out_pid = 0;
  for (auto it = m_server_entry_list->begin(); it != m_server_entry_list->end(); it++)
    {
      if (it->get_pid () == pid)
	{
	  it->set_need_revive (true);
	  tv = std::chrono::steady_clock::now ();
	  auto timediff = std::chrono::duration_cast<std::chrono::milliseconds> (tv - it->get_last_revive_time()).count();
	  if (timediff > SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_MSECS)
	    {
execute:
	      server_monitor_try_revive_server (it->get_exec_path(), it->get_argv(), &out_pid);
	      assert (out_pid > 0);

	      do
		{
		  error_code = kill (out_pid, 0);
		  if (error_code == ESRCH)
		    {
		      // If there is no process with the given pid, fork and exectute the server again.
		      goto execute;
		    }
		  error_code = server_monitor_check_server_revived (*it);
		  if (error_code == NO_ERROR)
		    {
		      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Server revive success. pid : %d\n", out_pid);
		      return;
		    }
		  std::this_thread::sleep_for (std::chrono::milliseconds (1000));
		}
	      while (++ (it->m_retries) < SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES);

	      assert (it->m_retries >= SERVER_MONITOR_REVIVE_CONFIRM_MAX_RETRIES);
	      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Server revive retries exceeds the maximum retries. pid : %d\n", out_pid);
	      it = m_server_entry_list->erase (it);
	      return;
	    }
	  else
	    {
	      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Process failure repeated within a short period of time. pid : %d\n",
		       it->get_pid());
	      it = m_server_entry_list->erase (it);
	      return;
	    }
	}
    }
}

server_monitor::server_monitor_job::
server_monitor_job (server_monitor_job_type job_type, int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn)
  : m_job_type {job_type}
  , m_pid {pid}
  , m_exec_path {exec_path}
  , m_args {args}
  , m_conn {conn}
{
  m_produce_time = std::chrono::steady_clock::now ();
}

server_monitor::server_entry::
server_entry (int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn,
	      std::chrono::steady_clock::time_point revive_time)
  : m_pid {pid}
  , m_exec_path {exec_path}
  , m_conn {conn}
  , m_need_revive {false}
  , m_retries {0}
  , m_last_revive_time {revive_time}
{
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
  m_need_revive = need_revive;
}

void
server_monitor::server_entry::set_last_revive_time (std::chrono::steady_clock::time_point revive_time)
{
  m_last_revive_time = revive_time;
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
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_name : %s, pid : %d, revive_count : %d\n",
	   sentry.get_argv()[1].c_str(), sentry.get_pid(), sentry.m_retries);
  if (!sentry.get_need_revive())
    {
      return NO_ERROR;
    }
  return ER_FAILED;
}

void
server_monitor::server_monitor_produce_job (server_monitor_job_type job_type, int pid, const char *exec_path,
    char *args,
    CSS_CONN_ENTRY *conn)
{
  server_monitor::server_monitor_job job (job_type, pid, exec_path, args, conn);
  m_job_queue->produce (job);
  m_monitor_cv.notify_all();
}