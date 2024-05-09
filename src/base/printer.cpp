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
 * printer.cpp - printing classes
 */

#include "printer.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

void print_output::operator+= (const char ch)
{
  m_sb.operator+= (ch);
  (void) flush ();
}

file_print_output::file_print_output (FILE *fp) :
  output_file (fp)
{
}

file_print_output &file_print_output::std_output (void)
{
  static file_print_output s_std_output (stdout);
  return s_std_output;
}

int file_print_output::flush ()
{
  int res = (int) fwrite (m_sb.get_buffer (), 1, m_sb.len (), output_file);
  m_sb.clear ();
  return res;
}


string_print_output::string_print_output ()
{
}

int string_print_output::flush ()
{
  return (int) m_sb.len ();
}
