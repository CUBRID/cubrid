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
 * file_sys.cpp: File System namespace & functionality
 */
#include "filesys_temp.hpp"
#include <stdlib.h>

#ifdef LINUX
#include "porting.h"
#elif WINDOWS
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
  std::string unique_tmp_filename (const char *prefix="cub_") //generates an unique filename in tmp folder
  {
#ifdef LINUX
    std::string filename = std::string ("/tmp/") + prefix + "XXXXXX"; //used with mkstemp()
    //TBD (not necessary yet)
#elif WINDOWS
    char buf[L_tmpnam] = {};
    std::string filename = std::tmpnam (buf);
    auto pos = filename.rfind ('\\');
    filename.insert (pos+1, prefix);
#endif
    return filename;
  }
}

//--------------------------------------------------------------------------------
std::pair<std::string, int> filesys::open_temp_filedes (const char *prefix, int flags)
{
#ifdef LINUX
  char filename[PATH_MAX] = {};
  snprintf (filename, sizeof (filename), "%s", unique_tmp_filename (prefix).c_str());
  auto filedesc = mkostemp (filename, flags);
#elif WINDOWS
  auto filename = unique_tmp_filename (prefix);
  auto filedesc = _open (filename.c_str(), _O_CREAT|_O_EXCL|_O_RDWR|flags);
#endif
  return {filename, filedesc};
}

//--------------------------------------------------------------------------------
std::pair<std::string, FILE *> filesys::open_temp_file (const char *prefix, const char *mode, int flags)
{
#ifdef LINUX
  char filename[PATH_MAX] = {};
  snprintf (filename, sizeof (filename), "%s", unique_tmp_filename (prefix).c_str());
  auto filedesc = mkostemp (filename, flags);
  FILE *fileptr = fdopen (filedesc, mode);
#elif WINDOWS
  auto filename = unique_tmp_filename (prefix);
  auto *fileptr = fopen (filename.c_str(), mode);
#endif
  return {filename, fileptr};
}