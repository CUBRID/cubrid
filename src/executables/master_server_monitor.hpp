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
#include <queue>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "connection_globals.h"

class server_monitor
{

  public:

    enum class job_type
    {
      REGISTER_SERVER = 0,
      UNREGISTER_SERVER = 1,
      REVIVE_SERVER = 2,
      CONFIRM_REVIVE_SERVER = 3,
      JOB_MAX
    };

  private:

    class job
    {

      public:

	job (job_type job_type, int pid, const std::string &exec_path, const std::string &args,
	     const std::string &server_name);
	job () : m_job_type (job_type::JOB_MAX) {};
	~job () {};

	job (const job &) = default;
	job (job &&) = default;

	job &operator= (const job &) = delete;
	job &operator= (job &&) = default;

	job_type get_job_type () const;
	int get_pid () const;
	std::string get_exec_path () const;
	std::string get_args () const;
	std::string get_server_name () const;

      private:

	job_type m_job_type;            // job type
	int m_pid;                      // process ID of server process
	std::string m_exec_path;        // executable path of server process
	std::string m_args;             // arguments of server process
	std::string m_server_name;      // server name
    };

  public:

    server_monitor ();
    ~server_monitor ();

    server_monitor (const server_monitor &) = delete;
    server_monitor (server_monitor &&) = delete;

    server_monitor &operator = (const server_monitor &) = delete;
    server_monitor &operator = (server_monitor &&) = delete;

    void register_server_entry (int pid, const std::string &exec_path, const std::string &args,
				const std::string &server_name);
    void remove_server_entry (const std::string &server_name);
    void revive_server (const std::string &server_name);
    int try_revive_server (const std::string &exec_path, char *const *argv);
    void check_server_revived (const std::string &server_name);

    void produce_job_internal (job_type job_type, int pid, const std::string &exec_path, const std::string &args,
			       const std::string &server_name);
    void produce_job (job_type job_type, int pid, const std::string &exec_path, const std::string &args,
		      const std::string &server_name);

    void consume_job (job &consune_job);
    void process_job (job &consume_job);

  private:

    class server_entry
    {
      public:
	server_entry (int pid, const std::string &exec_path, const std::string &args,
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
	      m_registered_time = other.m_registered_time;
	      m_exec_path = other.m_exec_path;
	      m_argv = std::move (other.m_argv);
	    }
	  return *this;
	}

	int get_pid () const;
	std::string get_exec_path () const;
	char *const *get_argv () const;
	bool get_need_revive () const;
	std::chrono::steady_clock::time_point get_registered_time () const;

	void set_pid (int pid);
	void set_exec_path (const std::string &exec_path);
	void set_need_revive (bool need_revive);
	void set_registered_time (std::chrono::steady_clock::time_point revive_time);

	void proc_make_arg (const std::string &args);

      private:
	int m_pid;                                                  // process ID of server process
	std::string m_exec_path;                                    // executable path of server process
	std::unique_ptr<char *[]> m_argv;                           // arguments of server process
	volatile bool m_need_revive;                                // need to be revived by monitoring thread
	std::chrono::steady_clock::time_point m_registered_time;    // last revive time
    };

    std::unordered_map <std::string, server_entry> m_server_entry_map;  // map of server entries
    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    std::queue<job> m_job_queue;                                        // job queue for monitoring thread
    bool m_thread_shutdown;                                             // flag to shutdown monitoring thread
    std::mutex m_server_monitor_mutex;                                  // lock that syncs m_job_queue and m_thread_shutdown
    std::condition_variable m_monitor_cv_consumer;                      // condition variable for m_job_queue empty check

    void start_monitoring_thread ();
    void stop_monitoring_thread ();
    void server_monitor_thread_worker ();
};

extern std::unique_ptr<server_monitor> master_Server_monitor;
extern bool auto_Restart_server;

#endif
