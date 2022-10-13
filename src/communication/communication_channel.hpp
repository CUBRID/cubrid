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
 * communication_channel.hpp - wrapper for communication primitives
 */

#ifndef _COMMUNICATION_CHANNEL_HPP_
#define _COMMUNICATION_CHANNEL_HPP_

#include "connection_support.h"
#include "connection_defs.h"

#include <string>
#include <mutex>
#include <memory>
#include <sstream>
#if !defined (WINDOWS)
#include <sys/uio.h>
#else
#include <winsock2.h>
#endif

namespace cubcomm
{

  /* TODO :
   * optimal TCP packet size ~ 1500 bytes and TCP header is 16 bytes and we need each read aligned to 8 bytes */
  const std::size_t MTU = 1480;

  enum CHANNEL_TYPE
  {
    NO_TYPE = 0,
    INITIATOR,
    LISTENER
  };

  class channel
  {
      constexpr static int INVALID_PORT = -1;

    public:
      channel (int max_timeout_in_ms);
      channel (std::string &&channel_name);
      channel (int max_timeout_in_ms, std::string &&channel_name);
      virtual ~channel ();

      /* only the move operation is permitted */
      channel (const channel &) = delete;
      channel (channel &&comm);

      channel &operator= (const channel &) = delete;
      channel &operator= (channel &&) = delete;

      /* receive/send functions that use the created m_socket */
      css_error_code recv (char *buffer, std::size_t &maxlen_in_recvlen_out);
      css_error_code send (const std::string &message);
      css_error_code send (const char *buffer, std::size_t length);
      css_error_code send_int (int val);
      css_error_code recv_int (int &received);

      /* simple connect */
      virtual css_error_code connect (const char *hostname, int port);

      /* creates a listener channel */
      virtual css_error_code accept (SOCKET socket);

      /* this function waits for events such as EPOLLIN, EPOLLOUT,
       * if (revents & EPOLLIN) != 0 it means that we have an "in" event
       */
      int wait_for (unsigned short int events, unsigned short int &revents) const;

      bool is_connection_alive () const;
      SOCKET get_socket () const;
      int get_port () const;
      void close_connection ();

      /* this is the command that the non overridden connect will send */
      int get_max_timeout_in_ms () const;

      void set_channel_name (std::string &&name)
      {
	m_channel_name = std::move (name);
      }

      std::string get_channel_id () const
      {
	std::stringstream ss;

	ss << m_channel_name << "_" << m_hostname;

	if (m_port != -1)
	  {
	    ss << "_" << m_port;
	  }

	ss << "_" << m_socket;

	return ss.str ();
      }

    protected:
      const int m_max_timeout_in_ms = -1;
      CHANNEL_TYPE m_type = NO_TYPE;
      SOCKET m_socket = INVALID_SOCKET;
      std::string m_channel_name;
      std::string m_hostname;
      int m_port = INVALID_PORT;
  };


} /* cubcomm namespace */

#endif /* _COMMUNICATION_CHANNEL_HPP_ */

