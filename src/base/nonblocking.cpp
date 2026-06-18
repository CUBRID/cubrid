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
 * nonblocking.cpp
 */

#include "nonblocking.hpp"
#include "error_manager.h"

#include <fcntl.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubsocket
{
  nonblocking::nonblocking ()
  {
  }

  nonblocking::~nonblocking ()
  {
  }

  bool nonblocking::is_nonblocking (int fd) noexcept
  {
    int flags;

    flags = fcntl (fd, F_GETFL, 0);
    if (flags == -1)
      {
	assert_release (false);
      }

    if (flags & O_NONBLOCK)
      {
	return true;
      }
    return false;
  }

  int nonblocking::get_flags (int fd) noexcept
  {
    return fcntl (fd, F_GETFL, 0);
  }


  int nonblocking::set_flags (int fd, int flags) noexcept
  {
    return fcntl (fd, F_SETFL, flags);
  }
}
