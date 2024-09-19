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
 * pl_command_runner.hpp
 */

#ifndef _PL_COMMAND_RUNNER_HPP_
#define _PL_COMMAND_RUNNER_HPP_

namespace cubpl
{
  class command_runner
  {
    public:
      command_runner () = delete; // Not DefaultConstructible
      command_runner (const command_runner &copy) = delete; // Not CopyConstructible
      command_runner (command_runner &&other) = delete; // Not MoveConstructible

      command_runner &operator= (const command_runner &copy) = delete; // Not CopyAssignable
      command_runner &operator= (command_runner &&other) = delete; // Not MoveAssignable

      virtual ~command_runner () = default;

      virtual void run () = 0;
  };
}

#endif