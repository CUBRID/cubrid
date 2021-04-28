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
 * cci_common.h -
 */

#ifndef	_CCI_COMMON_H_
#define	_CCI_COMMON_H_

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
#include "porting.h"

/************************************************************************
 * PUBLIC DEFINITIONS							*
 ************************************************************************/

#if defined(WINDOWS)
#define __func__		__FUNCTION__
#define gettid()                GetCurrentThreadId()
#elif defined(AIX)
#define gettid()                pthread_self()
#else
#define gettid()                syscall(__NR_gettid)
#endif

#define API_SLOG(con) \
  do { \
    if ((con)->log_trace_api) \
      CCI_LOGF_DEBUG ((con)->logger, "[%04d][API][S][%s]", (con)->id, __func__); \
  } while (false)

#define API_ELOG(con, err) \
  do { \
    if ((con)->log_trace_api) \
      CCI_LOGF_DEBUG ((con)->logger, "[%04d][API][E][%s] ERROR[%d]", (con)->id, __func__, (err)); \
  } while (false)

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
	    (PTR) = 0;	        \
	  }			\
	} while (0)

#define ALLOC_COPY(PTR, STR)			                 \
	do {					                 \
	  if ((STR) == NULL)			                 \
	    (PTR) = NULL;			                 \
	  else {				                 \
	    (PTR) = (char *) MALLOC(strlen((char*) (STR)) + 1);	 \
	    if (PTR) {				                 \
	      strcpy((char*) (PTR), (const char*) (STR));	 \
	    }					                 \
	  }					                 \
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

#define SET_START_TIME_FOR_LOGIN(CON_HANDLE)                        \
  do {                                                              \
    if (CON_HANDLE) {                                               \
      gettimeofday(&((CON_HANDLE)->start_time), NULL);              \
      if ((CON_HANDLE)->login_timeout > 0) {                        \
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
typedef unsigned long in_addr_t;
#endif


#define MAX_NUMERIC_PRECISION	38

enum t_fn_status_type
{
  FN_STATUS_NONE = -2,
  FN_STATUS_IDLE = -1,
  FN_STATUS_CONN = 0,
  FN_STATUS_BUSY = 1,
  FN_STATUS_DONE = 2
};

#define SRV_STATUS_REQUEST_INFO_SIZE           10
#define SRV_STATUS_REQUEST_MSG_ID_SIZE         2
#define SRV_STATUS_REQUEST_CAS_ID_POS          2
#define SRV_STATUS_REQUEST_SESSION_ID_POS      6

/************************************************************************
 * PUBLIC TYPE DEFINITIONS						*
 ************************************************************************/
#ifdef __cplusplus
extern "C"
{
#endif
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

    int max_pool_size;
    int pool_size;
    int max_wait;
    bool pool_prepared_statement;
    int max_open_prepared_statement;
    int default_autocommit;
    T_CCI_TRAN_ISOLATION default_isolation;
    int default_lock_timeout;
    int login_timeout;

    int num_idle;
    int num_waiter;
    int *con_handles;		/* realloc by pool_size */
  };

  typedef unsigned int (*HASH_FUNC) (const void *key, unsigned int ht_size);
  typedef int (*CMP_FUNC) (const void *key1, const void *key2);
  typedef int (*REM_FUNC) (void *key, void *data, void *args);
  typedef int (*PRINT_FUNC) (FILE * fp, void *key, void *data, void *args);

/* CCI Hash Table Entry - linked list */
  typedef struct cci_hentry CCI_HENTRY;
  typedef struct cci_hentry *CCI_HENTRY_PTR;
  struct cci_hentry
  {
    CCI_HENTRY_PTR act_next;	/* Next active entry on hash table */
    CCI_HENTRY_PTR act_prev;	/* Previous active entry on hash table */
    CCI_HENTRY_PTR next;	/* Next hash table entry for colisions */
    void *key;			/* Key associated with entry */
    void *data;			/* Data associated with key entry */
  };

/* CCI Memory Hash Table */
  typedef struct cci_mht_table CCI_MHT_TABLE;
  struct cci_mht_table
  {
    HASH_FUNC hash_func;
    CMP_FUNC cmp_func;
    const char *name;
    CCI_HENTRY_PTR *table;	/* The hash table (entries) */
    CCI_HENTRY_PTR act_head;	/* Head of active double link list entries. Used to perform quick mappings of hash
				 * table. */
    CCI_HENTRY_PTR act_tail;	/* Tail of active double link list entries. Used to perform quick mappings of hash
				 * table. */
    CCI_HENTRY_PTR prealloc_entries;	/* Free entries allocated for locality reasons */
    unsigned int size;		/* Better if prime number */
    unsigned int rehash_at;	/* Rehash at this num of entries */
    unsigned int nentries;	/* Actual number of entries */
    unsigned int nprealloc_entries;	/* Number of preallocated entries for future insertions */
    unsigned int ncollisions;	/* Number of collisions in HT */
  };

/************************************************************************
 * PUBLIC FUNCTION PROTOTYPES						*
 ************************************************************************/
  extern int get_elapsed_time (struct timeval *start_time);

  extern unsigned int cci_mht_5strhash (const void *key, unsigned int ht_size);
  extern int cci_mht_strcasecmpeq (const void *key1, const void *key2);

  extern CCI_MHT_TABLE *cci_mht_create (char *name, int est_size, HASH_FUNC hash_func, CMP_FUNC cmp_func);
  extern void cci_mht_destroy (CCI_MHT_TABLE * ht, bool free_key, bool free_data);
  extern void *cci_mht_rem (CCI_MHT_TABLE * ht, const void *key, bool free_key, bool free_data);
  extern void *cci_mht_get (CCI_MHT_TABLE * ht, void *key);
#if defined(ENABLE_UNUSED_FUNCTION)
  extern void *cci_mht_put (CCI_MHT_TABLE * ht, void *key, void *data);
#endif
  extern void *cci_mht_put_data (CCI_MHT_TABLE * ht, void *key, void *data);

  extern int hostname2uchar (char *host, unsigned char *ip_addr);

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

#endif				/* _CCI_COMMON_H_ */
