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
 * db_value_converter.cpp - conversion from string to DB_VALUE
 */

#ident "$Id$"

#include <cstring>
#include <cassert>

#include "common.hpp"
#include "db_date.h"
#include "db_json.hpp"
#include "db_value_converter.hpp"
#if defined (SERVER_MODE)
#include "dbtype.h"
#endif
#if defined (SA_MODE)
#include "dbtype_function.h"
#endif
#include "language_support.h"
#include "numeric_opfunc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "porting.h"

#define MAX_DIGITS_FOR_SHORT  5  // default for 16 bit signed shorts: 32767 (0x7FFF)
#define MAX_DIGITS_FOR_INT    10 // default for 32 bit signed integers: 2147483647 (0x7FFFFFFF)
#define MAX_DIGITS_FOR_BIGINT 19 // default for 64 bit signed big integers: 9223372036854775807 (0x7FFFFFFFFFFFFFFF)

#define ROUND(x) (int)((x) > 0 ? ((x) + .5) : ((x) - .5))

namespace cubload
{

  conv_func
  get_conv_func (int ldr_type, const tp_domain_t *domain)
  {
    conv_func setters[DB_TYPE_LAST + 1][LDR_TYPE_MAX + 1];

    setters[DB_TYPE_CHAR][LDR_STR] = &to_db_char;

    setters[DB_TYPE_VARCHAR][LDR_STR] = &to_db_varchar;

    setters[DB_TYPE_BIGINT][LDR_INT] = &to_db_bigint;

    setters[DB_TYPE_INTEGER][LDR_INT] = &to_db_int;

    setters[DB_TYPE_SHORT][LDR_INT] = &to_db_short;

    setters[DB_TYPE_FLOAT][LDR_INT] = &to_db_float;
    setters[DB_TYPE_FLOAT][LDR_NUMERIC] = &to_db_float;
    setters[DB_TYPE_FLOAT][LDR_DOUBLE] = &to_db_float;
    setters[DB_TYPE_FLOAT][LDR_FLOAT] = &to_db_float;

    setters[DB_TYPE_DOUBLE][LDR_INT] = &to_db_double;
    setters[DB_TYPE_DOUBLE][LDR_NUMERIC] = &to_db_double;
    setters[DB_TYPE_DOUBLE][LDR_DOUBLE] = &to_db_double;
    setters[DB_TYPE_DOUBLE][LDR_FLOAT] = &to_db_double;

    setters[DB_TYPE_DATE][LDR_DATE] = &to_db_date;
    setters[DB_TYPE_TIME][LDR_TIME] = &to_db_time;

    setters[DB_TYPE_TIMESTAMP][LDR_STR] = &to_db_timestamp;
    setters[DB_TYPE_TIMESTAMP][LDR_TIMESTAMP] = &to_db_timestamp;

    setters[DB_TYPE_TIMESTAMPLTZ][LDR_TIMESTAMPLTZ] = &to_db_timestampltz;

    setters[DB_TYPE_TIMESTAMPTZ][LDR_TIMESTAMPTZ] = &to_db_timestamptz;

    setters[DB_TYPE_DATETIME][LDR_DATETIME] = &to_db_datetime;

    setters[DB_TYPE_DATETIMELTZ][LDR_DATETIMELTZ] = &to_db_datetimeltz;

    setters[DB_TYPE_DATETIMETZ][LDR_DATETIMETZ] = &to_db_datetimetz;

    setters[DB_TYPE_MONETARY][LDR_MONETARY] = &to_db_monetary;
    setters[DB_TYPE_JSON][LDR_STR] = &to_db_json;

    return setters[domain->type->id][ldr_type];
  }

  void
  to_db_null (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    db_make_null (val);
  }

  void
  to_db_short (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int result = 0;
    char *str_ptr;
    size_t str_len = strlen (str);

    db_make_short (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (str_len > MAX_DIGITS_FOR_SHORT)
      {
	double d;
	d = strtod (str, &str_ptr);

	if (str_ptr == str || OR_CHECK_SHORT_OVERFLOW (d))
	  {
	    // TODO CBRD-21654 handle error
	  }
	else
	  {
	    val->data.sh = (short) ROUND (d);
	  }
      }
    else
      {
	int i_val;
	result = parse_int (&i_val, str, 10);

	if (result != 0)
	  {
	    // TODO CBRD-21654 handle error
	  }
	val->data.sh = (short) i_val;
      }
  }

  void
  to_db_int (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int result = 0;
    char *str_ptr;
    size_t str_len = strlen (str);

    db_make_int (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (str_len < MAX_DIGITS_FOR_INT || (str_len == MAX_DIGITS_FOR_INT && (str[0] == '0' || str[0] == '1')))
      {
	result = parse_int (& (val->data.i), str, 10);
	if (result != 0)
	  {
	    // TODO CBRD-21654 handle error
	  }
      }
    else
      {
	double d;
	d = strtod (str, &str_ptr);

	if (str_ptr == str || OR_CHECK_INT_OVERFLOW (d))
	  {
	    // TODO CBRD-21654 handle error
	  }
	else
	  {
	    val->data.i = ROUND (d);
	  }
      }
  }

  void
  to_db_bigint (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int result = 0;
    size_t str_len = strlen (str);

    db_make_bigint (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (str_len < MAX_DIGITS_FOR_BIGINT || (str_len == MAX_DIGITS_FOR_BIGINT && str[0] != '9'))
      {
	result = parse_bigint (& (val->data.bigint), str, 10);
	if (result != 0)
	  {
	    // TODO CBRD-21654 handle error
	  }
      }
    else
      {
	DB_NUMERIC num;
	DB_BIGINT tmp_bigint;

	numeric_coerce_dec_str_to_num (str, num.d.buf);
	if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint) != NO_ERROR)
	  {
	    // TODO CBRD-21654 handle error
	  }
	else
	  {
	    val->data.bigint = tmp_bigint;
	  }
      }
  }

  void
  to_db_char (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int char_count = 0;
    int str_len = (int) strlen (str);

    assert (domain != NULL);

    int precision = domain->precision;
    unsigned char codeset = domain->codeset;

    db_make_char (val, 1, (char *) "a", 1, LANG_SYS_CODESET, LANG_SYS_COLLATION);

    intl_char_count ((unsigned char *) str, str_len, (codeset_t) codeset, &char_count);

    if (char_count > precision)
      {
	/*
	 * May be a violation, but first we have to check for trailing pad
	 * characters that might allow us to successfully truncate the
	 * thing.
	 */
	int safe;
	const char *p;
	int truncate_size;

	intl_char_size ((unsigned char *) str, precision, (codeset_t) codeset, &truncate_size);

	for (p = &str[truncate_size], safe = 1; p < &str[str_len]; p++)
	  {
	    if (*p != ' ')
	      {
		safe = 0;
		break;
	      }
	  }
	if (safe)
	  {
	    str_len = truncate_size;
	  }
	else
	  {
	    /*
	     * It's a genuine violation; raise an error.
	     */
	    // TODO CBRD-21654 handle error
	  }
      }

    val->domain.char_info.length = char_count;
    val->data.ch.info.style = MEDIUM_STRING;
    val->data.ch.info.is_max_string = (unsigned char) false;
    val->data.ch.info.compressed_need_clear = (unsigned char) false;
    val->data.ch.medium.size = str_len;
    val->data.ch.medium.buf = (char *) str;
    val->data.ch.medium.compressed_buf = NULL;
    val->data.ch.medium.compressed_size = 0;
  }

  void
  to_db_varchar (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int char_count = 0;
    int str_len = (int) strlen (str);

    assert (domain != NULL);

    int precision = domain->precision;
    unsigned char codeset = domain->codeset;

    db_make_varchar (val, 1, (char *) "a", 1, LANG_SYS_CODESET, LANG_SYS_COLLATION);

    intl_char_count ((unsigned char *) str, str_len, (codeset_t) codeset, &char_count);

    if (char_count > precision)
      {
	/*
	 * May be a violation, but first we have to check for trailing pad
	 * characters that might allow us to successfully truncate the
	 * thing.
	 */
	int safe;
	const char *p;
	int truncate_size;

	intl_char_size ((unsigned char *) str, precision, (codeset_t) codeset, &truncate_size);
	for (p = &str[truncate_size], safe = 1; p < &str[str_len]; p++)
	  {
	    if (*p != ' ')
	      {
		safe = 0;
		break;
	      }
	  }
	if (safe)
	  {
	    str_len = truncate_size;
	  }
	else
	  {
	    /*
	     * It's a genuine violation; raise an error.
	     */
	    // TODO CBRD-21654 handle error
	  }
      }

    val->domain.char_info.length = char_count;
    val->data.ch.medium.size = str_len;
    val->data.ch.medium.buf = (char *) str;
    val->data.ch.info.style = MEDIUM_STRING;
    val->data.ch.info.is_max_string = (unsigned char) false;
    val->data.ch.info.compressed_need_clear = (unsigned char) false;
    val->data.ch.medium.compressed_buf = NULL;
    val->data.ch.medium.compressed_size = 0;
  }

  void
  to_db_string (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret = db_make_string (val, (char *) str);

    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_float (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    double d;
    char *str_ptr;

    db_make_float (val, (float) 0.0);
    d = strtod (str, &str_ptr);

    /* The ascii representation should be ok, check for overflow */
    if (str_ptr == str || OR_CHECK_FLOAT_OVERFLOW (d))
      {
	// TODO CBRD-21654 handle error
      }
    else
      {
	val->data.f = (float) d;
      }
  }

  void
  to_db_double (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    double d;
    char *str_ptr;

    db_make_double (val, (double) 0.0);
    d = strtod (str, &str_ptr);

    /* The ascii representation should be ok, check for overflow */
    if (str_ptr == str || OR_CHECK_DOUBLE_OVERFLOW (d))
      {
	// TODO CBRD-21654 handle error
      }
    else
      {
	val->data.d = d;
      }
  }

  void
  to_db_date (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;

    db_make_date (val, 1, 1, 1996);

    ret = db_string_to_date (str, & (val->data.date));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_time (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;

    db_make_time (val, 0, 0, 0);

    ret = db_string_to_time (str, & (val->data.time));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_timeltz (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;
    DB_TIMETZ timetz;

    timetz.time = 0;
    timetz.tz_id = 0;

    db_make_timeltz (val, & (timetz.time));

    ret = db_string_to_timeltz (str, & (val->data.time));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_timetz (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;
    bool has_zone;
    DB_TIMETZ timetz;

    timetz.time = 0;
    timetz.tz_id = 0;

    db_make_timetz (val, &timetz);

    ret = db_string_to_timetz (str, & (val->data.timetz), &has_zone);
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_timestamp (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;

    db_make_timestamp (val, 0);

    ret = db_string_to_timestamp (str, & (val->data.utime));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_timestampltz (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;

    db_make_timestampltz (val, 0);

    ret = db_string_to_timestampltz (str, & (val->data.utime));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_timestamptz (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;
    bool has_zone;
    DB_TIMESTAMPTZ timestamptz;

    timestamptz.timestamp = 0;
    timestamptz.tz_id = 0;

    db_make_timestamptz (val, &timestamptz);

    ret = db_string_to_timestamptz (str, & (val->data.timestamptz), &has_zone);
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_datetime (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;
    DB_DATETIME datetime;

    db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);
    db_make_datetime (val, &datetime);

    ret = db_string_to_datetime (str, & (val->data.datetime));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_datetimeltz (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;
    DB_DATETIME datetime;

    db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);
    db_make_datetimeltz (val,  &datetime);

    ret = db_string_to_datetimeltz (str, & (val->data.datetime));
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_datetimetz (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    int ret;
    bool has_zone;
    DB_DATETIME datetime;
    DB_DATETIMETZ datetimetz;

    db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);

    datetimetz.datetime = datetime;
    datetimetz.tz_id = 0;

    db_make_datetimetz (val, &datetimetz);

    ret = db_string_to_datetimetz (str, & (val->data.datetimetz), &has_zone);
    if (ret != NO_ERROR)
      {
	// TODO CBRD-21654 handle error
      }
  }

  void
  to_db_json (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    JSON_DOC *document = NULL;
    char *json_body = NULL;
    int ret = NO_ERROR;

    ret = db_json_get_json_from_str (str, document);
    if (ret != NO_ERROR)
      {
	assert (document == NULL);
	// TODO CBRD-21654 handle error
      }

    json_body = db_private_strdup (NULL, str);

    db_make_json (val, json_body, document, true);
  }

  void
  to_db_monetary (const char *str, const tp_domain_t *domain, db_value_t *val)
  {
    char *str_ptr;
    double amt;
    int symbol_size = 0;
    size_t str_len = strlen (str);
    DB_CURRENCY currency_type = DB_CURRENCY_NULL;
    const unsigned char *p = (const unsigned char *) str;
    const unsigned char *token = (const unsigned char *) str;

    if (str_len >= 2 && intl_is_currency_symbol ((const char *) p, &currency_type, &symbol_size,
	(CURRENCY_CHECK_MODE) (CURRENCY_CHECK_MODE_ESC_ISO | CURRENCY_CHECK_MODE_GRAMMAR)))
      {
	token += symbol_size;
      }

    if (currency_type == DB_CURRENCY_NULL)
      {
	currency_type = DB_CURRENCY_DOLLAR;
      }

    amt = strtod ((const char *) token, &str_ptr);

    if (str == str_ptr || OR_CHECK_DOUBLE_OVERFLOW (amt))
      {
	// TODO CBRD-21654 handle error
      }
    else
      {
	if (db_make_monetary (val, currency_type, amt) != NO_ERROR)
	  {
	    // TODO CBRD-21654 handle error
	  }
      }
  }
}