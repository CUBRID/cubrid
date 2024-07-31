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
#include <time.h>
#include <mutex>
#include <master_util.h>
#include <master_request.h>
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
	server_entry &operator= (server_entry && other)
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

	CSS_CONN_ENTRY *get_conn () const;
	bool get_need_revive () const;
	void set_need_revive (bool need_revive);
	struct timeval get_last_revive_time () const;

	int m_revive_count;                           // revive count of server process
      	std::string m_exec_path;                      // executable path of server process
	std::vector<std::string> m_argv;              // arguments of server process

      private:
	void proc_make_arg (char *args);

	int m_pid;                                    // process ID of server process
	bool m_need_revive;                           // need to revive (true if the server is abnormally terminated)
	CSS_CONN_ENTRY *m_conn;                       // connection entry of server process
	timeval m_last_revive_time;                   // latest revive time
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
    int get_server_entry_count () const;

    std::mutex m_server_entry_list_lock;                                 // lock for server entry list

  private:
    std::unique_ptr<std::list <server_entry>> m_server_entry_list;      // list of server entries
    std::unique_ptr<std::thread> m_monitoring_thread;                   // monitoring thread
    volatile bool m_thread_shutdown;                                    // flag to shutdown monitoring thread
};

extern std::unique_ptr<server_monitor> master_Server_monitor;
extern SOCKET_QUEUE_ENTRY *css_Master_socket_anchor;
extern pthread_mutex_t css_Master_socket_anchor_lock;
extern void css_remove_entry_by_conn (CSS_CONN_ENTRY * conn_p, SOCKET_QUEUE_ENTRY ** anchor_p);
#endif
