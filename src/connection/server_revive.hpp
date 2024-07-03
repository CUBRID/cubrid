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
 * server_revive.hpp -
 */


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

void proc_make_arg (char **arg, char *args);

class master_monitor
{
  public:
    master_monitor ();
    ~master_monitor ();

    master_monitor (const master_monitor &) = delete;
    master_monitor (master_monitor &&) = delete;

    master_monitor &operator = (const master_monitor &) = delete;
    master_monitor &operator = (master_monitor &&) = delete;

    void make_and_insert_server_entry (int pid, const char *server_name, const char *exec_path, char *args,
				       CSS_CONN_ENTRY *conn);
    void master_monitor_worker (void);

    class server_entry
    {
      public:
	server_entry (int pid, const char *server_name, const char *exec_path, char *args, CSS_CONN_ENTRY *conn);
	~server_entry ();

	server_entry (const server_entry &) = delete;
	server_entry (server_entry &&) = delete;

	server_entry &operator = (const server_entry &) = delete;
	server_entry &operator = (server_entry &&) = delete;

	int pid;
	char *server_name;
	char *exec_path;
	char **argv;
	CSS_CONN_ENTRY *conn;
	timeval last_revive_time;
	bool need_revive;
    };

    class master_monitor_list
    {
      public:

	master_monitor_list ();
	~master_monitor_list () {};

	master_monitor_list (const master_monitor_list &) = delete;
	master_monitor_list (master_monitor_list &&) = delete;

	master_monitor_list &operator = (const master_monitor_list &) = delete;
	master_monitor_list &operator = (master_monitor_list &&) = delete;

	std::vector <std::unique_ptr<server_entry>>server_entry_list;
	int unacceptable_proc_restart_timediff;
	int max_process_start_confirm;
    };

    void remove_server_entry (std::unique_ptr<server_entry> entry);
    std::unique_ptr<server_entry> get_server_entry_by_conn (CSS_CONN_ENTRY *conn);
    void revive_server (std::unique_ptr<server_entry> entry);

  private:
    std::unique_ptr<std::thread> m_monitoring_thread;
    std::unique_ptr<master_monitor_list> m_monitor_list;
};

#ifdef __cplusplus
}
#endif
