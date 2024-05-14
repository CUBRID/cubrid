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
 * condition_handler.c : condition handling module
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <assert.h>

#include "porting.h"
#include "condition_handler.h"
#include "condition_handler_code.h"
#include "message_catalog.h"
#include "adjustable_array.h"
#include "condition_handler_err.h"
#include "intl_support.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* format type of condition argument */
enum co_format_type
{
  FORMAT_INTEGER = 0,
  FORMAT_FLOAT,
  FORMAT_POINTER,
  FORMAT_STRING,
  FORMAT_LITERAL,
  FORMAT_LONG_INTEGER,
  FORMAT_LONG_DOUBLE,
  FORMAT_UNKNOWN
};
typedef enum co_format_type CO_FORMAT_TYPE;

/* condition argument value structure */
typedef struct co_argument CO_ARGUMENT;
struct co_argument
{
  CO_FORMAT_TYPE format;
  union
  {
    int int_value;
    double double_value;
    void *pointer_value;
    long long_int_value;
    long double long_double_value;
  } value;
};

/* Constants for parsing message text. */
#define MAX_FLOAT_WIDTH     (128*MB_LEN_MAX)	/* Max size of float string */
#define MAX_INT_WIDTH       (64*MB_LEN_MAX)	/* Max size of integer string */
#define MAX_POINTER_WIDTH   (128*MB_LEN_MAX)	/* Max size of pointer string */

#define SPACE()         L" \t"
#define DIGITS()        L"0123456789"
#define INTEGER_SPECS() L"dioxXuc"
#define STRING_SPECS()  L"s"
#define POINTER_SPECS() L"p"
#define FLOAT_SPECS()   L"fgGeE"
#define FLAGS()         L"- +0#"
#define WC_CSPEC()      L'%'
#define WC_PSPEC()      L'$'
#define WC_RADIX()      L'.'
#define SHORT_SPEC()    L'h'
#define LONG_SPEC()     L'l'
#define LONG_DOUBLE_SPEC() L'L'

#define GET_WC( wc, mbs, size) (size = mbtowc( &wc, mbs, MB_LEN_MAX), wc)

#define REPORT_LINE_WIDTH 72
#define REPORT_LINE_INDENT 9

/* Current condition data. */
static int co_Current_code = 0;
static ADJ_ARRAY *co_Current_arguments = NULL;
static ADJ_ARRAY *co_String_values = NULL;
static ADJ_ARRAY *co_Current_message = NULL;
static int co_Message_completed;

static ADJ_ARRAY *co_Parameter_string = NULL;
static ADJ_ARRAY *co_Conversion_buffer = NULL;
static CO_DETAIL co_Current_detail = CO_DETAIL_USER;


static int co_signalv (int code, const char *format, va_list args);
#if defined(ENABLE_UNUSED_FUNCTION)
static const char *co_print_parameter (int p, CO_FORMAT_TYPE type, const char *format, int width);
#endif
static int co_find_conversion (const char *format, int from, int *start, CO_FORMAT_TYPE * type, int *position,
			       int *width);

static const char *co_conversion_spec (const char *cspec, int size);

/*
 * co_signal() - signal a condition by recording the code, default message, and
 *               parameters used to report the condition
 *   return: condition code
 *      0                   if success.
 *      CO_ERR_BAD_FORMAT     if format contains invalid format char.
 *      CO_ERR_BAD_CODE       if invalid code.
 *      CO_ERR_BAD_POSITION   if format contains a position specifier.
 *   code(in): condition code
 *   format(in): printf-style format string that defines the default
 *               message for the condition
 *
 * Note:
 *   The format string contains a conversion specifier for each following
 *   parameter value, in order. The format cannot contain position specifiers.
 *   The reason is that the format string controls how the following parameters
 *   are interpreted. Therefore, the conversion specs in the format must match the
 *   parameters in order and number.
 */
int
co_signal (int code, const char *format, ...)
{
  int error;
  va_list args;

  assert (format != NULL);

  va_start (args, format);
  error = co_signalv (code, format, args);
  va_end (args);
  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * co_code_module() - return module identifier for the given condition code
 *   return:  module identifier
 *   code(in): condition code
 */
int
co_code_module (int code)
{
  return (-code) / CO_MAX_CODE;
}

/*
 * co_code_id() - return the code identifier for the given codition code
 *   return: code identifier
 *   code(in): condition code
 */
int
co_code_id (int code)
{
  return ((-code) % CO_MAX_CODE) + 1;
}

/*
 * co_report() - print current condition messageto the given file
 *   return: none
 *   file(in/out): output FILE pointer
 *   severity(in): condition severity level
 *      CO_WARNING_SEVERITY: The user should be aware of the condition but does
 *                           not necessarily need to respond to it.
 *      CO_ERROR_SEVERITY:   Something has gone wrong. The user should do
 *                           something to correct or undo the condition.
 *      CO_FATAL_SEVERITY:   Fatal internal error. The program cannot continue.
 *
 * Note: If severity is CO_FATAL_SEVERITY, abort() is called after printing
 *       the message.
 */
void
co_report (FILE * file, CO_SEVERITY severity)
{
  static const int co_Report_msg_width = REPORT_LINE_WIDTH - REPORT_LINE_INDENT;

  char label[MB_LEN_MAX * 32];
  char line[MB_LEN_MAX * REPORT_LINE_WIDTH];
  const char *message;
  int label_length;
  int msg_length;
  int length;
  int line_length;
  int code;

  assert (file != NULL);

  code = co_code ();
  if (!code)
    {
      /* No condition to report!! */
      return;
    }

  /* Get condition module/code label. */
  sprintf (label, " (%d/%d)", co_code_module (code), co_code_id (code));

  /* Print severity. */
  switch (severity)
    {
    case CO_FATAL_SEVERITY:
      message = "Quit:";
      break;
    case CO_ERROR_SEVERITY:
      message = "Error:";
      break;
    default:
      message = "Warning:";
    }
  fprintf (file, "%-*s", REPORT_LINE_INDENT, message);

  /* Print message lines. */
  message = co_message ();
  for (msg_length = intl_mbs_len (message), label_length = intl_mbs_len (label);
       (length = msg_length + label_length) > co_Report_msg_width;
       message = intl_mbs_nth (message, line_length), message += intl_mbs_spn (message, SPACE ()), msg_length =
       intl_mbs_len (message))
    {
      /* Look for word break for next line. */
      if (msg_length <= co_Report_msg_width)
	{
	  /* Break at code label. */
	  line_length = msg_length;
	}
      else
	{
	  /* Break at word break. */
	  int nbytes;
	  wchar_t wc;
	  for (line_length = co_Report_msg_width;
	       line_length > 0 && (nbytes = mbtowc (&wc, intl_mbs_nth (message, line_length), MB_LEN_MAX))
	       && !wcschr (SPACE (), wc); line_length--)
	    {
	      ;
	    }
	  if (line_length == 0)
	    {
	      /* Couldn't find word break. */
	      line_length = co_Report_msg_width;
	    }
	}

      if (message != NULL)
	{
	  int len;
	  const char *cptr = intl_mbs_nth (message, line_length);
	  if (cptr == NULL)
	    {
	      len = intl_mbs_len (message);
	    }
	  else
	    {
	      len = cptr - message;
	    }

	  /* Print next message line. */
	  strncpy (line, message, len);
	  line[len] = '\0';
	  fprintf (file, "%s\n%-*s", line, REPORT_LINE_INDENT, "");
	}
    }

  /* Print last line with code label. */
  fprintf (file, "%s%s\n\n", message, label);

  if (severity == CO_FATAL_SEVERITY)
    {
      abort ();
    }
}

/*
 * co_message() - return a message for the current condition
 *   return: NULL if current condition code is 0,
 *           current condition message string otherwise
 *
 * Note:
  *   The caller must not modify or free the returned message string. The
 *   message pointed to by the return value may be changed by the next
 *   call to co_signal(v).
 *
 */
const char *
co_message (void)
{
  int width;
  int default_position = 0;
  int start, end;
  int length;
  CO_FORMAT_TYPE type;
  int position;
  const char *parameter;

  if (!co_code ())
    {
      return NULL;
    }

  /* Message already retrieved? */
  if (!co_Message_completed)
    {
      /* For each conversion specification in message... */
      for (start = 0;
	   (start =
	    co_find_conversion ((const char *) adj_ar_get_buffer (co_Current_message), start, &end, &type, &position,
				&width)) != ADJ_AR_EOA; start += length)
	{
	  /* Get formatted parameter string. */
	  if (type == FORMAT_LITERAL)
	    {
	      parameter = "%";
	    }
	  else
	    {
	      int index;

	      if (position)
		{
		  index = position;
		}
	      else
		{
		  index = default_position;
		}
	      parameter =
		co_print_parameter (index, type,
				    co_conversion_spec ((const char *) adj_ar_get_buffer (co_Current_message) + start,
							end - start), width);
	    }

	  /* Replace conversion spec with formatted parameter string. */
	  length = (parameter != NULL) ? strlen (parameter) : 0;
	  adj_ar_replace (co_Current_message, parameter, length, start, end);

	  if (type != FORMAT_LITERAL)
	    {
	      default_position++;
	    }
	}

      co_Message_completed = 1;
    }
  return (const char *) adj_ar_get_buffer (co_Current_message);

}
#endif

/*
 * co_code() - return the current condition code
 *   return: current condition code
 */
int
co_code (void)
{
  return co_Current_code;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * co_put_detail() - change the current condition detail level.
 *   return: condition code
 *      0 if success,
 *      CO_ERR_BAD_DETAIL if invalid detail value is given.
 *   level(in): new condition detail level
 *      CO_DETAIL_USER:  Show detail needed by users to understand
 *                       and respond to reported conditions.
 *      CO_DETAIL_DBA:   Show detail needed by a DBA to understand
 *                       and respond to reported conditions.
 *      CO_DETAIL_DEBUG: Show detail needed to locate the source of
 *                       internal errors causing conditions.
 */
int
co_put_detail (CO_DETAIL level)
{
  int error = 0;

  if (level < CO_DETAIL_USER || level >= CO_DETAIL_MAX)
    {
      error = CO_ERR_BAD_DETAIL;
      co_signal (CO_ERR_BAD_DETAIL, CO_ER_FMT_BAD_DETAIL, level);
    }
  else
    {
      co_Current_detail = level;
    }

  return error;

}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * co_final() - clean up all memory allocated in this module
 *   return: none
 */
void
co_final (void)
{
  if (co_Current_arguments)
    {
      adj_ar_free (co_Current_arguments);
      co_Current_arguments = NULL;
    }

  if (co_String_values)
    {
      adj_ar_free (co_String_values);
      co_String_values = NULL;
    }

  if (co_Current_message)
    {
      adj_ar_free (co_Current_message);
      co_Current_message = NULL;
    }

  if (co_Conversion_buffer != NULL)
    {
      adj_ar_free (co_Conversion_buffer);
      co_Conversion_buffer = NULL;
    }

  if (co_Parameter_string != NULL)
    {
      adj_ar_free (co_Parameter_string);
      co_Parameter_string = NULL;
    }
}

/*
 * co_signal() - an alternate form of co_signal
 *   return: condition code
 *      0                   if success.
 *      CO_ERR_BAD_FORMAT     if format contains invalid format char.
 *      CO_ERR_BAD_CODE       if invalid code.
 *      CO_ERR_BAD_POSITION   if format contains a position specifier.
 *   code(in): condition code
 *   format(in): printf-style format string
 *   args(in) : parameter values specified by format string
 */
static int
co_signalv (int code, const char *format, va_list args)
{
  int error;
  int start, end;

  /* Check for bad code value. */
  if (code > 0)
    {
      co_signal (CO_ERR_BAD_CODE, CO_ER_FMT_BAD_CODE, code);
      return CO_ERR_BAD_CODE;
    }

  /* Initialize current condition state. */
  if (!co_Current_arguments)
    {
      co_Current_arguments = adj_ar_new (sizeof (CO_ARGUMENT), 0, 2.0);
      co_Current_message = adj_ar_new (sizeof (char), 64 * MB_LEN_MAX, 2.0);
      co_String_values = adj_ar_new (sizeof (char), 64 * MB_LEN_MAX, 2.0);
    }
  co_Current_code = code;
  adj_ar_remove (co_Current_arguments, 0, ADJ_AR_EOA);
  adj_ar_remove (co_Current_message, 0, ADJ_AR_EOA);
  adj_ar_remove (co_String_values, 0, ADJ_AR_EOA);
  co_Message_completed = 0;

  /* Save default message */
  adj_ar_append (co_Current_message, format, (int) strlen (format) + 1);

  /* Initialize args for new condition. */
  for (error = 0, start = 0; !error; start = end)
    {
      CO_ARGUMENT arg;
      CO_FORMAT_TYPE type;
      int position;
      int width;

      start = co_find_conversion (format, start, &end, &type, &position, &width);
      if (start == ADJ_AR_EOA)
	{
	  break;
	}

      if (position)
	{
	  error = CO_ERR_BAD_POSITION;
	  co_signal (error, CO_ER_FMT_BAD_POSITION);
	}
      else if (type != FORMAT_LITERAL)
	{
	  char *string;
	  int nchars;

	  arg.format = type;
	  switch (arg.format)
	    {
	    case FORMAT_STRING:
	      string = va_arg (args, char *);
	      nchars = (int) strlen (string) + 1;

	      /* Save offset to arg in array of string values. */
	      adj_ar_append (co_String_values, (void *) string, nchars);
	      arg.value.int_value = adj_ar_length (co_String_values) - nchars;
	      break;

	    case FORMAT_INTEGER:
	      arg.value.int_value = va_arg (args, int);
	      break;

	    case FORMAT_FLOAT:
	      arg.value.double_value = va_arg (args, double);
	      break;

	    case FORMAT_POINTER:
	      arg.value.pointer_value = va_arg (args, void *);
	      break;

	    case FORMAT_LONG_INTEGER:
	      arg.value.long_int_value = va_arg (args, long);
	      break;

	    case FORMAT_LONG_DOUBLE:
	      arg.value.long_double_value = va_arg (args, long double);
	      break;

	    default:
	      error = CO_ERR_BAD_FORMAT;
	      co_signal (error, CO_ER_FMT_BAD_FORMAT, co_conversion_spec (format + start, end - start));
	    }

	  if (!error)
	    {
	      /* Append next arg to array of arg values. */
	      adj_ar_append (co_Current_arguments, (void *) &arg, 1);
	    }
	}
    }

  return error;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * co_print_parameter() - print given parameter using given format
 *   return: formatted parameter string
 *   index(in): index of parameter to print
 *   type(in): parameter format type
 *   format(in) : parameter format string
 *   width(in) : parameter field width
 */
static const char *
co_print_parameter (int index, CO_FORMAT_TYPE type, const char *format, int width)
{
  static const char *bad_index = "?";
  static const char *bad_type = "*";
  CO_ARGUMENT *arg;
  char *string;

  if (!co_Parameter_string)
    {
      co_Parameter_string = adj_ar_new (sizeof (char), 32 * MB_LEN_MAX, 2.0);
      if (co_Parameter_string == NULL)
	{
	  return NULL;
	}
    }
  adj_ar_remove (co_Parameter_string, 0, ADJ_AR_EOA);

  /* Adjust position to 0-based index. */
  index--;

  /* Adjust width to char size. */
  width *= MB_LEN_MAX;

  /* Valid parameter index? */
  if (index < 0 || index >= adj_ar_length (co_Current_arguments))
    {
      adj_ar_replace (co_Parameter_string, bad_index, strlen (bad_index) + 1, 0, ADJ_AR_EOA);
    }
  else
    {
      /* Format matches arg type? */
      arg = (CO_ARGUMENT *) adj_ar_get_buffer (co_Current_arguments) + index;
      if (type != arg->format)
	{
	  adj_ar_replace (co_Parameter_string, bad_type, strlen (bad_type) + 1, 0, ADJ_AR_EOA);
	}
      else
	{
	  /* Print parameter into string array. */
	  switch (type)
	    {
	    case FORMAT_STRING:
	      string = (char *) adj_ar_get_buffer (co_String_values) + arg->value.int_value;
	      adj_ar_initialize (co_Parameter_string, NULL,
				 width > (int) strlen (string) ? width + 1 : (int) strlen (string) + 1);
	      sprintf ((char *) adj_ar_get_buffer (co_Parameter_string), format, string);
	      break;

	    case FORMAT_INTEGER:
	      adj_ar_initialize (co_Parameter_string, NULL, width > MAX_INT_WIDTH ? width + 1 : MAX_INT_WIDTH);
	      sprintf ((char *) adj_ar_get_buffer (co_Parameter_string), format, arg->value.int_value);
	      break;

	    case FORMAT_FLOAT:
	      adj_ar_initialize (co_Parameter_string, NULL, width > MAX_FLOAT_WIDTH ? width + 1 : MAX_FLOAT_WIDTH);
	      sprintf ((char *) adj_ar_get_buffer (co_Parameter_string), format, arg->value.double_value);
	      break;

	    case FORMAT_POINTER:
	      adj_ar_initialize (co_Parameter_string, NULL, width > MAX_POINTER_WIDTH ? width + 1 : MAX_POINTER_WIDTH);
	      sprintf ((char *) adj_ar_get_buffer (co_Parameter_string), format, arg->value.pointer_value);
	      break;

	    case FORMAT_LONG_INTEGER:
	      adj_ar_initialize (co_Parameter_string, NULL, width > MAX_INT_WIDTH ? width + 1 : MAX_INT_WIDTH);
	      sprintf ((char *) adj_ar_get_buffer (co_Parameter_string), format, arg->value.long_int_value);
	      break;

	    case FORMAT_LONG_DOUBLE:
	      adj_ar_initialize (co_Parameter_string, NULL, width > MAX_FLOAT_WIDTH ? width + 1 : MAX_FLOAT_WIDTH);
	      sprintf ((char *) adj_ar_get_buffer (co_Parameter_string), format, arg->value.long_double_value);
	      break;

	    default:
	      /* Should never get here. */
	      adj_ar_replace (co_Parameter_string, bad_type, strlen (bad_type) + 1, 0, ADJ_AR_EOA);
	      break;
	    }
	}
    }

  return (const char *) adj_ar_get_buffer (co_Parameter_string);
}
#endif

/*
 * co_find_conversion() - return a description of the next conversion spec in
 *                 the given format string, starting at the given position
 *   return: ADJ_AR_EOF if no more conversion specs are found,
 *           index of the first char in the conversion spec otherwise.
 *   format(in): format string
 *   from(in): start position to scan
 *   end(out): end of conversion spec
 *   type(out): parameter format type returned (%, d, f, p, or s)
 *   position(out): position specifier returned (0 if none)
 *   width(out): field width of format returned (0 if none)
 */
static int
co_find_conversion (const char *format, int from, int *end, CO_FORMAT_TYPE * type, int *position, int *width)
{
  const char *p;
  wchar_t wc;
  int csize;
  int result = 0;
  int val;

  /* No more conversion specs? */
  const char *start = intl_mbs_chr (format + from, WC_CSPEC ());
  if (!start)
    {
      return ADJ_AR_EOA;
    }

  /* Parse next conversion spec. */
  p = intl_mbs_nth (start, 1);
  if (!p)
    {
      return ADJ_AR_EOA;
    }
  *position = 0;

  /* Handle %% spec */
  if (GET_WC (wc, p, csize) == WC_CSPEC ())
    {
      *type = FORMAT_LITERAL;
      *width = 0;
    }
  else
    {
      CO_FORMAT_TYPE length_type;
      char *rest;

      /* Return position specifier. */
      result = str_to_int32 (&val, &rest, p, 10);
      if (result == 0 && val > 0 && GET_WC (wc, rest, csize) == WC_PSPEC ())
	{
	  *position = val;
	  p = rest + csize;
	}
      else
	{
	  *position = 0;
	}

      /* Skip flags. */
      p += intl_mbs_spn (p, FLAGS ());

      /* Width specified? */
      result = str_to_int32 (&val, &rest, p, 10);
      if (result == 0)
	{
	  *width = val;

	  /* Yes, skip precision. */
	  p = rest;
	  if (GET_WC (wc, p, csize) == WC_RADIX ())
	    {
	      while (wcschr (DIGITS (), GET_WC (wc, (p += csize), csize)))
		{
		  ;
		}
	    }
	}

      /* Check for short/long specifier. */
      GET_WC (wc, p, csize);
      switch (wc)
	{
	case SHORT_SPEC ():
	  length_type = FORMAT_INTEGER;
	  p += csize;
	  break;
	case LONG_SPEC ():
	  length_type = FORMAT_LONG_INTEGER;
	  p += csize;
	  break;
	case LONG_DOUBLE_SPEC ():
	  length_type = FORMAT_LONG_DOUBLE;
	  p += csize;
	  break;
	default:
	  length_type = FORMAT_UNKNOWN;
	}

      /* Return format type. */
      GET_WC (wc, p, csize);

      if (wcschr (INTEGER_SPECS (), wc))
	{
	  *type = FORMAT_INTEGER;
	}
      else if (wcschr (STRING_SPECS (), wc))
	{
	  *type = FORMAT_STRING;
	}
      else if (wcschr (FLOAT_SPECS (), wc))
	{
	  *type = FORMAT_FLOAT;
	}
      else if (wcschr (POINTER_SPECS (), wc))
	{
	  *type = FORMAT_POINTER;
	}
      else
	{
	  *type = FORMAT_UNKNOWN;
	}

      /* Reconcile conversion char with short/long spec. */
      if ((length_type == FORMAT_INTEGER && *type != FORMAT_INTEGER)
	  || (length_type == FORMAT_LONG_INTEGER && *type != FORMAT_INTEGER) || (length_type == FORMAT_LONG_DOUBLE
										 && *type != FORMAT_FLOAT))
	{
	  /* Invalid combination! */
	  *type = FORMAT_UNKNOWN;
	}
      else if (length_type != FORMAT_UNKNOWN)
	{
	  /* Conversion type modified by short/long spec. */
	  *type = length_type;
	}
    }

  /* Return end of conversion spec. */
  *end = CAST_STRLEN (p + csize - format);

  /* Return start of conversion spec. */
  return CAST_STRLEN (start - format);
}

/*
 * co_conversion_spec() - return the same conversion spec with any position
 *                     specifier removed
 *   return: modified conversion spec string
 *   cspec(in): conversion spec string
 *   size(in): size of cspec
 */
static const char *
co_conversion_spec (const char *cspec, int size)
{
  const char *new_cspec, *end;

  /* Copy conversion spec to NUL-terminated array. */
  if (!co_Conversion_buffer)
    {
      co_Conversion_buffer = adj_ar_new (sizeof (char), 4 * MB_LEN_MAX, 2.0);
      if (co_Conversion_buffer == NULL)
	{
	  return NULL;
	}
    }
  adj_ar_replace (co_Conversion_buffer, cspec, size, 0, ADJ_AR_EOA);
  adj_ar_append (co_Conversion_buffer, "\0", 1);
  new_cspec = (const char *) adj_ar_get_buffer (co_Conversion_buffer);

  /* Is there a position specifier? */
  end = intl_mbs_chr (new_cspec, WC_PSPEC ());
  if (end)
    {
      /*
       * remove position specifier from conversion spec, i.e. remove
       * all chars following initial '%' up through final '$'.
       */
      adj_ar_remove (co_Conversion_buffer, mblen (new_cspec, MB_LEN_MAX),
		     CAST_STRLEN (end + mblen (end, MB_LEN_MAX) - new_cspec));
    }

  return new_cspec;
}
