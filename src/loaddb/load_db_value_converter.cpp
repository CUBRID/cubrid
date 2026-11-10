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
#include "set_object.h"
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
  //
  // CBRD-27337 took most of it. The server loader reads nothing but this table, and on the
  // standalone side an attribute goes through ldr_convert_value () and a collection element
  // through ldr_convert_elem_value (). What is left is the %constructor argument slot, which
  // still hands every type to elem_converter[] in load_sa_loader.cpp, so ldr_date_elem (),
  // ldr_time_elem (), ldr_bstr_elem (), ldr_xstr_elem (), ldr_float_elem (), ldr_double_elem ()
  // and ldr_int_elem () are kept alive by that one call. Object references stay there on
  // purpose - this table has no entry for them.
  int mismatch (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_null (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_short (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_int (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_bigint (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_generic_char (DB_TYPE type, const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_char (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_varchar (const char *str, const size_t str_size, const attribute *attr, db_value *val);

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
  int to_elem_varbit_from_bin_str (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_elem_varbit_from_hex_str (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_elo_ext (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_db_elo_int (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_int_generic (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  int to_elem_null (const char *str, const size_t str_size, const attribute *attr, db_value *val);
  template <data_type LDR>
  int to_db_fallback (const char *str, const size_t str_size, const attribute *attr, db_value *val);

  using conv_setters = std::array<std::array<conv_func, NUM_LDR_TYPES>, NUM_DB_TYPES>;
  using elem_conv_setters = std::array<conv_func, NUM_LDR_TYPES>;

  static conv_setters init_setters ();
  static conv_setters setters = init_setters ();

  static elem_conv_setters init_elem_setters ();
  static elem_conv_setters elem_setters = init_elem_setters ();

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

    /*
     * Allow loaddb to support the typecasts implicitly permitted by INSERT. A literal
     * whose target has no converter of its own falls back to its natural type plus the
     * engine's cast, instead of being refused by the table. The manual's implicit
     * conversion table marks these as allowed and INSERT takes them - DATE'2020-01-01'
     * into a VARCHAR column stores '01/01/2020', B'1010' stores 'a'.
     *
     * What stays out and why. Object references, collections, ^u and ^c are not values
     * the engine casts between domains. The LOB literals are not either, and
     * to_db_elo_int () is not even implemented. LDR_JSON is never produced: the parser
     * has no rule for it, and a JSON column takes its value as LDR_STR.
     */
    for (int i = 0; i < NUM_DB_TYPES; i++)
      {
	setters_[i][LDR_INT] = &to_db_fallback<LDR_INT>;
	setters_[i][LDR_STR] = &to_db_fallback<LDR_STR>;
	setters_[i][LDR_NUMERIC] = &to_db_fallback<LDR_NUMERIC>;
	setters_[i][LDR_DOUBLE] = &to_db_fallback<LDR_DOUBLE>;
	setters_[i][LDR_FLOAT] = &to_db_fallback<LDR_FLOAT>;
	setters_[i][LDR_DATE] = &to_db_fallback<LDR_DATE>;
	setters_[i][LDR_TIME] = &to_db_fallback<LDR_TIME>;
	setters_[i][LDR_TIMESTAMP] = &to_db_fallback<LDR_TIMESTAMP>;
	setters_[i][LDR_TIMESTAMPLTZ] = &to_db_fallback<LDR_TIMESTAMPLTZ>;
	setters_[i][LDR_TIMESTAMPTZ] = &to_db_fallback<LDR_TIMESTAMPTZ>;
	setters_[i][LDR_DATETIME] = &to_db_fallback<LDR_DATETIME>;
	setters_[i][LDR_DATETIMELTZ] = &to_db_fallback<LDR_DATETIMELTZ>;
	setters_[i][LDR_DATETIMETZ] = &to_db_fallback<LDR_DATETIMETZ>;
	setters_[i][LDR_BSTR] = &to_db_fallback<LDR_BSTR>;
	setters_[i][LDR_XSTR] = &to_db_fallback<LDR_XSTR>;
	setters_[i][LDR_MONETARY] = &to_db_fallback<LDR_MONETARY>;
      }

    setters_[DB_TYPE_CHAR][LDR_STR] = &to_db_char;

    setters_[DB_TYPE_VARCHAR][LDR_STR] = &to_db_varchar;

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

    setters_[DB_TYPE_NUMERIC][LDR_INT] = &to_int_generic;
    setters_[DB_TYPE_NUMERIC][LDR_NUMERIC] = &to_db_numeric;
    setters_[DB_TYPE_NUMERIC][LDR_DOUBLE] = &to_db_double;

    setters_[DB_TYPE_BIT][LDR_BSTR] = &to_db_varbit_from_bin_str;
    setters_[DB_TYPE_BIT][LDR_XSTR] = &to_db_varbit_from_hex_str;
    setters_[DB_TYPE_VARBIT][LDR_BSTR] = &to_db_varbit_from_bin_str;
    setters_[DB_TYPE_VARBIT][LDR_XSTR] = &to_db_varbit_from_hex_str;

    setters_[DB_TYPE_BLOB][LDR_ELO_EXT] = &to_db_elo_ext;
    setters_[DB_TYPE_BLOB][LDR_ELO_INT] = &to_db_elo_int;
    setters_[DB_TYPE_CLOB][LDR_ELO_EXT] = &to_db_elo_ext;
    setters_[DB_TYPE_CLOB][LDR_ELO_INT] = &to_db_elo_int;

    setters_[DB_TYPE_JSON][LDR_STR] = &to_db_json;

    /*
     * Only the $ literal comes here. A plain number keeps the fallback above, which
     * builds the literal's own natural type and lets the engine cast it - the same
     * path INSERT takes, and the reason a number has to stay off to_db_monetary ():
     * that one reads every literal with strtod (), and strtod ("-0") is IEEE negative
     * zero, so -0 and -0.0 were stored as $-0.00 where INSERT stores $0.00. INSERT
     * keeps the sign for -0e0, because that literal really is a double; deferring to
     * the engine reproduces both answers instead of picking one.
     */
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

  /*
   * A collection element is converted to the literal's natural type. The collection
   * checks it against its own set domain when the element is added, which is the only
   * thing that works for a heterogeneous collection, where there is no single element
   * domain to look up. This is why the varbit entries below stop at
   * make_varbit_value () while the attribute table casts into the attribute domain.
   */
  static elem_conv_setters
  init_elem_setters ()
  {
    elem_conv_setters setters_;

    for (int i = 0; i < NUM_LDR_TYPES; i++)
      {
	setters_[i] = &mismatch;
      }

    setters_[LDR_NULL] = &to_elem_null;
    setters_[LDR_INT] = &to_int_generic;
    setters_[LDR_STR] = &to_db_string;
    setters_[LDR_NUMERIC] = &to_db_numeric;
    setters_[LDR_DOUBLE] = &to_db_double;
    setters_[LDR_FLOAT] = &to_db_float;
    setters_[LDR_DATE] = &to_db_date;
    setters_[LDR_TIME] = &to_db_time;
    setters_[LDR_TIMESTAMP] = &to_db_timestamp;
    setters_[LDR_TIMESTAMPLTZ] = &to_db_timestampltz;
    setters_[LDR_TIMESTAMPTZ] = &to_db_timestamptz;
    setters_[LDR_DATETIME] = &to_db_datetime;
    setters_[LDR_DATETIMELTZ] = &to_db_datetimeltz;
    setters_[LDR_DATETIMETZ] = &to_db_datetimetz;
    setters_[LDR_BSTR] = &to_elem_varbit_from_bin_str;
    setters_[LDR_XSTR] = &to_elem_varbit_from_hex_str;
    setters_[LDR_MONETARY] = &to_db_monetary;
    setters_[LDR_ELO_EXT] = &to_db_elo_ext;
    setters_[LDR_ELO_INT] = &to_db_elo_int;
    setters_[LDR_JSON] = &to_db_json;

    return setters_;
  }

  /*
   * to_elem_null - null as a collection element
   * Note:
   *    Not to_db_null (), which applies the attribute's NOT NULL constraint.
   *    An element being null does not make the attribute null.
   */
  int
  to_elem_null (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return db_make_null (val);
  }

  /*
   * is_date_time_literal - is the token one of the date and time literals
   *    return: true for DATE'...', TIME'...', TIMESTAMP'...', DATETIME'...' and their
   *            time zone forms
   *    ldr_type(in): parser type of the token
   */
  static bool
  is_date_time_literal (const data_type ldr_type)
  {
    switch (ldr_type)
      {
      case LDR_DATE:
      case LDR_TIME:
      case LDR_TIMESTAMP:
      case LDR_TIMESTAMPLTZ:
      case LDR_TIMESTAMPTZ:
      case LDR_DATETIME:
      case LDR_DATETIMELTZ:
      case LDR_DATETIMETZ:
	return true;
      default:
	return false;
      }
  }

  /*
   * check_not_null - refuse a null the column cannot take
   *    return: NO_ERROR, or ER_OBJ_ATTRIBUTE_CANT_BE_NULL
   *    attr(in): attribute the value is going into
   *    val(in): the converted value
   * Note:
   *    A conversion can hand back a null without failing. Under
   *    oracle_style_empty_string an empty string is a null, so '' arrives here as one
   *    whichever converter built it: tp_value_cast () calls it DOMAIN_COMPATIBLE for a
   *    numeric column, and to_db_generic_char () builds a char of length zero for a
   *    CHAR or VARCHAR one. A NOT NULL column cannot take either.
   *
   *    Say so here rather than leaving it to whoever stores the value. The standalone
   *    loader's object template refuses a null a second time, but the server loader
   *    tests NOT NULL only on a NULL token, so nothing downstream would catch it and
   *    the row would be written with the constraint broken.
   *
   *    Both loaders ask this of an attribute's value, once, right after the conversion.
   *    A collection element does not come here: the constraint belongs to the column,
   *    not to what the collection holds.
   */
  int
  check_not_null (const attribute *attr, const db_value *val)
  {
    if (attr->is_not_null () && DB_IS_NULL (val))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1, attr->get_name ());
	return ER_OBJ_ATTRIBUTE_CANT_BE_NULL;
      }

    return NO_ERROR;
  }

  /*
   * fallback_convert - convert through the literal's natural type and let the engine cast
   *    return: NO_ERROR or an error code
   *    ldr_type(in): parser type of the token
   *    str(in), str_size(in): the token
   *    attr(in): attribute being set
   *    val(out): value in the attribute's domain
   * Note:
   *    The value is built with the literal's natural type and handed to tp_value_cast() against the attribute domain.
   *    The same cast SQL INSERT performs on the same literal.
   */
  static int cast_to_attr_domain (db_value *natural, const attribute *attr, db_value *val);

  static int
  fallback_convert (const data_type ldr_type, const char *str, const size_t str_size, const attribute *attr,
		    db_value *val)
  {
    db_value natural;

    db_make_null (&natural);
    db_make_null (val);

    int error_code = get_elem_conv_func (ldr_type) (str, str_size, attr, &natural);
    if (error_code != NO_ERROR)
      {
	if (is_date_time_literal (ldr_type))
	  {
	    /*
	     * A date or time literal that will not parse keeps the code this pair has
	     * always had. Only the pair whose column matches the literal - a date into a
	     * DATE column - had a converter of its own before, and that one still reports
	     * ER_DATE_CONVERSION. Every pair reached through here had none, so it was
	     * refused outright as ER_OBJ_DOMAIN_CONFLICT, and --error-control-file names
	     * codes: a file written against an earlier version lists -214 for these, and
	     * reported as the parse error instead it stops matching, so a row that used to
	     * be skipped fails the whole load.
	     */
	    db_value_clear (&natural);
	    error_code = ER_OBJ_DOMAIN_CONFLICT;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());
	  }

	return error_code;
      }

    return cast_to_attr_domain (&natural, attr, val);
  }

  /*
   * cast_to_attr_domain - cast a value of its natural type into the attribute domain
   *    return: NO_ERROR or an error code
   *    natural(in/out): the value to cast, cleared on return
   *    attr(in): attribute being set
   *    val(out): value in the attribute's domain
   * Note:
   *    The second half of fallback_convert (), shared with the binary section reader
   *    (binary_to_db_value ()), so a value read from either form of the object file is refused
   *    with the same error code and an --error-control-file names it the same way.
   */
  static int
  cast_to_attr_domain (db_value *natural, const attribute *attr, db_value *val)
  {
    const tp_domain &domain = attr->get_domain ();
    int error_code;

    error_code = db_value_domain_init (val, domain.type->id, domain.precision, domain.scale);
    if (error_code != NO_ERROR)
      {
	db_value_clear (natural);
	return error_code;
      }

    bool implicit_coercion = !TP_IS_CHAR_TYPE (domain.type->id);
    TP_DOMAIN_STATUS status = tp_value_cast (natural, val, const_cast<tp_domain *> (&domain), implicit_coercion);

    if (status != DOMAIN_COMPATIBLE)
      {
	if (status == DOMAIN_INCOMPATIBLE)
	  {
	    /*
	     * A value the column's type cannot take keeps the code it has always had
	     * here. --error-control-file names codes, so a file written against an
	     * earlier version lists ER_OBJ_DOMAIN_CONFLICT for this one; reported as the
	     * engine's ER_TP_CANT_COERCE instead, the file stops matching and a row that
	     * used to be skipped fails the whole load. The message is also the better of
	     * the two for a loader: it names the attribute, where ER_TP_CANT_COERCE names
	     * only the two domains.
	     */
	    error_code = ER_OBJ_DOMAIN_CONFLICT;
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());
	  }
	else
	  {
	    /*
	     * Let the engine name the rest, through the call INSERT makes. A cast that
	     * overflowed is not a domain conflict - INSERT says ER_IT_DATA_OVERFLOW for it -
	     * and --error-control-file filters by code, so a file naming -427 has to match the
	     * overflow it was written for.
	     */
	    error_code = tp_domain_status_er_set (status, ARG_FILE_LINE, natural, &domain);
	  }

	db_value_clear (natural);
	db_value_clear (val);

	return error_code;
      }

    db_value_clear (natural);

    return check_not_null (attr, val);
  }

  /* template for fallback_convert () */
  template <data_type LDR>
  int
  to_db_fallback (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return fallback_convert (LDR, str, str_size, attr, val);
  }

  /*
   * get_token_db_type - the type a message should name for a failed token
   *    return: DB_TYPE of the literal
   *    type(in): parser type of the token
   * Note:
   *    What the file said the token is, which is not where it was going. A message
   *    about a token that would not parse has to name the token: the attribute domain
   *    would name the column, and for a collection element it would name the
   *    collection rather than the element.
   */
  DB_TYPE
  get_token_db_type (const data_type type)
  {
    switch (type)
      {
      case LDR_INT:
	return DB_TYPE_INTEGER;
      case LDR_NUMERIC:
	return DB_TYPE_NUMERIC;
      case LDR_DOUBLE:
	return DB_TYPE_DOUBLE;
      case LDR_FLOAT:
	return DB_TYPE_FLOAT;
      case LDR_STR:
	return DB_TYPE_STRING;
      case LDR_DATE:
	return DB_TYPE_DATE;
      case LDR_TIME:
	return DB_TYPE_TIME;
      case LDR_TIMESTAMP:
	return DB_TYPE_TIMESTAMP;
      case LDR_TIMESTAMPLTZ:
	return DB_TYPE_TIMESTAMPLTZ;
      case LDR_TIMESTAMPTZ:
	return DB_TYPE_TIMESTAMPTZ;
      case LDR_DATETIME:
	return DB_TYPE_DATETIME;
      case LDR_DATETIMELTZ:
	return DB_TYPE_DATETIMELTZ;
      case LDR_DATETIMETZ:
	return DB_TYPE_DATETIMETZ;
      case LDR_BSTR:
      case LDR_XSTR:
	return DB_TYPE_BIT;
      case LDR_MONETARY:
	return DB_TYPE_MONETARY;
      case LDR_ELO_EXT:
      case LDR_ELO_INT:
	return DB_TYPE_BLOB;
      case LDR_JSON:
	return DB_TYPE_JSON;
      default:
	/* Except for LDR_NULL, the following five cannot reach here:
	 * LDR_COLLECTION, LDR_OID, LDR_CLASS_OID, LDR_SYS_USER, LDR_SYS_CLASS
	 */
	assert (type == LDR_NULL);
	return DB_TYPE_NULL;
      }
  }

  conv_func &
  get_elem_conv_func (const data_type ldr_type)
  {
    conv_func &c_func = elem_setters[ldr_type];
    return c_func;
  }

  conv_func &
  get_conv_func (const data_type ldr_type, const DB_TYPE db_type)
  {
    conv_func &c_func = setters[db_type][ldr_type];
    return c_func;
  }

  /*
   * cast_elem_to_set_domain - coerce an element into the collection's element domain
   *    return: void
   *    setdomain(in): element domain of the collection being built, or NULL when it has none
   *    val(in/out): the element, converted to the literal's natural type
   * Note:
   *    The element converters build the literal's natural type and look at no target
   *    domain, which is what a collection with no element domain needs. When the
   *    collection has one, INSERT coerces the element into it - {B'1010'} goes into a
   *    SET(VARCHAR) as {'a'} - so do the same here, or a bit string, a date, an amount
   *    and a plain number would all be refused where INSERT takes them.
   *
   *    setdomain is a list, and a collection may name several element domains. The cast
   *    takes the list whole: tp_value_cast () runs tp_domain_select () over it, which is
   *    the same choice set_add_element () makes further down, so an element that already
   *    matches one of them is not dragged into another. Walk the list here too, or an
   *    element matching any domain but the first would be cast when it has nowhere to go.
   *
   *    The cast is explicit, where fallback_convert () asks for an implicit one unless the
   *    target is a character type. The two are not the same question. That one assigns to
   *    a column; this one is the coercion INSERT performs on an element while it builds
   *    the collection, and that coercion takes a string into a BIT element - an implicit
   *    one refuses it, since TP_IMPLICIT_COERCION_NOT_ALLOWED () bars a character string
   *    from a target that is not character, date, numeric or enum.
   *
   *    A cast that does not work leaves the value alone on purpose: set_add_element ()
   *    then refuses it and reports what it refused, which is the message the loader has
   *    always printed for an element the collection cannot hold.
   */
  void
  cast_elem_to_set_domain (const tp_domain *setdomain, db_value *val)
  {
    db_value cast_val;

    if (setdomain == NULL)
      {
	return;
      }

    for (const tp_domain *d = setdomain; d != NULL; d = d->next)
      {
	if (TP_DOMAIN_TYPE (d) == db_value_type (val))
	  {
	    return;
	  }
      }

    db_make_null (&cast_val);

    if (db_value_domain_init (&cast_val, TP_DOMAIN_TYPE (setdomain), setdomain->precision,
			      setdomain->scale) != NO_ERROR)
      {
	db_value_clear (&cast_val);
	return;
      }

    if (tp_value_cast (val, &cast_val, const_cast<tp_domain *> (setdomain), false) != DOMAIN_COMPATIBLE)
      {
	db_value_clear (&cast_val);
	return;
      }

    db_value_clear (val);
    *val = cast_val;
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
    if (attr->is_not_null ())
      {
	/*
	 * Say it, do not just return it. A bare code leaves er_errid () empty, and
	 * every loader failure goes through er_filter_errid (), which reads an empty
	 * er_errid () as "nothing to report" and hands back NO_ERROR - the failure
	 * becomes a success. An ordinary attribute survives that because the object
	 * template refuses the null again further down, but a %constructor line has
	 * nothing after the conversion, so the row would go missing without a word.
	 */
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_ATTRIBUTE_CANT_BE_NULL, 1, attr->get_name ());
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

  /* An integer token with a leading sign taken off it. */
  struct signed_digits
  {
    std::size_t num_digits;     /* digits, not counting the sign */
    const char *digits;         /* the first digit */
    const char *dec_str;        /* the token as numeric_coerce_dec_str_to_num () wants it */
  };

  /*
   * overflow_type_name - the type an overflow message should name
   *    return: type name, never NULL
   *    attr(in): attribute the value was going into, or NULL
   *    built(in): the type the converter was building
   * Note:
   *    The column is the useful thing to name, so the attribute's domain is used where
   *    it is the one the value is going into. Two slots do not have one to name. An
   *    element's attribute is the collection, whose domain is the set and not the
   *    element's - naming it gave "Data overflow on data type \"set\"". And a
   *    %constructor argument, or a slot the loader walked past after an earlier error
   *    on the line, has no attribute at all: ldr_refresh_conv_att () leaves conv_att
   *    NULL wherever att is.
   *
   *    Both of those name the type the converter built, which for an element is what
   *    the element is. The informative line beside it already calls it that, and so
   *    does the set domain when it refuses one: "type double is not allowed in the
   *    domain of the set".
   */
  static const char *
  overflow_type_name (const attribute *attr, DB_TYPE built)
  {
    if (attr == NULL || TP_IS_SET_TYPE (attr->get_domain ().type->get_id ()))
      {
	return pr_type_name (built);
      }

    return attr->get_domain ().type->get_name ();
  }

  /*
   * split_leading_sign - take a leading sign off an integer token
   *    return: the token's digit count, its first digit, and the string for the long number path
   *    str(in), str_size(in): the token
   * Note:
   *    str_size counts a leading sign while MAX_DIGITS_FOR_* count digits, so the
   *    thresholds have to be tested against num_digits rather than the token length.
   *
   *    The two paths those thresholds choose between do not want the same string, which
   *    is why dec_str is separate. parse_bigint () reads either sign.
   *    numeric_coerce_dec_str_to_num () does not: it is an engine internal whose other
   *    callers hand it a bare digit string and carry the sign apart from it, so it takes
   *    a '-' and reads a '+' as a digit. Counting the sign pushed a 19 digit literal onto
   *    that path, where +1234567890123456789 came back an overflow and the line was lost -
   *    INSERT reads the sign in the grammar and stores the value.
   */
  static signed_digits
  split_leading_sign (const char *str, const size_t str_size)
  {
    const bool has_sign = (str_size > 0 && (str[0] == '+' || str[0] == '-'));
    const bool drop_sign = (has_sign && str[0] == '+');
    signed_digits tok;

    tok.num_digits = has_sign ? str_size - 1 : str_size;
    tok.digits = has_sign ? str + 1 : str;
    tok.dec_str = drop_sign ? str + 1 : str;

    return tok;
  }

  int
  to_db_bigint (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    const signed_digits tok = split_leading_sign (str, str_size);

    db_make_bigint (val, 0);

    /* Let try take the fastest path here, if we know that number we are getting fits into a long, use strtol, else we
     * need to convert it to a double and coerce it, checking for overflow. Note if integers with leading zeros are
     * entered this can take the slower route. */
    if (tok.num_digits < MAX_DIGITS_FOR_BIGINT || (tok.num_digits == MAX_DIGITS_FOR_BIGINT && tok.digits[0] != '9'))
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
	bool is_value_negative = false;

	numeric_coerce_dec_str_to_num (tok.dec_str, num.d.buf, &is_value_negative);
	if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint, is_value_negative) != NO_ERROR)
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
    int str_len = (int) str_size;
    int error = NO_ERROR;
    const tp_domain &domain = attr->get_domain ();
    int precision = domain.precision;
    INTL_CODESET codeset = (INTL_CODESET) domain.codeset;

    /* char_count <= byte_count in every codeset, so byte_size <= precision guarantees no truncation */
    if (str_len > precision)
      {
	int char_count = 0;
	intl_char_count ((unsigned char *) str, str_len, codeset, &char_count);
	if (char_count > precision)
	  {
	    /*
	     * May be a violation, but the overflow can be nothing but trailing pad
	     * characters, and then the value is truncated rather than refused.
	     *
	     * Ask the same question INSERT asks, through the same call it makes -
	     * see varchar_truncated () in string_opfunc.c. A real space always
	     * pads, and so does the codeset's own pad character, which is the wide
	     * space 0xa1a1 under EUC-KR. A tab or a carriage return does not pad,
	     * in either place.
	     */
	    unsigned char pad[2];
	    int pad_size = 0;
	    int trim_length = 0, trim_size = 0;
	    int truncate_size;

	    intl_pad_char (codeset, pad, &pad_size);
	    qstr_trim_trailing (pad, pad_size, (const unsigned char *) str, type, char_count, str_len, codeset,
				&trim_length, &trim_size, true);

	    if (trim_length > precision)
	      {
		/*
		 * It's a genuine violation; raise an error.
		 */
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (type));
		return ER_IT_DATA_OVERFLOW;
	      }

	    intl_char_size ((unsigned char *) str, precision, codeset, &truncate_size);
	    str_len = truncate_size;
	  }
      }

    error = db_value_domain_init (val, type, precision, 0);
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

  int
  to_db_string (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    /*
     * Not db_make_string (): it measures the token with strlen (), so a NUL inside
     * the token cuts the value short without a word. '2.25\0junk' then reached the
     * engine as "2.25" and cast cleanly into a NUMERIC column, where INSERT refuses
     * the string whole. The loader already knows the real length, so say it. Apart
     * from the length this is what db_make_string () does.
     */
    int error_code = db_value_domain_init (val, DB_TYPE_VARCHAR, TP_FLOATING_PRECISION_VALUE, 0);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    return db_make_db_char (val, LANG_SYS_CODESET, LANG_SYS_COLLATION, str, (int) str_size);
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
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		overflow_type_name (attr, DB_TYPE_FLOAT));
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
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1,
		overflow_type_name (attr, DB_TYPE_DOUBLE));
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
    const char *dot = (const char *) memchr (str, '.', str_size);
    int sign = (str_size > 0 && (str[0] == '+' || str[0] == '-')) ? 1 : 0;
    int precision = (int) str_size - sign - (dot != NULL ? 1 : 0);
    int scale = (dot != NULL) ? (int) (str + str_size - dot - 1) : 0;

    if (precision > DB_MAX_NUMERIC_PRECISION)
      {
	scale = (scale == 0) ? (DB_MAX_NUMERIC_PRECISION - precision) : scale;
	precision = DB_MAX_NUMERIC_PRECISION;

	if (scale > DB_MAX_NUMERIC_SCALE)
	  {
	    scale = DB_MAX_NUMERIC_SCALE;
	  }
	else if (scale < DB_MIN_NUMERIC_SCALE)
	  {
	    scale = DB_MIN_NUMERIC_SCALE;
	  }
      }

    int error_code = db_value_domain_init (val, DB_TYPE_NUMERIC, precision, scale);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    if (precision > DB_MAX_FIXED_NUMERIC_PRECISION)
      {
	FIXED_TO_FLOAT_NUMERIC (val);
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

    if (token == NULL || str_size == 0)
      {
	/*
	 * The text could not be assembled. A monetary constant is the one kind text_token ()
	 * has to build rather than point at, and a failed malloc is the only way it hands
	 * back an empty one.
	 *
	 * Say so rather than returning whatever er_errid () holds. An error from earlier in
	 * the same line is still standing when a converter is entered - a time literal the
	 * engine refused leaves ER_DATE_CONVERSION there, and an element the collection
	 * turned down leaves its own - and the control file has not been consulted yet, so
	 * even an error it ignores is still set. Handing one of those back would report this
	 * value as having failed for a reason that belongs to another.
	 */
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	return ER_LDR_MEMORY_ERROR;
      }

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

  /*
   * Build a bit string value of floating precision out of a B'..' or X'..' token.
   * The value owns its buffer, so the caller has to release it with db_value_clear ().
   */
  static int
  make_varbit_value (const char *str, const size_t str_size, bool from_hex, const attribute *attr, db_value *val)
  {
    int error_code = NO_ERROR;
    char *bstring = NULL;
    std::size_t dest_size = from_hex ? (str_size + 1) / 2 : (str_size + 7) / 8;
    int bit_length;
    int converted;

    db_make_null (val);

    bstring = (char *) db_private_alloc (NULL, dest_size + 1);
    if (bstring == NULL)
      {
	error_code = er_errid ();
	assert (error_code != NO_ERROR);

	return error_code;
      }

    if (from_hex)
      {
	bit_length = ((int) str_size) << 2;
	converted = qstr_hex_to_bin (bstring, (int) dest_size, const_cast<char *> (str), (int) str_size);
      }
    else
      {
	bit_length = (int) str_size;
	converted = qstr_bit_to_bin (bstring, (int) dest_size, const_cast<char *> (str), (int) str_size);
      }

    if (converted != (int) str_size)
      {
	db_private_free_and_init (NULL, bstring);

	error_code = ER_OBJ_DOMAIN_CONFLICT;
	/* An element slot may have no attribute; to_db_float () explains. */
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, (attr != NULL) ? attr->get_name () : "");

	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	return error_code;
      }

    error_code = db_make_varbit (val, TP_FLOATING_PRECISION_VALUE, bstring, bit_length);
    if (error_code != NO_ERROR)
      {
	db_private_free_and_init (NULL, bstring);
	return error_code;
      }

    val->need_clear = true;

    return NO_ERROR;
  }

  /* Convert a bit string token into the domain of the attribute it is assigned to.  */
  static int
  to_db_varbit (const char *str, const size_t str_size, bool from_hex, const attribute *attr, db_value *val)
  {
    db_value temp;
    tp_domain temp_domain, *domain_ptr = NULL;

    int error_code = make_varbit_value (str, str_size, from_hex, attr, &temp);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

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
	db_value_clear (&temp);

	error_code = ER_OBJ_DOMAIN_CONFLICT;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());

	// TODO CBRD-22271 log LOADDB_MSG_PARSE_ERROR
	return error_code;
      }

    db_value_clear (&temp);

    return NO_ERROR;
  }

  int
  to_db_varbit_from_bin_str (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return to_db_varbit (str, str_size, false, attr, val);
  }

  int
  to_db_varbit_from_hex_str (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return to_db_varbit (str, str_size, true, attr, val);
  }

  /*
   * Used within a collection. The attribute domain is the domain of the collection, not of the
   * element, so it must not be used to coerce an element. The value is built with its natural type
   * and set_add_element () coerces it into the element domain of the collection, the same way
   * ldr_bstr_elem () / ldr_xstr_elem () of the SA loader do.
   */
  int
  to_elem_varbit_from_bin_str (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return make_varbit_value (str, str_size, false, attr, val);
  }

  int
  to_elem_varbit_from_hex_str (const char *str, const size_t str_size, const attribute *attr, db_value *val)
  {
    return make_varbit_value (str, str_size, true, attr, val);
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

    if (new_len == 0)
      {
	/*
	 * An empty reference, ^E''. Nothing names a file, and the rest of this function
	 * has no input: it reads the type from str[0] and the size, the locator and the
	 * meta data from what follows. The column takes a null, which is what the
	 * standalone loader has always stored for it.
	 */
	db_make_null (val);
	return NO_ERROR;
      }

    if (str[0] != 'B' && str[0] != 'C')
      {
	assert (0);
	error_code = ER_LDR_ELO_INPUT_FILE;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, str);

	return error_code;
      }

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

    const signed_digits tok = split_leading_sign (str, str_size);

    /*
     * Watch out for really long digit strings that really are being
     * assigned into a DB_TYPE_NUMERIC attribute; they can hold more than a
     * standard integer can, and calling atol() on that string will lose
     * data.
     * Is there some better way to test for this condition?
     */
    if (tok.num_digits < MAX_DIGITS_FOR_INT
	|| (tok.num_digits == MAX_DIGITS_FOR_INT && (tok.digits[0] == '0' || tok.digits[0] == '1')))
      {
	db_make_int (val, 0);
	error_code = parse_int (&val->data.i, str, 10);
	if (error_code != NO_ERROR)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IT_DATA_OVERFLOW, 1, pr_type_name (DB_TYPE_INTEGER));
	    return ER_IT_DATA_OVERFLOW;
	  }
      }
    else if (tok.num_digits < MAX_DIGITS_FOR_BIGINT
	     || (tok.num_digits == MAX_DIGITS_FOR_BIGINT && tok.digits[0] != '9'))
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
	bool is_value_negative = false;

	numeric_coerce_dec_str_to_num (tok.dec_str, num.d.buf, &is_value_negative);
	if (numeric_coerce_num_to_bigint (num.d.buf, 0, &tmp_bigint, is_value_negative) != NO_ERROR)
	  {
	    int precision = (int) tok.num_digits;
	    if (precision > DB_MAX_NUMERIC_PRECISION)
	      {
		precision = DB_MAX_NUMERIC_PRECISION;
	      }

	    error_code = db_value_domain_init (val, DB_TYPE_NUMERIC, precision, 0);
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

  /*
   * Binary section values
   *
   *    unloaddb --data-format writes each attribute value as the raw memory of its DB_VALUE payload,
   *    see desc_value_special_fprint_binary () in src/executables/unload_object_file.c. The functions
   *    below turn those bytes back into a DB_VALUE, of the type the object file names first, and
   *    then into the attribute domain the way a text token reaches it:
   *
   *      - a string into a CHAR or VARCHAR column goes through to_db_generic_char (), the converter
   *        a quoted string of the text form takes, so truncation and overflow are decided alike;
   *      - a value of the attribute's own type is taken as it is, unless the domain carries a
   *        precision the value has to be fitted to;
   *      - anything else is cast by cast_to_attr_domain (), the cast fallback_convert () makes.
   *
   *    JSON and ELO values are written as text, the text their text form carries, and are converted
   *    by the converters of that text.
   */

  static int
  binary_corrupted (const binary_column &col)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_CORRUPTED_BINARY_RECORD, 1, (long long) col.offset);
    return ER_LDR_CORRUPTED_BINARY_RECORD;
  }

  template <typename T>
  static bool
  binary_read_raw (const binary_column &col, T &out)
  {
    if (col.size != sizeof (T))
      {
	return false;
      }

    std::memcpy (&out, col.data, sizeof (T));
    return true;
  }

  /*
   * binary_value_hint - the domain a value of the given type is built after
   *    return: the domain, or NULL when none fits
   *    domain(in): attribute domain, or the domain of a collection
   *    type(in): type the object file names for the value
   * Note:
   *    A string carries no codeset and an enumeration value no more than its index, so they are
   *    built after the domain they are going into. For a collection that is the first element
   *    domain of the same kind.
   */
  static const tp_domain *
  binary_value_hint (const tp_domain *domain, DB_TYPE type)
  {
    const tp_domain *d = domain;

    if (d != NULL && TP_IS_SET_TYPE (TP_DOMAIN_TYPE (d)))
      {
	d = d->setdomain;
      }

    for (; d != NULL; d = d->next)
      {
	if (TP_IS_CHAR_TYPE (type) && TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (d)))
	  {
	    return d;
	  }

	if (type == DB_TYPE_ENUMERATION && TP_DOMAIN_TYPE (d) == DB_TYPE_ENUMERATION)
	  {
	    return d;
	  }

	if (domain == NULL || !TP_IS_SET_TYPE (TP_DOMAIN_TYPE (domain)))
	  {
	    /* an attribute domain is not a list */
	    break;
	  }
      }

    return NULL;
  }

  /*
   * binary_natural_value - build the value the object file wrote, in its own type
   *    return: NO_ERROR or an error code
   *    col(in): the column
   *    attr(in): attribute being set, for error messages and for the text converters
   *    hint(in): see binary_value_hint (), may be NULL
   *    val(out): the value
   * Note:
   *    A string value points into the row and is not copied, the way a text token's value points
   *    into the parse tree. It must not outlive the row.
   */
  static int
  binary_natural_value (const binary_column &col, const attribute *attr, const tp_domain *hint, db_value *val)
  {
    db_make_null (val);

    switch (col.type)
      {
      case DB_TYPE_NULL:
	return NO_ERROR;

      case DB_TYPE_SHORT:
      {
	DB_C_SHORT v;
	return binary_read_raw (col, v) ? db_make_short (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_INTEGER:
      {
	int v;
	return binary_read_raw (col, v) ? db_make_int (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_BIGINT:
      {
	DB_BIGINT v;
	return binary_read_raw (col, v) ? db_make_bigint (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_FLOAT:
      {
	float v;
	return binary_read_raw (col, v) ? db_make_float (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_DOUBLE:
      {
	double v;
	return binary_read_raw (col, v) ? db_make_double (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_MONETARY:
      {
	DB_MONETARY v;
	return binary_read_raw (col, v) ? db_make_monetary (val, v.type, v.amount) : binary_corrupted (col);
      }

      case DB_TYPE_DATE:
      {
	DB_DATE v;
	return binary_read_raw (col, v) ? db_value_put_encoded_date (val, &v) : binary_corrupted (col);
      }

      case DB_TYPE_TIME:
      {
	DB_TIME v;
	return binary_read_raw (col, v) ? db_value_put_encoded_time (val, &v) : binary_corrupted (col);
      }

      case DB_TYPE_TIMESTAMP:
      {
	DB_TIMESTAMP v;
	return binary_read_raw (col, v) ? db_make_timestamp (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_TIMESTAMPLTZ:
      {
	DB_TIMESTAMP v;
	return binary_read_raw (col, v) ? db_make_timestampltz (val, v) : binary_corrupted (col);
      }

      case DB_TYPE_TIMESTAMPTZ:
      {
	DB_TIMESTAMPTZ v;
	return binary_read_raw (col, v) ? db_make_timestamptz (val, &v) : binary_corrupted (col);
      }

      case DB_TYPE_DATETIME:
      {
	DB_DATETIME v;
	return binary_read_raw (col, v) ? db_make_datetime (val, &v) : binary_corrupted (col);
      }

      case DB_TYPE_DATETIMELTZ:
      {
	DB_DATETIME v;
	return binary_read_raw (col, v) ? db_make_datetimeltz (val, &v) : binary_corrupted (col);
      }

      case DB_TYPE_DATETIMETZ:
      {
	DB_DATETIMETZ v;
	return binary_read_raw (col, v) ? db_make_datetimetz (val, &v) : binary_corrupted (col);
      }

      case DB_TYPE_NUMERIC:
      {
	/* [int precision][int scale][DB_NUMERIC_BUF_SIZE bytes of the buffer][flags] */
	int precision, scale;
	unsigned char flags;

	if (col.size != UNLOAD_RAW_NUMERIC_SIZE)
	  {
	    return binary_corrupted (col);
	  }

	std::memcpy (&precision, col.data, sizeof (int));
	std::memcpy (&scale, col.data + sizeof (int), sizeof (int));
	flags = (unsigned char) col.data[UNLOAD_RAW_NUMERIC_SIZE - 1];

	if (precision < 1 || precision > DB_MAX_NUMERIC_PRECISION)
	  {
	    return binary_corrupted (col);
	  }

	/* with DB_NUMERIC_BUF_SIZE as its size, the buffer is taken as it is */
	return db_make_numeric (val, (DB_C_NUMERIC) (col.data + (sizeof (int) << 1)), precision, scale,
				DB_NUMERIC_BUF_SIZE, (flags & UNLOAD_RAW_NUMERIC_NEGATIVE) != 0,
				(flags & UNLOAD_RAW_NUMERIC_FLOATING) != 0);
      }

      case DB_TYPE_CHAR:
      case DB_TYPE_VARCHAR:
      {
	int codeset = LANG_SYS_CODESET;
	int collation = LANG_SYS_COLLATION;

	if (col.size > (std::size_t) DB_MAX_STRING_LENGTH)
	  {
	    return binary_corrupted (col);
	  }

	if (hint != NULL)
	  {
	    codeset = hint->codeset;
	    collation = hint->collation_id;
	  }

	/* the cast into the target domain fits the length, so the natural value need not know it */
	return db_make_varchar (val, DB_MAX_VARCHAR_PRECISION, col.data, (int) col.size, codeset, collation);
      }

      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
      {
	/* [int length in bits][the bytes holding the bits] */
	int bit_length;

	if (col.size < UNLOAD_RAW_BIT_LENGTH_SIZE)
	  {
	    return binary_corrupted (col);
	  }

	std::memcpy (&bit_length, col.data, sizeof (int));
	if (bit_length < 0 || bit_length > DB_MAX_VARBIT_PRECISION
	    || col.size != UNLOAD_RAW_BIT_LENGTH_SIZE + (std::size_t) ((bit_length + 7) / 8))
	  {
	    return binary_corrupted (col);
	  }

	return db_make_varbit (val, DB_MAX_VARBIT_PRECISION, col.data + UNLOAD_RAW_BIT_LENGTH_SIZE, bit_length);
      }

      case DB_TYPE_ENUMERATION:
      {
	/*
	 * unloaddb writes the index only, so the value is looked up in the enumeration it is going
	 * into. That assumes the enumeration lists its elements in the order the one the file was
	 * written from did; the object file holds no description to check that against. An index
	 * the enumeration does not have is refused.
	 */
	unsigned short index;

	if (!binary_read_raw (col, index))
	  {
	    return binary_corrupted (col);
	  }

	if (hint == NULL)
	  {
	    /* not going into an enumeration, the index is all there is */
	    return db_make_short (val, (DB_C_SHORT) index);
	  }

	if (index == 0)
	  {
	    /* the special empty element */
	    return db_make_enumeration (val, 0, NULL, 0, (unsigned char) hint->codeset, hint->collation_id);
	  }

	if (index > DOM_GET_ENUM_ELEMS_COUNT (hint))
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_DOMAIN_CONFLICT, 1, attr->get_name ());
	    return ER_OBJ_DOMAIN_CONFLICT;
	  }

	const DB_ENUM_ELEMENT *elem = &DOM_GET_ENUM_ELEM (hint, index);
	return db_make_enumeration (val, index, DB_GET_ENUM_ELEM_STRING (elem), DB_GET_ENUM_ELEM_STRING_SIZE (elem),
				    (unsigned char) DB_GET_ENUM_ELEM_CODESET (elem), hint->collation_id);
      }

      case DB_TYPE_JSON:
	return to_db_json (col.data, col.size, attr, val);

      case DB_TYPE_BLOB:
      case DB_TYPE_CLOB:
      {
	/* the text of ^E'...', which to_db_elo_ext () reads as a null terminated string */
	std::string elo_text (col.data, col.size);

	return to_db_elo_ext (elo_text.c_str (), elo_text.size (), attr, val);
      }

      default:
	/* a collection is read by binary_to_collection (), and unloaddb writes no other type */
	return binary_corrupted (col);
      }
  }

  /*
   * binary_needs_cast - whether a value of the attribute's own type still has to be cast into it
   *    return: true for a type whose domain carries a precision or a scale the value must fit
   *    type(in): type of the attribute domain
   */
  static bool
  binary_needs_cast (DB_TYPE type)
  {
    switch (type)
      {
      case DB_TYPE_NUMERIC:
      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
      case DB_TYPE_CHAR:
      case DB_TYPE_VARCHAR:
	return true;
      default:
	return false;
      }
  }

  /*
   * binary_to_collection - read a collection and the elements that follow it in the row
   *    return: NO_ERROR or an error code
   *    reader(in/out): the row, positioned after the collection column
   *    col(in): the collection column, which holds the number of elements
   *    attr(in): attribute being set
   *    collection_domain(in): domain to create the collection with
   *    val(out): the collection
   * Note:
   *    Every element is read even after one has failed, so that the reader stays on the column
   *    boundaries and the attributes behind the collection can still be checked. The first failure
   *    is the one returned.
   */
  static int
  binary_to_collection (binary_row_reader &reader, const binary_column &col, const attribute *attr,
			const tp_domain *collection_domain, db_value *val)
  {
    DB_COLLECTION *set = NULL;
    int count = 0;
    int error_code = NO_ERROR;

    if (!binary_read_raw (col, count) || count < 0)
      {
	return binary_corrupted (col);
      }

    if (!TP_IS_SET_TYPE (TP_DOMAIN_TYPE (collection_domain)))
      {
	error_code = ER_OBJ_DOMAIN_CONFLICT;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, attr->get_name ());
      }
    else
      {
	set = set_create_with_domain (const_cast<tp_domain *> (collection_domain), 0);
	if (set == NULL)
	  {
	    error_code = er_errid ();
	    if (error_code == NO_ERROR)
	      {
		error_code = ER_FAILED;
		er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	      }
	  }
      }

    for (int i = 0; i < count; i++)
      {
	binary_column elem;
	db_value elem_val;

	if (reader.next (elem) != NO_ERROR)
	  {
	    error_code = ER_LDR_CORRUPTED_BINARY_RECORD;
	    break;
	  }

	if (TP_IS_SET_TYPE (elem.type))
	  {
	    /* unloaddb does not write nested collections, the row is not one it wrote */
	    error_code = binary_corrupted (elem);
	    break;
	  }

	if (error_code != NO_ERROR)
	  {
	    continue;
	  }

	int elem_error = binary_natural_value (elem, attr, binary_value_hint (collection_domain, elem.type), &elem_val);
	if (elem_error == NO_ERROR)
	  {
	    cast_elem_to_set_domain (collection_domain->setdomain, &elem_val);

	    /* the element is cloned into the collection */
	    elem_error = set_add_element (set, &elem_val);
	  }
	db_value_clear (&elem_val);

	if (elem_error != NO_ERROR)
	  {
	    error_code = elem_error;
	  }
      }

    if (error_code != NO_ERROR)
      {
	if (set != NULL)
	  {
	    set_free (set);
	  }
	return error_code;
      }

    return db_make_collection (val, set);
  }

  /*
   * binary_to_db_value - turn one column of a binary section row into the value of an attribute
   *    return: NO_ERROR or an error code. ER_LDR_CORRUPTED_BINARY_RECORD means the row cannot be
   *            read any further, reader.is_corrupted () tells whether that is the case
   *    reader(in/out): the row, positioned after col. A collection reads its elements from it
   *    col(in): the column
   *    attr(in): attribute being set
   *    collection_domain(in): domain to create a collection with, NULL for the attribute domain
   *    val(out): value in the attribute's domain
   */
  int
  binary_to_db_value (binary_row_reader &reader, const binary_column &col, const attribute *attr,
		      const tp_domain *collection_domain, db_value *val)
  {
    const tp_domain &domain = attr->get_domain ();
    DB_TYPE dom_type = TP_DOMAIN_TYPE (&domain);
    db_value natural;
    int error_code;

    db_make_null (val);

    if (TP_IS_SET_TYPE (col.type))
      {
	error_code = binary_to_collection (reader, col, attr, collection_domain != NULL ? collection_domain : &domain,
					   val);
	return error_code != NO_ERROR ? error_code : check_not_null (attr, val);
      }

    if (col.type == DB_TYPE_NULL)
      {
	return to_db_null (NULL, 0, attr, val);
      }

    if (TP_IS_CHAR_TYPE (col.type) && TP_IS_CHAR_TYPE (dom_type))
      {
	error_code = to_db_generic_char (dom_type, col.data, col.size, attr, val);
	return error_code != NO_ERROR ? error_code : check_not_null (attr, val);
      }

    error_code = binary_natural_value (col, attr, binary_value_hint (&domain, col.type), &natural);
    if (error_code != NO_ERROR)
      {
	db_value_clear (&natural);
	return error_code;
      }

    if (db_value_type (&natural) == dom_type && !binary_needs_cast (dom_type))
      {
	/* the value is handed over as it is, with whatever it owns */
	*val = natural;
	return check_not_null (attr, val);
      }

    return cast_to_attr_domain (&natural, attr, val);
  }
}
