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
 * object_description.hpp
 *
 * Structure contains information about an instance.
 * extracted from object_print and improved with constructor & destructor
 */

#ifndef _OBJECT_DESCRIPTION_HPP_
#define _OBJECT_DESCRIPTION_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

struct db_object;

struct object_description
{
  char *classname;
  char *oid;
  char **attributes;                //ToDo: refactor as std::vector<char*>
  char **shared;                    //ToDo: looks like not used anywhere, remove it?

  object_description ();            //former obj_print_make_obj_help()
  ~object_description ();           //former help_free_obj()

  int init (struct db_object *op); //former help_obj()
};

#endif // _OBJECT_DESCRIPTION_HPP_
