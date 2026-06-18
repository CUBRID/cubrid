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
 * connection_context.cpp
 */

#include "connection_context.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* --------------------------------------------------------------------------- */
/* master connector								 */
/* --------------------------------------------------------------------------- */
namespace cubconn::master
{
  context::context () :
    m_conn (nullptr),
    m_sendbuf (32),
    m_has_error (false)
  {
  }

  context::~context ()
  {
    m_recvbuf.reset ();
    m_sendbuf.clear ();
  }

  void context::reset ()
  {
    m_recvbuf.reset ();
    m_sendbuf.clear ();

    m_state = state::SendInHandshake;
    m_has_error = false;
  }

  bool context::has_data_to_send ()
  {
    if (m_sendbuf.get_msghdr ().msg_iovlen)
      {
	return true;
      }

    return false;
  }
}

/* --------------------------------------------------------------------------- */
/* connection worker								 */
/* --------------------------------------------------------------------------- */
namespace cubconn::connection
{
  context::context (std::size_t capacity) :
    m_conn (nullptr),
    m_worker (-1),
    m_id (0),
    m_ignore (ignore_level::DONT_IGNORE),
    m_removed (false),
    m_recv
  {
    .m_state = state::HEADER,
    .m_receiver = receiver (capacity, &m_stats),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  },
  m_send
  {
    .m_transmitter = transmitter (&m_stats),
    .m_blocker = nullptr
  }
  {
  }

  context::context () :
    m_conn (nullptr),
    m_worker (-1),
    m_id (0),
    m_ignore (ignore_level::DONT_IGNORE),
    m_removed (false),
    m_recv
  {
    .m_state = state::HEADER,
    .m_receiver = receiver (),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  },
  m_send
  {
    .m_transmitter = transmitter (),
    .m_blocker = nullptr
  }
  {
  }

  context::~context ()
  {
  }

  bool context::prepare ()
  {
    return m_recv.m_receiver.prepare ();
  }

  void context::reset ()
  {
    m_conn = nullptr;
    m_worker = -1;
    m_id = 0;
    m_ignore = ignore_level::DONT_IGNORE;
    m_removed = false;

    m_recv.m_state = state::HEADER;
    m_recv.m_receiver.reset ();
    m_recv.m_header = { nullptr, 0 };
    m_recv.m_request_id = -1;
    m_recv.m_command = false;

    m_send.m_transmitter.clear ();
    m_send.m_blocker = nullptr;

    m_stats.reset ();
  }
}
