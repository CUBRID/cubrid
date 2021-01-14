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
