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

//
// string_regex - definitions and functions related to regular expression
//

#ifndef _STRING_REGEX_CONSTANTS_HPP_
#define _STRING_REGEX_CONSTANTS_HPP_

namespace cubregex
{
  enum engine_type
  {
    LIB_NONE    = -1,
    LIB_CPPSTD  = 0,
    LIB_RE2     = 1
  };

  using opt_flag_type = unsigned int;
  enum opt_flag : unsigned int
  {
    OPT_SYNTAX_ECMA           = 0x1,
    OPT_SYNTAX_POSIX_EXTENDED = 0x2,
    OPT_ICASE                 = 0x4, // case insensitive
    OPT_ERROR                 = 0xf0000000
  };
}

#endif // _STRING_REGEX_CONSTANTS_HPP_
