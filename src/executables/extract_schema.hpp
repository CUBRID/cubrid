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
 * extract_schema.hpp -
 */

#ifndef _EXTRACT_SCHEMA_HPP_
#define _EXTRACT_SCHEMA_HPP_

#include "dbtype_def.h"
#include "string_buffer.hpp"

#include <stdio.h>
#include <string>

class string_buffer;

typedef enum
{
  FOLLOW_STORAGE_ORDER = 0,
  FOLLOW_ATTRIBUTE_ORDER = 1
} EMIT_STORAGE_ORDER;

struct extract_context
{
  /* input */
  int do_auth;
  EMIT_STORAGE_ORDER storage_order;
  const char *exec_name;

  /* working */
  int has_indexes;
  DB_OBJLIST *classes;
  DB_OBJLIST *vclass_list_has_using_index;

  extract_context ():
    do_auth (0),
    storage_order (FOLLOW_STORAGE_ORDER),
    exec_name (NULL),
    has_indexes (0),
    classes (NULL),
    vclass_list_has_using_index (NULL)
  {
  }

  void clear_schema_workspace (void);
};

class print_output
{
  private:
    std::string context_name;

  protected:
    string_buffer m_sb;

  public:
    print_output (const std::string &ctx_name):
      context_name (ctx_name)
    {}

    ~print_output ()
    {
      assert (m_sb.len () == 0);
    }

    const char *exec_name (void);

    virtual int flush (void) = 0;

    string_buffer *grab_string_buffer (void)
    {
      return &m_sb;
    }

    void operator+= (const char ch);

    template<typename... Args> inline int operator() (Args &&... args);
};


template<typename... Args> int print_output::operator() (Args &&... args)
{
  int res = m_sb (std::forward<Args> (args)...);
  if (res < 0)
    {
      return res;
    }

  res = flush ();

  return res;
}


class file_print_output : public print_output
{
  private:
    FILE *output_file;

  public:
    file_print_output (const std::string &ctx_name, FILE *fp);
    ~file_print_output () {}

    static file_print_output &std_output (void);
    int flush (void);
};

class string_print_output : public print_output
{
  public:
    string_print_output (const std::string &ctx_name);
    ~string_print_output () {}

    int flush (void);
};

#endif /* _EXTRACT_SCHEMA_HPP_ */
