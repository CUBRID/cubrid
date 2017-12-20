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
