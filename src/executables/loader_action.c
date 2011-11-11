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
 * loader_action.c - action routines for the database loader
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "language_support.h"
#include "loader_old.h"
#include "class_object.h"
#include "object_domain.h"
#include "db.h"
#include "string_opfunc.h"

#include "message_catalog.h"
#include "utility.h"
#include "loader_action.h"


extern int loader_yylineno;

/* The class currently being referenced. */
static MOP Refclass = NULL;
/* The class being assigned an id. */
static MOP Idclass = NULL;

static void display_error (int adjust);
static void parse_error (DB_TYPE token_type, const char *token);
static void domain_error (DB_TYPE token_type);
static void invalid_class_error (const char *name);
static void unknown_class_error (const char *name);
static void invalid_class_id_error (int id);
static void unauthorized_class_error (const char *name);
static DB_VALUE *get_value (DB_TYPE token_type);

/*
 * ERROR REPORTING
 *
 * NOTE: There are two levels of error handling here.  When a call
 * is made to a ldr_ function, those functions will return a int
 * and use er_set to set the global error state.  For those errors,
 * we simply use db_error_string to get the text and display it.
 *
 * Other errors are detected by the action routines in this file.
 * For these, we don't use er_set since it isn't really necessary.  Instead
 * we just have message catalog entries for the error text we wish
 * to display.
 *
 */


/*
 * display_error_line - display the line number of the current input file.
 *    return: void
 *    adjust(in): line number adjustor
 * Note:
 *    It is intended to give error messages context within the file.
 *    Its public because there are a few places in the loader internals
 *    where we need to display messages without propogating
 *    errors back up to the action routine level.
 */
void
display_error_line (int adjust)
{
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_LINE),
	   loader_yylineno + adjust);
}


/*
 * display_error - called whenever one of the ldr_ functions returns
 * an error.
 *    return: void
 *    adjust(in): line number ajustment value
 * Note:
 *    In this case, a standard system error has been set and the text is
 *    obtained through db_error_string.
 */
static void
display_error (int adjust)
{
  const char *msg;

  display_error_line (adjust);
  msg = db_error_string (3);
  fprintf (stderr, msg);
  fprintf (stderr, "\n");
}


/*
 * parse_error - Called when we have some sort of parsing problem
 *    return: void
 *    token_type(in): incoming token type
 *    token(in): token string
 */
static void
parse_error (DB_TYPE token_type, const char *token)
{
  display_error_line (0);

  /*
   * Note, this is always called after a successful call
   * to ldr_add_attribute() via get_value().  At this point
   * the current value counter has been incremented to the
   * next value so we have to use ldr_prev_att_name() to
   * get the name of the attribute we're having problems with.
   */
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_PARSE_ERROR), token,
	   db_get_type_name (token_type), ldr_prev_att_name (),
	   ldr_class_name ());

  ldr_invalid_object ();
}


/*
 * domain_error - called when we have trouble converting an incoming
 * token to a destination value.
 *    return: void
 *    token_type(in): incoming token type
 * Note:
 *    Usually, this kind of error will be caught by the ldr_ module in the
 *    call to ldr_add_value. If not, we display error messages here.
 */
static void
domain_error (DB_TYPE token_type)
{
  SM_DOMAIN *domain;

  display_error_line (0);
  domain = ldr_att_domain ();
  if (domain == NULL)
    fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				     MSGCAT_UTIL_SET_LOADDB,
				     LOADDB_MSG_MISSING_DOMAIN),
	     ldr_att_name (), ldr_class_name ());
  else
    {
      if (pr_is_set_type (TP_DOMAIN_TYPE (domain)))
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_LOADDB,
					   LOADDB_MSG_SET_DOMAIN_ERROR),
		   ldr_att_name (), ldr_class_name (),
		   db_get_type_name (token_type));
	}
      else
	{
	  if (token_type == DB_TYPE_SET)
	    fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_UNEXPECTED_SET),
		     ldr_att_name (), ldr_class_name (),
		     ldr_att_domain_name ());
	  else
	    fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_UNEXPECTED_TYPE),
		     ldr_att_name (), ldr_class_name (),
		     ldr_att_domain_name (), db_get_type_name (token_type));
	}
    }
  ldr_invalid_object ();
}


/*
 * invalid_class_error - Called when the name of an invalid class reference
 * was specified.
 *    return: void
 *    name(in): class name
 */
static void
invalid_class_error (const char *name)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNKNOWN_ATT_CLASS), name,
	   ldr_att_name (), ldr_class_name ());
  ldr_invalid_class ();
}


/*
 * unknown_class_error - Called when an invalid class name was entered.
 *    return: void
 *    name(in): class name
 */
static void
unknown_class_error (const char *name)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNKNOWN_CLASS), name);
  ldr_invalid_class ();
}


/*
 * invalid_class_id_error - Called when an invalid class id is encountered.
 *    return: void
 *    id(in): class id
 */
static void
invalid_class_id_error (int id)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNKNOWN_CLASS_ID), id,
	   ldr_att_name (), ldr_class_name ());
  ldr_invalid_class ();
}


/*
 * unauthorized_class_error - Called when we attempt an operation on a
 * protected class.
 *    return: void
 *    name(in): class name
 */
static void
unauthorized_class_error (const char *name)
{
  display_error_line (0);
  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				   MSGCAT_UTIL_SET_LOADDB,
				   LOADDB_MSG_UNAUTHORIZED_CLASS), name);
  ldr_invalid_class ();
}



/*
 * act_init - Initialize the action routines
 *    return: void
 * Note:
 *    Called by the first statement production by parser module
 */
void
act_init (void)
{
  Refclass = NULL;
  Idclass = NULL;
}


/*
 * act_finish - Called when parser has finished parsing.
 *    return: void
 *    parse_error(in): non-zero if parser detected a parse error
 * Note:
 *    If the parse_error argument is non-zero it indicates that parser
 *    found a parsing error.
 */
void
act_finish (int parse_error)
{
  if (ldr_finish (parse_error))
    {
      display_error (-1);
    }

  if (parse_error)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_LOADDB, LOADDB_MSG_STOPPED));
    }
}


/*
 * act_set_class - Initial handler for the %class command.
 *    return: void
 *    classname(in):  class name
 */
void
act_set_class (char *classname)
{
  MOP class;

  class = db_find_class (classname);
  if (class == NULL)
    {
      unknown_class_error (classname);
    }
  else
    {
      if (ldr_start_class (class, classname))
	display_error (-1);
    }
}


/*
 * act_class_attributes - Restrict the subsequent attribute definitions to
 * only class attributes.
 *    return: void
 */
void
act_class_attributes (void)
{
  ldr_restrict_attributes (LDR_ATTRIBUTE_CLASS);
}


/*
 * act_shared_attributes - Restrict the subsequent attribute definitions to
 * only shared attributes.
 *    return: void
 */
void
act_shared_attributes (void)
{
  ldr_restrict_attributes (LDR_ATTRIBUTE_SHARED);
}


/*
 * act_default_attributes - Restrict the subsequent attribute definitions to
 * only allow default values on non-class attributes.
 *    return: void
 */
void
act_default_attributes ()
{
  ldr_restrict_attributes (LDR_ATTRIBUTE_DEFAULT);
}


/*
 * act_add_attribute - Begin the definition of an attribute.
 *    return: void
 *    attname(in): attribute name
 */
void
act_add_attribute (char *attname)
{
  if (ldr_add_attribute (attname))
    display_error (0);
}


/*
 * act_set_constructor - Register the name of a constructor method.
 *    return: void
 *    name(in): method name
 */
void
act_set_constructor (char *name)
{
  if (ldr_set_constructor (name))
    {
      display_error (0);
    }
}


/*
 * act_add_argument - Add another argument definition for the constructor.
 *    return: void
 *    name(in): argument name
 */
void
act_add_argument (char *name)
{
  if (ldr_add_argument (name))
    display_error (0);
}


/*
 * act_add_instance - Begin the insertion of a new instance.
 *    return: void
 *    id(in): instance id (-1 if none)
 */
void
act_add_instance (int id)
{
  if (ldr_add_instance (id))
    {
      display_error (-1);
    }
}


/*
 * act_set_ref_class - Find the class that is about to be used in an instance
 * reference.
 *    return: void
 *    name(in): class name
 */
void
act_set_ref_class (char *name)
{
  MOP class;

  class = db_find_class (name);
  if (class != NULL)
    {
      Refclass = class;
    }
  else
    {
      invalid_class_error (name);
      ldr_invalid_object ();
    }
}


/*
 * act_set_ref_class_id - Find the class that is about to be used in an
 * instance reference.
 *    return: void
 *    id(in): class id
 * Note:
 *    Unlike act_set_ref_class, the class is identified through a previously
 *    assigned id number.
 */
void
act_set_ref_class_id (int id)
{
  MOP class;

  class = ldr_get_class_from_id (id);
  if (class != NULL)
    {
      Refclass = class;
    }
  else
    {
      invalid_class_id_error (id);
      ldr_invalid_object ();
    }
}


/*
 * act_start_id - Begin the specification of a class id assignment.
 *    return: void
 *    name(in): class name
 */
void
act_start_id (char *name)
{
  MOP class;

  class = db_find_class (name);
  if (class != NULL)
    {
      Idclass = class;
    }
  else
    {
      invalid_class_error (name);
    }
}


/*
 * act_set_id - Adding an id number to a class
 *    return: void
 *    id(in): class id
 */
void
act_set_id (int id)
{
  if (Idclass != NULL)
    {
      ldr_assign_class_id (Idclass, id);
      Idclass = NULL;
    }
}


/*
 * act_reference - Define a value through a reference to another instance.
 *    return: void
 *    id(in): instance number
 */
void
act_reference (int id)
{
  if (ldr_add_reference (Refclass, id))
    {
      display_error (0);
    }

  /* always reset this each time */
  Refclass = NULL;
}


/*
 * act_reference_class - Define a value as a reference directly to a class
 * object rather than an instance.
 *    return: void
 */
void
act_reference_class (void)
{
  if (ldr_add_reference_to_class (Refclass))
    {
      display_error (0);
    }
}


/*
 * act_start_set - Begin the population of a set constant.
 *    return: void
 */
void
act_start_set (void)
{
  if (ldr_start_set ())
    {
      display_error (0);
    }
}


/*
 * act_end_set - Stop the population of a set constant.
 *    return: void
 */
void
act_end_set (void)
{
  if (ldr_end_set ())
    {
      display_error (0);
    }
}


/*
 * get_value - Checks to see if the token type is compatible with the next
 * expected attribute or argument
 *    return: destination value container to store the incoming data value
 *    token_type(in): incoming token type
 */
static DB_VALUE *
get_value (DB_TYPE token_type)
{
  DB_VALUE *value = NULL;

  if (ldr_add_value (token_type, &value))
    {
      display_error (0);
    }

  return (value);
}


/*
 * act_int - assign an integer value
 *    return: void
 *    token(in): int token value
 */
void
act_int (char *token)
{
  DB_VALUE temp, *value;
  int error;
  TP_DOMAIN domain;
  TP_DOMAIN *domain_ptr;

  value = get_value (DB_TYPE_INTEGER);
  if (value != NULL)
    {
      domain_ptr = tp_domain_resolve_value (value, &domain);
      if (domain_ptr == NULL)
	{
	  domain_error (DB_TYPE_INTEGER);
	  return;
	}

      db_make_string (&temp, token);
      error = tp_value_cast (&temp, value, domain_ptr, false);
      if (error)
	{
	  parse_error (TP_DOMAIN_TYPE (domain_ptr), token);
	}
    }
}

/*
 * act_real - Assign a floating point value.
 *    return: void
 *    token(in): token string
 */
void
act_real (char *token)
{
  DB_VALUE temp, *value;
  int error;
  TP_DOMAIN domain;
  TP_DOMAIN *domain_ptr;

  value = get_value (DB_TYPE_FLOAT);
  if (value != NULL)
    {
      domain_ptr = tp_domain_resolve_value (value, &domain);
      if (domain_ptr == NULL)
	{
	  domain_error (DB_TYPE_FLOAT);
	  return;
	}

      db_make_string (&temp, token);
      error = tp_value_cast (&temp, value, domain_ptr, false);
      if (error)
	{
	  parse_error (TP_DOMAIN_TYPE (domain_ptr), token);
	}
    }
}


/*
 * act_monetary - Assign a monetary value.
 *    return: void
 *    token(in): token string
 */
void
act_monetary (char *token)
{
  DB_VALUE *value;
  int args;
  const unsigned char *p = (const unsigned char *) token;
  double amt;

  if ((value = get_value (DB_TYPE_MONETARY)) != NULL)
    {
      if (DB_VALUE_DOMAIN_TYPE (value) != DB_TYPE_MONETARY)
	{
	  domain_error (DB_TYPE_MONETARY);
	}
      else
	{
	  switch (lang_id ())
	    {
	    case INTL_LANG_KOREAN:
	      if (p[0] == '\\')
		{
		  token += 1;
		}
	      else if (p[0] == 0xa3 && p[1] == 0xdc)
		{
		  token += 2;
		}
	      break;
	    default:
	      if (p[0] == '$')
		{
		  token += 1;
		}
	    }
	  args = sscanf (token, "%lf", &amt);

	  if (args != 1)
	    parse_error (DB_TYPE_MONETARY, token);
	  else
	    {
	      switch (lang_id ())
		{
		case INTL_LANG_KOREAN:
		  db_make_monetary (value, DB_CURRENCY_WON, amt);
		  break;
		default:
		  db_make_monetary (value, DB_CURRENCY_DOLLAR, amt);
		  break;
		}
	    }
	}
    }
}


/*
 * act_date - assign a date value
 *    return: void
 *    token(in): token string
 */
void
act_date (char *token)
{
  DB_VALUE *value;
  int args, month, day, year;

  if ((value = get_value (DB_TYPE_DATE)) != NULL)
    {
      if (DB_VALUE_DOMAIN_TYPE (value) != DB_TYPE_DATE)
	{
	  domain_error (DB_TYPE_DATE);
	}
      else
	{
	  args = sscanf (token, "%d/%d/%d", &month, &day, &year);
	  if (args != 3)
	    {
	      parse_error (DB_TYPE_DATE, token);
	    }
	  else
	    {
	      db_make_date (value, month, day, year);
	    }
	}
    }
}


/*
 * act_time - assign a time value
 *    return: void
 *    token(in): token string
 *    ttype(in): time format type
 */
void
act_time (char *token, int ttype)
{
  DB_VALUE *value;
  int args, hour, minute, second;
  char half[8];

  if ((value = get_value (DB_TYPE_TIME)) != NULL)
    {
      if (DB_VALUE_DOMAIN_TYPE (value) != DB_TYPE_TIME)
	{
	  domain_error (DB_TYPE_TIME);
	}
      else
	{
	  switch (ttype)
	    {
	    case 1:
	      args = sscanf (token, "%d:%d", &hour, &minute);
	      if (args != 2)
		{
		  parse_error (DB_TYPE_TIME, token);
		}
	      else
		{
		  db_make_time (value, hour, minute, 0);
		}
	      break;
	    case 2:
	      args = sscanf (token, "%d:%d:%d", &hour, &minute, &second);
	      if (args != 3)
		{
		  parse_error (DB_TYPE_TIME, token);
		}
	      else
		{
		  db_make_time (value, hour, minute, second);
		}
	      break;
	    case 3:
	      args = sscanf (token, "%d:%d %7s", &hour, &minute, half);
	      if (args != 3)
		parse_error (DB_TYPE_TIME, token);
	      else
		{
		  if (strcasecmp (half, "am") == 0)
		    {
		      db_make_time (value, hour, minute, 0);
		    }
		  else
		    {
		      db_make_time (value, hour + 12, minute, 0);
		    }
		}
	      break;
	    case 4:
	      args =
		sscanf (token, "%d:%d:%d %7s", &hour, &minute, &second, half);
	      if (args != 4)
		parse_error (DB_TYPE_TIME, token);
	      else
		{
		  if (strcasecmp (half, "am") == 0)
		    {
		      db_make_time (value, hour, minute, second);
		    }
		  else
		    {
		      db_make_time (value, hour + 12, minute, second);
		    }
		}
	      break;
	    default:
	      DB_MAKE_NULL (value);
	      break;
	    }
	}
    }
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * act_datetime - assign a date value
 *    return: void
 *    token(in): token string
 */
void
act_datetime (char *token)
{
  DB_VALUE *value;
  int args, month, day, year;
  int hour, minute, second, millisecond;
  DB_DATETIME datetime;

  if ((value = get_value (DB_TYPE_DATETIME)) != NULL)
    {
      if (DB_VALUE_DOMAIN_TYPE (value) != DB_TYPE_DATETIME)
	{
	  domain_error (DB_TYPE_DATETIME);
	}
      else
	{
	  args = sscanf (token, "%d/%d/%d %d:%d:%d.%d", &month, &day, &year,
			 &hour, &minute, &second, &millisecond);
	  if (args != 7)
	    {
	      parse_error (DB_TYPE_DATETIME, token);
	    }
	  else
	    {
	      db_datetime_encode (&datetime, month, day, year, hour,
				  minute, second, millisecond);
	      db_make_datetime (value, &datetime);
	    }
	}
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * act_string - assign a string value
 *    return: void
 *    token(in): string
 *    size(in): integer number of bytes in the string
 *    dtype(in): DB_DATA_TYPE of the string
 */
void
act_string (char *token, int size, DB_TYPE dtype)
{
  DB_VALUE temp, *value;
  TP_DOMAIN domain;
  TP_DOMAIN *domain_ptr;
  DB_TYPE domain_type;

  /* size = 0 is an empty string, which is fine */
  value = get_value (dtype);
  if (value != NULL && size >= 0)
    {
      if (dtype == DB_TYPE_CHAR)
	{
	  DB_MAKE_VARCHAR (&temp, TP_FLOATING_PRECISION_VALUE, token, size);
	}
      else
	{
	  DB_MAKE_VARNCHAR (&temp, TP_FLOATING_PRECISION_VALUE, token, size);
	}

      domain_ptr = tp_domain_resolve_value (value, &domain);
      if (domain_ptr == NULL)
	{
	  domain_error (dtype);
	  return;
	}

      if (tp_value_cast (&temp, value, domain_ptr,
			 false) != DOMAIN_COMPATIBLE)
	{
	  domain_type = TP_DOMAIN_TYPE (domain_ptr);
	  if (domain_type == DB_TYPE_TIME
	      || domain_type == DB_TYPE_DATE
	      || domain_type == DB_TYPE_TIMESTAMP
	      || domain_type == DB_TYPE_DATETIME)
	    {
	      printf ("Illegal date/time literal - %s. Resetting to NULL\n",
		      token);
	      db_value_domain_init (value, domain_type, 0, 0);
	    }
	  else
	    {
	      parse_error (domain_type, token);
	    }
	}
    }
}


/*
 * act_null - assign a NULL value
 *    return: void
 */
void
act_null (void)
{
  DB_VALUE *value;

  if ((value = get_value (DB_TYPE_NULL)) != NULL)
    {
      DB_MAKE_NULL (value);
    }
}


/*
 * act_system - Assign a reference to one of the special system objects.
 *    return: void
 *    token(in): token string
 *    type(in): system object type
 */
void
act_system (const char *token, ACT_SYSOBJ_TYPE type)
{
  char name[PATH_MAX + 8];
  int error, len, external;

  error = 0;
  if (token[0] == '\"')
    {
      token++;
    }
  len = strlen (token);
  if (len && token[len - 1] == '\"')
    {
      len--;
    }
  if (len > 0 && len < (int) sizeof (name))
    {
      strncpy (name, token, len);
      name[len] = '\0';

      /* we don't handle user or class object references yet,
         convert to unauthorized class error for now */
      if (type == SYS_USER)
	{
	  unauthorized_class_error ("db_user");
	}
      else
	{
	  unauthorized_class_error ("*system class*");
	}
    }
}


/*
 * act_bstring - assign an bit string value
 *    return: void
 *    token(in): token string
 *    type(in): integer indicating whether the string is bit or hex
 */
void
act_bstring (char *token, int type)
{
  DB_VALUE temp, *value;
  TP_DOMAIN domain;
  TP_DOMAIN *domain_ptr;

  value = get_value (DB_TYPE_BIT);
  if (value)
    {
      if (type == 1)
	{			/* Binary formatted string */
	  /* The source size is the number of binary digits to convert */
	  int src_size = strlen (token);

	  /* The destination size is the number of bytes needed to
	   * represent the source.
	   */
	  int dest_size = (src_size + 7) / 8;

	  /* Allocate the destination buffer.  After converting, check
	   * that all of the source digits got converted
	   */
	  char *bstring = db_private_alloc (NULL, dest_size + 1);
	  if (bstring == NULL
	      || qstr_bit_to_bin (bstring, dest_size, token,
				  src_size) != src_size)
	    {
	      parse_error (DB_TYPE_BIT, token);
	    }

	  /* Coerce to the proper domain */
	  DB_MAKE_VARBIT (&temp, TP_FLOATING_PRECISION_VALUE, bstring,
			  src_size);
	  temp.need_clear = true;

	  domain_ptr = tp_domain_resolve_value (value, &domain);
	  if (domain_ptr == NULL)
	    {
	      domain_error (DB_TYPE_BIT);
	      return;
	    }

	  if (tp_value_cast (&temp, value, domain_ptr,
			     false) != DOMAIN_COMPATIBLE)
	    {
	      parse_error (TP_DOMAIN_TYPE (domain_ptr), token);
	    }
	}
      else
	{			/* Hexadecimal formatted string */
	  /* The source size is the number of hex digits to convert */
	  int src_size = strlen (token);

	  /* The destination size is the number of bytes needed to
	   * represent the source.
	   */
	  int dest_size = (src_size + 1) / 2;

	  /* Allocate the destination buffer.  After converting, check
	   * that all of the source digits got converted
	   */
	  char *bstring = db_private_alloc (NULL, dest_size + 1);
	  if (qstr_hex_to_bin (bstring, dest_size, token, src_size) !=
	      src_size)
	    {
	      parse_error (DB_TYPE_BIT, token);
	    }

	  /* Coerce to the proper domain */
	  DB_MAKE_VARBIT (&temp, TP_FLOATING_PRECISION_VALUE, bstring,
			  src_size * 4);
	  temp.need_clear = true;

	  domain_ptr = tp_domain_resolve_value (value, &domain);
	  if (domain_ptr == NULL)
	    {
	      domain_error (DB_TYPE_BIT);
	      return;
	    }

	  if (tp_value_cast (&temp, value, domain_ptr,
			     false) != DOMAIN_COMPATIBLE)
	    {
	      parse_error (TP_DOMAIN_TYPE (domain_ptr), token);
	    }
	}
    }
}
