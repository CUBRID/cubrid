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

/* format db values
 */

#ifndef _OBJECT_PRINT_COMMON_HPP_
#define _OBJECT_PRINT_COMMON_HPP_
struct db_collection;
struct db_midxkey;
struct db_monetary;
struct db_value;
class string_buffer;

class object_print_common
{
  private:
    string_buffer &m_buf;
  public:
    static constexpr char DECIMAL_FORMAT[] = "%#.*g";

    object_print_common (string_buffer &buf)
      : m_buf (buf)
    {}

    void describe_money (const db_monetary *value);
    void describe_value (const db_value *value);
    void describe_data (const db_value *value);

  protected:
    void describe_midxkey (const db_midxkey *midxkey, int help_Max_set_elements=20);
    void describe_set (const db_collection *set, int help_Max_set_elements=20);
};

#endif //_OBJECT_PRINT_COMMON_HPP_