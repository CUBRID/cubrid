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
#include <assert.h>
#if defined (SERVER_MODE)
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

/* Shorthand for simple warnings and errors */
#define ERROR0(error, code) \
  do \
    { \
      error = code; \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); \
    } \
  while (0)

#define ERROR1(error, code, arg1) \
  do \
    { \
      error = code; \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 1, arg1); \
    } \
  while (0)

#define ERROR2(error, code, arg1, arg2) \
  do \
    { \
      error = code; \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 2, arg1, arg2); \
    } \
  while (0)

#define ERROR3(error, code, arg1, arg2, arg3) \
  do \
    { \
      error = code; \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 3, arg1, arg2, arg3); \
    } \
  while (0)

#define ERROR4(error, code, arg1, arg2, arg3, arg4) \
  do \
    { \
      error = code; \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 4, arg1, arg2, arg3, arg4); \
    } \
  while (0)

/*
 * custom assert macro for release mode
 */
#if defined(NDEBUG)
#define STRINGIZE(s) #s
#define assert_release(e) \
  ((e) ? (void) 0  : er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_FAILED_ASSERTION, 1, STRINGIZE (e)))
#else
#define assert_release(e) assert(e)
#endif

enum er_exit_ask
{
  ER_NEVER_EXIT = 0,
  ER_EXIT_ASK = 1,
  ER_EXIT_DONT_ASK = 2,
  ER_ABORT = 3,
  ER_EXIT_DEFAULT = ER_NEVER_EXIT
};

enum er_print_option
{
  ER_DO_NOT_PRINT = 0,
  ER_PRINT_TO_CONSOLE = 1
};

/* do not change the order of this enumeration; see er_Fnlog[] */
enum er_severity
{
  ER_FATAL_ERROR_SEVERITY, ER_ERROR_SEVERITY,
  ER_SYNTAX_ERROR_SEVERITY, ER_WARNING_SEVERITY,
  ER_NOTIFICATION_SEVERITY,
  ER_MAX_SEVERITY = ER_NOTIFICATION_SEVERITY
};

enum er_level
{
  ER_LEVEL_SYSTEM, ER_LEVEL_APPLICATION
};

typedef enum er_final_code
{
  ER_THREAD_FINAL = 0,
  ER_ALL_FINAL = 1
} ER_FINAL_CODE;

typedef void (*PTR_FNERLOG) (int err_id);

#define ER_IS_LOCK_TIMEOUT_ERROR(err) \
  ((err) == ER_LK_UNILATERALLY_ABORTED \
   || (err) == ER_LK_OBJECT_TIMEOUT_SIMPLE_MSG \
   || (err) == ER_LK_OBJECT_TIMEOUT_CLASS_MSG \
   || (err) == ER_LK_OBJECT_TIMEOUT_CLASSOF_MSG \
   || (err) == ER_LK_OBJECT_DL_TIMEOUT_SIMPLE_MSG \
   || (err) == ER_LK_OBJECT_DL_TIMEOUT_CLASS_MSG \
   || (err) == ER_LK_OBJECT_DL_TIMEOUT_CLASSOF_MSG)

#define ER_IS_ABORTED_DUE_TO_DEADLOCK(err) \
  ((err) == ER_LK_UNILATERALLY_ABORTED \
   || (err) == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED)

#define ER_IS_SERVER_DOWN_ERROR(err) \
  ((err) == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED \
   || (err) == ER_NET_SERVER_CRASHED \
   || (err) == ER_OBJ_NO_CONNECT \
   || (err) == ER_BO_CONNECT_FAILED)

/* Macros to assert that error is set. */
#define ASSERT_ERROR() \
  assert (er_errid () != NO_ERROR)
/* This macro will also make sure the error_code to be returned is not
 * NO_ERROR.
 */
#define ASSERT_ERROR_AND_SET(error_code) \
  do \
    { \
      error_code = er_errid (); \
      if (error_code == NO_ERROR) \
	{ \
	  /* Error should be set */ \
	  assert (false); \
	  error_code = ER_FAILED; \
	} \
    } while (false)
/* Macro that checks no error was set. */
#define ASSERT_NO_ERROR() \
  assert (er_errid () == NO_ERROR);

#ifdef __cplusplus
extern "C"
{
#endif

  extern const char *er_get_msglog_filename (void);
  extern int er_init (const char *msglog_filename, int exit_ask);
  extern bool er_is_initialized (void);
  extern void er_set_print_property (int print_console);
  extern void er_final (ER_FINAL_CODE do_global_final);
  extern void er_clear (void);
  extern void er_set (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...);
  extern void er_set_with_file (int severity, const char *file_name, const int line_no, int err_id, FILE * fp,
				int num_args, ...);
  extern void er_set_with_oserror (int severity, const char *file_name, const int line_no, int err_id, int num_args,
				   ...);
  typedef void (*er_log_handler_t) (unsigned int);
  extern er_log_handler_t er_register_log_handler (er_log_handler_t f);

#if !defined (WINDOWS) && defined (SERVER_MODE)
  extern void er_file_create_link_to_current_log_file (const char *er_file_path, const char *suffix);
#endif				/* !WINDOWS && SERVER_MODE */

  extern int er_errid (void);
  extern int er_errid_if_has_error (void);
  extern int er_get_severity (void);
  extern const char *er_msg (void);
  extern void er_all (int *err_id, int *severity, int *nlevels, int *line_no, const char **file_name, const char **msg);

  extern void _er_log_debug (const char *file_name, const int line_no, const char *fmt, ...);
#define er_log_debug(...) if (prm_get_bool_value (PRM_ID_ER_LOG_DEBUG)) _er_log_debug(__VA_ARGS__)

  extern char *er_get_ermsg_from_area_error (char *buffer);
  extern char *er_get_area_error (char *buffer, int *length);
  extern int er_set_area_error (char *server_area);
  extern void er_stack_push (void);
  extern void er_stack_push_if_exists (void);
  extern void er_stack_pop (void);
  extern void er_stack_pop_and_keep_error (void);
  extern void er_restore_last_error (void);
  extern void er_stack_clearall (void);
  extern void *db_default_malloc_handler (void *arg, const char *filename, int line_no, size_t size);
  extern int er_event_restart (void);
  extern void er_clearid (void);
  extern void er_setid (int err_id);

  extern bool er_has_error (void);
  extern void er_print_callstack (const char *file_name, const int line_no, const char *fmt, ...);
#if defined (CS_MODE)
  extern void er_set_ignore_uninit (bool ignore);
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#if defined (SERVER_MODE) || !defined (WINDOWS)
// not dll linkage
#define CUBERR_MANAGER_DLL
#elif defined (CS_MODE) || defined (SA_MODE)
// Windows CS_MODE or SA_MODE - export
#define CUBERR_MANAGER_DLL __declspec( dllexport )
#else				// Windows, not CS_MODE and not SA_MODE
// import
#define CUBERR_MANAGER_DLL __declspec( dllimport )
#endif				// Windows, not CS_MODE and not SA_MODE

/* *INDENT-OFF* */
namespace cuberr
{
  class CUBERR_MANAGER_DLL manager
  {
  public:
    manager (const char * msg_file, er_exit_ask exit_arg);
    ~manager (void);
  };
} // namespace cuberr
/* *INDENT-ON* */

// to use in C units instead of er_init; makes sure that er_final is called before exiting scope
// NOTE - cuberr_manager variable is created. it may cause naming conflicts
// NOTE - if used after jumps, expect "crosses initialization" errors
#define ER_SAFE_INIT(msg_file, exit_arg) cuberr::manager cuberr_manager (msg_file, exit_arg)
#endif				// c++

#endif				/* _ERROR_MANAGER_H_ */
