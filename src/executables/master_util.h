/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * master_util.h : common module for commdb and master
 *
 */

#ifndef _MASTER_UTIL_H_
#define _MASTER_UTIL_H_

#ident "$Id$"

#include "thread.h"
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

#define MASTER_ER_SET(...) \
  do { \
      pthread_mutex_lock (&css_Master_er_log_lock); \
      er_set (__VA_ARGS__); \
      pthread_mutex_unlock (&css_Master_er_log_lock); \
  } while (0)

#define MASTER_ER_SET_WITH_OSERROR(...) \
  do { \
      pthread_mutex_lock (&css_Master_er_log_lock); \
      er_set_with_oserror (__VA_ARGS__); \
      pthread_mutex_unlock (&css_Master_er_log_lock); \
  } while (0)

#define MASTER_ER_LOG_DEBUG(...) \
  do { \
      pthread_mutex_lock (&css_Master_er_log_lock); \
      er_log_debug (__VA_ARGS__); \
      pthread_mutex_unlock (&css_Master_er_log_lock); \
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

#define GET_REAL_MASTER_CONN_NAME(name)         (((char *) name) + 1)
#define IS_MASTER_CONN_NAME_DRIVER(name)        (*((char *)name) == '-')
#define IS_MASTER_CONN_NAME_HA_SERVER(name)     (*((char *)name) == '#')
#define IS_MASTER_CONN_NAME_HA_COPYLOG(name)    (*((char *)name) == '$')
#define IS_MASTER_CONN_NAME_HA_APPLYLOG(name)   (*((char *)name) == '%')

#endif /* _MASTER_UTIL_H_ */
