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
