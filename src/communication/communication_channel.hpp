/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#if !defined (WINDOWS)
#include <sys/uio.h>
#else
#include <winsock2.h>
#endif

namespace cubcomm
{

  /* TODO[arnia] this needs to be calculated a priori */
  const int MTU = 1500;

  enum CHANNEL_TYPE
  {
    NO_TYPE = 0,
    INITIATOR,
    LISTENER
  };

  class channel
  {
    public:
      channel (int max_timeout_in_ms = -1);
      virtual ~channel ();

      /* only the move operation is permitted */
      channel (const channel &comm) = delete;
      channel &operator= (const channel &comm) = delete;
      channel (channel &&comm);
      channel &operator= (channel &&comm);

      /* receive/send functions that use the created m_socket */
      css_error_code recv (char *buffer, std::size_t &maxlen_in_recvlen_out);
      css_error_code send (const std::string &message);
      css_error_code send (const char *buffer, std::size_t length);

      /* simple connect */
      virtual css_error_code connect (const char *hostname, int port);

      /* creates a listener channel */
      css_error_code accept (SOCKET socket);

      /* this function waits for events such as EPOLLIN, EPOLLOUT,
       * if (revents & EPOLLIN) != 0 it means that we have an "in" event
       */
      int wait_for (unsigned short int events, unsigned short int &revents);

      bool is_connection_alive ();
      SOCKET get_socket ();
      void close_connection ();

      /* this is the command that the non overridden connect will send */
      int get_max_timeout_in_ms ();

    protected:
      const int m_max_timeout_in_ms;
      CHANNEL_TYPE m_type;
      SOCKET m_socket;
  };

} /* cubcomm namespace */

#endif /* _COMMUNICATION_CHANNEL_HPP_ */

