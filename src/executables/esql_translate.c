/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * trans.c - translate ESQL statements into sequences of UniCI function calls.
 * 
 * note : Translator is a library which is used by Embedded SQL/X C compiler.
 *  The compiler accepts Embedded SQL/X statements and call this library
 *  functions to emit the corresponding C codes. Each function in this library
 *  emits CUBRID Call Interface functions (prefixed by `uci_') to perform the
 *  work corresponding to the Embbed SQL/X statement at the run-time.
 * 
 *  GENERATION OF UNIQUE ID FOR EACH SOURCE FILE
 * 
 *  Repetitive statements, cursors and dynamic statements are created and then
 *  possibly accessed more than one time by users. Users specify a unique name
 *  for each cursor or dynamic statement. Translator generates serial number
 *  for each repetitive statement. Therefore, these statements are given their
 *  own unique identifier within a C source file. To get unique identifiers
 *  for these statements at run-time (possibly among more than one source
 *  files), we introduce a unique run-time file identifier which will be
 *  associated with the statement identifiers built at compilation time.
 * 
 *  This is accomplished by introducing a static variable in each Embedded
 *  SQL/X C source file (by forcing users to include a header file which
 *  contains a declaration like following.
 * 
 *  static char uci_esqlxc_file;
 * 
 *  Actually, this inclusion can be done automatically by pre-compiler.
 *  During the pre-compilation time of each file, the pre-compiler assigns
 *  a serial number for such statements. This serial number is unique within
 *  a source file. At the run-time, each statement can be identified uniquely
 *  using both the address of `uci_esqlxc_file' and the serial number.
 * 
 *  The caller (parser) should be responsible for the followings
 *      . all indicators should be C_TYPE_SHORT
 *      . db_name/user_name/passwd in tr_connect(), `stmt' in tr_prepare()/
 *        tr_execute_immediate() should be one of C_TYPE_CHAR_ARRAY,
 *        C_TYPE_CHAR_POINTER and C_TYPE_STRING_CONST.
 *      . `obj' in tr_object_{describe,fetch}() should be C_TYPE_OBJECTID.
 * 
 */
#ident "$Id$"

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include "language_support.h"
#include "ustring.h"
#include "util_func.h"
#include "dbi.h"
#include "esql_translate.h"
#include "memory_manager_2.h"
#include "esql_misc.h"

/* the following definition is subject to changes of esqlx.h which defines
 * stuffs directly exported to users. ESQLX_FILE_ID_VAR_NAME is the
 * name of static variable which all user's esqlx C program should have.
 * NOT_FOUND_MACRO_NAME is the macro definition name of SQL_NOT_FOUND
 * in esqlx.h. WARN_CHAR_MACRO_NAME is the macro definition name of
 * SQL_WARNING_CHAR in esqlx.h.
 * 
 * NOTE: Be sure to make these definition consistent with the esqlx.h file.
 */
#define	ESQLX_FILE_ID_VAR_NAME		"uci_esqlxc_file"
#define	NOT_FOUND_MACRO_NAME		"SQL_NOT_FOUND"
#define	WARN_CHAR_MACRO_NAME		"SQL_WARNING_CHAR"

/* to assign a unique statement number to repetitive ordinary statements and
 * dynamic statements.
 */
#define	REPETITIVE_SERIAL_NO	(repetitive_no++)
#define	FP			c_out_stream

/* WHENEVER action descriptor structure */
typedef struct when_desc
{
  WHEN_ACTION action;		/* action number */
  const char *name;		/* label or procedure name */
} WHEN_DESC;

static int repetitive_no = 0;	/* serial number for repititive stmt */

/* C source file output stream */
static FILE *c_out_stream;

/* Line termination; default is to string everything onto one line. */
static const char *NL = " ";

/* WHENEVER action descriptors */
static WHEN_DESC on_warning = { CONTINUE, (char *) NULL };
static WHEN_DESC on_error = { CONTINUE, (char *) NULL };
static WHEN_DESC on_not_found = { CONTINUE, (char *) NULL };

static void emit_start (int leading_brace);
static void emit_end (void);
static void get_quasi_string (HOST_REF * ref,
			      const char **buf_str, const char **bufsize_str);
static void tr_connect (HOST_REF * db_name, HOST_REF * user_name,
			HOST_REF * passwd);
static void tr_disconnect (void);
static void tr_commit (void);
static void tr_rollback (void);
static void tr_static (const char *stmt, int length, bool repeat,
		       int num_in_vars, HOST_REF * in_vars,
		       const char *in_desc_name, int num_out_vars,
		       HOST_REF * out_vars, const char *out_desc_name);
static void tr_open_cs (int cs_no, const char *stmt, int length,
			int stmt_no, bool readonly, int num_in_vars,
			HOST_REF * in_vars, const char *desc_name);
static void tr_fetch_cs (int cs_no, int num_out_vars, HOST_REF * out_vars,
			 const char *desc_name);
static void tr_update_cs (int cs_no, const char *text, int length,
			  bool repetitive,
			  int num_in_vars, HOST_REF * in_vars);
static void tr_delete_cs (int cs_no);
static void tr_close_cs (int cs_no);
static void tr_prepare_esql (int stmt_no, HOST_REF * stmt);
static void tr_describe (int stmt_no, const char *desc_name);
static void tr_execute (int stmt_no, int num_in_vars, HOST_REF * in_vars,
			const char *in_desc_name, int num_out_vars,
			HOST_REF * out_vars, const char *out_desc_name);
static void tr_execute_immediate (HOST_REF * stmt);
static void tr_object_describe (HOST_REF * obj, int num_attrs,
				const char **attr_names,
				const char *desc_name);
static void tr_object_fetch (HOST_REF * obj, int num_attrs,
			     const char **attr_names, int num_out_vars,
			     HOST_REF * out_vars, const char *desc_name);
static void tr_object_update (const char *set_expr, int length,
			      bool repetitive, int num_in_vars,
			      HOST_REF * in_vars);
static void tr_whenever (WHEN_CONDITION condition, WHEN_ACTION action,
			 const char *name);
static void tr_set_out_stream (FILE * out_stream);
static void tr_set_line_terminator (const char *);

static void emit_put_db_value (HOST_REF * host);
static void emit_get_db_value (int cs_no, HOST_REF * host);
static void tr_print_n_string (char *stmt, int length);
static void emit_whenever (void);
static char *escape_string (const char *, int length, int *counter);

static const char *c_type_to_db_type_c[NUM_C_VARIABLE_TYPES] = {
  "DB_TYPE_C_SHORT",
  "DB_TYPE_C_INT",
  "DB_TYPE_C_LONG",
  "DB_TYPE_C_FLOAT",
  "DB_TYPE_C_DOUBLE",
  "DB_TYPE_C_CHAR",
  "DB_TYPE_C_CHAR",
  "DB_TYPE_C_OBJECT",
  "DB_TYPE_C_SET",		/* Probably ought to be collection */
  "DB_TYPE_C_SET",		/* Probably ought to be collection */
  "DB_TYPE_C_SET",		/* Probably ought to be collection */
  "DB_TYPE_C_SET",		/* Probably ought to be collection */
  "DB_TYPE_C_TIME",
  "DB_TYPE_C_TIMESTAMP",
  "DB_TYPE_C_DATE",
  "DB_TYPE_C_MONETARY",
  "",				/* DB_VALUE; what to do? */
  "DB_TYPE_C_VARCHAR",
  "DB_TYPE_C_BIT",
  "DB_TYPE_C_VARBIT"
};

enum esqlx_msg			/* Message codes for esqlx.msg */
{
  MSG_BAD_CASE = 1
};

ESQL_TRANSLATE_TABLE esql_Translate_table = {
  tr_connect,
  tr_disconnect,
  tr_commit,
  tr_rollback,
  tr_static,
  tr_open_cs,
  tr_fetch_cs,
  tr_update_cs,
  tr_delete_cs,
  tr_close_cs,
  tr_prepare_esql,
  tr_describe,
  tr_execute,
  tr_execute_immediate,
  tr_object_describe,
  tr_object_fetch,
  tr_object_update,
  tr_whenever,
  tr_set_out_stream,
  tr_set_line_terminator
};

/*
 * emit_start() - 
 * return:
 * leading_brace(in) :
 */
static void
emit_start (int leading_brace)
{
  if (leading_brace)
    {
      fprintf (FP, "{ ");
    }
  fprintf (FP, "uci_start((void *)&%s, __FILE__, __LINE__, 0x%04x); %s",
	   ESQLX_FILE_ID_VAR_NAME, pp_uci_opt, NL);
  emit_line_directive ();
}

/*
 * emit_end() - 
 * return:
 */
static void
emit_end (void)
{
  fprintf (FP, "  uci_end();%s", NL);
  emit_line_directive ();
  emit_whenever ();
  fprintf (FP, "}%s", NL);
}

/*
 * get_quasi_string() - 
 * return: void
 * ref(in) :
 * buf_str(out) :
 * bufsize_str(in) :
 */
static void
get_quasi_string (HOST_REF * ref,
		  const char **buf_str, const char **bufsize_str)
{
  /*
   * Take any host variable that can legitimately act as a string
   * "constant" for input purposes, and return strings to expressions
   * that describe the start of the char buffer and the length of the
   * char buffer.
   */
  switch (pp_get_type (ref))
    {
    case C_TYPE_VARCHAR:
    case C_TYPE_NCHAR:
    case C_TYPE_VARNCHAR:
      {
	*buf_str = pp_get_addr_expr (ref);
	*bufsize_str = pp_get_input_size (ref);
      }
      break;

    case C_TYPE_CHAR_ARRAY:
    case C_TYPE_CHAR_POINTER:
    case C_TYPE_STRING_CONST:
      {
	*buf_str = pp_get_expr (ref);
	*bufsize_str = pp_get_input_size (ref);
      }
      break;

    default:
      {
	yyverror (pp_get_msg (EX_TRANS_SET, MSG_BAD_CASE), "emit_prepare");
	*buf_str = "NULL";
	*bufsize_str = "0";
      }
      break;
    }
}

/*
 * tr_connect() - emit code to connect to the speficied database using
 *    the given user information. `user_name' can be NULL to indicate
 *    current user's login name. If 'user_name' is NULL, `passwd' has
 *    no meaning. if `passwd' is NULL the empty passwd is assumed
 * return : void 
 * db_name(in) - pointer to symbol desc of database name
 * user_name(in) - pointer to symbol desc of user name
 * passwd(in) - pointer to symbol desc of passwd
 */
static void
tr_connect (HOST_REF * db_name, HOST_REF * user_name, HOST_REF * passwd)
{
  const char *buf_str;
  const char *bufsize_str;

  emit_start (1);

  /*
   * Notice that the code we're emitting here is ignoring buffer lengths,
   * so it's not going to fare well with embedded nulls.  This probably
   * isn't much of a practical concern, since the strings we're coping
   * with here are external names and such, and probably have to be
   * null-terminated anyway.
   */
  if (pp_enable_uci_trace)
    {
      fprintf (FP, "fprintf(stderr, \"uci_connect(%%s, %%s, %%s)\\n\", ");
      get_quasi_string (db_name, &buf_str, &bufsize_str);
      fprintf (FP, "%s, ", buf_str);
      if (user_name == NULL)
	{
	  fprintf (FP, " \"(char *)0\", \"(char *)0\"); %s", NL);
	}
      else
	{
	  get_quasi_string (user_name, &buf_str, &bufsize_str);
	  fprintf (FP, " %s, ", buf_str);
	  if (passwd == NULL)
	    {
	      fprintf (FP, " \"(char *)0\"); %s", NL);
	    }
	  else
	    {
	      get_quasi_string (passwd, &buf_str, &bufsize_str);
	      fprintf (FP, " %s); %s", buf_str, NL);
	    }
	}
    }
  get_quasi_string (db_name, &buf_str, &bufsize_str);
  fprintf (FP, "  uci_connect(%s, ", buf_str);
  if (user_name == NULL)
    {
      fprintf (FP, "(char *)0, (char *)0);%s", NL);
      emit_line_directive ();
    }
  else
    {
      get_quasi_string (user_name, &buf_str, &bufsize_str);
      fprintf (FP, "%s, ", buf_str);
      if (passwd == NULL)
	{
	  fprintf (FP, "(char *)0);%s", NL);
	  emit_line_directive ();
	}
      else
	{
	  get_quasi_string (passwd, &buf_str, &bufsize_str);
	  fprintf (FP, "%s);%s", buf_str, NL);
	  emit_line_directive ();
	}
    }

  emit_end ();
}

/*
 * tr_disconnect() - Emit the code to disconnect from the current database
 * return : void 
 */
static void
tr_disconnect (void)
{
  emit_start (1);
  fprintf (FP, "  uci_disconnect();%s", NL);
  emit_line_directive ();
  emit_end ();
}

/*
 * tr_commit() - Emit the code to commit the current transaction.
 * return : void
 */
static void
tr_commit (void)
{
  emit_start (1);
  if (pp_enable_uci_trace)
    {
      fprintf (FP, "fprintf(stderr, \"uci_commit()\\n\"); %s", NL);
    }
  fprintf (FP, "  uci_commit();%s", NL);
  emit_line_directive ();
  emit_end ();
}

/*
 * tr_rollback() - emit the code to rollback the current transaction.
 * return : none
 */
static void
tr_rollback (void)
{
  emit_start (1);
  if (pp_enable_uci_trace)
    {
      fprintf (FP, "fprintf(stderr, \"uci_rollback()\\n\"); %s", NL);
    }
  fprintf (FP, "  uci_rollback();%s", NL);
  emit_line_directive ();
  emit_end ();
}

/*
 * tr_static() - emit codes to execute the given statements.  If `num_in_vars'
 *    is positive, it is assumed to be the number of input host variables
 *    provided by the user, and `in_vars' is assumed to be an array of that
 *    many HOST_REF structures.  If `num_in_vars' is 0 and 'in_desc_name' is
 *    non-NULL, `in_desc_name' is assumed to be the name of an sqlda structure
 *    that specifies the input host variables.  If `num_in_vars' is 0 and
 *    'in_desc_name' is NULL, it is assumed that there are no input host
 *    variables.  Similar rules apply to 'num_out_vars', `out_vars', and
 *    'out_desc_name'.  If repetitive is 0, the statement is regarded as an ad
 *    hoc statement, otherwise, a repetitive statement.
 *    statement_coverage: all static statements
 * return/side-effects: none
 * stmt(in) : pointer to statement string
 * length(in) : length of stmt
 * repetitive(in) : whether the stmt is repetitive or not
 * num_in_vars(in) : # of input variables
 * in_vars(in) : array of pointers to input variable descriptions
 * in_desc_name(in) : descriptor name if given
 * num_out_vars(in) : # of output variables
 * out_vars(in) : array of pointerss to output variable descriptions
 * out_desc_name(in) : descriptor name if given
 */
static void
tr_static (const char *stmt, int length, bool repetitive,
	   int num_in_vars, HOST_REF * in_vars,
	   const char *in_desc_name, int num_out_vars,
	   HOST_REF * out_vars, const char *out_desc_name)
{
  int i;
  int counter = 0;
  char *temp = escape_string (stmt, length, &counter);

  emit_start (1);

  if (num_in_vars > 0)
    {
      for (i = 0; i < num_in_vars; i++)
	{
	  emit_put_db_value (&in_vars[i]);
	}
    }
  else if (in_desc_name != NULL)
    {
      fprintf (FP, "  uci_put_descriptor(%s);%s", in_desc_name, NL);
      emit_line_directive ();
    }

  /* emit uci function */
  if (pp_enable_uci_trace)
    {
      fprintf (FP,
	       "fprintf(stderr, "
	       "\"uci_static(%%d, \\\"%%s\\\", %%ld, %%d)\\n\", %d, \"",
	       (repetitive) ? REPETITIVE_SERIAL_NO : -1);
      tr_print_n_string (temp, length + counter);
      fprintf (FP, "\", %ld, %d); %s", (long) length,
	       (num_out_vars <= 0) ? -1 : num_out_vars, NL);
    }
  fprintf (FP, "  uci_static(%d, \"",
	   (repetitive) ? REPETITIVE_SERIAL_NO : -1);
  tr_print_n_string (temp, length + counter);
  fprintf (FP, "\", %ld, %d);%s", (long) length,
	   (num_out_vars <= 0) ? -1 : num_out_vars, NL);
  emit_line_directive ();

  if (temp != NULL)
    {
      free_and_init (temp);
    }

  if (num_out_vars > 0)
    {
      for (i = 0; i < num_out_vars; i++)
	{
	  emit_get_db_value (-1, &out_vars[i]);
	}
    }
  else if (out_desc_name != NULL)
    {
      /* descriptor is given */
      fprintf (FP, "  uci_get_descriptor(-1, %s);%s", out_desc_name, NL);
      emit_line_directive ();
    }

  emit_end ();
}

/*
 * tr_print_n_string() - This function is intended to print a string which may
 *   contain one or more embedded NULL characters in it.  It will print
 *   strings with length > 75 on multiple lines, breaking the line at the
 *   next comma.
 * return : void
 * stmt(in) : statement to be printed
 * length(in) : length of statment to be printed
 */
static void
tr_print_n_string (char *stmt, int length)
{
  int ctr = 0;
  bool need_newline = false;

  if (length == 0 || stmt == NULL)
    {
      return;
    }

  for (ctr = 0; ctr < length; ctr++)
    {
      fputc (stmt[ctr], FP);
      need_newline = need_newline || (ctr && !(ctr % 75));
      if (need_newline && stmt[ctr] == ',')
	{
	  fputc ('"', FP);
	  fputc ('\n', FP);
	  emit_line_directive ();
	  fputc ('"', FP);
	  need_newline = false;
	}
    }
}

/*
 * tr_open_cs() - emits codes to open a cursor. if `stmt' is not NULL, 'stmt'
 *    will be regarded as it contains SELECT stmt, otherwise, 'stmt_no' will
 *    specify prepared stmt no. if 'num_in_vars' is positive, it is assumed
 *    that a user specifies host variables, if it is not positive and
 *    'desc_name' is not NULL, it is assumed that a user specifies
 *    a descriptor, if 'num_in_vars' is not positive and 'desc_name' is NULL,
 *    it is assumed that neither host variables nor descriptor is given.
 * 
 *    statement-coverage: OPEN statement
 * 
 * return : none
 * cs_no(in) : cursor number
 * stmt(in) : statement contents or prepared stmt name
 * stmt_no(in) : prepared stmt number
 * readonly(in) : flag for readonly cursor
 * num_in_vars(in) : # of input variables
 * in_vars(in) : array of input variable descriptions
 * desc_name(in) : descriptor name if given
 * 
 */
static void
tr_open_cs (int cs_no, const char *stmt, int length, int stmt_no,
	    bool readonly, int num_in_vars, HOST_REF * in_vars,
	    const char *desc_name)
{
  int i;
  int counter = 0;
  char *temp = escape_string (stmt, length, &counter);

  emit_start (1);

  if (num_in_vars > 0)
    {
      for (i = 0; i < num_in_vars; i++)
	{
	  emit_put_db_value (in_vars + i);
	}
    }
  else if (desc_name != NULL)
    {
      fprintf (FP, "  uci_put_descriptor(%s);%s", desc_name, NL);
      emit_line_directive ();
    }

  /* emits uci function */
  if (pp_enable_uci_trace)
    {
      fprintf (FP,
	       "fprintf(stderr,"
	       "\"uci_open_cs(%%d, \\\"%%s\\\", %%ld, %%d, %%d)\\n\", %d, ",
	       cs_no);
      if (stmt == NULL)
	{
	  fprintf (FP, "\"(char *)0\", ");
	}
      else
	{
	  fprintf (FP, "\"");
	  tr_print_n_string (temp, length + counter);
	  fprintf (FP, "\", ");
	}
      fprintf (FP, "%ld, %d, %d); %s", (long) length, stmt_no, readonly, NL);
    }
  fprintf (FP, "  uci_open_cs(%d, ", cs_no);
  if (stmt == NULL)
    {
      fprintf (FP, "(char *)0, ");
    }
  else
    {
      fprintf (FP, "\"");
      tr_print_n_string (temp, length + counter);
      fprintf (FP, "\", ");
    }

  if (temp != NULL)
    {
      free_and_init (temp);
    }

  fprintf (FP, "%ld, %d, %d);%s", (long) length, stmt_no, readonly, NL);
  emit_line_directive ();

  emit_end ();
}

/*
 * tr_fetch_cs() - emits codes to fetch a cursor. `cs_name' should be declared
 *    prior to call this function. if `num_out_vars' is positive, it is assumed
 *    that a user specifies host variables, if it is not positive and
 *    'desc_name' is not NULL, it is assumed that a user specifies
 *    a descriptor, if `num_out_vars' is not positive and `desc_name' is NULL,
 *    it is assumed that neither host variables nor descriptor is given
 *    (in most cases, it will cause run time warning).
 * 
 *    statement-coverage: FETCH statement
 * 
 * return : none
 * cs_no(in) : cursor number
 * num_out_vars(in) : # of output variables
 * out_vars(in) : array of output variable descriptions
 * desc_name(in) : descriptor name if given
 */
static void
tr_fetch_cs (int cs_no, int num_out_vars, HOST_REF * out_vars,
	     const char *desc_name)
{
  int i;

  emit_start (1);

  if (pp_enable_uci_trace)
    {
      if (num_out_vars > 0)
	{
	  fprintf (FP, "fprintf(stderr, \"uci_fetch_cs(%d, %d)\\n\"); %s",
		   cs_no, num_out_vars, NL);
	}
      else if (desc_name != NULL)
	{
	  fprintf (FP,
		   "fprintf(stderr, "
		   "\"uci_fetch_cs(%d, (%s)->sqldesc)\\n\"); %s",
		   cs_no, desc_name, NL);
	}
      else
	{
	  fprintf (FP,
		   "fprintf(stderr, \"uci_fetch_cs(%%d, -1)\\n\", %d); %s",
		   cs_no, NL);
	}
    }
  if (num_out_vars > 0)
    {
      /* host variable is given */
      fprintf (FP, "  uci_fetch_cs(%d, %d);%s", cs_no, num_out_vars, NL);
      emit_line_directive ();
      for (i = 0; i < num_out_vars; i++)
	{
	  emit_get_db_value (cs_no, out_vars + i);
	}
    }
  else if (desc_name != NULL)
    {
      /* descriptor is given */
      fprintf (FP, "  uci_fetch_cs(%d, (%s)->sqldesc);%s", cs_no, desc_name,
	       NL);
      emit_line_directive ();
      fprintf (FP, "  uci_get_descriptor(%d, %s);%s", cs_no, desc_name, NL);
      emit_line_directive ();
    }
  else
    {
      /* neither host var nor desc is given */
      fprintf (FP, "  uci_fetch_cs(%d, -1);%s", cs_no, NL);
      emit_line_directive ();
    }

  emit_end ();
}

/*
 * tr_update_cs() - emits the codes to update the current object of the
 *    cursor using the given set expression.
 * 
 *    statement-coverage: [REPEAT] UPDATE ...WHERE CURRENT OF cs_name
 * 
 * return : none
 * cs_no(in) : cursor id
 * text(in) : update statement
 * length(in) : length of text
 * repetitive(in) :
 * num_in_vars(in) : # of input host variables
 * in_vars(in) : array of input host variables
 * 
 * note : 'text' should NOT include WHERE CURRENT OF cs_name
 */
static void
tr_update_cs (int cs_no, const char *text, int length,
	      bool repetitive, int num_in_vars, HOST_REF * in_vars)
{
  int i;

  emit_start (1);

  /* first emit the code to push the current object of the cursor */
  fprintf (FP, "  uci_psh_curr_csr_oid(%d);%s", cs_no, NL);
  emit_line_directive ();

  if (num_in_vars > 0)
    {
      /* put variables in binding table */
      for (i = 0; i < num_in_vars; i++)
	{
	  emit_put_db_value (&in_vars[i]);
	}
    }

  /* emit uci function */
  if (pp_enable_uci_trace)
    {
      fprintf (FP,
	       "fprintf(stderr, "
	       "\"uci_static(%%d, \\\"%%s\\\", %%ld, 0)\\n\", %d, \"",
	       (repetitive) ? REPETITIVE_SERIAL_NO : -1);
      tr_print_n_string ((char *) text, length);
      fprintf (FP, "\", %ld); %s", (long) length, NL);
    }
  fprintf (FP, "  uci_static(%d,\"",
	   (repetitive) ? REPETITIVE_SERIAL_NO : -1);
  tr_print_n_string ((char *) text, length);
  fprintf (FP, "\", %ld, 0);%s", (long) length, NL);
  emit_line_directive ();

  emit_end ();
}

/*
 * tr_delete_cs() - emit uci function call for delete cursor statement
 * statement-coverage: DELETE FROM class_name WHERE CURRENT OF cs_name
 * return : void
 * cs_no(in) : cursor number
 */
static void
tr_delete_cs (int cs_no)
{
  emit_start (1);

  /* emit uci function */
  if (pp_enable_uci_trace)
    {
      fprintf (FP, "fprintf(stderr, \"uci_delete_cs(%d)\\n\"); %s",
	       cs_no, NL);
    }
  fprintf (FP, "  uci_delete_cs(%d);%s", cs_no, NL);
  emit_line_directive ();

  emit_end ();
}

/*
 * tr_close_cs() - emits the codes to close the specified cursor
 * statement-coverage: CLOSE cursor_name
 * return : void
 * cs_no(in) : cursor number
 */
static void
tr_close_cs (int cs_no)
{
  emit_start (1);

  if (pp_enable_uci_trace)
    {
      fprintf (FP, "fprintf(stderr, \"uci_close_cs(%d)\\n\"); %s", cs_no, NL);
    }
  fprintf (FP, "  uci_close_cs(%d);%s", cs_no, NL);
  emit_line_directive ();

  emit_end ();
}

/*
 * tr_prepare_esql() - emits codes to prepare the given statement.
 * statement-coverage: PREPARE statement
 * return : void
 * stmt_no(in) : statement number to be prepared
 * stmt(in) : dynamic statement
 */
static void
tr_prepare_esql (int stmt_no, HOST_REF * stmt)
{
  const char *buf_str;
  const char *bufsize_str;

  emit_start (1);

  /*
   * The parser should have guaranteed that only a HOST_REF that smells
   * like some sort of pseudo-string can get in here.  Other types should
   * be impossible.
   */
  get_quasi_string (stmt, &buf_str, &bufsize_str);

  /* emit uci function */
  if (pp_enable_uci_trace)
    {
      fprintf (FP,
	       "fprintf(stderr, "
	       "\"uci_prepare(%%d, \\\"%%s\\\", %%d)\\n\", %d, %s, %s); %s",
	       stmt_no, buf_str, bufsize_str, NL);
    }
  fprintf (FP, "  uci_prepare(%d, %s, %s);%s",
	   stmt_no, buf_str, bufsize_str, NL);
  emit_line_directive ();

  emit_end ();
}

/*
 * tr_describe() - emits the codes to describe the result attributes of
 *    the prepared statement into the given descriptor.
 * statement-coverage: DESCRIBE statement
 * return : void
 * stmt_no(in) : number of prepared statement
 * desc_name(in) : name of descriptor variable
 */
static void
tr_describe (int stmt_no, const char *desc_name)
{
  emit_start (1);
  fprintf (FP, "  uci_describe(%d, %s);%s", stmt_no, desc_name, NL);
  emit_line_directive ();
  emit_end ();
}

/*
 * tr_execute() - emits the codes to execute the prepared dynamic stmt using
 *    host variables (if `num_in_vars' is positive) or descriptor
 *    (if 'num_in_vars' is not positive and 'out_desc_name' is not NULL).
 *    if 'num_in_vars' is not positive and 'out_desc_name' is NULL, it is
 *    assumed that there is not input values. output variables are also treated
 *    as the same way.
 * statement-coverage: EXECUTE statement
 * return : void
 * stmt_no(in) : no of prepared statement
 * num_in_vars(in) : # of input host variables
 * in_vars(in) : array of input variable descriptions
 * in_desc_name(in) : name of input descriptor variable
 * num_out_vars(in) : # of output host variables
 * out_vars(in) : array of output variable descriptions
 * out_desc_name(in) : name of output descriptor variable
 */
static void
tr_execute (int stmt_no, int num_in_vars, HOST_REF * in_vars,
	    const char *in_desc_name, int num_out_vars,
	    HOST_REF * out_vars, const char *out_desc_name)
{
  int i;

  emit_start (1);

  /* emit code to put var infor */
  if (num_in_vars > 0)
    {
      for (i = 0; i < num_in_vars; i++)
	{
	  emit_put_db_value (in_vars + i);
	}
    }
  else if (in_desc_name != NULL)
    {
      /* descriptor is given */
      fprintf (FP, "  uci_put_descriptor(%s);%s", in_desc_name, NL);
      emit_line_directive ();
    }

  /* emit uci function */
  if (pp_enable_uci_trace)
    {
      if (num_out_vars > 0)
	{
	  fprintf (FP, "fprintf(stderr, \"uci_execute(%d, %d)\\n\"); %s",
		   stmt_no, num_out_vars, NL);
	}
      else if (out_desc_name != NULL)
	{
	  fprintf (FP,
		   "fprintf(stderr,"
		   " \"uci_execute(%d, (%s)->sqldesc)\\n\"); %s",
		   stmt_no, out_desc_name, NL);
	}
      else
	{
	  fprintf (FP, "fprintf(stderr, \"uci_execute(%d, -1)\\n\"); %s",
		   stmt_no, NL);
	}
    }

  if (num_out_vars > 0)
    {
      /* output host variables are given */
      fprintf (FP, "  uci_execute(%d, %d);%s", stmt_no, num_out_vars, NL);
      emit_line_directive ();
      for (i = 0; i < num_out_vars; i++)
	{
	  emit_get_db_value (-1, out_vars + i);
	}
    }
  else if (out_desc_name != NULL)
    {
      /* descriptor is given */
      fprintf (FP, "  uci_execute(%d, (%s)->sqldesc);%s", stmt_no,
	       out_desc_name, NL);
      emit_line_directive ();
      fprintf (FP, "  uci_get_descriptor(-1, %s);%s", out_desc_name, NL);
      emit_line_directive ();
    }
  else
    {
      fprintf (FP, "  uci_execute(%d, -1);%s", stmt_no, NL);
      emit_line_directive ();
    }

  emit_end ();
}

/*
 * tr_execute_immediate() - emits the codes to execute the given statement
 *    immediately.
 * statement-coverage: EXECUTE IMMEDIATE statement
 * return : void
 * stmt(in) : pointer to symbol tab entry describing the stmt to be executed
 *    immediately.
 */
static void
tr_execute_immediate (HOST_REF * stmt)
{
  const char *buf_str;
  const char *bufsize_str;

  emit_start (1);
  get_quasi_string (stmt, &buf_str, &bufsize_str);
  if (pp_enable_uci_trace)
    {
      fprintf (FP,
	       "fprintf(stderr, "
	       "\"uci_execute_immediate(\\\"%%s\\\", %%d)\\n\", %s, %s); %s",
	       buf_str, bufsize_str, NL);
    }
  fprintf (FP, "  uci_execute_immediate(%s, %s);%s",
	   buf_str, bufsize_str, NL);
  emit_line_directive ();
  emit_end ();
}

/*
 * tr_object_describe() - emits the codes to describe the given object on the
 *    given attributes
 * statement-coverage: DESCRIBE OBJECT :obj ON attr INTO :desc
 * return : void
 * obj(in) : object id host variable reference
 * num_attrs(in) : # of attribute names
 * attr_names(in) : array of attribute name to be described
 * desc_name(in) : SQLDA name to hold the description
 * 
 * note: Caller should make sure the 'obj' refers to C_TYPE_OBJECTID.
 */
static void
tr_object_describe (HOST_REF * obj, int num_attrs, const char **attr_names,
		    const char *desc_name)
{
  int i;

  fprintf (FP, "{%s", NL);
  emit_line_directive ();

  if (num_attrs > 0)
    {
#if defined(PRODUCE_ANSI_CODE)
      fprintf (FP, "  static const char *uci_attr_names[%d] = {", num_attrs);
#else
      fprintf (FP, "  static char *uci_attr_names[%d] = {", num_attrs);
#endif
      for (i = 0; i < num_attrs; i++)
	{
	  fprintf (FP, "%s\"%s\"", (i > 0 ? "," : ""), attr_names[i]);
	}
      fprintf (FP, "};%s", NL);
      emit_line_directive ();
    }

  emit_start (0);
  fprintf (FP, "  uci_object_describe(%s, %d, uci_attr_names, %s);%s",
	   pp_get_expr (obj), num_attrs, desc_name, NL);
  emit_line_directive ();

  emit_end ();
}

/*
 * tr_object_fetch() - emits the codes to fetch the specified attribute values
 *    of the given object into host variables or descriptor. If `num_out_vars'
 *    is positive, it is assumed that host variables are given in `out_vars'.
 *    Otherwise, if 'desc_name' is not NULL, it is assumed that a desc is
 *    given. Otherwise, no variable is assumed to be given to hold output.
 * statement-coverage: FETCH OBJECT :obj ON attr ...
 * return : void
 * obj(in) : object id host variable reference
 * num_attrs(in) : # of attribute names
 * attr_names(in) : array of attribute name to be described
 * num_out_vars(in) : # of output host variables
 * out_vars(in) : array of output host variables
 * desc_name(in) : SQLDA name if given
 * 
 * note : Caller should make sure the `obj' refers to C_TYPE_OBJECTID.
 */
static void
tr_object_fetch (HOST_REF * obj, int num_attrs, const char **attr_names,
		 int num_out_vars, HOST_REF * out_vars, const char *desc_name)
{
  int i;

  fprintf (FP, "{%s", NL);
  emit_line_directive ();

  if (num_attrs > 0)
    {
#if defined(PRODUCE_ANSI_CODE)
      fprintf (FP, "  static const char *uci_attr_names[%d] = {", num_attrs);
#else
      fprintf (FP, "  static char *uci_attr_names[%d] = {", num_attrs);
#endif
      for (i = 0; i < num_attrs; i++)
	{
	  fprintf (FP, "%s\"%s\"", (i > 0 ? "," : ""), attr_names[i]);
	}
      fprintf (FP, "};%s", NL);
      emit_line_directive ();
    }

  emit_start (0);		/* Leading brace already emitted */

  if (pp_enable_uci_trace)
    {
      if (num_out_vars > 0)
	{
	  fprintf (FP,
		   "fprintf(stderr, "
		   "\"uci_object_fetch(%%s, %%d, uci_attr_names, %%d)\\n\""
		   ", \"%s\", %d, %d); %s",
		   pp_get_expr (obj), num_attrs, num_out_vars, NL);
	}
      else if (desc_name != NULL)
	{
	  fprintf (FP,
		   "fprintf(stderr, "
		   "\"uci_object_fetch(%%s, %%d, uci_attr_names, %%s)\\n\""
		   ", \"%s\", %d, \"(%s)->sqldesc\"); %s",
		   pp_get_expr (obj), num_attrs, desc_name, NL);
	}
      else
	{
	  fprintf (FP,
		   "fprintf(stderr, "
		   "\"uci_object_fetch(%%s, %%d, uci_attr_names, -1)\\n\""
		   ", \"%s\", %d); %s", pp_get_expr (obj), num_attrs, NL);
	}
    }
  if (num_out_vars > 0)
    {
      /* output host variables are given */
      fprintf (FP, "  uci_object_fetch(%s, %d, uci_attr_names, %d);%s",
	       pp_get_expr (obj), num_attrs, num_out_vars, NL);
      emit_line_directive ();
      for (i = 0; i < num_out_vars; i++)
	{
	  emit_get_db_value (-1, out_vars + i);
	}
    }
  else if (desc_name != NULL)
    {
      /* descriptor is given */
      fprintf (FP,
	       "  uci_object_fetch(%s, %d, uci_attr_names, (%s)->sqldesc);%s",
	       pp_get_expr (obj), num_attrs, desc_name, NL);
      emit_line_directive ();
      fprintf (FP, "  uci_get_descriptor(-1, %s);%s", desc_name, NL);
      emit_line_directive ();
    }
  else
    {
      fprintf (FP, "  uci_object_fetch(%s, %d, uci_attr_names, -1);%s",
	       pp_get_expr (obj), num_attrs, NL);
      emit_line_directive ();
    }

  emit_end ();
}

/*
 * tr_object_update() - emits the codes to update the object specified by 'obj'
 *    using the given set expression.
 * statement-coverage: [REPEAT] UPDATE OBJECT :obj SET ...
 * return : void
 * set_expr(in) : string to have SET-clause
 * repetitive(in) : true if repetitive
 * num_in_vars(in) : # of input host variables
 * in_vars(in) : array of pointers to input host variables
 * 
 * note : - Caller should make sure the `obj' refers to	C_TYPE_OBJECTID.
 *        - 'set_expr' should NOT include 'SET' keyword itself.
 */
static void
tr_object_update (const char *set_expr, int length, bool repetitive,
		  int num_in_vars, HOST_REF * in_vars)
{
  int i;
  int counter = 0;
  char *temp = escape_string (set_expr, length, &counter);

  emit_start (1);

  /* put variables in binding table */
  for (i = 0; i < num_in_vars; i++)
    {
      emit_put_db_value (&in_vars[i]);
    }

  /* emit uci function */
  if (pp_enable_uci_trace)
    {
      fprintf (FP,
	       "fprintf(stderr, \"uci_static(%%d, \\\"%%s\\\", %%ld, 0)\\n\""
	       ", %d, \"", (repetitive) ? REPETITIVE_SERIAL_NO : -1);
      tr_print_n_string (temp, length + counter);
      fprintf (FP, "\", %ld); %s", (long) length, NL);
    }

  fprintf (FP, "  uci_static(%d,\"",
	   (repetitive) ? REPETITIVE_SERIAL_NO : -1);
  tr_print_n_string (temp, length + counter);
  fprintf (FP, "\",%ld, 0);%s", (long) length, NL);
  emit_line_directive ();

  if (temp != NULL)
    {
      free_and_init (temp);
    }

  emit_end ();
}

/*
 * tr_whenever() - set action on specific condition. if 'condition' is
 *    NOT_FOUND and 'action' is STOP, there is no effect.
 * return : void
 * condition(in) : whenever condition
 * action(in) : action on the condition
 * name(in) :label or procedure name
 */
static void
tr_whenever (WHEN_CONDITION condition, WHEN_ACTION action, const char *name)
{
  WHEN_DESC *wd;

  switch (condition)
    {
    case SQLWARNING:
      wd = &on_warning;
      break;
    case SQLERROR:
      wd = &on_error;
      break;
    case NOT_FOUND:
      {
	if (action == STOP)
	  {
	    return;
	  }
	wd = &on_not_found;
      }
      break;
    default:
      return;
    }

  wd->action = action;
  wd->name = name;
}

/*
 * tr_set_out_stream() - set the current ouput file stream to the given stream.
 * return : void
 * out_stream(in) : output file stream
 */
static void
tr_set_out_stream (FILE * out_stream)
{
  c_out_stream = out_stream;
}

/*
 * tr_set_line_terminator() - set the line terminator string
 * return/side-effects: none
 * terminator(in) : string used to terminate each emitted line
 */
static void
tr_set_line_terminator (const char *terminator)
{
  NL = terminator;
}

/*
 * emit_put_db_value() - emit uci_put_...() function according to the type
 *    of the given host variable information.
 * return : void
 * host(in) : symbol table entry for host variable
 */
static void
emit_put_db_value (HOST_REF * host)
{
  C_TYPE type;
  char *type_str = NULL;
  const char *prec_str = NULL;
  const char *scale_str = NULL;
  const char *ctype_str = NULL;
  const char *buf_str = NULL;
  const char *bufsize_str = NULL;
  const char *fmt = "db_col_type(%s)";

  /*
   * Print the indicator expression first, so we can be done with it.
   * pp_get_ind_addr_expr() indirectly shares a scratch buffer with
   * pp_get_addr_expr(), so we need to print the indicator string out
   * before we try to do anything else, lest we inadvertently clobber
   * it.
   *
   * pp_get_input_size() uses a different buffer than either
   * pp_get_expr() or pp_get_addr_expr(), so we it's ok just to hold on
   * to the pointer for that result.
   */
  fprintf (FP, "  uci_put_value(%s, ", pp_get_ind_addr_expr (host));

  prec_str = "0";
  scale_str = "0";
  bufsize_str = "0";

  type = pp_get_type (host);
  switch (type)
    {
    case C_TYPE_SHORT:
      {
	type_str = (char *) "DB_TYPE_SHORT";
	ctype_str = "DB_TYPE_C_SHORT";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_INTEGER:
      {
	type_str = (char *) "DB_TYPE_INTEGER";
	ctype_str = "DB_TYPE_C_INT";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_LONG:
      {
	type_str = (char *) "DB_TYPE_INTEGER";
	ctype_str = "DB_TYPE_C_LONG";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_FLOAT:
      {
	type_str = (char *) "DB_TYPE_FLOAT";
	ctype_str = "DB_TYPE_C_FLOAT";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_DOUBLE:
      {
	type_str = (char *) "DB_TYPE_DOUBLE";
	ctype_str = "DB_TYPE_C_DOUBLE";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_VARCHAR:
      {
	type_str = (char *) "DB_TYPE_VARCHAR";
	ctype_str = "DB_TYPE_C_CHAR";
	prec_str = pp_get_precision (host);
	buf_str = pp_get_addr_expr (host);
	if (pp_disable_varchar_length)
	  {
	    bufsize_str = pp_get_input_size (host);
	  }
      }
      break;

    case C_TYPE_CHAR_ARRAY:
    case C_TYPE_CHAR_POINTER:
      {
	type_str = (char *) "DB_TYPE_CHAR";
	ctype_str = "DB_TYPE_C_CHAR";
	prec_str = pp_get_precision (host);
	buf_str = pp_get_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_NCHAR:
      {
	type_str = (char *) "DB_TYPE_NCHAR";
	ctype_str = "DB_TYPE_C_NCHAR";
	prec_str = pp_get_precision (host);
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_VARNCHAR:
      {
	type_str = (char *) "DB_TYPE_VARNCHAR";
	ctype_str = "DB_TYPE_C_NCHAR";
	prec_str = pp_get_precision (host);
	buf_str = pp_get_addr_expr (host);
	if (pp_disable_varchar_length)
	  {
	    bufsize_str = pp_get_input_size (host);
	  }
      }
      break;

    case C_TYPE_BIT:
      {
	type_str = (char *) "DB_TYPE_BIT";
	ctype_str = "DB_TYPE_C_BIT";
	prec_str = pp_get_precision (host);
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_VARBIT:
      {
	type_str = (char *) "DB_TYPE_VARBIT";
	ctype_str = "DB_TYPE_C_BIT";
	prec_str = pp_get_precision (host);
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_BASICSET:
      {
	type_str = (char *) "DB_TYPE_SET";
	ctype_str = "DB_TYPE_C_SET";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_MULTISET:
      {
	type_str = (char *) "DB_TYPE_MULTISET";
	ctype_str = "DB_TYPE_C_SET";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_SEQUENCE:
      {
	type_str = (char *) "DB_TYPE_SEQUENCE";
	ctype_str = "DB_TYPE_C_SET";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_COLLECTION:
      {
	type_str =
	  (char *) pp_malloc (sizeof (fmt) + strlen (pp_get_expr (host)));
	sprintf (type_str, fmt, pp_get_expr (host));
	ctype_str = "DB_TYPE_C_SET";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_TIME:
      {
	type_str = (char *) "DB_TYPE_TIME";
	ctype_str = "DB_TYPE_C_TIME";
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_TIMESTAMP:
      {
	type_str = (char *) "DB_TYPE_TIMESTAMP";
	ctype_str = "DB_TYPE_C_TIMESTAMP";
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_DATE:
      {
	type_str = (char *) "DB_TYPE_DATE";
	ctype_str = "DB_TYPE_C_DATE";
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_MONETARY:
      {
	type_str = (char *) "DB_TYPE_MONETARY";
	ctype_str = "DB_TYPE_C_MONETARY";
	buf_str = pp_get_addr_expr (host);
	bufsize_str = pp_get_input_size (host);
      }
      break;

    case C_TYPE_OBJECTID:
      {
	type_str = (char *) "DB_TYPE_OBJECT";
	ctype_str = "DB_TYPE_C_OBJECT";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_DB_VALUE:
      {
	type_str = (char *) "DB_TYPE_DB_VALUE";
	ctype_str = "0";
	buf_str = pp_get_addr_expr (host);
      }
      break;

    case C_TYPE_STRING_CONST:
    case C_TYPE_SQLDA:
    case NUM_C_TYPES:
      {
	/*
	 * These cases should be impossible.
	 */
	yyverror (pp_get_msg (EX_TRANS_SET, MSG_BAD_CASE),
		  "emit_put_db_value");
	type_str = (char *) "DB_TYPE_UNKNOWN";
	ctype_str = "0";
	buf_str = "NULL";
      }
      break;
    }

  fprintf (FP, "%s, %s, %s, %s, %s, %s);%s",
	   type_str,
	   prec_str, scale_str, ctype_str, buf_str, bufsize_str, NL);
  emit_line_directive ();

  if (type == C_TYPE_COLLECTION)
    {
      free ((void *) type_str);
    }
}

/*
 * emit_get_db_value() - emit uci_get_db_value() function according to
 *    the type of the given host variable information.
 * return : void
 * cs_no(in) : cursor no to identify result
 * host(in) : symbol table entry for host variable
 */
static void
emit_get_db_value (int cs_no, HOST_REF * host)
{
  C_TYPE ctype;

  ctype = pp_get_type (host);

  if (ctype == C_TYPE_DB_VALUE)
    {
      fprintf (FP, "  uci_get_db_value(%d, %s);%s",
	       cs_no, pp_get_addr_expr (host), NL);
      emit_line_directive ();
      return;
    }
  /*
   * Since the various calls to pp_get_<whatever> tend to share common
   * scratch areas, putting all of them in one giant fprintf() will
   * create havoc.  It's safer to sequence the calls through separate
   * calls to fprintf().
   */
  ctype = pp_get_type (host);
  fprintf (FP, "  uci_get_value(%d, %s, ",
	   cs_no, pp_get_ind_addr_expr (host));
  fprintf (FP, "(void *)(%s), ", pp_get_addr_expr (host));
  fprintf (FP, "%s, ", c_type_to_db_type_c[ctype]);
  fprintf (FP, "(int)(%s), ", pp_get_output_size (host));
  if (ctype == C_TYPE_VARCHAR || ctype == C_TYPE_VARNCHAR
      || ctype == C_TYPE_VARBIT)
    {
      fprintf (FP, "&%s", pp_get_input_size (host));
    }
  else
    {
      fprintf (FP, "NULL");
    }
  fprintf (FP, ");%s", NL);
  emit_line_directive ();
}

/*
 * emit_whenever() - emit codes to process whenever action. if more than
 *    condition is true, action is occurred by the following order:
 *            NOT_FOUND > SQLERROR > SQLWARNING.
 * return : void
 */
static void
emit_whenever (void)
{
  /* for not_found */
  if (on_not_found.action != CONTINUE)
    {
      fprintf (FP, "  if(sqlca.sqlcode == %s) ", NOT_FOUND_MACRO_NAME);
      switch (on_not_found.action)
	{
	case GOTO:
	  {
	    fprintf (FP, "goto %s;%s", on_not_found.name, NL);
	    emit_line_directive ();
	  }
	  break;
	case CALL:
	  {
	    fprintf (FP, "%s();%s", on_not_found.name, NL);
	    emit_line_directive ();
	  }
	  break;
	default:
	  break;
	}
    }

  /* for errors */
  if (on_error.action != CONTINUE)
    {
      fprintf (FP, "  if(sqlca.sqlcode < 0) ");
      switch (on_error.action)
	{
	case STOP:
	  {
	    if (pp_enable_uci_trace)
	      {
		fprintf (FP, "fprintf(stderr, \"uci_stop()\\n\"); %s", NL);
	      }
	    fprintf (FP, "uci_stop();%s", NL);
	    emit_line_directive ();
	  }
	  break;
	case GOTO:
	  {
	    fprintf (FP, "goto %s;%s", on_error.name, NL);
	    emit_line_directive ();
	  }
	  break;
	case CALL:
	  {
	    fprintf (FP, "%s();%s", on_error.name, NL);
	    emit_line_directive ();
	  }
	  break;
	default:
	  break;
	}
    }

  /* for warnings */
  if (on_warning.action != CONTINUE)
    {
      fprintf (FP, "  if(sqlca.sqlwarn.sqlwarn0 == %s) ",
	       WARN_CHAR_MACRO_NAME);
      switch (on_warning.action)
	{
	case STOP:
	  {
	    if (pp_enable_uci_trace)
	      {
		fprintf (FP, "fprintf(stderr, \"uci_stop()\\n\"); %s", NL);
	      }
	    fprintf (FP, "uci_stop();%s", NL);
	    emit_line_directive ();
	  }
	  break;
	case GOTO:
	  {
	    fprintf (FP, "goto %s;%s", on_warning.name, NL);
	    emit_line_directive ();
	  }
	  break;
	case CALL:
	  {
	    fprintf (FP, "%s();%s", on_warning.name, NL);
	    emit_line_directive ();
	  }
	  break;
	default:
	  break;
	}
    }
}

/*
 * escape_string() - copies the input string and escapes ("\") quotes, single
 *    quotes, null, and backslashes. 
 * return : char *
 * in_str(in) :
 * length(in) :
 * counter(out) :
 * 
 * note : calling routine must free the string when through.
 */
static char *
escape_string (const char *in_str, int length, int *counter)
{
  char *out_str = NULL;
  char *temp;
  char *end = (char *) in_str + length - 1;
  int add_count;
  char temp_buffer[1024];


  if (in_str == NULL)
    {
      return NULL;
    }

  add_count = 0;
  /*
   * Need four times the size of the input string  since this is the 
   * max size the out string could be.
   */
  if (length * 4 + 1 <= 1024)
    {
      out_str = temp_buffer;
    }
  else
    {
      out_str = (char *) pp_malloc (length * 4 + 1);
    }
  temp = out_str;

  while (in_str <= end)
    {
      /*
       * If input char is double quote,single quote, escape char, newline
       * of form feed, precede it by an escape character in the emitted
       * string
       */
      if ((*in_str == '"') ||
	  (*in_str == '\'') ||
	  (*in_str == '\\') || (*in_str == '\n') || (*in_str == '\f'))
	{

	  *temp = '\\';
	  temp++;
	  add_count++;
	}

      /*
       * If input char is the null character, then replace it with
       * escape char 000 (octal 0) in the emitted string so that
       * the 'C' compilers will not choke on an unescaped null char
       * in string literals.
       */
      if (*in_str == '\000')
	{
	  *temp = '\\';
	  temp++;

	  *temp = '0';
	  temp++;

	  *temp = '0';
	  temp++;

	  *temp = '0';
	  temp++;

	  /* Increment string addition counter */
	  add_count += 3;

	  /* Skip over (now replaced) null char in input string. */
	  in_str++;
	}
      else
	{
	  /* 
	   * Emit current char.
	   * Increment input string pointer by 1.
	   * Increment output string pointer by 1.
	   */
	  *temp = *in_str;
	  temp++;
	  in_str++;
	}
    }

  *temp = '\0';
  if (out_str == temp_buffer)
    {
      int sz = temp - out_str + 1;

      out_str = pp_malloc (sz);
      memcpy (out_str, temp_buffer, sz);
    }
  *counter = add_count;

  return out_str;
}
