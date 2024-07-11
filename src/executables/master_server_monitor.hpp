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
#include <memory>
#include <time.h>
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

    server_monitor ();
    ~server_monitor ();

    server_monitor (const server_monitor &) = delete;
    server_monitor (server_monitor &&) = delete;

    server_monitor &operator = (const server_monitor &) = delete;
    server_monitor &operator = (server_monitor &&) = delete;

  private:
    std::unique_ptr<std::vector <server_entry>> m_server_entry_list;    // list of server entries
    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
};

extern std::unique_ptr<server_monitor> master_Server_monitor;
#endif
