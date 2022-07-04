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
#if defined(USE_NCHAR_PT_TYPE)
    "GENERIC ANY NCHAR TYPE",
#endif
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
