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
 * db_value_printer.hpp
 *
 * format db values
 * common things (server & client) extracted from object_print
 */

#ifndef _DB_VALUE_PRINTER_HPP_
#define _DB_VALUE_PRINTER_HPP_

#include <cstdio>

struct db_set;
struct db_midxkey;
struct db_monetary;
struct db_value;
class string_buffer;
class print_output;

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
    void describe_comment_value (const db_value *value);    //former describe_value(parser...)

  protected:
    void describe_midxkey (const db_midxkey *midxkey, int help_Max_set_elements=20);  //former describe_midxkey()
    void describe_set (const db_set *set, int help_Max_set_elements=20);       //former describe_set()
};

void db_fprint_value (FILE *fp, const db_value *value);
void db_print_value (print_output &output_ctx, const db_value *value);
void db_sprint_value (const db_value *value, string_buffer &sb);
#ifndef NDEBUG
void db_value_print_console (const db_value *value, bool add_newline, char *fmt, ...);
#endif

#endif //_DB_VALUE_PRINTER_HPP_
