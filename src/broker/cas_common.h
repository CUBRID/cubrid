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
 * cas_common.h -
 */

#ifndef	_CAS_COMMON_H_
#define	_CAS_COMMON_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/socket.h>
#endif

#include "porting.h"

#define makestring1(x) #x
#define makestring(x) makestring1(x)

#define MAX_SERVER_H_ID         256
#define MAX_BIND_VALUE          10240
#define MAX_QUERY_LEN           100000
#define CAS_RUNNER_CONF         "cas_runner.conf"
#define CAS_RUNNER_CONF_ENV     "CAS_RUNNER_CONF"

#define ON	1
#define OFF	0

#define TRUE	1
#define FALSE	0

#define INT_STR_LEN     16

#define MALLOC(SIZE)            malloc(SIZE)
#define REALLOC(PTR, SIZE)      \
        ((PTR == NULL) ? malloc(SIZE) : realloc(PTR, SIZE))
#define FREE(PTR)               free(PTR)

#define FREE_MEM(PTR)		\
	do {			\
	  if (PTR) {		\
	    FREE(PTR);		\
	    PTR = 0;	\
	  }			\
	} while (0)

#define ALLOC_COPY(PTR, STR)			\
	do {					\
	  if (STR == NULL)			\
	    PTR = NULL;				\
	  else {				\
	    PTR = (char *) MALLOC(strlen(STR) + 1);	\
	    if (PTR) {				\
	      strcpy(PTR, STR);			\
	    }					\
	  }					\
	} while (0)

#if defined(WINDOWS)
#define CLOSE_SOCKET(X)		\
	do {			\
	  if (!IS_INVALID_SOCKET(X)) closesocket(X);	\
	  (X) = INVALID_SOCKET;	\
	} while (0)
#else
#define CLOSE_SOCKET(X)		\
	do {			\
	  if (!IS_INVALID_SOCKET(X)) close(X);		\
	  (X) = INVALID_SOCKET;	\
	} while (0)
#endif

#define ALLOC_N_COPY(PTR, STR, SIZE)		\
	do {					\
	  if (STR == NULL)			\
	    PTR = NULL;				\
	  else {				\
	    PTR = MALLOC(SIZE);			\
	    if (PTR) {				\
	      strncpy(PTR, STR, SIZE);		\
	      PTR[SIZE - 1] = '\0';		\
	    }					\
	  }					\
	} while (0)

#if defined(WINDOWS)
#define SLEEP_SEC(X)                    Sleep((X) * 1000)
#define SLEEP_MILISEC(SEC, MSEC)	Sleep((SEC) * 1000 + (MSEC))
#else
#define SLEEP_SEC(X)                    sleep(X)
#define SLEEP_MILISEC(sec, msec)			\
	do {						\
	  struct timeval sleep_time_val;		\
	  sleep_time_val.tv_sec = sec;			\
	  sleep_time_val.tv_usec = (msec) * 1000;	\
	  select(0, 0, 0, 0, &sleep_time_val);		\
	} while(0)
#endif


#if defined(WINDOWS)
#define THREAD_BEGIN(THR_ID, FUNC, ARG)				\
	do {							\
	  THR_ID = _beginthread(FUNC, 0, (void*) (ARG));	\
	} while(0)
#else
#define THREAD_BEGIN(THR_ID, FUNC, ARG)		\
	do {					\
	  pthread_attr_t	thread_attr;	\
	  pthread_attr_init(&thread_attr);	\
	  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED); \
	  pthread_create(&(THR_ID), &thread_attr, FUNC, ARG);	\
	} while (0)
#endif


#if defined(WINDOWS)
#define READ_FROM_SOCKET(fd, buf, size)         recv(fd, buf, size, 0)
#define WRITE_TO_SOCKET(fd, buf, size)          send(fd, buf, size, 0)
#else
#define READ_FROM_SOCKET(fd, buf, size)         read(fd, buf, size)
#define WRITE_TO_SOCKET(fd, buf, size)          write(fd, buf, size)
#endif

#if defined(WINDOWS)
#define THREAD_FUNC     void
#else
#define THREAD_FUNC     void*
#endif

#if defined(WINDOWS) || defined(SOLARIS) || defined(HPUX)
typedef int T_SOCKLEN;
#elif defined(UNIXWARE7)
typedef size_t T_SOCKLEN;
#else
typedef socklen_t T_SOCKLEN;
#endif

extern int uts_key_check_local_host (void);

#endif /* _CAS_COMMON_H_ */
