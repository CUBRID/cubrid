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
 * fileline_location.hpp - interface of file & line location
 */

#ifndef _FILELINE_LOCATION_HPP_
#define _FILELINE_LOCATION_HPP_

#include <iostream>

namespace cubbase
{
  // file_line - holder of file/line location
  //
  // probably should be moved elsewhere
  //
  struct fileline_location
  {
    fileline_location (const char *fn_arg = "", int l_arg = 0);

    static const std::size_t MAX_FILENAME_SIZE = 20;

    static const char *print_format (void)
    {
      return "%s:%d";
    }

    void set (const char *fn_arg, int l_arg);

    char m_file[MAX_FILENAME_SIZE];
    int m_line;
  };

#define FILELINE_LOCATION_AS_ARGS(fileline) (fileline).m_file, (fileline).m_line

  std::ostream &operator<< (std::ostream &os, const fileline_location &fileline);
} // namespace cubbase

#endif // _FILELINE_LOCATION_HPP_
