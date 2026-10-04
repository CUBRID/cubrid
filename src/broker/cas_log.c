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
 * cas_log.c -
 */

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
#include <io.h>
#else
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#endif
#include <assert.h>

#include "porting.h"
#include "cas_common.h"
#include "cas_log.h"
#include "broker_config.h"
#include "broker_filename.h"
#include "broker_util.h"
#include "dbi.h"
#include "hide_password.h"
#include "cas_optimization.h"
#include "cas_common_vars.h"

#if defined(WINDOWS)
typedef int mode_t;
#endif /* WINDOWS */

#define CAS_LOG_BUFFER_SIZE (8192)
#define SQL_LOG_BUFFER_SIZE (163840)
#define ACCESS_LOG_IS_DENIED_TYPE(T)  ((T)==ACL_REJECTED)

#define CAS_LOG_VISIBLE_PW     0
#define CAS_LOG_HIDE_PW        1

static const char *get_access_log_type_string (ACCESS_LOG_TYPE type);
static char cas_log_buffer[CAS_LOG_BUFFER_SIZE];	/* 8K buffer */
static char sql_log_buffer[SQL_LOG_BUFFER_SIZE];

/*
 * SQL / slow logs bypass stdio to buffer writes and reduce flushes.
 * In ON(=ALL) mode a one-shot SIGUSR2 timer flushes the SQL log with pwrite ().
 */
#define SLOW_LOG_BUFFER_SIZE (SQL_LOG_BUFFER_SIZE)
static char slow_log_buffer[SLOW_LOG_BUFFER_SIZE];

/* SQL_LOG=ERROR, TIMEOUT or NOTICE put the current unit in this scratch file and append it to the log when cas_log_end () keeps it */
static char scratch_filepath[BROKER_PATH_MAX];

/*
 * Log buffer layout.
 *
 *  file    [ before open ][ flushed by earlier buffers ][ flushed by this buffer ][ not yet flushed ]
 *          ^              ^                             ^                         ^
 *          file_open_pos  file_buf_base                 + buf_flushed             + buf_used  (logical end)
 *                                                                      ^ saved_log_fpos
 *
 *  buffer  [ already in the file ][ not yet in the file ][ free                     ]
 *          ^                      ^                      ^                          ^
 *          0                      buf_flushed            buf_used                   buf_capacity
 *                                                        = logical file end (file_buf_base + buf_used)
 *
 * NOTE:
 *   saved_log_fpos is SQL log only, cas_ftell () at the start of the current unit.  It is before file_buf_base
 *   only after a spill.
 *   Outside ON(=ALL) mode, the unit uses scratch_log_fd, which shares sql_log_buffer and is bound to the scratch
 *   file.  scratch_log_fd positions, and so saved_log_fpos and saved_temp_stmt_fpos, count from the unit start.
 *   In that case, sql_log_fd keeps buf_used at 0, and its file_buf_base is the SQL log position where
 *   copy_scratch_to_sql_log () writes the unit.
 */
typedef struct cas_log_fd CAS_LOG_FD;
struct cas_log_fd
{
  int fd;			/* raw fd, -1 while closed */
  char *buf;

  /* buf_capacity >= buf_used >= buf_flushed */
  int buf_capacity;		/* size of buf */
  volatile sig_atomic_t buf_used;	/* committed data, changed by the main flow only */
  volatile sig_atomic_t buf_flushed;	/* flushed data, advanced by the main flow or the signal handler */

  INT64 file_open_pos;		/* file size when opened, a rewind never goes below this */
  INT64 file_buf_base;		/* file position of buf[0] */
};

/* buf_used / buf_flushed must hold the entire buffer size. */
static_assert (SIG_ATOMIC_MAX >= SQL_LOG_BUFFER_SIZE, "SQL_LOG_BUFFER_SIZE does not fit in sig_atomic_t");

static CAS_LOG_FD sql_log_fd = { -1, sql_log_buffer, SQL_LOG_BUFFER_SIZE, 0, 0, 0, 0 };
static CAS_LOG_FD slow_log_fd = { -1, slow_log_buffer, SLOW_LOG_BUFFER_SIZE, 0, 0, 0, 0 };
static CAS_LOG_FD scratch_log_fd = { -1, sql_log_buffer, SQL_LOG_BUFFER_SIZE, 0, 0, 0, 0 };	/* SQL log unit outside ON(=ALL) mode */

#define CAS_LOG_COMPILER_BARRIER() __asm__ __volatile__ ("" ::: "memory")
/* the barriers keep the compiler from moving buffer accesses across the flag update */
#define CAS_LOG_WRITING_BEGIN() \
  do { CAS_LOG_COMPILER_BARRIER (); sql_log_writing = 1; CAS_LOG_COMPILER_BARRIER (); } while (0)
/* clear the flag first, then flush what the handler deferred while it was set */
#define CAS_LOG_WRITING_END() \
  do { \
    CAS_LOG_COMPILER_BARRIER (); \
    sql_log_writing = 0; \
    CAS_LOG_COMPILER_BARRIER (); \
    if (sql_log_write_flush_pending) \
      { \
	sql_log_write_flush_pending = 0; \
	cas_fflush (&sql_log_fd); \
      } \
  } while (0)
#define CAS_LOG_SET_UNMASKED(v) \
  do { CAS_LOG_COMPILER_BARRIER (); sql_log_unmasked = (v); CAS_LOG_COMPILER_BARRIER (); } while (0)

static char *make_sql_log_filename (T_CUBRID_FILE_ID fid, char *filename_buf, size_t buf_size, const char *br_name);
static void cas_log_backup (T_CUBRID_FILE_ID fid);


#if defined (ENABLE_UNUSED_FUNCTION)
static void cas_log_rename (int run_time, time_t cur_time, char *br_name, int as_index);
#endif
static void cas_log_write_internal (CAS_LOG_FD * fp, struct timeval *log_time, unsigned int seq_num,
				    const char *fmt, va_list ap);
static void cas_log_write2_internal (CAS_LOG_FD * fp, const char *fmt, va_list ap);

static FILE *access_log_open (char *log_file_name);
static void cas_log_write_query_string_internal (char *query, int size, bool newline,
						 HIDE_PWD_INFO_PTR hide_pwd_info_ptr, bool ishidepw);
static void cas_log_compile_begin_internal (char *query, bool newline);
static void cas_log_compile_end_internal (char *query, bool newline, HIDE_PWD_INFO_PTR hide_pwd_info_ptr);

#ifdef CAS_ERROR_LOG
static int error_file_offset;
static char cas_log_error_flag;
#endif
static CAS_LOG_FD *log_fp = NULL, *slow_log_fp = NULL;
static char log_filepath[BROKER_PATH_MAX], slow_log_filepath[BROKER_PATH_MAX];
static INT64 saved_log_fpos = 0;
static CAS_LOG_FD_STATUS cas_log_fd_status = CAS_LOG_FD_NONE;

static inline size_t cas_fwrite (const void *ptr, size_t size, size_t nmemb, CAS_LOG_FD * lfd);
static inline void cas_fwrite_oneline (CAS_LOG_FD * lfd, const char *str);
static inline INT64 cas_ftell (CAS_LOG_FD * lfd);
static inline int cas_fseek (CAS_LOG_FD * lfd, INT64 offset, int whence);
static CAS_LOG_FD *cas_fopen (CAS_LOG_FD * lfd, const char *path, const char *mode);
#if defined (WINDOWS)
static FILE *cas_fopen_and_lock (const char *path, const char *mode);
#endif
static int cas_fclose (CAS_LOG_FD * lfd);
static inline int cas_fflush (CAS_LOG_FD * lfd);
static inline int cas_fflush_partial (CAS_LOG_FD * lfd, int end);
static int cas_fprintf (void *stream, const char *format, ...);
static inline int cas_fputc (int c, CAS_LOG_FD * lfd);
static int cas_unlink (const char *pathname);
static int cas_rename (const char *oldpath, const char *newpath);
static int cas_mkdir (const char *pathname, mode_t mode);
static void access_log_backup (char *access_log_file, struct tm *ct);

static volatile sig_atomic_t sql_log_writing = 0;
static volatile sig_atomic_t sql_log_write_flush_pending = 0;

/*
 * One of the following indicates how the statement between compile_begin and compile_end is handled.
 *   BUFFERED  its text is in the buffer with passwords visible, so nothing flushes it until compile_end masks it.
 *   OVERFLOW  its text did not fit in the buffer, so nothing is written until compile_end writes it masked.
 */
#define SQL_LOG_UNMASKED_NONE      0
#define SQL_LOG_UNMASKED_BUFFERED  1
#define SQL_LOG_UNMASKED_OVERFLOW  2
static volatile sig_atomic_t sql_log_unmasked = SQL_LOG_UNMASKED_NONE;
static volatile sig_atomic_t sql_log_unmask_flush_pending = 0;
static timer_t sql_log_timer;	/* SQL log only, cas_slow_log_end () flushes the slow log */
static bool sql_log_timer_created = false;
/* a flush is scheduled, so do not arm another timer until it fires */
static volatile sig_atomic_t sql_log_timer_armed = 0;

static void arm_flush_timer (void);
static void cas_log_timer_init (void);
static int pwrite_all (int fd, const char *p, size_t len, INT64 off);
static int ftruncate_all (int fd, INT64 len);
static void cas_log_start_unit (void);
static void copy_scratch_to_sql_log (void);

static INT64 saved_temp_stmt_fpos = 0;

static char *
make_sql_log_filename (T_CUBRID_FILE_ID fid, char *filename_buf, size_t buf_size, const char *br_name)
{

  char dirname[BROKER_PATH_MAX];
  int ret = 0;

  assert (filename_buf != NULL);

  get_cubrid_file (fid, dirname, BROKER_PATH_MAX);
  switch (fid)
    {
    case FID_SQL_LOG_DIR:
      if (cas_shard_flag == ON)
	{
	  ret = snprintf (filename_buf, buf_size, "%s%s_%d_%d_%d.sql.log", dirname, br_name, shm_proxy_id + 1,
			  shm_shard_id, shm_shard_cas_id + 1);
	}
      else
	{
	  ret = snprintf (filename_buf, buf_size, "%s%s_%d.sql.log", dirname, br_name, shm_as_index + 1);
	}
      break;
    case FID_SLOW_LOG_DIR:
      if (cas_shard_flag == ON)
	{
	  ret = snprintf (filename_buf, buf_size, "%s%s_%d_%d_%d.slow.log", dirname, br_name, shm_proxy_id + 1,
			  shm_shard_id, shm_shard_cas_id + 1);
	}
      else
	{
	  ret = snprintf (filename_buf, buf_size, "%s%s_%d.slow.log", dirname, br_name, shm_as_index + 1);
	}
      break;
    default:
      assert (0);
      ret = snprintf (filename_buf, buf_size, "unknown.log");
      break;
    }
  if (ret < 0)
    {
      assert (false);
      filename_buf[0] = '\0';
    }
  return filename_buf;

  return NULL;
}

void
cas_log_open (char *br_name)
{

  if (log_fp != NULL)
    {
      cas_log_close (true);
    }

  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      if (br_name != NULL)
	{
	  if (as_info->cas_log_reset == CAS_LOG_RESET_REOPEN)
	    {
	      set_cubrid_file (FID_SQL_LOG_DIR, shm_appl->log_dir);
	    }

	  make_sql_log_filename (FID_SQL_LOG_DIR, log_filepath, BROKER_PATH_MAX, br_name);
	}

      /* note: in "a+" mode, output is always appended */
      cas_log_fd_status = CAS_LOG_FD_OPENING;
      log_fp = cas_fopen (&sql_log_fd, log_filepath, "r+");
      if (log_fp != NULL)
	{
	  cas_fseek (log_fp, 0, SEEK_END);
	  saved_log_fpos = cas_ftell (log_fp);
	}
      else
	{
	  log_fp = cas_fopen (&sql_log_fd, log_filepath, "w");
	  saved_log_fpos = 0;
	}

      /* scratch file of this slot, used for the unit outside ON(=ALL) mode */
      if (log_fp != NULL)
	{
	  const char *dir_end = strrchr (log_filepath, '/');
	  int dir_len = (dir_end == NULL) ? 0 : (int) (dir_end - log_filepath + 1);

	  /* <dir>/<name>.sql.log to <dir>/.<name>.sql.log.scratch */
	  snprintf (scratch_filepath, BROKER_PATH_MAX, "%.*s.%s.scratch", dir_len, log_filepath,
		    log_filepath + dir_len);

	  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_ALL)
	    {
	      int scratch_fd;

	      cas_unlink (scratch_filepath);	/* a leftover file or a planted link, never its target */
	      scratch_fd = open (scratch_filepath, O_RDWR | O_CREAT | O_EXCL, 0600);

	      if (scratch_fd >= 0)
		{
		  scratch_log_fd.file_open_pos = 0;
		  scratch_log_fd.file_buf_base = 0;
		  scratch_log_fd.buf_used = 0;
		  scratch_log_fd.buf_flushed = 0;
		  scratch_log_fd.fd = scratch_fd;
		  saved_log_fpos = 0;
		  log_fp = &scratch_log_fd;
		}
	      else
		{
		  /* treated like a failed log open, the next log call retries */
		  cas_fclose (&sql_log_fd);
		  log_fp = NULL;
		}
	    }
	  else
	    {
	      /* remove a leftover scratch file */
	      cas_unlink (scratch_filepath);
	    }
	}

      cas_log_fd_status = CAS_LOG_FD_OPENED;
    }
  else
    {
      log_fp = NULL;
      saved_log_fpos = 0;
    }
  as_info->cas_log_reset = 0;

}

void
cas_log_reset (char *br_name)
{

  if (as_info->cas_log_reset)
    {
      if (log_fp != NULL)
	{
	  cas_log_close (true);
	}
      if ((as_info->cas_log_reset & CAS_LOG_RESET_REMOVE) != 0)
	{
	  cas_unlink (log_filepath);
	}

      if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
	{
	  cas_log_open (br_name);
	}
    }

}

void
cas_log_close (bool flag)
{

  if (log_fp != NULL)
    {
      if (flag)
	{
	  cas_fseek (log_fp, saved_log_fpos, SEEK_SET);	/* discard the unfinished unit */
	}
      cas_log_fd_status = CAS_LOG_FD_CLOSING;
      if (log_fp == &scratch_log_fd)
	{
	  cas_fclose (&scratch_log_fd);
	  cas_fclose (&sql_log_fd);
	  cas_unlink (scratch_filepath);
	}
      else
	{
	  cas_fclose (log_fp);
	}
      cas_log_fd_status = CAS_LOG_FD_CLOSED;
      log_fp = NULL;
      saved_log_fpos = 0;
    }

}


/*
 * cas_log_flush_on_exit () - flush pending SQL / slow logs before exiting.
 *   Called from the terminating signal handler.  Async-signal-safe.
 */
void
cas_log_flush_on_exit (void)
{
  CAS_LOG_WRITING_BEGIN ();
  if (sql_log_fd.fd >= 0)
    {
      if (!sql_log_unmasked)	/* SQL_LOG_UNMASKED_NONE */
	{
	  (void) cas_fflush (&sql_log_fd);
	}
      else if (log_fp == &sql_log_fd)
	{
	  /* keep the lines before the statement being compiled, which may still hold a password */
	  INT64 end = saved_temp_stmt_fpos - sql_log_fd.file_buf_base;

	  (void) cas_fflush_partial (&sql_log_fd, (int) MIN (end, (INT64) sql_log_fd.buf_used));
	}
    }
  if (slow_log_fd.fd >= 0)
    {
      (void) cas_fflush (&slow_log_fd);
    }
}

static void
cas_log_backup (T_CUBRID_FILE_ID fid)
{
  char backup_filepath[BROKER_PATH_MAX];
  char *filepath = NULL;

  switch (fid)
    {
    case FID_SQL_LOG_DIR:
      assert (log_filepath[0] != '\0');
      filepath = log_filepath;
      break;
    case FID_SLOW_LOG_DIR:
      assert (slow_log_filepath[0] != '\0');
      filepath = slow_log_filepath;
      break;
    default:
      assert (0);
      return;
    }

  if (snprintf (backup_filepath, BROKER_PATH_MAX, "%s.bak", filepath) < 0)
    {
      assert (false);
      return;
    }
  cas_unlink (backup_filepath);
  cas_rename (filepath, backup_filepath);
}


#if defined (ENABLE_UNUSED_FUNCTION)
static void
cas_log_rename (int run_time, time_t cur_time, char *br_name, int as_index)
{
  char new_filepath[BROKER_PATH_MAX];
  struct tm tmp_tm;

  assert (log_filepath[0] != '\0');

  localtime_r (&cur_time, &tmp_tm);
  tmp_tm.tm_year += 1900;

  snprintf (new_filepath, BROKER_PATH_MAX, "%s.%02d%02d%02d%02d%02d.%d", log_filepath, tmp_tm.tm_mon + 1,
	    tmp_tm.tm_mday, tmp_tm.tm_hour, tmp_tm.tm_min, tmp_tm.tm_sec, run_time);
  cas_rename (log_filepath, new_filepath);
}
#endif /* ENABLE_UNUSED_FUNCTION */

void
cas_log_end (int mode, int run_time_sec, int run_time_msec)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      bool abandon = false;

      /* 'mode' will be either ALL, ERROR, or TIMEOUT */
      switch (mode)
	{
	case SQL_LOG_MODE_ALL:
	  /* if mode == ALL, write log regardless sql_log_mode */
	  break;
	case SQL_LOG_MODE_ERROR:
	  /* if mode == ERROR, write log if sql_log_mode == ALL || ERROR || NOTICE */
	  if (as_info->cur_sql_log_mode == SQL_LOG_MODE_NONE || as_info->cur_sql_log_mode == SQL_LOG_MODE_TIMEOUT)
	    {
	      abandon = true;
	    }
	  break;
	case SQL_LOG_MODE_TIMEOUT:
	  /* if mode == TIMEOUT, write log if sql_log_mode == ALL || TIMEOUT || NOTICE */
	  if (as_info->cur_sql_log_mode == SQL_LOG_MODE_NONE || as_info->cur_sql_log_mode == SQL_LOG_MODE_ERROR)
	    {
	      abandon = true;
	    }
	  /* if mode == TIMEOUT and sql_log_mode == TIMEOUT || NOTICE, check if it timed out */
	  else if (as_info->cur_sql_log_mode == SQL_LOG_MODE_TIMEOUT
		   || as_info->cur_sql_log_mode == SQL_LOG_MODE_NOTICE)
	    {
	      /* check timeout */
	      if ((run_time_sec * 1000 + run_time_msec) < shm_appl->long_transaction_time)
		{
		  abandon = true;
		}
	    }
	  break;
	  /* if mode == NONE, write log if sql_log_mode == ALL */
	case SQL_LOG_MODE_NONE:
	  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_ALL)
	    {
	      abandon = true;
	    }
	  break;
	case SQL_LOG_MODE_NOTICE:
	default:
	  /* mode NOTICE or others are unexpected values; do not write log */
	  abandon = true;
	  break;
	}

      if (abandon)
	{
	  cas_fseek (log_fp, saved_log_fpos, SEEK_SET);	/* discard the unit */
	}
      else
	{
	  INT64 sql_log_logical_end;

	  if (run_time_sec >= 0 && run_time_msec >= 0)
	    {
	      cas_log_write (0, false, "*** elapsed time %d.%03d\n", run_time_sec, run_time_msec);
	    }
	  if (log_fp == &scratch_log_fd)
	    {
	      copy_scratch_to_sql_log ();
	      sql_log_logical_end = cas_ftell (&sql_log_fd);
	    }
	  else
	    {
	      saved_log_fpos = cas_ftell (log_fp);
	      sql_log_logical_end = saved_log_fpos;
	    }

	  if ((sql_log_logical_end / 1000) > shm_appl->sql_log_max_size)
	    {
	      cas_log_close (true);
	      cas_log_backup (FID_SQL_LOG_DIR);
	      cas_log_open (NULL);
	    }
	}
    }

}

/*
 * cas_log_start_unit () - start a new unit at the current position.
 *
 * NOTE:
 *   A unit is the part of the log that one cas_log_end () decision keeps or discards.  In ON(=ALL) mode,
 *   every unit that reaches cas_log_end () is kept.  In other modes, moving the boundary keeps the preceding
 *   part even if the new unit is later discarded.
 */
static void
cas_log_start_unit (void)
{
  if (log_fp == &scratch_log_fd)
    {
      /* the unit always starts at offset 0 in the scratch, so saved_log_fpos needs no update */
      copy_scratch_to_sql_log ();
    }
  else
    {
      saved_log_fpos = cas_ftell (log_fp);
    }
}

/*
 * copy_scratch_to_sql_log () - copy the current unit from the scratch to the end of the SQL log file.
 *   If the unit has not spilled, copy it directly from the buffer.  Otherwise, flush the scratch buffer
 *   and copy the whole scratch file with sendfile ().  The unit is reset afterwards.
 *   A copy failure drops the unit or keeps the part already copied, like a failed flush drops the buffer.
 *
 * NOTE:
 *   sendfile () reads the scratch file, so cas_log_open () opens it with O_RDWR.
 */
static void
copy_scratch_to_sql_log (void)
{
  CAS_LOG_FD *unit = &scratch_log_fd;
  CAS_LOG_FD *file = &sql_log_fd;
  INT64 unit_len = unit->file_buf_base + unit->buf_used;

  if (unit_len == 0)
    {
      return;
    }

  if (unit->file_buf_base == 0)
    {
      if (pwrite_all (file->fd, unit->buf, (size_t) unit->buf_used, file->file_buf_base) == 0)
	{
	  file->file_buf_base += unit_len;
	}
    }
  else if (cas_fflush (unit) == 0 && lseek (file->fd, (off_t) file->file_buf_base, SEEK_SET) >= 0)
    {
      off_t scratch_off = 0;

      while (scratch_off < unit_len)
	{
	  ssize_t copied = sendfile (file->fd, unit->fd, &scratch_off, (size_t) (unit_len - scratch_off));

	  if (copied <= 0)
	    {
	      if (copied < 0 && errno == EINTR)
		{
		  continue;
		}
	      break;
	    }
	}
      file->file_buf_base += scratch_off;	/* one store, so a terminating handler copies once at most */
    }

  cas_fseek (unit, 0, SEEK_SET);	/* reset the buffer and truncate the scratch file if it was used */
}

static void
cas_log_write_internal (CAS_LOG_FD * fp, struct timeval *log_time, unsigned int seq_num, const char *fmt, va_list ap)
{
  char *buf, *p;
  int len, n;

  p = buf = cas_log_buffer;
  len = CAS_LOG_BUFFER_SIZE;
  n = ut_time_string (p, log_time);
  len -= n;
  p += n;

  if (len > 0)
    {
      n = snprintf (p, len, " (%u) ", seq_num);
      len -= n;
      p += n;
      if (len > 0)
	{
	  n = vsnprintf (p, len, fmt, ap);
	  if (n >= len)
	    {
	      /* string is truncated and trailing '\0' is included */
	      n = len - 1;
	    }
	  len -= n;
	  p += n;
	}
    }

  cas_fwrite (buf, (p - buf), 1, fp);

}

void
cas_log_write_nonl (unsigned int seq_num, bool unit_start, const char *fmt, ...)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  cas_log_start_unit ();
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num, fmt, ap);
      va_end (ap);
    }

}

void
cas_log_write_nonl_noflush (unsigned int seq_num, bool unit_start, const char *fmt, ...)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  cas_log_start_unit ();
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num, fmt, ap);
      va_end (ap);
    }

}

static void
cas_log_query_cancel (int dummy, ...)
{

  va_list ap;
  const char *fmt;
  char buf[LINE_MAX];
  struct timeval tv;

  if (log_fp == NULL || query_cancel_flag != 1)
    {
      return;
    }

  tv.tv_sec = query_cancel_time / 1000;
  tv.tv_usec = (query_cancel_time % 1000) * 1000;

  if (as_info->clt_version >= CAS_PROTO_MAKE_VER (PROTOCOL_V1))
    {
      char ip_str[16];
      ut_get_ipv4_string (ip_str, 16, as_info->cas_clt_ip);
      fmt = "query_cancel client ip %s port %u";
      snprintf (buf, LINE_MAX, fmt, ip_str, as_info->cas_clt_port);
    }
  else
    {
      snprintf (buf, LINE_MAX, "query_cancel");
    }

  va_start (ap, dummy);
  cas_log_write_internal (log_fp, &tv, 0, buf, ap);
  va_end (ap);
  cas_fputc ('\n', log_fp);

  query_cancel_flag = 0;

}

void
cas_log_write (unsigned int seq_num, bool unit_start, const char *fmt, ...)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  cas_log_query_cancel (0);

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  cas_log_start_unit ();
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num, fmt, ap);
      va_end (ap);
      cas_fputc ('\n', log_fp);
    }

}

void
cas_log_write_and_end (unsigned int seq_num, bool unit_start, const char *fmt, ...)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      if (unit_start)
	{
	  cas_log_start_unit ();
	}
      va_start (ap, fmt);
      cas_log_write_internal (log_fp, NULL, seq_num, fmt, ap);
      va_end (ap);
      cas_fputc ('\n', log_fp);
      cas_log_end (SQL_LOG_MODE_ALL, -1, -1);
    }

}

void
cas_log_open_and_write (char *br_name, unsigned int seq_num, bool unit_start, const char *fmt, ...)
{
  CAS_LOG_FD *fp = NULL;
  va_list ap;

  if (as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      if (log_filepath[0] == '\0')
	{
	  if (br_name != NULL)
	    {
	      make_sql_log_filename (FID_SQL_LOG_DIR, log_filepath, BROKER_PATH_MAX, br_name);
	    }
	  else
	    {
	      return;
	    }
	}

      if (log_fp != NULL)
	{
	  fp = log_fp;
	}
      else
	{
	  fp = cas_fopen (&sql_log_fd, log_filepath, "a");
	  if (fp == NULL)
	    {
	      fp = cas_fopen (&sql_log_fd, log_filepath, "w");
	      if (fp == NULL)
		{
		  return;
		}
	    }
	}

      va_start (ap, fmt);
      cas_log_write_internal (fp, NULL, seq_num, fmt, ap);
      va_end (ap);
      cas_fputc ('\n', fp);

      if (fp == log_fp)
	{
	  cas_fflush (fp);
	}
      else
	{
	  cas_fclose (fp);
	}
      fp = NULL;
    }
}

CAS_LOG_FD_STATUS
cas_log_get_fd_status (void)
{
  return cas_log_fd_status;
}

static void
cas_log_write2_internal (CAS_LOG_FD * fp, const char *fmt, va_list ap)
{
  char *buf, *p;
  int len, n;

  p = buf = cas_log_buffer;
  len = CAS_LOG_BUFFER_SIZE;
  n = vsnprintf (p, len, fmt, ap);
  if (n >= len)
    {
      /* string is truncated and trailing '\0' is included */
      n = len - 1;
    }
  len -= n;
  p += n;

  cas_fwrite (buf, (p - buf), 1, fp);

}

void
cas_log_write2_nonl_noflush (const char *fmt, ...)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (log_fp, fmt, ap);
      va_end (ap);
    }

}

void
cas_log_write2 (const char *fmt, ...)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (log_fp, fmt, ap);
      va_end (ap);
      cas_fputc ('\n', log_fp);
    }

}

void
cas_log_write_value_string (char *value, int size)
{

  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL)
    {
      cas_fwrite (value, size, 1, log_fp);
    }

}

void
cas_log_compile_begin_write_query_string (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  cas_log_compile_begin_internal (query, true);
}

void
cas_log_compile_end_write_query_string (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  cas_log_compile_end_internal (query, true, hide_pwd_info_ptr);
}

void
cas_log_compile_begin_write_query_string_nonl (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  cas_log_compile_begin_internal (query, false);
}

void
cas_log_compile_end_write_query_string_nonl (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  cas_log_compile_end_internal (query, false, hide_pwd_info_ptr);
}

static void
cas_log_compile_begin_internal (char *query, bool newline)
{
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  assert (!sql_log_unmasked);	/* every compile_begin is followed by compile_end */
  if (log_fp != NULL && query != NULL)
    {
      saved_temp_stmt_fpos = cas_ftell (log_fp);
      if ((int) strlen (query) < log_fp->buf_capacity - log_fp->buf_used)
	{
	  CAS_LOG_SET_UNMASKED (SQL_LOG_UNMASKED_BUFFERED);
	  cas_log_write_query_string_internal (query, 0, newline, NULL, CAS_LOG_VISIBLE_PW);
	}
      else
	{
	  CAS_LOG_SET_UNMASKED (SQL_LOG_UNMASKED_OVERFLOW);
	}
    }
}

static void
cas_log_compile_end_internal (char *query, bool newline, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL && query != NULL)
    {
      /* rewrite unless the text is already there and has no password */
      if (sql_log_unmasked == SQL_LOG_UNMASKED_OVERFLOW || hide_pwd_info_ptr == NULL || hide_pwd_info_ptr->used > 0)
	{
	  cas_fseek (log_fp, saved_temp_stmt_fpos, SEEK_SET);
	  cas_log_write_query_string_internal (query, 0, newline, hide_pwd_info_ptr, CAS_LOG_HIDE_PW);
	}
    }

  CAS_LOG_SET_UNMASKED (SQL_LOG_UNMASKED_NONE);
  saved_temp_stmt_fpos = 0;

  if (sql_log_unmask_flush_pending)
    {
      CAS_LOG_WRITING_BEGIN ();
      cas_fflush (&sql_log_fd);
      sql_log_unmask_flush_pending = 0;
      CAS_LOG_WRITING_END ();
    }
}

void
cas_log_write_query_string_nonl (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  cas_log_write_query_string_internal (query, size, false, hide_pwd_info_ptr, CAS_LOG_HIDE_PW);
}

void
cas_log_write_query_string (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  cas_log_write_query_string_internal (query, size, true, hide_pwd_info_ptr, CAS_LOG_HIDE_PW);
}

static void
cas_fprintf_password (CAS_LOG_FD * fp, char *query, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{
  if (hide_pwd_info_ptr != NULL && hide_pwd_info_ptr->pwd_info_ptr != NULL && hide_pwd_info_ptr->used == 0)
    {
      /* 'used' is reset to 0 by INIT_HIDE_PASSWORD_INFO() in parser_create_parser()
       * and incremented by password_add_offset() in csql_grammar.y for each password found while parsing.
       * A zero value means the statement was already scanned and has no password,
       * so write the SQL directly without the printf machinery. */
      cas_fwrite_oneline (fp, query);
      return;
    }

  password_fprintf (fp, query, hide_pwd_info_ptr, cas_fprintf);
}

static void
cas_log_write_query_string_internal (char *query, int size, bool newline, HIDE_PWD_INFO_PTR hide_pwd_info_ptr,
				     bool ishidepw)
{
  if (log_fp == NULL && as_info->cur_sql_log_mode != SQL_LOG_MODE_NONE)
    {
      cas_log_open (shm_appl->broker_name);
    }

  if (log_fp != NULL && query != NULL)
    {
      if (ishidepw == CAS_LOG_HIDE_PW)
	{
	  cas_fprintf_password (log_fp, query, hide_pwd_info_ptr);
	}
      else
	{
	  cas_fwrite_oneline (log_fp, query);
	}

      if (newline)
	{
	  cas_fputc ('\n', log_fp);
	}
    }
}

void
cas_log_write_client_ip (const unsigned char *ip_addr)
{
  char client_ip_str[16];

  if (ip_addr != NULL && *((int *) ip_addr) != 0)
    {
      ut_get_ipv4_string (client_ip_str, sizeof (client_ip_str), ip_addr);
      cas_log_write_and_end (0, false, "CLIENT IP %s", client_ip_str);
    }
}

#if !defined (NDEBUG)
void
cas_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
#if 0

  if (log_fp != NULL)
    {
      char buf[LINE_MAX], *p;
      int len, n;
      va_list ap;

      va_start (ap, fmt);
      p = buf;
      len = LINE_MAX;
      n = ut_time_string (p);
      len -= n;
      p += n;
      if (len > 0)
	{
	  n = snprintf (p, len, " (debug) file %s line %d ", file_name, line_no);
	  len -= n;
	  p += n;
	  if (len > 0)
	    {
	      n = vsnprintf (p, len, fmt, ap);
	      len -= n;
	      p += n;
	    }
	}
      cas_fwrite (buf, (p - buf), 1, log_fp);
      cas_fputc ('\n', log_fp);
      va_end (ap);
    }

#endif
}
#endif

#ifdef CAS_ERROR_LOG

#if defined (ENABLE_UNUSED_FUNCTION)
void
cas_error_log (int err_code, char *err_msg_str, int client_ip_addr)
{

  FILE *fp;
  char *err_log_file = shm_appl->error_log_file;
  char *script_file = getenv (PATH_INFO_ENV_STR);
  time_t t = time (NULL);
  struct tm ct1;
  char err_code_str[12];
  char *lastcmd = "";
  char *ip_str;

  localtime_r (&t, &ct1);
  ct1.tm_year += 1900;

  fp = access_log_open (err_log_file);
  if (fp == NULL)
    {
      return;
    }

#ifdef CAS_ERROR_LOG
  error_file_offset = ftell (fp);
#endif

  if (script_file == NULL)
    script_file = "";
  sprintf (err_code_str, "%d", err_code);
  ip_str = ut_uchar2ipstr ((unsigned char *) (&client_ip_addr));

  fprintf (fp, "[%d] %s %s %d/%d/%d %d:%d:%d %d\n%s:%s\ncmd:%s\n", (int) getpid (), ip_str, script_file,
	   ct1.tm_year, ct1.tm_mon + 1, ct1.tm_mday, ct1.tm_hour, ct1.tm_min, ct1.tm_sec,
	   (int) (strlen (err_code_str) + strlen (err_msg_str) + 1), err_code_str, err_msg_str, lastcmd);
  fclose (fp);

  cas_log_error_flag = 1;

}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif

int
cas_access_log (struct timeval *start_time, int as_index, int client_ip_addr, char *dbname, char *dbuser,
		ACCESS_LOG_TYPE log_type)
{

  FILE *fp;
  char *access_log_file = shm_appl->access_log_file;
  char clt_ip_str[16];
  struct tm start_tm;
  time_t start_sec;
  struct timeval end_time;
  char log_file_buf[PATH_MAX];
  const char *print_format = "%d %s %04d/%02d/%02d %02d:%02d:%02d %s %s %s %s\n";
  char session_id_buf[16];

  gettimeofday (&end_time, NULL);

  start_sec = start_time->tv_sec;
  if (localtime_r (&start_sec, &start_tm) == NULL)
    {
      return -1;
    }
  start_tm.tm_year += 1900;

  if (ACCESS_LOG_IS_DENIED_TYPE (log_type))
    {
      int n;
      n = snprintf (log_file_buf, PATH_MAX, "%s%s", shm_appl->access_log_file, ACCESS_LOG_DENIED_FILENAME_POSTFIX);
      if (n >= PATH_MAX)
	{
	  return -1;
	}

      access_log_file = log_file_buf;
    }

  fp = access_log_open (access_log_file);
  if (fp == NULL)
    {
      return -1;
    }

  fseek (fp, 0, SEEK_END);
  if ((ftell (fp) / ONE_K) > shm_appl->access_log_max_size)
    {
      time_t backup_sec = time (NULL);
      struct tm backup_tm;

      if (localtime_r (&backup_sec, &backup_tm) != NULL)
	{
	  backup_tm.tm_year += 1900;

	  fclose (fp);

	  access_log_backup (access_log_file, &backup_tm);

	  fp = access_log_open (access_log_file);
	  if (fp == NULL)
	    {
	      return -1;
	    }
	}
    }

  ut_get_ipv4_string (clt_ip_str, sizeof (clt_ip_str), (unsigned char *) (&client_ip_addr));

  session_id_buf[0] = '\0';

  if (!ACCESS_LOG_IS_DENIED_TYPE (log_type))
    {
      sprintf (session_id_buf, "%u", db_get_session_id ());
    }

  fprintf (fp, print_format, as_index + 1, clt_ip_str, start_tm.tm_year, start_tm.tm_mon + 1, start_tm.tm_mday,
	   start_tm.tm_hour, start_tm.tm_min, start_tm.tm_sec, dbname, dbuser,
	   get_access_log_type_string (log_type), session_id_buf);

  fclose (fp);
  return (end_time.tv_sec - start_time->tv_sec);
}

void
cas_log_query_info_init (int id, char is_only_query_plan)
{
  char *plan_dump_filename;

  plan_dump_filename = cas_log_query_plan_file (id);
  cas_unlink (plan_dump_filename);
  db_query_plan_dump_file (plan_dump_filename);

  if (is_only_query_plan)
    {
      set_optimization_level (514);
    }
  else
    {
      set_optimization_level (513);
    }
}

char *
cas_log_query_plan_file (int id)
{
  static char plan_file_name[BROKER_PATH_MAX];
  char dirname[BROKER_PATH_MAX];
  get_cubrid_file (FID_CAS_TMP_DIR, dirname, BROKER_PATH_MAX);
  if (snprintf (plan_file_name, BROKER_PATH_MAX - 1, "%s/%d.%d.plan", dirname, (int) getpid (), id) < 0)
    {
      assert (false);
      return NULL;
    }
  return plan_file_name;
}

static FILE *
access_log_open (char *log_file_name)
{
  FILE *fp;
  int ret;
  char *tmp_dirname;
  char *tmp_filename;

  if (log_file_name == NULL)
    return NULL;

#if defined (WINDOWS)
  fp = cas_fopen_and_lock (log_file_name, "a");
#else
  /* In case of Linux and solaris..., Openning a file in append mode guarantees subsequent write operations to occur at
   * ent-of-file. So we don't need to lock to the opened file. */
  fp = fopen (log_file_name, "a");
#endif

  if (fp == NULL)
    {
      if (errno == ENOENT)
	{
	  tmp_filename = strdup (log_file_name);
	  if (tmp_filename == NULL)
	    {
	      return NULL;
	    }
	  tmp_dirname = dirname (tmp_filename);
	  ret = cas_mkdir (tmp_dirname, 0777);
	  free (tmp_filename);
	  if (ret == 0)
	    {
#if defined (WINDOWS)
	      fp = cas_fopen_and_lock (log_file_name, "a");
#else
	      fp = fopen (log_file_name, "a");
#endif
	      if (fp == NULL)
		{
		  return NULL;
		}
	    }
	  else
	    {
	      return NULL;
	    }
	}
      else
	{
	  return NULL;
	}
    }
  return fp;
}

void
cas_slow_log_open (char *br_name)
{

  if (slow_log_fp != NULL)
    {
      cas_slow_log_close ();
    }

  if (as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      if (br_name != NULL)
	{
	  if (as_info->cas_slow_log_reset == CAS_LOG_RESET_REOPEN)
	    {
	      set_cubrid_file (FID_SLOW_LOG_DIR, shm_appl->slow_log_dir);
	    }

	  make_sql_log_filename (FID_SLOW_LOG_DIR, slow_log_filepath, BROKER_PATH_MAX, br_name);
	}

      /* note: in "a+" mode, output is always appended */
      slow_log_fp = cas_fopen (&slow_log_fd, slow_log_filepath, "a+");
    }
  else
    {
      slow_log_fp = NULL;
    }
  as_info->cas_slow_log_reset = 0;

}

void
cas_slow_log_reset (char *br_name)
{

  if (as_info->cas_slow_log_reset)
    {
      if (slow_log_fp != NULL)
	{
	  cas_slow_log_close ();
	}
      if ((as_info->cas_slow_log_reset & CAS_LOG_RESET_REMOVE) != 0)
	{
	  cas_unlink (slow_log_filepath);
	}

      if (as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
	{
	  cas_slow_log_open (br_name);
	}
    }

}

void
cas_slow_log_close ()
{

  if (slow_log_fp != NULL)
    {
      cas_fclose (slow_log_fp);
      slow_log_fp = NULL;
    }

}

void
cas_slow_log_end ()
{

  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      long slow_log_fpos;
      slow_log_fpos = cas_ftell (slow_log_fp);

      if ((slow_log_fpos / 1000) > shm_appl->sql_log_max_size)
	{
	  cas_slow_log_close ();
	  cas_log_backup (FID_SLOW_LOG_DIR);
	  cas_slow_log_open (NULL);
	}
      else
	{
	  cas_fputc ('\n', slow_log_fp);
	  cas_fflush (slow_log_fp);
	}
    }

}

void
cas_slow_log_write_and_end (struct timeval *log_time, unsigned int seq_num, const char *fmt, ...)
{

  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write_internal (slow_log_fp, log_time, seq_num, fmt, ap);
      va_end (ap);

      cas_slow_log_end ();
    }


}

void
cas_slow_log_write (struct timeval *log_time, unsigned int seq_num, bool unit_start, const char *fmt, ...)
{

  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write_internal (slow_log_fp, log_time, seq_num, fmt, ap);
      va_end (ap);
    }

}

void
cas_slow_log_write2 (const char *fmt, ...)
{

  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      va_list ap;

      va_start (ap, fmt);
      cas_log_write2_internal (slow_log_fp, fmt, ap);
      va_end (ap);
    }

}

void
cas_slow_log_write_value_string (char *value, int size)
{

  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL)
    {
      cas_fwrite (value, size, 1, slow_log_fp);
    }

}

void
cas_slow_log_write_query_string (char *query, int size, HIDE_PWD_INFO_PTR hide_pwd_info_ptr)
{

  if (slow_log_fp == NULL && as_info->cur_slow_log_mode != SLOW_LOG_MODE_OFF)
    {
      cas_slow_log_open (shm_appl->broker_name);
    }

  if (slow_log_fp != NULL && query != NULL)
    {
      cas_fprintf_password (slow_log_fp, query, hide_pwd_info_ptr);
      cas_fputc ('\n', slow_log_fp);
    }

}

/* glibc headers may define only the internal name of this field */
#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

/*
 * cas_log_timer_init () - create the one-shot timer that delivers SIGUSR2 for log flushing.
 *   Called from cas_fopen () on the first SQL / slow log open.  The SIGUSR2 handler is registered
 *   separately in cas_sig_init () before any log is opened.
 */
static void
cas_log_timer_init (void)
{
  struct sigevent sev;

  memset (&sev, 0, sizeof (sev));
  /* deliver only to this thread so that the handler never runs on a thread of a loaded driver */
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_notify_thread_id = (pid_t) syscall (SYS_gettid);
  sev.sigev_signo = SIGUSR2;
  sev.sigev_value.sival_ptr = &sql_log_fd;	/* tag: the handler ignores other SIGUSR2 signals */
  sql_log_timer_created = (timer_create (CLOCK_MONOTONIC, &sev, &sql_log_timer) == 0);

  /* If timer creation fails, the buffer is still flushed when full, at close and on exit.
   * arm_flush_timer () retries timer creation. */
}

/*
 * cas_log_sigusr2_handler () - flush the SQL log from the flush timer.
 *   Ignores SIGUSR2 not generated by our timer (si_code / si_value).
 *   Async-signal-safe.  SIGUSR2 is blocked while this handler runs.
 */
void
cas_log_sigusr2_handler (int signo, siginfo_t * info, void *ctx)
{
  int saved_errno = errno;

  (void) signo;
  (void) ctx;

  if (info == NULL || info->si_code != SI_TIMER || info->si_value.sival_ptr != (void *) &sql_log_fd)
    {
      return;
    }
  sql_log_timer_armed = 0;
  if (as_info == NULL || as_info->cur_sql_log_mode != SQL_LOG_MODE_ALL)
    {
      /* the log mode changed before the timer fired, so ignore it.
       * Modes other than ON(=ALL) flush at unit boundaries */
      return;
    }

  if (sql_log_unmasked)
    {
      sql_log_unmask_flush_pending = 1;	/* compile_end flushes once the statement is masked */
      errno = saved_errno;
      return;
    }
  if (sql_log_writing)
    {
      sql_log_write_flush_pending = 1;	/* cas_fwrite () flushes once the write is complete */
      errno = saved_errno;
      return;
    }

  cas_fflush (&sql_log_fd);

  errno = saved_errno;
}

/*
 * arm_flush_timer () - arm the one-shot flush timer for 1 s from now.
 *
 * NOTE:
 *   Do not report a timer creation failure to the SQL log.
 *   it_interval must stay 0.  A periodic SIGUSR2 would keep restarting the CAS poll () loops,
 *   whose timeouts count in DEFAULT_CHECK_INTERVAL (1 s) steps.
 */
static void
arm_flush_timer (void)
{
  struct itimerspec its;

  if (!sql_log_timer_created)
    {
      /* timer_create () failed earlier.  Retry at most once a second. */
      static time_t last_try = 0;
      time_t now = time (NULL);

      if (now == last_try)
	{
	  return;
	}
      last_try = now;
      cas_log_timer_init ();
      if (!sql_log_timer_created)
	{
	  return;
	}
    }

  memset (&its, 0, sizeof (its));
  its.it_value.tv_sec = 1;
  if (timer_settime (sql_log_timer, 0, &its, NULL) == 0)
    {
      sql_log_timer_armed = 1;
    }
}

static int
pwrite_all (int fd, const char *p, size_t len, INT64 off)
{
  while (len > 0)
    {
      ssize_t w = pwrite (fd, p, len, (off_t) off);

      if (w < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  return -1;
	}
      p += w;
      len -= (size_t) w;
      off += w;
    }
  return 0;
}

/* ftruncate () may return EINTR; retry like pwrite_all () does.  Only ever shrinks the file:
 * offset is below the physical end in both callers, so no sparse hole can be created. */
static int
ftruncate_all (int fd, INT64 len)
{
  int r;

  do
    {
      r = ftruncate (fd, (off_t) len);
    }
  while (r < 0 && errno == EINTR);
  return r;
}

/*
 * cas_fflush () - pwrite [buf_flushed, buf_used) and advance buf_flushed.
 *   Safe to overlap with the SIGUSR2 handler because buf_flushed is read once and only advanced.
 */
static inline int
cas_fflush (CAS_LOG_FD * lfd)
{
  return cas_fflush_partial (lfd, lfd->buf_used);
}

/*
 * cas_fflush_partial () - pwrite [buf_flushed, end) and advance buf_flushed to end.
 */
static inline int
cas_fflush_partial (CAS_LOG_FD * lfd, int end)
{
  int flushed;

  if (lfd->fd < 0)
    {
      return 0;
    }
  flushed = lfd->buf_flushed;
  if (end <= flushed)
    {
      return 0;
    }
  if (pwrite_all (lfd->fd, lfd->buf + flushed, (size_t) (end - flushed), lfd->file_buf_base + flushed) < 0)
    {
      /* pwrite_all () handles EINTR.
       * Other failures are a no-op for buf_flushed. */
      return -1;
    }
  lfd->buf_flushed = end;
  return 0;
}

/*
 * cas_fwrite () - append one piece of log data to the buffer.
 *   The only place that adds to the buffer.  A piece that does not fit empties the buffer to the
 *   file first.  A piece larger than the buffer itself goes straight to the file.
 *
 * NOTE:
 *   Copy first, then commit by updating buf_used once, so a signal handler never sees a
 *   half-copied piece.
 */
static inline size_t
cas_fwrite (const void *ptr, size_t size, size_t nmemb, CAS_LOG_FD * lfd)
{
  size_t n;
  size_t result;
  bool buf_overflow;
  bool oversized_log_data;

  n = size * nmemb;
  if (n == 0)
    {
      return 0;
    }
  if (lfd->fd < 0)
    {
      return 0;
    }

  result = nmemb;
  buf_overflow = (lfd->buf_used + (int) n > lfd->buf_capacity);
  oversized_log_data = ((INT64) n > (INT64) lfd->buf_capacity);

  CAS_LOG_WRITING_BEGIN ();

  if (buf_overflow)
    {
      INT64 advance;

      /* the buffer is full.  Flush it and start over, dropping the unflushed tail on failure. */
      if (cas_fflush (lfd) < 0)
	{
	  advance = lfd->buf_flushed;
	}
      else
	{
	  advance = lfd->buf_used;
	}
      lfd->buf_used = 0;	/* zero buf_used first so a terminating handler flushes nothing */
      lfd->buf_flushed = 0;
      lfd->file_buf_base += advance;
    }

  if (oversized_log_data)
    {
      /* larger than the buffer, so write it straight to the file */
      if (pwrite_all (lfd->fd, (const char *) ptr, n, lfd->file_buf_base) < 0)
	{
	  result = 0;
	}
      else
	{
	  lfd->file_buf_base += (INT64) n;
	}
      goto done;
    }

  memcpy (lfd->buf + lfd->buf_used, ptr, n);
  CAS_LOG_COMPILER_BARRIER ();
  lfd->buf_used += (int) n;
  if (!sql_log_timer_armed && !sql_log_write_flush_pending && lfd == &sql_log_fd)
    {
      arm_flush_timer ();
    }

done:
  CAS_LOG_WRITING_END ();
  return result;
}

/*
 * cas_fwrite_oneline () -
 *   Write a string, replacing embedded newlines with spaces so the SQL stays on one log line.
 *   strcspn () finds each newline-free run, which is written in a single cas_fwrite ()
 *   instead of one call per character.
 *
 * note:
 *   This is a byte-level scan rather than a SQL parse,
 *   so newlines inside string literals are also replaced.
 */
static inline void
cas_fwrite_oneline (CAS_LOG_FD * lfd, const char *str)
{
  while (*str)
    {
      size_t run = strcspn (str, "\r\n");

      if (run > 0)
	{
	  cas_fwrite (str, run, 1, lfd);
	  str += run;
	}
      if (*str)
	{
	  cas_fputc (' ', lfd);
	  str++;
	}
    }
}

static inline INT64
cas_ftell (CAS_LOG_FD * lfd)
{
  return lfd->file_buf_base + lfd->buf_used;	/* logical position */
}

static inline int
cas_fseek (CAS_LOG_FD * lfd, INT64 offset, int whence)
{
  INT64 new_len;

  assert (whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END);

  if (lfd->fd < 0)
    {
      return -1;
    }

  if (whence == SEEK_CUR || whence == SEEK_END)
    {
      /* the current position and the end are both the logical end of this log */
      offset += cas_ftell (lfd);
    }
  else if (whence != SEEK_SET)
    {
      return -1;
    }

  if (offset < lfd->file_open_pos)
    {
      /* Never truncate below the file size at open.  Reachable at runtime, so no assert. */
      return -1;
    }

  new_len = offset - lfd->file_buf_base;

  /* target inside the buffer, so shrink it */
  if (new_len >= 0)
    {
      if (new_len < lfd->buf_used)
	{
	  lfd->buf_used = (int) new_len;	/* shrink buf_used first so a terminating handler flushes nothing */
	  if (lfd->buf_flushed > new_len)
	    {
	      /* Discarded bytes may already be in the file.  Truncate them. */
	      (void) ftruncate_all (lfd->fd, offset);
	      lfd->buf_flushed = (int) new_len;
	    }
	}
    }
  /* target before the buffer start, so discard the whole buffer */
  else
    {
      lfd->buf_used = 0;	/* zero buf_used first so a terminating handler flushes nothing */
      lfd->buf_flushed = 0;
      lfd->file_buf_base = offset;
      (void) ftruncate_all (lfd->fd, offset);
    }

  return 0;
}

static CAS_LOG_FD *
cas_fopen (CAS_LOG_FD * lfd, const char *path, const char *mode)
{
  int fd;
  int flags;
  INT64 end;

  if (!sql_log_timer_created)
    {
      cas_log_timer_init ();
    }

  if (lfd->fd >= 0)
    {
      /* one CAS_LOG_FD per log, never reopened */
      assert (false);
      return lfd;
    }

  flags = O_WRONLY | O_CREAT;	/* not O_APPEND, or pwrite () would ignore its offset */
  if (mode != NULL && mode[0] == 'w')
    {
      flags |= O_TRUNC;
    }
  fd = open (path, flags, 0666);
  if (fd < 0)
    {
      return NULL;
    }
  end = lseek (fd, 0, SEEK_END);
  if (end < 0)
    {
      close (fd);
      return NULL;
    }

  lfd->file_open_pos = end;
  lfd->file_buf_base = end;
  lfd->buf_used = 0;
  lfd->buf_flushed = 0;
  lfd->fd = fd;			/* fd last: no valid fd is visible with stale offsets */

  return lfd;
}

#if defined (WINDOWS)
static FILE *
cas_fopen_and_lock (const char *path, const char *mode)
{
#define MAX_RETRY_COUNT 100
  int retry_count;
  FILE *result;

  retry_count = 0;

retry:
  result = fopen (path, mode);
  if (result != NULL)
    {
      if (lockf (fileno (result), F_TLOCK, 0) < 0)
	{
	  fclose (result);
	  if (retry_count < MAX_RETRY_COUNT)
	    {
	      SLEEP_MILISEC (0, 10);
	      retry_count++;
	      goto retry;
	    }
	  result = NULL;
	}
    }

  return result;
}
#endif

static int
cas_fclose (CAS_LOG_FD * lfd)
{
  int fd;

  if (lfd->fd < 0)
    {
      return 0;
    }

  (void) cas_fflush (lfd);
  if (lfd == &sql_log_fd && sql_log_timer_created)
    {
      /* disarm the flush timer */
      struct itimerspec off;

      memset (&off, 0, sizeof (off));
      timer_settime (sql_log_timer, 0, &off, NULL);
      sql_log_timer_armed = 0;
    }
  fd = lfd->fd;
  lfd->fd = -1;			/* fd first: the handler must not write to a closed descriptor */
  lfd->buf_used = 0;
  lfd->buf_flushed = 0;
  lfd->file_buf_base = 0;
  lfd->file_open_pos = 0;
  close (fd);
  return 0;
}

/*
 * cas_fprintf () - writes formatted output to the CAS log file.
 *   Used as the callback for password_fprintf (); stream points to CAS_LOG_FD.
 */
static int
cas_fprintf (void *stream, const char *format, ...)
{
  CAS_LOG_FD *lfd = (CAS_LOG_FD *) stream;
  int result;
  va_list ap;

  va_start (ap, format);

  if (strcmp (format, "%s") == 0)
    {
      /* password_fprintf () emits the SQL in newline-delimited "%s" pieces; a piece can be tens
       * of KB, so pass it through untouched instead of bouncing it off a fixed buffer */
      const char *str = va_arg (ap, const char *);
      size_t len = (str == NULL) ? 0 : strlen (str);

      if (len > 0)
	{
	  cas_fwrite (str, 1, len, lfd);
	}
      result = (int) len;
    }
  else if (strchr (format, '%') == NULL)
    {
      size_t len = strlen (format);	/* plain literal, e.g. the " " that replaces a newline */

      if (len > 0)
	{
	  cas_fwrite (format, 1, len, lfd);
	}
      result = (int) len;
    }
  else
    {
      /* a result longer than the local buffer is formatted again into an allocated one */
      char tmp[CAS_LOG_BUFFER_SIZE];
      char *buf = tmp;
      va_list ap_copy;
      int n;

      va_copy (ap_copy, ap);
      n = vsnprintf (tmp, sizeof (tmp), format, ap);
      if (n >= (int) sizeof (tmp))
	{
	  buf = (char *) MALLOC ((size_t) n + 1);
	  if (buf != NULL)
	    {
	      n = vsnprintf (buf, (size_t) n + 1, format, ap_copy);
	    }
	  else
	    {
	      buf = tmp;
	      n = (int) sizeof (tmp) - 1;
	    }
	}
      va_end (ap_copy);
      if (n > 0)
	{
	  cas_fwrite (buf, 1, (size_t) n, lfd);
	}
      if (buf != tmp)
	{
	  FREE_MEM (buf);
	}
      result = n;
    }

  va_end (ap);

  return result;
}

static inline int
cas_fputc (int c, CAS_LOG_FD * lfd)
{
  unsigned char b = (unsigned char) c;

  return (cas_fwrite (&b, 1, 1, lfd) == 1) ? (int) b : EOF;
}

static int
cas_unlink (const char *pathname)
{
  int result;

  result = unlink (pathname);

  return result;
}

static int
cas_rename (const char *oldpath, const char *newpath)
{
  int result;

  result = rename (oldpath, newpath);

  return result;
}

static int
cas_mkdir (const char *pathname, mode_t mode)
{
  int result;

  result = mkdir (pathname, mode);

  return result;
}

static void
access_log_backup (char *access_log_file, struct tm *ct)
{
  char cmd_buf[BUFSIZ];

  sprintf (cmd_buf, "%s.%04d%02d%02d%02d%02d%02d", access_log_file, ct->tm_year, ct->tm_mon + 1, ct->tm_mday,
	   ct->tm_hour, ct->tm_min, ct->tm_sec);
  rename (access_log_file, cmd_buf);
}

static const char *
get_access_log_type_string (ACCESS_LOG_TYPE type)
{
  switch (type)
    {
    case NEW_CONNECTION:
      return "NEW";
    case CLIENT_CHANGED:
      return "OLD";
    case ACL_REJECTED:
      return "REJECT";
    default:
      assert (0);
      break;
    }

  return "";
}
