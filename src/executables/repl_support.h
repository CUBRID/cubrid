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
 * repl_support.h : Common definition & Declaration for replication processes
 *
 */

#ifndef _REPL_SUPPORT_H_
#define _REPL_SUPPORT_H_

#ident "$Id$"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>

#include "log_impl.h"
#include "memory_alloc.h"

extern int port_Num;


#define FILE_PATH_LENGTH                 256

/*
 * default log page size, we need this value to read the log header page
 * for the first tim.
 */
#define REPL_DEF_LOG_PAGE_SIZE           4096

#define REPL_LOG_PHYSICAL_HDR_PAGEID    0
#define LOG_HDR_PAGEID                  (-9)

/* Constant for Requests of repl_agent  (repl_agent -> repl_server) */
#define REPL_MSG_GET_AGENT_ID            1
#define REPL_MSG_GET_LOG_HEADER          2
#define REPL_MSG_GET_NEXT_LOG            3
#define REPL_MSG_READ_LOG                4	/* reads from the disk */
#define REPL_MSG_SHUTDOWN                5

/* Constant for Result of req. of repl_agent (repl_server -> repl_agent) */
#define REPL_REQUEST_SUCCESS             1
#define REPL_REQUEST_FAIL                2
#define REPL_REQUEST_NOPAGE              3

/* Communication Buffer size */
#define COMM_REQ_BUF_SIZE                50
#define COMM_RESP_BUF_SIZE               10
#define COMM_DATA_BUF_SIZE               (repl_Pagesize)

/* the result of getting request */
#define REPL_GET_REQ_SUCCESS              1
#define REPL_GET_REQ_CLOSE_CONN           2
#define REPL_GET_REQ_FAIL                 3


#define FILE_CREATE_MODE     O_RDWR | O_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH

/* file id definition for error debug */
#define REPL_FILE_COMM                      1

#define REPL_FILE_SERVER                   11
#define REPL_FILE_SVR_SOCK                 12
#define REPL_FILE_SVR_TP                   13

#define REPL_FILE_AGENT                    21
#define REPL_FILE_AG_SOCK                  22
#define REPL_FILE_AG_TP                    23

#define REPL_COMMON_ERROR                 -10

#define REPL_DEBUG_ERROR_DETAIL           0x00000001
#define REPL_DEBUG_LOG_DUMP               0x00000002
#define REPL_DEBUG_AGENT_STATUE           0x00000004
#define REPL_DEBUG_VALUE_DUMP             0x00000008
#define REPL_DEBUG_VALUE_CHECK            0x00000010


/* Error processing macro.. */

/* flush the error msg, and abort */
#define REPL_ERR_ABORT(text) do {                                       \
      fprintf(err_Log_fp, "%s at \"%s\":%d: %s\n",                      \
              text, __FILE__, __LINE__, strerror(errno));               \
      fflush(err_Log_fp);                                               \
      abort();                                                          \
   } while(0)

/* push the error message, return the error code */
#define REPL_ERR_RETURN(file_id, code) do {                             \
      repl_error_push(file_id, __LINE__, code, NULL);                   \
      return code;                                                      \
  } while(0)

#define REPL_ERR_RETURN_ONE_ARG(file_id, code, arg) do {                \
      repl_error_push(file_id, __LINE__, code, arg);                    \
      return code;                                                      \
  } while(0)

/* push the error message, return NULL */
#define REPL_ERR_RETURN_NULL(file_id, code) do {                        \
      repl_error_push(file_id, __LINE__, code, NULL);                   \
      return NULL;                                                      \
  } while(0)

#define REPL_ERR_RETURN_NULL_ONE_ARG(file_id, code, arg) do {           \
      repl_error_push(file_id, __LINE__, code, arg);                    \
      return NULL;                                                      \
  } while(0)


/* push the error message, set error code */
#define REPL_ERR_LOG(file_id, code) do {                                \
      repl_error_push(file_id, __LINE__, code, NULL);                   \
      error = code;                                                     \
   } while(0)


#define REPL_ERR_LOG_ONE_ARG(file_id, code, arg) do {                   \
      repl_error_push(file_id, __LINE__, code, arg);                    \
      error = code;                                                     \
   } while(0)

#define REPL_CHECK_ERR_LOG_ONE_ARG(file_id, code, arg) do {             \
      if(error != NO_ERROR) {                                           \
      repl_error_push(file_id, __LINE__, code, arg);                    \
      error = code;                                                     \
      }                                                                 \
   } while(0)

/* check the error code, if error occurs,
 * push the error message,
 * return error code
 */
#define REPL_CHECK_ERR_ERROR(file_id, code) do {                        \
      if(error != NO_ERROR) {                                           \
         repl_error_push(file_id, __LINE__, code, NULL);                \
         return code;                                                   \
      }                                                                 \
   } while(0)

#define REPL_CHECK_ERR_ERROR_ONE_ARG(file_id, code, arg) do {           \
      if(error != NO_ERROR) {                                           \
         repl_error_push(file_id, __LINE__, code, arg);                 \
         return code;                                                   \
      }                                                                 \
   } while(0)

/* check the error code, if error occurs,
 * unlock mutex - lock2
 * push the error message,
 * return error code
 */
#define REPL_CHECK_ERR_ERROR2(file_id, code, lock2) do {                \
      if(error != NO_ERROR) {                                           \
         PTHREAD_MUTEX_UNLOCK(lock2);                                   \
         repl_error_push(file_id, __LINE__, code, NULL);                \
         return code;                                                   \
      }                                                                 \
   } while(0)

/* check the pointer - ptr, if it's NULL,
 * set error code,
 * push the error message,
 * return error code
 */
#define REPL_CHECK_ERR_NULL(file_id, code, ptr) do {                    \
      if(ptr == NULL) {                                                 \
         repl_error_push(file_id, __LINE__, code, NULL);                \
         return code;                                                   \
      }                                                                 \
   } while(0)

#define REPL_CHECK_ERR_NULL_ONE_ARG(file_id, code, ptr, arg) do {       \
      if(ptr == NULL) {                                                 \
         repl_error_push(file_id, __LINE__, code, arg);                 \
         error = code;                                                  \
         return code;                                                   \
      }                                                                 \
   } while(0)

/* check the pointer - ptr, if it's NULL,
 * unlock mutex - lock2
 * set error code,
 *  the error message,
 * return error code
 */
#define REPL_CHECK_ERR_NULL2(file_id, code, lock2, ptr) do {            \
      if(ptr == NULL) {                                                 \
         PTHREAD_MUTEX_UNLOCK(lock2);                                   \
         repl_error_push(file_id, __LINE__, code, NULL);                \
         error = code;                                                  \
         return code;                                                   \
      }                                                                 \
   } while(0)

#define REPL_GET_FILE_SIZE(pagesize, npages) \
  (((off_t)(pagesize)) * ((off_t)(npages)))

#define SLEEP_USEC(sec, usec)                           \
        do {                                            \
          struct timeval sleep_time_val;                \
          sleep_time_val.tv_sec = sec;                  \
          sleep_time_val.tv_usec = usec;                \
          select(0, 0, 0, 0, &sleep_time_val);          \
        } while (0)

/* Log Buffer Structure */
typedef struct
{
  PAGEID pageid;		/* Logical page of the log. (Page identifier of
				 * the infinite log)                             */
  PAGEID phy_pageid;		/* Physical pageid for the active log portion    */
  bool in_archive;
  LOG_PAGE logpage;		/* The actual buffered log page                  */
} REPL_LOG_BUFFER;

/* CACHE Log Buffer Structure */
typedef struct
{
  int fix_count;		/* Fix Count */
  int recently_freed;		/* Reference value 0/1 used by the clock
				   algorithm */
  REPL_LOG_BUFFER log_buffer;	/* log buffer */
} REPL_CACHE_LOG_BUFFER;

/* CACHE Log Buffer area Structure */
typedef struct repl_cache_area
{
  REPL_CACHE_LOG_BUFFER *buffer_area;	/* cache log buffer area */
  struct repl_cache_area *next;	/* next area */
} REPL_CACHE_BUFFER_AREA;

/* error stack */
typedef struct repl_err
{
  int line;
  int code;
  char arg[FILE_PATH_LENGTH];
  struct repl_err *next;
} REPL_ERR;

extern bool is_Debug;

extern int repl_io_open (const char *vlabel, int flags, int mode);
extern int repl_io_read (int vdes, void *io_pgptr, PAGEID pageid,
			 int pagesize);
extern int repl_io_write (int vdes, void *op_pgptr, PAGEID pageid,
			  int pagesize);
extern int repl_io_write_copy_log_info (int vdes, void *op_pgptr,
					PAGEID pageid, int pagesize);
extern int repl_connect_to_master (bool serveryn, const char *server_name);
extern int repl_start_daemon (void);
extern void repl_signal_process (SIGNAL_HANDLER_FUNCTION routine);
extern const char *repl_error (int error);
extern void repl_error_flush (FILE * fp, bool serveryn);
extern void repl_error_push (int file_id, int line_num, int code, char *arg);
extern int repl_io_truncate (int vdes, int pagesize, PAGEID pageid);
extern int repl_io_rename (char *active_copy_log, int *active_vol,
			   char *archive_copy_log, int *archive_vol);
extern off_t repl_io_file_size (int vdes);
extern int repl_set_socket_tcp_nodelay (int sock_fd);

#endif /* _REPL_SUPPORT_H_ */
