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
