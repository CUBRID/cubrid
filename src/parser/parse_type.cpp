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
 * parse_type.cpp
 */

#include "parse_type.hpp"
#include "parser.h"
#include "string_buffer.hpp"

const char *str (pt_generic_type_enum type)
{
  static const char *arr[] =
  {
    "GT_NONE",
    "GT_STRING",
    "GT_STRING_VARYING",
    "GT_CHAR",
    "GT_NCHAR",
    "GT_BIT",
    "GT_DISCRETE_NUMBER",
    "GT_NUMBER",
    "GT_DATE",
    "GT_DATETIME",
    "GT_SEQUENCE",
    "GT_LOB",
    "GT_QUERY",
    "GT_PRIMITIVE",
    "GT_ANY",
    "GT_JSON_VAL",
    "GT_JSON_DOC",
    "GT_JSON_PATH",
    "GT_SCALAR",
  };
  return arr[type];
}

//--------------------------------------------------------------------------------
const char *str (const pt_arg_type &type, string_buffer &sb)
{
  switch (type.type)
    {
    case pt_arg_type::NORMAL:
      //sb("%s", str(type.val.type));
      sb ("%s", pt_show_type_enum (type.val.type));
      break;
    case pt_arg_type::GENERIC:
      sb ("%s", str (type.val.generic_type));
      break;
    case pt_arg_type::INDEX:
      sb ("IDX%d", type.val.index);
      break;
    }
  return sb.get_buffer();
}
