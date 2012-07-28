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
 * error_manager.c - Error module (both client & server)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <assert.h>

#if defined (SERVER_MODE) && !defined (WINDOWS)
#include <pthread.h>
#endif /* SERVER_MODE && !WINDOWS */

#if defined (WINDOWS)
#include <process.h>
#include <io.h>
#else /* WINDOWS */
#include <sys/time.h>
#include <unistd.h>
#include <sys/time.h>
#include <syslog.h>
#endif /* WINDOWS */

#if !defined(CS_MODE)
#include <sys/types.h>
#include <sys/stat.h>
#endif /* !CS_MODE */

#if defined (SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */

#include "porting.h"
#include "chartype.h"
#include "language_support.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "object_representation.h"
#include "message_catalog.h"
#include "release_string.h"
#include "environment_variable.h"
#if defined (SERVER_MODE)
#include "thread.h"
#else /* SERVER_MODE */
#include "transaction_cl.h"
#endif /* !SERVER_MODE */
#include "critical_section.h"
#include "error_manager.h"
#include "error_code.h"
#include "memory_hash.h"
#include "stack_dump.h"
#include "log_impl.h"

#if defined (WINDOWS)
#include "wintcp.h"
#endif /* WINDOWS */

#if defined(WINDOWS)
#define LOG_ALERT 0
static int
syslog (long priority, const char *message, ...)
{
  return 0;
}
#endif

#if defined (SERVER_MODE)
#define ER_CSECT_ENTER_LOG_FILE() \
    (csect_enter (NULL, CSECT_ER_LOG_FILE, INF_WAIT))
#define ER_CSECT_EXIT_LOG_FILE() \
    (csect_exit (CSECT_ER_LOG_FILE))
#elif defined (CS_MODE)
static pthread_mutex_t er_log_file_mutex = PTHREAD_MUTEX_INITIALIZER;
static int er_csect_enter_log_file (void);

#define ER_CSECT_ENTER_LOG_FILE() \
   er_csect_enter_log_file()
#define ER_CSECT_EXIT_LOG_FILE() \
   pthread_mutex_unlock (&er_log_file_mutex)
#else /* SA_MODE */
#define ER_CSECT_ENTER_LOG_FILE() NO_ERROR
#define ER_CSECT_EXIT_LOG_FILE()
#endif
/*
 * These are done via complied constants rather than the message
 * catalog, because they must be avilable if the message catalog is not
 * available.
 */
static const char *er_severity_string[] =
  { "FATAL ERROR", "ERROR", "SYNTAX ERROR", "WARNING", "NOTIFICATION" };

static const char *er_unknown_severity = "Unknown severity level";

#define ER_SEVERITY_STRING(severity) \
    ( ( ((severity) >= ER_FATAL_ERROR_SEVERITY) && \
        ((severity) <= ER_MAX_SEVERITY) ) ? \
     er_severity_string[severity] : er_unknown_severity )

#define ER_ERROR_WARNING_STRING(severity) \
    ( ( ((severity) >= ER_FATAL_ERROR_SEVERITY) && \
        ((severity) < ER_WARNING_SEVERITY) ) ? \
     er_severity_string[ER_ERROR_SEVERITY] : "" )


/*
 * Message sets within the er.msg catalog.  Set #1 comprises the system
 * error messages proper, while set #2 comprises the specific messages
 * that the er_ module uses itself.
 */
#define ER_MSG_SET		1
#define ER_INTERNAL_MSG_SET	2

#define PRM_ER_MSGLEVEL         0

#define ER_MALLOC(size)	er_malloc_helper((size), __FILE__, __LINE__)

#define SPEC_CODE_LONGLONG ((char)0x88)

/*
 * Use this macro for freeing msg_area things so that you don't forget
 * and someday accidentally try to free the er_emergency_buf.
 */
#if defined (SERVER_MODE)
#define ER_FREE_AREA(area, th_entry) \
    do { \
      if ((area) && (area) != th_entry->er_emergency_buf) \
  free((area)); \
    } while(0)
#else /* SERVER_MODE */
#define ER_FREE_AREA(area) \
    do { \
      if ((area) && (area) != er_emergency_buf) \
  free((area)); \
    } while(0)
#endif /* SERVER_MODE */

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

static const char *er_builtin_msg[] = {
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
  "er_start: *** WARNING: Unable to open message log \"%s\"; will assume stderr instead. ***\n",
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
static char *er_cached_msg[sizeof (er_builtin_msg) / sizeof (const char *)];
static bool er_is_cached_msg = false;

/* Error log message file related */
static char er_Log_file_name[PATH_MAX];
static const char *er_Msglog_file_name = NULL;
static FILE *er_Msglog = NULL;

static ER_FMT er_fmt_list[(-ER_LAST_ERROR) + 1];
static int er_fmt_msg_fail_count = -ER_LAST_ERROR;
#if !defined (SERVER_MODE)
static ER_MSG ermsg = { 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0 };
static ER_MSG *er_Msg = NULL;
static char er_emergency_buf[256];	/* message when all else fails */
static er_log_handler_t er_Handler = NULL;
#endif /* !SERVER_MODE */
static unsigned int er_Eid = 0;

/* Event handler related */
static FILE *er_Event_pipe = NULL;
static bool er_event_started = false;
static jmp_buf er_Event_jmp_buf;
static SIGNAL_HANDLER_FUNCTION saved_sig_handler;

/* Other supporting global variables */
static bool er_hasalready_initiated = false;
static bool er_isa_null_device = false;
static int er_Exit_ask = ER_EXIT_DEFAULT;

static void er_event_sigpipe_handler (int sig);
static void er_event (void);
static int er_event_init (void);
static void er_event_final (void);

static FILE *er_file_open (const char *path);
static bool er_file_isa_null_device (const char *path);
static FILE *er_file_backup (FILE * fp, const char *path);

#if defined (SERVER_MODE)
static bool logfile_opened = false;

static int er_start (THREAD_ENTRY * th_entry);
#else /* SERVER_MODE */
static int er_start (void);
#endif /* SERVER_MODE */

static void er_call_stack_dump_on_error (int severity, int err_id);
static void er_notify_event_on_error (int err_id);
static int er_set_internal (int severity, const char *file_name,
			    const int line_no, int err_id, int num_args,
			    bool include_os_error, FILE * fp,
			    va_list * ap_ptr);
static void er_log (void);
static void er_stop_on_error (void);

static int er_study_spec (const char *conversion_spec, char *simple_spec,
			  int *position, int *width, int *va_class);
static void er_study_fmt (ER_FMT * fmt);
static size_t er_estimate_size (ER_FMT * fmt, va_list * ap);
static ER_FMT *er_find_fmt (int err_id, int num_args);
static void er_init_fmt (ER_FMT * fmt);
static ER_FMT *er_create_fmt_msg (ER_FMT * fmt, int err_id, const char *msg);
static void er_clear_fmt (ER_FMT * fmt);
#if defined (SERVER_MODE)
static int er_make_room (int size, THREAD_ENTRY * th_entry);
#else /* SERVER_MODE */
static int er_make_room (int size);
#endif /* SERVER_MODE */
static void er_internal_msg (ER_FMT * fmt, int code, int msg_num);
static void *er_malloc_helper (int size, const char *file, int line);
static void er_emergency (const char *file, int line, const char *fmt, ...);
static int er_vsprintf (ER_FMT * fmt, va_list * ap);

static int er_call_stack_init (void);
static int er_fname_free (const void *key, void *data, void *args);
static void er_call_stack_final (void);

/* vector of functions to call when an error is set */
static PTR_FNERLOG er_Fnlog[ER_MAX_SEVERITY + 1] = {
  er_log,			/* ER_FATAL_ERROR_SEVERITY */
  er_log,			/* ER_ERROR_SEVERITY */
  er_log,			/* ER_SYNTAX_ERROR_SEVERITY */
  er_log,			/* ER_WARNING_SEVERITY */
  er_log			/* ER_NOTIFICATION_SEVERITY */
};

/*
 * er_msglog_file_name - Find the error message log file name
 *   return: log file name
 */
const char *
er_msglog_filename (void)
{
  return er_Msglog_file_name;
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

  if (er_Event_pipe == NULL || er_event_started == false)
    {
      return;
    }

  /* Get the most detailed error message available */
  er_all (&err_id, &severity, &nlevels, &line_no, &file_name, &msg);

#if !defined(WINDOWS)
  saved_sig_handler =
    os_set_signal_handler (SIGPIPE, er_event_sigpipe_handler);
#endif /* not WINDOWS */
  if (_setjmp (er_Event_jmp_buf) == 0)
    {
      fprintf (er_Event_pipe, "%d %s %s\n", err_id,
	       ER_SEVERITY_STRING (severity), msg);
      fflush (er_Event_pipe);
    }
  else
    {
      er_event_started = false;
      if (er_hasalready_initiated)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_BROKEN_PIPE, 1,
		  prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
      if (er_Event_pipe != NULL)
	{
	  fclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
    }
#if !defined(WINDOWS)
  os_set_signal_handler (SIGPIPE, saved_sig_handler);
#endif /* not WINDOWS */
}

/*
 * er_evnet_init
 */
static int
er_event_init (void)
{
  int error = NO_ERROR;
  const char *msg;

#if !defined(WINDOWS)
  saved_sig_handler =
    os_set_signal_handler (SIGPIPE, er_event_sigpipe_handler);
#endif /* not WINDOWS */
  if (_setjmp (er_Event_jmp_buf) == 0)
    {
      er_event_started = false;
      if (er_Event_pipe != NULL)
	{
	  if (er_hasalready_initiated)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_EV_STOPPED,
		      0);
	    }
	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR,
				-ER_EV_STOPPED);
	  fprintf (er_Event_pipe, "%d %s %s", ER_EV_STOPPED,
		   ER_SEVERITY_STRING (ER_NOTIFICATION_SEVERITY),
		   (msg ? msg : "?"));

	  fflush (er_Event_pipe);
	  pclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}

      er_Event_pipe =
	popen (prm_get_string_value (PRM_ID_EVENT_HANDLER), "w");
      if (er_Event_pipe != NULL)
	{
	  if (er_hasalready_initiated)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_EV_STARTED,
		      0);
	    }
	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR,
				-ER_EV_STARTED);
	  fprintf (er_Event_pipe, "%d %s %s", ER_EV_STARTED,
		   ER_SEVERITY_STRING (ER_NOTIFICATION_SEVERITY),
		   (msg ? msg : "?"));

	  fflush (er_Event_pipe);
	  er_event_started = true;
	}
      else
	{
	  if (er_hasalready_initiated)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_CONNECT_HANDLER,
		      1, prm_get_string_value (PRM_ID_EVENT_HANDLER));
	    }
	  error = ER_EV_CONNECT_HANDLER;
	}
    }
  else
    {
      if (er_hasalready_initiated)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_BROKEN_PIPE, 1,
		  prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
      if (er_Event_pipe != NULL)
	{
	  fclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
      error = ER_EV_BROKEN_PIPE;
    }
#if !defined(WINDOWS)
  os_set_signal_handler (SIGPIPE, saved_sig_handler);
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
  saved_sig_handler =
    os_set_signal_handler (SIGPIPE, er_event_sigpipe_handler);
#endif /* not WINDOWS */
  if (_setjmp (er_Event_jmp_buf) == 0)
    {
      er_event_started = false;
      if (er_Event_pipe != NULL)
	{
	  if (er_hasalready_initiated)
	    {
	      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_EV_STOPPED,
		      0);
	    }
	  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR,
				-ER_EV_STOPPED);
	  fprintf (er_Event_pipe, "%d %s %s", ER_EV_STOPPED,
		   ER_SEVERITY_STRING (ER_NOTIFICATION_SEVERITY),
		   (msg ? msg : "?"));

	  fflush (er_Event_pipe);
	  pclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
    }
  else
    {
      if (er_hasalready_initiated)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_EV_BROKEN_PIPE, 1,
		  prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
      if (er_Event_pipe != NULL)
	{
	  fclose (er_Event_pipe);
	  er_Event_pipe = NULL;
	}
    }
#if !defined(WINDOWS)
  os_set_signal_handler (SIGPIPE, saved_sig_handler);
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
  fname_table = mht_create (0, 100, mht_5strhash,
			    mht_compare_strings_are_equal);
  if (fname_table == NULL)
    {
      return ER_FAILED;
    }
#endif

  return NO_ERROR;
}

/*
 * er_init - Initialize parameters for message module
 *   return: none
 *   msglog_file_name(in): name of message log file
 *   exit_ask(in): define behavior when a sever error is found
 *
 * Note: This function initializes parameters that define the behavior
 *       of the message module (i.e., logging file , and fatal error
 *       exit condition). If the value of msglog_file_name is NULL,
 *       error messages are logged/sent to PRM_ER_LOG_FILE. If that
 *       is null, then messages go to stderr. If msglog_file_name
 *       is equal to /dev/null, error messages are not logged.
 */
int
er_init (const char *msglog_file_name, int exit_ask)
{
  int i;
  int r;
  const char *msg;
  MSG_CATD msg_catd;

  if (er_hasalready_initiated)
    {
#if defined (SERVER_MODE)
      er_final (1);
#else
      er_final ();
#endif
    }

  for (i = 0; i < (int) DIM (er_builtin_msg); i++)
    {
      if (er_cached_msg[i] && er_cached_msg[i] != er_builtin_msg[i])
	{
	  free_and_init (er_cached_msg[i]);
	}
      er_cached_msg[i] = (char *) er_builtin_msg[i];
    }

  assert (DIM (er_fmt_list) > abs (ER_LAST_ERROR));

  for (i = 0; i < abs (ER_LAST_ERROR); i++)
    {
      er_init_fmt (&er_fmt_list[i]);
    }

  msg_catd = msgcat_get_descriptor (MSGCAT_CATALOG_CUBRID);
  if (msg_catd != NULL)
    {
      er_fmt_msg_fail_count = 0;
      for (i = 2; i < abs (ER_LAST_ERROR); i++)
	{
	  msg = msgcat_gets (msg_catd, MSGCAT_SET_ERROR, i, NULL);
	  if (msg == NULL || msg[0] == '\0')
	    {
	      er_fmt_msg_fail_count++;
	      continue;
	    }
	  else
	    {
	      if (er_create_fmt_msg (&er_fmt_list[i], -i, msg) == NULL)
		{
		  er_fmt_msg_fail_count++;
		}
	    }
	}
    }
  else
    {
      er_fmt_msg_fail_count = abs (ER_LAST_ERROR) - 2;
    }

  r = ER_CSECT_ENTER_LOG_FILE ();
  if (r != NO_ERROR)
    {
      return ER_FAILED;
    }

  switch (exit_ask)
    {
    case ER_EXIT_ASK:
    case ER_EXIT_DONT_ASK:
    case ER_NEVER_EXIT:
      er_Exit_ask = exit_ask;
      break;

    default:
      er_Exit_ask = ER_EXIT_DEFAULT;
      er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_ASK_VALUE], exit_ask,
		    ((er_Exit_ask == ER_EXIT_ASK) ?
		     "ER_EXIT_ASK" : "ER_NEVER_EXIT"));
      break;
    }

  /*
   * Remember the name of the message log file
   */
  if (msglog_file_name == NULL)
    {
      if (prm_get_string_value (PRM_ID_ER_LOG_FILE))
	{
	  msglog_file_name = prm_get_string_value (PRM_ID_ER_LOG_FILE);
	}
    }
  if (msglog_file_name)
    {
      if (IS_ABS_PATH (msglog_file_name)
	  || msglog_file_name[0] == PATH_CURRENT)
	{
	  strncpy (er_Log_file_name, msglog_file_name, PATH_MAX - 1);
	}
      else
	{
	  envvar_logdir_file (er_Log_file_name, PATH_MAX, msglog_file_name);
	}
      er_Log_file_name[PATH_MAX - 1] = '\0';
      er_Msglog_file_name = er_Log_file_name;
    }
  else
    {
      er_Msglog_file_name = NULL;
    }

  er_hasalready_initiated = true;

  /*
   * Install event handler
   */
  if (prm_get_string_value (PRM_ID_EVENT_HANDLER) != NULL
      && *prm_get_string_value (PRM_ID_EVENT_HANDLER) != '\0')
    {
      if (er_event_init () != NO_ERROR)
	{
	  er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_EVENT_HANDLER],
			prm_get_string_value (PRM_ID_EVENT_HANDLER));
	}
    }

  ER_CSECT_EXIT_LOG_FILE ();

  if (er_call_stack_init () != NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
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
      if (dirname_r (tpath, dir, PATH_MAX) > 0 && access (dir, F_OK) < 0)
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

  /* note: in "a+" mode, output is alwarys appended */
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

#if defined (SERVER_MODE)
/*
 * er_start - Start the error message module
 *   return: NO_ERROR or ER_FAILED
 *   th_entry(in):
 *
 * Note: Error message areas are initialized, the text message and the
 *       log message files are opened. In addition, the behavior of the
 *       system when a fatal error condition is set, is defined.
 */
static int
er_start (THREAD_ENTRY * th_entry)
{
  int status = NO_ERROR;
  int r;
  int i;

  if (er_hasalready_initiated == false)
    {
      (void) er_init (prm_get_string_value (PRM_ID_ER_LOG_FILE),
		      prm_get_integer_value (PRM_ID_ER_EXIT_ASK));
      if (er_hasalready_initiated == false)
	{
	  return ER_FAILED;
	}
    }
  if (th_entry->er_Msg != NULL)
    {
      fprintf (stderr, er_cached_msg[ER_ER_HEADER], __LINE__);
      fflush (stderr);
      er_final (0);
    }
  memset (&th_entry->ermsg, 0, sizeof (ER_MSG));

  th_entry->er_Msg = &th_entry->ermsg;
  th_entry->er_Msg->err_id = NO_ERROR;
  th_entry->er_Msg->severity = ER_WARNING_SEVERITY;
  th_entry->er_Msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
  th_entry->er_Msg->line_no = -1;
  th_entry->er_Msg->stack = NULL;
  th_entry->er_Msg->msg_area = th_entry->er_emergency_buf;
  th_entry->er_Msg->msg_area_size = sizeof (th_entry->er_emergency_buf);
  th_entry->er_Msg->args = NULL;
  th_entry->er_Msg->nargs = 0;

  r = ER_CSECT_ENTER_LOG_FILE ();
  if (r != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* Define message log file */
  if (logfile_opened == false)
    {
      if (er_Msglog_file_name)
	{
	  er_isa_null_device = er_file_isa_null_device (er_Msglog_file_name);

	  if (er_isa_null_device
	      || prm_get_bool_value (PRM_ID_ER_PRODUCTION_MODE))
	    {
	      er_Msglog = er_file_open (er_Msglog_file_name);
	    }
	  else
	    {
	      /* want to err on the side of doing production style error logs
	       * because this may be getting set at some naive customer site.*/
	      char path[PATH_MAX];
	      sprintf (path, "%s.%d", er_Msglog_file_name, getpid ());
	      er_Msglog = er_file_open (path);
	    }
	  if (er_Msglog == NULL)
	    {
	      er_Msglog = stderr;
	      er_log_debug (ARG_FILE_LINE,
			    er_cached_msg[ER_LOG_MSGLOG_WARNING],
			    er_Msglog_file_name);
	    }
	}
      else
	{
	  er_Msglog = stderr;
	}
      logfile_opened = true;
    }

  /*
   * Message catalog may be initialized by msgcat_init() during bootstrap.
   * But, try once more to call msgcat_init() because there could be
   * an exception case that get here before bootstrap.
   */
  if (msgcat_init () != NO_ERROR)
    {
      er_emergency (__FILE__, __LINE__, er_cached_msg[ER_ER_NO_CATALOG]);
      status = ER_FAILED;
    }
  else if (er_is_cached_msg == false)
    {
      /* cache the messages */

      /*
       * Remember, we skip code 0.  If we can't find enough memory to
       * copy the silly message, things are going to be pretty tight
       * anyway, so just use the default version.
       */
      for (i = 1; i < (int) DIM (er_cached_msg); i++)
	{
	  const char *msg;
	  char *tmp;

	  msg =
	    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_INTERNAL, i);
	  if (msg && *msg)
	    {
	      tmp = (char *) malloc (strlen (msg) + 1);
	      if (tmp)
		{
		  strcpy (tmp, msg);
		  er_cached_msg[i] = tmp;
		}
	    }
	}

      er_is_cached_msg = true;
    }

  r = ER_CSECT_EXIT_LOG_FILE ();
  return status;
}

#else /* SERVER_MODE */

/*
 * er_start - Start the error message module
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: Error message areas are initialized, the text message and the
 *       log message files are opened. In addition, the behavior of the
 *       system when a fatal error condition is set, is defined.
 */
static int
er_start (void)
{
  int status = NO_ERROR;
  int i;

  /* Make sure that the behavior of the module has been defined */

  if (er_hasalready_initiated == false)
    {
      (void) er_init (prm_get_string_value (PRM_ID_ER_LOG_FILE),
		      prm_get_integer_value (PRM_ID_ER_EXIT_ASK));
    }

  /*
   * If the error module has already been initialized, shutted down
   * before it is reinitialized again.
   */

  if (er_Msg != NULL)
    {
      er_final ();
    }

  er_Msg = &ermsg;
  er_Msg->err_id = NO_ERROR;
  er_Msg->severity = ER_WARNING_SEVERITY;
  er_Msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
  er_Msg->line_no = -1;
  er_Msg->stack = NULL;
  er_Msg->msg_area = er_emergency_buf;
  er_Msg->msg_area_size = sizeof (er_emergency_buf);
  er_Msg->args = NULL;
  er_Msg->nargs = 0;

  /* Define message log file */

  if (er_Msglog_file_name)
    {
      er_isa_null_device = er_file_isa_null_device (er_Msglog_file_name);

      if (er_isa_null_device
	  || prm_get_bool_value (PRM_ID_ER_PRODUCTION_MODE))
	{
	  er_Msglog = er_file_open (er_Msglog_file_name);
	}
      else
	{
	  /* want to err on the side of doing production style error logs
	   * because this may be getting set at some naive customer site. */
	  char path[PATH_MAX];
	  sprintf (path, "%s.%d", er_Msglog_file_name, getpid ());
	  er_Msglog = er_file_open (path);
	}

      if (er_Msglog == NULL)
	{
	  er_Msglog = stderr;
	  er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_MSGLOG_WARNING],
			er_Msglog_file_name);
	}
    }
  else
    {
      er_Msglog = stderr;
    }

  /*
   * Message catalog may be initialized by msgcat_init() during bootstrap.
   * But, try once more to call msgcat_init() because there could be
   * an exception case that get here before bootstrap.
   */
  if (msgcat_init () != NO_ERROR)
    {
      er_emergency (__FILE__, __LINE__, er_cached_msg[ER_ER_NO_CATALOG]);
      status = ER_FAILED;
    }
  else if (er_is_cached_msg == false)
    {
      /* cache the messages */

      /*
       * Remember, we skip code 0.  If we can't find enough memory to
       * copy the silly message, things are going to be pretty tight
       * anyway, so just use the default version.
       */
      for (i = 1; i < (int) DIM (er_cached_msg); i++)
	{
	  const char *msg;
	  char *tmp;

	  msg =
	    msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_INTERNAL, i);
	  if (msg && *msg)
	    {
	      tmp = (char *) malloc (strlen (msg) + 1);
	      if (tmp)
		{
		  strcpy (tmp, msg);
		  er_cached_msg[i] = tmp;
		}
	    }
	}

      er_is_cached_msg = true;
    }

  return status;
}
#endif /* SERVER_MODE */


#if defined (SERVER_MODE)

/*
 * er_fname_free -
 *   return: error code
 */
static int
er_fname_free (const void *key, void *data, void *args)
{
  free (key);
  free (data);

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
 * er_final - Terminate the error message module
 *   return: none
 *   do_global_final(in):
 */
void
er_final (bool do_global_final)
{
  if (do_global_final == false)
    {
      THREAD_ENTRY *th_entry;

      th_entry = thread_get_thread_entry_info ();
      if (th_entry == NULL)
	{
	  return;
	}

      if (th_entry->er_Msg != NULL)
	{
	  er_stack_clearall ();
	}

      if (th_entry->er_Msg)
	{
	  ER_FREE_AREA (th_entry->er_Msg->msg_area, th_entry);
	  th_entry->er_Msg->msg_area = th_entry->er_emergency_buf;
	  th_entry->er_Msg->msg_area_size =
	    sizeof (th_entry->er_emergency_buf);
	  if (th_entry->er_Msg->args)
	    {
	      free_and_init (th_entry->er_Msg->args);
	    }
	  th_entry->er_Msg->nargs = 0;

	  th_entry->er_Msg = NULL;
	}
    }
  else
    {
      int i;

      if (ER_CSECT_ENTER_LOG_FILE () != NO_ERROR)
	{
	  assert (false);
	  return;
	}

      er_event_final ();

      if (logfile_opened == true)
	{
	  if (er_Msglog != NULL && er_Msglog != stderr)
	    {
	      (void) fclose (er_Msglog);
	      er_Msglog = NULL;
	    }
	  logfile_opened = false;
	}

      for (i = 0; i < (int) DIM (er_fmt_list); i++)
	{
	  er_clear_fmt (&er_fmt_list[i]);
	}

      for (i = 0; i < (int) DIM (er_cached_msg); i++)
	{
	  if (er_cached_msg[i] != er_builtin_msg[i])
	    {
	      free_and_init (er_cached_msg[i]);
	      er_cached_msg[i] = (char *) er_builtin_msg[i];
	    }
	}

      er_hasalready_initiated = false;
      ER_CSECT_EXIT_LOG_FILE ();
    }

  er_call_stack_final ();
}

#else /* SERVER_MODE */

/*
 * er_final - Terminate the error message module
 *   return: none
 */
void
er_final (void)
{
  int i;

  er_event_final ();

  if (er_Msg != NULL)
    {
      er_stack_clear ();
      if (er_Msglog != NULL && er_Msglog != stderr)
	{
	  (void) fclose (er_Msglog);
	  er_Msglog = NULL;
	}
      ER_FREE_AREA (er_Msg->msg_area);
      er_Msg->msg_area = er_emergency_buf;
      er_Msg->msg_area_size = sizeof (er_emergency_buf);
      if (er_Msg->args)
	{
	  free_and_init (er_Msg->args);
	}
      er_Msg->nargs = 0;

      for (i = 0; i < (int) DIM (er_fmt_list); i++)
	{
	  er_clear_fmt (&er_fmt_list[i]);
	}

      er_Msg = NULL;
    }

  for (i = 0; i < (int) DIM (er_cached_msg); i++)
    {
      if (er_cached_msg[i] != er_builtin_msg[i])
	{
	  free_and_init (er_cached_msg[i]);
	  er_cached_msg[i] = (char *) er_builtin_msg[i];
	}
    }

  er_hasalready_initiated = false;
}
#endif /* SERVER_MODE */

/*
 * er_clear - Clear any error message
 *   return: none
 *
 * Note: This function is used to ignore an occurred error.
 */
void
er_clear (void)
{
  char *buf;
  int size;
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  if (th_entry->er_Msg == NULL)
    {
      (void) er_start (th_entry);
    }

  th_entry->er_Msg->err_id = NO_ERROR;
  th_entry->er_Msg->severity = ER_WARNING_SEVERITY;
  th_entry->er_Msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
  th_entry->er_Msg->line_no = -1;
  buf = th_entry->er_Msg->msg_area;
  size = th_entry->er_Msg->msg_area_size;
#else /* SERVER_MODE */
  if (er_Msg == NULL)
    {
      (void) er_start ();
    }

  er_Msg->err_id = NO_ERROR;
  er_Msg->severity = ER_WARNING_SEVERITY;
  er_Msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
  er_Msg->line_no = -1;
  buf = er_Msg->msg_area;
  size = er_Msg->msg_area_size;
#endif /* SERVER_MODE */

  if (buf)
    {
      buf[0] = '\0';
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * er_fnerlog - Reset log error function
 *   return:
 *   severity(in): Severity of log function to reset
 *   new_fnlog(in):
 *
 * Note: Reset the log error function for the given severity. This
 *       function is called when an error of this severity is set.
 */
PTR_FNERLOG
er_fnerlog (int severity, PTR_FNERLOG new_fnlog)
{
  PTR_FNERLOG old_fnlog;

  if (severity < 0 || severity > ER_MAX_SEVERITY)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }
  old_fnlog = er_Fnlog[severity];
  er_Fnlog[severity] = new_fnlog;
  return old_fnlog;

}
#endif

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
er_set (int severity, const char *file_name, const int line_no, int err_id,
	int num_args, ...)
{
  va_list ap;

  va_start (ap, num_args);
  (void) er_set_internal (severity, file_name, line_no, err_id, num_args,
			  false, NULL, &ap);
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
er_set_with_file (int severity, const char *file_name, const int line_no,
		  int err_id, FILE * fp, int num_args, ...)
{
  va_list ap;

  va_start (ap, num_args);
  (void) er_set_internal (severity, file_name, line_no, err_id, num_args,
			  false, fp, &ap);
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
er_set_with_oserror (int severity, const char *file_name, const int line_no,
		     int err_id, int num_args, ...)
{
  va_list ap;

  va_start (ap, num_args);
  (void) er_set_internal (severity, file_name, line_no, err_id, num_args,
			  true, NULL, &ap);
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
  if (prm_get_error_list_value (PRM_ID_EVENT_ACTIVATION))
    {
      if (prm_get_error_list_value (PRM_ID_EVENT_ACTIVATION)[err_id] == true)
	{
	  er_event ();
	}
    }
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
      er_dump_call_stack (er_Msglog);
    }
  else if (prm_get_bool_value (PRM_ID_CALL_STACK_DUMP_ON_ERROR))
    {
      if (prm_get_error_list_value (PRM_ID_CALL_STACK_DUMP_DEACTIVATION))
	{
	  if (prm_get_error_list_value (PRM_ID_CALL_STACK_DUMP_DEACTIVATION)
	      [err_id] == false)
	    {
	      er_dump_call_stack (er_Msglog);
	    }
	}
      else
	{
	  er_dump_call_stack (er_Msglog);
	}
    }
  else
    {
      if (prm_get_error_list_value (PRM_ID_CALL_STACK_DUMP_ACTIVATION))
	{
	  if (prm_get_error_list_value (PRM_ID_CALL_STACK_DUMP_ACTIVATION)
	      [err_id] == true)
	    {
	      er_dump_call_stack (er_Msglog);
	    }
	}
    }
}

/*
 * er_set_internal - Set an error and an optionaly the Unix error
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
er_set_internal (int severity, const char *file_name, const int line_no,
		 int err_id, int num_args, bool include_os_error,
		 FILE * fp, va_list * ap_ptr)
{
  va_list ap;
  const char *os_error;
  size_t new_size;
  ER_FMT *er_fmt = NULL;
  int r;
  int ret_val = NO_ERROR;
  bool need_stack_pop = false;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry;
#endif

  if (er_hasalready_initiated == false)
    {
      return ER_FAILED;
    }

#if defined (SERVER_MODE)
  th_entry = thread_get_thread_entry_info ();
  er_Msg = th_entry->er_Msg;
#endif

  /*
   * Get the UNIX error message if needed. We need to get this as soon
   * as possible to avoid resetting the error.
   */
  os_error = (include_os_error && errno != 0) ? strerror (errno) : NULL;

  memcpy (&ap, ap_ptr, sizeof (ap));

#if defined (SERVER_MODE)
  if (er_Msg == NULL)
    {
      (void) er_start (th_entry);
      er_Msg = th_entry->er_Msg;
    }
#else /* SERVER_MODE */
  if (er_Msg == NULL)
    (void) er_start ();
#endif /* SERVER_MODE */

  if ((severity == ER_NOTIFICATION_SEVERITY) && (er_Msg->err_id != NO_ERROR))
    {
      er_stack_push ();
#if defined (SERVER_MODE)
      er_Msg = th_entry->er_Msg;
#endif
      need_stack_pop = true;
    }

  /* Initialize the area... */
  er_Msg->err_id = err_id;
  er_Msg->severity = severity;
  er_Msg->file_name = file_name;
  er_Msg->line_no = line_no;

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
#if defined (SERVER_MODE)
  if (er_make_room (new_size + 1, th_entry) == ER_FAILED)
    {
      ret_val = ER_FAILED;
      goto end;
    }
#else /* SERVER_MODE */
  if (er_make_room (new_size + 1) == ER_FAILED)
    {
      ret_val = ER_FAILED;
      goto end;
    }
#endif /* SERVER_MODE */

  /* And now format the silly thing. */
  if (er_vsprintf (er_fmt, ap_ptr) == ER_FAILED)
    {
      ret_val = ER_FAILED;
      goto end;
    }
  if (os_error)
    {
      strcat (er_Msg->msg_area, "... ");
      strcat (er_Msg->msg_area, os_error);
    }

  /* Call the logging function if any */
  if (severity <= prm_get_integer_value (PRM_ID_ER_LOG_LEVEL)
      && !(prm_get_bool_value (PRM_ID_ER_LOG_WARNING) == false
	   && severity == ER_WARNING_SEVERITY) && er_Fnlog[severity] != NULL)
    {
      r = ER_CSECT_ENTER_LOG_FILE ();
      if (r == NO_ERROR)
	{
	  (*er_Fnlog[severity]) ();

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
		      fprintf (er_Msglog, "%s", buf);
		    }
		  (void) fflush (er_Msglog);
		}
	    }
	  ER_CSECT_EXIT_LOG_FILE ();
	}
    }

  /*
   * Do we want to stop the system on this error ... for debugging
   * purposes?
   */
  if (prm_get_integer_value (PRM_ID_ER_STOP_ON_ERROR) == err_id)
    {
      er_stop_on_error ();
    }

  if (severity == ER_NOTIFICATION_SEVERITY)
    {
      er_Msg->err_id = NO_ERROR;
      er_Msg->severity = ER_WARNING_SEVERITY;
      er_Msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
      er_Msg->line_no = -1;
      if (er_Msg->msg_area != NULL)
	{
	  er_Msg->msg_area[0] = '\0';
	}
    }

end:

  if (need_stack_pop)
    {
      er_stack_pop ();
    }

  return ret_val;
}

/*
 * er_stop_on_error - Stop the sysem when an error occurs
 *   return: none
 *
 * Note: This feature can be used when debugging a particular error
 *       outside the debugger. The user is asked wheater or not to continue.
 */
static void
er_stop_on_error (void)
{
  int exit_requested;

#if !defined (WINDOWS)
  syslog (LOG_ALERT, er_cached_msg[ER_STOP_SYSLOG],
	  rel_name (), er_errid (), cuserid (NULL), getpid ());
#endif /* !WINDOWS */

  (void) fprintf (stderr, "%s", er_cached_msg[ER_ER_ASK]);
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
 * Note: The maximum available level of the error is logged into
 *       er_Msglog file.This function is used at the default logging
 *       function to call when error are set. The calling logging
 *       function can be modified by calling the function er_fnerlog.
 */
static void
er_log (void)
{
  int err_id;
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
  char more_info[MAXHOSTNAMELEN + PATH_MAX + 64];

#if defined (WINDOWS)
  /* Check for an invalid output file    */
  /* (stderr is not valid under Windows and is set to NULL) */
  if (er_Msglog == NULL)
    {
      return;
    }
#endif /* WINDOWS */

  /* Make sure that we have a valid error identifier */

  /* Get the most detailed error message available */
  er_all (&err_id, &severity, &nlevels, &line_no, &file_name, &msg);

  /*
   * Don't let the file of log messages get very long. Backup or go back to the
   * top if need be.
   */

  if (er_Msglog != stderr && er_Msglog != stdout
      && ftell (er_Msglog) > (int) prm_get_integer_value (PRM_ID_ER_LOG_SIZE))
    {
      (void) fflush (er_Msglog);
      (void) fprintf (er_Msglog, "%s", er_cached_msg[ER_LOG_WRAPAROUND]);

      if (!er_isa_null_device)
	{
	  er_Msglog = er_file_backup (er_Msglog, er_Msglog_file_name);

	  if (er_Msglog == NULL)
	    {
	      er_Msglog = stderr;
	      er_log_debug (ARG_FILE_LINE,
			    er_cached_msg[ER_LOG_MSGLOG_WARNING],
			    er_Msglog_file_name);
	    }
	}
      else
	{
	  /* Should be rewinded to avoid repeated limit check hitting */
	  (void) fseek (er_Msglog, 0L, SEEK_SET);
	}
    }

  if (er_Msglog == stderr || er_Msglog == stdout)
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
#if defined (SERVER_MODE) && !defined (WINDOWS)
  er_tm_p = localtime_r (&er_time, &er_tm);
#else /* SERVER_MODE && !WINDOWS */
  er_tm_p = localtime (&er_time);
#endif /* SERVER_MODE && !WINDOWS */
  if (er_tm_p == NULL)
    {
      strcpy (time_array, "00/00/00 00:00:00.000");
    }
  else
    {
      gettimeofday (&tv, NULL);
      snprintf (time_array +
		strftime (time_array, 128, "%m/%d/%y %H:%M:%S", er_tm_p), 255,
		".%03ld", tv.tv_usec / 1000);
    }

  more_info_p = (char *) "";

  if (++er_Eid == 0)
    {
      er_Eid = 1;
    }

#if defined (SERVER_MODE)
  tran_index = thread_get_current_tran_index ();
  do
    {
      char *prog_name = NULL;
      char *user_name = NULL;
      char *host_name = NULL;
      int pid = 0;

      if (logtb_find_client_name_host_pid (tran_index, &prog_name,
					   &user_name, &host_name,
					   &pid) == NO_ERROR)
	{
	  ret = snprintf (more_info, sizeof (more_info),
			  ", CLIENT = %s:%s(%d), EID = %u",
			  host_name ? host_name : "unknown",
			  prog_name ? prog_name : "unknown", pid, er_Eid);
	  if (ret > 0)
	    {
	      more_info_p = &more_info[0];
	    }
	}

    }
  while (0);
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

  fprintf (er_Msglog, er_cached_msg[ER_LOG_MSG_WRAPPER_D], time_array,
	   ER_SEVERITY_STRING (severity), file_name, line_no,
	   ER_ERROR_WARNING_STRING (severity), err_id, tran_index,
	   more_info_p, msg);
  fflush (er_Msglog);

  /* Flush the message so it is printed immediately */
  (void) fflush (er_Msglog);

  if (er_Msglog != stderr || er_Msglog != stdout)
    {
      position = ftell (er_Msglog);
      (void) fprintf (er_Msglog, "%s", er_cached_msg[ER_LOG_LAST_MSG]);
      (void) fflush (er_Msglog);
      (void) fseek (er_Msglog, position, SEEK_SET);
    }

  /* Do we want to exit ? */

  if (severity == ER_FATAL_ERROR_SEVERITY)
    {
      switch (er_Exit_ask)
	{
	case ER_EXIT_ASK:
#if defined (NDEBUG)
	  er_stop_on_error ();
	  break;
#endif /* NDEBUG */

	case ER_EXIT_DONT_ASK:
	  (void) fprintf (er_Msglog, "%s", er_cached_msg[ER_ER_EXIT]);
	  (void) fflush (er_Msglog);
#if defined (SERVER_MODE)
	  er_final (1);
#else /* SERVER_MODE */
	  er_final ();
#endif /* SERVER_MODE */
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
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  return ((th_entry->er_Msg != NULL) ? th_entry->er_Msg->err_id : NO_ERROR);
#else /* SERVER_MODE */
  return ((er_Msg != NULL) ? er_Msg->err_id : NO_ERROR);
#endif /* SERVER_MODE */
}

/*
 * er_clearid - Clear only error identifier
 *   return: none
 */
void
er_clearid (void)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  if (th_entry->er_Msg != NULL)
    {
      th_entry->er_Msg->err_id = NO_ERROR;
    }
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    {
      er_Msg->err_id = NO_ERROR;
    }
#endif /* SERVER_MODE */
}

/*
 * er_setid - Set onlt an error identifier
 *   return: none
 *   err_id(in):
 */
void
er_setid (int err_id)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  if (th_entry->er_Msg != NULL)
    {
      th_entry->er_Msg->err_id = err_id;
    }
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    {
      er_Msg->err_id = err_id;
    }
#endif /* SERVER_MODE */
}

/*
 * er_severity - Get severity of the last error set before
 *   return: severity
 */
int
er_severity (void)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  return ((th_entry->er_Msg != NULL) ? th_entry->er_Msg->severity
	  : ER_WARNING_SEVERITY);
#else /* SERVER_MODE */
  return ((er_Msg != NULL) ? er_Msg->severity : ER_WARNING_SEVERITY);
#endif /* SERVER_MODE */
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * er_nlevels - Get number of levels of the last error
 *   return: number of levels
 */
int
er_nlevels (void)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  return th_entry->er_Msg ? 1 : 0;
#else /* SERVER_MODE */
  return er_Msg ? 1 : 0;
#endif /* SERVER_MODE */
}

/*
 * enclosing_method - Get file name and line number of the last error
 *   return: file name
 *   line_no(out): line number
 */
const char *
er_file_line (int *line_no)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  if (th_entry->er_Msg != NULL)
    {
      *line_no = th_entry->er_Msg->line_no;
      return th_entry->er_Msg->file_name;
    }
  else
    {
      *line_no = -1;
      return NULL;
    }
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    {
      *line_no = er_Msg->line_no;
      return er_Msg->file_name;
    }
  else
    {
      *line_no = -1;
      return NULL;
    }
#endif /* SERVER_MODE */
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * er_msg - Retrieve current error message
 *   return: a string, at the given level, describing the last error
 *
 * Note: The string returned is overwritten when the next error is set,
 *       so it may be necessary to copy it to a static area if you
 *       need to keep it for some length of time.
 */
const char *
er_msg ()
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  if (th_entry->er_Msg == NULL)
    {
      (void) er_clear ();
    }

  if (th_entry->er_Msg == NULL)
    {
      return NULL;
    }

  if (th_entry->er_Msg->msg_area[0] == '\0')
    {
      strncpy (th_entry->er_Msg->msg_area, er_cached_msg[ER_ER_MISSING_MSG],
	       th_entry->er_Msg->msg_area_size);
      th_entry->er_Msg->msg_area[th_entry->er_Msg->msg_area_size - 1] = '\0';
    }

  return th_entry->er_Msg->msg_area;
#else /* SERVER_MODE */
  if (er_Msg == NULL)
    {
      (void) er_clear ();
    }
  if (!er_Msg->msg_area[0])
    {
      strncpy (er_Msg->msg_area, er_cached_msg[ER_ER_MISSING_MSG],
	       er_Msg->msg_area_size);
      er_Msg->msg_area[er_Msg->msg_area_size - 1] = '\0';
    }

  return er_Msg->msg_area;
#endif /* SERVER_MODE */
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
er_all (int *err_id, int *severity, int *n_levels, int *line_no,
	const char **file_name, const char **error_msg)
{
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  if (er_Msg != NULL)
    {
      *err_id = er_Msg->err_id;
      *severity = er_Msg->severity;
      *n_levels = 1;
      *line_no = er_Msg->line_no;
      *file_name = er_Msg->file_name;
      *error_msg = er_msg ();
    }
  else
    {
      *err_id = NO_ERROR;
      *severity = ER_WARNING_SEVERITY;
      *n_levels = 0;
      *line_no = -1;
      *error_msg = NULL;
    }
}

/*
 * er_print - Print current error message to stdout
 *   return: none
 */
void
er_print (void)
{
  int err_id;
  int severity;
  int nlevels;
  int line_no;
  const char *file_name;
  const char *msg;
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  char time_array[256];
  char *time_array_p = time_array;
  int tran_index;

  er_all (&err_id, &severity, &nlevels, &line_no, &file_name, &msg);

  er_time = time (NULL);
#if defined (SERVER_MODE) && !defined (WINDOWS)
  er_tm_p = localtime_r (&er_time, &er_tm);
#else /* SERVER_MODE && !WINDOWS */
  er_tm_p = localtime (&er_time);
#endif /* SERVER_MODE && !WINDOWS */
  if (er_tm_p)
    {
      strftime (time_array_p, 256, "%c", er_tm_p);
    }

#if defined (SERVER_MODE)
  tran_index = thread_get_current_tran_index ();
#else /* SERVER_MODE */
  tran_index = TM_TRAN_INDEX ();
#endif /* !SERVER_MODE */

  fprintf (stdout, er_cached_msg[ER_LOG_MSG_WRAPPER_D], time_array_p,
	   ER_SEVERITY_STRING (severity), file_name, line_no,
	   ER_ERROR_WARNING_STRING (severity), err_id, tran_index, msg);
  fflush (stdout);

}

/*
 * er_log_debug - Print debugging message to the log file
 *   return: none
 *   file_name(in):
 *   line_no(in):
 *   fmt(in):
 *   ...(in):
 *
 * Note:
 */
void
_er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
  va_list ap;
  FILE *out;
  static bool doing_er_start = false;
  time_t er_time;
  struct tm er_tm;
  struct tm *er_tm_p = &er_tm;
  struct timeval tv;
  char time_array[256];
  int r;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  r = ER_CSECT_ENTER_LOG_FILE ();
  if (r != NO_ERROR)
    {
      return;
    }

  if (er_Msg == NULL)
    {
      /* Avoid infinite recursion in case of errors in error restart */

      if (doing_er_start == false)
	{
	  doing_er_start = true;

#if defined (SERVER_MODE)
	  (void) er_start (th_entry);
	  er_Msg = th_entry->er_Msg;
#else /* SERVER_MODE */
	  (void) er_start ();
#endif /* SERVER_MODE */

	  doing_er_start = false;
	}
    }

  va_start (ap, fmt);

  out = (er_Msglog != NULL) ? er_Msglog : stderr;

  if (er_Msg != NULL)
    {
      er_time = time (NULL);

#if defined (SERVER_MODE) && !defined (WINDOWS)
      er_tm_p = localtime_r (&er_time, &er_tm);
#else /* SERVER_MODE && !WINDOWS */
      er_tm_p = localtime (&er_time);
#endif /* SERVER_MODE && !WINDOWS */

      if (er_tm_p == NULL)
	{
	  strcpy (time_array, "00/00/00 00:00:00.000");
	}
      else
	{
	  gettimeofday (&tv, NULL);
	  snprintf (time_array +
		    strftime (time_array, 128, "%m/%d/%y %H:%M:%S", er_tm_p),
		    255, ".%03ld", tv.tv_usec / 1000);
	}

      fprintf (out, er_cached_msg[ER_LOG_DEBUG_NOTIFY], time_array,
	       file_name, line_no);
    }

  /* Print out remainder of message */
  vfprintf (out, fmt, ap);
  fflush (out);

  va_end (ap);

  ER_CSECT_EXIT_LOG_FILE ();
}

/*
 * er_get_area_error - Flatten error information into an area
 *   return: packed error information that can be transmitted to the client
 *   length(out): length of the flatten area (set as a side effect)
 *
 * Note: The returned area must be freed by the caller using free_and_init.
 *       This function is used for Client/Server transfering of errors.
 */
void *
er_get_area_error (void *buffer, int *length)
{
  int len;
  char *ptr;
  const char *msg;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  assert (*length > OR_INT_SIZE * 3);

  /*
   * Changed to use the OR_PUT_ macros rather than
   * packing an ER_COPY_AREA structure.
   */

  if (er_Msg == NULL || er_Msg->err_id == NO_ERROR)
    {
      *length = 0;
      return NULL;
    }

  /* Now copy the information */
  msg = er_Msg->msg_area ? er_Msg->msg_area : "(null)";
  len = (OR_INT_SIZE * 3) + strlen (msg) + 1;
  *length = len = (*length > len) ? len : *length;

  ptr = buffer;
  OR_PUT_INT (ptr, (int) (er_Msg->err_id));
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, (int) (er_Msg->severity));
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, len);
  ptr += OR_INT_SIZE;
  len -= OR_INT_SIZE * 3;
  strncpy (ptr, msg, --len /* <= strlen(msg) */ );
  *(ptr + len) = '\0';		/* bullet proofing */

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
er_set_area_error (void *server_area)
{
  char *ptr;
  int err_id, severity, length, r;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;

  if (er_Msg == NULL)
    {
      (void) er_start (th_entry);
      er_Msg = th_entry->er_Msg;
    }
#else /* SERVER_MODE */
  if (er_Msg == NULL)
    {
      (void) er_start ();
    }
#endif /* SERVER_MODE */

  if (server_area == NULL)
    {
      er_clear ();
      return NO_ERROR;
    }

  ptr = (char *) server_area;
  err_id = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  severity = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  length = OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;

  er_Msg->err_id = ((err_id >= 0 || err_id <= ER_LAST_ERROR) ? -1 : err_id);
  er_Msg->severity = severity;
  er_Msg->file_name = "Unknown from server";
  er_Msg->line_no = -1;

  /* Note, current length is the length of the packet not the string,
     considering that this is NULL terminated, we don't really need to
     be sending this. Use the actual string length in the memcpy here! */
  length = strlen (ptr) + 1;

#if defined (SERVER_MODE)
  if (er_make_room (length, th_entry) == NO_ERROR)
    {
      memcpy (er_Msg->msg_area, ptr, length);
    }
#else /* SERVER_MODE */
  if (er_make_room (length) == NO_ERROR)
    {
      memcpy (er_Msg->msg_area, ptr, length);
    }
#endif /* SERVER_MODE */

  /* Call the logging function if any */
  if (severity <= prm_get_integer_value (PRM_ID_ER_LOG_LEVEL)
      && !(prm_get_bool_value (PRM_ID_ER_LOG_WARNING) == false
	   && severity == ER_WARNING_SEVERITY) && er_Fnlog[severity] != NULL)
    {
      r = ER_CSECT_ENTER_LOG_FILE ();
      if (r == NO_ERROR)
	{
	  (*er_Fnlog[severity]) ();
	  ER_CSECT_EXIT_LOG_FILE ();
	}
    }

  return er_Msg->err_id;
}

/*
 * er_stack_push - Save the current error onto the stack
 *   return: NO_ERROR or ER_FAILED
 *
 * Note: The current set error information is saved onto a stack.
 *       This function can be used in conjuction with er_stack_pop when
 *       the caller function wants to return the current error message
 *       no matter what other additional errors are set. For example,
 *       a function may detect an error, then call another function to
 *       do some cleanup. If the cleanup function set an error, the
 *       desired error can be lost.
 *       A function that push something should pop or clear the entry,
 *       otherwise, a function doing a pop may not get the right entry.
 */
int
er_stack_push (void)
{
  ER_MSG *new_msg;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  if (er_Msg == NULL)
    {
      return ER_FAILED;
    }

  new_msg = (ER_MSG *) ER_MALLOC (sizeof (ER_MSG));
  if (new_msg == NULL)
    {
      return ER_FAILED;
    }

  /* Initialize the new message gadget. */
  new_msg->err_id = NO_ERROR;
  new_msg->severity = ER_WARNING_SEVERITY;
  new_msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
  new_msg->line_no = -1;
  new_msg->msg_area_size = 0;
  new_msg->msg_area = NULL;
  new_msg->stack = er_Msg;
  new_msg->args = NULL;
  new_msg->nargs = 0;

  /* Now make er_Msg be the new thing. */
#if defined (SERVER_MODE)
  th_entry->er_Msg = new_msg;
#else
  er_Msg = new_msg;
#endif

  return NO_ERROR;
}

/*
 * er_stack_pop - Restore the previous error from the stack.
 *                The latest saved error is restored in the error area.
 *   return: NO_ERROR or ER_FAILED
 */
int
er_stack_pop (void)
{
  ER_MSG *old_msg;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;

  if (er_Msg == NULL || er_Msg == &th_entry->ermsg)
    {
      return ER_FAILED;
    }
#else /* SERVER_MODE */
  if (er_Msg == NULL || er_Msg == &ermsg)
    {
      return ER_FAILED;
    }
#endif /* SERVER_MODE */

  old_msg = er_Msg;
#if defined (SERVER_MODE)
  th_entry->er_Msg = er_Msg->stack;
#else
  er_Msg = er_Msg->stack;
#endif

#if defined (SERVER_MODE)
  ER_FREE_AREA (old_msg->msg_area, th_entry);
#else /* SERVER_MODE */
  ER_FREE_AREA (old_msg->msg_area);
#endif /* SERVER_MODE */

  if (old_msg->args)
    {
      free_and_init (old_msg->args);
    }
  free_and_init (old_msg);

  return NO_ERROR;
}

/*
 * er_stack_clear - Clear the lastest saved error message in the stack
 *                  That is, pop without restore.
 *   return: none
 */
void
er_stack_clear (void)
{
  ER_MSG *next_msg;
  ER_MSG *save_stack;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  er_Msg = th_entry->er_Msg;
  if (er_Msg == NULL || er_Msg == &th_entry->ermsg)
    {
      return;
    }
#else /* SERVER_MODE */
  if (er_Msg == NULL || er_Msg == &ermsg)
    {
      return;
    }
#endif /* SERVER_MODE */

  next_msg = er_Msg->stack;

#if defined (SERVER_MODE)
  ER_FREE_AREA (next_msg->msg_area, th_entry);
#else /* SERVER_MODE */
  ER_FREE_AREA (next_msg->msg_area);
#endif /* SERVER_MODE */

  if (next_msg->args)
    {
      free_and_init (next_msg->args);
    }

  save_stack = next_msg->stack;
  *next_msg = *er_Msg;
  next_msg->stack = save_stack;

  free_and_init (er_Msg);
#if defined  SERVER_MODE
  th_entry->er_Msg = next_msg;
#else /* SERVER_MODE */
  er_Msg = next_msg;
#endif /* SERVER_MODE */
}

/*
 * er_stack_clearall - Clear all saved error messages
 *   return: none
 */
void
er_stack_clearall (void)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  if (th_entry->er_Msg != NULL)
    {
      while (th_entry->er_Msg->stack != NULL)
	{
	  er_stack_clear ();
	}
    }
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    {
      while (er_Msg->stack != NULL)
	{
	  er_stack_clear ();
	}
    }
#endif /* SERVER_MODE */
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
er_study_spec (const char *conversion_spec, char *simple_spec,
	       int *position, int *width, int *va_class)
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
  if (*q == 'l' || *q == 'L')
    {
      /*
       * Keep this modifier as the class; we'll always use 'long int'
       * (or 'long double') as the carrier type for any conversion code
       * prefixed by an 'l' (or 'L').
       */
      if (*q == 'l' && *(q + 1) == 'l')
	{
	  /* long long type */
	  class_ = SPEC_CODE_LONGLONG;
	  *p++ = *q++;
	  *p++ = *q++;
	}
      else
	{
	  class_ = *q;
	  *p++ = *q++;
	}
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
	  er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_UNKNOWN_CODE],
			code);
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
	  er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_SUSPECT_FMT],
			code);
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
  int i, n, width;
  size_t len;
  va_list args;
  const char *str;

  /*
   * fmt->fmt can be NULL if something went wrong while studying it.
   */
  if (fmt->fmt == NULL)
    {
      return strlen (er_cached_msg[ER_ER_SUBSTITUTE_MSG]);
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

	case 'l':
	  (void) va_arg (args, long int);
	  n = MAX_INT_WIDTH;
	  break;

	case SPEC_CODE_LONGLONG:
	  (void) va_arg (args, long long int);
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
	  er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_UNKNOWN_CODE],
			fmt->spec[i].code);
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
  int r;
  bool entered_critical_section = false;

  if (err_id < ER_FAILED && err_id > ER_LAST_ERROR)
    {
      fmt = &er_fmt_list[-err_id];
    }
  else
    {
      assert (0);
      fmt = &er_fmt_list[-ER_FAILED];
    }

  if (er_fmt_msg_fail_count > 0)
    {
      r = csect_enter (NULL, CSECT_ER_MSG_CACHE, INF_WAIT);
      if (r != NO_ERROR)
	{
	  return NULL;
	}
      entered_critical_section = true;
    }

  if (fmt->fmt == NULL)
    {
      assert (er_fmt_msg_fail_count > 0);

      msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -err_id);
      if (msg == NULL || msg[0] == '\0')
	{
	  er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_ER_LOG_MISSING_MSG],
			err_id);
	  msg = er_cached_msg[ER_ER_MISSING_MSG];
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
	      er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_SUSPECT_FMT],
			    err_id);
	      er_internal_msg (fmt, err_id, ER_ER_SUBSTITUTE_MSG);
	    }
	}
      er_fmt_msg_fail_count--;
    }

  if (entered_critical_section == true)
    {
      csect_exit (CSECT_ER_MSG_CACHE);
    }

  return fmt;
}

static ER_FMT *
er_create_fmt_msg (ER_FMT * fmt, int err_id, const char *msg)
{
  int msg_length;

  msg_length = strlen (msg);

  fmt->fmt = (char *) ER_MALLOC (msg_length + 1);
  if (fmt->fmt == NULL)
    {
      er_internal_msg (fmt, err_id, ER_ER_MISSING_MSG);
      return NULL;
    }

  fmt->fmt_length = msg_length;
  fmt->must_free = 1;

  strcpy (fmt->fmt, msg);

  /*
   * Now study the format specs and squirrel away info about them.
   */
  er_study_fmt (fmt);
  fmt->err_id = err_id;

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
  fmt->fmt = (char *) er_cached_msg[msg_num];
  fmt->fmt_length = strlen (fmt->fmt);
  fmt->must_free = 0;
}

#if defined (SERVER_MODE)
/*
 * enclosing_method -
 *   return:
 *   arg1(in):
 *   arg2(in):
 *
 * Note:
 */
static int
er_make_room (int size, THREAD_ENTRY * th_entry)
{
  ER_MSG *er_Msg = th_entry->er_Msg;
  if (th_entry->er_Msg->msg_area_size < size)
    {
      ER_FREE_AREA (th_entry->er_Msg->msg_area, th_entry);

      er_Msg->msg_area_size = size;
      er_Msg->msg_area = (char *) ER_MALLOC (size);

      if (er_Msg->msg_area == NULL)
	return ER_FAILED;
    }

  return NO_ERROR;
}
#else /* SERVER_MODE */
/*
 * enclosing_method -
 *   return:
 *   arg1(in):
 *   arg2(in):
 *
 * Note:
 */
static int
er_make_room (int size)
{
  if (er_Msg->msg_area_size < size)
    {
      ER_FREE_AREA (er_Msg->msg_area);

      er_Msg->msg_area_size = size;
      er_Msg->msg_area = (char *) ER_MALLOC (size);

      if (er_Msg->msg_area == NULL)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}
#endif /* SERVER_MODE */

/*
 * er_malloc_helper -
 *   return:
 *   size(in):
 *   file(in):
 *   line(in):
 */
static void *
er_malloc_helper (int size, const char *file, int line)
{
  void *mem;

  mem = malloc (size);
  if (mem == NULL)
    {
      er_emergency (file, line, er_cached_msg[ER_ER_OUT_OF_MEMORY], size);
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
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;

  ER_FREE_AREA (er_Msg->msg_area, th_entry);
#else /* SERVER_MODE */
  ER_FREE_AREA (er_Msg->msg_area);
#endif /* SERVER_MODE */

  er_Msg->err_id = ER_GENERIC_ERROR;
  er_Msg->severity = ER_ERROR_SEVERITY;
  er_Msg->file_name = file;
  er_Msg->line_no = line;

#if defined (SERVER_MODE)
  er_Msg->msg_area = th_entry->er_emergency_buf;
  er_Msg->msg_area_size = sizeof (th_entry->er_emergency_buf);
#else /* SERVER_MODE */
  er_Msg->msg_area = er_emergency_buf;
  er_Msg->msg_area_size = sizeof (er_emergency_buf);
#endif /* SERVER_MODE */

  /*
   * Assumes that er_emergency_buf is at least big enough to hold this
   * stuff.
   */
  sprintf (er_Msg->msg_area, er_cached_msg[ER_ER_HEADER], line);
  limit = er_Msg->msg_area_size - strlen (er_Msg->msg_area) - 1;

  va_start (args, fmt);

  p = fmt;
  er_Msg->msg_area[0] = '\0';
  while ((q = strchr (p, '%')) && limit > 0)
    {
      /*
       * Copy the text between the last conversion spec and the next.
       */
      span = CAST_STRLEN (q - p);
      span = MIN (limit, span);
      strncat (er_Msg->msg_area, p, span);
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
	    str = "???";
	  break;
	case 's':
	  str = va_arg (args, const char *);
	  if (str == NULL)
	    str = "(null)";
	  break;
	case '%':
	  str = "%";
	  break;
	default:
	  str = "???";
	  break;
	}
      strncat (er_Msg->msg_area, str, limit);
      limit -= strlen (str);
      limit = MAX (limit, 0);
    }

  va_end (args);

  /*
   * Now copy the message text following the last conversion spec,
   * making sure that we null-terminate the buffer (since strncat won't
   * do it if it reaches the end of the buffer).
   */
  strncat (er_Msg->msg_area, p, limit);
#if defined (SERVER_MODE)
  er_Msg->msg_area[sizeof (th_entry->er_emergency_buf) - 1] = '\0';
#else /* SERVER_MODE */
  er_Msg->msg_area[sizeof (er_emergency_buf) - 1] = '\0';
#endif /* SERVER_MODE */

  /* Now get it into the log. */
  er_log ();
}


/*
 * er_vsprintf -
 *   return:
 *   fmt(in/out):
 *   ap(in):
 */
static int
er_vsprintf (ER_FMT * fmt, va_list * ap)
{
  const char *p;		/* The start of the current non-spec part of fmt */
  const char *q;		/* The start of the next conversion spec        */
  char *s;			/* The end of the valid part of er_Msg->msg_area */
  int n;			/* The va_list position of the current arg      */
  int i;
  va_list args;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  assert (th_entry != NULL);

  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  /*
   *                  *** WARNING ***
   *
   * This routine assumes that er_Msg->msg_area is large enough to
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
      strncpy (er_Msg->msg_area, er_cached_msg[ER_ER_SUBSTITUTE_MSG],
	       er_Msg->msg_area_size);
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
  if (er_Msg->nargs < fmt->nspecs)
    {
      int size;

      if (er_Msg->args)
	free_and_init (er_Msg->args);
      size = fmt->nspecs * sizeof (ER_VA_ARG);
      er_Msg->args = (ER_VA_ARG *) ER_MALLOC (size);
      if (er_Msg->args == NULL)
	return ER_FAILED;
      er_Msg->nargs = fmt->nspecs;
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
	  er_Msg->args[i].int_value = va_arg (args, int);
	  break;
	case 'l':
	  er_Msg->args[i].long_value = va_arg (args, int);
	  break;
	case SPEC_CODE_LONGLONG:
	  er_Msg->args[i].longlong_value = va_arg (args, long long);
	  break;
	case 'p':
	  er_Msg->args[i].pointer_value = va_arg (args, void *);
	  break;
	case 'f':
	  er_Msg->args[i].double_value = va_arg (args, double);
	  break;
	case 'L':
	  er_Msg->args[i].longdouble_value = va_arg (args, long double);
	  break;
	case 's':
	  er_Msg->args[i].string_value = va_arg (args, char *);
	  if (er_Msg->args[i].string_value == NULL)
	    {
	      er_Msg->args[i].string_value = "(null)";
	    }
	  break;
	default:
	  /*
	   * There should be no way to get in here with any other code;
	   * er_study_fmt() should have protected us from that.  If
	   * we're here, it's likely that memory has been corrupted.
	   */
	  er_emergency (__FILE__, __LINE__,
			er_cached_msg[ER_LOG_UNKNOWN_CODE],
			fmt->spec[i].code);
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
  s = er_Msg->msg_area;
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
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].int_value);
	  break;
	case 'l':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].long_value);
	  break;
	case SPEC_CODE_LONGLONG:
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].longlong_value);
	  break;
	case 'p':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].pointer_value);
	  break;
	case 'f':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].double_value);
	  break;
	case 'L':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].longdouble_value);
	  break;
	case 's':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].string_value);
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

#if defined(CS_MODE)
/*
 * er_csect_enter_log_file -
 *   return:
 */
static int
er_csect_enter_log_file (void)
{
  int ret;
  ret = pthread_mutex_lock (&er_log_file_mutex);
  return ret;
}
#endif
