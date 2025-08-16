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
 * transmitter.cpp
 */

#include "connection_defs.h"
#include "transmitter.hpp"
#include "error_manager.h"
#include "span.hpp"
#include "object_primitive.h"

#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>

#define NEXT_STATE(x) do { \
    _er_log_debug (__FILE__, __LINE__, "transmitter state %d -> state = %d\n", m_state, state::x); \
    (m_state = state::x); \
} while (0)

namespace cubconn
{
  transmitter::transmitter ()
  {
    m_buf.reserve (16);
    this->reset ();
  }

  transmitter::~transmitter ()
  {
  }

  void transmitter::reset ()
  {
  }
}

