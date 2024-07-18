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
	server_entry (int pid, const char *exec_path, char *args, CSS_CONN_ENTRY *conn);
	~server_entry () {};

	server_entry &operator= (const server_entry &) = default;
	server_entry &operator= (server_entry &&) = default;

	server_entry (const server_entry &) = default;
	server_entry (server_entry &&) = default;

	CSS_CONN_ENTRY *m_conn;                       // connection entry of server process

      private:
	void proc_make_arg (char *args);

	int m_pid;                                    // process ID of server process
	std::string m_exec_path;                      // executable path of server process
	std::vector<std::string> m_argv;              // arguments of server process
	timeval m_last_revive_time;                   // latest revive time
	bool m_need_revive;                           // need to revive (true if the server is abnormally terminated)
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

  private:
    std::unique_ptr<std::vector <server_entry>> m_server_entry_list;    // list of server entries
    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
};

/*
 * server register resource message body
 */

/* process register */
typedef struct server_proc_register SERVER_PROC_REGISTER;
struct server_proc_register
{
  static constexpr int SERVER_MAX_SZ_SERVER_NAME = 256;
  static constexpr int SERVER_MAX_SZ_PROC_EXEC_PATH = 128;
  static constexpr int SERVER_MAX_SZ_PROC_ARGS = 1024;

  char server_name[SERVER_MAX_SZ_SERVER_NAME];
  int server_name_length;
  int pid;
  char exec_path[SERVER_MAX_SZ_PROC_EXEC_PATH];
  char args[SERVER_MAX_SZ_PROC_ARGS];

  inline server_proc_register ();
};

extern std::unique_ptr<server_monitor> master_Server_monitor;

server_proc_register::server_proc_register ()
  : server_name_length (0)
  , pid (0)
{
  memset (server_name, 0, SERVER_MAX_SZ_SERVER_NAME);
  memset (exec_path, 0, SERVER_MAX_SZ_PROC_EXEC_PATH);
  memset (args, 0, SERVER_MAX_SZ_PROC_ARGS);
}

#endif
