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

class server_monitor
{
  public:
    class server_entry
    {
      public:
	server_entry (int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn);
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
	      m_revive_count = other.m_revive_count;
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
	void set_last_revive_time ();

	int m_revive_count;                                           // revive count of server process

	void proc_make_arg (char *args);
	bool server_entry_compare_args_and_argv (const char *args);

      private:
	int m_pid;                                                    // process ID of server process
	std::string m_exec_path;                                      // executable path of server process
	std::vector<std::string> m_argv;                              // arguments of server process
	CSS_CONN_ENTRY *m_conn;                                       // connection entry of server process
	volatile bool m_need_revive;                                  // need to revive (true if the server is abnormally terminated)
	std::chrono::steady_clock::time_point m_last_revive_time;     // last revive time
	std::mutex m_entry_mutex;                                     // lock for server entry
    };

    server_monitor ();
    ~server_monitor ();

    server_monitor (const server_monitor &) = delete;
    server_monitor (server_monitor &&) = delete;

    server_monitor &operator = (const server_monitor &) = delete;
    server_monitor &operator = (server_monitor &&) = delete;

    void make_and_insert_server_entry (int pid, const char *exec_path, char *args,
				       CSS_CONN_ENTRY *conn);
    void remove_server_entry_by_conn (CSS_CONN_ENTRY *conn);
    void find_set_entry_to_revive (CSS_CONN_ENTRY *conn);
    void server_monitor_try_revive_server (std::string exec_path, std::vector<std::string> argv, int *out_pid);
    int server_monitor_check_server_revived (server_monitor::server_entry &sentry);

  private:
    std::unique_ptr<std::list <server_entry>> m_server_entry_list;      // list of server entries
    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
    std::mutex m_monitor_mutex;                                         // lock for server entry list
    std::condition_variable m_monitor_cv;                               // condition variable for server entry list
    std::atomic_int m_revive_entry_count;                               // count of server entries for revive
};

extern std::unique_ptr<server_monitor> master_Server_monitor;

#endif
