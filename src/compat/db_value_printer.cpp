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
 * db_value_printer.cpp
 */

#include "db_value_printer.hpp"

#include "db_date.h"
#include "dbtype.h"
#include "intl_support.h"
#include "language_support.h"
#include "numeric_opfunc.h"
#include "object_primitive.h"
#include "set_object.h"
#include "string_buffer.hpp"
#include "string_opfunc.h"
#if !defined(SERVER_MODE)
#include "virtual_object.h"
#endif
#include "work_space.h"

#include <assert.h>
#include <float.h>

#if defined(SA_MODE)
extern unsigned int db_on_server;
#endif

const char db_value_printer::DECIMAL_FORMAT[] = "%#.*g";

namespace
{
  //--------------------------------------------------------------------------------
  // DB_VALUE of type DB_TYPE_BIT or DB_TYPE_VARBIT
  void describe_bit_string (string_buffer &buf, const db_value *value, bool pad_byte)
  {
    unsigned char *bstring;
    int nibble_length, nibbles, count;

    assert (value != NULL);

    bstring = (unsigned char *) db_get_string (value);
    if (bstring == NULL)
      {
	return;
      }

    nibble_length = ((db_get_string_length (value) + 3) / 4);
    for (nibbles = 0, count = 0; nibbles < nibble_length - 1; count++, nibbles += 2)
      {
	buf ("%02x", bstring[count]);
      }

    /* If we don't have a full byte on the end, print the nibble. */
    if (nibbles < nibble_length)
      {
	if (pad_byte)
	  {
	    buf ("%02x", bstring[count]);
	  }
	else
	  {
	    //use only the 1st hex digit
	    char tmp[3] = {0};
	    sprintf (tmp, "%1x", bstring[count]);
	    buf += tmp[0];
	  }
      }
  }

  //--------------------------------------------------------------------------------
  void describe_real (string_buffer &buf, double value, int precision)
  {
    char tbuf[24];

    snprintf (tbuf, sizeof (tbuf), db_value_printer::DECIMAL_FORMAT, precision, value);
    if (strstr (tbuf, "Inf"))
      {
	snprintf (tbuf, sizeof (tbuf), db_value_printer::DECIMAL_FORMAT, precision, (value > 0 ? FLT_MAX : -FLT_MAX));
      }
    buf (tbuf);
  }
}//namespace

//--------------------------------------------------------------------------------
void db_value_printer::describe_money (const db_monetary *value)
{
  assert (value != NULL);

  m_buf ("%s%.2f", intl_get_money_esc_ISO_symbol (value->type), value->amount);
  if (strstr (m_buf.get_buffer (), "Inf"))
    {
      m_buf ("%s%.2f", intl_get_money_esc_ISO_symbol (value->type), (value->amount > 0 ? DBL_MAX : -DBL_MAX));
    }
}

//--------------------------------------------------------------------------------
void db_value_printer::describe_value (const db_value *value)
{
  INTL_CODESET codeset = INTL_CODESET_NONE;

  if (DB_IS_NULL (value))
    {
      m_buf ("NULL");
    }
  else
    {
      /* add some extra info to the basic data value */
      switch (DB_VALUE_TYPE (value))
	{
	case DB_TYPE_CHAR:
	case DB_TYPE_VARCHAR:
	  codeset = db_get_string_codeset (value);
	  if (codeset != LANG_SYS_CODESET)
	    {
	      m_buf ("%s", lang_charset_introducer (codeset));
	    }
	  m_buf += '\'';
	  describe_data (value);
	  m_buf += '\'';
	  break;

	case DB_TYPE_ENUMERATION:
	  if (db_get_enum_string (value) == NULL && db_get_enum_short (value) != 0)
	    {
#if defined(SERVER_MODE)
	      /* to print enum index as int */
	      m_buf ("%d", (int)db_get_enum_short (value));
	      break;
#elif defined(SA_MODE)
	      if (db_on_server)
		{
		  /* to print enum index as int */
		  m_buf ("%d", (int)db_get_enum_short (value));
		  break;
		}
	      else
		{
		  /* describe value should not be called on an enumeration which is not fully constructed */
		  assert (false);
		  m_buf ("''");
		}
#else /* CS_MODE */
	      /* describe value should not be called on an enumeration which is not fully constructed */
	      assert (false);
	      m_buf ("''");
#endif
	    }
	  else
	    {
	      DB_VALUE varchar_val;

	      /* print enumerations as strings */
	      if (tp_enumeration_to_varchar (value, &varchar_val) == NO_ERROR)
		{
		  codeset = (INTL_CODESET) db_get_enum_codeset (value);
		  if (codeset != LANG_SYS_CODESET)
		    {
		      m_buf ("%s", lang_charset_introducer (codeset));
		    }
		  describe_value (&varchar_val);
		}
	      else
		{
		  /* tp_enumeration_to_varchar only fails if the enum string is null which we already checked */
		  assert (false);
		}
	    }
	  break;
	case DB_TYPE_DATE:
	  m_buf ("date '");
	  describe_data (value);
	  m_buf += '\'';
	  break;

	case DB_TYPE_JSON:
	  m_buf ("json '");
	  describe_data (value);
	  m_buf += '\'';
	  break;

	case DB_TYPE_TIME:
	  m_buf ("time '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_TIMETZ:
	  m_buf ("timetz '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_TIMELTZ:
	  m_buf ("timeltz '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_TIMESTAMP:
	  m_buf ("timestamp '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_TIMESTAMPTZ:
	  m_buf ("timestamptz '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_TIMESTAMPLTZ:
	  m_buf ("timestampltz '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_DATETIME:
	  m_buf ("datetime '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_DATETIMETZ:
	  m_buf ("datetimetz '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_DATETIMELTZ:
	  m_buf ("datetimeltz '");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	  m_buf ("N'");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  m_buf ("X'");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_BLOB:
	  m_buf ("BLOB'");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	case DB_TYPE_CLOB:
	  m_buf ("CLOB'");
	  describe_data (value);
	  m_buf += '\'';
	  break;
	default:
	  describe_data (value);
	  break;
	}
    }
}

//--------------------------------------------------------------------------------
void db_value_printer::describe_data (const db_value *value)
{
  OID         *oid = 0;
  db_object   *obj = 0;
  db_monetary *money = 0;
  DB_SET      *set = 0;
  db_elo      *elo = 0;
  DB_MIDXKEY *midxkey;
  char *src, *pos, *end;
  double d;
  char line[1025];

  if (DB_IS_NULL (value))
    {
      m_buf ("NULL");
    }
  switch (DB_VALUE_TYPE (value))
    {
    case DB_TYPE_INTEGER:
      m_buf ("%d", db_get_int (value));
      break;

    case DB_TYPE_BIGINT:
      m_buf ("%lld", (long long) db_get_bigint (value));
      break;

    case DB_TYPE_POINTER:
      m_buf ("%p", db_get_pointer (value));
      break;

    case DB_TYPE_SHORT:
      m_buf ("%d", (int) db_get_short (value));
      break;

    case DB_TYPE_ERROR:
      m_buf ("%d", (int) db_get_error (value));
      break;

    case DB_TYPE_FLOAT:
      describe_real (m_buf, db_get_float (value), DB_FLOAT_DECIMAL_PRECISION);
      break;

    case DB_TYPE_DOUBLE:
      describe_real (m_buf, db_get_double (value), DB_DOUBLE_DECIMAL_PRECISION);
      break;

    case DB_TYPE_NUMERIC:
      m_buf ("%s", numeric_db_value_print (value, line));
      break;

    case DB_TYPE_BIT:
    case DB_TYPE_VARBIT:
      describe_bit_string (m_buf, value, m_padding);
      break;

    case DB_TYPE_CHAR:
    case DB_TYPE_NCHAR:
    case DB_TYPE_VARCHAR:
    case DB_TYPE_VARNCHAR:
      /* Copy string into buf providing for any embedded quotes. Strings may have embedded NULL characters and
       * embedded quotes.  None of the supported multibyte character codesets have a conflict between a quote
       * character and the second byte of the multibyte character.
       */
      src = db_get_string (value);
      end = src + db_get_string_size (value);
      while (src < end)
	{
	  /* Find the position of the next quote or the end of the string, whichever comes first.  This loop is
	   * done in place of strchr in case the string has an embedded NULL.
	   */
	  for (pos = src; pos && pos < end && (*pos) != '\''; pos++)
	    ;

	  /* If pos < end, then a quote was found.  If so, copy the partial buffer and duplicate the quote */
	  if (pos < end)
	    {
	      m_buf.add_bytes (pos - src + 1, src);
	      m_buf += '\'';
	    }
	  /* If not, copy the remaining part of the buffer */
	  else
	    {
	      m_buf.add_bytes (end - src, src);
	    }

	  /* advance src to just beyond the point where we left off */
	  src = pos + 1;
	}
      break;

    case DB_TYPE_OBJECT:
#if defined(SERVER_MODE)
      assert (false);
#else //#if defined(SERVER_MODE)
      obj = db_get_object (value);
      if (obj == NULL)
	{
	  break;
	}
      if (obj->is_vid)
	{
	  DB_VALUE vobj;
	  vid_object_to_vobj (obj, &vobj);
	  describe_value (&vobj);
	  break;
	}
      oid = WS_OID (obj);
      m_buf ("%d|%d|%d", int (oid->volid), int (oid->pageid), int (oid->slotid));
#endif //#if defined(SERVER_MODE)
      break;
    /* If we are on the server, fall thru to the oid case The value is probably nonsense, but that is safe to do.
     * This case should simply not occur.
     */

    case DB_TYPE_OID:
      oid = (OID *) db_get_oid (value);
      if (oid == NULL)
	{
	  break;
	}
      m_buf ("%d|%d|%d", int (oid->volid), int (oid->pageid), int (oid->slotid));
      break;

    case DB_TYPE_VOBJ:
      m_buf ("vid:");
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      set = db_get_set (value);
      if (set != NULL)
	{
	  describe_set ((const db_collection *) set);
	}
      else
	{
	  m_buf ("NULL");
	}
      break;

    case DB_TYPE_JSON:
      m_buf ("%s", value->data.json.json_body);
      break;
    case DB_TYPE_MIDXKEY:
      midxkey = db_get_midxkey (value);
      if (midxkey != NULL)
	{
	  describe_midxkey (midxkey);
	}
      else
	{
	  m_buf ("NULL");
	}
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      elo = db_get_elo (value);
      if (elo != NULL)
	{
	  if (elo->type == ELO_FBO)
	    {
	      assert (elo->locator != NULL);
	      m_buf ("%s", elo->locator);
	    }
	  else        /* ELO_LO */
	    {
	      /* should not happen for now */
	      assert (0);
	    }
	}
      else
	{
	  m_buf ("NULL");
	}
      break;

      /*
       * This constant is necessary to fake out the db_?_to_string()
       * routines that are expecting a buffer length.  Since we assume
       * that our buffer is big enough in this code, just pass something
       * that ought to work for every case.
       */
#define TOO_BIG_TO_MATTER       1024

    case DB_TYPE_TIME:
      (void) db_time_to_string (line, TOO_BIG_TO_MATTER, db_get_time (value));
      m_buf (line);
      break;
    case DB_TYPE_TIMELTZ:
      (void) db_timeltz_to_string (line, TOO_BIG_TO_MATTER, db_get_time (value));
      m_buf (line);
      break;

    case DB_TYPE_TIMETZ:
    {
      DB_TIMETZ *time_tz;

      time_tz = db_get_timetz (value);
      (void) db_timetz_to_string (line, TOO_BIG_TO_MATTER, &time_tz->time, &time_tz->tz_id);
      m_buf (line);
    }
    break;

    case DB_TYPE_TIMESTAMP:
      (void) db_utime_to_string (line, TOO_BIG_TO_MATTER, db_get_timestamp (value));
      m_buf (line);
      break;
    case DB_TYPE_TIMESTAMPLTZ:
      (void) db_timestampltz_to_string (line, TOO_BIG_TO_MATTER, db_get_timestamp (value));
      m_buf (line);
      break;

    case DB_TYPE_TIMESTAMPTZ:
    {
      DB_TIMESTAMPTZ *ts_tz;

      ts_tz = db_get_timestamptz (value);
      (void) db_timestamptz_to_string (line, TOO_BIG_TO_MATTER, & (ts_tz->timestamp), & (ts_tz->tz_id));
      m_buf (line);
    }
    break;

    case DB_TYPE_DATETIME:
      (void) db_datetime_to_string (line, TOO_BIG_TO_MATTER, db_get_datetime (value));
      m_buf (line);
      break;
    case DB_TYPE_DATETIMELTZ:
      (void) db_datetimeltz_to_string (line, TOO_BIG_TO_MATTER, db_get_datetime (value));
      m_buf (line);
      break;

    case DB_TYPE_DATETIMETZ:
    {
      DB_DATETIMETZ *dt_tz;

      dt_tz = db_get_datetimetz (value);
      (void) db_datetimetz_to_string (line, TOO_BIG_TO_MATTER, & (dt_tz->datetime), & (dt_tz->tz_id));
      m_buf (line);
    }
    break;

    case DB_TYPE_DATE:
      (void) db_date_to_string (line, TOO_BIG_TO_MATTER, db_get_date (value));
      m_buf (line);
      break;

    case DB_TYPE_MONETARY:
      money = db_get_monetary (value);
      OR_MOVE_DOUBLE (&money->amount, &d);
      describe_money (money);
      break;

    case DB_TYPE_NULL:
      /* Can't get here because the DB_IS_NULL test covers DB_TYPE_NULL */
      break;

    case DB_TYPE_VARIABLE:
    case DB_TYPE_SUB:
    case DB_TYPE_DB_VALUE:
      /* make sure line is NULL terminated, may not be necessary line[0] = '\0'; */
      break;

    default:
      /* NB: THERE MUST BE NO DEFAULT CASE HERE. ALL TYPES MUST BE HANDLED! */
      assert (false);
      break;
    }
}

//--------------------------------------------------------------------------------
void db_value_printer::describe_midxkey (const db_midxkey *midxkey, int help_Max_set_elements)
{
  db_value value;
  int size, end, i;
  int prev_i_index;
  char *prev_i_ptr;

  assert (midxkey != NULL);

  m_buf += '{';
  size = midxkey->ncolumns;
  if (help_Max_set_elements == 0 || help_Max_set_elements > size)
    {
      end = size;
    }
  else
    {
      end = help_Max_set_elements;
    }

  prev_i_index = 0;
  prev_i_ptr = NULL;
  for (i = 0; i < end; i++)
    {
      pr_midxkey_get_element_nocopy (midxkey, i, &value, &prev_i_index, &prev_i_ptr);
      describe_value (&value);
      if (i < size - 1)
	{
	  m_buf (", ");
	}
      if (!DB_IS_NULL (&value) && value.need_clear == true)
	{
	  pr_clear_value (&value);
	}
    }
  if (i < size)
    {
      m_buf (". . .");
    }
  m_buf += '}';
}

//--------------------------------------------------------------------------------
void db_value_printer::describe_set (const db_collection *set, int help_Max_set_elements)
{
  DB_VALUE value;
  int size, end, i;

  assert (set != NULL);

  m_buf += '{';
  size = set_size ((DB_COLLECTION *)set);
  if (help_Max_set_elements == 0 || help_Max_set_elements > size)
    {
      end = size;
    }
  else
    {
      end = help_Max_set_elements;
    }

  for (i = 0; i < end; ++i)
    {
      set_get_element ((DB_COLLECTION *)set, i, &value);
      describe_value (&value);
      db_value_clear (&value);
      if (i < size - 1)
	{
	  m_buf (", ");
	}
    }
  if (i < size)
    {
      m_buf (". . .");
    }
  m_buf += '}';
}
