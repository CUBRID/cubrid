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
 * parse_type.hpp
 */

#ifndef _PARSE_TYPE_HPP_
#define _PARSE_TYPE_HPP_

#include "parse_tree.h"
#include <string>
class string_buffer;

/* generic types */
enum pt_generic_type_enum
{
  PT_GENERIC_TYPE_NONE,
  PT_GENERIC_TYPE_STRING, // any type of string
  PT_GENERIC_TYPE_STRING_VARYING, // VARCHAR or VARNCHAR
  PT_GENERIC_TYPE_CHAR, // VARCHAR or CHAR
  PT_GENERIC_TYPE_NCHAR, // VARNCHAR or NCHAR
  PT_GENERIC_TYPE_BIT, // BIT OR VARBIT
  PT_GENERIC_TYPE_DISCRETE_NUMBER, // SMALLINT, INTEGER, BIGINTEGER
  PT_GENERIC_TYPE_NUMBER, // any number type
  PT_GENERIC_TYPE_DATE, // date, datetime or timestamp
  PT_GENERIC_TYPE_DATETIME, // any date or time type
  PT_GENERIC_TYPE_SEQUENCE, // any type of sequence
  PT_GENERIC_TYPE_LOB, // BLOB or CLOB
  PT_GENERIC_TYPE_QUERY, // Sub query (for range operators)
  PT_GENERIC_TYPE_PRIMITIVE, // primitive types
  PT_GENERIC_TYPE_ANY, // any type
  PT_GENERIC_TYPE_JSON_VAL, // PT_TYPE_VARCHAR, PT_TYPE_VARCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR,
  // PT_TYPE_NUMERIC, PT_TYPE_INTEGER, PT_TYPE_LOGICAL, PT_TYPE_DOUBLE, PT_TYPE_NUMERIC,
  // PT_TYPE_JSON, PT_TYPE_MAYBE, PT_TYPE_NULL
  PT_GENERIC_TYPE_JSON_DOC, // PT_TYPE_VARCHAR, PT_TYPE_VARNCHAR, PT_TYPE_CHAR, PT_TYPE_NCHAR,
  // PT_TYPE_JSON, PT_TYPE_MAYBE, PT_TYPE_NULL
  PT_GENERIC_TYPE_SCALAR, // any type but set
};

const char *pt_generic_type_to_string (pt_generic_type_enum type);

/* expression argument type */
struct pt_arg_type
{
  enum
  {
    NORMAL,
    GENERIC,
    INDEX
  } type;

  union pt_arg_type_val
  {
    PT_TYPE_ENUM type;
    pt_generic_type_enum generic_type;
    size_t index; //index type

    pt_arg_type_val (pt_type_enum type)
      : type (type)
    {
    }

    pt_arg_type_val (pt_generic_type_enum enum_val)
      : generic_type (enum_val)
    {
    }

    pt_arg_type_val (size_t index)
      : index (index)
    {
    }
  } val;

  pt_arg_type (pt_type_enum type = PT_TYPE_NONE)
    : type (NORMAL)
    , val (type)
  {
  }

  pt_arg_type (pt_generic_type_enum generic_type)
    : type (GENERIC)
    , val (generic_type)
  {
  }

  pt_arg_type (size_t index)
    : type (INDEX)
    , val (index)
  {
  }

  void operator() (pt_type_enum normal_type)
  {
    type = NORMAL;
    val.type = normal_type;
  }

  void operator() (pt_generic_type_enum generic_type)
  {
    type = GENERIC;
    val.generic_type = generic_type;
  }

  void operator() (size_t index)
  {
    type = INDEX;
    val.index = index;
  }
};
typedef pt_arg_type PT_ARG_TYPE;

const char *pt_arg_type_to_string_buffer (const pt_arg_type &type, string_buffer &sb);
PT_TYPE_ENUM pt_to_variable_size_type (PT_TYPE_ENUM type_enum);

#endif // _PARSE_TYPE_HPP_
