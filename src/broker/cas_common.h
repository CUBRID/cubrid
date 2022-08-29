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

#define MAX_SERVER_H_ID                 256
#define MAX_BIND_VALUE                  10240
#define MAX_QUERY_LEN                   100000
#define CAS_RUNNER_CONF                 "cas_runner.conf"
#define CAS_RUNNER_CONF_ENV             "CAS_RUNNER_CONF"
#define CAS_USE_DEFAULT_DB_PARAM        -2

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

#define ALLOC_N_COPY(PTR, STR, SIZE)		\
	do {					\
	  if (STR == NULL)			\
	    PTR = NULL;				\
	  else {				\
	    PTR = (char *) MALLOC(SIZE);			\
	    if (PTR) {				\
	      strncpy(PTR, STR, SIZE);		\
	      PTR[SIZE - 1] = '\0';		\
	    }					\
	  }					\
	} while (0)

#define ALLOC_COPY_STRLEN(PTR, STR)			\
	do {					\
	  ALLOC_N_COPY(PTR, STR, strlen(STR) + 1); \
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
	  THR_ID = (pthread_t) _beginthread(FUNC, 0, (void*) (ARG));	\
	} while(0)
#else
#define THREAD_BEGIN(THR_ID, FUNC, ARG)		\
	do {					\
	  pthread_attr_t	thread_attr;	\
	  pthread_attr_init(&thread_attr);	\
	  pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED); \
	  pthread_create(&(THR_ID), &thread_attr, FUNC, ARG);	\
	  pthread_attr_destroy(&thread_attr);  \
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

enum
{
  FN_STATUS_NONE = -2,
  FN_STATUS_IDLE = -1,
  FN_STATUS_CONN = 0,
  FN_STATUS_BUSY = 1,
  FN_STATUS_DONE = 2
};

/* default charset for JDBC : ISO8859-1 */
#if !defined(CAS_FOR_ORACLE) && !defined(CAS_FOR_MYSQL)
#define CAS_SCHEMA_DEFAULT_CHARSET (lang_charset ())
#else
#define CAS_SCHEMA_DEFAULT_CHARSET 0
#endif


extern int uts_key_check_local_host (void);

#endif /* _CAS_COMMON_H_ */
