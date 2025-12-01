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

namespace cubconn
{
  /* --------------------------------------------------------------------------- */
  /* master connector								 */
  /* --------------------------------------------------------------------------- */
  master_connector_context::master_connector_context () :
    m_conn (nullptr),
    m_sendbuf (32),
    m_has_error (false)
  {
  }

  master_connector_context::~master_connector_context ()
  {
    m_recvbuf.reset ();
    m_sendbuf.clear ();
  }

  void master_connector_context::reset ()
  {
    m_recvbuf.reset ();
    m_sendbuf.clear ();

    m_state = master_connector_state::SendInHandshake;
    m_has_error = false;
  }

  bool master_connector_context::has_data_to_send ()
  {
    if (m_sendbuf.get_msghdr ().msg_iovlen)
      {
	return true;
      }

    return false;
  }

  /* --------------------------------------------------------------------------- */
  /* connection worker								 */
  /* --------------------------------------------------------------------------- */
  connection_worker_context::connection_worker_context (std::size_t capacity, connection_stats *stats) :
    m_conn (nullptr),
    m_ignore (connection_worker_ignore::DONT_IGNORE),
    m_removed (false),
    m_recv
  {
    .m_state = connection_worker_state::HEADER,
    .m_receiver = receiver (capacity, stats),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  },
  m_send
  {
    .m_transmitter = transmitter (stats)
  }
  {
  }

  connection_worker_context::connection_worker_context () :
    m_conn (nullptr),
    m_recv
  {
    .m_state = connection_worker_state::HEADER,
    .m_receiver = receiver (),
    .m_header = { nullptr, 0 },
    .m_request_id = -1,
    .m_command = false
  },
  m_send
  {
    .m_transmitter = transmitter ()
  }
  {
  }

  connection_worker_context::~connection_worker_context ()
  {
  }
}
