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
 * printer.hpp - printing classes
 */

#ifndef _PRINTER_HPP_
#define _PRINTER_HPP_

#include "string_buffer.hpp"
#include <string>

/*
 * print_output : interface class to print contents
 * operator () : template variadic operator to allows printf-like syntax
 *               it automaticaly calls virtual flush () method which needs to be implemented in specialized class
 */
class print_output
{
  protected:
    string_buffer m_sb;

  public:
    print_output () {}

    ~print_output ()
    {
      assert (m_sb.len () == 0);
    }

    virtual int flush (void) = 0;

    virtual void end_item (const char *item) { }

    string_buffer *grab_string_buffer (void)
    {
      return &m_sb;
    }

    void operator+= (const char ch);

    template<typename... Args> inline int operator() (Args &&... args);
};


template<typename... Args> int print_output::operator() (Args &&... args)
{
  m_sb (std::forward<Args> (args)...);

  int res = flush ();

  return res;
}

/*
 * file_print_output : ouput to a file
 */
class file_print_output : public print_output
{
  private:
    FILE *output_file;

  public:
    file_print_output (FILE *fp);
    ~file_print_output () {}

    static file_print_output &std_output (void);
    int flush (void);
};


/*
 * string_print_output : ouput to a string buffer
 */

class string_print_output : public print_output
{
  public:
    string_print_output ();
    ~string_print_output () {}

    int flush (void);

    const char *get_buffer () const
    {
      return m_sb.get_buffer ();
    }

    void clear (void)
    {
      m_sb.clear ();
    }
};

#endif // _PRINTER_HPP_
