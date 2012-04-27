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

#ifdef __cplusplus
extern "C"
{
#endif

#ident "$Id$"

/************************************************************************
 * IMPORTED SYSTEM HEADER FILES						*
 ************************************************************************/
#include <assert.h>
#include <errno.h>

#if defined(WINDOWS)
#include <sys/timeb.h>
#endif

/************************************************************************
 * OTHER IMPORTED HEADER FILES						*
 ************************************************************************/
#include "system.h"
#include "cas_cci.h"

/************************************************************************
 * PUBLIC DEFINITIONS							*
 ************************************************************************/

#if defined(WINDOWS)
#define __func__		__FUNCTION__
#define PATH_MAX		256
#define mkdir(dir, mode)        _mkdir(dir)
#define localtime_r(time, tm)   localtime_s((tm), (const time_t *)(time))
#endif

#define API_SLOG(con) \
  do { \
    if ((con)->log_trace_api) \
      cci_log_write ((con)->logger, "[%04d][API][S][%s]", (con)->id, __func__); \
  } while (false)

#define API_ELOG(con, err) \
  do { \
    if ((con)->log_trace_api) \
      cci_log_write ((con)->logger, "[%04d][API][E][%s] ERROR[%d]", (con)->id, __func__, (err)); \
  } while (false)

#define strlen(s1)  ((int) strlen(s1))
#define CAST_STRLEN (int)

#if defined(WINDOWS)
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

#if defined(WINDOWS)
#define T_MUTEX	HANDLE
#define T_THREAD uintptr_t
#define THREAD_FUNC	void
#else
#define T_MUTEX	pthread_mutex_t
#define T_THREAD pthread_t
#define THREAD_FUNC	void*
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
	  pthread_attr_destroy(&thread_attr);			\
	} while (0)
#endif

#ifdef AIX
#define MALLOC(SIZE)            cci_malloc(((SIZE) == 0) ? 1 : SIZE)
#else
#define MALLOC(SIZE)            cci_malloc(SIZE)
#endif
#define CALLOC(NUM, SIZE)       cci_calloc(NUM, SIZE)
#define REALLOC(PTR, SIZE)      \
        ((PTR == NULL) ? cci_malloc(SIZE) : cci_realloc(PTR, SIZE))
#define FREE(PTR)               cci_free(PTR)
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

#if defined(WINDOWS)
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

#define ALLOC_N_COPY(PTR, STR, SIZE, TYPE)	\
	do {					\
	  if ((SIZE) == 0)	                \
	    PTR = NULL;				\
	  else {				\
	    PTR = (TYPE) MALLOC((SIZE) + 1);	\
	    if (PTR) {				\
	      strncpy(PTR, STR, SIZE);		\
	      PTR[SIZE] = '\0';			\
	    }					\
	  }					\
	} while (0)


#if defined(WINDOWS)
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

#define SET_START_TIME_FOR_QUERY(CON_HANDLE, REQ_HANDLE)            \
  do {                                                              \
    if (CON_HANDLE) {                                               \
      int time_to_check = 0;                                        \
      if (REQ_HANDLE) {                                             \
        time_to_check = ((T_REQ_HANDLE *)(REQ_HANDLE))->query_timeout;\
      }                                                             \
      else {                                                        \
        time_to_check = (CON_HANDLE)->query_timeout;                \
      }                                                             \
      if (time_to_check > 0) {                                      \
        cci_gettimeofday(&((CON_HANDLE)->start_time), NULL);        \
        (CON_HANDLE)->current_timeout = (time_to_check);            \
      }                                                             \
    }                                                               \
  } while (0)

#define SET_START_TIME_FOR_LOGIN(CON_HANDLE)                        \
  do {                                                              \
    if (CON_HANDLE) {                                               \
      if ((CON_HANDLE)->login_timeout > 0) {                        \
        cci_gettimeofday(&((CON_HANDLE)->start_time), NULL);        \
        (CON_HANDLE)->current_timeout = (CON_HANDLE)->login_timeout;\
      }                                                             \
    }                                                               \
  } while (0)

#define TIMEOUT_IS_SET(CON_HANDLE) \
  ((CON_HANDLE) && ((CON_HANDLE)->current_timeout > 0) && \
   ((CON_HANDLE)->start_time.tv_sec != 0 || (CON_HANDLE)->start_time.tv_usec != 0))

#define RESET_START_TIME(CON_HANDLE) \
  do {\
    if (CON_HANDLE) {\
      (CON_HANDLE)->start_time.tv_sec = 0;\
      (CON_HANDLE)->start_time.tv_usec = 0;\
      (CON_HANDLE)->current_timeout = 0; \
    }\
  } while (0)

#if defined(WINDOWS)
#define IS_INVALID_SOCKET(socket) ((socket) == INVALID_SOCKET)
typedef int socklen_t;
#else
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define IS_INVALID_SOCKET(socket) ((socket) < 0)
#endif

#if defined(WINDOWS)
#define snprintf _snprintf
#define strcasecmp(str1, str2) _stricmp(str1, str2)
#define strncasecmp(str1, str2, size) _strnicmp(str1, str2, size)
#define strtok_r strtok_s
#endif

/************************************************************************
 * PUBLIC TYPE DEFINITIONS						*
 ************************************************************************/

typedef struct
{
  char *key;
  char *value;
} T_CCI_PROPERTIES_PAIR;

struct PROPERTIES_T
{
  int capacity;
  int size;

  T_CCI_PROPERTIES_PAIR *pair;
};

struct DATASOURCE_T
{
  int is_init;

  void *mutex;
  void *cond;

  char *user;
  char *pass;
  char *url;

  int pool_size;
  int max_wait;
  bool pool_prepared_statement;
  int default_autocommit;
  T_CCI_TRAN_ISOLATION default_isolation;
  int default_lock_timeout;

  int num_idle;
  int *con_handles;		/* realloc by pool_size */
};

#if defined(WINDOWS)
typedef struct
{
  CRITICAL_SECTION cs;
  CRITICAL_SECTION *csp;
} cci_mutex_t;

typedef HANDLE cci_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER       {{ NULL, 0, 0, NULL, NULL, 0 }, NULL}

typedef union
{
  CONDITION_VARIABLE native_cond;

  struct
  {
    unsigned int waiting;
    CRITICAL_SECTION lock_waiting;
    enum
    {
      COND_SIGNAL = 0,
      COND_BROADCAST = 1,
      MAX_EVENTS = 2
    } EVENTS;
    HANDLE events[MAX_EVENTS];
    HANDLE broadcast_block_event;
  };
} cci_cond_t;

typedef HANDLE cci_condattr_t;

#define ETIMEDOUT WAIT_TIMEOUT
#define PTHREAD_COND_INITIALIZER        { NULL }

struct timespec
{
  int tv_sec;
  int tv_nsec;
};
#else
typedef pthread_mutex_t cci_mutex_t;
typedef pthread_mutexattr_t cci_mutexattr_t;
typedef pthread_cond_t cci_cond_t;
typedef pthread_condattr_t cci_condattr_t;
#endif

typedef unsigned int (*HASH_FUNC) (void *key, unsigned int ht_size);
typedef int (*CMP_FUNC) (void *key1, void *key2);
typedef int (*REM_FUNC) (void *key, void *data, void *args);
typedef int (*PRINT_FUNC) (FILE * fp, void *key, void *data, void *args);

/* Hash Table Entry - linked list */
typedef struct hentry HENTRY;
typedef struct hentry *HENTRY_PTR;
struct hentry
{
  HENTRY_PTR act_next;		/* Next active entry on hash table */
  HENTRY_PTR act_prev;		/* Previous active entry on hash table */
  HENTRY_PTR next;		/* Next hash table entry for colisions */
  void *key;			/* Key associated with entry */
  void *data;			/* Data associated with key entry */
};

/* Memory Hash Table */
typedef struct mht_table MHT_TABLE;
struct mht_table
{
  HASH_FUNC hash_func;
  CMP_FUNC cmp_func;
  const char *name;
  HENTRY_PTR *table;		/* The hash table (entries) */
  HENTRY_PTR act_head;		/* Head of active double link list
				 * entries. Used to perform quick
				 * mappings of hash table.
				 */
  HENTRY_PTR act_tail;		/* Tail of active double link list
				 * entries. Used to perform quick
				 * mappings of hash table.
				 */
  HENTRY_PTR prealloc_entries;	/* Free entries allocated for
				 * locality reasons
				 */
  unsigned int size;		/* Better if prime number */
  unsigned int rehash_at;	/* Rehash at this num of entries */
  unsigned int nentries;	/* Actual number of entries */
  unsigned int nprealloc_entries;	/* Number of preallocated entries
					 * for future insertions
					 */
  unsigned int ncollisions;	/* Number of collisions in HT */
};

/************************************************************************
 * PUBLIC FUNCTION PROTOTYPES						*
 ************************************************************************/
#if defined (WINDOWS)
extern cci_mutex_t cci_Internal_mutex_for_mutex_initialize;
extern int cci_mutex_init (cci_mutex_t * mutex, cci_mutexattr_t * attr);
extern int cci_mutex_destroy (cci_mutex_t * mutex);

extern void port_cci_mutex_init_and_lock (cci_mutex_t * mutex);

__inline int
cci_mutex_lock (cci_mutex_t * mutex)
{
  if (mutex->csp == &mutex->cs)
    {
      EnterCriticalSection (mutex->csp);
    }
  else
    {
      port_cci_mutex_init_and_lock (mutex);
    }

  return 0;
}

__inline int
cci_mutex_unlock (cci_mutex_t * mutex)
{
  if (mutex->csp->LockCount == -1)
    {
      /* this means unlock mutex which isn't locked */
      assert (0);
      return 0;
    }

  LeaveCriticalSection (mutex->csp);
  return 0;
}

extern int cci_cond_init (cci_cond_t * cond, const cci_condattr_t * attr);
extern int cci_cond_wait (cci_cond_t * cond, cci_mutex_t * mutex);
extern int cci_cond_timedwait (cci_cond_t * cond, cci_mutex_t * mutex,
			       struct timespec *ts);
extern int cci_cond_destroy (cci_cond_t * cond);
extern int cci_cond_signal (cci_cond_t * cond);
extern int cci_cond_broadcast (cci_cond_t * cond);
#else
#define cci_mutex_init pthread_mutex_init
#define cci_mutex_destroy pthread_mutex_destroy
#define cci_mutex_lock pthread_mutex_lock
#define cci_mutex_unlock pthread_mutex_unlock
#define cci_cond_init pthread_cond_init
#define cci_cond_destroy pthread_cond_destroy
#define cci_cond_signal pthread_cond_signal
#define cci_cond_timedwait pthread_cond_timedwait
#define cci_cond_wait pthread_cond_wait
#define cci_gettimeofday gettimeofday
#endif

extern int get_elapsed_time (struct timeval *start_time);
#if defined(WINDOWS)
extern int cci_gettimeofday (struct timeval *tp, void *tzp);
#endif

extern unsigned int mht_5strhash (void *key, unsigned int ht_size);
extern int mht_strcasecmpeq (void *key1, void *key2);

extern MHT_TABLE *mht_create (char *name, int est_size, HASH_FUNC hash_func,
			      CMP_FUNC cmp_func);
extern void mht_destroy (MHT_TABLE * ht, bool free_key, bool free_data);
extern void *mht_get (MHT_TABLE * ht, void *key);
extern void *mht_put (MHT_TABLE * ht, void *key, void *data);
extern void *mht_put_data (MHT_TABLE * ht, void *key, void *data);

/************************************************************************
 * PUBLIC VARIABLES							*
 ************************************************************************/
extern CCI_MALLOC_FUNCTION cci_malloc;
extern CCI_FREE_FUNCTION cci_free;
extern CCI_REALLOC_FUNCTION cci_realloc;
extern CCI_CALLOC_FUNCTION cci_calloc;

#ifdef __cplusplus
}
#endif

#endif /* _CCI_COMMON_H_ */
