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
 * error_manager.c - Error module (both client & server)
 */

#ident "$Id$"

// fix porting issue _lseek64
#if defined (WINDOWS)
#include <io.h>
#endif /* WINDOWS */

#include "error_manager.h"

#include "chartype.h"
#include "error_context.hpp"
#include "environment_variable.h"
#if defined (SERVER_MODE)
#include "log_impl.h"
#endif /* !SERVER_MODE */
#include "memory_alloc.h"
#include "message_catalog.h"
#include "object_representation.h"
#if !defined (WINDOWS)
#include "release_string.h"
#endif // not WINDOWS
#include "system_parameter.h"
#include "stack_dump.h"
#if !defined (SERVER_MODE)
#include "transaction_cl.h"
#endif /* !SERVER_MODE */

#if !defined (WINDOWS) && defined (SERVER_MODE)
#include "boot_sr.h"
#endif /* !WINDWS && SERVER_MODE */

// c++ headers
#include <cassert>
#include <cstddef>
#include <cstring>

// c headers
#include <errno.h>
#if defined (SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */
#if defined (WINDOWS)
#include <process.h>
#endif // WINDOWS
#if defined (SERVER_MODE) && !defined (WINDOWS)
#include <pthread.h>
#endif /* SERVER_MODE && !WINDOWS */
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(CS_MODE)
#include <sys/stat.h>
#endif /* !CS_MODE */
#if !defined (WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */
#if !defined(CS_MODE)
#include <sys/types.h>
#endif /* !CS_MODE */
#if !defined (WINDOWS)
#include <syslog.h>
#endif /* !WINDOWS */
#include <time.h>
#if !defined (WINDOWS)
#include <unistd.h>
#endif /* !WINDOWS */

#include <mutex>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

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
  char area[1];			/* actually, more than one */
};

typedef struct er_spec ER_SPEC;
struct er_spec
{
  int width;			/* minimum width of field */
  char code;			/* what to retrieve from the va_list int, long, double, long double or char */
  char spec[10];		/* buffer to hold the actual sprintf code */
};

typedef struct er_fmt ER_FMT;
struct er_fmt
{
  int err_id;			/* The int associated with the msg */
  char *fmt;			/* A printf-style format for the msg */
  ER_SPEC *spec;		/* Pointer to real array; points to spec_buf if nspecs < DIM(spec_buf) */
  int fmt_length;		/* The strlen() of fmt */
  int must_free;		/* TRUE if fmt must be free_and_initd */
  int nspecs;			/* The number of format specs in fmt */
  int spec_top;			/* The capacity of spec */
  ER_SPEC spec_buf[16];		/* Array of format specs for args */
};

using namespace cuberr;		// until we add everything under cuberr

#if defined(WINDOWS)
#define LOG_ALERT 0
static int
syslog (long priority, const char *message, ...)
{
  return 0;
}
#endif

// *INDENT-OFF*
std::mutex er_Log_file_mutex;
std::mutex er_Message_cache_mutex;
// *INDENT-ON*

/*
 * These are done via complied constants rather than the message catalog, because they must be available if the message
 * catalog is not available.
 */
static const char *er_severity_string[] = { "FATAL ERROR", "ERROR", "SYNTAX ERROR", "WARNING", "NOTIFICATION" };

static const char *er_unknown_severity = "Unknown severity level";

#define ER_SEVERITY_STRING(severity) \
  (((severity) >= ER_FATAL_ERROR_SEVERITY && (severity) <= ER_MAX_SEVERITY) \
   ? er_severity_string[severity] : er_unknown_severity)

#define ER_ERROR_WARNING_STRING(severity) \
  (((severity) >= ER_FATAL_ERROR_SEVERITY && (severity) < ER_WARNING_SEVERITY) \
   ? er_severity_string[ER_ERROR_SEVERITY] : "")


/*
 * Message sets within the er.msg catalog.  Set #1 comprises the system
 * error messages proper, while set #2 comprises the specific messages
 * that the er_ module uses itself.
 */
#define ER_MSG_SET		1
#define ER_INTERNAL_MSG_SET	2

#define PRM_ER_MSGLEVEL         0

#define ER_MALLOC(size)	er_malloc_helper ((size), __FILE__, __LINE__)

#define SPEC_CODE_LONGLONG ((char)0x88)
#define SPEC_CODE_SIZE_T ((char)0x89)

#define MAX_LINE 4096

/*
 * Default text for messages. These messages are used only when we can't
 * locate a message catalog from which to read their counterparts. There
 * is no need to localize these messages.
 *
 * Make sure that this enumeration remains consistent with the following
 * array of default messages and with the message entries in every
 * message catalog.  If you add a new code, you need to add a new entry
 * to default_internal_msg[] and to every catalog.
 */
enum er_msg_no
{
  /* skip msg no 0 */
  ER_ER_HEADER = 1,
  ER_ER_MISSING_MSG,
  ER_ER_OUT_OF_MEMORY,
  ER_ER_NO_CATALOG,
  ER_ER_LOG_MISSING_MSG,
  ER_ER_EXIT,
  ER_ER_ASK,
  ER_ER_UNKNOWN_FILE,
  ER_ER_SUBSTITUTE_MSG,
  ER_LOG_ASK_VALUE,
  ER_LOG_MSGLOG_WARNING,
  ER_LOG_SUSPECT_FMT,
  ER_LOG_UNKNOWN_CODE,
  ER_LOG_WRAPAROUND,
  ER_LOG_MSG_WRAPPER,
  ER_LOG_SYSLOG_WRAPPER,
  ER_LOG_MSG_WRAPPER_D,
  ER_LOG_SYSLOG_WRAPPER_D,
  ER_LOG_LAST_MSG,
  ER_LOG_DEBUG_NOTIFY,
  ER_STOP_MAIL_SUBJECT,
  ER_STOP_MAIL_BODY,
  ER_STOP_SYSLOG,
  ER_EVENT_HANDLER
};

static const char *er_Builtin_msg[] = {
  /* skip msg no 0 */
  NULL,
  /* ER_ER_HEADER */
  "Error in error subsystem (line %d): ",
  /*
   * ER_ER_MISSING_MSG
   *
   * It's important that this message have no conversion specs, because
   * it is sometimes used to replace messages in which we have no
   * confidence (e.g., because they're proven not to have the same
   * number of conversion specs as arguments they are given).  In those
   * cases we can't rely on the arguments at all, and we must use a
   * format that won't try to do anything with va_arg().
   */
  "No error message available.",
  /* ER_ER_OUT_OF_MEMORY */
  "Can't allocate %d bytes.",
  /* ER_ER_NO_CATALOG */
  "Can't find message catalog files.",
  /* ER_ER_LOG_MISSING_MSG */
  "Missing message for error code %d.",
  /* ER_ER_EXIT */
  "\n... ABORT/EXIT IMMEDIATELY ...\n",
  /* ER_ER_ASK */
  "Do you want to exit? 1/0 ",
  /* ER_ER_UNKNOWN_FILE */
  "Unknown file",
  /* ER_ER_SUBSTITUTE_MSG */
  "No message available; original message format in error.",
  /* ER_LOG_ASK_VALUE */
  "er_init: *** Incorrect exit_ask value = %d; will assume %s instead. ***\n",
  /* ER_LOG_MSGLOG_WARNING */
  "er_init: *** WARNING: Unable to open message log \"%s\"; will assume stderr instead. ***\n",
  /* ER_LOG_SUSPECT_FMT */
  "er_study_fmt: suspect message for error code %d.",
  /* ER_LOG_UNKNOWN_CODE */
  "er_estimate_size: unknown conversion code (%d).",
  /* ER_LOG_WRAPAROUND */
  "\n\n*** Message log wraparound. Messages will continue at top of the file. ***\n\n",
  /* ER_LOG_MSG_WRAPPER */
  "\nTime: %s - %s *** %s CODE = %d, Tran = %d%s\n%s\n",
  /* ER_LOG_SYSLOG_WRAPPER */
  "CUBRID (pid: %d) *** %s *** %s CODE = %d, Tran = %d, %s",
  /* ER_LOG_MSG_WRAPPER_D */
  "\nTime: %s - %s *** file %s, line %d %s CODE = %d, Tran = %d%s\n%s\n",
  /* ER_LOG_SYSLOG_WRAPPER_D */
  "CUBRID (pid: %d) *** %s *** file %s, line %d, %s CODE = %d, Tran = %d. %s",
  /* ER_LOG_LAST_MSG */
  "\n*** The previous error message is the last one. ***\n\n",
  /* ER_LOG_DEBUG_NOTIFY */
  "\nTime: %s - DEBUG *** file %s, line %d\n",
  /* ER_STOP_MAIL_SUBJECT */
  "Mail -s \"CUBRID has been stopped\" ",
  /* ER_STOP_MAIL_BODY */
  "--->>>\n%s has been stopped at your request when the following error was set:\nerrid = %d, %s\nUser: %s, pid: %d, host: %s, time: %s<<<---",
  /* ER_STOP_SYSLOG */
  "%s has been stopped on errid = %d. User: %s, pid: %d",
  /* ER_EVENT_HANDLER */
  "er_init: cannot install event handler \"%s\""
};

static char *er_Cached_msg[sizeof (er_Builtin_msg) / sizeof (const char *)];
static bool er_Is_cached_msg = false;

/* Error log message file related */
#define ER_MSG_LOG_FILE_SUFFIX ".err"
static char er_Msglog_filename_buff[PATH_MAX];
static const char *er_Msglog_filename = NULL;
static FILE *er_Msglog_fh = NULL;

/* Error log message file related */
#define ER_ACCESS_LOG_FILE_SUFFIX ".access"
static char er_Accesslog_filename_buff[PATH_MAX];
static const char *er_Accesslog_filename = NULL;
static FILE *er_Accesslog_fh = NULL;


static ER_FMT er_Fmt_list[(-ER_LAST_ERROR) + 1];
static int er_Fmt_msg_fail_count = -ER_LAST_ERROR;
static int er_Errid_not_initialized = 0;
#if !defined (SERVER_MODE)
static er_log_handler_t er_Handler = NULL;
#endif /* !SERVER_MODE */
static unsigned int er_Eid = 0;

/* Event handler related */
static FILE *er_Event_pipe = NULL;
static bool er_Event_started = false;
static jmp_buf er_Event_jmp_buf;
static SIGNAL_HANDLER_FUNCTION saved_Sig_handler;

/* Other supporting global variables */
static bool er_Logfile_opened = false;
static bool er_Hasalready_initiated = false;
static bool er_Has_sticky_init = false;
static bool er_Isa_null_device = false;
static int er_Exit_ask = ER_EXIT_DEFAULT;
static int er_Print_to_console = ER_DO_NOT_PRINT;
/* TODO : remove this when applylogdb and copylogdb are removed
 * multithreaded client processes which start+end database (and error module) in a loop, may need to log errors on
 * other threads (while error module is stopped); this flag prevents assertion failure of error module initialization
*  for such case */
#if defined (CS_MODE)
static bool er_Ignore_uninit = false;
#endif

#if !defined (SERVER_MODE)
// requires own context
static context *er_Singleton_context_p;
#endif // not SERVER_MODE

static void er_event_sigpipe_handler (int sig);
static void er_event (void);
static int er_event_init (void);
static void er_event_final (void);

#if defined (SERVER_MODE) || defined (SA_MODE)
static void er_set_access_log_filename (void);
#endif /* SERVER_MODE || SA_MODE */

static FILE *er_file_open (const char *path);
static bool er_file_isa_null_device (const char *path);
static FILE *er_file_backup (FILE * fp, const char *path);

static void er_call_stack_dump_on_error (int severity, int err_id);
static void er_notify_event_on_error (int err_id);
static int er_set_internal (int severity, const char *file_name, const int line_no, int err_id, int num_args,
			    bool include_os_error, FILE * fp, va_list * ap_ptr);
static void er_log (int err_id);
static void er_stop_on_error (void);

static int er_study_spec (const char *conversion_spec, char *simple_spec, int *position, int *width, int *va_class);
static void er_study_fmt (ER_FMT * fmt);
static size_t er_estimate_size (ER_FMT * fmt, va_list * ap);
static ER_FMT *er_find_fmt (int err_id, int num_args);
static void er_init_fmt (ER_FMT * fmt);
static ER_FMT *er_create_fmt_msg (ER_FMT * fmt, int err_id, const char *msg);
static void er_clear_fmt (ER_FMT * fmt);
static void er_internal_msg (ER_FMT * fmt, int code, int msg_num);
static void *er_malloc_helper (std::size_t size, const char *file, int line);
static void er_emergency (const char *file, int line, const char *fmt, ...);
static int er_vsprintf (er_message * er_entry_p, ER_FMT * fmt, va_list * ap);

static int er_call_stack_init (void);
static int er_fname_free (const void *key, void *data, void *args);
static void er_call_stack_final (void);

static void _er_log_debug_internal (const char *file_name, const int line_no, const char *fmt, va_list * ap);

static bool er_is_error_severity (er_severity severity);

/* vector of functions to call when an error is set */
static PTR_FNERLOG er_Fnlog[ER_MAX_SEVERITY + 1] = {
  er_log,			/* ER_FATAL_ERROR_SEVERITY */
  er_log,			/* ER_ERROR_SEVERITY */
  er_log,			/* ER_SYNTAX_ERROR_SEVERITY */
  er_log,			/* ER_WARNING_SEVERITY */
  er_log			/* ER_NOTIFICATION_SEVERITY */
};

/*
 * er_get_msglog_filename - Find the error message log file name
 *   return: log file name
 */
const char *
er_get_msglog_filename (void)
{
  return er_Msglog_filename;
}

/*
 * er_event_sigpipe_handler
 */
static void
er_event_sigpipe_handler (int sig)
{
  _longjmp (er_Event_jmp_buf, 1);
}

/*
 * er_event - Notify a error event to the handler
 *   return: none
 */
static void
er_event (void)
{
  int err_id, severity, nlevels, line_no;
  const char *file_name, *msg;

  if (er_Event_pipe == NULL || er_Event_started == false)
    {
      return;
    }

  /* Get the most detailed error message available */
  er_all (&err_id, &severity, &nlevels, &line_no, &file_name, &msg);

#if !defined(WINDOWS)
  saved_Sig_handler = os_set_signal_handler (SIGPIPE, er_event_sigpipe_handler);
#endif /* not WINDOWS */
  if (_setjmp (er_Event_jmp_buf) == 0)
    {
      fprintf (er_Event_pipe, "%d %s %s\n", err_id, ER_SEVERITY_STRING (severity), msg);
      fflush (er_Event_pipe);
    }
  else
    {
      er_Event_started = false;
      if (er_Hasalready_initiated)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_BROKEN_PIPE, 1, prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
      if (er_Event_pipe != NULL)
	{
	  fclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
    }
#if !defined(WINDOWS)
  os_set_signal_handler (SIGPIPE, saved_Sig_handler);
#endif /* not WINDOWS */
}

/*
 * er_evnet_init
 */
static int
er_event_init (void)
{
  volatile int error = NO_ERROR;
  const char *msg;

#if !defined(WINDOWS)
  saved_Sig_handler = os_set_signal_handler (SIGPIPE, er_event_sigpipe_handler);
#endif /* not WINDOWS */
  if (_setjmp (er_Event_jmp_buf) == 0)
    {
      er_Event_started = false;
      if (er_Event_pipe != NULL)
	{
	  if (er_Hasalready_initiated)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_EV_STOPPED, 0);
	    }
	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_EV_STOPPED);
	  fprintf (er_Event_pipe, "%d %s %s", ER_EV_STOPPED, ER_SEVERITY_STRING (ER_NOTIFICATION_SEVERITY),
		   (msg ? msg : "?"));

	  fflush (er_Event_pipe);
	  pclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}

      er_Event_pipe = popen (prm_get_string_value (PRM_ID_EVENT_HANDLER), "w");
      if (er_Event_pipe != NULL)
	{
	  if (er_Hasalready_initiated)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_EV_STARTED, 0);
	    }
	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_EV_STARTED);
	  fprintf (er_Event_pipe, "%d %s %s", ER_EV_STARTED, ER_SEVERITY_STRING (ER_NOTIFICATION_SEVERITY),
		   (msg ? msg : "?"));

	  fflush (er_Event_pipe);
	  er_Event_started = true;
	}
      else
	{
	  if (er_Hasalready_initiated)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_CONNECT_HANDLER, 1,
		      prm_get_string_value (PRM_ID_EVENT_HANDLER));
	    }
	  error = ER_EV_CONNECT_HANDLER;
	}
    }
  else
    {
      if (er_Hasalready_initiated)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_BROKEN_PIPE, 1, prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
      if (er_Event_pipe != NULL)
	{
	  fclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
      error = ER_EV_BROKEN_PIPE;
    }
#if !defined(WINDOWS)
  os_set_signal_handler (SIGPIPE, saved_Sig_handler);
#endif /* not WINDOWS */

  return error;
}

/*
 * er_event_final
 */
static void
er_event_final (void)
{
  const char *msg;

#if !defined(WINDOWS)
  saved_Sig_handler = os_set_signal_handler (SIGPIPE, er_event_sigpipe_handler);
#endif /* not WINDOWS */
  if (_setjmp (er_Event_jmp_buf) == 0)
    {
      er_Event_started = false;
      if (er_Event_pipe != NULL)
	{
	  if (er_Hasalready_initiated)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_EV_STOPPED, 0);
	    }
	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_EV_STOPPED);
	  fprintf (er_Event_pipe, "%d %s %s", ER_EV_STOPPED, ER_SEVERITY_STRING (ER_NOTIFICATION_SEVERITY),
		   (msg ? msg : "?"));

	  fflush (er_Event_pipe);
	  pclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
    }
  else
    {
      if (er_Hasalready_initiated)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_BROKEN_PIPE, 1, prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
      if (er_Event_pipe != NULL)
	{
	  fclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
    }
#if !defined(WINDOWS)
  os_set_signal_handler (SIGPIPE, saved_Sig_handler);
#endif /* not WINDOWS */
}

/*
 * er_call_stack_init -
 *   return: error code
 */
static int
er_call_stack_init (void)
{
#if defined(LINUX)
  fname_table = mht_create (0, 100, mht_5strhash, mht_compare_strings_are_equal);
  if (fname_table == NULL)
    {
      return ER_FAILED;
    }
#endif

  return NO_ERROR;
}

/*
 * er_call_stack_final -
 *   return: none
 */
static void
er_call_stack_final (void)
{
#if defined(LINUX)
  if (fname_table == NULL)
    {
      return;
    }

  mht_map (fname_table, er_fname_free, NULL);
  mht_destroy (fname_table);
  fname_table = NULL;
#endif
}

/*
 * er_fname_free -
 *   return: error code
 */
static int
er_fname_free (const void *key, void *data, void *args)
{
  free ((void *) key);
  free (data);

  return NO_ERROR;
}

#if defined (SERVER_MODE) || defined (SA_MODE)
/*
 * er_set_access_log_filename - set er_Accesslog_filename_buff, er_Accesslog_filename
 *   return:
 */
static void
er_set_access_log_filename (void)
{
  char tmp[PATH_MAX];
  std::size_t len, suffix_len;

  if (er_Msglog_filename == NULL)
    {
      er_Accesslog_filename = NULL;
      return;
    }

  len = std::strlen (er_Msglog_filename);
  suffix_len = std::strlen (ER_MSG_LOG_FILE_SUFFIX);

  if (len < suffix_len || strncmp (&er_Msglog_filename[len - suffix_len], ER_MSG_LOG_FILE_SUFFIX, suffix_len) != 0)
    {
      if (snprintf (er_Accesslog_filename_buff, PATH_MAX, "%s%s", er_Msglog_filename, ER_ACCESS_LOG_FILE_SUFFIX) < 0)
	{
	  er_Accesslog_filename = NULL;
	  return;
	}
      /* ex) server.log => server.log.access */
    }
  else
    {
      strncpy (tmp, er_Msglog_filename, PATH_MAX);
      tmp[len - suffix_len] = '\0';
      if (snprintf (er_Accesslog_filename_buff, PATH_MAX - 1, "%s%s", tmp, ER_ACCESS_LOG_FILE_SUFFIX) < 0)
	{
	  er_Accesslog_filename = NULL;
	  return;
	}
      /* ex) server_log.err => server_log.access */
    }

  er_Accesslog_filename = er_Accesslog_filename_buff;

  /* in case of strlen(er_Msglog_filename) > PATH_MAX - 7 */
  if (strnlen (er_Accesslog_filename_buff, PATH_MAX) >= PATH_MAX)
    {
      er_Accesslog_filename = NULL;
    }
}
#endif /* SERVER_MODE || SA_MODE */

/*
 * er_init - Initialize parameters for message module
 *   return: none
 *   msglog_filename(in): name of message log file
 *   exit_ask(in): define behavior when a sever error is found
 *
 * Note: This function initializes parameters that define the behavior
 *       of the message module (i.e., logging file , and fatal error
 *       exit condition). If the value of msglog_filename is NULL,
 *       error messages are logged/sent to PRM_ER_LOG_FILE. If that
 *       is null, then messages go to stderr. If msglog_filename
 *       is equal to /dev/null, error messages are not logged.
 */
int
er_init (const char *msglog_filename, int exit_ask)
{
  int i;
  const char *msg;
  MSG_CATD msg_catd;

  if (er_Has_sticky_init)
    {
      assert (er_Hasalready_initiated);
      // do not reinitialize
      return NO_ERROR;
    }

  if (er_Hasalready_initiated)
    {
      er_final (ER_ALL_FINAL);
    }

  for (i = 0; i < (int) DIM (er_Builtin_msg); i++)
    {
      if (er_Cached_msg[i] && er_Cached_msg[i] != er_Builtin_msg[i])
	{
	  free_and_init (er_Cached_msg[i]);
	}
      er_Cached_msg[i] = (char *) er_Builtin_msg[i];
    }

  assert (DIM (er_Fmt_list) > abs (ER_LAST_ERROR));

  for (i = 0; i < abs (ER_LAST_ERROR); i++)
    {
      er_init_fmt (&er_Fmt_list[i]);
    }

  msg_catd = msgcat_get_descriptor (MSGCAT_CATALOG_CUBRID);
  if (msg_catd != NULL)
    {
      er_Fmt_msg_fail_count = 0;
      for (i = 2; i < abs (ER_LAST_ERROR); i++)
	{
	  msg = msgcat_gets (msg_catd, MSGCAT_SET_ERROR, i, NULL);
	  if (msg == NULL || msg[0] == '\0')
	    {
	      er_Fmt_msg_fail_count++;
	      continue;
	    }
	  else
	    {
	      if (er_create_fmt_msg (&er_Fmt_list[i], -i, msg) == NULL)
		{
		  er_Fmt_msg_fail_count++;
		}
	    }
	}
    }
  else
    {
      er_Fmt_msg_fail_count = abs (ER_LAST_ERROR) - 2;
    }

  er_Hasalready_initiated = true;
  // quick-fix for reloading error manager issues. the broker generated cas client need to reload error manager with
  // update file name parameter. however, csql and other utilities should keep their initial file name.
  // we need to treat this case properly. for now, I just consider "sticky" initialization when a specific file name
  // is provided as argument. error manager is not reloaded after.
  // for cas case, error manager is first initialized without a specific filename, so it won't be "sticky". It is
  // reloaded during boot_register_client.
  er_Has_sticky_init = (msglog_filename != NULL);

#if !defined (SERVER_MODE)
  // we need to register a context
  er_Singleton_context_p = new context ();
  er_Singleton_context_p->register_thread_local ();
  // SERVER_MODE should have already one
#endif // not SERVER_MODE

  switch (exit_ask)
    {
    case ER_EXIT_ASK:
    case ER_EXIT_DONT_ASK:
    case ER_NEVER_EXIT:
    case ER_ABORT:
      er_Exit_ask = exit_ask;
      break;

    default:
      er_Exit_ask = ER_EXIT_DEFAULT;
      er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_ASK_VALUE], exit_ask,
		    ((er_Exit_ask == ER_EXIT_ASK) ? "ER_EXIT_ASK" : "ER_NEVER_EXIT"));
      break;
    }

  /*
   * Install event handler
   */
  if (prm_get_string_value (PRM_ID_EVENT_HANDLER) != NULL && *prm_get_string_value (PRM_ID_EVENT_HANDLER) != '\0')
    {
      if (er_event_init () != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_EVENT_HANDLER], prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
    }

  /*
   * Remember the name of the message log file
   */
  if (msglog_filename == NULL)
    {
      if (prm_get_string_value (PRM_ID_ER_LOG_FILE))
	{
	  msglog_filename = prm_get_string_value (PRM_ID_ER_LOG_FILE);
	}
    }

  // *INDENT-OFF*
  // protect log file mutex
  std::unique_lock<std::mutex> log_file_lock (er_Log_file_mutex); // mutex is released on destructor
  // *INDENT-ON*

  if (msglog_filename != NULL)
    {
      if (IS_ABS_PATH (msglog_filename) || msglog_filename[0] == PATH_CURRENT)
	{
	  strncpy (er_Msglog_filename_buff, msglog_filename, PATH_MAX - 1);
	}
      else
	{
	  envvar_logdir_file (er_Msglog_filename_buff, PATH_MAX, msglog_filename);
	}

      er_Msglog_filename_buff[PATH_MAX - 1] = '\0';
      er_Msglog_filename = er_Msglog_filename_buff;
    }
  else
    {
      er_Msglog_filename = NULL;
    }

#if defined (SERVER_MODE) || defined (SA_MODE)
  er_set_access_log_filename ();
#endif /* SERVER_MODE || SA_MODE */

  /* Define message log file */
  if (er_Logfile_opened == false)
    {
      if (er_Msglog_filename)
	{
	  er_Isa_null_device = er_file_isa_null_device (er_Msglog_filename);

	  if (er_Isa_null_device)
	    {
	      er_Msglog_fh = er_file_open (er_Msglog_filename);
	    }
	  else
	    {
	      char path[PATH_MAX];
	      const char *er_file_path;

	      if (prm_get_bool_value (PRM_ID_ER_PRODUCTION_MODE))
		{
		  er_file_path = er_Msglog_filename;
		}
	      else
		{
		  /* want to err on the side of doing production style error logs because this may be getting set at some
		   * naive customer site. */
		  sprintf (path, "%s.%d", er_Msglog_filename, getpid ());
		  er_file_path = path;
		}

	      er_Msglog_fh = er_file_open (er_file_path);

#if !defined (WINDOWS) && defined (SERVER_MODE)
	      if (er_Msglog_fh != NULL)
		{
		  er_file_create_link_to_current_log_file (er_file_path, ER_MSG_LOG_FILE_SUFFIX);
		}
#endif /* !WINDOWS && SERVER_MODE */
	    }

	  if (er_Msglog_fh == NULL)
	    {
	      er_Msglog_fh = stderr;
	      er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_MSGLOG_WARNING], er_Msglog_filename);
	    }
	}
      else
	{
	  er_Msglog_fh = stderr;
	}

      if (er_Accesslog_filename)
	{
	  er_Isa_null_device = er_file_isa_null_device (er_Accesslog_filename);

	  if (er_Isa_null_device)
	    {
	      er_Accesslog_fh = er_file_open (er_Accesslog_filename);
	    }
	  else
	    {
	      char path[PATH_MAX];
	      const char *ac_file_path;

	      if (prm_get_bool_value (PRM_ID_ER_PRODUCTION_MODE))
		{
		  ac_file_path = er_Accesslog_filename;
		}
	      else
		{
		  /* want to err on the side of doing production style error logs because this may be getting set at some
		   * naive customer site. */
		  sprintf (path, "%s.%d", er_Accesslog_filename, getpid ());
		  ac_file_path = path;
		}

	      er_Accesslog_fh = er_file_open (ac_file_path);

#if !defined (WINDOWS) && defined (SERVER_MODE)
	      if (er_Accesslog_fh != NULL)
		{
		  er_file_create_link_to_current_log_file (ac_file_path, ER_ACCESS_LOG_FILE_SUFFIX);
		}
#endif /* !WINDOWS && SERVER_MODE */
	    }

	  if (er_Accesslog_fh == NULL)
	    {
	      er_Accesslog_fh = stderr;
	      er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_MSGLOG_WARNING], er_Accesslog_filename);
	    }
	}
      else
	{
	  er_Accesslog_fh = stderr;
	}

      er_Logfile_opened = true;
    }

  log_file_lock.unlock ();

  /*
   * Message catalog may be initialized by msgcat_init() during bootstrap.
   * But, try once more to call msgcat_init() because there could be
   * an exception case that get here before bootstrap.
   */
  int status = NO_ERROR;
  if (msgcat_init () != NO_ERROR)
    {
      // todo
      er_emergency (__FILE__, __LINE__, er_Cached_msg[ER_ER_NO_CATALOG]);
      status = ER_FAILED;
    }
  else if (er_Is_cached_msg == false)
    {
      /* cache the messages */

      /*
       * Remember, we skip code 0.  If we can't find enough memory to
       * copy the silly message, things are going to be pretty tight
       * anyway, so just use the default version.
       */
      for (i = 1; i < (int) DIM (er_Cached_msg); i++)
	{
	  const char *msg;
	  char *tmp;

	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_INTERNAL, i);
	  if (msg && *msg)
	    {
	      tmp = (char *) malloc (std::strlen (msg) + 1);
	      if (tmp)
		{
		  strcpy (tmp, msg);
		  er_Cached_msg[i] = tmp;
		}
	    }
	}

      er_Is_cached_msg = true;
    }

  if (er_call_stack_init () != NO_ERROR)
    {
      status = ER_FAILED;
    }

  return status;
}

/*
 * er_is_initialized () - return if error manager was initialized.
 *
 * return :
 * void (in) :
 */
bool
er_is_initialized (void)
{
  return er_Hasalready_initiated;
}

/*
 * er_set_print_property -
 *   return: void
 *   print_console(in):
 */
void
er_set_print_property (int print_console)
{
  er_Print_to_console = print_console;
}

/*
 * er_file_open - small utility function to open error log file
 *   return: FILE *
 *   path(in): file path
 */
static FILE *
er_file_open (const char *path)
{
  FILE *fp;
  char dir[PATH_MAX], *tpath;

  assert (path != NULL);

  tpath = strdup (path);
  while (1)
    {
      if (cub_dirname_r (tpath, dir, PATH_MAX) > 0 && access (dir, F_OK) < 0)
	{
	  if (mkdir (dir, 0777) < 0 && errno == ENOENT)
	    {
	      free_and_init (tpath);
	      tpath = strdup (dir);
	      continue;
	    }
	}
      break;
    }
  free_and_init (tpath);

  /* note: in "a+" mode, output is always appended */
  fp = fopen (path, "r+");
  if (fp != NULL)
    {
      fseek (fp, 0, SEEK_END);
      if (ftell (fp) > prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
	{
	  /* not a null device file */
	  fp = er_file_backup (fp, path);
	}
    }
  else
    {
      fp = fopen (path, "w");
    }
  return fp;
}

/*
 * er_file_isa_null_device - check if it is a null device
 *    return: true if the path is a null device. false otherwise
 *    path(in): path to the file
 */
static bool
er_file_isa_null_device (const char *path)
{
#if defined(WINDOWS)
  const char *null_dev = "NUL";
#else /* UNIX family */
  const char *null_dev = "/dev/null";
#endif

  if (path != NULL && strcmp (path, null_dev) == 0)
    {
      return true;
    }
  else
    {
      return false;
    }
}

static FILE *
er_file_backup (FILE * fp, const char *path)
{
  char backup_file[PATH_MAX];

  assert (fp != NULL);
  assert (path != NULL);

  fclose (fp);

  sprintf (backup_file, "%s.bak", path);
  (void) unlink (backup_file);
  (void) rename (path, backup_file);

  return fopen (path, "w");
}

#if !defined (WINDOWS) && defined (SERVER_MODE)
/*
 * er_file_create_link_to_current_log_file - creates a symbolic link to the current log file
 *   return: none
 *   log_file_path (in): path to the log file
 *   suffix (in): file suffix
 *
 * The symbolic link is something like $CUBRID/log/server/{db_name}_latest{suffix}.
 */
void
er_file_create_link_to_current_log_file (const char *log_file_path, const char *suffix)
{
  FILE *fp;
  char link_path[PATH_MAX];
  char link_dir_path[PATH_MAX];
  const char *db_name;

  assert (log_file_path != NULL);

  db_name = boot_db_name ();
  if (*db_name == '\0')
    {
      // boot in progress
      return;
    }

  cub_dirname_r (log_file_path, link_dir_path, PATH_MAX);

  if (snprintf (link_path, PATH_MAX, "%s%c%s_latest%s", link_dir_path, PATH_SEPARATOR, db_name, suffix) >= PATH_MAX)
    {
      // overflow
      return;
    }

  (void) unlink (link_path);	// remove existing link file
  symlink (log_file_path, link_path);
}
#endif /* !WINDOWS && SERVER_MODE */

/*
 * er_final - Terminate the error message module
 *   return: none
 *   do_global_final(in):
 *   need_csect(in): server_mode only; if true, get ENTER_LOG_FILE critical section
 */
void
er_final (ER_FINAL_CODE do_global_final)
{
  FILE *fh = NULL;
  int i = 0;

  if (!er_Hasalready_initiated)
    {
      // not initialized
      return;
    }

  if (do_global_final == ER_ALL_FINAL)
    {
#if !defined (SERVER_MODE)
      // destroy singleton context
      er_Singleton_context_p->deregister_thread_local ();
      delete er_Singleton_context_p;
      er_Singleton_context_p = NULL;
#endif // not SERVER_MODE

      er_event_final ();

      // *INDENT-OFF*
      // protect log file mutex
      std::unique_lock<std::mutex> log_file_lock (er_Log_file_mutex); // mutex is released on destructor
      // *INDENT-ON*

      if (er_Logfile_opened == true)
	{
	  if (er_Msglog_fh != NULL && er_Msglog_fh != stderr)
	    {
	      fh = er_Msglog_fh;
	      er_Msglog_fh = NULL;
	      (void) fclose (fh);
	    }
	  er_Logfile_opened = false;

	  if (er_Accesslog_fh != NULL && er_Accesslog_fh != stderr)
	    {
	      fh = er_Accesslog_fh;
	      er_Accesslog_fh = NULL;
	      (void) fclose (fh);
	    }
	}

      log_file_lock.unlock ();

      for (i = 0; i < (int) DIM (er_Fmt_list); i++)
	{
	  er_clear_fmt (&er_Fmt_list[i]);
	}

      for (i = 0; i < (int) DIM (er_Cached_msg); i++)
	{
	  if (er_Cached_msg[i] != er_Builtin_msg[i])
	    {
	      free_and_init (er_Cached_msg[i]);
	      er_Cached_msg[i] = (char *) er_Builtin_msg[i];
	    }
	}

      er_call_stack_final ();

      er_Hasalready_initiated = false;
      er_Has_sticky_init = false;
    }
}

/*
 * er_clear - Clear any error message
 *   return: none
 *
 * Note: This function is used to ignore an occurred error.
 */
void
er_clear (void)
{
  if (!er_is_initialized ())
    {
      // ignore
      return;
    }
  context::get_thread_local_context ().clear_current_error_level ();
}

/*
 * er_set - Set an error
 *   return: none
 *   severity(in): may exit if severity == ER_FATAL_ERROR_SEVERITY
 *   file_name(in): file name setting the error
 *   line_no(in): line in file where the error is set
 *   err_id(in): the error identifier
 *   num_args(in): number of arguments...
 *   ...(in): variable list of arguments (just like fprintf)
 *
 * Note: The error message associated with the given error identifier
 *       is parsed and the arguments are substituted for later
 *       retrieval. The caller must supply the exact number of
 *       arguments to set all level messages of the error. The error
 *       logging function (if any) associated with the error is called.
 */
void
er_set (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...)
{
  va_list ap;

  va_start (ap, num_args);
  (void) er_set_internal (severity, file_name, line_no, err_id, num_args, false, NULL, &ap);
  va_end (ap);
}

/*
 * er_set_with_file - Set an error and print file contents into
 *                    the error log file
 *   return: none
 *   severity(in): may exit if severity == ER_FATAL_ERROR_SEVERITY
 *   file_name(in): file name setting the error
 *   line_no(in): line in file where the error is set
 *   err_id(in): the error identifier
 *   fp(in): file pointer
 *   num_args(in): number of arguments...
 *   ...(in): variable list of arguments (just like fprintf)
 *
 * Note: The error message associated with the given error identifier
 *       is parsed and the arguments are substituted for later
 *       retrieval. The caller must supply the exact number of
 *       arguments to set all level messages of the error. The error
 *       logging function (if any) associated with the error is called.
 */
void
er_set_with_file (int severity, const char *file_name, const int line_no, int err_id, FILE * fp, int num_args, ...)
{
  va_list ap;

  va_start (ap, num_args);
  (void) er_set_internal (severity, file_name, line_no, err_id, num_args, false, fp, &ap);
  va_end (ap);
}

/*
 * er_set_with_oserror - Set an error and include the OS last error
 *   return: none
 *   severity(in): may exit if severity == ER_FATAL_ERROR_SEVERITY
 *   file_name(in): file name setting the error
 *   line_no(in): line in file where the error is set
 *   err_id(in): the error identifier
 *   num_args(in): number of arguments...
 *   ...(in): variable list of arguments (just like fprintf)
 *
 * Note: This function is the same as er_set + append Unix error message.
 *       The error message associated with the given error identifier
 *       is parsed and the arguments are substituted for later
 *       retrieval. In addition the latest OS error message is appended
 *       in all level messages for the error. The caller must supply
 *       the exact number of arguments to set all level messages of the
 *       error. The log error message function associated with the
 *       error is called.
 */
void
er_set_with_oserror (int severity, const char *file_name, const int line_no, int err_id, int num_args, ...)
{
  va_list ap;

  va_start (ap, num_args);
  (void) er_set_internal (severity, file_name, line_no, err_id, num_args, true, NULL, &ap);
  va_end (ap);
}

/*
 * er_notify_event_on_error - notify event
 *   return: none
 *   err_id(in):
 */
static void
er_notify_event_on_error (int err_id)
{
  assert (err_id != NO_ERROR);

  err_id = abs (err_id);
  if (sysprm_find_err_in_integer_list (PRM_ID_EVENT_ACTIVATION, err_id))
    {
      er_event ();
    }
}

/*
 * er_print_callstack () - Print message with callstack (should help for
 *			   debug).
 *
 * return :
 * const char * file_name (in) :
 * const int line_no (in) :
 * const char * fmt (in) :
 * ... (in) :
 */
void
er_print_callstack (const char *file_name, const int line_no, const char *fmt, ...)
{
  va_list ap;

  // *INDENT-OFF*
  // protect log file mutex
  std::unique_lock<std::mutex> log_file_lock (er_Log_file_mutex); // mutex is released on destructor
  // *INDENT-ON*

  va_start (ap, fmt);
  _er_log_debug_internal (file_name, line_no, fmt, &ap);
  va_end (ap);

  FILE *out = (er_Msglog_fh != NULL) ? er_Msglog_fh : stderr;
  er_dump_call_stack (out);
  fprintf (out, "\n");
}

/*
 * er_call_stack_dump_on_error - call stack dump
 *   return: none
 *   severity(in):
 *   err_id(in):
 */
static void
er_call_stack_dump_on_error (int severity, int err_id)
{
  assert (err_id != NO_ERROR);

  err_id = abs (err_id);
  if (severity == ER_FATAL_ERROR_SEVERITY)
    {
      er_dump_call_stack (er_Msglog_fh);
    }
  else if (prm_get_bool_value (PRM_ID_CALL_STACK_DUMP_ON_ERROR))
    {
      if (!sysprm_find_err_in_integer_list (PRM_ID_CALL_STACK_DUMP_DEACTIVATION, err_id))
	{
	  er_dump_call_stack (er_Msglog_fh);
	}
    }
  else
    {
      if (sysprm_find_err_in_integer_list (PRM_ID_CALL_STACK_DUMP_ACTIVATION, err_id))
	{
	  er_dump_call_stack (er_Msglog_fh);
	}
    }
}

/*
 * er_set_internal - Set an error and an optionally the Unix error
 *   return:
 *   severity(in): may exit if severity == ER_FATAL_ERROR_SEVERITY
 *   file_name(in): file name setting the error
 *   line_no(in): line in file where the error is set
 *   err_id(in): the error identifier
 *   num_args(in): number of arguments...
 *   fp(in): file pointer
 *   ...(in): variable list of arguments (just like fprintf)
 *
 * Note:
 */
static int
er_set_internal (int severity, const char *file_name, const int line_no, int err_id, int num_args,
		 bool include_os_error, FILE * fp, va_list * ap_ptr)
{
  va_list ap;
  const char *os_error;
  std::size_t new_size;
  ER_FMT *er_fmt = NULL;
  int ret_val = NO_ERROR;
  bool need_stack_pop = false;

  /* check if not used error code */
  assert (err_id != ER_TP_INCOMPATIBLE_DOMAINS);
  assert (err_id != ER_NUM_OVERFLOW);
  assert (err_id != ER_QPROC_OVERFLOW_COERCION);
  assert (err_id != ER_FK_CANT_ASSIGN_CACHE_ATTR);
  assert (err_id != ER_FK_CANT_DROP_CACHE_ATTR);
  assert (err_id != ER_SM_CATALOG_SPACE);
  assert (err_id != ER_CT_UNKNOWN_CLASSID);
  assert (err_id != ER_CT_REPRCNT_OVERFLOW);

  if (er_Hasalready_initiated == false)
    {
#if defined (CS_MODE) && !defined (NDEBUG)
      /* temporary workaround for HA process which may encounter missing er_module */
      if (er_Ignore_uninit)
	{
	  return ER_FAILED;
	}
#endif
      assert (false);
      er_Errid_not_initialized = err_id;
      return ER_FAILED;
    }

  // *INDENT-OFF*
  context &tl_context = context::get_thread_local_context ();
  // *INDENT-ON*

  /*
   * Get the UNIX error message if needed. We need to get this as soon
   * as possible to avoid resetting the error.
   */
  os_error = (include_os_error && errno != 0) ? strerror (errno) : NULL;

  memcpy (&ap, ap_ptr, sizeof (ap));

  // *INDENT-OFF*
  // should force error stacking? yes if:
  // 1. this is a notification and an error was already set
  // 2. current error is interrupted error.
  er_message &prev_err = tl_context.get_current_error_level ();
  // *INDENT-ON*

  if ((severity == ER_NOTIFICATION_SEVERITY && prev_err.err_id != NO_ERROR) || (prev_err.err_id == ER_INTERRUPTED))
    {
      tl_context.push_error_stack ();
      need_stack_pop = true;
    }

  // *INDENT-OFF*
  // get current error reference
  er_message &crt_error = tl_context.get_current_error_level ();
  // *INDENT-ON*

  /* Initialize the area... */
  crt_error.set_error (err_id, severity, file_name, line_no);

  /*
   * Get hold of the compiled format string for this message and get an
   * estimate of the size of the buffer required to print it.
   */
  er_fmt = er_find_fmt (err_id, num_args);
  if (er_fmt == NULL)
    {
      /*
       * Assumes that er_find_fmt() has already called er_emergency().
       */
      ret_val = ER_FAILED;
      goto end;
    }

  if (err_id >= ER_FAILED || err_id <= ER_LAST_ERROR)
    {
      assert (0);		/* invalid error id */
      err_id = ER_FAILED;	/* invalid error id handling */
    }

  new_size = er_estimate_size (er_fmt, ap_ptr);

  /* Do we need to copy the OS error message? */
  if (os_error)
    {
      new_size += 4 + strlen (os_error);
    }

  /* Do any necessary allocation for the buffer. */
  crt_error.reserve_message_area (new_size + 1);

  /* And now format the silly thing. */
  if (er_vsprintf (&crt_error, er_fmt, ap_ptr) == ER_FAILED)
    {
      ret_val = ER_FAILED;
      goto end;
    }
  if (os_error != NULL)
    {
      strcat (crt_error.msg_area, "... ");
      strcat (crt_error.msg_area, os_error);
    }

  /* Call the logging function if any */
  if (severity <= prm_get_integer_value (PRM_ID_ER_LOG_LEVEL)
      && !(prm_get_bool_value (PRM_ID_ER_LOG_WARNING) == false && severity == ER_WARNING_SEVERITY)
      && er_Fnlog[severity] != NULL)
    {
      // *INDENT-OFF*
      // protect log file mutex
      std::unique_lock<std::mutex> log_file_lock (er_Log_file_mutex); // mutex is released on destructor
      // *INDENT-ON*
      (*er_Fnlog[severity]) (err_id);

      /* call stack dump */
      er_call_stack_dump_on_error (severity, err_id);

      /* event handler */
      er_notify_event_on_error (err_id);

      if (fp != NULL)
	{
	  /* print file contents */
	  if (fseek (fp, 0L, SEEK_SET) == 0)
	    {
	      char buf[MAX_LINE];
	      while (fgets (buf, MAX_LINE, fp) != NULL)
		{
		  fprintf (er_Msglog_fh, "%s", buf);
		}
	      (void) fflush (er_Msglog_fh);
	    }
	}

      log_file_lock.unlock ();

      if (er_Print_to_console && severity <= ER_ERROR_SEVERITY && crt_error.msg_area)
	{
	  fprintf (stderr, "%s\n", crt_error.msg_area);
	}
    }

  /*
   * Do we want to stop the system on this error ... for debugging purposes?
   */
  if (prm_get_integer_value (PRM_ID_ER_STOP_ON_ERROR) == err_id)
    {
      er_stop_on_error ();
    }

  if (severity == ER_NOTIFICATION_SEVERITY)
    {
      crt_error.clear_error ();
    }

end:

  if (need_stack_pop)
    {
      tl_context.pop_error_stack_and_destroy ();
    }

  return ret_val;
}

/*
 * er_stop_on_error - Stop the system when an error occurs
 *   return: none
 *
 * Note: This feature can be used when debugging a particular error outside the debugger. The user is asked whether or
 *       not to continue.
 */
static void
er_stop_on_error (void)
{
  int exit_requested;

#if !defined (WINDOWS)
  syslog (LOG_ALERT, er_Cached_msg[ER_STOP_SYSLOG], rel_name (), er_errid (), cuserid (NULL), getpid ());
#endif /* !WINDOWS */

  (void) fprintf (stderr, "%s", er_Cached_msg[ER_ER_ASK]);
  if (scanf ("%d", &exit_requested) != 1)
    {
      exit_requested = TRUE;
    }

  if (exit_requested)
    {
      exit (EXIT_FAILURE);
    }
}

/*
 * er_log - Log the error message
 *   return: none
 *
 * Note: The maximum available level of the error is logged into file.
 *       This function is used at the default logging
 *       function to call when error are set. The calling logging
 *       function can be modified by calling the function er_fnerlog.
 */
void
er_log (int err_id)
{
  int severity;
  int nlevels;
  int line_no;
  const char *file_name;
  const char *msg;
  off_t position;
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  struct timeval tv;
  char time_array[256];
  int tran_index;
  char *more_info_p;
  int ret;
  char more_info[CUB_MAXHOSTNAMELEN + PATH_MAX + 64];
  const char *log_file_name;
  const char *log_file_suffix;
  FILE **log_fh;

  if (er_Accesslog_filename != NULL && err_id == ER_BO_CLIENT_CONNECTED)
    {
      log_file_name = er_Accesslog_filename;
      log_file_suffix = ER_ACCESS_LOG_FILE_SUFFIX;
      log_fh = &er_Accesslog_fh;
    }
  else
    {
      log_file_name = er_Msglog_filename;
      log_file_suffix = ER_MSG_LOG_FILE_SUFFIX;
      log_fh = &er_Msglog_fh;
    }

  /* Check for an invalid output file */
  /* (stderr is not valid under Windows and is set to NULL) */
  if (log_fh == NULL || *log_fh == NULL)
    {
      return;
    }

  /* Make sure that we have a valid error identifier */

  /* Get the most detailed error message available */
  er_all (&err_id, &severity, &nlevels, &line_no, &file_name, &msg);

  /*
   * Don't let the file of log messages get very long. Backup or go back to the top if need be.
   */
  if (*log_fh != stderr && *log_fh != stdout && ftell (*log_fh) > (int) prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
    {
      (void) fflush (*log_fh);
      (void) fprintf (*log_fh, "%s", er_Cached_msg[ER_LOG_WRAPAROUND]);

      if (!er_Isa_null_device)
	{
	  *log_fh = er_file_backup (*log_fh, log_file_name);

	  if (*log_fh == NULL)
	    {
	      *log_fh = stderr;
	      er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_MSGLOG_WARNING], log_file_name);
	    }
#if !defined (WINDOWS) && defined (SERVER_MODE)
	  else
	    {
	      er_file_create_link_to_current_log_file (log_file_name, log_file_suffix);
	    }
#endif /* !WINDOWS && SERVER_MODE */
	}
      else
	{
	  /* Should be rewinded to avoid repeated limit check hitting */
	  (void) fseek (*log_fh, 0L, SEEK_SET);
	}
    }

  if (*log_fh == stderr || *log_fh == stdout)
    {
      /*
       * Try to avoid out of sequence stderr & stdout.
       *
       */
      (void) fflush (stderr);
      (void) fflush (stdout);
    }

  /* LOG THE MESSAGE */

  er_time = time (NULL);
  er_tm_p = localtime_r (&er_time, &er_tm);
  if (er_tm_p == NULL)
    {
      strcpy (time_array, "00/00/00 00:00:00.000");
    }
  else
    {
      gettimeofday (&tv, NULL);
      snprintf (time_array + strftime (time_array, 128, "%m/%d/%y %H:%M:%S", er_tm_p), 255, ".%03ld",
		tv.tv_usec / 1000);
    }

  more_info_p = (char *) "";

  if (++er_Eid == 0)
    {
      er_Eid = 1;
    }

#if defined (SERVER_MODE)
  {
    const char *prog_name = NULL;
    const char *user_name = NULL;
    const char *host_name = NULL;
    int pid = 0;

    if (logtb_find_client_tran_name_host_pid (tran_index, &prog_name, &user_name, &host_name, &pid) == NO_ERROR)
      {
	ret = snprintf (more_info, sizeof (more_info), ", CLIENT = %s:%s(%d), EID = %u",
			host_name ? host_name : "unknown", prog_name ? prog_name : "unknown", pid, er_Eid);
	if (ret > 0)
	  {
	    more_info_p = &more_info[0];
	  }
      }
  }
#else /* SERVER_MODE */
  tran_index = TM_TRAN_INDEX ();
  ret = snprintf (more_info, sizeof (more_info), ", EID = %u", er_Eid);
  if (ret > 0)
    {
      more_info_p = &more_info[0];
    }

  if (er_Handler != NULL)
    {
      (*er_Handler) (er_Eid);
    }
#endif /* !SERVER_MODE */

  /* If file is not exist, it will recreate *log_fh file. */
  if ((access (log_file_name, F_OK) == -1) && *log_fh != stderr && *log_fh != stdout)
    {
      (void) fclose (*log_fh);
      *log_fh = er_file_open (log_file_name);

      if (*log_fh == NULL)
	{
	  *log_fh = stderr;
	  er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_MSGLOG_WARNING], log_file_name);
	}
    }

  fprintf (*log_fh, er_Cached_msg[ER_LOG_MSG_WRAPPER_D], time_array, ER_SEVERITY_STRING (severity), file_name, line_no,
	   ER_ERROR_WARNING_STRING (severity), err_id, tran_index, more_info_p, msg);
  fflush (*log_fh);

  /* Flush the message so it is printed immediately */
  (void) fflush (*log_fh);

  if (*log_fh != stderr || *log_fh != stdout)
    {
      position = ftell (*log_fh);
      (void) fprintf (*log_fh, "%s", er_Cached_msg[ER_LOG_LAST_MSG]);
      (void) fflush (*log_fh);
      (void) fseek (*log_fh, position, SEEK_SET);
    }

  /* Do we want to exit ? */

  if (severity == ER_FATAL_ERROR_SEVERITY)
    {
      switch (er_Exit_ask)
	{
	case ER_ABORT:
	  abort ();
	  break;

	case ER_EXIT_ASK:
#if defined (NDEBUG)
	  er_stop_on_error ();
	  break;
#endif /* NDEBUG */

	case ER_EXIT_DONT_ASK:
	  (void) fprintf (er_Msglog_fh, "%s", er_Cached_msg[ER_ER_EXIT]);
	  (void) fflush (er_Msglog_fh);
	  er_final (ER_ALL_FINAL);
	  exit (EXIT_FAILURE);
	  break;

	case ER_NEVER_EXIT:
	default:
	  break;
	}
    }
}

er_log_handler_t
er_register_log_handler (er_log_handler_t handler)
{
#if !defined (SERVER_MODE)
  er_log_handler_t prev;

  prev = er_Handler;
  er_Handler = handler;
  return prev;
#else
  assert (0);

  return NULL;
#endif
}

/*
 * er_errid - Retrieve last error identifier set before
 *   return: error identifier
 *
 * Note: In most cases, it is simply enough to know whether or not
 *       there was an error. However, in some cases it is convenient to
 *       design the system and application to anticipate and handle
 *       some errors.
 */
int
er_errid (void)
{
  if (!er_Hasalready_initiated)
    {
#if defined (CS_MODE) && !defined (NDEBUG)
      /* temporary workaround for HA process which may encounter missing er_module */
      if (er_Ignore_uninit)
	{
	  return er_Errid_not_initialized;
	}
#endif
      assert (false);
      return er_Errid_not_initialized;
    }

  return context::get_thread_local_error ().err_id;
}

/*
 * er_errid_if_has_error - Retrieve last error identifier set before
 *   return: error identifier
 *
 * Note: The function ignores an error with ER_WARNING_SEVERITY or
 *       ER_NOTIFICATION_SEVERITY.
 */
int
er_errid_if_has_error (void)
{
  // *INDENT-OFF*
  er_message &crt_error = context::get_thread_local_error ();
  // *INDENT-ON*

  return er_is_error_severity ((er_severity) crt_error.severity) ? crt_error.err_id : NO_ERROR;
}

/*
 * er_clearid - Clear only error identifier
 *   return: none
 */
void
er_clearid (void)
{
  // todo: is this necessary?
#if defined (CS_MODE) && !defined (NDEBUG)
  /* temporary workaround for HA process which may encounter missing er_module */
  if (!er_Hasalready_initiated && er_Ignore_uninit)
    {
      return;
    }
#endif
  assert (er_Hasalready_initiated);

  context::get_thread_local_error ().err_id = NO_ERROR;
}

/*
 * er_setid - Set only an error identifier
 *   return: none
 *   err_id(in):
 */
void
er_setid (int err_id)
{
  // todo: is this necessary?
#if defined (CS_MODE) && !defined (NDEBUG)
  /* temporary workaround for HA process which may encounter missing er_module */
  if (!er_Hasalready_initiated && er_Ignore_uninit)
    {
      return;
    }
#endif
  assert (er_Hasalready_initiated);

  context::get_thread_local_error ().err_id = err_id;
}

/*
 * er_get_severity - Get severity of the last error set before
 *   return: severity
 */
int
er_get_severity (void)
{
  return context::get_thread_local_error ().severity;
}

/*
 * er_has_error -
 *   return: true if it has an actual error, otherwise false.
 *   note: NOTIFICATION and WARNING are not regarded as an actual error.
 */
bool
er_has_error (void)
{
  return er_is_error_severity ((er_severity) context::get_thread_local_error ().severity);
}

/*
 * er_msg - Retrieve current error message
 *   return: a string, at the given level, describing the last error
 *
 * Note: The string returned is overwritten when the next error is set,
 *       so it may be necessary to copy it to a static area if you
 *       need to keep it for some length of time.
 */
const char *
er_msg (void)
{
#if defined (CS_MODE) && !defined (NDEBUG)
  /* temporary workaround for HA process which may encounter missing er_module */
  if (!er_Hasalready_initiated && er_Ignore_uninit)
    {
      return "Not available";
    }
#endif

  if (!er_Hasalready_initiated)
    {
      assert (false);
      return "Not available";	// todo: is this safe?
    }

  // *INDENT-OFF*
  er_message &crt_error = context::get_thread_local_error ();
  // *INDENT-ON*

  if (crt_error.msg_area[0] == '\0')
    {
      std::strncpy (crt_error.msg_area, er_Cached_msg[ER_ER_MISSING_MSG], crt_error.msg_area_size);
      crt_error.msg_area[crt_error.msg_area_size - 1] = '\0';
    }

  return crt_error.msg_area;
}

/*
 * er_all - Return everything about the last error
 *   return: none
 *   err_id(out): error identifier
 *   severity(out): severity of the error
 *   n_levels(out): number of levels of the error
 *   line_no(out): line number in the file where the error was set
 *   file_name(out): file name where the error was set
 *   error_msg(out): the formatted message of the error
 *
 * Note:
 */
void
er_all (int *err_id, int *severity, int *n_levels, int *line_no, const char **file_name, const char **error_msg)
{
  // *INDENT-OFF*
  er_message &crt_error = context::get_thread_local_error ();
  // *INDENT-ON*

  *err_id = crt_error.err_id;
  *severity = crt_error.severity;
  *n_levels = 1;		// is this stack level?
  *line_no = crt_error.line_no;
  *file_name = crt_error.file_name;
  *error_msg = er_msg ();
}

/*
 * _er_log_debug () - Print message to error log file.
 *
 * return	  : Void.
 * file_name (in) : __FILE__
 * line_no (in)	  : __LINE__
 * fmt (in)	  : Formatted message.
 * ... (in)	  : Arguments for formatted message.
 */
void
_er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
  va_list ap;

#if defined (CS_MODE) && !defined (NDEBUG)
  /* temporary workaround for HA process which may encounter missing er_module */
  if (!er_Hasalready_initiated && er_Ignore_uninit)
    {
      return;
    }
#endif

  assert (er_Hasalready_initiated);

  // *INDENT-OFF*
  // protect log file mutex
  std::unique_lock<std::mutex> log_file_lock (er_Log_file_mutex); // mutex is released on destructor
  // *INDENT-ON*

  va_start (ap, fmt);
  _er_log_debug_internal (file_name, line_no, fmt, &ap);
  va_end (ap);
}

/*
 * er_log_debug - Print debugging message to the log file
 *   return: none
 *   file_name(in):
 *   line_no(in):
 *   fmt(in):
 *   ap(in):
 *
 * Note:
 */
static void
_er_log_debug_internal (const char *file_name, const int line_no, const char *fmt, va_list * ap)
{
  FILE *out;
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  struct timeval tv;
  char time_array[256];

  if (er_Hasalready_initiated == false)
    {
      /* do not print debug info */
      return;
    }

  out = (er_Msglog_fh != NULL) ? er_Msglog_fh : stderr;

  er_time = time (NULL);

  er_tm_p = localtime_r (&er_time, &er_tm);
  if (er_tm_p == NULL)
    {
      strcpy (time_array, "00/00/00 00:00:00.000");
    }
  else
    {
      gettimeofday (&tv, NULL);
      snprintf (time_array + strftime (time_array, 128, "%m/%d/%y %H:%M:%S", er_tm_p), 255, ".%03ld",
		tv.tv_usec / 1000);
    }

  fprintf (out, er_Cached_msg[ER_LOG_DEBUG_NOTIFY], time_array, file_name, line_no);

  /* Print out remainder of message */
  vfprintf (out, fmt, *ap);
  fflush (out);
}

/*
 * er_get_ermsg_from_area_error -
 *   return: ermsg string from the flatten error area
 *   buffer(in): flatten error area
 */
char *
er_get_ermsg_from_area_error (char *buffer)
{
  assert (buffer != NULL);

  return buffer + (OR_INT_SIZE * 3);
}

/*
 * er_get_area_error - Flatten error information into an area
 *   return: packed error information that can be transmitted to the client
 *   length(out): length of the flatten area (set as a side effect)
 *
 * Note: This function is used for Client/Server transfering of errors.
 */
char *
er_get_area_error (char *buffer, int *length)
{
  std::size_t len, max_msglen;
  char *ptr;
  const char *msg;

  assert (*length > OR_INT_SIZE * 3);

  // *INDENT-OFF*
  er_message &crt_error = context::get_thread_local_error ();
  // *INDENT-ON*

  /* Now copy the information */
  msg = strlen (crt_error.msg_area) != 0 ? crt_error.msg_area : "(null)";

  len = (OR_INT_SIZE * 3) + strlen (msg) + 1;
  len = MIN (len, *length);
  *length = (int) len;
  max_msglen = len - (OR_INT_SIZE * 3) - 1;

  ptr = buffer;
  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  OR_PUT_INT (ptr, (int) crt_error.err_id);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, (int) crt_error.severity);
  ptr += OR_INT_SIZE;

  OR_PUT_INT (ptr, len);
  ptr += OR_INT_SIZE;

  strncpy (ptr, msg, max_msglen);
  *(ptr + max_msglen) = '\0';	/* bullet proofing */

  return buffer;
}

/*
 * er_set_area_error - Reset the error information
 *   return: error id which was contained in the given error information
 *   server_area(in): the flatten area with error information
 *
 * Note: Error information is reset with the one provided by the packed area,
 *       which is the last error found in the server.
 */
int
er_set_area_error (char *server_area)
{
  char *ptr;
  int err_id, severity;
  std::size_t length = 0;

  if (server_area == NULL)
    {
      er_clear ();
      return NO_ERROR;
    }

  // *INDENT-OFF*
  er_message &crt_error = context::get_thread_local_error ();
  // *INDENT-ON*

  ptr = server_area;
  ASSERT_ALIGN (ptr, INT_ALIGNMENT);

  err_id = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  assert (err_id <= NO_ERROR && ER_LAST_ERROR < err_id);

  severity = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  assert (ER_FATAL_ERROR_SEVERITY <= severity && severity <= ER_MAX_SEVERITY);

  length = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  crt_error.err_id = ((err_id >= 0 || err_id <= ER_LAST_ERROR) ? -1 : err_id);
  crt_error.severity = severity;
  crt_error.file_name = "Unknown from server";
  crt_error.line_no = -1;

  /* Note, current length is the length of the packet not the string, considering that this is NULL terminated, we
   * don't really need to be sending this. Use the actual string length in the memcpy here! */
  length = strlen (ptr) + 1;

  crt_error.reserve_message_area (length);
  memcpy (crt_error.msg_area, ptr, length);

  /* Call the logging function if any */
  if (severity <= prm_get_integer_value (PRM_ID_ER_LOG_LEVEL)
      && !(prm_get_bool_value (PRM_ID_ER_LOG_WARNING) == false && severity == ER_WARNING_SEVERITY)
      && er_Fnlog[severity] != NULL)
    {
      // *INDENT-OFF*
      // protect log file mutex
      std::unique_lock<std::mutex> log_file_lock (er_Log_file_mutex); // mutex is released on destructor
      // *INDENT-ON*

      (*er_Fnlog[severity]) (err_id);
      log_file_lock.unlock ();

      if (er_Print_to_console && severity <= ER_ERROR_SEVERITY && crt_error.msg_area)
	{
	  fprintf (stderr, "%s\n", crt_error.msg_area);
	}
    }

  return crt_error.err_id;
}

/*
 * er_stack_push - Save the current error onto the stack
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: The current set error information is saved onto a stack.
 *       This function can be used in conjunction with er_stack_pop when
 *       the caller function wants to return the current error message
 *       no matter what other additional errors are set. For example,
 *       a function may detect an error, then call another function to
 *       do some cleanup. If the cleanup function set an error, the
 *       desired error can be lost.
 *       A function that push something should pop or clear the entry,
 *       otherwise, a function doing a pop may not get the right entry.
 */
void
er_stack_push (void)
{
  (void) context::get_thread_local_context ().push_error_stack ();
}

/*
 * er_stack_push_if_exists - Save the last error if exists onto the stack
 *
 * Note: Please notice the difference from er_stack_push.
 *       This function only pushes when an error was set, while er_stack_push always makes a room
 *       and pushes the current entry. It will be used in conjunction with er_restore_last_error.
 */
void
er_stack_push_if_exists (void)
{
  // *INDENT-OFF*
  context &tl_context = context::get_thread_local_context ();
  // *INDENT-ON*

  if (tl_context.get_current_error_level ().err_id == NO_ERROR && !tl_context.has_error_stack ())
    {
      /* If neither an error was set nor pushed, keep using the current error entry.
       *
       * If this is not a base error entry,
       * we have to push one since it means that caller pushed one and latest entry will be soon popped.
       */
      return;
    }

  // push
  (void) tl_context.push_error_stack ();
}

/*
 * er_stack_pop - Restore the previous error from the stack.
 *                The latest saved error is restored in the error area.
 *   return: NO_ERROR or ER_FAILED
 */
void
er_stack_pop (void)
{
  context::get_thread_local_context ().pop_error_stack_and_destroy ();
}

/*
 * er_stack_pop_and_keep_error - Clear the latest saved error message in the stack
 *                  That is, pop without restore.
 *   return: none
 */
void
er_stack_pop_and_keep_error (void)
{
  // *INDENT-OFF*
  context &tl_context = context::get_thread_local_context ();
  // *INDENT-ON*
  er_message top (tl_context.get_logging ());

  if (!tl_context.has_error_stack ())
    {
      // bad pop
      assert (false);
      return;
    }

  tl_context.pop_error_stack (top);
  if (top.err_id != NO_ERROR)
    {
      /* keep the error. push it to current top */
      top.swap (tl_context.get_thread_local_error ());
    }
  else
    {
      // leave as is
    }

  // popped error stack is destroyed
}

/*
 * er_restore_last_error - Restore the last error between the current entry and the pushed one.
 *                         If the current entry has an error, clear the pushed entry which is no longer needed.
 *                         Otherwise, pop the current entry and restore the saved one.
 *
 *   return: none
 *
 * Note: Please see also er_stack_push_if_exists
 */
void
er_restore_last_error (void)
{
  // *INDENT-OFF*
  context &tl_context = context::get_thread_local_context ();
  // *INDENT-ON*

  if (!tl_context.has_error_stack ())
    {
      /* When no pushed entry exists, keep using the current entry. */
      return;
    }

  er_stack_pop_and_keep_error ();
}

/*
 * er_stack_clearall - Clear all saved error messages
 *   return: none
 */
void
er_stack_clearall (void)
{
  // *INDENT-OFF*
  context &tl_context = context::get_thread_local_context ();
  // *INDENT-ON*

  // remove all stacks, but keep last error
  while (tl_context.has_error_stack ())
    {
      er_stack_pop_and_keep_error ();
    }
}


/*
 * er_study_spec -
 *   return: the length of the spec
 *   conversion_spec(in): a single printf() conversion spec, without the '%'
 *   simple_spec(out): a pointer to a buffer to receive a simple version of
 *                     the spec (one without a positional specifier)
 *   position(out): the position of the spec
 *   width(out): the nominal width of the field
 *   va_class(out): a classification of the base (va_list)
 *                  type of the arguments described by the spec
 *
 * Note: Breaks apart the individual components of the conversion spec
 *	 (as described in the Sun man page) and sets the appropriate
 *	 buffers to record that info.
 */
static int
er_study_spec (const char *conversion_spec, char *simple_spec, int *position, int *width, int *va_class)
{
  char *p;
  const char *q;
  int n, code, class_;

  code = 0;
  class_ = 0;

  simple_spec[0] = '%';
  p = &simple_spec[1];
  q = conversion_spec;

  /*
   * Skip leading flags...
   */

  while (*q == '-' || *q == '+' || *q == ' ' || *q == '#')
    {
      *p++ = *q++;
    }

  /*
   * Now look for a numeric field.  This could be either a position
   * specifier or a width specifier; we won't know until we find out
   * what follows it.
   */

  n = 0;
  while (char_isdigit (*q))
    {
      n *= 10;
      n += (*q) - '0';
      *p++ = *q++;
    }

  if (*q == '$')
    {
      /*
       * The number was a position specifier, so record that, skip the
       * '$', and start over depositing conversion spec characters at
       * the beginning of simple_spec.
       */
      q++;

      if (n)
	{
	  *position = n;
	}
      p = &simple_spec[1];

      /*
       * Look for flags again...
       */
      while (*q == '-' || *q == '+' || *q == ' ' || *q == '#')
	{
	  *p++ = *q++;
	}

      /*
       * And then look for a width specifier...
       */
      n = 0;
      while (char_isdigit (*q))
	{
	  n *= 10;
	  n += (*q) - '0';
	  *p++ = *q++;
	}
      *width = n;
    }

  /*
   * Look for an optional precision...
   */
  if (*q == '.')
    {
      *p++ = *q++;
      while (char_isdigit (*q))
	{
	  *p++ = *q++;
	}
    }

  /*
   * And then for modifier flags...
   */
  if (*q == 'l' && *(q + 1) == 'l')
    {
      /* long long type */
      class_ = SPEC_CODE_LONGLONG;
      *p++ = *q++;
      *p++ = *q++;
    }
  else if (*q == 'z')
    {
      /* size_t type */
      class_ = SPEC_CODE_SIZE_T;
#if defined (WINDOWS)
      *p++ = 'I';
      q++;
#elif defined (__GNUC__)
      *p++ = *q++;
#else
      if (sizeof (size_t) == sizeof (long long int))
	{
	  *p++ = 'l';
	  *p++ = 'l';
	}
      else
	{
	  /* no size modifier */
	}
      q++;
#endif /* WINDOWS */
    }
  else if (*q == 'h')
    {
      /*
       * Ignore this spec and use the class determined (later) by
       * examining the coversion code.  According to Plauger, the
       * standard dictates that stdarg.h be implemented so that short
       * values are all coerced to int.
       */
      *p++ = *q++;
    }

  /*
   * Now copy the actual conversion code.
   */
  code = *p++ = *q++;
  *p++ = '\0';

  if (class_ == 0)
    {
      switch (code)
	{
	case 'c':
	case 'd':
	case 'i':
	case 'o':
	case 'u':
	case 'x':
	case 'X':
	  class_ = 'i';
	  break;
	case 'p':
	  class_ = 'p';
	  break;
	case 'e':
	case 'f':
	case 'g':
	case 'E':
	case 'F':
	case 'G':
	  class_ = 'f';
	  break;
	case 's':
	  class_ = 's';
	  break;
	default:
	  assert (0);
	  er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_UNKNOWN_CODE], code);
	  break;
	}
    }
  *va_class = class_;

  return CAST_STRLEN (q - conversion_spec);
}

/*
 * er_study_fmt -
 *   return:
 *   fmt(in/out): a pointer to an ER_FMT structure to be initialized
 *
 * Note: Scans the printf format string in fmt->fmt and compiles
 *	 interesting information about the conversion specs in the
 *	 string into the spec[] array.
 */
static void
er_study_fmt (ER_FMT * fmt)
{
  const char *p;
  int width, va_class;
  char buf[10];

  int i, n;

  fmt->nspecs = 0;
  for (p = strchr (fmt->fmt, '%'); p; p = strchr (p, '%'))
    {
      if (p[1] == '%')
	{			/* " ...%%..." ??? */
	  p += 1;
	}
      else
	{
	  /*
	   * Set up the position parameter off by one so that we can
	   * decrement it without checking later.
	   */
	  n = ++fmt->nspecs;
	  width = 0;
	  va_class = 0;

	  p += er_study_spec (&p[1], buf, &n, &width, &va_class);

	  /*
	   * 'n' may have been modified by er_study_spec() if we ran
	   * into a conversion spec with a positional component (e.g.,
	   * %3$d).
	   */
	  n -= 1;

	  if (n >= fmt->spec_top)
	    {
	      ER_SPEC *new_spec;
	      int size;

	      /*
	       * Grow the conversion spec array.
	       */
	      size = (n + 1) * sizeof (ER_SPEC);
	      new_spec = (ER_SPEC *) ER_MALLOC (size);
	      if (new_spec == NULL)
		return;
	      memcpy (new_spec, fmt->spec, fmt->spec_top * sizeof (ER_SPEC));
	      if (fmt->spec != fmt->spec_buf)
		free_and_init (fmt->spec);
	      fmt->spec = new_spec;
	      fmt->spec_top = (n + 1);
	    }

	  strcpy (fmt->spec[n].spec, buf);
	  fmt->spec[n].code = va_class;
	  fmt->spec[n].width = width;
	}
    }

  /*
   * Make sure that there were no "holes" left in the parameter space
   * (e.g., "%1$d" and "%3$d", but no "%2$d" spec), and that there were
   * no unknown conversion codes.  If there was a problem, we can't
   * count on being able to safely decode the va_list we'll get, and
   * we're better off just printing a generic message that requires no
   * formatting.
   */
  for (i = 0; i < fmt->nspecs; i++)
    {
      if (fmt->spec[i].code == 0)
	{
	  int code;
	  code = fmt->err_id;
	  er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_SUSPECT_FMT], code);
	  er_internal_msg (fmt, code, ER_ER_SUBSTITUTE_MSG);
	  break;
	}
    }
}

#define MAX_INT_WIDTH		20
#define MAX_DOUBLE_WIDTH	32
/*
 * er_estimate_size -
 *   return: a byte count
 *   fmt(in/out): a pointer to an already-studied ER_FMT structure
 *   ap(in): a va_list of arguments
 *
 * Note:
 * Uses the arg_spec[] info in *fmt, along with the actual args
 * in ap, to make a conservative guess of how many bytes will be
 * required by vsprintf().
 *
 * If fmt hasn't already been studied by er_study_fmt(), this
 * will yield total garbage, if it doesn't blow up.
 *
 * DOESN'T AFFECT THE CALLER'S VIEW OF THE VA_LIST.
 */
static size_t
er_estimate_size (ER_FMT * fmt, va_list * ap)
{
  int i, width;
  size_t n;
  size_t len;
  va_list args;
  const char *str;

  /*
   * fmt->fmt can be NULL if something went wrong while studying it.
   */
  if (fmt->fmt == NULL)
    {
      return strlen (er_Cached_msg[ER_ER_SUBSTITUTE_MSG]);
    }

  memcpy (&args, ap, sizeof (args));

  len = fmt->fmt_length;

  for (i = 0; i < fmt->nspecs; i++)
    {
      switch (fmt->spec[i].code)
	{
	case 'i':
	  (void) va_arg (args, int);
	  n = MAX_INT_WIDTH;
	  break;

	case SPEC_CODE_LONGLONG:
	  (void) va_arg (args, long long int);
	  n = MAX_INT_WIDTH;
	  break;

	case SPEC_CODE_SIZE_T:
	  if (sizeof (size_t) == sizeof (long long int))
	    {
	      (void) va_arg (args, long long int);
	    }
	  else
	    {
	      (void) va_arg (args, int);
	    }
	  n = MAX_INT_WIDTH;
	  break;

	case 'p':
	  (void) va_arg (args, void *);
	  n = MAX_INT_WIDTH;
	  break;

	case 'f':
	  (void) va_arg (args, double);
	  n = MAX_DOUBLE_WIDTH;
	  break;

	case 'L':
	  (void) va_arg (args, long double);
	  n = MAX_DOUBLE_WIDTH;
	  break;

	case 's':
	  str = va_arg (args, char *);
	  if (str == NULL)
	    str = "(null)";
	  n = strlen (str);
	  break;

	default:
	  er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_UNKNOWN_CODE], fmt->spec[i].code);
	  /*
	   * Pray for protection...  We really shouldn't be able to get
	   * here, since er_study_fmt() should protect us from it.
	   */
	  n = MAX_DOUBLE_WIDTH;
	  break;
	}
      width = fmt->spec[i].width;
      len += MAX (width, n);
    }

  return len;
}

/*
 * er_find_fmt -
 *   return: error formats
 *   err_id(in): error identifier
 *
 * Note: "er_cache.lock" should have been acquired before calling this function.
 *       And this thread should not release the mutex before return.
 */
static ER_FMT *
er_find_fmt (int err_id, int num_args)
{
  const char *msg;
  ER_FMT *fmt;

  // *INDENT-OFF*
  // protect log file mutex
  std::unique_lock<std::mutex> log_msg_cache (er_Message_cache_mutex, std::defer_lock); // mutex is released on
                                                                                        // destructor
  // *INDENT-ON*

  if (err_id < ER_FAILED && err_id > ER_LAST_ERROR)
    {
      fmt = &er_Fmt_list[-err_id];
    }
  else
    {
      assert (0);
      fmt = &er_Fmt_list[-ER_FAILED];
    }

  if (er_Fmt_msg_fail_count > 0)
    {
      log_msg_cache.lock ();
    }

  if (fmt->fmt == NULL)
    {
      assert (er_Fmt_msg_fail_count > 0);

      msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -err_id);
      if (msg == NULL || msg[0] == '\0')
	{
	  er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_ER_LOG_MISSING_MSG], err_id);
	  msg = er_Cached_msg[ER_ER_MISSING_MSG];
	}

      fmt = er_create_fmt_msg (fmt, err_id, msg);

      if (fmt != NULL)
	{
	  /*
	   * Be sure that we have the same number of arguments before calling
	   * er_estimate_size().  Because it uses straight va_arg() and friends
	   * to grab its arguments, it is vulnerable to argument mismatches
	   * (e.g., too few arguments, ints in string positions, etc).  This
	   * won't save us when someone passes an integer argument where the
	   * format says to expect a string, but it will save us if someone
	   * just plain forgets how many args there are.
	   */
	  if (fmt->nspecs != num_args)
	    {
	      er_log_debug (ARG_FILE_LINE, er_Cached_msg[ER_LOG_SUSPECT_FMT], err_id);
	      er_internal_msg (fmt, err_id, ER_ER_SUBSTITUTE_MSG);
	    }
	}
      er_Fmt_msg_fail_count--;
    }

  return fmt;
}

static ER_FMT *
er_create_fmt_msg (ER_FMT * fmt, int err_id, const char *msg)
{
  std::size_t msg_length;

  msg_length = strlen (msg);

  fmt->fmt = (char *) ER_MALLOC (msg_length + 1);
  if (fmt->fmt == NULL)
    {
      er_internal_msg (fmt, err_id, ER_ER_MISSING_MSG);
      return NULL;
    }

  fmt->fmt_length = (int) msg_length;
  fmt->must_free = 1;

  strcpy (fmt->fmt, msg);

  /*
   * Now study the format specs and squirrel away info about them.
   */
  fmt->err_id = err_id;
  er_study_fmt (fmt);

  return fmt;
}

/*
 * er_init_fmt -
 *   return: none
 *   fmt(in/out):
 */
static void
er_init_fmt (ER_FMT * fmt)
{
  fmt->err_id = 0;
  fmt->fmt = NULL;
  fmt->fmt_length = 0;
  fmt->must_free = 0;
  fmt->nspecs = 0;
  fmt->spec_top = DIM (fmt->spec_buf);
  fmt->spec = fmt->spec_buf;
}

/*
 * er_clear_fmt -
 *   return: none
 *   fmt(in/out):
 */
static void
er_clear_fmt (ER_FMT * fmt)
{
  if (fmt->fmt && fmt->must_free)
    {
      free_and_init (fmt->fmt);
    }
  fmt->fmt = NULL;
  fmt->fmt_length = 0;
  fmt->must_free = 0;

  if (fmt->spec && fmt->spec != fmt->spec_buf)
    {
      free_and_init (fmt->spec);
    }
  fmt->spec = fmt->spec_buf;
  fmt->spec_top = DIM (fmt->spec_buf);
  fmt->nspecs = 0;
}

/*
 * er_internal_msg -
 *   return:
 *   fmt(in/out):
 *   code(in):
 *   msg_num(in):
 */
static void
er_internal_msg (ER_FMT * fmt, int code, int msg_num)
{
  er_clear_fmt (fmt);

  fmt->err_id = code;
  fmt->fmt = (char *) er_Cached_msg[msg_num];
  fmt->fmt_length = (int) strlen (fmt->fmt);
  fmt->must_free = 0;
}

/*
 * er_malloc_helper -
 *   return:
 *   size(in):
 *   file(in):
 *   line(in):
 */
static void *
er_malloc_helper (std::size_t size, const char *file, int line)
{
  void *mem;

  mem = malloc (size);
  if (mem == NULL)
    {
      er_emergency (file, line, er_Cached_msg[ER_ER_OUT_OF_MEMORY], size);
    }

  return mem;
}

/*
 * er_emergency - Does a poor man's sprintf()
 *                which understands only '%s' and '%d'
 *   return: none
 *   file(in):
 *   line(in):
 *   fmt(in):
 *   ...(in):
 *
 * Note: Do not malloc() any memory since this can be called
 *       from low-memory situations
 */
static void
er_emergency (const char *file, int line, const char *fmt, ...)
{
  va_list args;
  const char *str, *p, *q;
  int limit, span;
  char buf[32];

  // *INDENT-OFF*
  er_message &crt_error = context::get_thread_local_error ();
  // *INDENT-ON*

  crt_error.err_id = ER_GENERIC_ERROR;
  crt_error.severity = ER_ERROR_SEVERITY;
  crt_error.file_name = file;
  crt_error.line_no = line;

  /* it is assumed that default message buffer is big enough to hold this */
  sprintf (crt_error.msg_area, er_Cached_msg[ER_ER_HEADER], line);
  limit = (int) (crt_error.msg_area_size - strlen (crt_error.msg_area) - 1);

  va_start (args, fmt);

  p = fmt;
  crt_error.msg_area[0] = '\0';
  while ((q = strchr (p, '%')) && limit > 0)
    {
      /*
       * Copy the text between the last conversion spec and the next.
       */
      span = CAST_STRLEN (q - p);
      span = MIN (limit, span);
      strncat (crt_error.msg_area, p, span);
      p = q + 2;
      limit -= span;

      /*
       * Now convert and print the arg.
       */
      switch (q[1])
	{
	case 'd':
	  sprintf (buf, "%d", va_arg (args, int));
	  str = buf;
	  break;
	case 'l':
	  if (q[2] == 'd')
	    {
	      sprintf (buf, "%d", va_arg (args, int));
	      str = buf;
	    }
	  else
	    {
	      str = "???";
	    }
	  break;
	case 's':
	  str = va_arg (args, const char *);
	  if (str == NULL)
	    {
	      str = "(null)";
	    }
	  break;
	case '%':
	  str = "%";
	  break;
	default:
	  str = "???";
	  break;
	}
      strncat (crt_error.msg_area, str, limit);
      limit -= (int) strlen (str);
      limit = MAX (limit, 0);
    }

  va_end (args);

  /*
   * Now copy the message text following the last conversion spec,
   * making sure that we null-terminate the buffer (since strncat won't
   * do it if it reaches the end of the buffer).
   */
  strncat (crt_error.msg_area, p, limit);
  crt_error.msg_area[crt_error.msg_area_size - 1] = '\0';

  /* Now get it into the log. */
  er_log (crt_error.err_id);
}

/*
 * er_vsprintf -
 *   return:
 *   fmt(in/out):
 *   ap(in):
 */
static int
er_vsprintf (er_message * er_entry_p, ER_FMT * fmt, va_list * ap)
{
  const char *p;		/* The start of the current non-spec part of fmt */
  const char *q;		/* The start of the next conversion spec */
  char *s;			/* The end of the valid part of er_entry_p->msg_area */
  int n;			/* The va_list position of the current arg */
  int i;
  va_list args;

  assert (er_entry_p != NULL);

  /*
   *                  *** WARNING ***
   *
   * This routine assumes that er_entry_p->msg_area is large enough to
   * receive the message being formatted.  If you haven't done your
   * homework with er_estimate_size() before calling this, you may be
   * in for a bruising.
   */

  /*
   * If there was trouble with the format for some reason, print out
   * something that seems a little reassuring.
   */
  if (fmt == NULL || fmt->fmt == NULL)
    {
      strncpy (er_entry_p->msg_area, er_Cached_msg[ER_ER_SUBSTITUTE_MSG], er_entry_p->msg_area_size);
      return ER_FAILED;
    }

  memcpy (&args, ap, sizeof (args));

  /*
   * Make room for the args that we are about to print.  These have to
   * be snatched from the va_list in the proper order and stored in an
   * array so that we can have random access to them in order to
   * support the %<num>$<code> notation in the message, that is, when
   * we're printing the format, we may not have the luxury of printing
   * the arguments in the same order that they appear in the va_list.
   */
  if (er_entry_p->nargs < fmt->nspecs)
    {
      int size;

      er_entry_p->clear_args ();

      size = fmt->nspecs * sizeof (er_va_arg);
      er_entry_p->args = (er_va_arg *) ER_MALLOC (size);
      if (er_entry_p->args == NULL)
	{
	  return ER_FAILED;
	}
      er_entry_p->nargs = fmt->nspecs;
    }

  /*
   * Now grab the args and put them in er_msg->args.  The work that we
   * did earlier in er_study_fmt() tells us what the base type of each
   * va_list item is, and we use that info here.
   */
  for (i = 0; i < fmt->nspecs; i++)
    {
      switch (fmt->spec[i].code)
	{
	case 'i':
	  er_entry_p->args[i].int_value = va_arg (args, int);
	  break;
	case SPEC_CODE_LONGLONG:
	  er_entry_p->args[i].longlong_value = va_arg (args, long long);
	  break;
	case SPEC_CODE_SIZE_T:
	  if (sizeof (size_t) == sizeof (long long int))
	    {
	      er_entry_p->args[i].longlong_value = va_arg (args, long long);
	    }
	  else
	    {
	      er_entry_p->args[i].int_value = va_arg (args, int);
	    }
	  break;
	case 'p':
	  er_entry_p->args[i].pointer_value = va_arg (args, void *);
	  break;
	case 'f':
	  er_entry_p->args[i].double_value = va_arg (args, double);
	  break;
	case 'L':
	  er_entry_p->args[i].longdouble_value = va_arg (args, long double);
	  break;
	case 's':
	  er_entry_p->args[i].string_value = va_arg (args, char *);
	  if (er_entry_p->args[i].string_value == NULL)
	    {
	      er_entry_p->args[i].string_value = "(null)";
	    }
	  break;
	default:
	  /*
	   * There should be no way to get in here with any other code;
	   * er_study_fmt() should have protected us from that.  If
	   * we're here, it's likely that memory has been corrupted.
	   */
	  er_emergency (__FILE__, __LINE__, er_Cached_msg[ER_LOG_UNKNOWN_CODE], fmt->spec[i].code);
	  return ER_FAILED;
	}
    }

  /*
   * Now do the printing.  Use sprintf to do the actual formatting,
   * this time using the simplified conversion specs we saved during
   * er_study_fmt().  This frees us from relying on sprintf (or
   * vsprintf) actually implementing the %<num>$<code> feature, which
   * is evidently unimplemented on some platforms (Sun ANSI C, at
   * least).
   */

  p = fmt->fmt;
  q = p;
  s = er_entry_p->msg_area;
  i = 0;
  while ((q = strchr (p, '%')))
    {
      /*
       * Copy the text between the last conversion spec and the next
       * and then advance the pointers.
       */
      strncpy (s, p, q - p);
      s += q - p;
      p = q;
      q += 1;

      if (q[0] == '%')
	{
	  *s++ = '%';
	  p = q + 2;
	  i += 1;
	  continue;
	}

      /*
       * See if we've got a position specifier; it will look like a
       * sequence of digits followed by a '$'.  Anything else is
       * assumed to be part of a conversion spec.  If there is no
       * explicit position specifier, we use the current loop index as
       * the position specifier.
       */
      n = 0;
      while (char_isdigit (*q))
	{
	  n *= 10;
	  n += (*q) - '0';
	  q += 1;
	}
      n = (*q == '$' && n) ? (n - 1) : i;

      /*
       * Format the specified argument using the simplified
       * (non-positional) conversion spec that we produced earlier.
       */
      switch (fmt->spec[n].code)
	{
	case 'i':
	  sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].int_value);
	  break;
	case SPEC_CODE_LONGLONG:
	  sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].longlong_value);
	  break;
	case SPEC_CODE_SIZE_T:
	  if (sizeof (size_t) == sizeof (long long int))
	    {
	      sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].longlong_value);
	    }
	  else
	    {
	      sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].int_value);
	    }
	  break;
	case 'p':
	  sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].pointer_value);
	  break;
	case 'f':
	  sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].double_value);
	  break;
	case 'L':
	  sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].longdouble_value);
	  break;
	case 's':
	  sprintf (s, fmt->spec[n].spec, er_entry_p->args[n].string_value);
	  break;
	default:
	  /*
	   * Can't get here.
	   */
	  break;
	}

      /*
       * Advance the pointers.  The conversion spec has to end with one
       * of the characters in the strcspn() argument, and none of
       * those characters can appear before the end of the spec.
       */
      s += strlen (s);
      p += strcspn (p, "cdiopsuxXefgEFG") + 1;
      i += 1;
    }

  /*
   * And get the last part of the fmt string after the last conversion
   * spec...
   */
  strcpy (s, p);
  s[strlen (p)] = '\0';

  return NO_ERROR;
}

static bool
er_is_error_severity (er_severity severity)
{
  switch (severity)
    {
    case ER_FATAL_ERROR_SEVERITY:
    case ER_ERROR_SEVERITY:
    case ER_SYNTAX_ERROR_SEVERITY:
      return true;
    case ER_WARNING_SEVERITY:
    case ER_NOTIFICATION_SEVERITY:
      return false;
    default:
      assert (false);
      return false;
    }
}

#if defined (CS_MODE)
void
er_set_ignore_uninit (bool ignore)
{
  er_Ignore_uninit = ignore;
}
#endif

/* *INDENT-OFF* */
namespace cuberr
{
  manager::manager (const char * msg_file, er_exit_ask exit_arg)
  {
    if (er_init (msg_file, exit_arg) != NO_ERROR)
      {
        assert_release (false);
      }
  }

  manager::~manager (void)
  {
    er_final (ER_ALL_FINAL);
  }
} // namespace cuberr
/* *INDENT-ON* */
