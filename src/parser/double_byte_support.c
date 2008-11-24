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
 * double_byte_support.c - parser supporting functions for double byte character set
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include "parser.h"
#include "language_support.h"

#define MAX_UNGET_SIZE 16
#define WSPACE_CHAR 0xa1a1

#define DBCS_UNGET_RETURN(X, Y) \
  do { *dbcs_Unget_buf++ = (X); return(Y); } while(0)

#define DBCS_STATUS_RETURN(X, Y) \
  do { dbcs_Input_status = X; return(Y); } while(0)

#define DBCS_STATUS_UNGET_RETURN(X,Y,Z) \
  do{ dbcs_Input_status = X; \
     *dbcs_Unget_buf++ = (Y); return(Z); } while(0)

#define DBCS_NEXT_CHAR(p)	((p->next_byte)(p))

typedef enum
{ CSQL_,			/* In CSQL language */
  CSQL_BEGIN_,			/* Just before CSQL */
  C_COMMENT_,			/* Within C comment */
  C_COMMENT_BEGIN_,		/* Just before C comment */
  SQL_COMMENT_,			/* -- sql comment */
  SQL_COMMENT_BEGIN_,		/* just before sql comment */
  CPP_COMMENT_,			/* C++ comment */
  CPP_COMMENT_BEGIN_,		/* just before C++ comment */
  DQS_,				/* Double quoted string */
  DQS_OCTAL_,			/* Octal escape in DQS */
  DQS_HEXA_,			/* Hexa decimal escape in DQS */
  DQS_HEXA_BEGIN_,		/* */
  DQS_HEXA_BEGIN_2,		/* */
  DQS_DECIMAL_,			/* Decimal escape in DQS */
  DQS_TRANSPARENT_,		/* DQS, but no check */
  DQS_TRANSPARENT_2,		/* has to skip two char */
  SQS_,				/* Single quoted string */
  SQS_TRANSPARENT_		/* SQS, but no check */
} DBCS_INPUT_STATUS;

static int DBCS_UNGET_BUF[MAX_UNGET_SIZE];
static int *dbcs_Unget_buf;
static unsigned int dbcs_Latter_byte;
static int dbcs_Latter_flag;
static DBCS_INPUT_STATUS dbcs_Input_status;

static int dbcs_get_next_w_char (PARSER_CONTEXT * parser);
static int dbcs_convert_w_char (int input_char);
static int dbcs_get_next_token_wchar (PARSER_CONTEXT * parser);
static int dbcs_process_csql (PARSER_CONTEXT * parser, int converted_char);
static int dbcs_process_double_quote_string (PARSER_CONTEXT * parser,
					     int input_char,
					     int converted_char);
static int dbcs_process_double_quote_string_octal (PARSER_CONTEXT * parser,
						   int input_char,
						   int converted_char);
static int dbcs_process_double_quote_string_hexa (PARSER_CONTEXT * parser,
						  int input_char,
						  int converted_char);
static int dbcs_process_double_quote_string_decimal (PARSER_CONTEXT * parser,
						     int input_char,
						     int converted_char);
static int dbcs_process_single_quote_string (PARSER_CONTEXT * parser,
					     int input_char,
					     int converted_char);
static int dbcs_process_c_comment (PARSER_CONTEXT * parser, int input_char,
				   int converted_char);

/*
 * dbcs_start_input () -
 *   return: none
 */
void
dbcs_start_input (void)
{
  dbcs_Unget_buf = DBCS_UNGET_BUF;	/* Lead ahead buffer */
  dbcs_Input_status = CSQL_;	/* Scanning status   */
  dbcs_Latter_flag = 0;		/* Two byte code     */
}

/*
 * dbcs_get_next () - Read one byte
 *   return:
 *   parser(in):
 */
int
dbcs_get_next (PARSER_CONTEXT * parser)
{
  int input_char;

  if (dbcs_Latter_flag)
    {
      dbcs_Latter_flag = 0;
      input_char = dbcs_Latter_byte;
    }
  else
    {
      input_char = dbcs_get_next_token_wchar (parser);
      if (input_char != EOF)
	{
	  if (input_char & 0xff00)
	    {
	      dbcs_Latter_flag = 1;
	      dbcs_Latter_byte = input_char & 0x00ff;
	      input_char = ((input_char & 0xff00) >> 8);
	    }
	  else
	    {
	      input_char = (input_char & 0x00ff);
	    }
	}
    }

  return input_char;
}


/*
 * dbcs_get_next_token_wchar () -
 *   return:
 *   parser(in):
 */
static int
dbcs_get_next_token_wchar (PARSER_CONTEXT * parser)
{
  int converted_char;
  int input_char;

  if ((input_char = dbcs_get_next_w_char (parser)) == EOF)
    {
      return (EOF);
    }

  converted_char = dbcs_convert_w_char (input_char);


  switch (dbcs_Input_status)
    {
    case CSQL_:
      /* Scanning over CSQL part, not in comments or character string */
      return (dbcs_process_csql (parser, converted_char));

    case CSQL_BEGIN_:
      /* Typically, the last character of C-style comment */
      DBCS_STATUS_RETURN (CSQL_, converted_char);

    case C_COMMENT_BEGIN_:
      /* Scanning latter part of the beginning of C-style comment */
      DBCS_STATUS_RETURN (C_COMMENT_, converted_char);

    case SQL_COMMENT_BEGIN_:
      /* Scanning latter part of the beginning of SQL comment */
      DBCS_STATUS_RETURN (SQL_COMMENT_, converted_char);

    case CPP_COMMENT_BEGIN_:
      /* Scanning latter part of the C++ style comment */
      DBCS_STATUS_RETURN (CPP_COMMENT_, converted_char);

    case SQL_COMMENT_:
    case CPP_COMMENT_:
      /* Scanning C++ style or SQL comment. Because termination condition
       * is the same, we have common source lines */
      if (converted_char == '\n')
	{
	  DBCS_STATUS_RETURN (CSQL_, converted_char);
	}
      else
	{
	  return (input_char);
	}

    case C_COMMENT_:
      /* Scanning C comments. */
      return (dbcs_process_c_comment (parser, input_char, converted_char));

    case SQS_:
      /* Scanning single quote character strings */
      return (dbcs_process_single_quote_string (parser, input_char,
						converted_char));

    case SQS_TRANSPARENT_:
      /* This happenes in the latter character of contiguous back slash */
      DBCS_STATUS_RETURN (SQS_, input_char);

    case DQS_:
      /* Scanning double quote character strings */
      return (dbcs_process_double_quote_string (parser, input_char,
						converted_char));

    case DQS_TRANSPARENT_:
    case DQS_TRANSPARENT_2:
      /* This happenes in the latter characters of escape sequences
       * in double quote string */
      DBCS_STATUS_RETURN (DQS_, input_char);

    case DQS_OCTAL_:
      /* Scanning octal escape sequence in double quote string */
      return (dbcs_process_double_quote_string_octal (parser, input_char,
						      converted_char));

    case DQS_HEXA_:
      /* Scanning hexadecimal escape sequence in double quote string */
      return (dbcs_process_double_quote_string_hexa (parser, input_char,
						     converted_char));

    case DQS_HEXA_BEGIN_:
      /* Scanning third character of hexadecimal escape sequence in
       * double quote string, that is, x (\0x__) */
      DBCS_STATUS_RETURN (DQS_HEXA_, converted_char);

    case DQS_HEXA_BEGIN_2:
      /* Scanning second character of hexadecimal escape sequence in
       * double quote string, that is, 0 (\0x__) */
      DBCS_STATUS_RETURN (DQS_HEXA_BEGIN_, converted_char);

    case DQS_DECIMAL_:
      /* Scanning decimal escape sequence in double quote string */
      return (dbcs_process_double_quote_string_decimal (parser, input_char,
							converted_char));
    }

  return converted_char;
}


/*
 * dbcs_process_double_quote_string_hexa () - Scanning hexadecimal
 * 	representation of escape sequence in double-quote string
 *   return:
 *   parser(in):
 *   input_char(in):
 *   converted_char(in):
 */
static int
dbcs_process_double_quote_string_hexa (PARSER_CONTEXT * parser,
				       int input_char, int converted_char)
{
  if ((converted_char >= '0' && converted_char <= '9')
      || (converted_char >= 'A' && converted_char <= 'F')
      || (converted_char >= 'a' && converted_char <= 'f'))
    {
      return (converted_char);	/* Within the sequence */
    }
  else
    {
      /* Sequence terminated.  Back to double quote string */
      dbcs_Input_status = DQS_;
      return (dbcs_process_double_quote_string (parser, input_char,
						converted_char));
    }
}


/*
 * dbcs_process_double_quote_string_decimal () - Scanning decomal
 * 	representation of escape sequence in double-quote string
 *   return:
 *   parser(in):
 *   input_char(in):
 *   converted_char(in):
 */
static int
dbcs_process_double_quote_string_decimal (PARSER_CONTEXT * parser,
					  int input_char, int converted_char)
{
  if (converted_char >= '0' && converted_char <= '9')
    {
      return (converted_char);	/* Within the sequence */
    }
  else
    {
      /* Sequence terminated.  Back to double quote string */
      dbcs_Input_status = DQS_;
      return (dbcs_process_double_quote_string (parser, input_char,
						converted_char));
    }
}


/*
 * dbcs_process_double_quote_string_octal () - Scanning octal representation
 * 	of escape sequence in double-quote string
 *   return:
 *   parser(in):
 *   input_char(in):
 *   converted_char(in):
 */
static int
dbcs_process_double_quote_string_octal (PARSER_CONTEXT * parser,
					int input_char, int converted_char)
{
  if (converted_char >= '0' && converted_char <= '7')
    {
      return (converted_char);	/* Within the sequence */
    }
  else
    {
      /* Sequence terminated.  Back to double quote string */
      dbcs_Input_status = DQS_;
      return (dbcs_process_double_quote_string (parser, input_char,
						converted_char));
    }
}


/*
 * dbcs_process_double_quote_string () - Scan Double-quote string
 *   return:
 *   parser(in):
 *   input_char(in):
 *   converted_char(in):
 *
 * Note :
 * When double quote is found in double-quote string,
 * there are three possibilities:
 *   1) Termination of the string,
 *   2) Escape sequence for double quote itself.
 *   3) Error token.
 */
static int
dbcs_process_double_quote_string (PARSER_CONTEXT * parser, int input_char,
				  int converted_char)
{
  switch (converted_char)
    {
    case '"':
      {
	int c1, c1_c;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_STATUS_UNGET_RETURN (CSQL_, c1, converted_char);
	  }

	switch (c1_c = dbcs_convert_w_char (c1))
	  {
	  case '"':
	    /*
	     * Contiguous double quote.  Then we may scann double quote
	     * string later still.
	     */
	    if (input_char == c1)
	      {
		if (input_char == '"')
		  {
		    /*
		     * Single byte double quote. Then, latter half character
		     * has to be scanned next time.
		     */
		    DBCS_STATUS_UNGET_RETURN (DQS_TRANSPARENT_, c1,
					      input_char);
		  }
		else
		  {
		    /*
		     * Double byte double quote.  Main scanner does not require
		     * escape sequence to accept this.
		     */
		    return (input_char);
		  }
	      }
	    else
	      {
		/* Contiguous double quote */
		DBCS_STATUS_UNGET_RETURN (CSQL_, c1, converted_char);
	      }
	  default:
	    /*
	     * Double quote did not appear after the double quote.  Then
	     * terminate double quote string status and go back to SQL/X
	     * statement status.
	     */
	    DBCS_STATUS_UNGET_RETURN (CSQL_, c1, converted_char);
	  }
      }
    case '\\':
      {				/* back slash escapement */
	int c1, c1_c;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }

	switch (c1_c = dbcs_convert_w_char (c1))
	  {
	  case '\n':
	  case 'a':
	  case 'b':
	  case 'f':
	  case 'n':
	  case 'r':
	  case 't':
	  case 'v':
	    /* standard escapes to represent control characters */
	    DBCS_STATUS_UNGET_RETURN (DQS_TRANSPARENT_, c1_c, converted_char);

	  case '?':
	    if (c1 == '?')
	      {
		DBCS_STATUS_UNGET_RETURN (DQS_TRANSPARENT_, c1,
					  converted_char);
	      }
	    else
	      {
		return (c1);
	      }

	  case '\'':
	    if (c1 == '\'')
	      {
		DBCS_STATUS_UNGET_RETURN (DQS_TRANSPARENT_, c1_c,
					  converted_char);
	      }
	    else
	      {
		return (c1);
	      }

	  case '"':
	    if (c1 == '"')
	      {
		DBCS_STATUS_UNGET_RETURN (DQS_TRANSPARENT_, c1_c,
					  converted_char);
	      }
	    else
	      {
		return (c1);
	      }

	  case '0':
	    {			/* Hexadecimal or Octal escape */
	      int c2, c2_c;

	      if ((c2 = dbcs_get_next_w_char (parser)) == EOF)
		{
		  /* Unexpected end of sequence */
		  *dbcs_Unget_buf++ = c2;
		  DBCS_STATUS_UNGET_RETURN (DQS_OCTAL_, c1, converted_char);
		}

	      switch (c2_c = dbcs_convert_w_char (c2))
		{
		case 'x':
		  /* Begen hexadecimal representation */
		  *dbcs_Unget_buf++ = c2;
		  DBCS_STATUS_UNGET_RETURN (DQS_HEXA_BEGIN_2, c1,
					    converted_char);
		default:
		  /* Begin Octal representation */
		  *dbcs_Unget_buf++ = c2;
		  DBCS_STATUS_UNGET_RETURN (DQS_OCTAL_, c1, converted_char);
		}
	    }

	  default:
	    /* Decimal escape or Self escape to ASCII character */
	    if (c1_c >= 1 && c1_c <= 9)
	      {
		DBCS_STATUS_UNGET_RETURN (DQS_DECIMAL_, c1, converted_char);
	      }

	    if ((c1 & 0xff00) != 0)
	      {
		return (c1);
	      }
	    else
	      {
		DBCS_STATUS_UNGET_RETURN (DQS_TRANSPARENT_, c1,
					  converted_char);
	      }
	  }
    default:
	return (input_char);
      }
    }
}



/*
 * dbcs_process_single_quote_string () - Scanning single quote character string
 *   return:
 *   parser(in):
 *   input_char(in):
 *   converted_char(in):
 */
static int
dbcs_process_single_quote_string (PARSER_CONTEXT * parser, int input_char,
				  int converted_char)
{
  switch (converted_char)
    {
    case '\'':
      {				/* detect some escape sequences */
	int c1, c1_c;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_STATUS_UNGET_RETURN (CSQL_, c1, converted_char);
	  }

	switch (c1_c = dbcs_convert_w_char (c1))
	  {
	  case '\'':
	    /* Escape for ASCII-coded single quote or Termination of quote */
	    if (input_char == c1)
	      {
		if (input_char == '\'')
		  {
		    DBCS_STATUS_UNGET_RETURN (SQS_TRANSPARENT_, c1,
					      input_char);
		  }
		else
		  {
		    return (input_char);
		  }
	      }
	    else
	      {
		DBCS_STATUS_UNGET_RETURN (CSQL_, c1, converted_char);
	      }

	  default:
	    /* Terminate single quote string */
	    DBCS_STATUS_UNGET_RETURN (CSQL_, c1, converted_char);
	  }
      }

    case '\\':
      {
	/* In single quote string, backslash escape is used only to
	 * delimit lengthy string with new line. */
	int c1;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }

	if (c1 == '\n')
	  {
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }
	else
	  {
	    DBCS_UNGET_RETURN (c1, input_char);
	  }
      }

    default:
      /* Other character is simple character in string */
      return (input_char);
    }
}


/*
 * dbcs_process_c_comment () - Scanning C-style comment
 *   return:
 *   parser(in):
 *   input_char(in):
 *   converted_char(in):
 */
static int
dbcs_process_c_comment (PARSER_CONTEXT * parser, int input_char,
			int converted_char)
{
  switch (converted_char)
    {
    case '*':
      {
	int c1, c1_c;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }

	if ((c1_c = dbcs_convert_w_char (c1)) == '/')
	  {
	    /*
	     * Because this is the end of the C-comment, converted value is
	     * returned so that this is recognized by the parser correctly.
	     */
	    *dbcs_Unget_buf++ = c1_c;
	    DBCS_STATUS_RETURN (CSQL_BEGIN_, converted_char);
	  }

	/*
	 * Because this is a part of comment, input character is returned
	 * without conversion.
	 */
	DBCS_UNGET_RETURN (c1, input_char);
      }

    default:
      return (input_char);
    }
}


/*
 * dbcs_process_csql () - Scanning CSQL language body
 *   return:
 *   parser(in):
 *   converted_char(in):
 */
static int
dbcs_process_csql (PARSER_CONTEXT * parser, int converted_char)
{
  switch (converted_char)
    {
    case '"':			/* Start Double quoted string " ... " */
      dbcs_Input_status = DQS_;
      return (converted_char);

    case '\'':			/* Start Single quoted string ' ... ' */
      dbcs_Input_status = SQS_;
      return (converted_char);

    case '-':			/* Maybe start of SQL comment '-- ... ' */
      {
	int c1, c1_c;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }

	if ((c1_c = dbcs_convert_w_char (c1)) == '-')
	  {
	    dbcs_Input_status = SQL_COMMENT_BEGIN_;
	  }
	DBCS_UNGET_RETURN (c1, converted_char);
      }

    case '/':			/* Maybe C++ comment or C comment */
      {
	int c1, c1_c;

	if ((c1 = dbcs_get_next_w_char (parser)) == EOF)
	  {
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }

	switch (c1_c = dbcs_convert_w_char (c1))
	  {
	  case '/':		/* C++ comment */
	    dbcs_Input_status = CPP_COMMENT_BEGIN_;
	    DBCS_UNGET_RETURN (c1, converted_char);

	  case '*':		/* C comment */
	    dbcs_Input_status = C_COMMENT_BEGIN_;
	    DBCS_UNGET_RETURN (c1, converted_char);

	  default:
	    DBCS_UNGET_RETURN (c1, converted_char);
	  }
      }

    default:
      return (converted_char);
    }
}


/*
 * dbcs_get_next_w_char () - Read one character (not byte)
 *   return:
 *   parser(in):
 */
static int
dbcs_get_next_w_char (PARSER_CONTEXT * parser)
{
  int input_char;
  int return_char;

  if (dbcs_Unget_buf != DBCS_UNGET_BUF)
    {
      return_char = *--dbcs_Unget_buf;
    }
  else if ((input_char = DBCS_NEXT_CHAR (parser)) == EOF)
    {
      return_char = EOF;
    }
  else
    {
      if ((input_char & 0x80) == 0)
	{
	  return_char = input_char;
	}
      else
	{
	  int c1;

	  if ((c1 = DBCS_NEXT_CHAR (parser)) == EOF)
	    {
	      return_char = EOF;
	    }
	  return_char = ((input_char & 0xff) << 8) | c1;
	}
    }

  return (return_char);
}


/*
 * dbcs_convert_w_char () - convert wide character into ASCII
 *   return:
 *   input_char(in):
 */
static int
dbcs_convert_w_char (int input_char)
{
  if (dbcs_Input_status == DQS_ ||
      dbcs_Input_status == DQS_TRANSPARENT_ ||
      dbcs_Input_status == DQS_TRANSPARENT_2 ||
      dbcs_Input_status == SQS_ || dbcs_Input_status == SQS_TRANSPARENT_)
    {
      return (input_char);
    }
  else
    {
      if (input_char == WSPACE_CHAR)
	{
	  *dbcs_Unget_buf++ = 0x20;
	  return (0x20);
	}

      return (input_char);
    }
}
