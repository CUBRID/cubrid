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
 * shard_proxy_log.h -
 */

#ifndef	_SHARD_PROXY_LOG_H_
#define	_SHARD_PROXY_LOG_H_

#ident "$Id$"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#if defined(WINDOWS)
#include <sys/timeb.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "broker_shm.h"
#include "broker_config.h"

extern void proxy_log_open (char *br_name, int proxy_index);
/* SHARD TODO : SUPPORT CUBRID MANAGER 
extern void proxy_log_reset (char *br_name, int proxy_index);
*/
extern void proxy_log_close (void);
extern void proxy_access_log_close (void);
extern void proxy_log_end (void);
extern void proxy_log_write (int level, char *svc_code, const char *fmt, ...);
extern int proxy_log_get_level (void);

extern int proxy_access_log (struct timeval *start_time,
			     int client_ip_addr, const char *dbname,
			     const char *dbuser, bool accepted);

#if defined(WINDOWS)
#define PROXY_LOG(level, fmt, ...)					\
  do {												\
     if (level > PROXY_LOG_MODE_NONE 				\
        && level <= PROXY_LOG_MODE_ALL 				\
	    && level <= proxy_log_get_level())			\
     {												\
         proxy_log_write (level, 					\
		 					NULL, "%s(%d): " fmt,	\
							__FILE__, __LINE__,		\
							__VA_ARGS__);			\
     }												\
     } while(0)

#define PROXY_DEBUG_LOG(fmt, ...) 					\
  do {												\
     if (PROXY_LOG_MODE_DEBUG <= proxy_log_get_level())	\
     {												\
		 proxy_log_write (PROXY_LOG_MODE_DEBUG, 	\
		 					NULL, "%s(%d): " fmt,	\
				  			__FILE__, __LINE__,		\
							__VA_ARGS__);			\
     }												\
  } while(0)
#else /* WINDOWS */
#define PROXY_LOG(level, fmt, args...)				\
  do {												\
     if (level > PROXY_LOG_MODE_NONE 				\
        && level <= PROXY_LOG_MODE_ALL 				\
	    && level <= proxy_log_get_level())			\
     {												\
         proxy_log_write (level, 					\
		 					NULL, "%s(%d): " fmt,	\
							__FILE__, __LINE__,		\
							##args);				\
     }												\
     } while(0)

#define PROXY_DEBUG_LOG(fmt, args...) 				\
  do {												\
     if (PROXY_LOG_MODE_DEBUG <= proxy_log_get_level())	\
     {												\
		 proxy_log_write (PROXY_LOG_MODE_DEBUG, 	\
		 					NULL, "%s(%d): " fmt,	\
				  			__FILE__, __LINE__,		\
							##args);				\
     }												\
  } while(0)
#endif /* !WINDOWS */

#if defined(NDEBUG) && defined(PROXY_VERBOSE_DEBUG)
#define ENTER_FUNC() PROXY_DEBUG_LOG("ENTER")
#else /* NDEBUG && PROXY_VERBOSE_DEBUG */
#define ENTER_FUNC()
#endif

#if defined(NDEBUG) && defined(PROXY_VERBOSE_DEBUG)
#define EXIT_FUNC()  PROXY_DEBUG_LOG("EXIT")
#else /* NDEBUG && PROXY_VERBOSE_DEBUG */
#define EXIT_FUNC()
#endif

#if defined(NDEBUG) && defined(PROXY_VERBOSE_DEBUG)
#define DEBUG_FUNC()  PROXY_DEBUG_LOG("DEBUG")
#else /* NDEBUG && PROXY_VERBOSE_DEBUG */
#define DEBUG_FUNC()
#endif

#endif /* _SHARD_PROXY_LOG_H_ */
