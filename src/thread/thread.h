/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * thread.h - threads wrapper for pthreads and WIN32 threads.
 */

#ifndef _THREAD_H_
#define _THREAD_H_

#ident "$Id$"


#if !defined(WINDOWS)

/* Data types */
#define THREAD_T	pthread_t
#define MUTEX_T		pthread_mutex_t
#define COND_T		pthread_cond_t
#define NULL_THREAD_T	((pthread_t) 0)
#define THREAD_KEY_T	pthread_key_t
#define MUTEXATTR_T	pthread_mutexattr_t
#define THREAD_ATTR_T	pthread_attr_t

/* Create/Destroy thread */
#define THREAD_CREATE(dummy, attr_ptr, start_addr, arg_ptr, id_ptr) \
     	pthread_create(id_ptr, attr_ptr, start_addr, arg_ptr)

#define THREAD_EXIT(ret_val)	pthread_exit((void *)(ret_val))
#define THREAD_ID()		pthread_self()

#define THREAD_JOIN(id_ptr, ret_val) \
	ret_val = pthread_join(id_ptr, NULL)

#ifdef LOG_DEBUG
#define THREAD_EQUAL(lhs, rhs) pthread_equal(lhs, rhs)
#endif /* LOG_DEBUG */

#define THREAD_ATTR_INIT(attr) pthread_attr_init(&(attr))

#define THREAD_ATTR_SETDETACHSTATE(attr, detachstate) \
   	pthread_attr_setdetachstate(&(attr), detachstate)

#define THREAD_ATTR_SETSCOPE(attr, scope) \
 	pthread_attr_setscope(&(attr), scope)

#define THREAD_ATTR_GETSTACKSIZE(attr, size) \
 	pthread_attr_getstacksize(&(attr), &(size))

#define THREAD_ATTR_SETSTACKSIZE(attr, size) \
 	pthread_attr_setstacksize(&(attr), size)

#define THREAD_ATTR_DESTROY(attr) pthread_attr_destroy(&(attr))

/* Mutex */
#define MUTEX_INITIALIZER	PTHREAD_MUTEX_INITIALIZER
#define TRYLOCK_SUCCESS		0
#define TRYLOCK_EBUSY		EBUSY

#define MUTEXATTR_INIT(mattr) \
	( pthread_mutexattr_init(&(mattr)) == 0 ? \
		NO_ERROR : ER_CSS_PTHREAD_MUTEX_UNLOCK)

#define MUTEXATTR_SETTYPE(mattr, type) \
  	pthread_mutexattr_settype(&(mattr), type)

#define MUTEXATTR_DESTROY(mattr) pthread_mutexattr_destroy(&(mattr))

#if 1				/* TODO: move porting.h */
#define MUTEX_INIT(mutex) \
    ( pthread_mutex_init(&(mutex), NULL) == 0 ? \
	NO_ERROR : ER_CSS_PTHREAD_MUTEX_INIT )
#endif

#define MUTEX_INIT_WITH_ATT(mutex, attr) \
	pthread_mutex_init(&(mutex), &(attr))

#if 1				/* TODO: move porting.h */
#define MUTEX_DESTROY(mutex)	\
    ( pthread_mutex_destroy(&(mutex)) == 0 ? \
	NO_ERROR : ER_CSS_PTHREAD_MUTEX_DESTROY )

#define MUTEX_LOCK(r, mutex) \
    ( (r = pthread_mutex_lock(&(mutex)) ) == 0 ? \
	NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK )

#define MUTEX_UNLOCK(mutex) \
    ( pthread_mutex_unlock(&(mutex)) == 0 ? \
	NO_ERROR : ER_CSS_PTHREAD_MUTEX_UNLOCK )
#endif

#define MUTEX_TRYLOCK(mutex)	pthread_mutex_trylock(&(mutex))

/* Condition Signal */
#define COND_INITIALIZER		PTHREAD_COND_INITIALIZER
#define TIMEDWAIT_GET_LK		0
#define TIMEDWAIT_TIMEOUT		ETIMEDOUT
#define COND_INIT(condvar)		pthread_cond_init(&(condvar), NULL)

#define COND_INIT_WITH_ATTR(condvar, attr) \
	pthread_cond_init(&(condvar), &(attr))

#define CONDATTR_INIT(attr)		pthread_cond_attr_init(&(attr))
#define CONDATTR_DESTROY(attr)	        pthread_cond_attr_destroy(&(attr))
#define COND_DESTROY(condvar)		pthread_cond_destroy(&(condvar))

/* Caution:
 *   There are some differences between pthreads and win32 threads
 *   over these macros of condition signal. Under the win32 threads,
 *     1. COND_WAIT() returns without acquiring lock.
 *     2. COND_BROADCAST() does not release all waiting threads, but keep
 *        signaling until only a thread is released. It continues signaling
 *        even if there is no thread to be waken up when this macro is called.
 *   You should take these into account for portability.
 */
#define COND_WAIT(c, m)		pthread_cond_wait(&(c), &(m))
#define COND_TIMEDWAIT(c, m, t)	pthread_cond_timedwait(&(c), &(m), &(t))
#define COND_SIGNAL(c)		pthread_cond_signal(&(c))
#define COND_BROADCAST(c)		pthread_cond_broadcast(&(c))

/* Thread Specific Data */
#define TLS_KEY_ALLOC(key, destructor)  pthread_key_create(&key, destructor)
#define TLS_KEY_FREE(key)		pthread_key_delete(key)
#define TLS_SET_SPECIFIC(key, valueptr) pthread_setspecific(key, valueptr)
#define TLS_GET_SPECIFIC(key)		pthread_getspecific(key)

#else /* WINDOWS */

/* Data Types */
#define THREAD_T	unsigned int
#define MUTEX_T	        HANDLE
#define COND_T	        HANDLE
#define NULL_THREAD_T	((int)0)
#define THREAD_KEY_T	int

/* Create/Destroy */
#ifdef WIN32_GENERAL_DEBUG
#define THREAD_CREATE(handle, dummy, start_addr, arg_ptr, id_ptr) \
	((handle = _beginthreadex(NULL, 1024*256, start_addr, \
    		(void *)arg_ptr, 0, id_ptr))  <= 0 ? -1 : 0)
#else /* not WIN32_GENERAL_DEBUG */
#define THREAD_CREATE(handle, dummy, start_addr, arg_ptr, id_ptr) \
	((handle = _beginthreadex(NULL, 0, start_addr, \
    		(void *)arg_ptr, 0, id_ptr))  <= 0 ? -1 : 0)
#endif /* not WIN32_GENERAL_DEBUG */

#define THREAD_EXIT(ret_val)	_endthreadex(ret_val)
#define THREAD_ID()		GetCurrentThreadId()

#define THREAD_JOIN(handle, ret_val) \
	ret_val = WaitForSingleObject(handle, INFINITE)

#ifdef LOG_DEBUG
#define THREAD_EQUAL(lhs, rhs) (lhs == rhs ? 1 : 0)
#endif /* LOG_DEBUG */

#define THREAD_ATTR_INIT(dummy1)			0
#define THREAD_ATTR_SETDETACHSTATE(dummy1, dummy2)	0
#define THREAD_ATTR_SETSCOPE(dummy1, dummy2)		0
#define THREAD_ATTR_GETSTACKSIZE(dummy1, dummy2)	0
#define THREAD_ATTR_SETSTACKSIZE(dummy1, dummy2)	0
#define THREAD_ATTR_DESTROY(dummy1)			0

/* Mutex */
#define MUTEX_INITIALIZER	NULL

#define TRYLOCK_SUCCESS	        WAIT_OBJECT_0
#define TRYLOCK_SUCCESS2	WAIT_ABANDONED
#define TRYLOCK_EBUSY		WAIT_TIMEOUT
#define TRYLOCK_FAIL		WAIT_FAIL

#define MUTEXATTR_INIT(dummy1)		        0
#define MUTEXATTR_SETTYPE(dummy1, dummy2)	0
#define MUTEXATTR_DESTROY(dummy1)		0

#if 1				/* TODO: move porting.h */
#ifdef WIN32_GENERAL_DEBUG
#define MUTEX_INIT(handle) \
    ( (handle = CreateMutex(NULL, FALSE, NULL)) != NULL ? \
		NO_ERROR : \
	( printf("(%s:%d) [[ Mutex_Create Error(%d) ]]\n", \
		 __FILE__, __LINE__, GetLastError()), \
	  fflush(stdout), Sleep(1000), ER_CSS_PTHREAD_MUTEX_INIT) \
    )

#define MUTEX_DESTROY(handle)	\
    ( (handle = (void *)(!CloseHandle(handle))) == 0 ? \
      NO_ERROR : \
     ( (printf("(%s:%d) [[[  Mutex_destroy Error(%d) ]]]\n", \
	       __FILE__, __LINE__, GetLastError()), \
        fflush(stdout), Sleep(1000), ER_CSS_PTHREAD_MUTEX_DESTROY) ) \
    )

#define MUTEX_LOCK(r, mutex) \
    do { \
      if(mutex) { \
	  r = (WaitForSingleObject(mutex, INFINITE) == WAIT_OBJECT_0 ? \
	    NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK); \
      } else { \
	  WaitForSingleObject(css_Internal_mutex_for_mutex_initialize, INFINITE); \
	  if(mutex) { \
	    r = (WaitForSingleObject(mutex, INFINITE) == WAIT_OBJECT_0 ? \
	      NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK); \
	  } else { \
	    r = ((mutex = CreateMutex(NULL, TRUE, NULL)) != NULL ? \
	      NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK); \
	  } \
	  ReleaseMutex(css_Internal_mutex_for_mutex_initialize); \
      } \
      if(r != NO_ERROR) { \
	printf("(%s:%d) [[[[   Mutex lock Error(%d) ]]]]\n", \
	    __FILE__, __LINE__, GetLastError()); \
        fflush(stdout); Sleep(1000) ; \
      } \
    } while(0)

#define MUTEX_UNLOCK(mutex) \
    ( ReleaseMutex(mutex) != 0 ?  NO_ERROR : \
	(printf("(%s:%d) [[[  Mutex unlock Error(%d) ]]]\n", \
		__FILE__, __LINE__, GetLastError()), \
        fflush(stdout), Sleep(1000), ER_CSS_PTHREAD_MUTEX_UNLOCK )\
    )

#else /* not WIN32_GENERAL_DEBUG */

#define MUTEX_INIT(handle) \
    ( (handle = CreateMutex(NULL, FALSE, NULL)) != NULL ? \
		NO_ERROR : ER_CSS_PTHREAD_MUTEX_INIT )

#define MUTEX_DESTROY(handle)	\
    ( (handle = (void *)(!CloseHandle(handle))) == 0 ? \
      NO_ERROR : ER_CSS_PTHREAD_MUTEX_DESTROY )

#define MUTEX_LOCK(r, mutex) \
    do { \
      if(mutex) { \
	  r = (WaitForSingleObject(mutex, INFINITE) == WAIT_OBJECT_0 ? \
	    NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK); \
      } else { \
	  WaitForSingleObject(css_Internal_mutex_for_mutex_initialize, INFINITE); \
	  if(mutex) { \
	    r = (WaitForSingleObject(mutex, INFINITE) == WAIT_OBJECT_0 ? \
	      NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK); \
	  } else { \
	    r = ((mutex = CreateMutex(NULL, TRUE, NULL)) != NULL ? \
	      NO_ERROR : ER_CSS_PTHREAD_MUTEX_LOCK); \
	  } \
	  ReleaseMutex(css_Internal_mutex_for_mutex_initialize); \
      } \
    } while(0)

#define MUTEX_UNLOCK(mutex) \
    ( ReleaseMutex(mutex) != 0 ?  NO_ERROR : ER_CSS_PTHREAD_MUTEX_UNLOCK )
#endif /* not WIN32_GENERAL_DEBUG */

#endif

#define MUTEX_INIT_WITH_ATT(handle, attr)  MUTEX_INIT(handle)
#define MUTEX_TRYLOCK(mutex)               WaitForSingleObject(mutex, 0)

/* Condition Signal */
#define COND_INITIALIZER	0
#define TIMEDWAIT_GET_LK	WAIT_OBJECT_0
#define TIMEDWAIT_TIMEOUT	WAIT_TIMEOUT
#define ETIMEDOUT		WAIT_TIMEOUT

/* Creates an Event of auto-reset mode */
#define COND_INIT(cond_var) \
    ((cond_var = CreateEvent(NULL, FALSE, FALSE, NULL)) != NULL ? 0 : -1)

#define COND_INIT_WITH_ATTR(cond_var, attr) COND_INIT(cond_var)
#define CONDATTR_INIT(dummy1)

#define COND_DESTROY(cond_var) \
	( (cond_var = (void *)CloseHandle(cond_var)) != 0 ? 0 : -1)

#ifdef WIN32_GENERAL_DEBUG
#define COND_WAIT(cond_var, mutex) \
    (SignalObjectAndWait(mutex, cond_var, INFINITE, FALSE) == WAIT_OBJECT_0 ? \
    0 : \
	(printf("(%s:%d) [[ CondWait Error(%d) ]]\n", \
		__FILE__, __LINE__, GetLastError()), \
        fflush(stdout), Sleep(1000), -1 ) \
    )

#define COND_TIMEDWAIT(cond_var, mutex, timeout) \
    SignalObjectAndWait(mutex, cond_var, timeout, FALSE)

#define COND_SIGNAL(cond_var)		(PulseEvent(cond_var) !=0 ? 0 : \
	(printf("(%s:%d) [[ CondSignal Error(%d) ]]\n", \
		__FILE__, __LINE__, GetLastError()), \
        fflush(stdout), Sleep(1000), -1 ) \
     )

#define COND_BROADCAST(cond_var)	(SetEvent(cond_var) !=0 ? 0 : -1 )
#else /* not WIN32_GENERAL_DEBUG */

#define COND_WAIT(cond_var, mutex) \
    (SignalObjectAndWait(mutex, cond_var, INFINITE, FALSE) == WAIT_OBJECT_0 ? \
    0 : -1)

#define COND_TIMEDWAIT(cond_var, mutex, timeout) \
    SignalObjectAndWait(mutex, cond_var, timeout, FALSE)

/* For an auto-reset event object, PulseEvent() returns after releasing a
 * waiting thread. If no threads are waiting, nothing happens - it simply
 * returns.
 */
#define COND_SIGNAL(cond_var)		(PulseEvent(cond_var) !=0 ? 0 : -1)

/* SetEvent() just wakes up one thread. It's actually not the broadcast. */
#define COND_BROADCAST(cond_var)	(SetEvent(cond_var) !=0 ? 0 : -1 )
#endif /* not WIN32_GENERAL_DEBUG */

/* Thread Specific Data */
#define TLS_KEY_ALLOC(key, dummy_destructor) \
    ((key = TlsAlloc()) != 0xFFFFFFFF ? 0 : -1)

#define TLS_KEY_FREE(key) (TlsFree(key) != 0 ? 0 : -1)

#define TLS_SET_SPECIFIC(key, valueptr) \
    (TlsSetValue(key, valueptr) != 0 ? 0 : -1)

#define TLS_GET_SPECIFIC(key) TlsGetValue(key)

extern HANDLE css_Internal_mutex_for_mutex_initialize;

#endif /* WINDOWS */

#endif /* _THREAD_H_ */
