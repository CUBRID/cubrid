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

 #if !defined(_OBJECT_DESCRIPTION_HPP_)
#define _OBJECT_DESCRIPTION_HPP_
#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)
struct db_object;

/*
 * OBJ_HELP
 *
 * Note :
 *    This structure contains information about an instance.  This will
 *    built and returned by help_obj().
 */
struct object_description
{
  char *classname;
  char *oid;
  char **attributes;
  char **shared;

  object_description(struct db_object* op = 0);
  ~object_description();
};

#endif //!defined(_OBJECT_DESCRIPTION_HPP_)