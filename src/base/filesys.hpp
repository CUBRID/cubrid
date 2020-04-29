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
 * filesys.hpp: File System namespace & functionality
 */
#ifndef _FILESYS_H_
#define _FILESYS_H_

#include <stdio.h>
#include <memory>
#ifdef LINUX
#include <unistd.h>
#elif WINDOWS
#include <io.h>
#endif

namespace filesys //File System
{
  struct file_closer //predicate|operator used as custom deleter in std::unique_ptr
  {
    void operator() (FILE *fp) const
    {
      fclose (fp);
    }
  };

  using auto_close_file = std::unique_ptr<FILE, file_closer>;//normal unique_ptr with a custom deleter


  struct file_deleter //predicate|operator used as custom deleter in std::unique_ptr
  {
    void operator() (const char *filename) const
    {
#ifdef LINUX
      unlink (filename);
#elif WINDOWS
      _unlink (filename);
#endif
    }
  };

  using auto_delete_file = std::unique_ptr<const char, file_deleter>;//normal unique_ptr with a custom deleter
}

#endif //_FILESYS_H_