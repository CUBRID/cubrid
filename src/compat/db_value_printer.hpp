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
 */

/*
 * db_value_printer.hpp
 *
 * format db values
 * common things (server & client) extracted from object_print
 */

#ifndef _DB_VALUE_PRINTER_HPP_
#define _DB_VALUE_PRINTER_HPP_

#include <cstdio>

struct db_collection;
struct db_midxkey;
struct db_monetary;
struct db_value;
class string_buffer;

class db_value_printer
{
  private:
    string_buffer &m_buf;
    bool m_padding;
  public:
    static const char DECIMAL_FORMAT[];

    db_value_printer (string_buffer &buf, bool padding=false)
      : m_buf (buf)
      , m_padding (padding)
    {}

    void describe_money (const db_monetary *value); //former describe_money(parser...)
    void describe_value (const db_value *value);    //former describe_value(parser...)
    void describe_data (const db_value *value);     //former describe_data(parser...)

  protected:
    void describe_midxkey (const db_midxkey *midxkey, int help_Max_set_elements=20);  //former describe_midxkey()
    void describe_set (const db_collection *set, int help_Max_set_elements=20);       //former describe_set()
};

void db_fprint_value (FILE *fp, const db_value *value);
void db_sprint_value (const db_value *value, string_buffer &sb);

#endif //_DB_VALUE_PRINTER_HPP_
