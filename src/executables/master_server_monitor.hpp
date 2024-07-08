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
#include <time.h>
#include "heartbeat.h"
#include "system_parameter.h"
#include "connection_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif

class server_monitor
{
  public:
    class server_entry;
    class server_monitor_list;

    using server_entry_uptr_t = std::unique_ptr<server_monitor::server_entry>;

    static server_monitor &get_instance ()
    {
      static server_monitor instance;
      return instance;
    }

    server_monitor (const server_monitor &) = delete;
    server_monitor (server_monitor &&) = delete;

    server_monitor &operator = (const server_monitor &) = delete;
    server_monitor &operator = (server_monitor &&) = delete;

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
	const int m_pid;
	const std::string m_server_name;
	const std::string m_exec_path;
	std::vector<std::string> m_argv;
	CSS_CONN_ENTRY *m_conn;
	timeval m_last_revive_time;
	bool m_need_revive;

      private:
	void proc_make_arg (char *args);
    };

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
	std::vector <server_entry_uptr_t> server_entry_list;
	int m_unacceptable_proc_restart_timediff;
	int m_max_process_start_confirm;
    };

  private:
    server_monitor ();
    ~server_monitor ();

    std::unique_ptr<std::thread> m_monitoring_thread;
    std::unique_ptr<server_monitor_list> m_monitor_list;
    bool m_thread_shutdown;
};

#ifdef __cplusplus
}
#endif

#endif
