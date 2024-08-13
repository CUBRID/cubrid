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

/*
 * master_server_monitor.hpp -
 */

#ifndef _MASTER_SERVER_MONITOR_HPP_
#define _MASTER_SERVER_MONITOR_HPP_

#include <cstring>
#include <thread>
#include <vector>
#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "connection_defs.h"
#include "connection_globals.h"
#include "lockfree_circular_queue.hpp"

class server_monitor
{
  public:

    enum class job_type
    {
      REGISTER_ENTRY = 0,
      REMOVE_ENTRY = 1,
      REVIVE_ENTRY = 2,
      CONFIRM_REVIVE_ENTRY =  3,
      JOB_MAX
    };

    class server_entry
    {
      public:
	server_entry (int pid, std::string exec_path, std::string args,
		      std::chrono::steady_clock::time_point revive_time);
	~server_entry () {};

	server_entry (const server_entry &) = delete;
	server_entry (server_entry &&) = default;

	server_entry &operator= (const server_entry &) = delete;
	server_entry &operator= (server_entry &&other)
	{
	  if (this != &other)
	    {
	      m_pid = other.m_pid;
	      m_need_revive = other.m_need_revive;
	      m_last_revive_time = other.m_last_revive_time;
	      m_exec_path = other.m_exec_path;
	      m_argv = other.m_argv;
	    }
	  return *this;
	}

	int get_pid () const;
	std::string get_exec_path () const;
	std::vector<std::string> get_argv () const;
	bool get_need_revive () const;
	std::chrono::steady_clock::time_point get_last_revive_time () const;

	void set_pid (int pid);
	void set_exec_path (std::string exec_path);
	void set_need_revive (bool need_revive);
	void set_last_revive_time (std::chrono::steady_clock::time_point revive_time);

	void proc_make_arg (std::string args);

      private:
	int m_pid;                                                 // process ID of server process
	std::string m_exec_path;                                   // executable path of server process
	std::vector<std::string> m_argv;                           // arguments of server process
	volatile bool m_need_revive;                               // need to be revived by monitoring thread
	std::chrono::steady_clock::time_point m_last_revive_time;  // last revive time
    };

    class server_monitor_job
    {
      public:

	job_type m_job_type;
	int m_pid;
	std::string m_exec_path;
	std::string m_args;
	std::string m_server_name;
	std::chrono::steady_clock::time_point m_produce_time;

	server_monitor_job (job_type job_type, int pid, std::string exec_path, std::string args,
			    std::string server_name);
	server_monitor_job () : m_job_type (job_type::JOB_MAX), m_pid (0), m_exec_path (""), m_args (""),
	  m_server_name ("") {};
	~server_monitor_job () {};

	server_monitor_job (const server_monitor_job &) = default;
	server_monitor_job (server_monitor_job &&) = delete;

	server_monitor_job &operator= (const server_monitor_job &) = default;
	server_monitor_job &operator= (server_monitor_job &&) = default;
    };

    server_monitor ();
    ~server_monitor ();

    server_monitor (const server_monitor &) = delete;
    server_monitor (server_monitor &&) = delete;

    server_monitor &operator = (const server_monitor &) = delete;
    server_monitor &operator = (server_monitor &&) = delete;

    void make_and_insert_server_entry (int pid, std::string exec_path, std::string args, std::string server_name,
				       std::chrono::steady_clock::time_point revive_time);
    void remove_server_entry (std::string server_name);
    void revive_server (std::string server_name);
    void try_revive_server (std::string exec_path, std::vector<std::string> argv, int *out_pid);
    void check_server_revived (std::string server_name);

    void produce_job (job_type job_type, int pid, std::string exec_path, std::string args,
		      std::string server_name);

  private:
    std::unordered_map <std::string, server_entry> m_server_entry_map;  // map of server entries

    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    lockfree::circular_queue <server_monitor_job> *m_job_queue;         // job queue for monitoring thread
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
    std::mutex m_monitor_mutex_consumer;                                // lock for m_job_queue empty check
    std::mutex m_monitor_mutex_producer;                                // lock for m_job_queue full check
    std::condition_variable m_monitor_cv_consumer;                      // condition variable for m_job_queue empty check
    std::condition_variable m_monitor_cv_producer;                      // condition variable for m_job_queue full check

    void start_monitoring_thread ();
    void stop_monitoring_thread ();
    void server_monitor_thread_worker ();
};

extern std::unique_ptr<server_monitor> master_Server_monitor;

#endif
