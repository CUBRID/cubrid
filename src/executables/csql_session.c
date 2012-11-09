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
 * csql_session.c : menu driver of csql
 */

#ident "$Id$"

#include "config.h"

#include <stdarg.h>
#include <string.h>
#include <signal.h>

#include "csql.h"
#include "memory_alloc.h"
#include "util_func.h"
#include "network_interface_cl.h"

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

/* session command table */
typedef struct
{
  const char *text;		/* lower case cmd name */
  SESSION_CMD cmd_no;		/* command number */
} SESSION_CMD_TABLE;

static SESSION_CMD_TABLE csql_Session_cmd_table[] = {
  /* File stuffs */
  {"read", S_CMD_READ},
  {"write", S_CMD_WRITE},
  {"append", S_CMD_APPEND},
  {"print", S_CMD_PRINT},
  {"shell", S_CMD_SHELL},
  {"!", S_CMD_SHELL},
  {"cd", S_CMD_CD},
  {"exit", S_CMD_EXIT},
  /* Edit stuffs */
  {"clear", S_CMD_CLEAR},
  {"edit", S_CMD_EDIT},
  {"list", S_CMD_LIST},
  /* Command stuffs */
  {"run", S_CMD_RUN},
  {"xrun", S_CMD_XRUN},
  {"commit", S_CMD_COMMIT},
  {"rollback", S_CMD_ROLLBACK},
  {"autocommit", S_CMD_AUTOCOMMIT},
  {"checkpoint", S_CMD_CHECKPOINT},
  {"killtran", S_CMD_KILLTRAN},
  {"restart", S_CMD_RESTART},
  /* Environment stuffs */
  {"shell_cmd", S_CMD_SHELL_CMD},
  {"editor_cmd", S_CMD_EDIT_CMD},
  {"print_cmd", S_CMD_PRINT_CMD},
  {"pager_cmd", S_CMD_PAGER_CMD},
  {"nopager", S_CMD_NOPAGER_CMD},
  {"set", S_CMD_SET_PARAM},
  {"get", S_CMD_GET_PARAM},
  {"plan", S_CMD_PLAN_DUMP},
  {"echo", S_CMD_ECHO},
  {"date", S_CMD_DATE},
  {"time", S_CMD_TIME},
  {"line-output", S_CMD_LINE_OUTPUT},
  {".hist", S_CMD_HISTO},
  {".clear_hist", S_CMD_CLR_HISTO},
  {".dump_hist", S_CMD_DUMP_HISTO},
  {".x_hist", S_CMD_DUMP_CLR_HISTO},
  /* Help stuffs */
  {"help", S_CMD_HELP},
  {"schema", S_CMD_SCHEMA},
  {"database", S_CMD_DATABASE},
  {"trigger", S_CMD_TRIGGER},
  {"info", S_CMD_INFO},
  /* history stuffs */
  {"historyread", S_CMD_HISTORY_READ},
  {"historylist", S_CMD_HISTORY_LIST}
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

  intl_char_count ((unsigned char *) input, strlen (input), lang_charset (),
		   &input_cmd_length);
  num_matches = 0;
  matched_index = -1;
  for (i = 0; i < (int) DIM (csql_Session_cmd_table); i++)
    {
      if (intl_identifier_ncasecmp
	  (input, csql_Session_cmd_table[i].text, input_cmd_length) == 0)
	{
	  int ses_cmd_length;

	  intl_char_count ((unsigned char *) csql_Session_cmd_table[i].text,
			   strlen (csql_Session_cmd_table[i].text),
			   lang_charset (), &ses_cmd_length);
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
      csql_Error_code = (num_matches > 1) ? CSQL_ERR_SESS_CMD_AMBIGUOUS :
	CSQL_ERR_SESS_CMD_NOT_FOUND;
      return (-1);
    }
  return (csql_Session_cmd_table[matched_index].cmd_no);
}

/*
 * csql_help_menu() - display appropriate help message
 *   return: none
 */
void
csql_help_menu (void)
{
  if (csql_append_more_line (0, msgcat_message (MSGCAT_CATALOG_CSQL,
						MSGCAT_CSQL_SET_CSQL,
						CSQL_HELP_SESSION_CMD_TEXT))
      == CSQL_FAILURE)
    {
      goto error;
    }
  csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL,
					   MSGCAT_CSQL_SET_CSQL,
					   CSQL_HELP_SESSION_CMD_TITLE_TEXT));

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
  CLASS_HELP *class_schema = NULL;
  char **line_ptr;
  char class_title[2 * DB_MAX_IDENTIFIER_LENGTH + 2];

  if (class_name == NULL || class_name[0] == 0)
    {
      csql_Error_code = CSQL_ERR_CLASS_NAME_MISSED;
      goto error;
    }

  class_schema = (CLASS_HELP *) NULL;
  if ((class_schema = obj_print_help_class_name (class_name)) == NULL)
    {
      csql_Error_code = CSQL_ERR_SQL_ERROR;
      goto error;
    }

  sprintf (class_title, msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_CLASS_HEAD_TEXT),
	   class_schema->class_type);
  APPEND_HEAD_LINE (class_title);
  APPEND_MORE_LINE (5, class_schema->name);

  if (class_schema->supers != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_SUPER_CLASS_HEAD_TEXT));
      for (line_ptr = class_schema->supers; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->subs != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_SUB_CLASS_HEAD_TEXT));
      for (line_ptr = class_schema->subs; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
				    MSGCAT_CSQL_SET_CSQL,
				    CSQL_HELP_ATTRIBUTE_HEAD_TEXT));
  if (class_schema->attributes == NULL)
    {
      APPEND_MORE_LINE (5, msgcat_message (MSGCAT_CATALOG_CSQL,
					   MSGCAT_CSQL_SET_CSQL,
					   CSQL_HELP_NONE_TEXT));
    }
  else
    {
      for (line_ptr = class_schema->attributes; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->class_attributes != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_CLASS_ATTRIBUTE_HEAD_TEXT));
      for (line_ptr = class_schema->class_attributes; *line_ptr != NULL;
	   line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->constraints != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_CONSTRAINT_HEAD_TEXT));
      for (line_ptr = class_schema->constraints; *line_ptr != NULL;
	   line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->object_id != NULL)
    {
      APPEND_MORE_LINE (0, "");
      APPEND_MORE_LINE (1, class_schema->object_id);
    }

  if (class_schema->methods != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_METHOD_HEAD_TEXT));
      for (line_ptr = class_schema->methods; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->class_methods != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_CLASS_METHOD_HEAD_TEXT));
      for (line_ptr = class_schema->class_methods; *line_ptr != NULL;
	   line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->resolutions != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_RESOLUTION_HEAD_TEXT));
      for (line_ptr = class_schema->resolutions; *line_ptr != NULL;
	   line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->method_files != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_METHFILE_HEAD_TEXT));
      for (line_ptr = class_schema->method_files; *line_ptr != NULL;
	   line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->query_spec != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_QUERY_SPEC_HEAD_TEXT));
      for (line_ptr = class_schema->query_spec; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->triggers != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_TRIGGER_HEAD_TEXT));
      for (line_ptr = class_schema->triggers; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  if (class_schema->partition != NULL)
    {
      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_PARTITION_HEAD_TEXT));
      for (line_ptr = class_schema->partition; *line_ptr != NULL; line_ptr++)
	{
	  APPEND_MORE_LINE (5, *line_ptr);
	}
    }

  csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL,
					   MSGCAT_CSQL_SET_CSQL,
					   CSQL_HELP_SCHEMA_TITLE_TEXT));

  obj_print_help_free_class (class_schema);
  csql_free_more_lines ();

  return;

error:

  if (class_schema != NULL)
    {
      obj_print_help_free_class (class_schema);
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
  TRIGGER_HELP *help = NULL;

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
	  csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL,
						   MSGCAT_CSQL_SET_CSQL,
						   CSQL_HELP_TRIGGER_NONE_TITLE_TEXT));
	}
      else
	{
	  char **line_ptr;	/* pointer to each line */

	  for (line_ptr = all_triggers; *line_ptr != NULL; line_ptr++)
	    {
	      APPEND_MORE_LINE (5, *line_ptr);
	    }

	  csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL,
						   MSGCAT_CSQL_SET_CSQL,
						   CSQL_HELP_TRIGGER_ALL_TITLE_TEXT));
	}
    }
  else
    {
      /* class name is given */
      if ((help = help_trigger_name (trigger_name)) == NULL)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}

      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_TRIGGER_NAME_TEXT));
      APPEND_MORE_LINE (5, help->name);

      if (help->status != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					    MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_STATUS_TEXT));
	  APPEND_MORE_LINE (5, help->status);
	}

      APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					MSGCAT_CSQL_SET_CSQL,
					CSQL_HELP_TRIGGER_EVENT_TEXT));
      APPEND_MORE_LINE (5, help->full_event);

      if (help->condition != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					    MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_CONDITION_TIME_TEXT));
	  APPEND_MORE_LINE (5, help->condition_time);

	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					    MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_CONDITION_TEXT));
	  APPEND_MORE_LINE (5, help->condition);
	}

      if (help->action != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					    MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_ACTION_TIME_TEXT));
	  APPEND_MORE_LINE (5, help->action_time);

	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					    MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_ACTION_TEXT));
	  APPEND_MORE_LINE (5, help->action);
	}

      if (help->priority != NULL)
	{
	  APPEND_HEAD_LINE (msgcat_message (MSGCAT_CATALOG_CSQL,
					    MSGCAT_CSQL_SET_CSQL,
					    CSQL_HELP_TRIGGER_PRIORITY_TEXT));
	  APPEND_MORE_LINE (5, help->priority);
	}

      csql_display_more_lines (msgcat_message (MSGCAT_CATALOG_CSQL,
					       MSGCAT_CSQL_SET_CSQL,
					       CSQL_HELP_TRIGGER_TITLE_TEXT));
    }

  if (all_triggers != NULL)
    {
      help_free_names (all_triggers);
    }
  if (help != NULL)
    {
      help_free_trigger (help);
    }
  csql_free_more_lines ();

  return;

error:
  if (all_triggers != NULL)
    {
      help_free_names (all_triggers);
    }
  if (help != NULL)
    {
      help_free_trigger (help);
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
  char *dup = NULL, *tok;
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
  tok = strtok (dup, " \t");
  if (tok != NULL &&
      (!strcasecmp (tok, "schema") || !strcasecmp (tok, "trigger") ||
       !strcasecmp (tok, "deferred") || !strcasecmp (tok, "workspace") ||
       !strcasecmp (tok, "lock") || !strcasecmp (tok, "stats") ||
       !strcasecmp (tok, "logstat") || !strcasecmp (tok, "csstat") ||
       !strcasecmp (tok, "plan") || !strcasecmp (tok, "qcache") ||
       !strcasecmp (tok, "trantable")))
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
	      csql_display_msg (msgcat_message (MSGCAT_CATALOG_CSQL,
						MSGCAT_CSQL_SET_CSQL,
						CSQL_STAT_COMMITTED_TEXT));
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
  FILE *p_stream;		/* pipe stream to pager */
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
      if (setjmp (csql_Jmp_buf) == 0)
	{
	  p_stream = csql_popen (csql_Pager_cmd, csql_Output_fp);

	  fprintf (p_stream, csql_get_message (CSQL_KILLTRAN_TITLE_TEXT));
	  for (i = 0; i < info->num_trans; i++)
	    {
	      fprintf (p_stream, csql_get_message (CSQL_KILLTRAN_FORMAT),
		       info->tran[i].tran_index,
		       TRAN_STATE_CHAR (info->tran[i].state),
		       info->tran[i].db_user, info->tran[i].host_name,
		       info->tran[i].process_id, info->tran[i].program_name);
	    }

	  csql_pclose (p_stream, csql_Output_fp);
	}
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
	      fprintf (csql_Output_fp,
		       csql_get_message (CSQL_KILLTRAN_TITLE_TEXT));
	      fprintf (csql_Output_fp,
		       csql_get_message (CSQL_KILLTRAN_FORMAT),
		       info->tran[i].tran_index,
		       TRAN_STATE_CHAR (info->tran[i].state),
		       info->tran[i].db_user, info->tran[i].host_name,
		       info->tran[i].process_id, info->tran[i].program_name);

	      if (thread_kill_tran_index (info->tran[i].tran_index,
					  info->tran[i].db_user,
					  info->tran[i].host_name,
					  info->tran[i].process_id) ==
		  NO_ERROR)
		{
		  csql_display_msg (csql_get_message
				    (CSQL_STAT_KILLTRAN_TEXT));
		}
	      else
		{
		  csql_display_msg (csql_get_message
				    (CSQL_STAT_KILLTRAN_FAIL_TEXT));
		}
	      break;
	    }
	}
    }

  if (info)
    {
      logtb_free_trans_info (info);
    }
  return;

error:
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  if (info)
    {
      logtb_free_trans_info (info);
    }
}
