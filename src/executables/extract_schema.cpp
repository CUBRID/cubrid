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

const char *print_output::exec_name (void)
{
  return context_name.c_str ();
}

void print_output::operator+= (const char ch)
{
  m_sb.operator+= (ch);
  (void) flush ();
}

file_print_output::file_print_output (const std::string &ctx_name, FILE *fp) :
  print_output (ctx_name),
  output_file (fp)
{
}

file_print_output &file_print_output::std_output (void)
{
  static file_print_output s_std_output ("STDOUT", stdout);
  return s_std_output;
}

int file_print_output::flush ()
{
  return (int) fwrite (m_sb.get_buffer (), 1, m_sb.len (), output_file);
}


string_print_output::string_print_output (const std::string &ctx_name) :
  print_output (ctx_name)
{
}

int string_print_output::flush ()
{
  /* nothing to do */
  return (int) m_sb.len ();
}
