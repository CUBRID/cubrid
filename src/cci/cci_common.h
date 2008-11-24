/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */


/*
 * cci_common.h - 
 */

#ifndef	_CCI_COMMON_H_
#define	_CCI_COMMON_H_

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/

/************************************************************************
 * PUBLIC DEFINITIONS							*
 ************************************************************************/

#ifdef WIN32
#define MUTEX_INIT(MUTEX)				\
	do {						\
	  MUTEX = CreateMutex(NULL, FALSE, NULL);	\
	} while (0)
#define MUTEX_LOCK(MUTEX)		WaitForSingleObject(MUTEX, INFINITE)
#define MUTEX_UNLOCK(MUTEX)		ReleaseMutex(MUTEX)
#define THREAD_RETURN(VALUE)
#else
#define MUTEX_INIT(MUTEX)		pthread_mutex_init(&(MUTEX), NULL)
#define MUTEX_LOCK(MUTEX)		pthread_mutex_lock(&(MUTEX))
#define MUTEX_UNLOCK(MUTEX)		pthread_mutex_unlock(&(MUTEX))
#define THREAD_RETURN(VALUE)		return (VALUE)
#endif

#ifdef WIN32
#define T_MUTEX	HANDLE
#define T_THREAD int
#define THREAD_FUNC	void
#else
#define T_MUTEX	pthread_mutex_t
#define T_THREAD pthread_t
#define THREAD_FUNC	void*
#endif

#ifdef WIN32
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
	  pthread_attr_destroy(&thread_attr);			\
	} while (0)
#endif

#ifdef AIX
#define MALLOC(SIZE)            malloc(((SIZE) == 0) ? 1 : SIZE)
#else
#define MALLOC(SIZE)            malloc(SIZE)
#endif

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
	    PTR = (char *) MALLOC(strlen((char*) STR) + 1);	\
	    if (PTR) {				\
	      strcpy((char*) PTR, (const char*) STR);			\
	    }					\
	  }					\
	} while (0)

#ifdef WIN32
#define CLOSE_SOCKET(X)			\
	do {				\
	  struct linger linger_buf;	\
	  linger_buf.l_onoff = 1;	\
	  linger_buf.l_linger = 0;	\
	  setsockopt(X, SOL_SOCKET, SO_LINGER, (char *) &linger_buf, sizeof(linger_buf));	\
	  closesocket(X);		\
	} while (0)
#else
#define CLOSE_SOCKET(X)			\
	do {				\
	  struct linger linger_buf;	\
	  linger_buf.l_onoff = 1;	\
	  linger_buf.l_linger = 0;	\
	  setsockopt(X, SOL_SOCKET, SO_LINGER, (char *) &linger_buf, sizeof(linger_buf));	\
	  close(X);			\
	} while (0)
#endif

#define ALLOC_N_COPY(PTR, STR, SIZE, TYPE)		\
	do {					\
	  if ((STR) == NULL || (SIZE) == 0)	\
	    PTR = NULL;				\
	  else {				\
	    PTR = (TYPE) MALLOC((SIZE) + 1);		\
	    if (PTR) {				\
	      strncpy(PTR, STR, SIZE);		\
	      PTR[SIZE] = '\0';			\
	    }					\
	  }					\
	} while (0)


#ifdef WIN32
#define SLEEP_MILISEC(SEC, MSEC)        Sleep((SEC) * 1000 + (MSEC))
#else
#define SLEEP_MILISEC(sec, msec)                        \
        do {                                            \
          struct timeval sleep_time_val;                \
          sleep_time_val.tv_sec = sec;                  \
          sleep_time_val.tv_usec = (msec) * 1000;       \
          select(0, 0, 0, 0, &sleep_time_val);          \
        } while(0)
#endif

/************************************************************************
 * PUBLIC TYPE DEFINITIONS						*
 ************************************************************************/

/************************************************************************
 * PUBLIC FUNCTION PROTOTYPES						*
 ************************************************************************/

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/

#endif /* _CCI_COMMON_H_ */
