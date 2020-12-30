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
