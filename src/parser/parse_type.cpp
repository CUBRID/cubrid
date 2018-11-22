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

const char *pt_generic_type_to_string (pt_generic_type_enum type)
{
  static const char *arr[] =
  {
    "GENERIC TYPE NONE",
    "GENERIC ANY STRING TYPE",
    "GENERIC VARIABLE STRING TYPE",
    "GENERIC ANY CHAR TYPE",
    "GENERIC ANY NCHAR TYPE",
    "GENERIC ANY BIT TYPE",
    "GENERIC DISCRETE NUMBER TYPE",
    "GENERIC ANY NUMBER TYPE",
    "GENERIC DATE TYPE",
    "GENERIC DATETIME TYPE",
    "GENERIC SEQUENCE TYPE",
    "GENERIC LOB TYPE",
    "GENERIC QUERY TYPE",    // what is this?
    "GENERIC PRIMITIVE TYPE",
    "GENERIC ANY TYPE",
    "JSON VALUE",
    "JSON DOCUMENT",
    "GENERIC SCALAR TYPE",
  };
  static_assert ((PT_GENERIC_TYPE_SCALAR + 1) == (sizeof (arr) / sizeof (const char *)),
		 "miss-match between pt_generic_type_enum and its name array");
  return arr[type];
}

//--------------------------------------------------------------------------------
const char *pt_arg_type_to_string_buffer (const pt_arg_type &type, string_buffer &sb)
{
  switch (type.type)
    {
    case pt_arg_type::NORMAL:
      //sb("%s", str(type.val.type));
      sb ("%s", pt_show_type_enum (type.val.type));
      break;
    case pt_arg_type::GENERIC:
      sb ("%s", pt_generic_type_to_string (type.val.generic_type));
      break;
    case pt_arg_type::INDEX:
      sb ("IDX%d", type.val.index);
      break;
    }
  return sb.get_buffer();
}
