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
#include <algorithm>
#include <memory>
#include <mutex>
#include <time.h>
#include "heartbeat.h"
#include "system_parameter.h"
#include "connection_defs.h"

class server_monitor
{
  public:
    class server_entry
    {
      public:
	server_entry (int pid, const char *server_name, const char *exec_path, char *args, CSS_CONN_ENTRY *conn);
	~server_entry () {};

	server_entry (const server_entry &) = delete;
	server_entry (server_entry &&) = delete;

	server_entry &operator = (const server_entry &) = delete;
	server_entry &operator = (server_entry &&) = delete;

      private:
	void proc_make_arg (char *args);

	const int m_pid;                              // process ID
	const std::string m_server_name;              // server name
	const std::string m_exec_path;                // executable path of server process
	std::vector<std::string> m_argv;              // arguments of server process
	CSS_CONN_ENTRY *m_conn;                       // connection entry of server process
	timeval m_last_revive_time;                   // latest revive time
	bool m_need_revive;                           // need to revive (true if the server is abnormally terminated)
    };

    using server_entry_uptr_t = std::unique_ptr<server_monitor::server_entry>;

    class server_monitor_list
    {
      public:
	server_monitor_list ();
	~server_monitor_list () {};

	server_monitor_list (const server_monitor_list &) = delete;
	server_monitor_list (server_monitor_list &&) = delete;

	server_monitor_list &operator = (const server_monitor_list &) = delete;
	server_monitor_list &operator = (server_monitor_list &&) = delete;

      private:
	std::vector <server_entry_uptr_t> server_entry_list;      // list of server entries
	int m_unacceptable_proc_restart_timediff;                 // unacceptable time difference between process restart
	int m_max_process_start_confirm;                          // Maximum number of process restart confirmations
    };

    static server_monitor &get_instance ()
    {
      static server_monitor instance;
      return instance;
    }

    static void delete_instance ()
    {
      server_monitor &instance = get_instance ();
      delete &instance;
    }

  private:
    server_monitor ();
    ~server_monitor ();

    server_monitor (const server_monitor &) = delete;
    server_monitor (server_monitor &&) = delete;

    server_monitor &operator = (const server_monitor &) = delete;
    server_monitor &operator = (server_monitor &&) = delete;

    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    std::unique_ptr<server_monitor_list> m_monitor_list;                // list of server entries
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
};

#endif
