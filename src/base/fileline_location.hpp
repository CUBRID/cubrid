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
