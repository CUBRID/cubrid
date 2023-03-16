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
#include "porting_inline.hpp"

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

#define ERROR5(error, code, arg1, arg2, arg3, arg4, arg5) \
  do \
    { \
      error = code; \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 5, arg1, arg2, arg3, arg4, arg5); \
    } \
  while (0)

#define ERROR_SET_WARNING(error, code) \
  do \
    { \
      (error) = (code); \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 0); \
    } \
  while (0)

#define ERROR_SET_WARNING_1ARG(error, code, arg1) \
  do \
    { \
      (error) = (code); \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 1, (arg1)); \
    } \
  while (0)

#define ERROR_SET_WARNING_2ARGS(error, code, arg1, arg2) \
  do \
    { \
      (error) = (code); \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 2, (arg1), (arg2)); \
    } \
  while (0)

#define ERROR_SET_WARNING_3ARGS(error, code, arg1, arg2, arg3) \
  do \
    { \
      (error) = (code); \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 3, (arg1), (arg2), (arg3)); \
    } \
  while (0)

#define ERROR_SET_WARNING_4ARGS(error, code, arg1, arg2, arg3, arg4) \
  do \
    { \
      (error) = (code); \
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 4, (arg1), (arg2), (arg3), (arg4)); \
    } \
  while (0)

#define ERROR_SET_ERROR(error, code) \
  do \
    { \
      (error) = (code); \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 0); \
    } \
  while (0)

#define ERROR_SET_ERROR_1ARG(error, code, arg1) \
  do \
    { \
      (error) = (code); \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 1, (arg1)); \
    } \
  while (0)

#define ERROR_SET_ERROR_2ARGS(error, code, arg1, arg2) \
  do \
    { \
      (error) = (code); \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 2, (arg1), (arg2)); \
    } \
  while (0)

#define ERROR_SET_ERROR_3ARGS(error, code, arg1, arg2, arg3) \
  do \
    { \
      (error) = (code); \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 3, (arg1), (arg2), (arg3)); \
    } \
  while (0)

#define ERROR_SET_ERROR_4ARGS(error, code, arg1, arg2, arg3, arg4) \
  do \
    { \
      (error) = (code); \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 4, (arg1), (arg2), (arg3), (arg4)); \
    } \
  while (0)

#define ERROR_SET_ERROR_ONLY(code) \
  do \
    { \
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 0); \
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

STATIC_INLINE void
ASSERT_NOT_ERROR (const int not_error_code)
{
  assert (not_error_code != NO_ERROR);
  const int error_code = er_errid ();
  assert (error_code != not_error_code);
}

#ifdef __cplusplus

#if defined (SERVER_MODE) || !defined (WINDOWS)
// not dll linkage
#define CUBERR_MANAGER_DLL
#elif defined (CS_MODE) || defined (SA_MODE)
// Windows CS_MODE or SA_MODE - export
#define CUBERR_MANAGER_DLL __declspec( dllexport )
#else // Windows, not CS_MODE and not SA_MODE
// import
#define CUBERR_MANAGER_DLL __declspec( dllimport )
#endif // Windows, not CS_MODE and not SA_MODE

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
#endif // c++

#endif /* _ERROR_MANAGER_H_ */
