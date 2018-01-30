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
 * object_print_util.hpp - Utility structures and functions extracted from object_print
 */

#ifndef _OBJECT_PRINT_UTIL_HPP_
#define _OBJECT_PRINT_UTIL_HPP_

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif //defined(SERVER_MODE)

namespace object_print
{
  void free_strarray (char **strs);                   //former obj_print_free_strarray()
  char *copy_string (const char *source);             //former obj_print_copy_string()
}

#endif // _OBJECT_PRINT_UTIL_HPP_
