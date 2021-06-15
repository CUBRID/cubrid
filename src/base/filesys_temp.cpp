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
 * file_sys.cpp: File System namespace & functionality
 */
#include "filesys_temp.hpp"
#include <filesystem>
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
    std::string filename = std::filesystem::temp_directory_path ();
    filename += "/";
    filename += prefix;
    filename += "XXXXXX"; //used with mkstemp()
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
  auto filedesc = _open (filename.c_str(), _O_CREAT|_O_EXCL|_O_RDWR|flags, _S_IWRITE);
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
