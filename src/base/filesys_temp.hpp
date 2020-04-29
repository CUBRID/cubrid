/*
 * Copyright (C) 2020 Search Solution Corporation. All rights reserved by Search Solution.
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
 * filesys_temp.hpp: File System - Automatic Close File
 */
#ifndef _FILESYS_TEMP_H_
#define _FILESYS_TEMP_H_

#include <stdio.h>
#include <string>
#include <utility>

namespace filesys //File System
{
  //opens a new file in OS's tmp folder; return file name & open descriptor
  std::pair<std::string, int> open_temp_filedes (const char *prefix, int flags=0);

  //opens a new file in OS's tmp folder; return file name & FILE*
  std::pair<std::string, FILE *> open_temp_file (const char *prefix, const char *mode="w", int flags=0);
}

#endif //_FILESYS_TEMP_H_