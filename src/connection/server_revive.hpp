/*
 * Copyright 2008 Search Solution Corporation
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



#include <time.h>
#include <cstring>
#include <thread>
#include <algorithm>
#include "heartbeat.h"
#include "system_parameter.h"
#include "connection_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif

  extern void create_monitor_thread ();
  extern void finalize_monitor_thread ();

  struct SERVER_ENTRY
  {
    int pid;
    char *server_name;
    char *exec_path;
    char **argv;
    CSS_CONN_ENTRY *conn;
    timeval last_revive_time;
    bool need_revive;

      SERVER_ENTRY (int pid, const char *server_name, const char *exec_path, char *args, CSS_CONN_ENTRY * conn);
     ~SERVER_ENTRY ();
  };

  class MASTER_MONITOR_LIST
  {
  public:

    MASTER_MONITOR_LIST ();
    ~MASTER_MONITOR_LIST ();

    void push_server_entry (SERVER_ENTRY * sentry);
    void remove_server_entry (SERVER_ENTRY * sentry);
    SERVER_ENTRY *get_server_entry_by_conn (CSS_CONN_ENTRY * conn);
    void revive_server (SERVER_ENTRY * sentry);

  private:

      std::vector < SERVER_ENTRY * >server_entry_list;
    int unacceptable_proc_restart_timediff;
    int max_process_start_confirm;
  };

#ifdef __cplusplus
}
#endif
