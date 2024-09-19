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

/*
 * pl_command.hpp
 */

#ifndef _PL_COMMAND_HPP_
#define _PL_COMMAND_HPP_

namespace cubpl
{
  class command
  {
    public:
      command () = delete; // Not DefaultConstructible
      command (const command &copy) = delete; // Not CopyConstructible
      command (command &&other) = delete; // Not MoveConstructible

      command &operator= (const command &copy) = delete; // Not CopyAssignable
      command &operator= (command &&other) = delete; // Not MoveAssignable

      virtual ~command () = default;

      virtual void on_request () = 0;
      virtual void on_response () = 0;
  };
}

#endif