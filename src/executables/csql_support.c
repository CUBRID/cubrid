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
 * csql_support.c : Utilities for csql module
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <setjmp.h>
#include <assert.h>
#if defined(WINDOWS)
#include <io.h>
#else /* !WINDOWS */
#include <pwd.h>
#endif /* !WINDOWS */
#include "porting.h"
#include "csql.h"
#include "filesys.hpp"
#include "filesys_temp.hpp"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "ddl_log.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/* fixed stop position of a tab */
#define TAB_STOP        8

/* number of lines at each expansion of more line pointer array */
#define	MORE_LINE_EXPANSION_UNIT	40

/* to build the current help message lines */
static char **iq_More_lines;	/* more message lines */
static int iq_Num_more_lines = 0;	/* number of more lines */
static jmp_buf iq_Jmp_buf;

#define DEFAULT_DB_ERROR_MSG_LEVEL      3	/* current max */

typedef enum csql_statement_state
{
  CSQL_STATE_GENERAL = 0,
  CSQL_STATE_C_COMMENT,
  CSQL_STATE_CPP_COMMENT,
  CSQL_STATE_SQL_COMMENT,
  CSQL_STATE_SINGLE_QUOTE,
  CSQL_STATE_MYSQL_QUOTE,
  CSQL_STATE_DOUBLE_QUOTE_IDENTIFIER,
  CSQL_STATE_BACKTICK_IDENTIFIER,
  CSQL_STATE_BRACKET_IDENTIFIER,
  CSQL_STATE_STATEMENT_END
} CSQL_STATEMENT_STATE;

typedef enum csql_statement_substate
{
  CSQL_SUBSTATE_INITIAL = 0,
  CSQL_SUBSTATE_SEEN_CREATE,
  CSQL_SUBSTATE_SEEN_OR,
  CSQL_SUBSTATE_SEEN_REPLACE,
  CSQL_SUBSTATE_EXPECTING_IS_OR_AS,
  CSQL_SUBSTATE_PL_LANG_SPEC,
  CSQL_SUBSTATE_SEEN_LANGUAGE,
  CSQL_SUBSTATE_PLCSQL_TEXT,
  CSQL_SUBSTATE_SEEN_END
} CSQL_STATEMENT_SUBSTATE;

/* editor buffer management */
typedef struct
{
  char *contents;
  int data_size;
  int alloc_size;
  CSQL_STATEMENT_STATE state;
  // following three fields are used to identify the beginning and the end of PL/CSQL texts
  CSQL_STATEMENT_SUBSTATE substate;
  int plcsql_begin_end_balance;
  int plcsql_nest_level;
} CSQL_EDIT_CONTENTS;

static CSQL_EDIT_CONTENTS csql_Edit_contents = { NULL, 0, 0, CSQL_STATE_GENERAL, CSQL_SUBSTATE_INITIAL, 0, 0 };


static bool is_identifier_letter (const char c);
static bool match_word_ci (const char *word, const char **bufp);

static void iq_pipe_handler (int sig_no);
static void iq_format_err (char *string, int buf_size, int line_no, int col_no);
static bool iq_input_device_is_a_tty (void);
static bool iq_output_device_is_a_tty (void);
#if !defined(WINDOWS)
static int csql_get_user_home (char *homebuf, int bufsize);
#endif /* !WINDOWS */

/*
 * iq_output_device_is_a_tty() - return if output stream is associated with
 *                               a "tty" device.
 *   return: true if the output device is a terminal
 */
static bool
iq_output_device_is_a_tty ()
{
  return (csql_Output_fp == stdout && isatty (fileno (stdout)));
}

/*
 * iq_input_device_is_a_tty() - return if input stream is associated with
 *                              a "tty" device.
 *   return: true if the input device is a terminal
 */
static bool
iq_input_device_is_a_tty ()
{
  return (csql_Input_fp == stdin && isatty (fileno (stdin)));
}

#if !defined(WINDOWS)
/*
 * csql_get_user_home() - get user home directory from /etc/passwd file
  *   return: 0 if success, -1 otherwise
 *   homedir(in/out) : user home directory
 *   homedir_size(in) : size of homedir buffer
 */
static int
csql_get_user_home (char *homedir, int homedir_size)
{
  struct passwd *ptr = NULL;
  uid_t userid = getuid ();

  setpwent ();

  while ((ptr = getpwent ()) != NULL)
    {
      if (userid == ptr->pw_uid)
	{
	  snprintf (homedir, homedir_size, "%s", ptr->pw_dir);
	  endpwent ();
	  return NO_ERROR;
	}
    }
  endpwent ();
  return ER_FAILED;
}
#endif /* !WINDOWS */

/*
 * csql_get_real_path() - get the real pathname (without wild/meta chars) using
 *                      the default shell
 *   return: the real path name
 *   pathname(in)
 *
 * Note:
 *   the real path name returned from this function is valid until next this
 *   function call. The return string will not have any leading/trailing
 *   characters other than the path name itself. If error occurred from O.S,
 *   give up the extension and just return the `pathname'.
 */
char *
csql_get_real_path (const char *pathname)
{
#if defined(WINDOWS)
  if (pathname == NULL)
    {
      return NULL;
    }

  while (isspace (pathname[0]))
    {
      pathname++;
    }

  if (pathname[0] == '\0')
    {
      return NULL;
    }

  return (char *) pathname;
#else /* ! WINDOWS */
  static char real_path[PATH_MAX];	/* real path name */
  char home[PATH_MAX];

  if (pathname == NULL)
    {
      return NULL;
    }

  while (isspace (pathname[0]))
    {
      pathname++;
    }

  if (pathname[0] == '\0')
    {
      return NULL;
    }

  /*
   * Do tilde-expansion here.
   */
  if (pathname[0] == '~')
    {
      if (csql_get_user_home (home, sizeof (home)) != NO_ERROR)
	{
	  return NULL;
	}

      snprintf (real_path, sizeof (real_path), "%s%s", home, &pathname[1]);
    }
  else
    {
      snprintf (real_path, sizeof (real_path), "%s", pathname);
    }

  return real_path;
#endif /* !WINDOWS */
}

/*
 * csql_invoke_system() - execute the given command with the argument using
 *                      system()
 *   return: none
 *   command(in)
 */
void
csql_invoke_system (const char *command)
{
  bool error_found = false;	/* TRUE if error found */

  if (system (command) == 127)
    {
      error_found = true;
      csql_Error_code = CSQL_ERR_OS_ERROR;
    }

  if (error_found)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
}


/*
 * csql_invoke_formatter()
 *   return: CSQL_SUCCESS/CSQL_FAILURE
 *
 * Note:
 *   copy command editor buffer into temporary file and
 *   invoke formatter. After the format is finished,
 *   read the file into editor buffer.
 */
int
csql_invoke_formatter ()
{
  /*create an unique file in tmp folder and open it for writing */
  auto[before_filename, before_fileptr] = filesys::open_temp_file ("bef_fmt_");
  if (before_fileptr == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      return CSQL_FAILURE;
    }
  filesys::auto_delete_file before_file_del (before_filename.c_str ());
  filesys::auto_close_file before_file (before_fileptr);

  if (csql_edit_write_file (before_file.get ()) == CSQL_FAILURE)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }
  fclose (before_file.release ());

  /*create an unique file in tmp folder */
  auto[after_filename, after_fileptr] = filesys::open_temp_file ("aft_fmt_");
  if (after_fileptr == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      return CSQL_FAILURE;
    }
  filesys::auto_delete_file after_file_del (after_filename.c_str ());
  filesys::auto_close_file after_file (after_fileptr);


  /* invoke the formatter command */
  char *cmd = csql_get_tmp_buf (strlen (csql_Formatter_cmd) + 1 + before_filename.size () + 3 + after_filename.size ());
  if (cmd == NULL)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }
  fclose (after_file.release ());
  sprintf (cmd, "%s %s > %s", csql_Formatter_cmd, before_filename.c_str (), after_filename.c_str ());

  if (system (cmd) != 0)
    {
      free_and_init (cmd);
      csql_Error_code = CSQL_ERR_FORMAT;
      return CSQL_FAILURE;
    }

  /* initialize editor buffer */
  csql_edit_contents_clear ();
  free_and_init (cmd);

  /*remove the file that saved before formatting command buffer */
  before_file.reset (fopen (before_filename.c_str (), "r"));
  if (!before_file)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      return CSQL_FAILURE;
    }

  /*remove the file that saved after formatting command buffer */
  after_file.reset (fopen (after_filename.c_str (), "r"));
  if (!after_file)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      return CSQL_FAILURE;
    }

  /* read the formatted file into editor */
  if (csql_edit_read_file (after_file.get ()) == CSQL_FAILURE)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }

  return CSQL_SUCCESS;
}


/*
 * csql_invoke_system_editor()
 *   return: CSQL_SUCCESS/CSQL_FAILURE
 *   argument: eidt session command argument input
 *
 * Note:
 *   copy command editor buffer into temporary file and
 *   invoke the user preferred system editor. After the
 *   edit is finished, read the file into editor buffer
 */
int
csql_invoke_system_editor (const char *argument)
{
  if (!iq_output_device_is_a_tty ())
    {
      csql_Error_code = CSQL_ERR_CANT_EDIT;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }

  if (csql_Formatter_cmd[0] != '\0' && argument && (!strcasecmp (argument, "format") || !strcasecmp (argument, "fmt")))
    {
      if (csql_invoke_formatter () != CSQL_SUCCESS)
	{
	  return CSQL_FAILURE;
	}
    }

  /* create an unique file in tmp folder and open it for writing */
  auto[filename, fileptr] = filesys::open_temp_file ("csql_");
  if (fileptr == NULL)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }
  filesys::auto_delete_file file_del (filename.c_str ());	//deletes file at scope end
  filesys::auto_close_file file (fileptr);	//closes file at scope end (before the above file deleter); forget about fp from now on

  /* write the content of editor to the temp file */
  if (csql_edit_write_file (file.get ()) == CSQL_FAILURE)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }

  /* invoke the system editor */
  char *cmd = csql_get_tmp_buf (strlen (csql_Editor_cmd) + 1 + filename.size ());
  if (cmd == NULL)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }
  fclose (file.release ());	//on Windows needs to be closed before being able to save from Notepad
  sprintf (cmd, "%s %s", csql_Editor_cmd, filename.c_str ());
  csql_invoke_system (cmd);

  /* initialize editor buffer */
  csql_edit_contents_clear ();
  free_and_init (cmd);


  file.reset (fopen (filename.c_str (), "r"));
  if (!file)
    {
      csql_Error_code = CSQL_ERR_OS_ERROR;
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }

  /* read the temp file into editor */
  if (csql_edit_read_file (file.get ()) == CSQL_FAILURE)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
      return CSQL_FAILURE;
    }

  return CSQL_SUCCESS;
}

/*
 * csql_fputs()
 *   return: none
 *   str(in): string to be displayed
 *   fp(in) : FILE stream
 *
 * Note:
 *   `fputs' version to cope with "\1" in the string. This function displays
 *   `<', `>' alternatively.
 */
void
csql_fputs (const char *str, FILE * fp)
{
  bool flag;			/* toggled at every "\1" */

  if (!fp)
    {
      return;
    }

  for (flag = false; *str != '\0'; str++)
    {
      if (*str == '\1')
	{
	  putc ((flag) ? '>' : '<', fp);
	  flag = !flag;
	}
      else
	{
	  putc (*str, fp);
	}
    }
}

/*
 * csql_fputs_console_conv() - format and display a string to the CSQL console
 *			       with console conversion applied, if available
 *   return: none
 *   str(in): string to be displayed
 *   fp(in) : FILE stream
 *
 * Note:
 *   `fputs' version to cope with "\1" in the string. This function displays
 *   `<', `>' alternatively.
 */
void
csql_fputs_console_conv (const char *str, FILE * fp)
{
  char *conv_buf = NULL;
  const char *conv_buf_ptr = NULL;
  int conv_buf_size = 0;

  if (!fp)
    {
      return;
    }

  if (csql_text_utf8_to_console != NULL
      && (*csql_text_utf8_to_console) (str, strlen (str), &conv_buf, &conv_buf_size) == NO_ERROR && conv_buf != NULL)
    {
      conv_buf_ptr = conv_buf;
    }
  else
    {
      conv_buf_ptr = str;
    }

  csql_fputs (conv_buf_ptr, fp);

  if (conv_buf != NULL)
    {
      free (conv_buf);
    }
}

/*
 * csql_popen() - Open & return a pipe file stream to a pager
 *   return: pipe file stream to a pager if stdout is a tty,
 *           otherwise return fd.
 *   cmd(in) : popen command
 *   fd(in): currently open file descriptor
 *
 * Note: Caller should call csql_pclose() after done.
 */
FILE *
csql_popen (const char *cmd, FILE * fd)
{

#if defined(WINDOWS)
  /* Nothing yet currently equivalent to the pagers on NT. Return iq_Output_fp so it can be simply sump stuff to the
   * console. */
  return fd;
#else /* ! WINDOWS */
  FILE *pf;			/* pipe stream to pager */

  pf = fd;
  if (cmd == NULL || cmd[0] == '\0')
    {
      return pf;
    }

  if (iq_output_device_is_a_tty () && iq_input_device_is_a_tty ())
    {
      pf = popen (cmd, "w");
      if (pf == NULL)
	{			/* pager failed, */
	  csql_Error_code = CSQL_ERR_CANT_EXEC_PAGER;
	  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
	  pf = fd;
	}
    }
  else
    {
      pf = fd;
    }

  return (pf);
#endif /* ! WINDOWS */
}

/*
 * csql_pclose(): close pipe file stream
 *   return: none
 *   pf(in): pipe stream pointer
 *   fd(in): This is the file descriptor for the output stream
 *           which was open prior to calling csql_popen().
 *
 * Note:
 *   We determine if it's a pipe by comparing the pipe stream pointer (pf)
 *   with the prior file descriptor (fd).  If they are different, then a pipe
 *   was opened and will be closed.
 */
void
csql_pclose (FILE * pf, FILE * fd)
{
#if !defined(WINDOWS)
  if (pf != fd)
    {
      pclose (pf);
    }
#endif /* ! WINDOWS */
}

/*
 * iq_format_err() - format an error string with line and/or column number
 *   return: none
 *   string(out): output string buffer
 *   line_no(in): error line number
 *   col_no(in) : error column number
 */
static void
iq_format_err (char *string, int buf_size, int line_no, int col_no)
{
  if (line_no > 0)
    {
      if (col_no > 0)
	snprintf (string, buf_size,
		  msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_EXACT_POSITION_ERR_FORMAT), line_no,
		  col_no);
      else
	snprintf (string, buf_size,
		  msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_START_POSITION_ERR_FORMAT), line_no);
      strcat (string, "\n");
    }
}

/*
 * csql_display_csql_err() - display error message
 *   return:  none
 *   line_no(in): error line number
 *   col_no(in) : error column number
 *
 * Note:
 *   if `line_no' is positive, this error is regarded as associated with
 *   the given line number. if `col_no' is positive, it represents the
 *   error position represents the exact position, otherwise it tells where
 *   the stmt starts.
 */
void
csql_display_csql_err (int line_no, int col_no)
{
  csql_Error_code = CSQL_ERR_SQL_ERROR;

  iq_format_err (csql_Scratch_text, SCRATCH_TEXT_LEN, line_no, col_no);

  if (line_no > 0)
    {
      csql_fputs ("\n", csql_Error_fp);
      csql_fputs_console_conv (csql_Scratch_text, csql_Error_fp);
    }
  nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
}

/*
 * csql_display_session_err() - display all query compilation errors
 *                            for this session
 *   return: none
 *   session(in): context of query compilation
 *   line_no(in): statement starting line number
 */
void
csql_display_session_err (DB_SESSION * session, int line_no)
{
  DB_SESSION_ERROR *err;
  int col_no = 0;

  csql_Error_code = CSQL_ERR_SQL_ERROR;

  err = db_get_errors (session);

  do
    {
      err = db_get_next_error (err, &line_no, &col_no);
      if (line_no > 0)
	{
	  csql_fputs ("\n", csql_Error_fp);
	  iq_format_err (csql_Scratch_text, SCRATCH_TEXT_LEN, line_no, col_no);
	  csql_fputs_console_conv (csql_Scratch_text, csql_Error_fp);
	}
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);
    }
  while (err);

  return;
}

/*
 * csql_append_more_line() - append the given line into the
 *                         more message line array
 *   return: CSQL_FAILURE/CSQL_SUCCESS
 *   indent(in): number of blanks to be prefixed
 *   line(in): new line to be put
 *
 * Note:
 *   After usage of the more lines, caller should free by calling
 *   free_more_lines(). The line cannot have control characters except tab,
 *   new-line and "\1".
 */
int
csql_append_more_line (int indent, const char *line)
{
  int i, j;
  int n;			/* register copy of num_more_lines */
  int exp_len;			/* length of lines after tab expand */
  int new_num;			/* new # of entries */
  char *p;
  const char *q;
  char **t_lines;		/* temp pointer */
  char *conv_buf = NULL;
  int conv_buf_size = 0;

  if (csql_text_utf8_to_console != NULL
      && (*csql_text_utf8_to_console) (line, strlen (line), &conv_buf, &conv_buf_size) == NO_ERROR)
    {
      line = (conv_buf != NULL) ? conv_buf : line;
    }
  else
    {
      assert (conv_buf == NULL);
    }

  n = iq_Num_more_lines;

  if (n % MORE_LINE_EXPANSION_UNIT == 0)
    {
      new_num = n + MORE_LINE_EXPANSION_UNIT;
      if (n == 0)
	{
	  t_lines = (char **) malloc (sizeof (char *) * new_num);
	}
      else
	{
	  t_lines = (char **) realloc (iq_More_lines, sizeof (char *) * new_num);
	}
      if (t_lines == NULL)
	{
	  csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
	  if (conv_buf != NULL)
	    {
	      assert (csql_text_utf8_to_console != NULL);
	      free_and_init (conv_buf);
	    }
	  return (CSQL_FAILURE);
	}
      iq_More_lines = t_lines;
    }

  /* calculate # of bytes should be allocated to store the given line in tab-expanded form */
  for (i = exp_len = 0, q = line; *q != '\0'; q++)
    {
      if (*q == '\n')
	{
	  exp_len += i + 1;
	  i = 0;
	}
      else if (*q == '\t')
	{
	  i += TAB_STOP - i % TAB_STOP;
	}
      else
	{
	  i++;
	}
    }
  exp_len += i + 1;

  iq_More_lines[n] = (char *) malloc (indent + exp_len);
  if (iq_More_lines[n] == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      if (conv_buf != NULL)
	{
	  assert (csql_text_utf8_to_console != NULL);
	  free_and_init (conv_buf);
	}
      return (CSQL_FAILURE);
    }
  for (i = 0, p = iq_More_lines[n]; i < indent; i++)
    {
      *p++ = ' ';
    }

  /* copy the line with tab expansion */
  for (i = 0, q = line; *q != '\0'; q++)
    {
      if (*q == '\n')
	{
	  *p++ = *q;
	  i = 0;
	}
      else if (*q == '\t')
	{
	  for (j = TAB_STOP - i % TAB_STOP; j > 0; j--, i++)
	    {
	      *p++ = ' ';
	    }
	}
      else
	{
	  *p++ = *q;
	  i++;
	}
    }
  *p = '\0';

  iq_Num_more_lines++;

  if (conv_buf != NULL)
    {
      assert (csql_text_utf8_to_console != NULL);
      free_and_init (conv_buf);
    }

  return (CSQL_SUCCESS);
}

/*
 * csql_display_more_lines() - display lines in stdout.
 *   return: none
 *   title(in): optional title message
 *
 * Note: "\1" in line will be displayed `<' and `>', alternatively.
 */
void
csql_display_more_lines (const char *title)
{
  int i;
  FILE *pf;			/* pipe stream to pager */
#if !defined(WINDOWS)
  void (*iq_pipe_save) (int sig);

  iq_pipe_save = signal (SIGPIPE, &iq_pipe_handler);
#endif /* ! WINDOWS */
  if (setjmp (iq_Jmp_buf) == 0)
    {
      pf = csql_popen (csql_Pager_cmd, csql_Output_fp);

      /* display title */
      if (title != NULL)
	{
	  sprintf (csql_Scratch_text, "\n=== %s ===\n\n", title);
	  csql_fputs (csql_Scratch_text, pf);
	}

      for (i = 0; i < iq_Num_more_lines; i++)
	{
	  csql_fputs (iq_More_lines[i], pf);
	  putc ('\n', pf);
	}
      putc ('\n', pf);

      csql_pclose (pf, csql_Output_fp);
    }
#if !defined(WINDOWS)
  signal (SIGPIPE, iq_pipe_save);
#endif /* ! WINDOWS */
}

/*
 * csql_free_more_lines() - free more lines built by csql_append_more_line()
 *   return: none
 */
void
csql_free_more_lines (void)
{
  int i;

  if (iq_Num_more_lines > 0)
    {
      for (i = 0; i < iq_Num_more_lines; i++)
	{
	  if (iq_More_lines[i] != NULL)
	    {
	      free_and_init (iq_More_lines[i]);
	    }
	}
      free_and_init (iq_More_lines);
      iq_Num_more_lines = 0;
    }
}

/*
 * iq_pipe_handler() - Generic longjmp'ing signal handler used
 *                     here we need to catch broken pipe
 *   return: none
 *   sig_no(in)
 *
 * Note:
 */
static void
iq_pipe_handler (int sig_no)
{
  longjmp (iq_Jmp_buf, 1);
}

/*
 * csql_check_server_down() - check if server is down
 *   return: none
 *
 * Note: If server is down, this function exit
 */
void
csql_check_server_down (void)
{
  if (db_error_code () == ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED)
    {
      nonscr_display_error (csql_Scratch_text, SCRATCH_TEXT_LEN);

      fprintf (csql_Error_fp, "Exiting ...\n");
      csql_exit (EXIT_FAILURE);
    }
}

/*
 * csql_get_tmp_buf()
 *   return: a pointer to a buffer for temporary formatting
 *   size(in): the number of characters required
 */
char *
csql_get_tmp_buf (size_t size)
{
  static char *bufp = NULL;
  static size_t bufsize = 0;

  bufsize = size + 1;
  bufp = (char *) malloc (bufsize);

  if (bufp == NULL)
    {
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      bufsize = 0;
      return NULL;
    }
  else
    {
      return bufp;
    }
}

/*
 * nonscr_display_error() - format error message with global error code
 *   return: none
 *   buffer(out): message ouput buffer
 *   buf_length(in): size of output buffer
 */
void
nonscr_display_error (char *buffer, int buf_length)
{
  int remaining = buf_length;
  char *msg;
  const char *errmsg;
  int len_errmsg;
  char *con_buf_ptr = NULL;
  int con_buf_size = 0;

  strncpy (buffer, "\n", remaining);
  remaining -= strlen ("\n");

  msg = msgcat_message (MSGCAT_CATALOG_CSQL, MSGCAT_CSQL_SET_CSQL, CSQL_ERROR_PREFIX);
  strncat (buffer, msg, remaining);
  remaining -= strlen (msg);

  errmsg = csql_errmsg (csql_Error_code);
  len_errmsg = strlen (errmsg);

  if (csql_text_utf8_to_console != NULL
      && (*csql_text_utf8_to_console) (errmsg, len_errmsg, &con_buf_ptr, &con_buf_size) == NO_ERROR)
    {
      if (con_buf_ptr != NULL)
	{
	  errmsg = con_buf_ptr;
	  len_errmsg = con_buf_size;
	}
    }

  if (len_errmsg > (remaining - 3) /* "\n\n" + NULL */ )
    {
      /* error msg will split into 2 pieces which is separated by "......" */
      int print_len;
      const char *separator = "......";
      int separator_len = strlen (separator);

      print_len = (remaining - 3 - separator_len) / 2;
      strncat (buffer, errmsg, print_len);	/* first half */
      strcat (buffer, separator);
      strncat (buffer, errmsg + len_errmsg - print_len, print_len);	/* second half */
      remaining -= (print_len * 2 + separator_len);
    }
  else
    {
      strncat (buffer, errmsg, remaining);
      remaining -= len_errmsg;
    }

  if (con_buf_ptr != NULL)
    {
      free_and_init (con_buf_ptr);
    }

  strncat (buffer, "\n\n", remaining);
  remaining -= strlen ("\n\n");

  buffer[buf_length - 1] = '\0';
  csql_fputs (buffer, csql_Error_fp);
  logddl_set_err_msg (buffer);
}

/*
 * csql_edit_buffer_get_data() - get string of current editor contents
 *   return: pointer of contents
 */
char *
csql_edit_contents_get ()
{
  if (csql_Edit_contents.data_size <= 0)
    {
      return ((char *) "");
    }
  return csql_Edit_contents.contents;
}

static int
csql_edit_contents_expand (int required_size)
{
  int new_alloc_size = csql_Edit_contents.alloc_size;
  if (new_alloc_size >= required_size)
    return CSQL_SUCCESS;

  if (new_alloc_size <= 0)
    {
      new_alloc_size = 1024;
    }
  while (new_alloc_size < required_size)
    {
      new_alloc_size *= 2;
    }
  csql_Edit_contents.contents = (char *) realloc (csql_Edit_contents.contents, new_alloc_size);
  if (csql_Edit_contents.contents == NULL)
    {
      csql_Edit_contents.alloc_size = 0;
      csql_Error_code = CSQL_ERR_NO_MORE_MEMORY;
      return CSQL_FAILURE;
    }
  csql_Edit_contents.alloc_size = new_alloc_size;
  return CSQL_SUCCESS;
}

/*
 * csql_edit_buffer_append() - append string to current editor contents
 *   return: CSQL_SUCCESS/CSQL_FAILURE
 *   str(in): string to append
 *   flag_append_new_line(in): whether or not to append new line char
 */
int
csql_edit_contents_append (const char *str, bool flag_append_new_line)
{
  int str_len, new_data_size;
  if (str == NULL)
    {
      return CSQL_SUCCESS;
    }
  str_len = strlen (str);
  new_data_size = csql_Edit_contents.data_size + str_len;
  if (csql_edit_contents_expand (new_data_size + 2) != CSQL_SUCCESS)
    {
      return CSQL_FAILURE;
    }
  memcpy (csql_Edit_contents.contents + csql_Edit_contents.data_size, str, str_len);
  csql_Edit_contents.data_size = new_data_size;
  if (flag_append_new_line)
    {
      csql_Edit_contents.contents[csql_Edit_contents.data_size++] = '\n';
    }
  csql_Edit_contents.contents[csql_Edit_contents.data_size] = '\0';
  return CSQL_SUCCESS;
}

/*
 * csql_walk_statement () - parse str and change the state
 * return : NULL
 * str (in) : the new statement chunk received from input
 */
void
csql_walk_statement (const char *str)
{
  /* using flags but not adding many states in here may be not good choice, but it will not change the state machine
   * model and save a lot of states. */
  bool include_stmt = false;
  bool is_last_stmt_valid = true;
  const char *p;
  int str_length;

  if (str == NULL)
    {
      return;
    }

  CSQL_STATEMENT_STATE state = csql_Edit_contents.state;
  CSQL_STATEMENT_SUBSTATE substate = csql_Edit_contents.substate;
  int plcsql_begin_end_balance = csql_Edit_contents.plcsql_begin_end_balance;
  int plcsql_nest_level = csql_Edit_contents.plcsql_nest_level;

  assert ((plcsql_begin_end_balance == 0 && plcsql_nest_level == 0) ||
	  (substate == CSQL_SUBSTATE_PLCSQL_TEXT || substate == CSQL_SUBSTATE_SEEN_END));

  if (state == CSQL_STATE_CPP_COMMENT || state == CSQL_STATE_SQL_COMMENT)
    {
      /* these are single line comments and we're parsing a new line */
      state = CSQL_STATE_GENERAL;
    }

  if (state == CSQL_STATE_STATEMENT_END)
    {
      /* reset state in prev statement */
      state = CSQL_STATE_GENERAL;
      substate = CSQL_SUBSTATE_INITIAL;
    }

  str_length = strlen (str);
  /* run as state machine */
  for (p = str; p < str + str_length; p++)
    {
      switch (state)
	{
	case CSQL_STATE_GENERAL:

	  // eat up blanks
	  switch (*p)
	    {
	    case ' ':
	    case '\t':
	    case '\r':
	    case '\n':
	      continue;
	    }

	  // here, *p is a non-white-space

	substate_transition:
	  switch (substate)
	    {
	    case CSQL_SUBSTATE_INITIAL:
	      if (match_word_ci ("create", &p))
		{
		  substate = CSQL_SUBSTATE_SEEN_CREATE;
		  continue;
		}
	      else
		{
		  // keep the substate CSQL_SUBSTATE_INITIAL
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_SEEN_CREATE:
	      if (match_word_ci ("or", &p))
		{
		  substate = CSQL_SUBSTATE_SEEN_OR;
		  continue;
		}
	      else if (match_word_ci ("procedure", &p) || match_word_ci ("function", &p))
		{
		  substate = CSQL_SUBSTATE_EXPECTING_IS_OR_AS;
		  continue;
		}
	      else
		{
		  substate = CSQL_SUBSTATE_INITIAL;
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_SEEN_OR:
	      if (match_word_ci ("replace", &p))
		{
		  substate = CSQL_SUBSTATE_SEEN_REPLACE;
		  continue;
		}
	      else
		{
		  substate = CSQL_SUBSTATE_INITIAL;
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_SEEN_REPLACE:
	      if (match_word_ci ("procedure", &p) || match_word_ci ("function", &p))
		{
		  substate = CSQL_SUBSTATE_EXPECTING_IS_OR_AS;
		  continue;
		}
	      else
		{
		  substate = CSQL_SUBSTATE_INITIAL;
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_EXPECTING_IS_OR_AS:
	      if (match_word_ci ("is", &p) || match_word_ci ("as", &p))
		{
		  substate = CSQL_SUBSTATE_PL_LANG_SPEC;
		  continue;
		}
	      else
		{
		  // keep the substate CSQL_SUBSTATE_EXPECTING_IS_OR_AS
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_PL_LANG_SPEC:
	      if (match_word_ci ("language", &p))
		{
		  substate = CSQL_SUBSTATE_SEEN_LANGUAGE;
		  continue;
		}
	      else
		{
		  // TRANSITION to CSQL_SUBSTATE_PLCSQL_TEXT!!!
		  substate = CSQL_SUBSTATE_PLCSQL_TEXT;
		  plcsql_begin_end_balance = 0;
		  plcsql_nest_level = 0;
		  goto substate_transition;	// use goto to repeat a substate transition without increasing p
		}
	      break;

	    case CSQL_SUBSTATE_SEEN_LANGUAGE:
	      if (match_word_ci ("java", &p))
		{
		  substate = CSQL_SUBSTATE_INITIAL;
		  continue;
		}
	      else if (match_word_ci ("plcsql", &p))
		{
		  // TRANSITION to CSQL_SUBSTATE_PLCSQL_TEXT!!!
		  substate = CSQL_SUBSTATE_PLCSQL_TEXT;
		  plcsql_begin_end_balance = 0;
		  plcsql_nest_level = 0;
		  continue;
		}
	      else
		{
		  // syntax error
		  substate = CSQL_SUBSTATE_INITIAL;
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_PLCSQL_TEXT:
	      if (match_word_ci ("procedure", &p) || match_word_ci ("function", &p))
		{
		  if (plcsql_begin_end_balance == 0)
		    {
		      plcsql_nest_level++;
		    }
		  continue;
		}
	      else if (match_word_ci ("case", &p))
		{
		  // case can start an expression and can appear in a balance 0 area
		  if (plcsql_begin_end_balance == 0)
		    {
		      plcsql_nest_level++;
		    }
		  plcsql_begin_end_balance++;
		  continue;
		}
	      else if (match_word_ci ("begin", &p) || match_word_ci ("if", &p) || match_word_ci ("loop", &p))
		{
		  plcsql_begin_end_balance++;
		  continue;
		}
	      else if (match_word_ci ("end", &p))
		{
		  substate = CSQL_SUBSTATE_SEEN_END;
		  continue;
		}
	      else
		{
		  // keep the substate CSQL_SUBSTATE_PLCSQL_TEXT
		  // break and proceed to the second switch
		}
	      break;

	    case CSQL_SUBSTATE_SEEN_END:
	      plcsql_begin_end_balance--;
	      if (plcsql_begin_end_balance < 0)
		{
		  // syntax error
		  plcsql_begin_end_balance = 0;
		}
	      if (plcsql_begin_end_balance == 0)
		{
		  plcsql_nest_level--;
		  if (plcsql_nest_level < 0)
		    {
		      // the last END closing PL/CSQL text was found
		      substate = CSQL_SUBSTATE_INITIAL;
		      plcsql_begin_end_balance = 0;
		      plcsql_nest_level = 0;
		      goto substate_transition;	// use goto to repeat a substate transition without increasing p
		    }
		}

	      substate = CSQL_SUBSTATE_PLCSQL_TEXT;

	      // match if/case/loop if exists, but just advance p and ignore them
	      if (match_word_ci ("if", &p) || match_word_ci ("case", &p) || match_word_ci ("loop", &p))
		{
		  continue;
		}
	      else
		{
		  goto substate_transition;	// use goto to repeat a substate transition without increasing p
		}

	      break;

	    default:
	      assert (false);	// unreachable
	    }

	  if (is_identifier_letter (*p))
	    {
	      if (!is_last_stmt_valid)
		{
		  is_last_stmt_valid = true;
		}

	      // once an identifier letter is found, advance p while the next letter is also an identifir letter
	      // in other words, consume the whole identifier
	      while (p + 1 < str + str_length && is_identifier_letter (*(p + 1)))
		{
		  p++;
		}
	      continue;
	    }

	  switch (*p)
	    {
	    case '/':
	      if (*(p + 1) == '/')
		{
		  state = CSQL_STATE_CPP_COMMENT;
		  p++;
		  break;
		}
	      if (*(p + 1) == '*')
		{
		  state = CSQL_STATE_C_COMMENT;
		  p++;
		  break;
		}
	      is_last_stmt_valid = true;
	      break;
	    case '-':
	      if (*(p + 1) == '-')
		{
		  state = CSQL_STATE_SQL_COMMENT;
		  p++;
		  break;
		}
	      is_last_stmt_valid = true;
	      break;
	    case '\'':
	      state = CSQL_STATE_SINGLE_QUOTE;
	      is_last_stmt_valid = true;
	      break;
	    case '"':
	      if (prm_get_bool_value (PRM_ID_ANSI_QUOTES) == false)
		{
		  state = CSQL_STATE_MYSQL_QUOTE;
		}
	      else
		{
		  state = CSQL_STATE_DOUBLE_QUOTE_IDENTIFIER;
		}
	      is_last_stmt_valid = true;
	      break;
	    case '`':
	      state = CSQL_STATE_BACKTICK_IDENTIFIER;
	      is_last_stmt_valid = true;
	      break;
	    case '[':
	      state = CSQL_STATE_BRACKET_IDENTIFIER;
	      is_last_stmt_valid = true;
	      break;
	    case ';':
	      if (substate != CSQL_SUBSTATE_PLCSQL_TEXT)
		{
		  assert (substate != CSQL_SUBSTATE_SEEN_END);

		  include_stmt = true;
		  is_last_stmt_valid = false;

		  // initialize the state variables used to identify PL/CSQL text
		  substate = CSQL_SUBSTATE_INITIAL;
		  plcsql_begin_end_balance = 0;
		  plcsql_nest_level = 0;
		}
	      break;
	    case ' ':
	    case '\t':
	    case '\r':
	    case '\n':
	      assert (false);	// unreachable
	      break;
	    default:
	      if (!is_last_stmt_valid)
		{
		  is_last_stmt_valid = true;
		}
	      break;
	    }
	  break;

	case CSQL_STATE_C_COMMENT:
	  if (*p == '*' && *(p + 1) == '/')
	    {
	      state = CSQL_STATE_GENERAL;
	      p++;
	      break;
	    }
	  break;

	case CSQL_STATE_CPP_COMMENT:
	  if (*p == '\n')
	    {
	      state = CSQL_STATE_GENERAL;
	    }
	  break;

	case CSQL_STATE_SQL_COMMENT:
	  if (*p == '\n')
	    {
	      state = CSQL_STATE_GENERAL;
	    }
	  break;

	case CSQL_STATE_SINGLE_QUOTE:
	  if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) == false && *p == '\\')
	    {
	      p++;
	    }
	  else if (*p == '\'')
	    {
	      if (*(p + 1) == '\'')
		{
		  /* escape by '' */
		  p++;
		}
	      else
		{
		  state = CSQL_STATE_GENERAL;
		}
	    }
	  break;

	case CSQL_STATE_MYSQL_QUOTE:
	  if (prm_get_bool_value (PRM_ID_NO_BACKSLASH_ESCAPES) == false && *p == '\\')
	    {
	      p++;
	    }
	  else if (*p == '"')
	    {
	      if (*(p + 1) == '\"')
		{
		  /* escape by "" */
		  p++;
		}
	      else
		{
		  state = CSQL_STATE_GENERAL;
		}
	    }
	  break;

	case CSQL_STATE_DOUBLE_QUOTE_IDENTIFIER:
	  if (*p == '"')
	    {
	      state = CSQL_STATE_GENERAL;
	    }
	  break;

	case CSQL_STATE_BACKTICK_IDENTIFIER:
	  if (*p == '`')
	    {
	      state = CSQL_STATE_GENERAL;
	    }
	  break;

	case CSQL_STATE_BRACKET_IDENTIFIER:
	  if (*p == ']')
	    {
	      state = CSQL_STATE_GENERAL;
	    }
	  break;

	default:
	  /* should not be here */
	  break;
	}
    }

  /* when include other stmts and the last smt is non sense stmt. */
  if (include_stmt && !is_last_stmt_valid
      && (state == CSQL_STATE_SQL_COMMENT || state == CSQL_STATE_CPP_COMMENT || state == CSQL_STATE_GENERAL))
    {
      state = CSQL_STATE_STATEMENT_END;
    }

  csql_Edit_contents.state = state;
  csql_Edit_contents.substate = substate;
  csql_Edit_contents.plcsql_begin_end_balance = plcsql_begin_end_balance;
  csql_Edit_contents.plcsql_nest_level = plcsql_nest_level;
}

/*
 * csql_is_statement_complete () - check if end of statement is reached
 * return : true if statement end is reached, false otherwise
 */
bool
csql_is_statement_complete (void)
{
  if (csql_Edit_contents.state == CSQL_STATE_STATEMENT_END)
    {
      return true;
    }
  else
    {
      return false;
    }
}

/*
 * csql_is_statement_in_block () - check if statement state is string block or
 *                       comment block or identifier block
 * return : true if yes, false otherwise
 */
bool
csql_is_statement_in_block (void)
{
  CSQL_STATEMENT_STATE state = csql_Edit_contents.state;
  if (state == CSQL_STATE_C_COMMENT || state == CSQL_STATE_SINGLE_QUOTE || state == CSQL_STATE_MYSQL_QUOTE
      || state == CSQL_STATE_DOUBLE_QUOTE_IDENTIFIER || state == CSQL_STATE_BACKTICK_IDENTIFIER
      || state == CSQL_STATE_BRACKET_IDENTIFIER)
    {
      return true;
    }

  CSQL_STATEMENT_SUBSTATE substate = csql_Edit_contents.substate;
  if (state == CSQL_STATE_GENERAL && (substate == CSQL_SUBSTATE_PLCSQL_TEXT || substate == CSQL_SUBSTATE_SEEN_END))
    {
      return true;
    }

  return false;
}

/*
 * csql_edit_buffer_clear() - clear current editor contents
 *   return: none
 * NOTE: allocated memory in csql_Edit_contents is not freed.
 */
void
csql_edit_contents_clear ()
{
  csql_Edit_contents.data_size = 0;
  csql_Edit_contents.state = CSQL_STATE_GENERAL;
  csql_Edit_contents.substate = CSQL_SUBSTATE_INITIAL;
  csql_Edit_contents.plcsql_begin_end_balance = 0;
  csql_Edit_contents.plcsql_nest_level = 0;
}

void
csql_edit_contents_finalize ()
{
  csql_edit_contents_clear ();
  free_and_init (csql_Edit_contents.contents);
  csql_Edit_contents.alloc_size = 0;
}

/*
 * csql_edit_read_file() - read chars from the given file stream into
 *                          current editor contents
 *   return: CSQL_FAILURE/CSQL_SUCCESS
 *   fp(in): file stream
 */
int
csql_edit_read_file (FILE * fp)
{
  char line_buf[1024];
  bool is_first_read_line = true;

  while (fgets (line_buf, sizeof (line_buf), fp) != NULL)
    {
      char *line_begin = line_buf;

      if (is_first_read_line && intl_is_bom_magic (line_buf, strlen (line_buf)))
	{
	  line_begin += 3;
	}

      is_first_read_line = false;

      if (csql_edit_contents_append (line_begin, false) != CSQL_SUCCESS)
	return CSQL_FAILURE;
    }
  return CSQL_SUCCESS;
}

/*
 * csql_edit_write_file() - write current editor contents to specified file
 *   return: CSQL_FAILURE/CSQL_SUCCESS
 *   fp(in): open file pointer
 */
int
csql_edit_write_file (FILE * fp)
{
  char *p = csql_Edit_contents.contents;
  int remain_size = csql_Edit_contents.data_size;
  int write_len;
  while (remain_size > 0)
    {
      write_len = (int) fwrite (p + (csql_Edit_contents.data_size - remain_size), 1, remain_size, fp);
      if (write_len <= 0)
	{
	  csql_Error_code = CSQL_ERR_OS_ERROR;
	  return CSQL_FAILURE;
	}
      remain_size -= write_len;
    }
  return CSQL_SUCCESS;
}

typedef struct
{
  int error_code;
  int msg_id;
} CSQL_ERR_MSG_MAP;

static CSQL_ERR_MSG_MAP csql_Err_msg_map[] = {
  {CSQL_ERR_NO_MORE_MEMORY, CSQL_E_NOMOREMEMORY_TEXT},
  {CSQL_ERR_TOO_LONG_LINE, CSQL_E_TOOLONGLINE_TEXT},
  {CSQL_ERR_TOO_MANY_LINES, CSQL_E_TOOMANYLINES_TEXT},
  {CSQL_ERR_TOO_MANY_FILE_NAMES, CSQL_E_TOOMANYFILENAMES_TEXT},
  {CSQL_ERR_SESS_CMD_NOT_FOUND, CSQL_E_SESSCMDNOTFOUND_TEXT},
  {CSQL_ERR_SESS_CMD_AMBIGUOUS, CSQL_E_SESSCMDAMBIGUOUS_TEXT},
  {CSQL_ERR_FILE_NAME_MISSED, CSQL_E_FILENAMEMISSED_TEXT},
  {CSQL_ERR_CUBRID_STMT_AMBIGUOUS, CSQL_E_CSQLCMDAMBIGUOUS_TEXT},
  {CSQL_ERR_CANT_EXEC_PAGER, CSQL_E_CANTEXECPAGER_TEXT},
  {CSQL_ERR_INVALID_ARG_COMBINATION, CSQL_E_INVALIDARGCOM_TEXT},
  {CSQL_ERR_CANT_EDIT, CSQL_E_CANT_EDIT_TEXT},
  {CSQL_ERR_INFO_CMD_HELP, CSQL_HELP_INFOCMD_TEXT},
  {CSQL_ERR_CLASS_NAME_MISSED, CSQL_E_CLASSNAMEMISSED_TEXT},
  {CSQL_ERR_FORMAT, CSQL_E_FORMAT_TEXT}
};

/*
 * csql_errmsg() - return an error message string according to the given
 *               error code
 *   return: error message
 *   code(in): error code
 */
const char *
csql_errmsg (int code)
{
  int msg_map_size;
  const char *msg;

  if (code == CSQL_ERR_OS_ERROR)
    {
      return (strerror (errno));
    }
  else if (code == CSQL_ERR_SQL_ERROR)
    {
      msg = db_error_string (DEFAULT_DB_ERROR_MSG_LEVEL);
      return ((msg == NULL) ? "" : msg);
    }
  else
    {
      int i;

      msg_map_size = DIM (csql_Err_msg_map);
      for (i = 0; i < msg_map_size; i++)
	{
	  if (code == csql_Err_msg_map[i].error_code)
	    {
	      return (csql_get_message (csql_Err_msg_map[i].msg_id));
	    }
	}
      return (csql_get_message (CSQL_E_UNKNOWN_TEXT));
    }
}

static bool
is_identifier_letter (const char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
}

static bool
match_word_ci (const char *word, const char **bufp)
{
  int len = strlen (word);
  assert (len > 0);

  if (strncasecmp (word, *bufp, len) == 0 && !is_identifier_letter ((*bufp)[len]))
    {
      *bufp += (len - 1);	// advance the pointer to the last letter
      return true;
    }
  else
    {
      return false;
    }
}
