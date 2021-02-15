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

#include "connection_defs.h"
#include "communication_channel.hpp"

#include <condition_variable>
#include <cstring>
#include <queue>

/*
 * mock channel implementation using 2 local queues, one for each communication direction ATS <--> PS
 */

namespace cubcomm
{

  // queues and synchronization variables
  std::queue<std::string> cs_q;
  std::queue<std::string> sc_q;
  std::condition_variable cs_cv;
  std::condition_variable sc_cv;
  std::mutex cs_mutex;
  std::mutex sc_mutex;

  channel::channel (int max_timeout_in_ms)
    : m_max_timeout_in_ms (max_timeout_in_ms),
      m_type (CHANNEL_TYPE::NO_TYPE),
      m_socket (INVALID_SOCKET)
  {
  }

  channel &channel::operator= (channel &&comm)
  {
    assert (!is_connection_alive ());
    this->~channel ();
    new (this) channel (std::move (comm));
    return *this;
  }

  channel::channel (channel &&comm)
    : m_max_timeout_in_ms (comm.m_max_timeout_in_ms)
  {
    m_type = comm.m_type;
    comm.m_type = NO_TYPE;

    m_socket = comm.m_socket;
    comm.m_socket = INVALID_SOCKET;
  }

  channel::~channel ()
  {
    close_connection ();
  }

  css_error_code channel::send (const std::string &message)
  {
    return channel::send (message.c_str (), message.length ());
  }

  css_error_code channel::recv (char *buffer, std::size_t &maxlen_in_recvlen_out)
  {
    // --- hack ---
    // choose queue direction according to even/odd timeout
    std::queue<std::string> *q = m_max_timeout_in_ms % 2 == 0 ? &cs_q : &sc_q;
    std::condition_variable *cv = m_max_timeout_in_ms % 2 == 0 ? &cs_cv : &sc_cv;
    std::mutex *mutex = m_max_timeout_in_ms % 2 == 0 ? &cs_mutex : &sc_mutex;

    std::unique_lock<std::mutex> lk (*mutex);
    if (m_max_timeout_in_ms < 0)
      {
	cv->wait (lk);
      }
    else
      {
	cv->wait_for (lk, std::chrono::milliseconds (m_max_timeout_in_ms));
      }
    if (q->empty ())
      {
	return NO_DATA_AVAILABLE;
      }

    std::string msg = q->front ();
    size_t maxlen = maxlen_in_recvlen_out > msg.length () ? msg.length () : maxlen_in_recvlen_out;
    memcpy (buffer, msg.c_str (), maxlen);
    maxlen_in_recvlen_out = maxlen;
    q->pop ();
    return NO_ERRORS;
  }

  css_error_code channel::send (const char *buffer, std::size_t length)
  {
    std::queue<std::string> *q = m_max_timeout_in_ms % 2 != 0 ? &cs_q : &sc_q;
    std::condition_variable *cv = m_max_timeout_in_ms % 2 != 0 ? &cs_cv : &sc_cv;
    std::mutex *mutex = m_max_timeout_in_ms % 2 != 0 ? &cs_mutex : &sc_mutex;

    std::string msg (buffer, length);
    std::unique_lock<std::mutex> lk (*mutex);
    q->push (std::move (msg));
    cv->notify_one ();
    return NO_ERRORS;
  }

  bool channel::send_int (int)
  {
    assert (0);
    return NO_ERRORS;
  }

  css_error_code channel::recv_int (int &)
  {
    assert (0);
    return NO_ERRORS;
  }

  css_error_code channel::connect (const char *, int)
  {
    assert (0);
    return NO_ERRORS;
  }

  css_error_code channel::accept (SOCKET)
  {
    assert (0);
    return NO_ERRORS;
  }

  void channel::close_connection ()
  {
    std::condition_variable *cv = m_max_timeout_in_ms % 2 != 0 ? &cs_cv : &sc_cv;
    cv->notify_one ();
  }

  int channel::get_max_timeout_in_ms ()
  {
    return m_max_timeout_in_ms;
  }

  int channel::wait_for (unsigned short int, unsigned short int &)
  {
    assert (0);
    return 0;
  }

  bool channel::is_connection_alive () const
  {
    return !IS_INVALID_SOCKET (m_socket);
  }

} /* namespace cubcomm */
