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
#include "system_parameter.h"
#include "master_server_monitor.hpp"

std::unique_ptr<server_monitor> master_Server_monitor = nullptr;


server_monitor::server_monitor ()
{

  m_server_entry_map = std::unordered_map<std::string, server_entry> ();
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_map is created. \n");

  {
    std::lock_guard<std::mutex> lock (m_server_monitor_mutex);
    m_thread_shutdown = false;
  }

  start_monitoring_thread ();

  fflush (stdout);
}

// In server_monitor destructor, it should guarentee that
// m_monitoring_thread is terminated before m_monitor_list is deleted.
server_monitor::~server_monitor ()
{
  stop_monitoring_thread ();

  assert (m_server_entry_map.size () == 0);
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_entry_map is deleted. \n");

  fflush (stdout);
}

void
server_monitor::start_monitoring_thread ()
{
  m_monitoring_thread = std::make_unique<std::thread> (&server_monitor::server_monitor_thread_worker, this);
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is created. \n");
}

void
server_monitor::stop_monitoring_thread ()
{
  {
    std::lock_guard<std::mutex> lock (m_server_monitor_mutex);
    m_thread_shutdown = true;
  }
  m_monitor_cv_consumer.notify_all();

  m_monitoring_thread->join();
  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : server_monitor_thread is terminated. \n");
}

void
server_monitor::server_monitor_thread_worker ()
{
  job job;

  while (true)
    {
      {
	std::unique_lock<std::mutex> lock (m_server_monitor_mutex);

	m_monitor_cv_consumer.wait (lock, [this]
	{
	  return !m_job_queue.empty () || m_thread_shutdown;
	});

	if (m_thread_shutdown)
	  {
	    break;
	  }
	else
	  {
	    assert (!m_job_queue.empty ());
	    job = consume_job ();
	  }
      }
      process_job (job);
    }
}

void
server_monitor::register_server_entry (int pid, const std::string &exec_path, const std::string &args,
				       const std::string &server_name
				      )
{
  auto entry = m_server_entry_map.find (server_name);

  if (entry != m_server_entry_map.end ())
    {
      entry->second.set_pid (pid);
      entry->second.set_need_revive (false);
      entry->second.set_registered_time (std::chrono::steady_clock::now ());
      fprintf (stdout,
	       "[SERVER_REVIVE_DEBUG] : server entry has been updated in master_Server_monitor : pid : %d, exec_path : %s, args : %s\n",
	       pid,
	       exec_path.c_str(), args.c_str());
    }
  else
    {
      m_server_entry_map.emplace (std::move (server_name), server_entry (pid, exec_path, args,
				  std::chrono::steady_clock::now ()));

      fprintf (stdout,
	       "[SERVER_REVIVE_DEBUG] : server entry has been registered into master_Server_monitor : pid : %d, exec_path : %s, args : %s\n",
	       pid,
	       exec_path.c_str(), args.c_str());
    }
}

void
server_monitor::remove_server_entry (const std::string &server_name)
{
  auto entry = m_server_entry_map.find (server_name);
  assert (entry != m_server_entry_map.end ());

  m_server_entry_map.erase (entry);
  fprintf (stdout,
	   "[SERVER_REVIVE_DEBUG] : server entry has been removed from master_Server_monitor. Number of server in master_Server_monitor: %d\n",
	   m_server_entry_map.size ());
}

void
server_monitor::revive_server (const std::string &server_name)
{
  int error_code;

  // Unacceptable revive time difference is set to be 120 seconds
  // as the timediff of server restart mechanism of heartbeat.
  constexpr int SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_SECS = 120;
  std::chrono::steady_clock::time_point tv;
  int out_pid;

  auto entry = m_server_entry_map.find (server_name);

  if (entry != m_server_entry_map.end ())
    {
      entry->second.set_need_revive (true);

      tv = std::chrono::steady_clock::now ();
      auto timediff = std::chrono::duration_cast<std::chrono::seconds> (tv -
		      entry->second.get_registered_time()).count();

      if (timediff > SERVER_MONITOR_UNACCEPTABLE_REVIVE_TIMEDIFF_IN_SECS)
	{
	  out_pid = try_revive_server (entry->second.get_exec_path(), entry->second.get_argv());
	  if (out_pid == -1)
	    {
	      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Fork at server revive failed. exec_path : %s, args : %s\n",
		       entry->second.get_exec_path().c_str(), entry->second.get_argv()[0]);
	      produce_job_internal (job_type::REVIVE_SERVER, -1, "", "", entry->first);
	    }
	  else
	    {
	      entry->second.set_pid (out_pid);
	      produce_job_internal (job_type::CONFIRM_REVIVE_SERVER, -1, "", "",
				    entry->first);
	    }
	  return;
	}
      else
	{
	  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Process failure repeated within a short period of time. pid : %d\n",
		   entry->second.get_pid());
	  m_server_entry_map.erase (entry);
	  return;
	}
    }
}

void
server_monitor::check_server_revived (const std::string &server_name)
{
  int error_code;
  auto entry = m_server_entry_map.find (server_name);
  assert (entry != m_server_entry_map.end ());

  error_code = kill (entry->second.get_pid (), 0);
  if (error_code)
    {
      if (errno == ESRCH)
	{
	  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Can't find server process. pid : %d\n", entry->second.get_pid());
	  produce_job (job_type::REVIVE_SERVER, -1, "", "", entry->first);
	}
      else
	{
	  fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Server revive failed. pid : %d\n", entry->second.get_pid());
	  m_server_entry_map.erase (entry);
	  kill (entry->second.get_pid (), SIGKILL);
	}
    }
  else if (entry->second.get_need_revive ())
    {
      // Server revive confirm interval is set to be 1 second to avoid busy waiting.
      constexpr int SERVER_MONITOR_CONFIRM_REVIVE_INTERVAL_IN_SECS = 1;

      std::this_thread::sleep_for (std::chrono::seconds (SERVER_MONITOR_CONFIRM_REVIVE_INTERVAL_IN_SECS));
      produce_job (job_type::CONFIRM_REVIVE_SERVER, -1, "", "",
		   entry->first);
    }
  else
    {
      fprintf (stdout, "[SERVER_REVIVE_DEBUG] : Server revive success. pid : %d\n", entry->second.get_pid());
    }
  return;
}

int
server_monitor::try_revive_server (const std::string &exec_path, char *const *argv)
{
  pid_t pid;

  pid = fork ();
  if (pid < 0)
    {
      return -1;
    }
  else if (pid == 0)
    {
      execv (exec_path.c_str(), argv);
    }
  else
    {
      return pid;
    }
}

void
server_monitor::produce_job_internal (job_type job_type, int pid, const std::string &exec_path,
				      const std::string &args, const std::string &server_name)
{
  server_monitor::job job (job_type, pid, exec_path, args, server_name);

  {
    std::lock_guard<std::mutex> lock (m_server_monitor_mutex);
    m_job_queue.emplace (job);
  }
}

void
server_monitor::produce_job (job_type job_type, int pid, const std::string &exec_path,
			     const std::string &args, const std::string &server_name)
{
  produce_job_internal (job_type, pid, exec_path, args, server_name);
  m_monitor_cv_consumer.notify_all();
}

server_monitor::job
server_monitor::consume_job ()
{
  job consume_job;

  consume_job = m_job_queue.front ();
  m_job_queue.pop ();

  return consume_job;
}

void
server_monitor::process_job (job consume_job)
{
  switch (consume_job.get_job_type ())
    {
    case job_type::REGISTER_SERVER:
      register_server_entry (consume_job.get_pid(), consume_job.get_exec_path(), consume_job.get_args(),
			     consume_job.get_server_name());

      break;
    case job_type::UNREGISTER_SERVER:
      remove_server_entry (consume_job.get_server_name());
      break;
    case job_type::REVIVE_SERVER:
      revive_server (consume_job.get_server_name());
      break;
    case job_type::CONFIRM_REVIVE_SERVER:
      check_server_revived (consume_job.get_server_name());
      break;
    case job_type::JOB_MAX:
    default:
      assert (false);
      break;
    }
}

server_monitor::job::
job (job_type job_type, int pid, const std::string &exec_path, const std::string &args,
     const std::string &server_name)
  : m_job_type {job_type}
  , m_pid {pid}
  , m_exec_path {exec_path}
  , m_args {args}
  , m_server_name {server_name}
{
}

server_monitor::job_type
server_monitor::job::get_job_type () const
{
  return m_job_type;
}

int
server_monitor::job::get_pid () const
{
  return m_pid;
}

std::string
server_monitor::job::get_exec_path () const
{
  return m_exec_path;
}

std::string
server_monitor::job::get_args () const
{
  return m_args;
}

std::string
server_monitor::job::get_server_name () const
{
  return m_server_name;
}

server_monitor::server_entry::
server_entry (int pid, const std::string &exec_path, const std::string &args,
	      std::chrono::steady_clock::time_point revive_time)
  : m_pid {pid}
  , m_exec_path {exec_path}
  , m_need_revive {false}
  , m_registered_time {revive_time}
{
  if (args.size() > 0)
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

char *const *
server_monitor::server_entry::get_argv () const
{
  return m_argv.get ();
}

bool
server_monitor::server_entry::get_need_revive () const
{
  return m_need_revive;
}

std::chrono::steady_clock::time_point
server_monitor::server_entry::get_registered_time () const
{
  return m_registered_time;
}

void
server_monitor::server_entry::set_pid (int pid)
{
  m_pid = pid;
}

void
server_monitor::server_entry::set_exec_path (const std::string &exec_path)
{
  m_exec_path = exec_path;
}

void
server_monitor::server_entry::set_need_revive (bool need_revive)
{
  m_need_revive = need_revive;
}

void
server_monitor::server_entry::set_registered_time (std::chrono::steady_clock::time_point revive_time)
{
  m_registered_time = revive_time;
}

void
server_monitor::server_entry::proc_make_arg (const std::string &args)
{
  //argv is type of std::unique_ptr<const char *[]>
  m_argv = std::make_unique<char *[]> (args.size () + 1);
  std::istringstream iss (args);
  std::string arg;
  int i = 0;
  while (std::getline (iss, arg, ' '))
    {
      m_argv[i] = new char[arg.size () + 1];
      std::copy (arg.begin (), arg.end (), m_argv[i]);
      i++;
    }
  m_argv[args.size()] = nullptr;
}
