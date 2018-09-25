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

//
// json_table_def.h - json table common definitions (cross modules)
//

#ifndef _JSON_TABLE_DEF_H_
#define _JSON_TABLE_DEF_H_

// note - this is included in C compiled files

// forward definitions
struct db_value;

enum json_table_column_behavior_type
{
  JSON_TABLE_RETURN_NULL,
  JSON_TABLE_THROW_ERROR,
  JSON_TABLE_DEFAULT_VALUE
};

enum json_table_column_function
{
  JSON_TABLE_EXTRACT,
  JSON_TABLE_EXISTS,
  JSON_TABLE_ORDINALITY
};

struct json_table_column_behavior
{
  enum json_table_column_behavior_type m_behavior;
  struct db_value *m_default_value;
};

enum json_table_expand_type
{
  JSON_TABLE_ARRAY_EXPAND,
  JSON_TABLE_OBJECT_EXPAND,
  JSON_TABLE_NO_EXPAND
};

#endif // _JSON_TABLE_DEF_H_
