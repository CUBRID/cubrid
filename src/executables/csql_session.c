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
 * csql_session.c : menu driver of csql
 */

#ident "$Id$"

#include "config.h"

#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

#include "csql.h"

#include "class_description.hpp"
#include "porting.h"
#include "memory_alloc.h"
#include "object_print.h"
#include "util_func.h"
#include "network_interface_cl.h"
#include "unicode_support.h"
#include "transaction_cl.h"
#include "trigger_description.hpp"
#include "db.h"

/* for short usage of `csql_append_more_line()' and error check */
#define	APPEND_MORE_LINE(indent, line)	\
		do { \
		  if(csql_append_more_line((indent), (line)) == CSQL_FAILURE) \
		    goto error; \
		} while(0)
#define	APPEND_HEAD_LINE(head_text)	\
		do { \
		  APPEND_MORE_LINE(0, ""); \
		  APPEND_MORE_LINE(1, (head_text)); \
		  APPEND_MORE_LINE(0, ""); \
		} while(0)

static jmp_buf csql_Jmp_buf;

static void csql_pipe_handler (int sig_no);
static void csql_dump_alltran (volatile TRANS_INFO * info);


#define CMD_EMPTY_FLAG	  0x00000000
#define CMD_CHECK_CONNECT 0x00000001

/* session command table */
typedef struct
{
  const char *text;		/* lower case cmd name */
  SESSION_CMD cmd_no;		/* command number */
  unsigned int flags;
} SESSION_CMD_TABLE;

static SESSION_CMD_TABLE csql_Session_cmd_table[] = {
  /* File stuffs */
  {"read", S_CMD_READ, CMD_EMPTY_FLAG},
  {"write", S_CMD_WRITE, CMD_EMPTY_FLAG},
  {"append", S_CMD_APPEND, CMD_EMPTY_FLAG},
  {"print", S_CMD_PRINT, CMD_EMPTY_FLAG},
  {"shell", S_CMD_SHELL, CMD_EMPTY_FLAG},
  {"!", S_CMD_SHELL, CMD_EMPTY_FLAG},
  {"cd", S_CMD_CD, CMD_EMPTY_FLAG},
  {"exit", S_CMD_EXIT, CMD_EMPTY_FLAG},
  /* Edit stuffs */
  {"clear", S_CMD_CLEAR, CMD_EMPTY_FLAG},
  {"edit", S_CMD_EDIT, CMD_EMPTY_FLAG},
  {"list", S_CMD_LIST, CMD_EMPTY_FLAG},
  /* Command stuffs */
  {"run", S_CMD_RUN, CMD_CHECK_CONNECT},
  {"xrun", S_CMD_XRUN, CMD_CHECK_CONNECT},
  {"commit", S_CMD_COMMIT, CMD_CHECK_CONNECT},
  {"rollback", S_CMD_ROLLBACK, CMD_CHECK_CONNECT},
  {"autocommit", S_CMD_AUTOCOMMIT, CMD_EMPTY_FLAG},
  {"checkpoint", S_CMD_CHECKPOINT, CMD_CHECK_CONNECT},
  {"killtran", S_CMD_KILLTRAN, CMD_CHECK_CONNECT},
  {"restart", S_CMD_RESTART, CMD_EMPTY_FLAG},
  /* Environment stuffs */
  {"shell_cmd", S_CMD_SHELL_CMD, CMD_EMPTY_FLAG},
  {"editor_cmd", S_CMD_EDIT_CMD, CMD_EMPTY_FLAG},
  {"print_cmd", S_CMD_PRINT_CMD, CMD_EMPTY_FLAG},
  {"pager_cmd", S_CMD_PAGER_CMD, CMD_EMPTY_FLAG},
  {"nopager", S_CMD_NOPAGER_CMD, CMD_EMPTY_FLAG},
  {"formatter_cmd", S_CMD_FORMATTER_CMD, CMD_EMPTY_FLAG},
  {"column-width", S_CMD_COLUMN_WIDTH, CMD_EMPTY_FLAG},
  {"string-width", S_CMD_STRING_WIDTH, CMD_EMPTY_FLAG},
  {"set", S_CMD_SET_PARAM, CMD_CHECK_CONNECT},
  {"get", S_CMD_GET_PARAM, CMD_CHECK_CONNECT},
  {"plan", S_CMD_PLAN_DUMP, CMD_CHECK_CONNECT},
  {"echo", S_CMD_ECHO, CMD_EMPTY_FLAG},
  {"date", S_CMD_DATE, CMD_EMPTY_FLAG},
  {"time", S_CMD_TIME, CMD_EMPTY_FLAG},
  {"line-output", S_CMD_LINE_OUTPUT, CMD_EMPTY_FLAG},
  {".hist", S_CMD_HISTO, CMD_EMPTY_FLAG},
  {".clear_hist", S_CMD_CLR_HISTO, CMD_EMPTY_FLAG},
  {".dump_hist", S_CMD_DUMP_HISTO, CMD_EMPTY_FLAG},
  {".x_hist", S_CMD_DUMP_CLR_HISTO, CMD_EMPTY_FLAG},
  /* Help stuffs */
  {"help", S_CMD_HELP, CMD_EMPTY_FLAG},
  {"schema", S_CMD_SCHEMA, CMD_CHECK_CONNECT},
  {"database", S_CMD_DATABASE, CMD_CHECK_CONNECT},
  {"trigger", S_CMD_TRIGGER, CMD_CHECK_CONNECT},
  {"info", S_CMD_INFO, CMD_EMPTY_FLAG},
  /* history stuffs */
  {"historyread", S_CMD_HISTORY_READ, CMD_EMPTY_FLAG},
  {"historylist", S_CMD_HISTORY_LIST, CMD_EMPTY_FLAG},

  {"trace", S_CMD_TRACE, CMD_CHECK_CONNECT},

  {"singleline", S_CMD_SINGLELINE, CMD_EMPTY_FLAG}
};

/*
 * csql_get_session_cmd_no() - find a session command
 *   return: SESSION_CMD number if success.
 *           if error, -1 is returned and csql_Error_code is set.
 *   input(in)
 *
 * Note:
 *   The search function succeed when there is only one entry which starts
 *   with the given string, or there is an entry which matches exactly.
 */
int
csql_get_session_cmd_no (const char *input)
{
  int i;			/* loop counter */
  int input_cmd_length;		/* input command length */
  int num_matches = 0;		/* # of matched commands */
  int matched_index = -1;	/* last matched entry index */

  if (*input == '\0')
    {
      /* csql>; means csql>;xrun */
      return S_CMD_XRUN;
    }

  input_cmd_length = (int) strlen (input);
  num_matches = 0;
  matched_index = -1;
  for (i = 0; i < (int) DIM (csql_Session_cmd_table); i++)
    {
      if (strncasecmp (input, csql_Session_cmd_table[i].text, input_cmd_length) == 0)
	{
	  int ses_cmd_length;

	  ses_cmd_length = (int) strlen (csql_Session_cmd_table[i].text);
	  if (ses_cmd_length == input_cmd_length)
	    {
	      return (csql_Session_cmd_table[i].cmd_no);
	    }
	  num_matches++;
	  matched_index = i;
	}
    }
  if (num_matches != 1)
    {
      csql_Error_code = (num_matches > 1) ? CSQL_ERR_SESS_CMD_AMBIGUOUS : CSQL_ERR_SESS_CMD_NOT_FOUND;
      return (-1);
    }
#if defined (CS_MODE)
  if (csql_Session_cmd_table[matched_index].flags & CMD_CHECK_CONNECT)
    {
      if (db_Connect_status != DB_CONNECTION_STATUS_CONNECTED)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_NO_CONNECT, 0);
	  return (-1);
	}
    }
#endif

  return (csql_Session_cmd_table[matched_index].cmd_no);
}

/*
 * csql_help_menu() - display appropriate help message
 *   return: none
 */
void
csql_help_menu (void)
{
  if (csql_append_more_line (0, msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_SESSION_CMD_TEXT))
      == CSQL_FAILURE)
    {
      goto error;
    }
  csql_display_more_lines (msgcat_message
			   (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_SESSION_CMD_TITLE_TEXT));

  csql_free_more_lines ();
  return;

error:
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  csql_free_more_lines ();
}

/*
 * csql_help_schema() - display schema information for given class name
 *   return: none
 *   class_name(in)
 */
void
csql_help_schema (const char *class_name)
{
  char **line_ptr;
  char class_title[2 * DB_MAX_IDENTIFIER_LENGTH + 2];
  char fixed_class_name[DB_MAX_IDENTIFIER_LENGTH];
  char *class_name_composed = NULL;
  int composed_size, class_name_size;
  class_description class_descr;

  if (class_name == NULL || class_name[0] == 0)
    {
      csql_Error_code = CSQL_ERR_CLASS_NAME_MISSED;
      goto error;
    }

  /* class name may be in Unicode decomposed form, in DB we store only composed form */
  class_name_size = (int) strlen (class_name);
  if (LANG_SYS_CODESET == INTL_CODESET_UTF8
      && unicode_string_need_compose (class_name, class_name_size, &composed_size, lang_get_generic_unicode_norm ()))
    {
      bool is_composed = false;

      class_name_composed = (char *) malloc (composed_size + 1);
      if (class_name_composed == NULL)
	{
	  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	  goto error;
	}

      unicode_compose_string (class_name, class_name_size, class_name_composed, &composed_size, &is_composed,
			      lang_get_generic_unicode_norm ());
      class_name_composed[composed_size] = '\0';
      assert (composed_size <= class_name_size);

      if (is_composed)
	{
	  class_name = class_name_composed;
	}
    }

  if (strlen (class_name) >= DB_MAX_IDENTIFIER_LENGTH)
    {
      csql_Error_code = CSQL_ERR_TOO_LONG_LINE;
      goto error;
    }
  else
    {
      strcpy (fixed_class_name, class_name);
      /* check that both lower and upper case are not truncated */
      if (intl_identifier_fix (fixed_class_name, -1, true) != NO_ERROR)
	{
	  csql_Error_code = CSQL_ERR_TOO_LONG_LINE;
	  goto error;
	}
      class_name = fixed_class_name;
    }

  if (class_descr.init (class_name) != NO_ERROR)
    {
      csql_Error_code = CSQL_ERR_SQL_ERROR;
      goto error;
    }

  snprintf (class_title, (2 * DB_MAX_IDENTIFIER_LENGTH + 2),
	    msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_CLASS_HEAD_TEXT),
	    class_descr.class_type);
  APPEND_HEAD_LINE (class_title);
  APPEND_MORE_LINE (5, class_descr.name);

  if (class_descr.supers != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_SUPER_CLASS_HEAD_TEXT));
      for (line_ptr = class_descr.supers; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.subs != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_SUB_CLASS_HEAD_TEXT));
      for (line_ptr = class_descr.subs; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_ATTRIBUTE_HEAD_TEXT));
  if (class_descr.attributes == NULL)
    {
      APPEND_MORE_LINE (5, msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_NONE_TEXT));
    }
  else
    {
      for (line_ptr = class_descr.attributes; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.class_attributes != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_CLASS_ATTRIBUTE_HEAD_TEXT));
      for (line_ptr = class_descr.class_attributes; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.constraints != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_CONSTRAINT_HEAD_TEXT));
      for (line_ptr = class_descr.constraints; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.object_id != NULL)
    {
      APPEND_MORE_LINE (0, "");
      APPEND_MORE_LINE (1, class_descr.object_id);
    }

  if (class_descr.methods != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_METHOD_HEAD_TEXT));
      for (line_ptr = class_descr.methods; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.class_methods != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_CLASS_METHOD_HEAD_TEXT));
      for (line_ptr = class_descr.class_methods; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.resolutions != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_RESOLUTION_HEAD_TEXT));
      for (line_ptr = class_descr.resolutions; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.method_files != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_METHFILE_HEAD_TEXT));
      for (line_ptr = class_descr.method_files; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_descr.query_spec != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_QUERY_SPEC_HEAD_TEXT));
      for (line_ptr = class_descr.query_spec; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (!class_descr.triggers.empty ())
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_HEAD_TEXT));
#if 0				//bSolo: temporary until evolve above gcc 4.4.7
/* *INDENT-OFF* */
      for (auto it : class_descr.triggers)
/* *INDENT-ON* */
      {
	APPEND_MORE_LINE (5, it);
      }
#else
      for (auto it = class_descr.triggers.begin (); it != class_descr.triggers.end (); ++it)
	{
	  APPEND_MORE_LINE (5, *it);
	}
#endif
    }

  if (!class_descr.partition.empty ())
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_PARTITION_HEAD_TEXT));
#if 0				//bSolo: temporary until evolve above gcc 4.4.7
/* *INDENT-OFF* */
      for (auto it : class_descr.partition)
/* *INDENT-ON* */
      {
	APPEND_MORE_LINE (5, it);
      }
#else
      for (auto it = class_descr.partition.begin (); it != class_descr.partition.end (); ++it)
	{
	  APPEND_MORE_LINE (5, *it);
	}
#endif
    }

  csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_SCHEMA_TITLE_TEXT));

  csql_free_more_lines ();

  if (class_name_composed != NULL)
    {
      free_and_init (class_name_composed);
    }
  return;

error:

  if (class_name_composed != NULL)
    {
      free_and_init (class_name_composed);
    }

  if (csql_Error_code == CSQL_ERR_SQL_ERROR)
    {
      csql_display_csql_err (0, 0);
    }
  else
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
  csql_free_more_lines ();
}

/*
 * csql_help_trigger() - display trigger information for given trigger name
 *   return: none
 *   trigger_name(in)
 */
void
csql_help_trigger (const char *trigger_name)
{
  char **all_triggers = NULL;
  char *trigger_name_composed = NULL;
  LC_FETCH_VERSION_TYPE read_fetch_instance_version;

  read_fetch_instance_version = TM_TRAN_READ_FETCH_VERSION ();
  db_set_read_fetch_instance_version (LC_FETCH_DIRTY_VERSION);

  if (trigger_name == NULL || strcmp (trigger_name, "*") == 0)
    {
      /* all classes */
      if (help_trigger_names (&all_triggers) != NO_ERROR)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}

      if (all_triggers == NULL)
	{
	  csql_display_more_lines (msgcat_message
				   (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_NONE_TITLE_TEXT));
	}
      else
	{
	  char **line_ptr;	/* pointer to each line */

	  for (line_ptr = all_triggers; *line_ptr != NULL; line_ptr++)
	    {
	      APPEND_MORE_LINE (5, *line_ptr);
	    }

	  csql_display_more_lines (msgcat_message
				   (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_ALL_TITLE_TEXT));
	}
    }
  else
    {
      int trigger_name_size, composed_size;
      /* trigger name is given */
      /* trigger name may be in Unicode decomposed form, in DB we store only composed form */
      trigger_name_size = (int) strlen (trigger_name);
      if (LANG_SYS_CODESET == INTL_CODESET_UTF8
	  && unicode_string_need_compose (trigger_name, trigger_name_size, &composed_size,
					  lang_get_generic_unicode_norm ()))
	{
	  bool is_composed = false;

	  trigger_name_composed = (char *) malloc (composed_size + 1);
	  if (trigger_name_composed == NULL)
	    {
	      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	      goto error;
	    }

	  unicode_compose_string (trigger_name, trigger_name_size, trigger_name_composed, &composed_size, &is_composed,
				  lang_get_generic_unicode_norm ());
	  trigger_name_composed[composed_size] = '\0';
	  assert (composed_size <= trigger_name_size);

	  if (is_composed)
	    {
	      trigger_name = trigger_name_composed;
	    }
	}

      trigger_description help;
      if (help.init (trigger_name) != NO_ERROR)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}

      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_NAME_TEXT));
      APPEND_MORE_LINE (5, help.name);

      if (help.status != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_STATUS_TEXT));
	  APPEND_MORE_LINE (5, help.status);
	}

      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_EVENT_TEXT));
      APPEND_MORE_LINE (5, help.full_event);

      if (help.condition != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_CONDITION_TIME_TEXT));
	  APPEND_MORE_LINE (5, help.condition_time);

	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_CONDITION_TEXT));
	  APPEND_MORE_LINE (5, help.condition);
	}

      if (help.action != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_ACTION_TIME_TEXT));
	  APPEND_MORE_LINE (5, help.action_time);

	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_ACTION_TEXT));
	  APPEND_MORE_LINE (5, help.action);
	}

      if (help.priority != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_PRIORITY_TEXT));
	  APPEND_MORE_LINE (5, help.priority);
	}

      if (help.comment != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_HELP_TRIGGER_COMMENT_TEXT));
	  APPEND_MORE_LINE (5, help.comment);
	}

      csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL,
					       CSQL_HELP_TRIGGER_TITLE_TEXT));
    }

  if (all_triggers != NULL)
    {
      help_free_names (all_triggers);
    }
  csql_free_more_lines ();

  if (trigger_name_composed != NULL)
    {
      free_and_init (trigger_name_composed);
    }

  db_set_read_fetch_instance_version (read_fetch_instance_version);

  return;

error:
  if (trigger_name_composed != NULL)
    {
      free_and_init (trigger_name_composed);
    }
  if (all_triggers != NULL)
    {
      help_free_names (all_triggers);
    }
  if (csql_Error_code == CSQL_ERR_SQL_ERROR)
    {
      csql_display_csql_err (0, 0);
    }
  else
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
  csql_free_more_lines ();

  db_set_read_fetch_instance_version (read_fetch_instance_version);
}

/*
 * csql_pipe_handler() - Generic longjmp'ing signal handler used
 *                     where we need to catch broken pipe
 *   return: none
 *   sig_no(in)
 */
static void
csql_pipe_handler (int sig_no)
{
  longjmp (csql_Jmp_buf, 1);
}

/*
 * csql_help_info() - display database information for given command
 *   return: none
 *   command(in): "schema [<class name>]"
 *                "trigger [<trigger name>]"
 *                "deferred"
 *                "workspace"
 *                "lock"
 *                "stats [<class name>]"
 *                "plan"
 *                "qcache"
 *   aucommit_flag(in): auto-commit mode flag
 */
void
csql_help_info (const char *command, int aucommit_flag)
{
  char *dup = NULL, *tok, *save;
  FILE *p_stream;		/* pipe stream to pager */
#if !defined(WINDOWS)
  void (*csql_intr_save) (int sig);
  void (*csql_pipe_save) (int sig);
#endif /* ! WINDOWS */

  if (!command)
    {
      csql_Error_code = CSQL_ERR_INFO_CMD_HELP;
      goto error;
    }

  dup = strdup (command);
  if (dup == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }
  tok = strtok_r (dup, " \t", &save);
  if (tok != NULL
      && (!strcasecmp (tok, "schema") || !strcasecmp (tok, "trigger") || !strcasecmp (tok, "deferred")
	  || !strcasecmp (tok, "workspace") || !strcasecmp (tok, "lock") || !strcasecmp (tok, "stats")
	  || !strcasecmp (tok, "logstat") || !strcasecmp (tok, "csstat") || !strcasecmp (tok, "plan")
	  || !strcasecmp (tok, "qcache") || !strcasecmp (tok, "trantable")))
    {
      int result;

#if !defined(WINDOWS)
      csql_intr_save = signal (SIGINT, SIG_IGN);
      csql_pipe_save = signal (SIGPIPE, SIG_IGN);
#else
      SetConsoleCtrlHandler (NULL, true);	/* ignore Ctrl + c */
#endif /* ! WINDOWS */
      result = NO_ERROR;

      p_stream = csql_popen (csql_Pager_cmd, csql_Output_fp);
      help_print_info (command, p_stream);
      if (aucommit_flag)
	{
	  result = db_commit_transaction ();
	}
      csql_pclose (p_stream, csql_Output_fp);
      if (aucommit_flag)
	{
	  if (result != NO_ERROR)
	    {
	      csql_display_csql_err (0, 0);
	      csql_check_server_down ();
	    }
	  else
	    {
	      csql_display_msg (msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_STAT_COMMITTED_TEXT));
	    }
	}
#if !defined(WINDOWS)
      signal (SIGINT, csql_intr_save);
      signal (SIGPIPE, csql_pipe_save);
#else
      SetConsoleCtrlHandler (NULL, false);
#endif /* ! WINDOWS */
    }
  else
    {
      csql_Error_code = CSQL_ERR_INFO_CMD_HELP;
      goto error;
    }

  free (dup);
  return;

error:
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  if (dup)
    {
      free (dup);
    }
}

static void
csql_dump_alltran (volatile TRANS_INFO * info)
{
  FILE *p_stream;		/* pipe stream to pager */

  if (setjmp (csql_Jmp_buf) == 0)
    {
      p_stream = csql_popen (csql_Pager_cmd, csql_Output_fp);

      fprintf (p_stream, csql_get_message (CSQL_KILLTRAN_TITLE_TEXT));
      for (int i = 0; i < info->num_trans; i++)
	{
	  fprintf (p_stream, csql_get_message (CSQL_KILLTRAN_FORMAT), info->tran[i].tran_index,
		   tran_get_tranlist_state_name (info->tran[i].state), info->tran[i].db_user,
		   info->tran[i].host_name, info->tran[i].process_id, info->tran[i].program_name);
	}

      csql_pclose (p_stream, csql_Output_fp);
    }
}

/*
 * csql_killtran() - kill a transaction
 *   return: none
 *   argument: tran index or NULL (dump transaction list)
 */
void
csql_killtran (const char *argument)
{
  TRANS_INFO *info = NULL;
  int tran_index = -1, i;
#if !defined(WINDOWS)
  void (*csql_pipe_save) (int sig);
#endif /* ! WINDOWS */

  if (argument)
    {
      tran_index = atoi (argument);
    }

  info = logtb_get_trans_info (false);
  if (info == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      goto error;
    }

  /* dump transaction */
  if (tran_index <= 0)
    {
#if !defined(WINDOWS)
      csql_pipe_save = signal (SIGPIPE, &csql_pipe_handler);
#endif /* ! WINDOWS */
      csql_dump_alltran (info);
#if !defined(WINDOWS)
      signal (SIGPIPE, csql_pipe_save);
#endif /* ! WINDOWS */
    }
  /* kill transaction */
  else
    {
      for (i = 0; i < info->num_trans; i++)
	{
	  if (info->tran[i].tran_index == tran_index)
	    {
	      fprintf (csql_Output_fp, csql_get_message (CSQL_KILLTRAN_TITLE_TEXT));
	      fprintf (csql_Output_fp, csql_get_message (CSQL_KILLTRAN_FORMAT), info->tran[i].tran_index,
		       tran_get_tranlist_state_name (info->tran[i].state), info->tran[i].db_user,
		       info->tran[i].host_name, info->tran[i].process_id, info->tran[i].program_name);

	      if (thread_kill_tran_index (info->tran[i].tran_index, info->tran[i].db_user, info->tran[i].host_name,
					  info->tran[i].process_id) == NO_ERROR)
		{
		  csql_display_msg (csql_get_message (CSQL_STAT_KILLTRAN_TEXT));
		}
	      else
		{
		  csql_display_msg (csql_get_message (CSQL_STAT_KILLTRAN_FAIL_TEXT));
		}
	      break;
	    }
	}
    }

  if (info != NULL)
    {
      logtb_free_trans_info (info);
    }

  return;

error:
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  if (info != NULL)
    {
      logtb_free_trans_info (info);
    }
}
