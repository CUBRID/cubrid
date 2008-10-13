/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * valcnv.c - type conversion routines
 *
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "object_representation.h"
#include "memory_manager_2.h"
#include "language_support.h"
#include "set_object_1.h"
#include "numeric.h"
#include "qp_str.h"
#include "dbtype.h"
#include "db_date.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define VALCNV_TOO_BIG_TO_MATTER   1024

typedef struct valcnv_buffer VALCNV_BUFFER;
struct valcnv_buffer
{
  size_t length;
  unsigned char *bytes;
};

static int valcnv_Max_set_elements = 10;

/* TODO: valcnv_convert_value_to_string() should be declared in valcnv.h */
extern int valcnv_convert_value_to_string (DB_VALUE * value);

static VALCNV_BUFFER *valcnv_append_bytes (VALCNV_BUFFER * old_string,
					   const char *new_tail,
					   const size_t new_tail_length);
static VALCNV_BUFFER *valcnv_append_string (VALCNV_BUFFER * old_string,
					    const char *new_tail);
static VALCNV_BUFFER *valcnv_convert_float_to_string (VALCNV_BUFFER * buf,
						      const float value);
static VALCNV_BUFFER *valcnv_convert_double_to_string (VALCNV_BUFFER * buf,
						       const double value);
static VALCNV_BUFFER *valcnv_convert_bit_to_string (VALCNV_BUFFER * buf,
						    const DB_VALUE * value);
static VALCNV_BUFFER *valcnv_convert_set_to_string (VALCNV_BUFFER * buf,
						    DB_SET * set);
static VALCNV_BUFFER *valcnv_convert_money_to_string (const double value);
static VALCNV_BUFFER *valcnv_convert_data_to_string (VALCNV_BUFFER * buf,
						     const DB_VALUE * value);
static VALCNV_BUFFER *valcnv_convert_db_value_to_string (VALCNV_BUFFER * buf,
							 const DB_VALUE *
							 value);

/*
 * vc_append_bytes(): append a string to string buffer 
 *
 *   returns: on success, ptr to concatenated string. otherwise, NULL. 
 *   old_string(IN/OUT): original string 
 *   new_tail(IN): string to be appended
 *   new_tail_length(IN): length of the string to be appended
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_append_bytes (VALCNV_BUFFER * buffer_p, const char *new_tail_p,
		     const size_t new_tail_length)
{
  size_t old_length;

  if (new_tail_p == NULL)
    {
      return buffer_p;
    }
  else if (buffer_p == NULL)
    {
      buffer_p = (VALCNV_BUFFER *) malloc (sizeof (VALCNV_BUFFER));
      if (buffer_p == NULL)
	{
	  return NULL;
	}

      buffer_p->length = 0;
      buffer_p->bytes = NULL;
    }

  old_length = buffer_p->length;
  buffer_p->length += new_tail_length;

  buffer_p->bytes =
    (unsigned char *) realloc (buffer_p->bytes, buffer_p->length);
  if (buffer_p->bytes == NULL)
    {
      return NULL;
    }

  memcpy (&buffer_p->bytes[old_length], new_tail_p, new_tail_length);
  return buffer_p;
}

/*
 * vc_append_string(): append a string to string buffer
 *
 *   returns: on success, ptr to concatenated string. otherwise, NULL.
 *   old_string(IN/OUT): original string
 *   new_tail(IN): string to be appended
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_append_string (VALCNV_BUFFER * buffer_p, const char *new_tail_p)
{
  return valcnv_append_bytes (buffer_p, new_tail_p, strlen (new_tail_p));
}

/*
 * vc_float_to_string(): append float value to string buffer
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   buf(IN/OUT): buffer
 *   value(IN): floating point value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_float_to_string (VALCNV_BUFFER * buffer_p, const float value)
{
  char tbuf[24];

  sprintf (tbuf, "%.17g", value);

#if defined(HPUX)
  /* workaround for HP's broken printf */
  if (strstr (tbuf, "++") || strstr (tbuf, "--"))
#else /* HPUX */
  if (strstr (tbuf, "Inf"))
#endif /* HPUX */
    {
      sprintf (tbuf, "%.17g", (value > 0 ? FLT_MAX : -FLT_MAX));
    }

  return valcnv_append_string (buffer_p, tbuf);
}

/*
 * vc_double_to_string(): append double value to string buffer
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   buf : buffer
 *   value(IN): double value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_double_to_string (VALCNV_BUFFER * buffer_p, const double value)
{
  char tbuf[24];

  sprintf (tbuf, "%.17g", value);

#if defined(HPUX)
  /* workaround for HP's broken printf */
  if (strstr (tbuf, "++") || strstr (tbuf, "--"))
#else /* HPUX */
  if (strstr (tbuf, "Inf"))
#endif /* HPUX */
    {
      sprintf (tbuf, "%.17g", (value > 0 ? DBL_MAX : -DBL_MAX));
    }

  return valcnv_append_string (buffer_p, tbuf);
}

/*
 * vc_bit_to_string(): append bit value to string buffer
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   buf(IN/OUT): buffer
 *   value(IN): BIT value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_bit_to_string (VALCNV_BUFFER * buffer_p,
			      const DB_VALUE * value_p)
{
  unsigned char *bit_string_p = (unsigned char *) DB_GET_STRING (value_p);
  int nibble_len, nibbles, count;
  char tbuf[10];

  nibble_len = (DB_GET_STRING_LENGTH (value_p) + 3) / 4;

  for (nibbles = 0, count = 0; nibbles < nibble_len - 1;
       count++, nibbles += 2)
    {
      sprintf (tbuf, "%02x", bit_string_p[count]);
      tbuf[2] = '\0';
      buffer_p = valcnv_append_string (buffer_p, tbuf);
      if (buffer_p == NULL)
	{
	  return NULL;
	}
    }

  if (nibbles < nibble_len)
    {
      /* unresolved...
       * Must we consider the ldb environment ?
       */
      sprintf (tbuf, "%1x", bit_string_p[count]);
      tbuf[1] = '\0';
      buffer_p = valcnv_append_string (buffer_p, tbuf);
      if (buffer_p == NULL)
	{
	  return NULL;
	}
    }

  return buffer_p;
}

/*
 * vc_set_to_string(): append set value to string buffer
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   buf(IN/OUT): buffer
 *   set(IN): SET value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_set_to_string (VALCNV_BUFFER * buffer_p, DB_SET * set_p)
{
  DB_VALUE value;
  int err, size, max_n, i;

  if (set_p == NULL)
    {
      return buffer_p;
    }

  buffer_p = valcnv_append_string (buffer_p, "{");
  if (buffer_p == NULL)
    {
      return NULL;
    }

  size = set_size (set_p);
  if (valcnv_Max_set_elements == 0)
    {
      max_n = size;
    }
  else
    {
      max_n = MIN (size, valcnv_Max_set_elements);
    }

  for (i = 0; i < max_n; i++)
    {
      err = set_get_element (set_p, i, &value);
      if (err < 0)
	{
	  return NULL;
	}

      buffer_p = valcnv_convert_db_value_to_string (buffer_p, &value);
      pr_clear_value (&value);
      if (i < size - 1)
	{
	  buffer_p = valcnv_append_string (buffer_p, ", ");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	}
    }

  if (i < size)
    {
      buffer_p = valcnv_append_string (buffer_p, "...");
      if (buffer_p == NULL)
	{
	  return NULL;
	}
    }

  buffer_p = valcnv_append_string (buffer_p, "}");
  if (buffer_p == NULL)
    {
      return NULL;
    }

  return buffer_p;
}

/*
 * vc_money_to_string(): append monetary value to string buffer
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   value(IN): monetary value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_money_to_string (const double value)
{
  char cbuf[LDBL_MAX_10_EXP + 20];	/* 20 == floating fudge factor */

  sprintf (cbuf, "%.2f", value);

#if defined(HPUX)
  /* workaround for HP's broken printf */
  if (strstr (cbuf, "++") || strstr (cbuf, "--"))
#else /* HPUX */
  if (strstr (cbuf, "Inf"))
#endif /* HPUX */
    {
      sprintf (cbuf, "%.2f", (value > 0 ? DBL_MAX : -DBL_MAX));
    }

  return valcnv_append_string (NULL, cbuf);
}

/*
 * vc_data_to_string(): append a value to string buffer
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   buf(IN/OUT): buffer
 *   value(IN): a value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_data_to_string (VALCNV_BUFFER * buffer_p,
			       const DB_VALUE * value_p)
{
  OID *oid_p;
  DB_SET *set_p;
  DB_ELO *elo_p;
  char *src_p, *end_p, *p;
  ptrdiff_t len;

  DB_MONETARY *money_p;
  VALCNV_BUFFER *money_string_p;
  const char *currency_symbol_p;
  double amount;

  char line[1025];

  int err;

  if (DB_IS_NULL (value_p))
    {
      buffer_p = valcnv_append_string (buffer_p, "NULL");
    }
  else
    {
      switch (DB_VALUE_TYPE (value_p))
	{
	case DB_TYPE_INTEGER:
	  sprintf (line, "%d", DB_GET_INTEGER (value_p));
	  buffer_p = valcnv_append_string (buffer_p, line);
	  break;

	case DB_TYPE_SHORT:
	  sprintf (line, "%d", (int) DB_GET_SHORT (value_p));
	  buffer_p = valcnv_append_string (buffer_p, line);
	  break;

	case DB_TYPE_FLOAT:
	  buffer_p =
	    valcnv_convert_float_to_string (buffer_p, DB_GET_FLOAT (value_p));
	  break;

	case DB_TYPE_DOUBLE:
	  buffer_p =
	    valcnv_convert_double_to_string (buffer_p,
					     DB_GET_DOUBLE (value_p));
	  break;

	case DB_TYPE_NUMERIC:
	  buffer_p =
	    valcnv_append_string (buffer_p,
				  numeric_db_value_print ((DB_VALUE *)
							  value_p));
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  buffer_p = valcnv_convert_bit_to_string (buffer_p, value_p);
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARCHAR:
	case DB_TYPE_VARNCHAR:
	  src_p = DB_GET_STRING (value_p);
	  end_p = src_p + DB_GET_STRING_SIZE (value_p);
	  while (src_p < end_p)
	    {
	      for (p = src_p; p < end_p && *p != '\''; p++)
		{
		  ;
		}

	      if (p < end_p)
		{
		  len = p - src_p + 1;
		  buffer_p = valcnv_append_bytes (buffer_p, src_p, len);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  buffer_p = valcnv_append_string (buffer_p, "'");
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }
		}
	      else
		{
		  buffer_p =
		    valcnv_append_bytes (buffer_p, src_p, end_p - src_p);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }
		}

	      src_p = p + 1;
	    }
	  break;

	case DB_TYPE_OBJECT:
	case DB_TYPE_OID:
	  oid_p = (OID *) DB_GET_OID (value_p);

	  sprintf (line, "%d", (int) oid_p->volid);
	  buffer_p = valcnv_append_string (buffer_p, line);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "|");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  sprintf (line, "%d", (int) oid_p->pageid);
	  buffer_p = valcnv_append_string (buffer_p, line);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "|");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  sprintf (line, "%d", (int) oid_p->slotid);
	  buffer_p = valcnv_append_string (buffer_p, line);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  break;

	case DB_TYPE_SET:
	case DB_TYPE_MULTI_SET:
	case DB_TYPE_SEQUENCE:
	  set_p = DB_GET_SET (value_p);
	  if (set_p == NULL)
	    {
	      buffer_p = valcnv_append_string (buffer_p, "NULL");
	    }
	  else
	    {
	      return valcnv_convert_set_to_string (buffer_p, set_p);
	    }

	  break;

	case DB_TYPE_ELO:
	  elo_p = DB_GET_ELO (value_p);
	  if (elo_p == NULL)
	    {
	      buffer_p = valcnv_append_string (buffer_p, "NULL");
	    }
	  else
	    {
	      if (elo_p->pathname != NULL)
		{
		  buffer_p = valcnv_append_string (buffer_p, elo_p->pathname);
		}
	      else if (elo_p->type == ELO_LO)
		{
		  sprintf (line, "%d", (int) elo_p->loid.vfid.volid);
		  buffer_p = valcnv_append_string (buffer_p, line);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  buffer_p = valcnv_append_string (buffer_p, "|");
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  sprintf (line, "%d", (int) elo_p->loid.vfid.fileid);
		  buffer_p = valcnv_append_string (buffer_p, line);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  buffer_p = valcnv_append_string (buffer_p, "|");
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  sprintf (line, "%d", (int) elo_p->loid.vpid.volid);
		  buffer_p = valcnv_append_string (buffer_p, line);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  buffer_p = valcnv_append_string (buffer_p, "|");
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }

		  sprintf (line, "%d", (int) elo_p->loid.vpid.pageid);
		  buffer_p = valcnv_append_string (buffer_p, line);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }
		}
	      else
		{
		  sprintf (line, "%lx", (unsigned long) elo_p);
		  buffer_p = valcnv_append_string (buffer_p, line);
		  if (buffer_p == NULL)
		    {
		      return NULL;
		    }
		}
	    }
	  break;

	case DB_TYPE_TIME:
	  err = db_time_to_string (line, VALCNV_TOO_BIG_TO_MATTER,
				   DB_GET_TIME (value_p));
	  if (err == 0)
	    {
	      return NULL;
	    }
	  buffer_p = valcnv_append_string (buffer_p, line);
	  break;

	case DB_TYPE_TIMESTAMP:
	  err = db_timestamp_to_string (line, VALCNV_TOO_BIG_TO_MATTER,
					DB_GET_TIMESTAMP (value_p));
	  if (err == 0)
	    {
	      return NULL;
	    }
	  buffer_p = valcnv_append_string (buffer_p, line);
	  break;

	case DB_TYPE_DATE:
	  err = db_date_to_string (line, VALCNV_TOO_BIG_TO_MATTER,
				   DB_GET_DATE (value_p));
	  if (err == 0)
	    {
	      return NULL;
	    }
	  buffer_p = valcnv_append_string (buffer_p, line);
	  break;

	case DB_TYPE_MONETARY:
	  money_p = DB_GET_MONETARY (value_p);
	  OR_MOVE_DOUBLE (&money_p->amount, &amount);
	  money_string_p = valcnv_convert_money_to_string (amount);

	  currency_symbol_p = lang_currency_symbol (money_p->type);
	  strncpy (line, currency_symbol_p, strlen (currency_symbol_p));
	  strncpy (line + strlen (currency_symbol_p),
		   (char *) money_string_p->bytes, money_string_p->length);
	  line[strlen (currency_symbol_p) + money_string_p->length] = '\0';

	  free_and_init (money_string_p->bytes);
	  free_and_init (money_string_p);
	  buffer_p = valcnv_append_string (buffer_p, line);
	  break;

	default:
	  break;
	}
    }

  return buffer_p;
}

/*
 * vc_db_value_to_string(): append a value to string buffer with a type prefix
 *
 *   returns: on success, ptr to converted string. otherwise, NULL.
 *   buf(IN/OUT): buffer
 *   value(IN): a value which is to be converted
 *                                                                           
 */
static VALCNV_BUFFER *
valcnv_convert_db_value_to_string (VALCNV_BUFFER * buffer_p,
				   const DB_VALUE * value_p)
{
  if (DB_IS_NULL (value_p))
    {
      buffer_p = valcnv_append_string (buffer_p, "NULL");
    }
  else
    {
      switch (DB_VALUE_TYPE (value_p))
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	case DB_TYPE_DATE:
	  buffer_p = valcnv_append_string (buffer_p, "date '");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	case DB_TYPE_TIME:
	  buffer_p = valcnv_append_string (buffer_p, "time '");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	case DB_TYPE_TIMESTAMP:
	  buffer_p = valcnv_append_string (buffer_p, "timestamp '");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  buffer_p = valcnv_append_string (buffer_p, "N'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  buffer_p = valcnv_append_string (buffer_p, "X'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	case DB_TYPE_ELO:
	  buffer_p = valcnv_append_string (buffer_p, "ELO'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }

	  buffer_p = valcnv_append_string (buffer_p, "'");
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;

	default:
	  buffer_p = valcnv_convert_data_to_string (buffer_p, value_p);
	  if (buffer_p == NULL)
	    {
	      return NULL;
	    }
	  break;
	}
    }

  return buffer_p;
}

/*
 * valcnv_convert_value_to_string(): convert a value to a string type value
 *
 *   returns: on success, NO_ERROR. otherwise, ER_FAILED.
 *   value(IN/OUT): a value which is to be converted to string
 *                  Note that the value is cleaned up during conversion.
 *                                                                           
 */
int
valcnv_convert_value_to_string (DB_VALUE * value_p)
{
  VALCNV_BUFFER buffer = { 0, NULL };
  VALCNV_BUFFER *buf_p;
  DB_VALUE src_value;

  if (!DB_IS_NULL (value_p))
    {
      buf_p = &buffer;
      buf_p = valcnv_convert_db_value_to_string (buf_p, value_p);
      if (buf_p == NULL)
	{
	  return ER_FAILED;
	}

      DB_MAKE_VARCHAR (&src_value, DB_MAX_STRING_LENGTH,
		       (char *) buf_p->bytes, buf_p->length);

      pr_clear_value (value_p);
      (*(tp_String.setval)) (value_p, &src_value, true);

      pr_clear_value (&src_value);
      free_and_init (buf_p->bytes);
    }

  return NO_ERROR;
}
