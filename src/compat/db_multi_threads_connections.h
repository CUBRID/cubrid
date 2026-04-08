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
 * db_multi_threads_connections.h - Definitions and function prototypes for the CUBRID
 *         Application Program Interface (API).
 */

#ifndef _DB_MULTI_THREADS_CONNECTIONS_H_
#define _DB_MULTI_THREADS_CONNECTIONS_H_

#ident "$Id$"

#if !defined (SERVER_MODE)
#if defined(__cplusplus) && __cplusplus >= 201103L
/* C++11 after */
#define THREAD_LOCAL thread_local

#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__cplusplus)
/* C11 standard */
#define THREAD_LOCAL _Thread_local

#elif defined(__GNUC__)
/* GCC (C/C++) extension */
#define THREAD_LOCAL __thread

#elif defined(_MSC_VER)
/* MSVC (Windows) */
#define THREAD_LOCAL __declspec(thread)

#else
/* not supported environment */
#error "No thread-local storage specifier available on this compiler."
#endif
#endif /* !defined (SERVER_MODE) */

#if !defined(SERVER_MODE) && defined(MULTI_CONN_TO_A_SERVER)
#define CUB_THREAD_LOCAL THREAD_LOCAL
#else
#define CUB_THREAD_LOCAL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(NDEBUG)
#define DBG_PRINT(format, ...)
#else
#define DBG_PRINT(format, ...) fprintf(stderr, format, ##__VA_ARGS__)
#endif


#if !defined(SERVER_MODE)
  extern pthread_t css_get_thread_id ();

#if !defined(NDEBUG) || defined(MULTI_CONN_TO_A_SERVER)
  extern pthread_t gv_main_tid;
#define  CHECK_MAIN_THREAD()   assert (pthread_equal (gv_main_tid, css_get_thread_id ()))
#else
#define  CHECK_MAIN_THREAD()
#endif

// *INDENT-OFF* 
#if defined(MULTI_CONN_TO_A_SERVER)
#  define CS_Lock(mutex)   pthread_mutex_lock(mutex)
#  define CS_UnLock(mutex) pthread_mutex_unlock(mutex)
#else
#  define CS_Lock(mutex)
#  define CS_UnLock(mutex)
#endif
// *INDENT-ON*

#endif				// #if !defined(SERVER_MODE)

#ifdef __cplusplus
}
#endif

#endif				/* _DB_MULTI_THREADS_CONNECTIONS_H_ */
