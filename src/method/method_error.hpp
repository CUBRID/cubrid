/*
 *
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
// method_error.hpp
//

#ifndef _METHOD_ERROR_HPP_
#define _METHOD_ERROR_HPP_

#include <string>

namespace cubmethod
{
  class error_context
  {
    public:
      int get_error ();
      int set_error (int err_number, int err_indicator, std::string file, int line);
      int set_error_with_msg (int err_number, int err_indicator, std::string err_msg, std::string err_file, int line,
			      bool force);

    private:
      int err_indicator;
      int err_number;
      std::string err_string;
      std::string err_file;
      int err_line;
  };
}

#endif
