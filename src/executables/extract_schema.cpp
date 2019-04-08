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
 * extract_schema.cpp -
 */

#include "extract_schema.hpp"
#include "string_buffer.hpp"
#include "dbi.h"


void extract_context::clear_schema_workspace (void)
    {
      if (vclass_list_has_using_index != NULL)
        {
          db_objlist_free (vclass_list_has_using_index);
          vclass_list_has_using_index = NULL;
        }

      if (classes != NULL)
        {
          db_objlist_free (classes);
          classes = NULL;
        }
    }

extract_output::extract_output (const std::string &ctx_name, string_buffer *sb_arg) :
    context_name (ctx_name),
    output_file (NULL),
    sb (sb_arg)
  {
  }

extract_output::extract_output (const std::string &ctx_name, FILE *fp) :
    context_name (ctx_name),
    output_file (fp),
    sb (NULL)
  {
  }

extract_output& extract_output::std_output (void)
{
  static extract_output s_std_output ("STDOUT", stdout);
  return s_std_output;
}

const char *extract_output::exec_name (void)
  {
    return context_name.c_str ();
  }
