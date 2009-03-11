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
 * error_manager.h - Error module (both client & server)
 */

#ifndef _ERROR_MANAGER_H_
#define _ERROR_MANAGER_H_

#ident "$Id$"

#include "config.h"

#include <stdarg.h>
#if defined (SERVER_MODE)
#include "thread.h"
#if defined (WINDOWS)
#include <winsock2.h>
#include <windows.h>
#else /* WINDOWS */
#include <pthread.h>
#endif /* WINDOWS */
#endif /* SERVER_MODE */

#include "error_code.h"

#define ARG_FILE_LINE           __FILE__, __LINE__
#define NULL_LEVEL              0

enum er_exit_ask
{
  ER_NEVER_EXIT, ER_EXIT_ASK, ER_EXIT_DONT_ASK,
  ER_EXIT_DEFAULT = ER_NEVER_EXIT
};

enum er_severity
{
  ER_WARNING_SEVERITY, ER_ERROR_SEVERITY, ER_SYNTAX_ERROR_SEVERITY,
  ER_FATAL_ERROR_SEVERITY, ER_NOTIFICATION_SEVERITY,
  ER_MAX_SEVERITY = ER_NOTIFICATION_SEVERITY
};

enum er_level
{
  ER_LEVEL_SYSTEM, ER_LEVEL_APPLICATION
};

typedef void (*PTR_FNERLOG) (void);

/*
 * Definition of error message structure. One structure is defined for each
 * thread of execution. Note message areas are stored in the structure for
 * multi-threading purposes.
 */
typedef struct er_copy_area ER_COPY_AREA;
struct er_copy_area
{
  int err_id;			/* error identifier of the current message */
  int severity;			/* warning, error, FATAL error, etc... */
  int length_msg;		/* length of the message */
  char area[1];			/* actualy, more than one */
};

typedef union er_va_arg ER_VA_ARG;
union er_va_arg
{
  int i;			/* holders for the values that we actually */
  int l;			/* retrieve from the va_list. */
  void *p;
  double f;
  long double lf;
  const char *s;
};

typedef struct er_spec ER_SPEC;
struct er_spec
{
  char code;			/* what to retrieve from the va_list
				   int, long, double, long double or char */
  int width;			/* minimum width of field */
  char spec[10];		/* buffer to hold the actual sprintf code */
};

typedef struct er_fmt ER_FMT;
struct er_fmt
{
  int err_id;			/* The int associated with the msg */
  int age;			/* Timestamp for eviction priority */
  char *fmt;			/* A printf-style format for the msg */
  int fmt_length;		/* The strlen() of fmt */
  int must_free;		/* TRUE iff fmt must be free_and_initd */
  int nspecs;			/* The number of format specs in fmt */
  ER_SPEC spec_buf[4];		/* Array of format specs for args */
  int spec_top;			/* The capacity of spec */
  ER_SPEC *spec;		/* Pointer to real array; points to
				   spec_buf if nspecs < DIM(spec_buf) */
};

typedef struct er_fmt_cache ER_FMT_CACHE;
struct er_fmt_cache
{
  int timestamp;
  ER_FMT fmt[10];
#if 0				/* We use csect_enter instead of mutex_lock */
  MUTEX_T lock;
#endif
};

typedef struct er_msg ER_MSG;
struct er_msg
{
  int err_id;			/* Error identifier of the current message */
  int severity;			/* Warning, Error, FATAL Error, etc... */
  const char *file_name;	/* File where the error is set */
  int line_no;			/* Line in the file where the error is set */
  int msg_area_size;		/* Size of the message area */
  char *msg_area;		/* Pointer to message area */
  ER_MSG *stack;		/* Stack to previous error messages */
  ER_VA_ARG *args;		/* Array of va_list entries */
  int nargs;			/* Length of array */
};


extern const char *er_msglog_filename (void);
extern int er_init (const char *msglog_filename, int exit_ask);
#if defined (SERVER_MODE)
extern void er_final (bool do_global_final);
#else /* SERVER_MODE */
extern void er_final (void);
#endif /* SERVER_MODE */
extern PTR_FNERLOG er_fnerlog (int severity, PTR_FNERLOG new_fnlog);
extern void er_clear (void);
extern void er_set (int severity, const char *file_name, const int line_no,
		    int err_id, int num_args, ...);
extern void er_set_with_oserror (int severity, const char *file_name,
				 const int line_no, int err_id, int num_args,
				 ...);
typedef void (*er_log_handler_t) (unsigned int);
extern er_log_handler_t er_register_log_handler (er_log_handler_t f);
						 

extern int er_errid (void);
extern int er_severity (void);
extern int er_nlevels (void);
extern const char *er_file_line (int *line_no);
extern const char *er_msg (void);
extern void er_all (int *err_id, int *severity, int *nlevels,
		    int *line_no, const char **file_name, const char **msg);
extern void er_print (void);
extern void er_log_debug (const char *file_name, const int line_no,
			  const char *fmt, ...);
extern void *er_get_area_error (int *length);
extern void er_set_area_error (void *server_area);
extern int er_stack_push (void);
extern int er_stack_pop (void);
extern void er_stack_clear (void);
extern void er_stack_clearall (void);
extern void *db_default_malloc_handler (void *arg, const char *filename,
					int line_no, size_t size);
extern int er_event_restart (void);
extern void er_clearid (void);
extern void er_setid (int err_id);

#if !defined (CS_MODE)
extern void er_save_dbname (const char *dbname);
#endif /* !CS_MODE */

#endif /* _ERROR_MANAGER_H_ */
