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
#include <list>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include "connection_defs.h"
#include "connection_globals.h"
#include "lockfree_circular_queue.hpp"

typedef enum
{
  SERVER_MONITOR_NO_JOB,
  SERVER_MONITOR_REGISTER_ENTRY,
  SERVER_MONITOR_REMOVE_ENTRY,
  SERVER_MONITOR_REVIVE_ENTRY
} server_monitor_job_type;

class server_monitor
{
  public:
    class server_entry
    {
      public:
	server_entry (int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn,
		      std::chrono::steady_clock::time_point revive_time);
	~server_entry () {};

	server_entry (const server_entry &) = delete;
	server_entry (server_entry &&) = delete;

	server_entry &operator= (const server_entry &) = delete;
	server_entry &operator= (server_entry &&other)
	{
	  if (this != &other)
	    {
	      m_pid = other.m_pid;
	      m_need_revive = other.m_need_revive;
	      m_conn = other.m_conn;
	      m_last_revive_time = other.m_last_revive_time;
	      m_retries = other.m_retries;
	      m_exec_path = other.m_exec_path;
	      m_argv = other.m_argv;
	    }
	  return *this;
	}

	int get_pid () const;
	std::string get_exec_path () const;
	std::vector<std::string> get_argv () const;
	CSS_CONN_ENTRY *get_conn () const;
	bool get_need_revive () const;
	std::chrono::steady_clock::time_point get_last_revive_time () const;

	void set_pid (int pid);
	void set_exec_path (const char *exec_path);
	void set_conn (CSS_CONN_ENTRY *conn);
	void set_need_revive (bool need_revive);
	void set_last_revive_time (std::chrono::steady_clock::time_point revive_time);

	void proc_make_arg (char *args);
	bool server_entry_compare_args_and_argv (const char *args);

	int m_retries;                                             // retry count of server process

      private:
	int m_pid;                                                 // process ID of server process
	std::string m_exec_path;                                   // executable path of server process
	std::vector<std::string> m_argv;                           // arguments of server process
	CSS_CONN_ENTRY *m_conn;                                    // connection entry of server process
	volatile bool m_need_revive;                               // need to be revived by monitoring thread
	std::chrono::steady_clock::time_point m_last_revive_time;  // last revive time

    };

    class server_monitor_job
    {
      public:

	server_monitor_job_type m_job_type;
	int m_pid;
	const char *m_exec_path;
	char *m_args;
	CSS_CONN_ENTRY *m_conn;
	std::chrono::steady_clock::time_point m_produce_time;

	server_monitor_job (server_monitor_job_type job_type, int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn);
	server_monitor_job () : m_job_type (SERVER_MONITOR_NO_JOB), m_pid (0), m_exec_path (nullptr), m_args (nullptr),
	  m_conn (nullptr) {};
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

    void make_and_insert_server_entry (int pid, const char *exec_path, char *args,
				       CSS_CONN_ENTRY *conn, std::chrono::steady_clock::time_point revive_time);
    void remove_server_entry_by_pid (int pid);
    void revive_server_with_pid (int pid);
    void server_monitor_try_revive_server (std::string exec_path, std::vector<std::string> argv, int *out_pid);
    int server_monitor_check_server_revived (server_monitor::server_entry &sentry);

    void server_monitor_produce_job (server_monitor_job_type job_type, int pid, const char *exec_path, char *args,
				     CSS_CONN_ENTRY *conn);

  private:
    std::unique_ptr<std::list <server_entry>> m_server_entry_list;      // list of server entries
    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    lockfree::circular_queue <server_monitor_job> *m_job_queue;         // job queue for monitoring thread
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
    std::mutex m_monitor_mutex;                                         // lock for server monitor
    std::condition_variable m_monitor_cv;                               // condition variable for server monitor

    void start_monitoring_thread ();
    void stop_monitoring_thread ();
    void server_monitor_thread_worker ();
};

extern std::unique_ptr<server_monitor> master_Server_monitor;

#endif
