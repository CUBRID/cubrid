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
 * master_util.h : common module for commdb and master
 *
 */

#ifndef _MASTER_UTIL_H_
#define _MASTER_UTIL_H_

#ident "$Id$"

#include "connection_defs.h"

#if defined(WINDOWS)
#define SLEEP_SEC(X)                    Sleep((X) * 1000)
#define SLEEP_MILISEC(sec, msec)        Sleep((sec) * 1000 + (msec))
#else
#define SLEEP_SEC(X)                    sleep(X)
#define SLEEP_MILISEC(sec, msec)        \
	do {            \
		struct timeval sleep_time_val;                \
		sleep_time_val.tv_sec = sec;                  \
		sleep_time_val.tv_usec = (msec) * 1000;       \
		select(0, 0, 0, 0, &sleep_time_val);          \
	} while(0)
#endif

// todo: MASTER_ER_... shouldn't be necessary anymore. each thread has own error context
#define MASTER_ER_SET(...) \
  do { \
      if (css_Master_er_log_enabled == true) { \
      pthread_mutex_lock (&css_Master_er_log_lock); \
      er_set (__VA_ARGS__); \
      pthread_mutex_unlock (&css_Master_er_log_lock); \
      } \
  } while (0)

#define MASTER_ER_SET_WITH_OSERROR(...) \
  do { \
      if (css_Master_er_log_enabled == true) { \
      pthread_mutex_lock (&css_Master_er_log_lock); \
      er_set_with_oserror (__VA_ARGS__); \
      pthread_mutex_unlock (&css_Master_er_log_lock); \
      } \
  } while (0)

#define MASTER_ER_LOG_DEBUG(...) \
  do { \
      if (css_Master_er_log_enabled == true) { \
      pthread_mutex_lock (&css_Master_er_log_lock); \
      er_log_debug (__VA_ARGS__); \
      pthread_mutex_unlock (&css_Master_er_log_lock); \
      } \
  } while (0)

typedef struct socket_queue_entry SOCKET_QUEUE_ENTRY;
struct socket_queue_entry
{
  SOCKET fd;
  int fd_type;
  int db_error;
  int queue_p;
  int info_p;
  int error_p;
  int pid;
  char *name;
  char *version_string;
  char *env_var;
  CSS_CONN_ENTRY *conn_ptr;
  int port_id;
  int ha_mode;
  struct socket_queue_entry *next;
};

extern bool master_util_config_startup (const char *db_name, int *port_id);
extern void master_util_wait_proc_terminate (int pid);

extern pthread_mutex_t css_Master_er_log_lock;
extern pthread_mutex_t css_Master_er_log_enable_lock;
extern bool css_Master_er_log_enabled;

#define GET_REAL_MASTER_CONN_NAME(name)         (((char *) name) + 1)
#define IS_MASTER_CONN_NAME_DRIVER(name)        (*((char *)name) == '-')
#define IS_MASTER_CONN_NAME_HA_SERVER(name)     (*((char *)name) == '#')
#define IS_MASTER_CONN_NAME_HA_COPYLOG(name)    (*((char *)name) == '$')
#define IS_MASTER_CONN_NAME_HA_APPLYLOG(name)   (*((char *)name) == '%')

#endif /* _MASTER_UTIL_H_ */
