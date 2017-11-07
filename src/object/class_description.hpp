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

#if !defined(_CLASS_DESCRIPTION_HPP_)
#define _CLASS_DESCRIPTION_HPP_
#if defined(SERVER_MODE)

#error Does not belong to server module
#endif //defined(SERVER_MODE)

struct db_object;

/*
 * CLASS_HELP
 *
 * Note :
 *    This structure contains information about a class defined in the database.
 *    This will be built and returned by help_class or help_class_name.
 */
struct class_description
{
  enum type
  {
    /*OBJ_PRINT_*/ CSQL_SCHEMA_COMMAND,
    /*OBJ_PRINT_*/ SHOW_CREATE_TABLE
  };

  char *name;
  char *class_type;
  char *collation;
  char **supers;
  char **subs;
  char **attributes;
  char **class_attributes;
  char **methods;
  char **class_methods;
  char **resolutions;
  char **method_files;
  char **query_spec;
  char *object_id;
  char **triggers;
  char **constraints;
  char **partition;
  char *comment;

  class_description ();                                    //former obj_print_make_class_help()
  class_description (const char *name);                    //former obj_print_help_class()
  ~class_description ();                                   //former obj_print_help_free_class()

  bool init(struct db_object *op, type prt_type);

  //ToDo: other special methods: copy&move ctor/assign
};

#endif //_CLASS_DESCRIPTION_HPP_
