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
#else /* WINDOWS */
#include <unistd.h>
#endif /* WINDOWS */
#if !defined(CS_MODE)
#include <sys/types.h>
#include <sys/stat.h>
#endif /* !CS_MODE */
#if !defined (WINDOWS)
#include <syslog.h>
#endif /* !WINDOWS */
#if defined (SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */

#include "chartype.h"
#include "language_support.h"
#include "memory_alloc.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "object_representation.h"
#if !defined (SERVER_MODE)
#include "boot_cl.h"
#endif /* !SERVER_MODE */
#include "message_catalog.h"
#include "release_string.h"
#include "environment_variable.h"
#if defined (SERVER_MODE)
#include "thread_impl.h"
#endif /* SERVER_MODE */
#include "critical_section.h"
#include "error_manager.h"
#if !defined(CS_MODE)
#include "error_code.h"
#endif /* !CS_MODE */
#include "stack_dump.h"
#include "porting.h"

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

/*
 * These are done via complied constants rather than the message
 * catalog, because they must be avilable if the message catalog is not
 * available.
 */
static const char *english_er_severity[] =
  { "WARNING", "ERROR", "SYNTAX ERROR", "FATAL ERROR", "NOTIFICATION" };

static const char *er_unknown_severity = "Unknown severity level";

static const char **er_severity_string = english_er_severity;

#define ER_SEVERITY_STRING(severity) \
    ( ( ((severity) >= ER_WARNING_SEVERITY) && 	\
        ((severity) <= ER_MAX_SEVERITY)    ) ?	\
     er_severity_string[severity] : er_unknown_severity )

#define ER_ERROR_WARNING_STRING(severity) \
    ( ( ((severity) > ER_WARNING_SEVERITY) && 	\
        ((severity) <= ER_MAX_SEVERITY)    ) ?	\
     er_severity_string[ER_ERROR_SEVERITY] : "" )

#if !defined(CS_MODE)
/* Used by event notification part */
#define ERR_SYSTEM_FILE_DIR     "admin"
#define ERR_EVENT_LINE_LEN      1024
#define ERR_EVENT_CLOSE_CLEAN   1
#define ERR_EVENT_CLOSE_RESTART 2
#endif /* !CS_MODE */

/*
 * Message sets within the er.msg catalog.  Set #1 comprises the system
 * error messages proper, while set #2 comprises the specific messages
 * that the er_ module uses itself.
 */
#define ER_MSG_SET		1
#define ER_INTERNAL_MSG_SET	2

#define PRM_ER_MSGLEVEL         0

#define ER_MALLOC(size)	er_malloc_helper((size), __FILE__, __LINE__)

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


#if !defined (CS_MODE)
/* Used to point to the database name held in boot_sr.c(bo_Dbfullname[]) */
static const char *er_Db_fullname = NULL;
#endif /* !CS_MODE */


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
  ER_STOP_SYSLOG
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
  "\nTime: %s --->>>\n*** %s *** %s CODE = %d, Tran = %d\n%s\n<<<---\n",
  /* ER_LOG_SYSLOG_WRAPPER */
  "CUBRID (pid: %d) *** %s *** %s CODE = %d, Tran = %d, %s",
  /* ER_LOG_MSG_WRAPPER_D */
  "\nTime: %s --->>>\n*** %s *** file %s, line %d %s CODE = %d, Tran = %d\n%s\n<<<---\n",
  /* ER_LOG_SYSLOG_WRAPPER_D */
  "CUBRID (pid: %d) *** %s *** file %s, line %d, %s CODE = %d, Tran = %d. %s",
  /* ER_LOG_LAST_MSG */
  "\n*** The previous error message is the last one. ***\n\n",
  /* ER_LOG_DEBUG_NOTIFY */
  "\n--->>>\n*** DEBUG/NOTIFICATION *** file %s, line %d\n<<<---\n",
  /* ER_STOP_MAIL_SUBJECT */
  "Mail -s \"CUBRID has been stopped\" ",
  /* ER_STOP_MAIL_BODY */
  "--->>>\n%s has been stopped at your request when the following error was set:\nerrid = %d, %s\nUser: %s, pid: %d, host: %s, time: %s<<<---",
  /* ER_STOP_SYSLOG */
  "%s has been stopped on errid = %d. User: %s, pid: %d"
};
static char *er_cached_msg[sizeof (er_builtin_msg) / sizeof (const char *)];

/** NAME OF TEXT AND LOG MESSAGE FILES  **/
static char er_Log_file_name[PATH_MAX];
static const char *er_Msglog_file_name = NULL;
static FILE *er_Msglog = NULL;

static ER_FMT_CACHE er_cache;
#if !defined (SERVER_MODE)
static ER_MSG ermsg = { 0, 0, NULL, 0, 0, NULL, NULL, NULL, 0 };
static ER_MSG *er_Msg = NULL;
static char er_emergency_buf[256];	/* message when all else fails */
#endif /* !SERVER_MODE */

/* Other supporting global variables */
static bool er_hasalready_initiated = false;
static int er_Exit_ask = ER_EXIT_DEFAULT;

#if defined (SERVER_MODE)
static bool logfile_opened = false;

static int er_start (THREAD_ENTRY * th_entry);
#else /* SERVER_MODE */
static int er_start (void);
#endif /* SERVER_MODE */

static bool er_is_enable_call_stack_dump (int err_id);
static int er_set_internal (int severity, const char *file_name,
			    const int line_no, int err_id, int num_args,
			    bool include_os_error, va_list * ap_ptr);
static void er_log (void);
static void er_stop_on_error (void);

static int er_study_spec (const char *conversion_spec, char *simple_spec,
			  int *position, int *width, int *va_class);
static void er_study_fmt (ER_FMT * fmt);
static size_t er_estimate_size (ER_FMT * fmt, va_list * ap);
static ER_FMT *er_find_fmt (int err_id);
static void er_init_fmt (ER_FMT * fmt);
static void er_clear_fmt (ER_FMT * fmt);
#if defined (SERVER_MODE)
static int er_make_room (int size, THREAD_ENTRY * th_entry);
#else /* SERVER_MODE */
static int er_make_room (int size);
static void er_init_cache (void);
static void er_clear_cache (void);
#endif /* SERVER_MODE */
static void er_internal_msg (ER_FMT * fmt, int code, int msg_num);
static void *er_malloc_helper (int size, const char *file, int line);
static void er_emergency (const char *file, int line, const char *fmt, ...);
static int er_vsprintf (ER_FMT * fmt, va_list * ap);

#if defined (ENABLE_UNUSED_FUNCTION)
#if !defined(CS_MODE)
static const char *er_dbname (void);
#endif /* !CS_MODE */
#endif /* ENABLE_UNUSED_FUNCTION */

/* vector of functions to call when an error is set */
static PTR_FNERLOG er_Fnlog[ER_MAX_SEVERITY + 1] =
  { NULL, er_log, er_log, er_log, er_log };


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
  unsigned int i;
#if defined (SERVER_MODE)
  int r;
#endif /* SERVER_MODE */

#if defined (SA_MODE)
  /*
   * If the error module has already been initialized, shutted down before
   * it is initialized again.
   */
  if (er_Msg != NULL)
    {
      er_final ();
    }
#endif /* SA_MODE */

  if (PRM_ER_LOG_WARNING)
    {
      /* if we want warnings logged to the error log */
      er_Fnlog[0] = er_log;
    }

  er_severity_string = english_er_severity;

  for (i = 0; i < DIM (er_builtin_msg); i++)
    {
      if (er_cached_msg[i] && er_cached_msg[i] != er_builtin_msg[i])
	{
	  free_and_init (er_cached_msg[i]);
	}
      er_cached_msg[i] = (char *) er_builtin_msg[i];
    }

#if defined (SERVER_MODE)
  for (i = 0; i < DIM (er_cache.fmt); i++)
    {
      er_init_fmt (&er_cache.fmt[i]);
    }
  er_cache.timestamp = 0;

  r = csect_enter (NULL, CSECT_ER_LOG_FILE, INF_WAIT);
  if (r != NO_ERROR)
    {
      return ER_FAILED;
    }
#else /* SERVER_MODE */
  er_init_cache ();
#endif /* SERVER_MODE */

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
  if (msglog_file_name)
    {
      strcpy (er_Log_file_name, msglog_file_name);
      er_Msglog_file_name = er_Log_file_name;
    }
  else
    {
      if (PRM_ER_LOG_FILE && strlen (PRM_ER_LOG_FILE))
	{
	  strcpy (er_Log_file_name, PRM_ER_LOG_FILE);
	  er_Msglog_file_name = er_Log_file_name;
	}
      else
	{
	  er_Msglog_file_name = NULL;
	}
    }

  er_hasalready_initiated = true;

  csect_exit (CSECT_ER_LOG_FILE);

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

  assert (path != NULL);

#if defined (CS_MODE)
  fp = fopen (path, "r+");
  if (fp != NULL)
    {
      fseek (fp, 0, SEEK_END);
      if (fp != NULL && ftell (fp) > PRM_ER_LOG_SIZE)
	{
	  fclose (fp);
	  fp = fopen (path, "w");
	}
    }
  else
    {
      fp = fopen (path, "w");
    }
#else /* CS_MODE */
  fp = fopen (path, "w");
#endif /* !CS_MODE */
  return fp;
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
  unsigned int i;

  if (er_hasalready_initiated == false)
    {
      (void) er_init (PRM_ER_LOG_FILE, PRM_ER_EXIT_ASK);
    }
  while (!er_hasalready_initiated)
    ;
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

  r = csect_enter (NULL, CSECT_ER_LOG_FILE, INF_WAIT);
  /* Define message log file */
  if (logfile_opened == false)
    {
      if (er_Msglog_file_name)
	{
	  /* want to err on the side of doing production style error logs
	   * because this may be getting set at some naive customer site. */
	  if (!PRM_ER_PRODUCTION_MODE)
	    {
	      char path[PATH_MAX];
	      sprintf (path, "%s.%d", er_Msglog_file_name, getpid ());
	      er_Msglog = er_file_open (path);;
	    }
	  else
	    {
	      er_Msglog = er_file_open (er_Msglog_file_name);
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
	er_Msglog = stderr;
      logfile_opened = true;
    }

  /*
   * Message catalog may be initialized by msgcat_init() during bootstrap.
   * But, try once more to call msgcat_init() becuase there could be
   * an exception case that get here before bootstrap.
   */
  if (msgcat_init () != NO_ERROR)
    {
      er_emergency (__FILE__, __LINE__, er_cached_msg[ER_ER_NO_CATALOG]);
      status = ER_FAILED;
    }
  else
    {
      /* cache the messages */

      /*
       * Remember, we skip code 0.  If we can't find enough memory to
       * copy the silly message, things are going to be pretty tight
       * anyway, so just use the default version.
       */
      for (i = 1; i < DIM (er_cached_msg); i++)
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
    }

  r = csect_exit (CSECT_ER_LOG_FILE);
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
  unsigned int i;

  /* Make sure that the behavior of the module has been defined */

  if (er_hasalready_initiated == false)
    (void) er_init (PRM_ER_LOG_FILE, PRM_ER_EXIT_ASK);

  /*
   * If the error module has already been initialized, shutted down
   * before it is reinitialized again.
   */

  if (er_Msg != NULL)
    er_final ();

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
      /* want to err on the side of doing production style error logs
       * because this may be getting set at some naive customer site. */
      if (!PRM_ER_PRODUCTION_MODE)
	{
	  char path[PATH_MAX];
	  sprintf (path, "%s.%d", er_Msglog_file_name, getpid ());
	  er_Msglog = er_file_open (path);
	}
      else
	{
	  er_Msglog = er_file_open (er_Msglog_file_name);
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
   * But, try once more to call msgcat_init() becuase there could be
   * an exception case that get here before bootstrap.
   */
  if (msgcat_init () != NO_ERROR)
    {
      er_emergency (__FILE__, __LINE__, er_cached_msg[ER_ER_NO_CATALOG]);
      status = ER_FAILED;
    }
  else
    {
      /* cache the messages */

      /*
       * Remember, we skip code 0.  If we can't find enough memory to
       * copy the silly message, things are going to be pretty tight
       * anyway, so just use the default version.
       */
      for (i = 1; i < DIM (er_cached_msg); i++)
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
    }

  return status;
}

#endif /* SERVER_MODE */


#if defined (SERVER_MODE)

/*
 * er_final - Terminate the error message module
 *   return: none
 *   do_global_final(in):
 */
void
er_final (bool do_global_final)
{
  unsigned int i;
  int r;
  THREAD_ENTRY *th_entry;

  r = csect_enter (NULL, CSECT_ER_LOG_FILE, INF_WAIT);

  if (do_global_final == false)
    {
      th_entry = thread_get_thread_entry_info ();
      if (th_entry == NULL)
	goto exit;

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
	      free (th_entry->er_Msg->args);
	    }
	  th_entry->er_Msg->nargs = 0;

	  th_entry->er_Msg = NULL;
	}
    }

  else
    {
      if (logfile_opened == true)
	{
	  if (er_Msglog != NULL && er_Msglog != stderr)
	    {
	      (void) fclose (er_Msglog);
	    }
	  logfile_opened = false;
	}

      /* er_clear_cache(); I leave this function to the system. */

      for (i = 0; i < DIM (er_cached_msg); i++)
	{
	  if (er_cached_msg[i] != er_builtin_msg[i])
	    {
	      free_and_init (er_cached_msg[i]);
	      er_cached_msg[i] = (char *) er_builtin_msg[i];
	    }
	}
    }
exit:
  r = csect_exit (CSECT_ER_LOG_FILE);
}

#else /* SERVER_MODE */

/*
 * er_final - Terminate the error message module
 *   return: none
 */
void
er_final (void)
{
  unsigned int i;

  if (er_Msg != NULL)
    {
      er_stack_clear ();
      if (er_Msglog != NULL && er_Msglog != stderr)
	(void) fclose (er_Msglog);
      ER_FREE_AREA (er_Msg->msg_area);
      er_Msg->msg_area = er_emergency_buf;
      er_Msg->msg_area_size = sizeof (er_emergency_buf);
      if (er_Msg->args)
	{
	  free (er_Msg->args);
	}
      er_Msg->nargs = 0;
      er_clear_cache ();

      er_Msg = NULL;
    }

  for (i = 0; i < DIM (er_cached_msg); i++)
    {
      if (er_cached_msg[i] != er_builtin_msg[i])
	{
	  free_and_init (er_cached_msg[i]);
	  er_cached_msg[i] = (char *) er_builtin_msg[i];
	}
    }
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
			  false, &ap);
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
			  true, &ap);
  va_end (ap);
}

/*
 * is_for_call_stack_dump - is the error identifier set for call stack
 *                          dump on error
 *   return: true or false
 *   err_id(in):
 */
static bool
er_is_enable_call_stack_dump (int err_id)
{
  if (PRM_CALL_STACK_DUMP_ON_ERROR)
    {
      return !PRM_CALL_STACK_DUMP_DEACTIVE_ERRORS[abs (err_id)];
    }
  else
    {
      return PRM_CALL_STACK_DUMP_ACTIVE_ERRORS[abs (err_id)];
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
 *   ...(in): variable list of arguments (just like fprintf)
 *
 * Note:
 */
static int
er_set_internal (int severity, const char *file_name, const int line_no,
		 int err_id, int num_args, bool include_os_error,
		 va_list * ap_ptr)
{
  va_list ap;
  const char *os_error;
  size_t new_size;
  int r;
  ER_FMT *er_fmt = NULL;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry;
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
  /* Initialize the area... */
  er_Msg->err_id = err_id;
  er_Msg->severity = severity;
  er_Msg->file_name = file_name;
  er_Msg->line_no = line_no;

  /*
   * Get hold of the compiled format string for this message and get an
   * estimate of the size of the buffer required to print it.
   */
  r = csect_enter (NULL, CSECT_ER_MSG_CACHE, INF_WAIT);	/* refer to "er_find_fmt()" */

  er_fmt = er_find_fmt (err_id);
  if (er_fmt == NULL)
    {
      /*
       * Assumes that er_find_fmt() has already called er_emergency().
       */
      csect_exit (CSECT_ER_MSG_CACHE);
      return ER_FAILED;
    }

  /*
   * Be sure that we have the same number of arguments before calling
   * er_estimate_size().  Because it uses straight va_arg() and friends
   * to grab its arguments, it is vulnerable to argument mismatches
   * (e.g., too few arguments, ints in string positions, etc).  This
   * won't save us when someone passes an integer argument where the
   * format says to expect a string, but it will save us if someone
   * just plain forgets how many args there are.
   */
  if (er_fmt->nspecs != num_args)
    {
      er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_LOG_SUSPECT_FMT], err_id);
      er_internal_msg (er_fmt, err_id, ER_ER_SUBSTITUTE_MSG);
    }
  new_size = er_estimate_size (er_fmt, ap_ptr);

  /*
   * Do we need to copy the OS error message?
   */
  if (os_error)
    {
      new_size += 4 + strlen (os_error);
    }

  /*
   * Do any necessary allocation for the buffer.
   */
#if defined (SERVER_MODE)
  if (er_make_room (new_size + 1, th_entry) == ER_FAILED)
    {
      r = csect_exit (CSECT_ER_MSG_CACHE);
#else /* SERVER_MODE */
  if (er_make_room (new_size + 1) == ER_FAILED)
    {
#endif /* SERVER_MODE */
      return ER_FAILED;
    }

  /*
   * And now format the silly thing.
   */
  if (er_vsprintf (er_fmt, ap_ptr) == ER_FAILED)
    {
      csect_exit (CSECT_ER_MSG_CACHE);
      return ER_FAILED;
    }
  if (os_error)
    {
      strcat (er_Msg->msg_area, "... ");
      strcat (er_Msg->msg_area, os_error);
    }

  /*
   * Call the logging function if any
   */
  if (er_Fnlog[severity] != NULL)
    {
      (*er_Fnlog[severity]) ();

      /* call stack dump */
      if (er_is_enable_call_stack_dump (err_id))
	{
	  er_dump_call_stack (er_Msglog);
	}
    }

  csect_exit (CSECT_ER_MSG_CACHE);
  /*
   * Do we want to stop the system on this error ... for debugging
   * purposes?
   */
  if (PRM_ER_STOP_ON_ERROR == err_id)
    er_stop_on_error ();

  return NO_ERROR;
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
   * Don't let the file of log messages get very long. Go back to the
   * top if need be.
   */

  if (er_Msglog != stderr && er_Msglog != stdout
      && ftell (er_Msglog) > (int) PRM_ER_LOG_SIZE)
    {
      (void) fflush (er_Msglog);
      (void) fprintf (er_Msglog, "%s", er_cached_msg[ER_LOG_WRAPAROUND]);

      /* Rewind to limit the amount of error messages being logged */
      (void) fseek (er_Msglog, 0L, SEEK_SET);
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
  strftime (time_array, 256, "%c", er_tm_p);
  gettimeofday (&tv, NULL);
  time_array[255] = '\0';
  snprintf (time_array +
	    strftime (time_array, 128, "%m/%d/%y %H:%M:%S", er_tm_p), 255,
	    ".%d", tv.tv_usec / 1000);

#if defined (SERVER_MODE)
  tran_index = thread_get_current_tran_index ();
#elif defined (FOR_EVENT_HANDLER)
  tran_index = NULL_TRAN_INDEX;
#else /* FOR_EVENT_HANDLER */
  tran_index = TM_TRAN_INDEX ();
#endif /* SERVER_MODE, FOR_EVENT_HANDLER */

  if (PRM_ER_PRODUCTION_MODE)
    {
      (void) fprintf (er_Msglog, er_cached_msg[ER_LOG_MSG_WRAPPER],
		      time_array, ER_SEVERITY_STRING (severity),
		      ER_ERROR_WARNING_STRING (severity), err_id, tran_index,
		      msg);
      (void) fflush (er_Msglog);
      if (severity == ER_NOTIFICATION_SEVERITY)
	(void) syslog (LOG_ALERT, er_cached_msg[ER_LOG_SYSLOG_WRAPPER],
		       getpid (),
		       ER_SEVERITY_STRING (severity),
		       ER_ERROR_WARNING_STRING (severity), err_id, tran_index,
		       msg);
    }
  else
    {
      (void) fprintf (er_Msglog, er_cached_msg[ER_LOG_MSG_WRAPPER_D],
		      time_array, ER_SEVERITY_STRING (severity), file_name,
		      line_no, ER_ERROR_WARNING_STRING (severity), err_id,
		      tran_index, msg);
      (void) fflush (er_Msglog);
      if (severity == ER_NOTIFICATION_SEVERITY)
	(void) syslog (LOG_ALERT, er_cached_msg[ER_LOG_SYSLOG_WRAPPER_D],
		       getpid (),
		       ER_SEVERITY_STRING (severity), file_name, line_no,
		       ER_ERROR_WARNING_STRING (severity), err_id, tran_index,
		       msg);
    }

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
  assert (th_entry != NULL);
  return ((th_entry->er_Msg != NULL) ? th_entry->er_Msg->err_id : NO_ERROR);
#else /* SERVER_MODE */
  return ((er_Msg != NULL) ? er_Msg->err_id : NO_ERROR);
#endif /* SERVER_MODE */
}

/*
 * er_clearid - Clear only error identifer
 *   return: none
 */
void
er_clearid (void)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
  assert (th_entry != NULL);
  if (th_entry->er_Msg != NULL)
    th_entry->er_Msg->err_id = NO_ERROR;
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    er_Msg->err_id = NO_ERROR;
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
  assert (th_entry != NULL);
  if (th_entry->er_Msg != NULL)
    th_entry->er_Msg->err_id = err_id;
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
  assert (th_entry != NULL);
  return ((th_entry->er_Msg != NULL) ? th_entry->er_Msg->severity
	  : ER_WARNING_SEVERITY);
#else /* SERVER_MODE */
  return ((er_Msg != NULL) ? er_Msg->severity : ER_WARNING_SEVERITY);
#endif /* SERVER_MODE */
}

/*
 * er_nlevels - Get number of levels of the last error
 *   return: number of levels
 */
int
er_nlevels (void)
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
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
  assert (th_entry != NULL);
  if (th_entry->er_Msg != NULL)
    {
      *line_no = th_entry->er_Msg->line_no;
      return th_entry->er_Msg->file_name;
    }
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    {
      *line_no = er_Msg->line_no;
      return er_Msg->file_name;
    }
#endif /* SERVER_MODE */
  else
    {
      *line_no = -1;
      return NULL;
    }
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
er_msg ()
{
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
  assert (th_entry != NULL);
  if (th_entry->er_Msg == NULL)
    {
      (void) er_clear ();
    }
  if (!th_entry->er_Msg->msg_area[0])
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
  strftime (time_array_p, 256, "%c", er_tm_p);

#if defined (SERVER_MODE)
  tran_index = thread_get_current_tran_index ();
#elif defined(FOR_EVENT_HANDLER)
  tran_index = NULL_TRAN_INDEX;
#else /* FOR_EVENT_HANDLER */
  tran_index = TM_TRAN_INDEX ();
#endif /* SERVER_MODE, FOR_EVENT_HANDLER */

  if (PRM_ER_PRODUCTION_MODE)
    {
      (void) fprintf (stdout, er_cached_msg[ER_LOG_MSG_WRAPPER],
		      time_array_p, ER_SEVERITY_STRING (severity),
		      ER_ERROR_WARNING_STRING (severity), err_id, tran_index,
		      msg);
    }
  else
    {
      (void) fprintf (stdout, er_cached_msg[ER_LOG_MSG_WRAPPER_D],
		      time_array_p, ER_SEVERITY_STRING (severity), file_name,
		      line_no, ER_ERROR_WARNING_STRING (severity), err_id,
		      tran_index, msg);
    }
  (void) fflush (stdout);

}

/*
 * enclosing_method - Print debugging message to the log file
 *   return: none
 *   file_name(in):
 *   line_no(in):
 *   fmt(in):
 *   ...(in):
 *
 * Note:
 */
void
er_log_debug (const char *file_name, const int line_no, const char *fmt, ...)
{
#if defined(CUBRID_DEBUG)
  va_list ap;
  FILE *out;
  static bool doing_er_start = false;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */
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
      (void) fprintf (out, er_cached_msg[ER_LOG_DEBUG_NOTIFY], file_name,
		      line_no);
    }

  /* Print out remainder of message */
  (void) vfprintf (out, fmt, ap);
  (void) fflush (out);

  va_end (ap);
#endif /* CUBRID_DEBUG */
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
er_get_area_error (int *length)
{
  int len;
  char *area, *ptr;
  const char *msg;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  /*
   * Changed to use the OR_PUT_ macros rather than
   * packing an ER_COPY_AREA structure.
   *
   */

  if (er_Msg == NULL || er_Msg->err_id == NO_ERROR)
    {
      *length = 0;
      return NULL;
    }

  /*
   * Guess the length needed and allocate the area.
   */
  msg = er_Msg->msg_area ? er_Msg->msg_area : "(null)";
  *length = len = (OR_INT_SIZE * 3) + strlen (msg) + 1;

  area = (char *) ER_MALLOC (len);
  if (area == NULL)
    {
      *length = 0;
      return NULL;
    }

  /*
   * Now copy the information
   */
  ptr = area;
  OR_PUT_INT (ptr, (int) (er_Msg->err_id));
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, (int) (er_Msg->severity));
  ptr += OR_INT_SIZE;
  OR_PUT_INT (ptr, len);
  ptr += OR_INT_SIZE;
  strcpy (ptr, msg);

  return area;
}

/*
 * er_set_area_error - Reset the error information
 *   return: none
 *   server_area(in): the flatten area with error information
 *
 * Note: Error information is reset with the one provided by the packed area,
 *       which is the last error found in the server. 						      *
 */
void
er_set_area_error (void *server_area)
{
  char *ptr;
  int err_id, severity, length;
#if defined (SERVER_MODE)
  ER_MSG *er_Msg;
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
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
      return;
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
#else /* SERVER_MODE */
  if (er_make_room (length) == NO_ERROR)
#endif /* SERVER_MODE */
    {
      memcpy (er_Msg->msg_area, ptr, length);
    }

  /* Call the logging function if any */
  if (er_Fnlog[severity] != NULL)
    {
      (*er_Fnlog[severity]) ();
    }
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
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

  if (er_Msg == NULL)
    {
      return ER_FAILED;
    }

  if ((new_msg = (ER_MSG *) ER_MALLOC (sizeof (ER_MSG))) == NULL)
    {
      return ER_FAILED;
    }

  /*
   * Initialize the new message gadget.
   */
  new_msg->err_id = NO_ERROR;
  new_msg->severity = ER_WARNING_SEVERITY;
  new_msg->file_name = er_cached_msg[ER_ER_UNKNOWN_FILE];
  new_msg->line_no = -1;
  new_msg->msg_area_size = 0;
  new_msg->msg_area = NULL;
  new_msg->stack = er_Msg;
  new_msg->args = NULL;
  new_msg->nargs = 0;

  /*
   * Now make er_Msg be the new thing.
   */
  er_Msg = new_msg;

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
  assert (th_entry != NULL);
  er_Msg = th_entry->er_Msg;
#endif /* SERVER_MODE */

#if defined (SERVER_MODE)
  if (er_Msg == NULL || er_Msg == &th_entry->ermsg)
#else /* SERVER_MODE */
  if (er_Msg == NULL || er_Msg == &ermsg)
#endif /* SERVER_MODE */
    {
      return ER_FAILED;
    }

  old_msg = er_Msg;
  er_Msg = er_Msg->stack;

#if defined (SERVER_MODE)
  ER_FREE_AREA (old_msg->msg_area, th_entry);
#else /* SERVER_MODE */
  ER_FREE_AREA (old_msg->msg_area);
#endif /* SERVER_MODE */
  if (old_msg->args)
    {
      free (old_msg->args);
    }
  free (old_msg);

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
  er_Msg = th_entry->er_Msg;
  if (er_Msg == NULL || er_Msg == &th_entry->ermsg)
#else /* SERVER_MODE */
  if (er_Msg == NULL || er_Msg == &ermsg)
#endif /* SERVER_MODE */
    return;

  next_msg = er_Msg->stack;

#if defined (SERVER_MODE)
  ER_FREE_AREA (next_msg->msg_area, th_entry);
#else /* SERVER_MODE */
  ER_FREE_AREA (next_msg->msg_area);
#endif /* SERVER_MODE */
  if (next_msg->args)
    {
      free (next_msg->args);
    }
  save_stack = next_msg->stack;
  *next_msg = *er_Msg;
  next_msg->stack = save_stack;

  free (er_Msg);
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
  assert (th_entry != NULL);

  if (th_entry->er_Msg != NULL)
    while (th_entry->er_Msg->stack != NULL)
#else /* SERVER_MODE */
  if (er_Msg != NULL)
    while (er_Msg->stack != NULL)
#endif /* SERVER_MODE */
      {
	er_stack_clear ();
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
      class_ = *q;
      *p++ = *q++;
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

  return q - conversion_spec;
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
		free (fmt->spec);
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
 *   fmt(in/out): a pointer to an already-studied ER_FMT structure           *
 *   ap(in): a va_list of arguments                                     *
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
	  (void) va_arg (args, int);
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
er_find_fmt (int err_id)
{
  const char *msg;
  ER_FMT *fmt;
  int msg_length, slot, slot_age;
  unsigned int i;
#if defined (SERVER_MODE)
  THREAD_ENTRY *th_entry = thread_get_thread_entry_info ();
#endif /* SERVER_MODE */

  er_cache.timestamp++;
#if defined (SERVER_MODE)
  /* when timestamp(int) overflows */
  if (er_cache.timestamp < 0)
    {
      er_cache.timestamp = 1;
      for (i = 0; i < DIM (er_cache.fmt); i++)
	er_cache.fmt[i].age = er_cache.timestamp++;
    }
#endif /* SERVER_MODE */

  /* * See if we have already cached the ER_FMT for this message id.  */
  for (i = 0; i < DIM (er_cache.fmt); i++)
    {
      if (er_cache.fmt[i].err_id == err_id)
	{
	  /*
	   * Record the fact that we have recently asked for this
	   * message, so it won't get evicted quite so easily.
	   */
	  er_cache.fmt[i].age = er_cache.timestamp;
	  return &er_cache.fmt[i];
	}
    }

  msg = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -err_id);
  if (msg == NULL || msg[0] == '\0')
    {
      er_log_debug (ARG_FILE_LINE, er_cached_msg[ER_ER_LOG_MISSING_MSG],
		    err_id);
      msg = er_cached_msg[ER_ER_MISSING_MSG];
    }
  msg_length = strlen (msg);

  /*
   * Now find a cache slot in which to store the info about this
   * message.  If the cache is full, kick out the entry that has been
   * used least recently.
   */
  slot = 0;
  slot_age = er_cache.fmt[0].age;
  for (i = 0; i < DIM (er_cache.fmt); i++)
    {
      if (er_cache.fmt[i].age == 0)
	{			/* Unused */
	  slot = i;
	  break;
	}
      else if (er_cache.fmt[i].age < slot_age)
	{
	  slot = i;
	  slot_age = er_cache.fmt[i].age;
	}
    }

  fmt = &er_cache.fmt[slot];

  fmt->err_id = err_id;
  fmt->age = er_cache.timestamp;

  /*
   * Try to reuse the area allocated for the previous format string, if
   * possible.
   */
#if !defined (SERVER_MODE)
  if (msg_length > fmt->fmt_length)
    {
#endif /* !SERVER_MODE */
      if (fmt->fmt && fmt->must_free)
	free ((char *) fmt->fmt);
      fmt->fmt = (char *) ER_MALLOC (msg_length + 1);
      if (fmt->fmt == NULL)
	{
#if defined (SERVER_MODE)
	  er_internal_msg (fmt, th_entry->er_Msg->err_id, ER_ER_MISSING_MSG);
#else /* SERVER_MODE */
	  er_internal_msg (fmt, er_Msg->err_id, ER_ER_MISSING_MSG);
#endif /* SERVER_MODE */
	  return NULL;
	}
      fmt->fmt_length = msg_length;
      fmt->must_free = 1;
#if !defined (SERVER_MODE)
    }
#endif /* !SERVER_MODE */
  strcpy (fmt->fmt, msg);

  /*
   * Now study the format specs and squirrel away info about them.
   */
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
  fmt->age = 0;
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
      free (fmt->fmt);
    }
  fmt->fmt = NULL;
  fmt->fmt_length = 0;
  fmt->must_free = 0;

  if (fmt->spec && fmt->spec != fmt->spec_buf)
    {
      free (fmt->spec);
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


#if !defined (SERVER_MODE)
/*
 * er_init_cache -
 *   return: none
 */
static void
er_init_cache (void)
{
  unsigned int i;

  for (i = 0; i < DIM (er_cache.fmt); i++)
    {
      er_init_fmt (&er_cache.fmt[i]);
    }

  er_cache.timestamp = 0;
}

/*
 * er_clear_cache -
 *   return: none
 */
static void
er_clear_cache (void)
{
  unsigned int i;
  ER_FMT *fmt;
  int r;

  r = csect_enter (NULL, CSECT_ER_MSG_CACHE, INF_WAIT);

  for (i = 0; i < DIM (er_cache.fmt); i++)
    {
      fmt = &er_cache.fmt[i];
      er_clear_fmt (fmt);
      er_init_fmt (fmt);
    }
  csect_exit (CSECT_ER_MSG_CACHE);
}
#endif /* !SERVER_MODE */


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
  limit = er_Msg->msg_area_size - strlen (er_Msg->msg_area);

  va_start (args, fmt);

  p = fmt;
  er_Msg->msg_area[0] = '\0';
  while ((q = strchr (p, '%')) && limit > 0)
    {
      /*
       * Copy the text between the last conversion spec and the next.
       */
      span = q - p;
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

  /*
   * Now get it into the log.
   */
  er_log ();
}

#if defined (SERVER_MODE)
#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * check_cache_consistency -
 *   return:
 *   fmt(in/out):
 */
int
check_cache_consistency (ER_FMT * fmt)
{
  int i;
  int r = 1;
  ER_FMT tmp_fmt;

  tmp_fmt.err_id = fmt->err_id;
  tmp_fmt.spec_top = DIM (tmp_fmt.spec_buf);
  tmp_fmt.spec = tmp_fmt.spec_buf;
  tmp_fmt.fmt_length = strlen (fmt->fmt);
  tmp_fmt.fmt = (char *) ER_MALLOC (tmp_fmt.fmt_length + 1);
  strcpy (tmp_fmt.fmt, fmt->fmt);
  er_study_fmt (&tmp_fmt);

  /*if(tmp_fmt.fmt_length != fmt->fmt_length || */
  if (tmp_fmt.nspecs != fmt->nspecs || tmp_fmt.spec_top != fmt->spec_top)
    {
      r = 0;
    }

  for (i = 0; i < tmp_fmt.nspecs; i++)
    {
      if (tmp_fmt.spec[i].code != fmt->spec[i].code)
	{
	  r = 0;
	}
    }

  if (tmp_fmt.fmt)
    {
      free (tmp_fmt.fmt);
    }

  return r;
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* SERVER_MODE */

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
	free (er_Msg->args);
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
	  er_Msg->args[i].i = va_arg (args, int);
	  break;
	case 'l':
	  er_Msg->args[i].l = va_arg (args, int);
	  break;
	case 'p':
	  er_Msg->args[i].p = va_arg (args, void *);
	  break;
	case 'f':
	  er_Msg->args[i].f = va_arg (args, double);
	  break;
	case 'L':
	  er_Msg->args[i].lf = va_arg (args, long double);
	  break;
	case 's':
	  er_Msg->args[i].s = va_arg (args, char *);
	  if (er_Msg->args[i].s == NULL)
	    {
	      er_Msg->args[i].s = "(null)";
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
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].i);
	  break;
	case 'l':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].l);
	  break;
	case 'p':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].p);
	  break;
	case 'f':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].f);
	  break;
	case 'L':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].lf);
	  break;
	case 's':
	  sprintf (s, fmt->spec[n].spec, er_Msg->args[n].s);
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

#if !defined(CS_MODE)

/*
 * er_save_dbname - Save a pointer to the variable holding the database name
 *   return: none
 *   dbname(in):
 *   arg2(in):
 *
 * Note: Initializes the global er_Db_fullname to the pointer.
 */
void
er_save_dbname (const char *dbname)
{
  er_Db_fullname = dbname;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * er_dbname - Returns a pointer to the database name string
 *   return: a local pointer to the saved database name
 *
 * Note: Strips off the leading path name
 */
static const char *
er_dbname ()
{
  const char *nopath_name;

  if (!er_Db_fullname || !(*er_Db_fullname))
    {
      return (NULL);
    }
  nopath_name = strrchr (er_Db_fullname, PATHSLASH);
#if defined (WINDOWS)
  {
    char *nn_tmp = (char *) strrchr (er_Db_fullname, '/');
    if (nopath_name < nn_tmp)
      {
	nopath_name = nn_tmp;
      }
  }
#endif /* WINDOWS */
  if (nopath_name == NULL)
    {
      nopath_name = er_Db_fullname;
    }
  else
    {
      nopath_name++;		/* Skip to the name */
    }

  return nopath_name;
}
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* !CS_MODE */
