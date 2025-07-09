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
 * load_db_value_converter.cpp - conversion from string to db_value
 */

#include "load_db_value_converter.hpp"

#include "db_date.h"
#include "db_json.hpp"
#include "dbtype.h"
#include "language_support.h"
#include "load_class_registry.hpp"
#include "numeric_opfunc.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "string_opfunc.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring> // for std::memcpy
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

const std::size_t MAX_DIGITS_FOR_SHORT = 5;   // default for 16 bit signed shorts: 32767 (0x7FFF)
const std::size_t MAX_DIGITS_FOR_INT = 10;    // default for 32 bit signed integers: 2147483647 (0x7FFFFFFF)
const std::size_t MAX_DIGITS_FOR_BIGINT = 19; // default for 64 bit signed big integers: 9223372036854775807
// (0x7FFFFFFFFFFFFFFF)

namespace cubload
{
  // TODO CBRD-21654 reuse conversion function in load_sa_loader.cpp source file
  int mismatch (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_null (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_short (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_int (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_int_set (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_bigint (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_generic_char (DB_TYPE type, const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_char (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_varchar (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_make_nchar (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_make_varnchar (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_string (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_float (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_double (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_numeric (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_date (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_time (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_timestamp (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_timestampltz (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_timestamptz (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_datetime (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_datetimeltz (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_datetimetz (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_json (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_monetary (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_varbit_from_bin_str (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_varbit_from_hex_str (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_elo_ext (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_elo_int (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_int_generic (const char *str, const size_t str_size, const attribute *attr, db_value *val);

  using conv_setters = std::array<std::array<conv_func, NUM_LDR_TYPES>, NUM_DB_TYPES>;

  static conv_setters init_setters ();
  static conv_setters setters = init_setters ();

  static conv_setters
  init_setters ()
  {
    conv_setters setters_;

    for (int i = 0; i < NUM_DB_TYPES; i++)
      {
	for (int j = 0; j < NUM_LDR_TYPES; j++)
	  {
	    setters_[i][j] = &mismatch;
	  }
      }

    for (int i = 0; i < NUM_DB_TYPES; i++)
      {
	setters_[i][LDR_NULL] = &to_db_null;
      }

    // used within collection
    DB_TYPE set_types[3] = {DB_TYPE_SET, DB_TYPE_MULTISET, DB_TYPE_SEQUENCE};
    for (DB_TYPE &set_type : set_types)
      {
	setters_[set_type][LDR_INT] = &to_db_int_set;
	setters_[set_type][LDR_STR] = &to_db_string;
	setters_[set_type][LDR_NSTR] = &to_db_make_varnchar;
	setters_[set_type][LDR_NUMERIC] = &to_db_numeric;
	setters_[set_type][LDR_DOUBLE] = &to_db_double;
	setters_[set_type][LDR_FLOAT] = &to_db_float;
	setters_[set_type][LDR_DATE] = &to_db_date;
	setters_[set_type][LDR_TIME] = &to_db_time;
	setters_[set_type][LDR_TIMESTAMP] = &to_db_timestamp;
	setters_[set_type][LDR_TIMESTAMPLTZ] = &to_db_timestampltz;
	setters_[set_type][LDR_TIMESTAMPTZ] = &to_db_timestamptz;
	setters_[set_type][LDR_DATETIME] = &to_db_datetime;
	setters_[set_type][LDR_DATETIMELTZ] = &to_db_datetimeltz;
	setters_[set_type][LDR_DATETIMETZ] = &to_db_datetimetz;
	setters_[set_type][LDR_BSTR] = &to_db_varbit_from_bin_str;
	setters_[set_type][LDR_XSTR] = &to_db_varbit_from_hex_str;
	setters_[set_type][LDR_MONETARY] = &to_db_monetary;
	setters_[set_type][LDR_ELO_EXT] = &to_db_elo_ext;
	setters_[set_type][LDR_ELO_INT] = &to_db_elo_int;
	setters_[set_type][LDR_JSON] = &to_db_json;
      }

    setters_[DB_TYPE_CHAR][LDR_STR] = &to_db_char;
    setters_[DB_TYPE_NCHAR][LDR_NSTR] = &to_db_make_nchar;

    setters_[DB_TYPE_VARCHAR][LDR_STR] = &to_db_varchar;
    setters_[DB_TYPE_VARNCHAR][LDR_NSTR] = &to_db_make_varnchar;

    setters_[DB_TYPE_BIGINT][LDR_INT] = &to_db_bigint;
    setters_[DB_TYPE_INTEGER][LDR_INT] = &to_db_int;
    setters_[DB_TYPE_SHORT][LDR_INT] = &to_db_short;

    setters_[DB_TYPE_FLOAT][LDR_INT] = &to_db_float;
    setters_[DB_TYPE_FLOAT][LDR_NUMERIC] = &to_db_float;
    setters_[DB_TYPE_FLOAT][LDR_DOUBLE] = &to_db_float;
    setters_[DB_TYPE_FLOAT][LDR_FLOAT] = &to_db_float;

    setters_[DB_TYPE_DOUBLE][LDR_INT] = &to_db_double;
    setters_[DB_TYPE_DOUBLE][LDR_NUMERIC] = &to_db_double;
    setters_[DB_TYPE_DOUBLE][LDR_DOUBLE] = &to_db_double;
    setters_[DB_TYPE_DOUBLE][LDR_FLOAT] = &to_db_double;

    setters_[DB_TYPE_NUMERIC][LDR_INT] = &to_int_generic;
    setters_[DB_TYPE_NUMERIC][LDR_NUMERIC] = &to_db_numeric;
    setters_[DB_TYPE_NUMERIC][LDR_DOUBLE] = &to_db_double;
    setters_[DB_TYPE_NUMERIC][LDR_FLOAT] = &to_db_double;

    setters_[DB_TYPE_BIT][LDR_BSTR] = &to_db_varbit_from_bin_str;
    setters_[DB_TYPE_BIT][LDR_XSTR] = &to_db_varbit_from_hex_str;
    setters_[DB_TYPE_VARBIT][LDR_BSTR] = &to_db_varbit_from_bin_str;
    setters_[DB_TYPE_VARBIT][LDR_XSTR] = &to_db_varbit_from_hex_str;

    setters_[DB_TYPE_BLOB][LDR_ELO_EXT] = &to_db_elo_ext;
    setters_[DB_TYPE_BLOB][LDR_ELO_INT] = &to_db_elo_int;
    setters_[DB_TYPE_CLOB][LDR_ELO_EXT] = &to_db_elo_ext;
    setters_[DB_TYPE_CLOB][LDR_ELO_INT] = &to_db_elo_int;

    setters_[DB_TYPE_JSON][LDR_STR] = &to_db_json;

    setters_[DB_TYPE_MONETARY][LDR_INT] = &to_db_monetary;
    setters_[DB_TYPE_MONETARY][LDR_NUMERIC] = &to_db_monetary;
    setters_[DB_TYPE_MONETARY][LDR_DOUBLE] = &to_db_monetary;
    setters_[DB_TYPE_MONETARY][LDR_FLOAT] = &to_db_monetary;
    setters_[DB_TYPE_MONETARY][LDR_MONETARY] = &to_db_monetary;

    setters_[DB_TYPE_DATE][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_TIME][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_DATETIME][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_DATETIMETZ][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_DATETIMELTZ][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_TIMESTAMP][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_TIMESTAMPTZ][LDR_STR] = &to_db_string;
    setters_[DB_TYPE_TIMESTAMPLTZ][LDR_STR] = &to_db_string;

    setters_[DB_TYPE_DATE][LDR_DATE] = &to_db_date;
    setters_[DB_TYPE_TIME][LDR_TIME] = &to_db_time;
    setters_[DB_TYPE_DATETIME][LDR_DATETIME] = &to_db_datetime;
    setters_[DB_TYPE_DATETIMETZ][LDR_DATETIMETZ] = &to_db_datetimetz;
    setters_[DB_TYPE_DATETIMELTZ][LDR_DATETIMELTZ] = &to_db_datetimeltz;
    setters_[DB_TYPE_TIMESTAMP][LDR_TIMESTAMP] = &to_db_timestamp;
    setters_[DB_TYPE_TIMESTAMPTZ][LDR_TIMESTAMPTZ] = &to_db_timestamptz;
    setters_[DB_TYPE_TIMESTAMPLTZ][LDR_TIMESTAMPLTZ] = &to_db_timestampltz;

    setters_[DB_TYPE_ENUMERATION][LDR_INT] = &to_db_int;
    setters_[DB_TYPE_ENUMERATION][LDR_STR] = &to_db_string;

    return setters_;
  }

  conv_func &
  get_conv_func (const data_type ldr_type, const DB_TYPE db_type)
  {
    conv_func &c_func = setters[db_type][ldr_type];
    return c_func;
  }

  int
  mismatch (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    int error_code = ER_OBJ_DOMAIN_CONFLICT;
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());
    return error_code;
  }

  int
  to_db_null (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    if (attr->get_repr ().is_notnull)
      {
	return ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
      }
    else
      {
	return db_make_null (val);
      }
  }

  int
  to_db_short (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    char *str_ptr;

    db_make_short (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (str_size > MAX_DIGITS_FOR_SHORT)
      {
	double d;
	d = strtod (str, &str_ptr);

	if (str_ptr == str || OR_CHECK_SHORT_OVERFLOW (d))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_SHORT));
	    return ER_IT_DATA_OVERFLOW;
	  }
	else
	  {
	    val->data.sh = (short) std::round (d);
	  }
      }
    else
      {
	int i_val;
	int error_code = parse_int (&i_val, str, 10);
	if (error_code != 0)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_SHORT));
	    return ER_IT_DATA_OVERFLOW;
	  }
	val->data.sh = (short) i_val;
      }

    return NO_ERROR;
  }

  int
  to_db_int (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    char *str_ptr;

    db_make_int (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (str_size < MAX_DIGITS_FOR_INT || (str_size == MAX_DIGITS_FOR_INT && (str[0] == '0' || str[0] == '1')))
      {
	int error_code = parse_int (&val->data.i, str, 10);
	if (error_code != 0)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_INTEGER));
	    return ER_IT_DATA_OVERFLOW;
	  }
      }
    else
      {
	double d;
	d = strtod (str, &str_ptr);

	if (str_ptr == str || OR_CHECK_INT_OVERFLOW (d))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_INTEGER));
	    return ER_IT_DATA_OVERFLOW;
	  }
	else
	  {
	    val->data.i = (int) std::round (d);
	  }
      }

    return NO_ERROR;
  }

  /**
   * Used in case of collection when if int overflows fallback to bigint
   */
  int
  to_db_int_set (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    int error_code = to_db_int (str, str_size, attr, val);
    if (error_code == ER_IT_DATA_OVERFLOW)
      {
	// if there is overflow on integer, try as bigint
	er_clear ();
	error_code = to_db_bigint (str, str_size, attr, val);
      }

    return error_code;
  }

  int
  to_db_bigint (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    db_make_bigint (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (str_size < MAX_DIGITS_FOR_BIGINT || (str_size == MAX_DIGITS_FOR_BIGINT && str[0] != '9'))
      {
	int error_code = parse_bigint (&val->data.bigint, str, 10);
	if (error_code != 0)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_BIGINT));
	    return ER_IT_DATA_OVERFLOW;
	  }
      }
    else
      {
	DB_NUMERIC num;
	DB_BIGINT tmp_bigint;

	numeric_coerce_dec_str_to_num (str, num.d.buf);
	if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint) != NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_BIGINT));
	    return ER_IT_DATA_OVERFLOW;
	  }
	else
	  {
	    val->data.bigint = tmp_bigint;
	  }
      }

    return NO_ERROR;
  }

  int
  to_db_generic_char (DB_TYPE type, const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    int char_count = 0;
    int str_len = (int) str_size;
    int error = NO_ERROR;
    const tp_domain &domain = attr->get_domain ();
    int precision = domain.precision;
    INTL_CODESET codeset = (INTL_CODESET) domain.codeset;

    intl_char_count ((unsigned char *) str, str_len, codeset, &char_count);

    if (char_count > precision)
      {
	/*
	 * May be a violation, but first we have to check for trailing pad
	 * characters that might allow us to successfully truncate the
	 * thing.
	 */
	const char *p;
	int truncate_size;

	intl_char_size ((unsigned char *) str, precision, codeset, &truncate_size);

	p = intl_skip_spaces (&str[truncate_size],  &str[str_len], codeset);
	if (p >= &str[str_len])
	  {
	    str_len = truncate_size;
	  }
	else
	  {
	    /*
	     * It's a genuine violation; raise an error.
	     */
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (type));
	    return ER_IT_DATA_OVERFLOW;
	  }
      }

    error = db_value_domain_init (val, type, char_count, 0);
    if (error == NO_ERROR)
      {
	error = db_make_db_char (val, codeset, domain.collation_id, str, str_len);
      }

    return error;
  }

  int
  to_db_char (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return to_db_generic_char (DB_TYPE_CHAR, str, str_size, attr, val);
  }

  int
  to_db_varchar (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return to_db_generic_char (DB_TYPE_VARCHAR, str, str_size, attr, val);
  }

  int to_db_make_nchar (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return to_db_generic_char (DB_TYPE_NCHAR, str, str_size, attr, val);
  }

  int to_db_make_varnchar (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return to_db_generic_char (DB_TYPE_VARNCHAR, str, str_size, attr, val);
  }

  int
  to_db_string (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return db_make_string (val, str);
  }

  int
  to_db_float (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    double d;
    char *str_ptr;

    db_make_float (val, (float) 0.0);
    d = strtod (str, &str_ptr);

    /* The ascii representation should be ok, check for overflow */
    if (str_ptr == str || OR_CHECK_FLOAT_OVERFLOW (d))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, attr->get_domain ().type->get_name ());
	return ER_IT_DATA_OVERFLOW;
      }
    else
      {
	val->data.f = (float) d;
      }

    return NO_ERROR;
  }

  int
  to_db_double (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    double d;
    char *str_ptr;

    db_make_double (val, (double) 0.0);
    d = strtod (str, &str_ptr);

    /* The ascii representation should be ok, check for overflow */
    if (str_ptr == str || OR_CHECK_DOUBLE_OVERFLOW (d))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, attr->get_domain ().type->get_name ());
	return ER_IT_DATA_OVERFLOW;
      }
    else
      {
	val->data.d = d;
      }

    return NO_ERROR;
  }

  int
  to_db_numeric (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    int precision = (int) str_size - 1 - (str[0] == '+' || str[0] == '-');
    int scale = (int) str_size - (int) strcspn (str, ".") - 1;

    int error_code = db_value_domain_init (val, DB_TYPE_NUMERIC, precision, scale);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    return db_value_put (val, DB_TYPE_C_CHAR, (void *) str, (int) str_size);
  }

  int
  to_db_date (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    db_make_date (val, 1, 1, 1996);

    return db_string_to_date (str, &val->data.date);
  }

  int
  to_db_time (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    db_make_time (val, 0, 0, 0);

    return db_string_to_time (str, &val->data.time);
  }

  int
  to_db_timestamp (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    db_make_timestamp (val, 0);

    return  db_string_to_timestamp (str, &val->data.utime);
  }

  int
  to_db_timestampltz (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    db_make_timestampltz (val, 0);

    return db_string_to_timestampltz (str, &val->data.utime);
  }

  int
  to_db_timestamptz (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    bool has_zone;
    DB_TIMESTAMPTZ timestamptz;

    timestamptz.timestamp = 0;
    timestamptz.tz_id = 0;

    db_make_timestamptz (val, &timestamptz);

    return db_string_to_timestamptz (str, &val->data.timestamptz, &has_zone);
  }

  int
  to_db_datetime (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    DB_DATETIME datetime;

    db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);
    db_make_datetime (val, &datetime);

    return db_string_to_datetime (str, &val->data.datetime);
  }

  int
  to_db_datetimeltz (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    DB_DATETIME datetime;

    db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);
    db_make_datetimeltz (val, &datetime);

    return db_string_to_datetimeltz (str, &val->data.datetime);
  }

  int
  to_db_datetimetz (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    bool has_zone;
    DB_DATETIME datetime;
    DB_DATETIMETZ datetimetz;

    db_datetime_encode (&datetime, 1, 1, 1996, 0, 0, 0, 0);

    datetimetz.datetime = datetime;
    datetimetz.tz_id = 0;

    db_make_datetimetz (val, &datetimetz);

    return db_string_to_datetimetz (str, &val->data.datetimetz, &has_zone);
  }

  int
  to_db_json (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    JSON_DOC *document = NULL;

    int error_code = db_json_get_json_from_str (str, document, str_size);
    if (error_code != NO_ERROR)
      {
	assert (document == NULL);
	return error_code;
      }

    return db_make_json (val, document, true);
  }

  int
  to_db_monetary (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    char *str_ptr;
    double amt;
    int symbol_size = 0;
    DB_CURRENCY currency_type = DB_CURRENCY_NULL;
    const unsigned char *p = (const unsigned char *) str;
    const unsigned char *token = (const unsigned char *) str;

    if (str_size >= 2
	&& intl_is_currency_symbol ((const char *) p, &currency_type, &symbol_size,
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
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_MONETARY));
	return ER_IT_DATA_OVERFLOW;
      }
    else
      {
	return db_make_monetary (val, currency_type, amt);
      }
  }

  int
  to_db_varbit_from_bin_str (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    int error_code = NO_ERROR;
    char *bstring;
    db_value temp;
    std::size_t dest_size;
    tp_domain temp_domain, *domain_ptr = NULL;

    dest_size = (str_size + 7) / 8;

    bstring = (char *) db_private_alloc (NULL, dest_size + 1);
    if (bstring == NULL)
      {
	error_code = er_errid ();
	assert (error_code != NO_ERROR);

	return error_code;
      }

    if (qstr_bit_to_bin (bstring, (int) dest_size, const_cast<char *> (str), (int) str_size) != (int) str_size)
      {
	db_private_free_and_init (NULL, bstring);

	error_code = ER_OBJ_DOMAIN_CONFLICT;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());

	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	return error_code;
      }

    error_code = db_make_varbit (&temp, TP_FLOATING_PRECISION_VALUE, bstring, (int) str_size);
    if (error_code != NO_ERROR)
      {
	db_private_free_and_init (NULL, bstring);
	return error_code;
      }

    temp.need_clear = true;

    const tp_domain &domain = attr->get_domain ();
    error_code = db_value_domain_init (val, domain.type->id, domain.precision, domain.scale);
    if (error_code != NO_ERROR)
      {
	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	db_value_clear (&temp);
	return error_code;
      }

    domain_ptr = tp_domain_resolve_value (val, &temp_domain);

    if (tp_value_cast (&temp, val, domain_ptr, false) != DOMAIN_COMPATIBLE)
      {
	db_value_clear (val);

	error_code = ER_OBJ_DOMAIN_CONFLICT;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());

	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	return error_code;
      }
    db_value_clear (&temp);

    return error_code;
  }

  int
  to_db_varbit_from_hex_str (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    int error_code = NO_ERROR;
    char *bstring = NULL;
    db_value temp;
    std::size_t dest_size;
    tp_domain *domain_ptr, temp_domain;

    db_make_null (&temp);

    dest_size = (str_size + 1) / 2;

    bstring = (char *) db_private_alloc (NULL, dest_size + 1);
    if (bstring == NULL)
      {
	error_code = er_errid ();
	assert (error_code != NO_ERROR);

	return error_code;
      }

    if (qstr_hex_to_bin (bstring, (int) dest_size, const_cast<char *> (str), (int) str_size) != (int) str_size)
      {
	db_private_free_and_init (NULL, bstring);

	error_code = ER_OBJ_DOMAIN_CONFLICT;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());

	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	return error_code;
      }

    error_code = db_make_varbit (&temp, TP_FLOATING_PRECISION_VALUE, bstring, ((int) str_size) * 4);
    if (error_code != NO_ERROR)
      {
	db_private_free_and_init (NULL, bstring);
	return error_code;
      }

    temp.need_clear = true;

    const tp_domain &domain = attr->get_domain ();
    error_code = db_value_domain_init (val, domain.type->id, domain.precision, domain.scale);
    if (error_code != NO_ERROR)
      {
	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	db_value_clear (&temp);
	return error_code;
      }

    domain_ptr = tp_domain_resolve_value (val, &temp_domain);
    if (tp_value_cast (&temp, val, domain_ptr, false))
      {
	db_value_clear (val);

	error_code = ER_OBJ_DOMAIN_CONFLICT;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());

	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	return error_code;
      }
    db_value_clear (&temp);

    return error_code;
  }

  int
  to_db_elo_ext (const char *str,  const size_t str_size, const attribute *attr, db_value *val)
  {
    db_elo elo;
    INT64 size;
    DB_TYPE type;
    size_t new_len;
    char *locator = NULL;
    char *meta_data = NULL;
    int error_code = NO_ERROR;
    char *str_start_ptr = NULL, *str_end_ptr = NULL;
    const char *meta_start_ptr = NULL, *meta_end_ptr = NULL;
    const char *locator_start_ptr = NULL, *locator_end_ptr = NULL;

    if (str[0] == '\"')
      {
	str++;
      }

    new_len = strlen (str);
    if (new_len && str[new_len - 1] == '\"')
      {
	new_len--;
      }

    assert (new_len > 0);
    assert (str[0] == 'B' || str[0] == 'C');

    if (str[0] == 'B')
      {
	type = DB_TYPE_BLOB;
      }
    else
      {
	type = DB_TYPE_CLOB;
      }

    /* size */
    str_start_ptr = (char *) (str + 1);
    str_end_ptr = strchr (str_start_ptr, '|');
    if (str_end_ptr == NULL || str_end_ptr - str_start_ptr == 0)
      {
	error_code = ER_LDR_ELO_INPUT_FILE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, str);

	return error_code;
      }

    /* locator */
    locator_start_ptr = str_end_ptr + 1;
    locator_end_ptr = strchr (locator_start_ptr, '|');
    if (locator_end_ptr == NULL || locator_end_ptr - locator_start_ptr == 0)
      {
	error_code = ER_LDR_ELO_INPUT_FILE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, str);

	return error_code;
      }

    /* meta_data */
    meta_end_ptr = meta_start_ptr = locator_end_ptr + 1;
    while (*meta_end_ptr)
      {
	meta_end_ptr++;
      }

    int ret = str_to_int64 (&size, &str_end_ptr, str_start_ptr, 10);
    if (ret != 0 || size < 0)
      {
	error_code = ER_LDR_ELO_INPUT_FILE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, str);

	return error_code;
      }

    size_t locator_size = locator_end_ptr - locator_start_ptr;
    locator = (char *) db_private_alloc (NULL, locator_size + 1);
    if (locator == NULL)
      {
	error_code = er_errid ();
	assert (error_code != NO_ERROR);

	return error_code;
      }

    std::memcpy (locator, locator_start_ptr, locator_size);
    locator[locator_size] = '\0';

    size_t meta_data_size = meta_end_ptr - meta_start_ptr;
    if (meta_data_size > 0)
      {
	meta_data = (char *) db_private_alloc (NULL, meta_data_size + 1);
	if (meta_data == NULL)
	  {
	    db_private_free_and_init (NULL, locator);

	    error_code = er_errid ();
	    assert (error_code != NO_ERROR);

	    return error_code;
	  }

	std::memcpy (meta_data, meta_start_ptr, meta_data_size);
	meta_data[meta_data_size] = '\0';
      }

    elo_init_structure (&elo);
    elo.size = size;
    elo.locator = locator;
    elo.meta_data = meta_data;
    elo.type = ELO_FBO;

    error_code = db_make_elo (val, type, &elo);
    if (error_code != NO_ERROR)
      {
	db_private_free_and_init (NULL, locator);
	db_private_free_and_init (NULL, meta_data);

	return error_code;
      }

    val->need_clear = true;

    return NO_ERROR;
  }

  int
  to_db_elo_int (const char *str,  const size_t str_size, const attribute *attr, db_value *val)
  {
    /* not implemented. should not be called */
    assert (0);
    return ER_FAILED;
  }

  int
  to_int_generic (const char *str,  const size_t str_size, const attribute *attr, db_value *val)
  {
    int error_code = NO_ERROR;

    /*
     * Watch out for really long digit strings that really are being
     * assigned into a DB_TYPE_NUMERIC attribute; they can hold more than a
     * standard integer can, and calling atol() on that string will lose
     * data.
     * Is there some better way to test for this condition?
     */
    if (str_size < MAX_DIGITS_FOR_INT || (str_size == MAX_DIGITS_FOR_INT && (str[0] == '0' || str[0] == '1')))
      {
	db_make_int (val, 0);
	error_code = parse_int (&val->data.i, str, 10);
	if (error_code != NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_INTEGER));
	    return ER_IT_DATA_OVERFLOW;
	  }
      }
    else if (str_size < MAX_DIGITS_FOR_BIGINT || (str_size == MAX_DIGITS_FOR_BIGINT && str[0] != '9'))
      {
	db_make_bigint (val, 0);
	error_code = parse_bigint (&val->data.bigint, str, 10);
	if (error_code != NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_BIGINT));
	    return ER_IT_DATA_OVERFLOW;
	  }
      }
    else
      {
	DB_NUMERIC num;
	DB_BIGINT tmp_bigint;

	numeric_coerce_dec_str_to_num (str, num.d.buf);
	if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint) != NO_ERROR)
	  {
	    error_code = db_value_domain_init (val, DB_TYPE_NUMERIC, (int) str_size, 0);
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		return error_code;
	      }

	    error_code = db_value_put (val, DB_TYPE_C_CHAR, (char *) str, (int) str_size);
	    if (error_code != NO_ERROR)
	      {
		ASSERT_ERROR ();
		return error_code;
	      }
	  }
	else
	  {
	    db_make_bigint (val, tmp_bigint);
	  }
      }

    return NO_ERROR;
  }
}
