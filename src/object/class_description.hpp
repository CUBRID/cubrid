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
 * class_description.hpp
 */

#ifndef _CLASS_DESCRIPTION_HPP_
#define _CLASS_DESCRIPTION_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

#include <vector>

struct db_object;
struct sm_class;
class string_buffer;
struct tr_triglist;

/*
 * CLASS_HELP
 *
 * Note :
 *    This structure contains information about a class defined in the database.
 *    This will be built and returned by help_class or help_class_name.
 */
//all members should be refactored but for the moment is only triggers and partition are...
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
  std::vector<char *> triggers;
  char **constraints;
  std::vector<char *> partition;
  char *comment;

  class_description ();                                   //former obj_print_make_class_help()
  ~class_description ();                                  //former obj_print_help_free_class()

  int init (const char *name);                           //former obj_print_help_class()
  int init (struct db_object *op, type prt_type);
  int init (struct db_object *op, type prt_type, string_buffer &sb);//to be used in object_printer::describe_class()

  //ToDo: other special methods: copy&move ctor/assign
};

#endif // _CLASS_DESCRIPTION_HPP_
