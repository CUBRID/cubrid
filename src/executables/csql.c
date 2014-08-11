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
 * csql.c : csql main module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#if defined(WINDOWS)
#include <direct.h>
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else /* !WINDOWS */
#include <sys/time.h>
#include <sys/errno.h>
#include <signal.h>
#include <wctype.h>
#include <editline/readline.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif /* !WINDOWS */

#include "csql.h"
#include "system_parameter.h"
#include "message_catalog.h"
#include "porting.h"
#include "release_string.h"
#include "error_manager.h"
#include "language_support.h"
#include "network.h"
#include "schema_manager.h"
#include "optimizer.h"
#include "environment_variable.h"
#include "tcp.h"
#include "db.h"
#include "parser.h"
#include "network_interface_cl.h"
#include "utility.h"
#include "tsc_timer.h"

#if defined(WINDOWS)
#include "file_io.h"		/* needed for _wyield() */
#endif /* WINDOWS */

/* input type specification for csql_execute_statements() */
enum
{
  FILE_INPUT = 0,		/* FILE stream */
  STRING_INPUT = 1,		/* null-terminated string */
  EDITOR_INPUT = 2		/* command buffer */
};

/* the return value of csql_do_session_cmd() */
enum
{
  DO_CMD_SUCCESS = 0,
  DO_CMD_FAILURE = 1,
  DO_CMD_EXIT = 2
};

#define CSQL_SESSION_COMMAND_PREFIX(C)	(((C) == ';') || ((C) == '!'))

/* size of input buffer */
#define LINE_BUFFER_SIZE         (4000)
#define COLUMN_WIDTH_INFO_LIST_INIT_SIZE 24
#define NOT_FOUND -1

#if defined (ENABLE_UNUSED_FUNCTION)
#if !defined(WINDOWS)
/* current max keyword is 16 + nul char + 3 for expansion */

static int csql_Keyword_num;
static KEYWORD_RECORD *csql_Keyword_list;
#endif /* !WINDOWS */
#endif /* ENABLE_UNUSED_FUNCTION */

int (*csql_text_utf8_to_console) (const char *, const int, char **,
				  int *) = NULL;

int (*csql_text_console_to_utf8) (const char *, const int, char **,
				  int *) = NULL;

int csql_Row_count;
int csql_Num_failures;

/* command editor lines */
int csql_Line_lwm = -1;

/* default environment command names */
char csql_Print_cmd[PATH_MAX] = "lpr";
char csql_Pager_cmd[PATH_MAX] = "more";	/* PAGER does not work on WINDOWS */
#if defined(WINDOWS)
char csql_Editor_cmd[PATH_MAX] = "notepad";
#else
char csql_Editor_cmd[PATH_MAX] = "vi";
#endif

#if defined(WINDOWS)
char csql_Shell_cmd[PATH_MAX] = "command.com";
#else
char csql_Shell_cmd[PATH_MAX] = "csh";
#endif

/* tty file stream which is used for conversation with users.
 * In batch mode, this will be set to "/dev/null"
 */
static FILE *csql_Tty_fp = NULL;

/* scratch area to make a message text to be displayed.
 * NOTE: Never put chars more than sizeof(csql_Scratch_text).
 */
char csql_Scratch_text[SCRATCH_TEXT_LEN];

int csql_Error_code = NO_ERROR;

static char csql_Prompt[100];
static char csql_Name[100];

/*
 * Handles for the various files
 */
FILE *csql_Input_fp = NULL;
FILE *csql_Output_fp = NULL;
FILE *csql_Error_fp = NULL;

/*
 * Global longjmp environment to terminate the csql() interpreter in the
 * event of fatal error.  This should be used rather than calling
 * exit(), primarily for the Windows version of the interpreter.
 *
 * Set csql_Exit_status to the numeric status code to be returned from
 * the csql() function after the longjmp has been performed.
 */
static jmp_buf csql_Exit_env;
static int csql_Exit_status = EXIT_SUCCESS;

/* this is non-zero if there is a dangling connection to a database */
static bool csql_Database_connected = false;

static bool csql_Is_interactive = false;
static bool csql_Is_sigint_caught = false;
static bool csql_Is_echo_on = false;
enum
{ HISTO_OFF, HISTO_ON };
static int csql_Is_histo_on = HISTO_OFF;
static bool csql_Is_time_on = true;

static jmp_buf csql_Jmp_buf;

static CSQL_COLUMN_WIDTH_INFO *csql_column_width_info_list = NULL;
static int csql_column_width_info_list_size = 0;
static int csql_column_width_info_list_index = 0;

static bool csql_Query_trace = false;

#if defined (ENABLE_UNUSED_FUNCTION)
#if !defined(WINDOWS)
static char *csql_keyword_generator (const char *text, int state);
static char **csql_cmd_completion_handler (const char *text, int start,
					   int end);
static void init_readline ();
#endif /* ! WINDOWS */
#endif /* ENABLE_UNUSED_FUNCTION */

static void free_csql_column_width_info_list ();
static int initialize_csql_column_width_info_list ();
static int get_column_name_argument (char **column_name,
				     char **val_str, char *argument);
static void csql_pipe_handler (int sig_no);
static void display_buffer (void);
static void csql_execute_rcfile (CSQL_ARGUMENT * csql_arg);
static void start_csql (CSQL_ARGUMENT * csql_arg);
static void csql_read_file (const char *file_name);
static void csql_write_file (const char *file_name, int append_flag);
static void display_error (DB_SESSION * session, int stmt_start_line_no);
static void free_attr_spec (DB_QUERY_TYPE ** attr_spec);
static void csql_print_database (void);
static void csql_set_sys_param (const char *arg_str);
static void csql_get_sys_param (const char *arg_str);
static void csql_set_plan_dump (const char *arg_str);
static void csql_exit_init (void);
static void csql_exit_cleanup (void);
static void csql_print_buffer (void);
static void csql_change_working_directory (const char *dirname);
static void csql_exit_session (int error);

static int csql_execute_statements (const CSQL_ARGUMENT * csql_arg, int type,
				    const void *stream, int line_no);

static int csql_do_session_cmd (char *line_read, CSQL_ARGUMENT * csql_arg);
static void csql_set_trace (const char *arg_str);
static void csql_display_trace (void);

#if defined (ENABLE_UNUSED_FUNCTION)
#if !defined(WINDOWS)
/*
 * for readline keyword completion
 */
/*
 * csql_keyword_generator()
 *   return: char*
 *   text(in)
 *   state(in)
 */
static char *
csql_keyword_generator (const char *text, int state)
{
  static int list_index, len;

  /* If this is a new word to complete, initialize now.  This
     includes saving the length of TEXT for efficiency, and
     initializing the index variable to 0. */
  if (!state)
    {
      list_index = 0;
      len = strlen (text);
    }
  if (len == 0)
    {
      return ((char *) NULL);
    }
  if (csql_Keyword_list == NULL)
    {
      return ((char *) NULL);
    }

  /* Return the next name which partially matches
     from the keyword list. */
  while (list_index < csql_Keyword_num)
    {
      if (strncasecmp ((csql_Keyword_list + list_index)->keyword, text, len)
	  == 0)
	{
	  char *ret_str = strdup ((csql_Keyword_list + list_index)->keyword);
	  list_index++;
	  return ret_str;
	}

      list_index++;
    }

  /* If no keyword matched, then return NULL. */
  return ((char *) NULL);
}

/*
 * csql_cmd_completion_handler()
 *   return: char**
 *   text(in)
 *   start(in)
 *   end(in)
 */
static char **
csql_cmd_completion_handler (const char *text, int start, int end)
{
  char **matches;

  matches = (char **) NULL;
  matches = completion_matches (text, csql_keyword_generator);
  rl_attempted_completion_over = 1;

  return (matches);
}

/*
 * init_readline() - initialize libedit module
 *   return: none
 */
static void
init_readline ()
{
  rl_attempted_completion_function = csql_cmd_completion_handler;
}
#endif /* !WINDOWS */
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * free_csql_column_width_info_list() - free csql_column_width_info_list
 * return: void
 */
static void
free_csql_column_width_info_list ()
{
  int i;

  if (csql_column_width_info_list == NULL)
    {
      csql_column_width_info_list_size = 0;
      csql_column_width_info_list_index = 0;

      return;
    }

  for (i = 0; i < csql_column_width_info_list_size; i++)
    {
      if (csql_column_width_info_list[i].name != NULL)
	{
	  free_and_init (csql_column_width_info_list[i].name);
	}
    }

  if (csql_column_width_info_list != NULL)
    {
      free_and_init (csql_column_width_info_list);
    }

  csql_column_width_info_list_size = 0;
  csql_column_width_info_list_index = 0;
}

/*
 * initialize_csql_column_width_info_list() - initialize csql_column_width_info_list
 * return: int
 */
static int
initialize_csql_column_width_info_list ()
{
  int i;

  csql_column_width_info_list = malloc (sizeof (CSQL_COLUMN_WIDTH_INFO)
					* COLUMN_WIDTH_INFO_LIST_INIT_SIZE);
  if (csql_column_width_info_list == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;

      return CSQL_FAILURE;
    }

  csql_column_width_info_list_size = COLUMN_WIDTH_INFO_LIST_INIT_SIZE;
  csql_column_width_info_list_index = 0;
  for (i = 0; i < csql_column_width_info_list_size; i++)
    {
      csql_column_width_info_list[i].name = NULL;
      csql_column_width_info_list[i].width = 0;
    }

  return CSQL_SUCCESS;
}

/*
 * csql_display_msg() - displays the given msg to output device
 *   return: none
 *   string(in)
 */
void
csql_display_msg (const char *string)
{
  csql_fputs ("\n", csql_Tty_fp);
  csql_fputs_console_conv (string, csql_Tty_fp);
  csql_fputs ("\n", csql_Tty_fp);
}

/*
 * csql_pipe_handler() generic longjmp'ing signal handler used
 *                   where we need to catch broken pipe.
 *   return: none
 *   sig_no(in)
 */
static void
csql_pipe_handler (int sig_no)
{
  longjmp (csql_Jmp_buf, 1);
}

/*
 * display_buffer() - display command buffer into stdout
 *   return: none
 */
static void
display_buffer (void)
{
  int l = 1;
  FILE *pf;
#if !defined(WINDOWS)
  void (*csql_pipe_save) (int);
#endif /* !WINDOWS */

#if !defined(WINDOWS)
  /* There is no SIGPIPE on WINDOWS */
  csql_pipe_save = os_set_signal_handler (SIGPIPE, &csql_pipe_handler);
#endif /* !WINDOWS */
  if (setjmp (csql_Jmp_buf) == 0)
    {
      char *edit_contents, *p;
      pf = csql_popen (csql_Pager_cmd, csql_Output_fp);

      edit_contents = csql_edit_contents_get ();

      putc ('\n', pf);
      while (edit_contents != NULL && *edit_contents != '\0')
	{
	  fprintf (pf, "%4d  ", l++);
	  p = strchr (edit_contents, '\n');
	  if (p)
	    {
	      fwrite (edit_contents, 1, p - edit_contents, pf);
	      edit_contents = p + 1;
	    }
	  else
	    {
	      fwrite (edit_contents, 1, strlen (edit_contents), pf);
	      edit_contents = NULL;
	    }
	  fprintf (pf, "\n");
	}
      putc ('\n', pf);

      csql_pclose (pf, csql_Output_fp);
    }
#if !defined(WINDOWS)
  (void) os_set_signal_handler (SIGPIPE, csql_pipe_save);
#endif /* !WINDOWS */
}



/*
 * start_csql()
 *   return: none
 *   sql_arg(in/out): CSQL_ARGUMENT structure
 *
 * Note:
 * There are four file pointers associated
 *      stdin     - input source
 *      stdout    - output file stream
 *      stderr    - error message file stream
 *      tty_fp    - conversation terminal file stream.
 *                  either NULL or stderr
 *
 * if -o is given, the output file descriptor is duplicated to STDOU_FILENO.
 * Also, if -i is given, -c is given or stdin is not a tty,
 *      `tty_fp' will be set to NULL. (No conversational messages)
 * Otherwise, `tty_fp' will be set to stderr
 *
 * If `single_line_execution' is true, it attemts to execute as soon as
 * it get a line. There is command buffer associated. This is effective
 * only when INTERACTIVE mode (stdin is tty).
 * If `command' is not NULL, it'll execute the command and exit and
 * `-i' option, preceding pipe (if any), `-s' option had no effect.
 */
static void
start_csql (CSQL_ARGUMENT * csql_arg)
{
#if !defined(WINDOWS)
  int i;
#endif /* !WINDOWS */
  unsigned char line_buf[LINE_BUFFER_SIZE];
  unsigned char utf8_line_buf[INTL_UTF8_MAX_CHAR_SIZE * LINE_BUFFER_SIZE];
  char *line_read = NULL;
  int line_length;
  int line_no;
  char *ptr;			/* loop pointer */
  char *line_read_alloced = NULL;
  bool is_first_read_line = true;
  bool read_whole_line;

  /* check in string block or comment block or identifier block */
  bool is_in_block = false;

  if (csql_arg->column_output && csql_arg->line_output)
    {
      csql_Error_code = CSQL_ERR_INVALID_ARG_COMBINATION;
      goto fatal_error;
    }

  csql_Output_fp = stdout;

  if (csql_arg->out_file_name != NULL)
    {
      csql_Output_fp = fopen (csql_arg->out_file_name, "w");
      if (csql_Output_fp == NULL)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto fatal_error;
	}
    }

  if (initialize_csql_column_width_info_list () != CSQL_SUCCESS)
    {
      goto fatal_error;
    }

  /* For batch file input and SQL/X command argument input */
  csql_Tty_fp = NULL;
  if (csql_arg->command)
    {
      /* command input */
      csql_exit_session (csql_execute_statements
			 (csql_arg, STRING_INPUT, csql_arg->command, -1));
    }

  if (!csql_Is_interactive && !csql_arg->single_line_execution)
    {
      csql_exit_session (csql_execute_statements
			 (csql_arg, FILE_INPUT, csql_Input_fp, -1));
    }

  /* Start interactive conversation or single line execution */
  if (csql_Is_interactive)
    {
      csql_Tty_fp = csql_Error_fp;

      if (lang_charset () == INTL_CODESET_UTF8)
	{
	  TEXT_CONVERSION *tc = lang_get_txt_conv ();

	  if (tc != NULL)
	    {
	      csql_text_utf8_to_console = tc->utf8_to_text_func;
	      csql_text_console_to_utf8 = tc->text_to_utf8_func;
	    }
	}
    }

  /* display product title */
  snprintf (csql_Scratch_text, SCRATCH_TEXT_LEN, "\n\t%s\n\n",
	    csql_get_message (CSQL_INITIAL_CSQL_TITLE));
  csql_fputs_console_conv (csql_Scratch_text, csql_Tty_fp);

  snprintf (csql_Scratch_text, SCRATCH_TEXT_LEN, "\n%s\n\n",
	    csql_get_message (CSQL_INITIAL_HELP_MSG));
  csql_fputs_console_conv (csql_Scratch_text, csql_Tty_fp);

#if !defined(WINDOWS)
  if (csql_Is_interactive)
    {
#if defined (ENABLE_UNUSED_FUNCTION)
      init_readline ();
#endif /* ENABLE_UNUSED_FUNCTION */
      stifle_history (prm_get_integer_value (PRM_ID_CSQL_HISTORY_NUM));
      using_history ();
#if defined (ENABLE_UNUSED_FUNCTION)
      csql_Keyword_list = pt_get_keyword_rec (&csql_Keyword_num);
#endif /* ENABLE_UNUSED_FUNCTION */
    }
#endif /* !WINDOWS */

  for (line_no = 1;; line_no++)
    {
      if (db_Connect_status != DB_CONNECTION_STATUS_CONNECTED)
	{
	  csql_Database_connected = false;
	  fputs ("!", csql_Output_fp);
	}

      read_whole_line = false;

      memset (line_buf, 0, LINE_BUFFER_SIZE);
      memset (utf8_line_buf, 0, INTL_UTF8_MAX_CHAR_SIZE * LINE_BUFFER_SIZE);

      if (csql_Is_interactive)
	{
#if defined(WINDOWS)
	  fputs (csql_Prompt, csql_Output_fp);	/* display prompt */
	  line_read =
	    fgets ((char *) line_buf, LINE_BUFFER_SIZE, csql_Input_fp);
#else
	  if ((line_read = readline (csql_Prompt)) != NULL)
	    {
	      if (line_read_alloced != NULL)
		{
		  free (line_read_alloced);
		  line_read_alloced = NULL;
		}

	      line_read_alloced = line_read;

	      /* readline assure whole line user typed is located in
	       * returned buffer
	       * (but it doesn't contain '\n' in it)
	       */
	      read_whole_line = true;
	    }
#endif /* WINDOWS */
	}
      else
	{
	  /* If input line exeeds LINE_BUFFER_SIZE, line_buf couldn't contain '\n'
	   * character in it.
	   * So, read_whole_line will be remained as false.
	   */
	  line_read =
	    fgets ((char *) line_buf, LINE_BUFFER_SIZE, csql_Input_fp);
	}

      fflush (csql_Output_fp);

      if (line_read == NULL)
	{
	  if (errno == EINTR && !feof (csql_Input_fp))
	    {
	      fprintf (csql_Output_fp, "\n");
	      continue;
	    }

	  /* Normal end condtion (with -i option) */
	  if (line_read_alloced != NULL)
	    {
	      free (line_read_alloced);
	      line_read_alloced = NULL;
	    }
	  csql_edit_contents_finalize ();
	  csql_exit_session (0);
	}

      line_length = strlen (line_read);

      if (csql_Is_interactive)
	{
	  char *utf8_line_read = NULL;

	  if (csql_text_console_to_utf8 != NULL)
	    {
	      int utf8_line_buf_size = sizeof (utf8_line_buf);

	      utf8_line_read = (char *) utf8_line_buf;
	      if ((*csql_text_console_to_utf8) (line_read, line_length,
						&utf8_line_read,
						&utf8_line_buf_size)
		  != NO_ERROR)
		{
		  goto error_continue;
		}
	      if (utf8_line_read != NULL)
		{
		  line_read = utf8_line_read;
		  line_length = strlen (line_read);
		}
	    }
	}

      /* skip UTF-8 BOM if present */
      if (is_first_read_line && intl_is_bom_magic (line_read, line_length))
	{
	  line_read += 3;
	  line_length -= 3;
	}
      is_first_read_line = false;

      for (ptr = line_read + line_length - 1; line_length > 0; ptr--)
	{
	  if (*ptr == '\n')
	    {
	      read_whole_line = true;
	    }

	  if (*ptr == '\n' || *ptr == '\r')
	    {
	      *ptr = '\0';
	      line_length--;
	    }
	  else
	    {
	      break;
	    }
	}

      if (CSQL_SESSION_COMMAND_PREFIX (line_read[0]) && is_in_block == false)
	{
	  int ret;
	  ret = csql_do_session_cmd (line_read, csql_arg);
	  if (ret == DO_CMD_EXIT)
	    {
	      if (line_read_alloced != NULL)
		{
		  free (line_read_alloced);
		  line_read_alloced = NULL;
		}
	      csql_edit_contents_finalize ();
	      csql_exit_session (0);
	    }
	  else if (ret == DO_CMD_FAILURE)
	    {
	      goto error_continue;
	    }

	  if (csql_Is_interactive)
	    {
	      line_no = 0;
	    }
	  continue;
	}
      else
	{
	  bool csql_execute = false;

	  if (csql_edit_contents_append (line_read, read_whole_line) !=
	      CSQL_SUCCESS)
	    {
	      goto error_continue;
	    }

	  if (feof (csql_Input_fp))
	    {
	      /* if eof is reached, execute all */
	      csql_execute = true;
	      is_in_block = false;
	    }
	  else
	    {
	      csql_walk_statement (line_read);
	      /* because we don't want to execute session commands
	       * in string block or comment block or identifier block
	       */
	      is_in_block = csql_is_statement_in_block ();

	      if (csql_arg->single_line_execution
		  /* read_whole_line == false means that
		   * fgets couldn't read whole line (exceeds buffer size)
		   * so we should concat next line before execute it.
		   */
		  && read_whole_line == true && csql_is_statement_complete ())
		{
		  /* if eof is not reached,
		   * execute it only on single line execution mode with
		   * complete statement
		   */
		  csql_execute = true;
		}
	    }

	  if (csql_execute)
	    {
	      /* single-line-oriented execution */
	      csql_execute_statements (csql_arg, EDITOR_INPUT, NULL, line_no);
	      csql_edit_contents_clear ();
	      if (csql_Is_interactive)
		{
		  line_no = 0;
		}
	    }
	}

      continue;

    error_continue:

      if (csql_Is_interactive)
	{
	  line_no = 0;
	}
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }

fatal_error:
  csql_edit_contents_finalize ();
  if (histo_is_supported ())
    {
      if (csql_Is_histo_on != HISTO_OFF)
	{
	  csql_Is_histo_on = HISTO_OFF;
	  histo_stop ();
	}
    }

  db_end_session ();
  db_shutdown ();
  csql_Database_connected = false;
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  csql_exit (EXIT_FAILURE);
}


static int
csql_do_session_cmd (char *line_read, CSQL_ARGUMENT * csql_arg)
{
  char *ptr;
  char *sess_end = NULL;	/* end pos of session command */
  char sess_end_char = '\0';	/* orginal char in end pos of session command */
  char *sess_cmd;		/* session command pointer */
  char *argument;		/* argument str */
  int cmd_no;			/* session command number */
  char *argument_end = NULL;
  int argument_len = 0;
  int error_code;
#if !defined(WINDOWS)
  HIST_ENTRY *hist_entry;
#endif /* !WINDOWS */

  /* get session command and argument */
  ptr = line_read;
  if (csql_Is_echo_on)
    {
      fprintf (csql_Output_fp, "%s\n", line_read);
    }

  /* 'ptr' points to the prefix char. */
  for (ptr++; *ptr != '\0' && iswspace ((wint_t) (*ptr)); ptr++)
    {
      ;
    }
  sess_cmd = (char *) ptr;
  for (; *ptr != '\0' && !iswspace ((wint_t) (*ptr)); ptr++)
    {
      ;
    }
  if (iswspace ((wint_t) (*ptr)))
    {
      sess_end = ptr;
      sess_end_char = *ptr;
      *ptr++ = '\0';		/* put null-termination */
    }
  else
    {
      sess_end_char = '\0';
    }
  for (; *ptr != '\0' && iswspace ((wint_t) (*ptr)); ptr++)
    {
      ;
    }
  argument = (char *) ptr;

  /* remove the end wide-space of argument */
  argument_len = strlen (argument);
  if (argument_len > 0)
    {
      argument_end = argument + argument_len - 1;
      for (; argument_end != argument
	   && iswspace ((wint_t) (*argument_end)); --argument_end)
	{
	  *argument_end = '\0';
	}
    }

  /* Now, `sess_cmd' points to null-terminated session command name and
   * `argument' points to remaining argument (it may be '\0' if not given).
   */

  if (*sess_cmd == '\0' && csql_arg->single_line_execution == false)
    {
      return DO_CMD_SUCCESS;
    }

  cmd_no = csql_get_session_cmd_no (sess_cmd);
  if (cmd_no == -1)
    {
      return DO_CMD_FAILURE;
    }

  /* restore line_read string */
  if (sess_end != NULL)
    {
      *sess_end = sess_end_char;
    }

#if !defined(WINDOWS)
  if (csql_Is_interactive)
    {
      add_history (line_read);
    }
#endif /* !WINDOWS */

  er_clear ();

  switch ((SESSION_CMD) cmd_no)
    {
      /* File stuffs */

    case S_CMD_READ:		/* read a file */
      csql_read_file (argument);
      break;

    case S_CMD_WRITE:		/* write to a file */
    case S_CMD_APPEND:		/* append to a file */
      csql_write_file (argument, cmd_no == S_CMD_APPEND);
      break;

    case S_CMD_PRINT:
      csql_print_buffer ();
      break;

    case S_CMD_SHELL:		/* invoke shell */
      csql_invoke_system (csql_Shell_cmd);
      csql_fputs ("\n", csql_Tty_fp);
      break;

    case S_CMD_CD:
      csql_change_working_directory (argument);
      break;

    case S_CMD_EXIT:		/* exit */
      return DO_CMD_EXIT;

      /* Edit stuffs */

    case S_CMD_CLEAR:		/* clear editor buffer */
      csql_edit_contents_clear ();
      break;

    case S_CMD_EDIT:		/* invoke system editor */
      if (csql_invoke_system_editor () != CSQL_SUCCESS)
	{
	  return DO_CMD_FAILURE;
	}
      break;

    case S_CMD_LIST:		/* display buffer */
      display_buffer ();
      break;

      /* Command stuffs */
    case S_CMD_RUN:
      csql_execute_statements (csql_arg, EDITOR_INPUT, NULL, -1);
      break;

    case S_CMD_XRUN:
      csql_execute_statements (csql_arg, EDITOR_INPUT, NULL, -1);
      csql_edit_contents_clear ();
      break;

    case S_CMD_COMMIT:
      if (db_commit_transaction () < 0)
	{
	  csql_display_csql_err (0, 0);
	  csql_check_server_down ();
	}
      else
	{
	  csql_display_msg (csql_get_message (CSQL_STAT_COMMITTED_TEXT));
	}
      break;

    case S_CMD_ROLLBACK:
      if (db_abort_transaction () < 0)
	{
	  csql_display_csql_err (0, 0);
	  csql_check_server_down ();
	}
      else
	{
	  csql_display_msg (csql_get_message (CSQL_STAT_ROLLBACKED_TEXT));
	}
      break;

    case S_CMD_AUTOCOMMIT:
      if (!strcasecmp (argument, "on"))
	{
	  csql_arg->auto_commit = true;
	}
      else if (!strcasecmp (argument, "off"))
	{
	  csql_arg->auto_commit = false;
	}

      fprintf (csql_Output_fp, "AUTOCOMMIT IS %s\n",
	       (csql_arg->auto_commit
		&& prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT) ? "ON" :
		"OFF"));
      break;

    case S_CMD_CHECKPOINT:
      if (csql_arg->sysadm && au_is_dba_group_member (Au_user))
	{
	  error_code = db_checkpoint ();
	  if (error_code != NO_ERROR)
	    {
	      csql_display_csql_err (0, 0);
	    }
	  else
	    {
	      csql_display_msg (csql_get_message (CSQL_STAT_CHECKPOINT_TEXT));
	    }
	}
      else
	{
	  fprintf (csql_Output_fp, "Checkpointing is only allowed for"
		   " the csql started with --sysadm\n");
	}
      break;

    case S_CMD_KILLTRAN:
      if (csql_arg->sysadm && au_is_dba_group_member (Au_user))
	{
	  csql_killtran ((argument[0] == '\0') ? NULL : argument);
	}
      else
	{
	  fprintf (csql_Output_fp, "Killing transaction is only allowed"
		   " for the csql started with --sysadm\n");
	}
      break;

    case S_CMD_RESTART:
      if (csql_Database_connected)
	{
	  csql_Database_connected = false;
	  db_end_session ();
	  db_shutdown ();
	}
      er_init ("./csql.err", ER_NEVER_EXIT);
      if (db_restart_ex (UTIL_CSQL_NAME, csql_arg->db_name,
			 csql_arg->user_name, csql_arg->passwd,
			 NULL, db_get_client_type ()) != NO_ERROR)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  csql_display_csql_err (0, 0);
	  csql_check_server_down ();
	}
      else
	{
	  if (csql_arg->sysadm && au_is_dba_group_member (Au_user))
	    {
	      au_disable ();
	    }
	  csql_Database_connected = true;

	  if (csql_arg->trigger_action_flag == false)
	    {
	      db_disable_trigger ();
	    }

	  csql_display_msg (csql_get_message (CSQL_STAT_RESTART_TEXT));
	}
      break;

      /* Environment stuffs */
    case S_CMD_SHELL_CMD:
    case S_CMD_EDIT_CMD:
    case S_CMD_PRINT_CMD:
    case S_CMD_PAGER_CMD:
      if (*argument == '\0')
	{
	  fprintf (csql_Output_fp, "\n\t%s\n\n",
		   (cmd_no == S_CMD_SHELL_CMD) ? csql_Shell_cmd :
		   (cmd_no == S_CMD_EDIT_CMD) ? csql_Editor_cmd :
		   (cmd_no == S_CMD_PRINT_CMD) ? csql_Print_cmd :
		   csql_Pager_cmd);
	}
      else
	{
	  strncpy ((cmd_no == S_CMD_SHELL_CMD) ? csql_Shell_cmd :
		   (cmd_no == S_CMD_EDIT_CMD) ? csql_Editor_cmd :
		   (cmd_no == S_CMD_PRINT_CMD) ? csql_Print_cmd :
		   csql_Pager_cmd, argument, PATH_MAX - 1);
	}
      break;

    case S_CMD_NOPAGER_CMD:
      csql_Pager_cmd[0] = '\0';
      break;

      /* Help stuffs */
    case S_CMD_HELP:
      csql_help_menu ();
      break;

    case S_CMD_SCHEMA:
      csql_help_schema ((argument[0] == '\0') ? NULL : argument);
      if (csql_arg->auto_commit
	  && prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT))
	{
	  if (db_commit_transaction () < 0)
	    {
	      csql_display_csql_err (0, 0);
	      csql_check_server_down ();
	    }
	  else
	    {
	      csql_display_msg (csql_get_message (CSQL_STAT_COMMITTED_TEXT));
	    }
	}
      break;

    case S_CMD_TRIGGER:
      csql_help_trigger ((argument[0] == '\0') ? NULL : argument);
      if (csql_arg->auto_commit
	  && prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT))
	{
	  if (db_commit_transaction () < 0)
	    {
	      csql_display_csql_err (0, 0);
	      csql_check_server_down ();
	    }
	  else
	    {
	      csql_display_msg (csql_get_message (CSQL_STAT_COMMITTED_TEXT));
	    }
	}
      break;

    case S_CMD_INFO:
      csql_help_info ((argument[0] == '\0') ? NULL : argument,
		      csql_arg->auto_commit
		      && prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT));
      break;

    case S_CMD_DATABASE:
      csql_print_database ();
      break;

    case S_CMD_SET_PARAM:
      csql_set_sys_param (argument);
      break;

    case S_CMD_GET_PARAM:
      csql_get_sys_param (argument);
      break;

    case S_CMD_PLAN_DUMP:
      csql_set_plan_dump ((argument[0] == '\0') ? NULL : argument);
      break;

    case S_CMD_ECHO:
      if (!strcasecmp (argument, "on"))
	{
	  csql_Is_echo_on = true;
	}
      else if (!strcasecmp (argument, "off"))
	{
	  csql_Is_echo_on = false;
	}
      else
	{
	  fprintf (csql_Output_fp, "ECHO IS %s\n",
		   (csql_Is_echo_on ? "ON" : "OFF"));
	}
      break;

    case S_CMD_DATE:
      {
	time_t tloc = time (NULL);
	struct tm tmloc;
	char str[80];
	utility_localtime (&tloc, &tmloc);
	strftime (str, 80, "%a %B %d %H:%M:%S %Z %Y", &tmloc);
	fprintf (csql_Output_fp, "\n\t%s\n", str);
      }
      break;

    case S_CMD_TIME:
      if (!strcasecmp (argument, "on"))
	{
	  csql_Is_time_on = true;
	}
      else if (!strcasecmp (argument, "off"))
	{
	  csql_Is_time_on = false;
	}
      else
	{
	  fprintf (csql_Output_fp, "TIME IS %s\n",
		   (csql_Is_time_on ? "ON" : "OFF"));
	}
      break;

    case S_CMD_LINE_OUTPUT:
      if (strcasecmp (argument, "on") == 0)
	{
	  if (csql_arg->column_output)
	    {
	      csql_Error_code = CSQL_ERR_INVALID_ARG_COMBINATION;
	      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
	      break;
	    }
	  csql_arg->line_output = true;
	}
      else if (strcasecmp (argument, "off") == 0)
	{
	  csql_arg->line_output = false;
	}
      else
	{
	  fprintf (csql_Output_fp, "LINE_OUTPUT IS %s\n",
		   (csql_arg->line_output ? "ON" : "OFF"));
	}
      break;

    case S_CMD_COLUMN_WIDTH:
      {
	char *column_name = NULL;
	int width = 0, result;

	if (*argument == '\0')
	  {
	    int i;

	    for (i = 0; i < csql_column_width_info_list_size; i++)
	      {
		if (csql_column_width_info_list[i].name == NULL)
		  {
		    break;
		  }

		if (csql_column_width_info_list[i].width <= 0)
		  {
		    continue;
		  }

		fprintf (csql_Output_fp,
			 "COLUMN-WIDTH %s : %d\n",
			 csql_column_width_info_list[i].name,
			 csql_column_width_info_list[i].width);
	      }
	  }
	else
	  {
	    char *val_str = NULL;

	    result =
	      get_column_name_argument (&column_name, &val_str, argument);
	    if (result == CSQL_FAILURE)
	      {
		fprintf (csql_Error_fp, "ERROR: Column name is too long.\n");
		break;
	      }

	    if (val_str == NULL)
	      {
		width = csql_get_column_width (column_name);
		fprintf (csql_Output_fp, "COLUMN-WIDTH %s : %d\n",
			 column_name, width);
	      }
	    else
	      {
		trim (val_str);
		result = parse_int (&width, val_str, 10);
		if (result != 0 || width < 0)
		  {
		    fprintf (csql_Error_fp, "ERROR: Invalid argument(%s).\n",
			     val_str);
		    break;
		  }

		if (csql_set_column_width_info (column_name, width) !=
		    CSQL_SUCCESS)
		  {
		    return DO_CMD_FAILURE;
		  }
	      }
	  }
      }
      break;

    case S_CMD_STRING_WIDTH:
      {
	int string_width = 0, result;

	if (*argument != '\0')
	  {
	    trim (argument);

	    result = parse_int (&string_width, argument, 10);

	    if (result != 0 || string_width < 0)
	      {
		fprintf (csql_Error_fp,
			 "ERROR: Invalid string-width(%s).\n", argument);
	      }

	    csql_arg->string_width = string_width;
	  }
	else
	  {
	    fprintf (csql_Output_fp,
		     "STRING-WIDTH : %d\n", csql_arg->string_width);
	  }
      }
      break;

    case S_CMD_HISTO:
      if (histo_is_supported ())
	{
	  if (!strcasecmp (argument, "on"))
	    {
	      if (histo_start (false) == NO_ERROR)
		{
		  csql_Is_histo_on = HISTO_ON;
		}
	      else
		{
		  if (er_errid () == ER_AU_DBA_ONLY)
		    {
		      fprintf (csql_Output_fp,
			       "Histogram is allowed only for DBA\n");
		    }
		  else
		    {
		      fprintf (csql_Output_fp, "Error on .hist command\n");
		    }
		}
	    }
	  else if (!strcasecmp (argument, "off"))
	    {
	      (void) histo_stop ();
	      csql_Is_histo_on = HISTO_OFF;
	    }
	  else
	    {
	      fprintf (csql_Output_fp, ".hist IS %s\n",
		       (csql_Is_histo_on == HISTO_OFF ? "OFF" : "ON"));
	    }
	}
      else
	{
	  fprintf (csql_Output_fp,
		   "Histogram is possible when the csql started with "
		   "`communication_histogram=yes'\n");
	}
      break;

    case S_CMD_CLR_HISTO:
      if (histo_is_supported ())
	{
	  if (csql_Is_histo_on == HISTO_ON)
	    {
	      histo_clear ();
	    }
	  else
	    {
	      fprintf (csql_Output_fp, ".hist IS currently OFF\n");
	    }
	}
      else
	{
	  fprintf (csql_Output_fp, "Histogram on execution statistics "
		   "is only allowed for the csql started "
		   "with `communication_histogram=yes'\n");
	}
      break;

    case S_CMD_DUMP_HISTO:
      if (histo_is_supported ())
	{
	  if (csql_Is_histo_on == HISTO_ON)
	    {
	      histo_print (csql_Output_fp);
	      fprintf (csql_Output_fp, "\n");
	    }
	  else
	    {
	      fprintf (csql_Output_fp, ".hist IS currently OFF\n");
	    }
	}
      else
	{
	  fprintf (csql_Output_fp, "Histogram on execution statistics "
		   "is only allowed for the csql started "
		   "with `communication_histogram=yes'\n");
	}
      break;

    case S_CMD_DUMP_CLR_HISTO:
      if (histo_is_supported ())
	{
	  if (csql_Is_histo_on == HISTO_ON)
	    {
	      histo_print (csql_Output_fp);
	      histo_clear ();
	      fprintf (csql_Output_fp, "\n");
	    }
	  else
	    {
	      fprintf (csql_Output_fp, ".hist IS currently OFF\n");
	    }
	}
      else
	{
	  fprintf (csql_Output_fp, "Histogram on execution statistics "
		   "is only allowed for the csql started "
		   "with `communication_histogram=yes'\n");
	}
      break;

    case S_CMD_HISTORY_READ:
#if !defined(WINDOWS)
      if (csql_Is_interactive)
	{
	  if (argument[0] != '\0')
	    {
	      int i = atoi (argument);
	      if (i > 0)
		{
		  HIST_ENTRY *hist;
		  hist = history_get (history_base + i - 1);
		  if (hist != NULL)
		    {
		      if (csql_edit_contents_append (hist->line, true) !=
			  CSQL_SUCCESS)
			{
			  return DO_CMD_FAILURE;
			}
		    }
		  else
		    {
		      fprintf (csql_Error_fp,
			       "ERROR: Invalid history number(%s).\n",
			       argument);
		    }
		}
	      else
		{
		  fprintf (csql_Error_fp, "ERROR: Invalid history number\n");
		}
	    }
	  else
	    {
	      fprintf (csql_Error_fp,
		       "ERROR: HISTORYRead {history_number}\n");
	    }
	}
#else
      if (csql_Is_interactive)
	{
	  fprintf (csql_Error_fp, "ERROR: Windows do not support HISTORYList"
		   " and HISTORYRead session commands in csql.\n");
	}
#endif /* !WINDOWS */
      break;

    case S_CMD_HISTORY_LIST:
#if !defined(WINDOWS)
      if (csql_Is_interactive)
	{
	  /* rewind history */
	  int i;

	  while (next_history ())
	    {
	      ;
	    }

	  for (i = 0, hist_entry = current_history (); hist_entry;
	       hist_entry = previous_history (), i++)
	    {
	      fprintf (csql_Output_fp, "----< %d >----\n", i + 1);
	      fprintf (csql_Output_fp, "%s\n\n", hist_entry->line);
	    }
	}
#else
      if (csql_Is_interactive)
	{
	  fprintf (csql_Error_fp, "ERROR: Windows do not support HISTORYList"
		   " and HISTORYRead session commands in csql.\n");
	}
#endif /* !WINDOWS */
      break;
    case S_CMD_TRACE:
      if (csql_arg->sa_mode == false)
	{
	  csql_set_trace ((argument[0] == '\0') ? NULL : argument);
	}
      else
	{
	  fprintf (csql_Error_fp, "Auto trace isn't allowed in SA mode.\n");
	}
      break;
    }

  return DO_CMD_SUCCESS;
}

/*
 * csql_read_file() - read a file into command editor
 *   return: none
 *   file_name(in): input file name
 */
static void
csql_read_file (const char *file_name)
{
  static char current_file[PATH_MAX] = "";
  char *p, *q;			/* pointer to string */
  FILE *fp = (FILE *) NULL;	/* file stream */

  p = csql_get_real_path (file_name);	/* get real path name */

  if (p == NULL || p[0] == '\0')
    {
      /*
       * No filename given; use the last one we were given.  If we've
       * never received one before we have a genuine error.
       */
      if (current_file[0] != '\0')
	{
	  p = current_file;
	}
      else
	{
	  csql_Error_code = CSQL_ERR_FILE_NAME_MISSED;
	  goto error;
	}
    }

  for (q = p; *q != '\0' && !iswspace ((wint_t) (*q)); q++)
    ;

  /* trim trailing blanks */
  for (; *q != '\0' && iswspace ((wint_t) (*q)); q++)
    {
      *q = '\0';
    }

  if (*q != '\0')
    {				/* contains more than one file name */
      csql_Error_code = CSQL_ERR_TOO_MANY_FILE_NAMES;
      goto error;
    }

  fp = fopen (p, "r");
  if (fp == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  /*
   * We've successfully read the file, so remember its name for
   * subsequent reads.
   */
  strncpy (current_file, p, sizeof (current_file));

  if (csql_edit_read_file (fp) == CSQL_FAILURE)
    {
      goto error;
    }

  fclose (fp);

  csql_display_msg (csql_get_message (CSQL_STAT_READ_DONE_TEXT));

  return;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_write_file() - write (or append) the current content of editor into
 *                   user specified file
 *   return: none
 *   file_name(in): output file name
 *   append_flag(in): true if append
 */
static void
csql_write_file (const char *file_name, int append_flag)
{
  static char current_file[PATH_MAX] = "";
  /* the name of the last file written */
  char *p, *q;			/* pointer to string */
  FILE *fp = (FILE *) NULL;	/* file stream */

  p = csql_get_real_path (file_name);	/* get real path name */

  if (p == NULL || p[0] == '\0')
    {
      /*
       * No filename given; use the last one we were given.  If we've
       * never received one before we have a genuine error.
       */
      if (current_file[0] != '\0')
	p = current_file;
      else
	{
	  csql_Error_code = CSQL_ERR_FILE_NAME_MISSED;
	  goto error;
	}
    }

  for (q = p; *q != '\0' && !iswspace ((wint_t) (*q)); q++)
    ;

  /* trim trailing blanks */
  for (; *q != '\0' && iswspace ((wint_t) (*q)); q++)
    {
      *q = '\0';
    }

  if (*q != '\0')
    {				/* contains more than one file name */
      csql_Error_code = CSQL_ERR_TOO_MANY_FILE_NAMES;
      goto error;
    }

  fp = fopen (p, (append_flag) ? "a" : "w");
  if (fp == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  /*
   * We've successfully opened the file, so remember its name for
   * subsequent writes.
   */
  strncpy (current_file, p, sizeof (current_file));

  if (csql_edit_write_file (fp) == CSQL_FAILURE)
    {
      goto error;
    }

  fclose (fp);

  csql_display_msg (csql_get_message (CSQL_STAT_EDITOR_SAVED_TEXT));

  return;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_print_buffer()
 *   return: none
 *
 * Note:
 *   copy command editor buffer into temporary file and
 *   invoke the user preferred print command to print
 */
static void
csql_print_buffer (void)
{
  char *cmd = NULL;
  char *fname = (char *) NULL;	/* pointer to temp file name */
  FILE *fp = (FILE *) NULL;	/* pointer to stream */

  /* create a temp file and open it */

  fname = tmpnam ((char *) NULL);
  if (fname == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  fp = fopen (fname, "w");
  if (fp == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }

  /* write the content of editor to the temp file */
  if (csql_edit_write_file (fp) == CSQL_FAILURE)
    {
      goto error;
    }

  fclose (fp);
  fp = (FILE *) NULL;

  /* invoke the print command */
  cmd = csql_get_tmp_buf (1 + strlen (csql_Print_cmd) + 3 + strlen (fname));
  if (cmd == NULL)
    {
      goto error;
    }
  /*
   * Parenthesize the print command and supply its input through stdin,
   * just in case it's a pipe or something odd.
   */
  sprintf (cmd, "(%s) <%s", csql_Print_cmd, fname);
  csql_invoke_system (cmd);

  unlink (fname);

  csql_display_msg (csql_get_message (CSQL_STAT_EDITOR_PRINTED_TEXT));

  return;

error:
  if (fp != NULL)
    {
      fclose (fp);
    }
  if (fname != NULL)
    {
      unlink (fname);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_change_working_directory()
 *   return: none
 *   dirname(in)
 *
 * Note:
 *   cd to the named directory; if dirname is NULL, cd to
 *   the home directory.
 */
static void
csql_change_working_directory (const char *dirname)
{
  const char *msg;
  char buf[100 + PATH_MAX];

  msg = csql_get_message (CSQL_STAT_CD_TEXT);

  dirname = csql_get_real_path (dirname);

  if (dirname == NULL)
    {
#if defined (WINDOWS)
      static char home_path[PATH_MAX];

      sprintf (home_path, "%s%s", getenv ("HOMEDRIVE"), getenv ("HOMEPATH"));
      dirname = home_path;
#else
      dirname = getenv ("HOME");
#endif
    }

  if (dirname == NULL || chdir (dirname) == -1)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
  else
    {
      snprintf (buf, sizeof (buf) - 1, "\n%s %s.\n\n", msg, dirname);
      csql_fputs_console_conv (buf, csql_Tty_fp);
    }
}

/*
 * display_error()
 *   return: none
 *   session(in)
 *   stmt_start_line_no(in)
 */
static void
display_error (DB_SESSION * session, int stmt_start_line_no)
{
  if (csql_Error_code == CSQL_ERR_SQL_ERROR)
    {
      csql_display_session_err (session, stmt_start_line_no);
      csql_check_server_down ();
    }
  else
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);

      /* let users read this message before the next overwrites */
      sleep (3);
    }
}

/*
 * csql_execute_statements() - execute statements
 *   return: >0 if some statement failed, zero otherwise
 *   csql_arg(in)
 *   type(in)
 *   stream(in)
 *
 * Note:
 *   If `type' is STRING_INPUT, it regards `stream' points to command string.
 *   If `type' is FILE_INPUT, it regards `stream' points FILE stream of input
 *   If `type' is EDITOR_INPUT, it attempts to get input string from command
 *   buffer.
 */
static int
csql_execute_statements (const CSQL_ARGUMENT * csql_arg, int type,
			 const void *stream, int line_no)
{
  char *stmts = NULL;		/* statements string */
  int num_stmts = 0;		/* # of stmts executed */
  int stmt_start_line_no = 0;	/* starting line no of each stmt */
  DB_SESSION *session = NULL;	/* query compilation session id */
  DB_QUERY_TYPE *attr_spec = NULL;	/* result attribute spec. */
  int total;			/* number of statements to execute */
  bool do_abort_transaction = false;	/* flag for transaction abort */
#if defined(CS_MODE)
  bool prm_query_mode_sync = prm_get_query_mode_sync ();
#endif

  csql_Num_failures = 0;
  er_clear ();
  db_set_interrupt (0);

  if (type == FILE_INPUT)
    {				/* FILE * input */
      if (!(session = db_open_file ((FILE *) stream)))
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}
    }
  else if (type == STRING_INPUT)
    {				/* string pointer input */
      if (!(session = db_open_buffer ((const char *) stream)))
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}
      if (csql_Is_echo_on)
	{
	  fprintf (csql_Output_fp, "%s\n", (char *) stream);
	}
    }
  else
    {				/* command buffer input */
      stmts = csql_edit_contents_get ();
      if (!(session = db_open_buffer (stmts)))
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}
      if (csql_Is_echo_on)
	{
	  fprintf (csql_Output_fp, "%s\n", stmts);
	}
    }

  /*
   * Make sure that there weren't any syntax errors; if there were, the
   * entire concept of "compile next statement" doesn't make sense, and
   * you run the risk of getting stuck in an infinite loop in the
   * following section (especially if the '-e' switch is on).
   */
  if (db_get_errors (session))
    {
      csql_Error_code = CSQL_ERR_SQL_ERROR;
#if !defined(WINDOWS)
      if ((stmts != NULL) && (csql_Is_interactive))
	{
	  add_history (stmts);
	}
#endif /* !WINDOWS */
      goto error;
    }
  else
    {
      total = db_statement_count (session);
#if !defined(WINDOWS)
      if ((total >= 1) && (stmts != NULL) && (csql_Is_interactive))
	{
	  add_history (stmts);
	}
#endif /* !WINDOWS */

      /* It is assumed we must always enter the for loop below */
      total = MAX (total, 1);
    }

  /* execute the statements one-by-one */

  for (num_stmts = 0; num_stmts < total; num_stmts++)
    {
      TSC_TICKS start_tick, end_tick;
      TSCTIMEVAL elapsed_time;

      int stmt_id;
      CUBRID_STMT_TYPE stmt_type;	/* statement type */
      DB_QUERY_RESULT *result = NULL;	/* result pointer */
      int db_error;
      char stmt_msg[LINE_BUFFER_SIZE];

      /* Start the execution of stms */
      stmt_msg[0] = '\0';

      if (csql_Is_time_on)
	{
	  tsc_getticks (&start_tick);
	}
      stmt_id = db_compile_statement (session);

      if (stmt_id < 0)
	{
	  /*
	   * Transaction should be aborted if an error occurs during
	   * compilation on auto commit mode.
	   */
	  if (csql_arg->auto_commit
	      && prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT))
	    {
	      do_abort_transaction = true;
	    }

	  /* compilation error */
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  /* Do not continue if there are no statments in the buffer */
	  if (csql_arg->continue_on_error
	      && (db_error_code () != ER_IT_EMPTY_STATEMENT))
	    {
	      display_error (session, 0);
	      /* do_abort_transaction() should be called after display_error()
	       * because in some cases it deallocates the parser containing
	       * the error message
	       */
	      if (do_abort_transaction)
		{
		  db_abort_transaction ();
		  do_abort_transaction = false;
		}
	      csql_Num_failures += 1;
	      continue;
	    }
	  else
	    {
	      goto error;
	    }
	}

      if (stmt_id == 0)		/* done */
	{
	  break;
	}

      if (line_no == -1)
	{
	  stmt_start_line_no = db_get_start_line (session, stmt_id);
	}
      else
	{
	  stmt_start_line_no = line_no;
	}
      attr_spec = db_get_query_type_list (session, stmt_id);
      stmt_type = (CUBRID_STMT_TYPE) db_get_statement_type (session, stmt_id);


#if defined(CS_MODE)
      if (prm_query_mode_sync)
	{
	  db_set_session_mode_sync (session);
	}
      else
	{
	  db_set_session_mode_async (session);
	}
#else /* !CS_MODE */
      db_set_session_mode_sync (session);
#endif /* CS_MODE */

      db_error = db_execute_statement (session, stmt_id, &result);

      if (db_error < 0)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  if (csql_arg->auto_commit
	      && prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT)
	      && stmt_type != CUBRID_STMT_ROLLBACK_WORK)
	    {
	      do_abort_transaction = true;
	    }
	  if (csql_arg->continue_on_error)
	    {
	      display_error (session, stmt_start_line_no);
	      if (do_abort_transaction)
		{
		  db_abort_transaction ();
		  do_abort_transaction = false;
		}
	      csql_Num_failures += 1;

	      free_attr_spec (&attr_spec);

	      continue;
	    }
	  goto error;
	}

      snprintf (stmt_msg, LINE_BUFFER_SIZE, "Execute OK.");

      csql_Row_count = 0;
      switch (stmt_type)
	{
	case CUBRID_STMT_SELECT:
	  {
	    const char *msg_p;

	    csql_results (csql_arg, result, attr_spec, stmt_start_line_no,
			  stmt_type);

#if defined(CS_MODE)
	    if (prm_query_mode_sync)
	      {
		csql_Row_count = db_error;
	      }
#else /* !CS_MODE */
	    csql_Row_count = db_error;
#endif /* CS_MODE */

	    msg_p = ((csql_Row_count > 1)
		     ? csql_get_message (CSQL_ROWS)
		     : csql_get_message (CSQL_ROW));
	    snprintf (stmt_msg, LINE_BUFFER_SIZE, msg_p, csql_Row_count,
		      "selected");
	    break;
	  }

	case CUBRID_STMT_CALL:
	case CUBRID_STMT_EVALUATE:
	  if (result != NULL)
	    {
	      csql_results (csql_arg, result, db_get_query_type_ptr (result),
			    stmt_start_line_no, stmt_type);
	    }
	  break;

	case CUBRID_STMT_GET_ISO_LVL:
	case CUBRID_STMT_GET_TIMEOUT:
	case CUBRID_STMT_GET_OPT_LVL:
	case CUBRID_STMT_GET_TRIGGER:
	case CUBRID_STMT_GET_STATS:
	  if (result != NULL)
	    {
	      csql_results (csql_arg, result, db_get_query_type_ptr (result),
			    stmt_start_line_no, stmt_type);
	    }
	  break;

	case CUBRID_STMT_UPDATE:
	case CUBRID_STMT_DELETE:
	case CUBRID_STMT_INSERT:
	case CUBRID_STMT_MERGE:
	  {
	    const char *msg_p;

	    msg_p = ((db_error > 1)
		     ? csql_get_message (CSQL_ROWS)
		     : csql_get_message (CSQL_ROW));
	    snprintf (stmt_msg, LINE_BUFFER_SIZE, msg_p, db_error,
		      "affected");
	    break;
	  }

	case CUBRID_STMT_KILL:
	  {
	    const char *msg_p;

	    msg_p = ((db_error > 1)
		     ? csql_get_message (CSQL_TRANSACTIONS)
		     : csql_get_message (CSQL_TRANSACTION));
	    snprintf (stmt_msg, LINE_BUFFER_SIZE, msg_p, db_error, "killed");
	    break;
	  }

	default:
	  break;
	}

      free_attr_spec (&attr_spec);

      if (result != NULL)
	{
	  db_query_end (result);
	  result = NULL;
	}
      else
	{
	  /*
	   * Even though there are no results, a query may have been
	   * run implicitly by the statement.  If so, we need to end the
	   * query on the server.
	   */
	  db_free_query (session);
	}

      if (csql_Is_time_on)
	{
	  char time[100];

	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&elapsed_time, end_tick, start_tick);

	  sprintf (time, " (%ld.%06ld sec) ",
		   elapsed_time.tv_sec, elapsed_time.tv_usec);
	  strncat (stmt_msg, time, sizeof (time));
	}

      if (csql_arg->auto_commit
	  && prm_get_bool_value (PRM_ID_CSQL_AUTO_COMMIT)
	  && stmt_type != CUBRID_STMT_COMMIT_WORK
	  && stmt_type != CUBRID_STMT_ROLLBACK_WORK)
	{
	  db_error = db_commit_transaction ();
	  if (db_error < 0)
	    {
	      csql_Error_code = CSQL_ERR_SQL_ERROR;
	      do_abort_transaction = true;

	      if (csql_arg->continue_on_error)
		{
		  display_error (session, stmt_start_line_no);
		  if (do_abort_transaction)
		    {
		      db_abort_transaction ();
		      do_abort_transaction = false;
		    }
		  csql_Num_failures += 1;
		  continue;
		}
	      goto error;
	    }
	  else
	    {
	      strncat (stmt_msg, csql_get_message (CSQL_STAT_COMMITTED_TEXT),
		       LINE_BUFFER_SIZE - 1);
	    }
	}
      fprintf (csql_Output_fp, "%s\n", stmt_msg);
      db_drop_statement (session, stmt_id);
    }

  snprintf (csql_Scratch_text, SCRATCH_TEXT_LEN,
	    csql_get_message (CSQL_EXECUTE_END_MSG_FORMAT),
	    num_stmts - csql_Num_failures);
  csql_display_msg (csql_Scratch_text);

  db_close_session (session);

  if (csql_Query_trace == true)
    {
      csql_display_trace ();
    }

  return csql_Num_failures;

error:

  display_error (session, stmt_start_line_no);
  if (do_abort_transaction)
    {
      db_abort_transaction ();
      do_abort_transaction = false;
    }

  /* Finish... */
  snprintf (csql_Scratch_text, SCRATCH_TEXT_LEN,
	    csql_get_message (CSQL_EXECUTE_END_MSG_FORMAT),
	    num_stmts - csql_Num_failures);
  csql_display_msg (csql_Scratch_text);

  if (session)
    {
      db_close_session (session);
    }

  free_attr_spec (&attr_spec);

  return 1;
}

/*
 * free_attr_spec()
 *   return: none
 *   attr_spec(in/out)
 *
 * Note: Free memory alloced for attr_spec and set pointer to NULL
 */
static void
free_attr_spec (DB_QUERY_TYPE ** attr_spec)
{
  if (*attr_spec != NULL)
    {
      db_query_format_free (*attr_spec);
      *attr_spec = NULL;
    }
}

/*
 * csql_print_database()
 */
static void
csql_print_database (void)
{
  struct sockaddr_in sin;
  const char *db_name, *host_name;
  char *pstr;
  char converted_host_name[MAXHOSTNAMELEN + 1];
  char ha_state[16];
  int res;

  db_name = db_get_database_name ();
  host_name = db_get_host_connected ();

  if (db_name == NULL || host_name == NULL)
    {
      fprintf (csql_Error_fp, "\n\tNOT CONNECTED\n\n");
    }
  else
    {
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = inet_addr (host_name);

      res = getnameinfo ((struct sockaddr *) &sin, sizeof (sin),
			 converted_host_name, sizeof (converted_host_name),
			 NULL, 0, NI_NAMEREQD);
      /*
       * if it fails to resolves hostname,
       * it will use db_get_host_connected()'s result.
       */
      if (res != 0)
	{
	  strncpy (converted_host_name, host_name, MAXHOSTNAMELEN);
	  converted_host_name[MAXHOSTNAMELEN] = '\0';
	}

      if (strcasecmp (converted_host_name, "localhost") == 0
	  || strcasecmp (converted_host_name, "localhost.localdomain") == 0)
	{
	  if (GETHOSTNAME (converted_host_name, MAXHOSTNAMELEN) != 0)
	    {
	      strncpy (converted_host_name, host_name, MAXHOSTNAMELEN);
	    }
	}
      converted_host_name[MAXHOSTNAMELEN] = '\0';

      /*
       * if there is hostname or ip address in db_name,
       * it will only use db_name except for hostname or ip address.
       */
      pstr = strchr (db_name, '@');
      if (pstr != NULL)
	{
	  *pstr = '\0';
	}

      if (prm_get_integer_value (PRM_ID_HA_MODE) == HA_MODE_OFF)
	{
	  fprintf (csql_Output_fp, "\n\t%s@%s\n\n", db_name,
		   converted_host_name);
	}
      else
	{
	  db_get_ha_server_state (ha_state, 16);
	  fprintf (csql_Output_fp, "\n\t%s@%s [%s]\n\n", db_name,
		   converted_host_name, ha_state);
	}

      db_ws_free ((char *) db_name);
    }
}

/*
 * csql_set_sys_param()
 *   return: none
 *   arg_str(in)
 *
 * Note: Parse the arg string to find out what system parameter to
 *       clobber, then clobber it.  Originally introduced to allow us
 *       to fiddle with optimizer parameters.
 */
static void
csql_set_sys_param (const char *arg_str)
{
  char plantype[128];
  char val[128];
  char ans[4096];
  int level;
  int len = sizeof (ans);

  if (arg_str == NULL)
    return;

  if (strncmp (arg_str, "cost", 4) == 0
      && sscanf (arg_str, "cost %127s %127s", plantype, val) == 2)
    {
      if (qo_plan_set_cost_fn (plantype, val[0]))
	{
	  snprintf (ans, 128, "cost %s: %s", plantype, val);
	}
      else
	{
	  snprintf (ans, 128, "error: unknown cost parameter %s", plantype);
	}
    }
  else if (strncmp (arg_str, "level", 5) == 0
	   && sscanf (arg_str, "level %d", &level) == 1)
    {
      qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);
      snprintf (ans, 128, "level %d", level);
    }
  else
    {
      strncpy (ans, arg_str, len - 1);
      if (db_set_system_parameters (ans) != NO_ERROR)
	{
	  snprintf (ans, len, "error: set %s", arg_str);
	}
    }

  csql_append_more_line (0, ans);
  csql_display_more_lines ("Set Param Input");
  csql_free_more_lines ();
}

/*
 * csql_get_sys_param()
 *   return:
 *   arg_str(in)
 */
static void
csql_get_sys_param (const char *arg_str)
{
  char plantype[128];
  int cost;
  char ans[4096];
  int level;
  int len = sizeof (ans);

  if (arg_str == NULL)
    return;

  if (strncmp (arg_str, "cost", 4) == 0
      && sscanf (arg_str, "cost %127s", plantype) == 1)
    {
      cost = qo_plan_get_cost_fn (plantype);
      if (cost == 'u')
	{
	  snprintf (ans, len, "error: unknown cost parameter %s", arg_str);
	}
      else
	{
	  snprintf (ans, len, "cost %s: %c", arg_str, (char) cost);
	}
    }
  else if (strncmp (arg_str, "level", 5) == 0
	   && sscanf (arg_str, "level") == 0)
    {
      qo_get_optimization_param (&level, QO_PARAM_LEVEL);
      snprintf (ans, len, "level %d", level);
    }
  else
    {
      strncpy (ans, arg_str, len - 1);
      if (db_get_system_parameters (ans, len - 1) != NO_ERROR)
	{
	  snprintf (ans, len - 1, "error: get %s", arg_str);
	}
    }

  csql_append_more_line (0, ans);
  csql_display_more_lines ("Get Param Input");
  csql_free_more_lines ();
}

/*
 * csql_set_plan_dump()
 *   return:
 *   arg_str(in)
 */
static void
csql_set_plan_dump (const char *arg_str)
{
  int level;
  char line[128];

  qo_get_optimization_param (&level, QO_PARAM_LEVEL);

  if (arg_str != NULL)
    {
      if (!strncmp (arg_str, "simple", 6))
	{
	  level &= ~0x200;
	  level |= 0x100;
	  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);
	}
      else if (!strncmp (arg_str, "detail", 6))
	{
	  level &= ~0x100;
	  level |= 0x200;
	  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);
	}
      else if (!strncmp (arg_str, "off", 3))
	{
	  level &= ~(0x100 | 0x200);
	  qo_set_optimization_param (NULL, QO_PARAM_LEVEL, level);
	}
    }

  if (PLAN_DUMP_ENABLED (level))
    {
      if (SIMPLE_DUMP (level))
	{
	  snprintf (line, 128, "plan simple (opt level %d)", level);
	}
      if (DETAILED_DUMP (level))
	{
	  snprintf (line, 128, "plan detail (opt level %d)", level);
	}
    }
  else
    {
      snprintf (line, 128, "plan off (opt level %d)", level);
    }

  csql_append_more_line (0, line);
  csql_display_more_lines ("Plan Dump");
  csql_free_more_lines ();
}

/*
 * signal_intr() - Interrupt handler for csql
 *   return: none
 *   sig_no(in)
 */
#if defined(WINDOWS)
static BOOL WINAPI
#else /* !WINDOWS */
static void
#endif				/* WINDOWS */
signal_intr (int sig_no)
{
  if (csql_Is_interactive)
    {
      db_set_interrupt (1);
    }
  csql_Is_sigint_caught = true;

#if defined(WINDOWS)
  if (sig_no == CTRL_C_EVENT)
    {
      return TRUE;
    }

  return FALSE;
#endif /* WINDOWS */
}

/*
 * signal_stop()
 *   return: none
 *   sig_no(in)
 *
 * Note: Interrupt handler for ^Z. This is needed since the terminal
 *       must be changed from raw to cooked. After we return, it must
 *       be set back.
 */
static void
signal_stop (int sig_no)
{
  static int cont = 0;

#if defined(WINDOWS)
  /* there is no SIGSTP on NT */
  cont = 1;
#else /* !WINDOWS */
  if (sig_no == SIGTSTP)
    {
      cont = 0;
      (void) os_set_signal_handler (SIGTSTP, SIG_DFL);

      /* send the signal to ourselves */
      os_send_signal (SIGTSTP);

      /* Wait for SIGCONT */
      while (cont == 0)
	{
	  pause ();
	}
      (void) os_set_signal_handler (SIGTSTP, signal_stop);
    }
  else
    {
      /* Continue */
      cont = 1;
    }
#endif /* !WINDOWS */
}

/*
 * csql_exit_session() - handling the default action of the last outstanding
 *                     transaction (i.e., commit or abort)
 *   return:  none
 *   error(in)
 *
 * Note: this function never return.
 */
static void
csql_exit_session (int error)
{
  char line_buf[LINE_BUFFER_SIZE];
  bool commit_on_shutdown = false;
  bool prm_commit_on_shutdown = prm_get_commit_on_shutdown ();

  free_csql_column_width_info_list ();

  if (!db_commit_is_needed ())
    {
      /* when select statements exist only in session,
         marks end of transaction to flush audit records
         for those statements */
      db_abort_transaction ();
    }

  if (csql_Is_interactive && !prm_commit_on_shutdown
      && db_commit_is_needed () && !feof (csql_Input_fp))
    {
      FILE *tf;

      tf = csql_Error_fp;

      /* interactive, default action is abort but there was update */
      fprintf (tf, csql_get_message (CSQL_TRANS_TERMINATE_PROMPT_TEXT));
      fflush (tf);
      for (; fgets (line_buf, LINE_BUFFER_SIZE, csql_Input_fp) != NULL;)
	{
	  if (line_buf[0] == 'y' || line_buf[0] == 'Y')
	    {
	      commit_on_shutdown = true;
	      break;
	    }
	  if (line_buf[0] == 'n' || line_buf[0] == 'N')
	    {
	      commit_on_shutdown = false;
	      break;
	    }

	  fprintf (tf,
		   csql_get_message (CSQL_TRANS_TERMINATE_PROMPT_RETRY_TEXT));
	  fflush (tf);
	}

      if (commit_on_shutdown && db_commit_transaction () < 0)
	{
	  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
	  error = 1;
	}
    }

  if (histo_is_supported ())
    {
      if (csql_Is_histo_on != HISTO_OFF)
	{
	  csql_Is_histo_on = HISTO_OFF;
	  histo_stop ();
	}
    }
  db_end_session ();

  if (db_shutdown () < 0)
    {
      csql_Database_connected = false;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      csql_exit (EXIT_FAILURE);
    }
  else
    {
      csql_Database_connected = false;
      csql_exit (error ? EXIT_FAILURE : EXIT_SUCCESS);
    }
}

/*
 * csql_exit_init()
 *   return: none
 *
 * Note:
 *    Initialize various state variables we keep to let us know what
 *    cleanup operations need to be performed when the csql() function
 *    exits.  This should properly initialize everything that is tested
 *    by the csql_exit_cleanup function.
 */
static void
csql_exit_init (void)
{
  csql_Exit_status = EXIT_SUCCESS;
  csql_Database_connected = false;

  csql_Input_fp = stdin;
  csql_Output_fp = stdout;
  csql_Error_fp = stderr;
}

/*
 * csql_exit_cleanup()
 *   return: none
 *
 * Note:
 *    Called by csql() when the exit longjmp has been taken.
 *    Examine the various state variables we keep and perform any
 *    termination cleanup operations that need to be performed.
 *    For the Windows implementation, it is especially important that the
 *    csql() function return cleanly.
 */
static void
csql_exit_cleanup ()
{
  FILE *oldout;

  if (csql_Input_fp != NULL && csql_Input_fp != stdin)
    {
      (void) fclose (csql_Input_fp);
      csql_Input_fp = NULL;
    }

  oldout = csql_Output_fp;
  if (csql_Output_fp != NULL && csql_Output_fp != stdout)
    {
      (void) fclose (csql_Output_fp);
      csql_Output_fp = NULL;
    }

  if (csql_Error_fp != NULL && csql_Error_fp != oldout
      && csql_Error_fp != stdout && csql_Error_fp != stderr)
    {
      (void) fclose (csql_Error_fp);
      csql_Error_fp = NULL;
    }

  if (csql_Database_connected)
    {
      if (histo_is_supported ())
	{
	  if (csql_Is_histo_on != HISTO_OFF)
	    {
	      csql_Is_histo_on = HISTO_OFF;
	      histo_stop ();
	    }
	}

      csql_Database_connected = false;
      db_end_session ();
      db_shutdown ();
    }

  /* Note that this closes a global resource, the "kernel" message catalog.
   * This is ok for the Unix implementation as the entire process is about
   * to exit.  For the Windows implementation, it happens to be ok since
   * the test driver application that calls csql() won't use this catalog.
   * If this ever changes however, we'll probably have to maintain some sort
   * of internal reference counter on this catalog so that it won't be freed
   * until all the nested users close it.
   */
  lang_final ();
}

/*
 * csql_exit()
 *   return:  none
 *   exit_status(in)
 * Note:
 *    This should be called rather than exit() any place that the code wants
 *    to terminate the csql interpreter program.  Rather than exit(), it
 *    will longjmp back to the csql() function which will clean up and
 *    return the status code to the calling function.  Usually the calling
 *    function is main() but under Windows, the caller may be a more complex
 *    application.
 */
void
csql_exit (int exit_status)
{
  csql_Exit_status = exit_status;
  longjmp (csql_Exit_env, 1);
}

/*
 * csql() - "main" interface function for the csql interpreter
 *   return: EXIT_SUCCESS, EXIT_FAILURE
 *   csql_arg(in)
 */
int
csql (const char *argv0, CSQL_ARGUMENT * csql_arg)
{
  char *env;
  int client_type;
  int avail_size;

  /* Establish a globaly accessible longjmp environment so we can terminate
   * on severe errors without calling exit(). */
  csql_exit_init ();

  if (setjmp (csql_Exit_env))
    {
      /* perform any dangling cleanup operations */
      csql_exit_cleanup ();
      return csql_Exit_status;
    }

  /* initialize message catalog for argument parsing and usage() */
  if (utility_initialize () != NO_ERROR)
    {
      csql_exit (EXIT_FAILURE);
    }

  /* set up prompt and message fields. */
  if (csql_arg->sysadm)
    {
      strncpy (csql_Prompt, csql_get_message (CSQL_SYSADM_PROMPT),
	       sizeof (csql_Prompt));
    }
  else
    {
      strncpy (csql_Prompt, csql_get_message (CSQL_PROMPT),
	       sizeof (csql_Prompt));
    }
  avail_size = sizeof (csql_Prompt) - strlen (csql_Prompt) - 1;
  if (avail_size > 0)
    {
      strncat (csql_Prompt, " ", avail_size);
    }
  strncpy (csql_Name, csql_get_message (CSQL_NAME), sizeof (csql_Name));

  /* as we must use db_open_file_name() to open the input file,
   * it is necessary to be opening csql_Input_fp at this point
   */
  if (csql_arg->in_file_name != NULL)
    {
#if defined(WINDOWS)
      csql_Input_fp = fopen (csql_arg->in_file_name, "rb");
#else /* !WINDOWS */
      csql_Input_fp = fopen (csql_arg->in_file_name, "r");
#endif /* WINDOWS */
      if (csql_Input_fp == NULL)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto error;
	}

#if defined(WINDOWS)
      {
	char tmpchar;		/* open breaks in DLL'S */
	/*
	 * Unless an operation is done on this stream before the DLL
	 * is entered the file descriptor will be invalid.  This is a bug in
	 * MSVC compiler and/or libc.
	 */
	tmpchar = fgetc (csql_Input_fp);
	ungetc (tmpchar, csql_Input_fp);
      }
#endif /* WINDOWS */
    }

  if ((csql_arg->in_file_name == NULL) && isatty (fileno (stdin)))
    {
      csql_Is_interactive = true;
    }

  /* initialize error log file */
  if (er_init ("./csql.err", ER_NEVER_EXIT) != NO_ERROR)
    {
      printf ("Failed to initialize error manager.\n");
      csql_Error_code = CSQL_ERR_OS_ERROR;
      goto error;
    }
  er_set_print_property (ER_PRINT_TO_CONSOLE);

  /*
   * login and restart database
   */
  if (csql_arg->sysadm)
    {
      client_type = DB_CLIENT_TYPE_ADMIN_CSQL;

      if (csql_arg->write_on_standby)
	{
	  client_type = DB_CLIENT_TYPE_ADMIN_CSQL_WOS;
	}
    }
  else if (csql_arg->read_only)
    {
      client_type = DB_CLIENT_TYPE_READ_ONLY_CSQL;
    }
  else
    {
      client_type = DB_CLIENT_TYPE_CSQL;
    }

  if (db_restart_ex (argv0, csql_arg->db_name,
		     csql_arg->user_name, csql_arg->passwd,
		     NULL, client_type) != NO_ERROR)
    {
      if (!csql_Is_interactive || csql_arg->passwd != NULL ||
	  db_error_code () != ER_AU_INVALID_PASSWORD)
	{
	  /* not INTERACTIVE mode, or password is given already, or
	   * the error code is not password related
	   */
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}

      /* get password interactively if interactive mode */
      csql_arg->passwd =
	getpass ((char *) csql_get_message (CSQL_PASSWD_PROMPT_TEXT));
      if (csql_arg->passwd[0] == '\0')
	csql_arg->passwd = (char *) NULL;	/* to fit into db_login protocol */

      /* try again */
      if (db_restart_ex (argv0, csql_arg->db_name,
			 csql_arg->user_name, csql_arg->passwd,
			 NULL, client_type) != NO_ERROR)
	{
	  csql_Error_code = CSQL_ERR_SQL_ERROR;
	  goto error;
	}
    }

  if (csql_arg->trigger_action_flag == false)
    {
      db_disable_trigger ();
    }

  if (csql_arg->sysadm && au_is_dba_group_member (Au_user))
    {
      au_disable ();
    }

  /* allow environmental setting of the "-s" command line flag
   * to enable automated testing */
  if (prm_get_bool_value (PRM_ID_CSQL_SINGLE_LINE_MODE))
    {
      csql_arg->single_line_execution = true;
    }

  /* record the connection so we know how to clean up on exit */
  csql_Database_connected = true;

#if defined(CSQL_NO_LOGGING)
  if (csql_arg->no_logging && locator_log_force_nologging () != NO_ERROR)
    {
      csql_Error_code = CSQL_ERR_SQL_ERROR;
      goto error;
    }
#endif /* CSQL_NO_LOGGING */

  csql_Editor_cmd[PATH_MAX - 1] = '\0';
  csql_Shell_cmd[PATH_MAX - 1] = '\0';
  csql_Print_cmd[PATH_MAX - 1] = '\0';
  csql_Pager_cmd[PATH_MAX - 1] = '\0';

  env = getenv ("EDITOR");
  if (env)
    {
      strncpy (csql_Editor_cmd, env, PATH_MAX - 1);
    }

  env = getenv ("SHELL");
  if (env)
    {
      strncpy (csql_Shell_cmd, env, PATH_MAX - 1);
    }

  if (csql_arg->nopager)
    {
      csql_Pager_cmd[0] = '\0';
    }

  lang_init_console_txt_conv ();

  if (csql_Is_interactive)
    {
      /* handling Ctrl-C */
#if defined(WINDOWS)
      SetConsoleCtrlHandler ((PHANDLER_ROUTINE) signal_intr, TRUE);
#else
      if (os_set_signal_handler (SIGINT, signal_intr) == SIG_ERR)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto error;
	}
#endif

#if !defined(WINDOWS)
      if (os_set_signal_handler (SIGQUIT, signal_intr) == SIG_ERR)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  goto error;
	}
#endif /* !WINDOWS */
    }

  er_set_print_property (ER_DO_NOT_PRINT);

  start_csql (csql_arg);

  csql_exit (EXIT_SUCCESS);	/* not reachable code, actually */

error:
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
  csql_exit (EXIT_FAILURE);
  return EXIT_FAILURE;		/* won't get here really */
}

/*
 * csql_get_message() - get a string of the csql-utility from the catalog
 *   return: message string
 *   message_index(in): an index of the message string
 */
const char *
csql_get_message (int message_index)
{
  return (msgcat_message (MSGCAT_CATALOG_CSQL,
			  MSGCAT_CSQL_SET_CSQL, message_index));
}

/*
 * csql_set_column_width_info() - insert column_name and column_width
 *                                in csql_column_width_info_list
 *   return: int
 *   column_name(in): column_name
 *   column_width(in): column_width
 */
int
csql_set_column_width_info (const char *column_name, int column_width)
{
  CSQL_COLUMN_WIDTH_INFO *temp_list;
  char *temp_name;
  int i, index;

  if (column_name == NULL || column_width < 0)
    {
      csql_Error_code = CSQL_ERR_INVALID_ARG_COMBINATION;

      return CSQL_FAILURE;
    }

  if (csql_column_width_info_list == NULL)
    {
      if (initialize_csql_column_width_info_list () != CSQL_SUCCESS)
	{
	  return CSQL_FAILURE;
	}
    }

  if (csql_column_width_info_list_index >= csql_column_width_info_list_size)
    {
      temp_list = realloc (csql_column_width_info_list,
			   sizeof (CSQL_COLUMN_WIDTH_INFO)
			   * (csql_column_width_info_list_size * 2));
      if (temp_list == NULL)
	{
	  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;

	  return CSQL_FAILURE;
	}

      csql_column_width_info_list_size *= 2;
      csql_column_width_info_list = temp_list;
      for (i = csql_column_width_info_list_index;
	   i < csql_column_width_info_list_size; i++)
	{
	  csql_column_width_info_list[i].name = NULL;
	  csql_column_width_info_list[i].width = 0;
	}
    }

  index = NOT_FOUND;
  for (i = 0; i < csql_column_width_info_list_index; i++)
    {
      if (strcasecmp (column_name, csql_column_width_info_list[i].name) == 0)
	{
	  index = i;
	  break;
	}
    }

  if (index == NOT_FOUND)
    {
      index = csql_column_width_info_list_index;
      csql_column_width_info_list_index++;
    }

  if (csql_column_width_info_list[index].name == NULL)
    {
      temp_name = strdup (column_name);
      if (temp_name == NULL)
	{
	  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;

	  return CSQL_FAILURE;
	}

      csql_column_width_info_list[index].name = temp_name;
      csql_column_width_info_list[index].width = column_width;
    }
  else
    {
      csql_column_width_info_list[index].width = column_width;
    }

  return CSQL_SUCCESS;
}

/*
 * csql_get_column_width() - get column_width related column_name
 *   return: column_width
 *   column_name(in): column_name
 */
int
csql_get_column_width (const char *column_name)
{
  char name_without_space[1024];
  char *result;
  int i;

  if (column_name == NULL)
    {
      return 0;
    }

  if (csql_column_width_info_list == NULL)
    {
      return 0;
    }

  strncpy (name_without_space, column_name, sizeof (name_without_space) - 1);
  name_without_space[sizeof (name_without_space) - 1] = '\0';
  result = trim (name_without_space);
  if (result == NULL)
    {
      return 0;
    }

  for (i = 0; i < csql_column_width_info_list_index; i++)
    {
      if (strcasecmp (result, csql_column_width_info_list[i].name) == 0)
	{
	  return csql_column_width_info_list[i].width;
	}
    }

  return 0;
}

/*
 * get_column_name_argument() - get column_name and value pointer from argument
 *   return: int
 *   column_name(out): column name
 *   val_str(out): value string in argument
 *   argument(in): argument
 */
static int
get_column_name_argument (char **column_name, char **val_str, char *argument)
{
  char *p;

  assert (column_name != NULL && val_str != NULL && argument != NULL);

  *column_name = NULL;
  *val_str = NULL;

  /* argument : "column_name=value" */
  *column_name = argument;

  p = strrchr (*column_name, '=');
  if (p != NULL)
    {
      *p = '\0';
      *val_str = (p + 1);
    }

  trim (*column_name);

  /* max column_name size is 254 */
  if (strlen (*column_name) > 254)
    {
      return CSQL_FAILURE;
    }

  return CSQL_SUCCESS;
}

/*
 * csql_set_trace() - set auto trace on or off
 *   return:
 *   arg_str(in):
 */
static void
csql_set_trace (const char *arg_str)
{
  char line[128];
  char format[128], *p;

  if (arg_str != NULL)
    {
      if (strncmp (arg_str, "on", 2) == 0)
	{
	  prm_set_bool_value (PRM_ID_QUERY_TRACE, true);
	  csql_Query_trace = true;

	  if (sscanf (arg_str, "on %127s", format) == 1)
	    {
	      p = trim (format);

	      if (strncmp (p, "text", 4) == 0)
		{
		  prm_set_integer_value (PRM_ID_QUERY_TRACE_FORMAT,
					 QUERY_TRACE_TEXT);
		}
	      else if (strncmp (p, "json", 4) == 0)
		{
		  prm_set_integer_value (PRM_ID_QUERY_TRACE_FORMAT,
					 QUERY_TRACE_JSON);
		}
	    }
	}
      else if (!strncmp (arg_str, "off", 3))
	{
	  prm_set_bool_value (PRM_ID_QUERY_TRACE, false);
	  csql_Query_trace = false;
	}
    }

  if (prm_get_bool_value (PRM_ID_QUERY_TRACE) == true)
    {
      if (prm_get_integer_value (PRM_ID_QUERY_TRACE_FORMAT)
	  == QUERY_TRACE_JSON)
	{
	  snprintf (line, 128, "trace on json");
	}
      else
	{
	  snprintf (line, 128, "trace on text");
	}
    }
  else
    {
      snprintf (line, 128, "trace off");
    }

  csql_append_more_line (0, line);
  csql_display_more_lines ("Query Trace");
  csql_free_more_lines ();
}

/*
 * csql_display_trace() -
 *   return:
 */
static void
csql_display_trace (void)
{
  const char *stmts = NULL;
  DB_SESSION *session = NULL;
  int stmt_id, dummy, db_error;
  DB_QUERY_RESULT *result = NULL;
  DB_VALUE trace;
  FILE *pf;
  int save_row_count;

  er_clear ();
  db_set_interrupt (0);
  db_make_null (&trace);
  save_row_count = db_get_row_count_cache ();

  stmts = "SHOW TRACE";

  session = db_open_buffer (stmts);
  if (session == NULL)
    {
      return;
    }

  stmt_id = db_compile_statement (session);

  if (stmt_id < 0)
    {
      goto end;
    }

  db_set_session_mode_sync (session);

  db_error = db_execute_statement (session, stmt_id, &result);

  if (db_error < 0)
    {
      goto end;
    }

  (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );

  if (db_query_first_tuple (result) < 0)
    {
      goto end;
    }

  if (db_query_get_tuple_value (result, 0, &trace) < 0)
    {
      goto end;
    }

  if (DB_VALUE_TYPE (&trace) == DB_TYPE_STRING)
    {
      pf = csql_popen (csql_Pager_cmd, csql_Output_fp);
      fprintf (pf, "\n=== Auto Trace ===\n");
      fprintf (pf, "%s\n", db_get_char (&trace, &dummy));
      csql_pclose (pf, csql_Output_fp);
    }

end:

  if (result != NULL)
    {
      db_query_end (result);
    }

  if (session != NULL)
    {
      db_close_session (session);
    }

  db_update_row_count_cache (save_row_count);

  return;
}
