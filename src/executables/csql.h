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
 * csql.h : header file for csql
 *
 */

#ifndef _CSQL_H_
#define _CSQL_H_

#ident "$Id$"

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <locale.h>

#include "porting.h"
#include "language_support.h"
#include "message_catalog.h"
#include "util_func.h"
#include "misc_string.h"
#include "dbi.h"
#include "error_manager.h"
#include "object_print.h"
#include "memory_alloc.h"

#if defined(WINDOWS)
#define isatty(stream)	_isatty(stream)
#endif /* WINDOWS */

#define MSGCAT_CSQL_SET_CSQL	  1

/*
 * MESSAGE NUMBERS
 */
enum
{
  CSQL_MSG_USAGE = 40,
  CSQL_MSG_BAD_MODE = 41,
  CSQL_MSG_BAD_ARGS = 42,
  CSQL_MSG_NO_ENV = 43,
  CSQL_MSG_EXEC_FAILURE = 44,
  CSQL_MSG_BOTH_MODES = 45,

  CSQL_EXECUTE_END_MSG_FORMAT = 46,
  CSQL_START_POSITION_ERR_FORMAT = 47,
  CSQL_EXACT_POSITION_ERR_FORMAT = 48,
  CSQL_INITIAL_HELP_MSG = 49,
  CSQL_ERROR_PREFIX = 50,
  CSQL_INITIAL_CSQL_TITLE = 51,
  CSQL_TRANS_TERMINATE_PROMPT_TEXT = 52,
  CSQL_TRANS_TERMINATE_PROMPT_RETRY_TEXT = 53,
  CSQL_STAT_COMMITTED_TEXT = 54,
  CSQL_STAT_ROLLBACKED_TEXT = 55,
  CSQL_STAT_EDITOR_SAVED_TEXT = 56,
  CSQL_STAT_READ_DONE_TEXT = 57,
  CSQL_STAT_EDITOR_PRINTED_TEXT = 58,
  CSQL_STAT_CD_TEXT = 59,
  CSQL_PASSWD_PROMPT_TEXT = 61,
  CSQL_RESULT_STMT_TITLE_FORMAT = 62,
  CSQL_STAT_NONSCR_EMPTY_RESULT_TEXT = 63,
  CSQL_STAT_CHECKPOINT_TEXT = 64,
  CSQL_STAT_RESTART_TEXT = 65,
  CSQL_KILLTRAN_TITLE_TEXT = 66,
  CSQL_KILLTRAN_FORMAT = 67,
  CSQL_STAT_KILLTRAN_TEXT = 68,
  CSQL_STAT_KILLTRAN_FAIL_TEXT = 69,
  CSQL_ROWS = 70,
  CSQL_ROW = 71,
  CSQL_ARG_AUTO = 75,
  CSQL_ARG_AUTO_HELP = 76,
  CSQL_PROMPT = 79,
  CSQL_NAME = 80,
  CSQL_SYSADM_PROMPT = 81,
  CSQL_TRANSACTIONS = 82,
  CSQL_TRANSACTION = 83,

  CSQL_HELP_SCHEMA_TITLE_TEXT = 145,
  CSQL_HELP_NONE_TEXT = 146,
  CSQL_HELP_TRIGGER_ALL_TITLE_TEXT = 147,
  CSQL_HELP_TRIGGER_NONE_TITLE_TEXT = 148,
  CSQL_HELP_TRIGGER_TITLE_TEXT = 150,
  CSQL_HELP_SQL_TITLE_TEXT = 151,
  CSQL_HELP_SESSION_CMD_TITLE_TEXT = 152,
  CSQL_E_FILENAMEMISSED_TEXT = 178,
  CSQL_E_CANTEXECPAGER_TEXT = 179,
  CSQL_E_NOMOREMEMORY_TEXT = 180,
  CSQL_E_TOOLONGLINE_TEXT = 184,
  CSQL_E_TOOMANYLINES_TEXT = 185,
  CSQL_E_TOOMANYFILENAMES_TEXT = 188,
  CSQL_E_SESSCMDNOTFOUND_TEXT = 190,
  CSQL_E_SESSCMDAMBIGUOUS_TEXT = 191,
  CSQL_E_CSQLCMDAMBIGUOUS_TEXT = 193,
  CSQL_E_INVALIDARGCOM_TEXT = 194,
  CSQL_E_UNKNOWN_TEXT = 196,
  CSQL_E_CANT_EDIT_TEXT = 197,

  CSQL_HELP_CLASS_HEAD_TEXT = 203,
  CSQL_HELP_SUPER_CLASS_HEAD_TEXT = 204,
  CSQL_HELP_SUB_CLASS_HEAD_TEXT = 205,
  CSQL_HELP_ATTRIBUTE_HEAD_TEXT = 206,
  CSQL_HELP_CLASS_ATTRIBUTE_HEAD_TEXT = 207,
  CSQL_HELP_METHOD_HEAD_TEXT = 208,
  CSQL_HELP_CLASS_METHOD_HEAD_TEXT = 209,
  CSQL_HELP_RESOLUTION_HEAD_TEXT = 210,
  CSQL_HELP_METHFILE_HEAD_TEXT = 211,
  CSQL_HELP_QUERY_SPEC_HEAD_TEXT = 212,
  CSQL_HELP_TRIGGER_HEAD_TEXT = 213,
  CSQL_HELP_TRIGGER_NAME_TEXT = 214,
  CSQL_HELP_TRIGGER_EVENT_TEXT = 215,
  CSQL_HELP_TRIGGER_CONDITION_TIME_TEXT = 216,
  CSQL_HELP_TRIGGER_CONDITION_TEXT = 217,
  CSQL_HELP_TRIGGER_ACTION_TIME_TEXT = 218,
  CSQL_HELP_TRIGGER_ACTION_TEXT = 219,
  CSQL_HELP_TRIGGER_STATUS_TEXT = 220,
  CSQL_HELP_TRIGGER_PRIORITY_TEXT = 221,
  CSQL_HELP_SQL_NAME_HEAD_TEXT = 222,
  CSQL_HELP_SQL_DESCRIPTION_HEAD_TEXT = 223,
  CSQL_HELP_SQL_SYNTAX_HEAD_TEXT = 224,
  CSQL_HELP_SQL_EXAMPLE_HEAD_TEXT = 225,
  CSQL_HELP_SESSION_CMD_TEXT = 231,
  CSQL_HELP_CONSTRAINT_HEAD_TEXT = 232,
  CSQL_HELP_INFOCMD_TEXT = 233,
  CSQL_HELP_PARTITION_HEAD_TEXT = 235,
  CSQL_E_CLASSNAMEMISSED_TEXT = 236
};

#define SCRATCH_TEXT_LEN (4096)

/* error codes defined in csql level */
enum
{
  CSQL_ERR_OS_ERROR = 1,
  CSQL_ERR_NO_MORE_MEMORY,
  CSQL_ERR_TOO_LONG_LINE,
  CSQL_ERR_TOO_MANY_LINES,
  CSQL_ERR_TOO_MANY_FILE_NAMES,
  CSQL_ERR_SQL_ERROR,
  CSQL_ERR_SESS_CMD_NOT_FOUND,
  CSQL_ERR_SESS_CMD_AMBIGUOUS,
  CSQL_ERR_FILE_NAME_MISSED,
  CSQL_ERR_CUBRID_STMT_NOT_FOUND,
  CSQL_ERR_CUBRID_STMT_AMBIGUOUS,
  CSQL_ERR_CANT_EXEC_PAGER,
  CSQL_ERR_INVALID_ARG_COMBINATION,
  CSQL_ERR_CANT_EDIT,
  CSQL_ERR_INFO_CMD_HELP,
  CSQL_ERR_CLASS_NAME_MISSED
};

/* session command numbers */
typedef enum
{
/* File stuffs */
  S_CMD_READ,
  S_CMD_WRITE,
  S_CMD_APPEND,
  S_CMD_PRINT,
  S_CMD_SHELL,
  S_CMD_CD,
  S_CMD_EXIT,

/* Edit stuffs */
  S_CMD_CLEAR,
  S_CMD_EDIT,
  S_CMD_LIST,

/* Command stuffs */
  S_CMD_RUN,
  S_CMD_XRUN,
  S_CMD_COMMIT,
  S_CMD_ROLLBACK,
  S_CMD_AUTOCOMMIT,
  S_CMD_CHECKPOINT,
  S_CMD_KILLTRAN,
  S_CMD_RESTART,

/* Environment stuffs */
  S_CMD_SHELL_CMD,
  S_CMD_EDIT_CMD,
  S_CMD_PRINT_CMD,
  S_CMD_PAGER_CMD,
  S_CMD_NOPAGER_CMD,
  S_CMD_COLUMN_WIDTH,
  S_CMD_STRING_WIDTH,

/* Help stuffs */
  S_CMD_HELP,
  S_CMD_SCHEMA,
  S_CMD_DATABASE,
  S_CMD_TRIGGER,
  S_CMD_INFO,

/* More environment stuff */
  S_CMD_SET_PARAM,
  S_CMD_GET_PARAM,
  S_CMD_PLAN_DUMP,
  S_CMD_ECHO,
  S_CMD_DATE,
  S_CMD_TIME,
  S_CMD_LINE_OUTPUT,

/* Histogram profile stuff */
  S_CMD_HISTO,
  S_CMD_CLR_HISTO,
  S_CMD_DUMP_HISTO,
  S_CMD_DUMP_CLR_HISTO,

/* cmd history stuffs */
  S_CMD_HISTORY_READ,
  S_CMD_HISTORY_LIST,

  S_CMD_TRACE
} SESSION_CMD;

/* iq_ function return status */
enum
{
  CSQL_FAILURE = -1,
  CSQL_SUCCESS = 0
};

typedef struct
{
  const char *db_name;
  const char *user_name;
  const char *passwd;
  const char *in_file_name;
  const char *out_file_name;
  const char *command;
  bool sa_mode;
  bool cs_mode;
  bool single_line_execution;
  bool column_output;
  bool line_output;
  bool read_only;
  bool auto_commit;
  bool nopager;
  bool continue_on_error;
  bool sysadm;
  bool write_on_standby;
  bool trigger_action_flag;
  int string_width;
#if defined(CSQL_NO_LONGGING)
  bool no_logging;
#endif				/* CSQL_NO_LONGGING */
} CSQL_ARGUMENT;

typedef struct
{
  char *name;
  int width;
} CSQL_COLUMN_WIDTH_INFO;

/* The file streams we are to use */
extern FILE *csql_Input_fp;
extern FILE *csql_Output_fp;
extern FILE *csql_Error_fp;

extern char csql_Editor_cmd[];
extern char csql_Shell_cmd[];
extern char csql_Print_cmd[];
extern char csql_Pager_cmd[];
extern char csql_Scratch_text[];
extern int csql_Error_code;


extern int csql_Line_lwm;
extern int csql_Row_count;
extern int csql_Num_failures;

extern int (*csql_text_utf8_to_console) (const char *, const int, char **,
					 int *);
extern int (*csql_text_console_to_utf8) (const char *, const int, char **,
					 int *);

extern void csql_display_msg (const char *string);
extern void csql_exit (int exit_status);
extern int csql (const char *argv0, CSQL_ARGUMENT * csql_arg);
extern const char *csql_get_message (int message_index);

extern char *csql_get_real_path (const char *pathname);
extern void csql_invoke_system (const char *command);
extern int csql_invoke_system_editor (void);
extern void csql_fputs (const char *str, FILE * fp);
extern void csql_fputs_console_conv (const char *str, FILE * fp);
extern FILE *csql_popen (const char *cmd, FILE * fd);
extern void csql_pclose (FILE * pf, FILE * fd);
extern void csql_display_csql_err (int line_no, int col_no);
extern void csql_display_session_err (DB_SESSION * session, int line_no);
extern int csql_append_more_line (int indent, const char *line);
extern void csql_display_more_lines (const char *title);
extern void csql_free_more_lines (void);
extern void csql_check_server_down (void);
extern char *csql_get_tmp_buf (size_t size);
extern void nonscr_display_error (char *buffer, int buf_length);

extern int csql_get_session_cmd_no (const char *input);

extern void csql_results (const CSQL_ARGUMENT * csql_arg,
			  DB_QUERY_RESULT * result, DB_QUERY_TYPE * attr_spec,
			  int line_no, CUBRID_STMT_TYPE stmt_type);

extern char *csql_edit_contents_get (void);
extern int csql_edit_contents_append (const char *str,
				      bool flag_append_new_line);
extern void csql_walk_statement (const char *str);
extern bool csql_is_statement_complete (void);
extern bool csql_is_statement_in_block (void);
extern void csql_edit_contents_clear (void);
extern void csql_edit_contents_finalize (void);
extern int csql_edit_read_file (FILE * fp);
extern int csql_edit_write_file (FILE * fp);

extern const char *csql_errmsg (int code);

extern void csql_help_menu (void);
extern void csql_help_schema (const char *class_name);
extern void csql_help_trigger (const char *class_name);
extern void csql_help_info (const char *command, int aucommit_flag);
extern void csql_killtran (const char *argument);

extern char *csql_db_value_as_string (DB_VALUE * value, int *length);

extern int csql_set_column_width_info (const char *column_name,
				       int column_width);
extern int csql_get_column_width (const char *column_name);

#endif /* _CSQL_H_ */
