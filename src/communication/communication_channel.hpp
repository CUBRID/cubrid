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

/* TODO[arnia] this needs to be calculated a priori */
#define MTU 1500

enum CHANNEL_TYPE
{
  NO_TYPE = 0,
  INITIATOR,
  LISTENER
};

class communication_channel
{
  public:
    communication_channel (int max_timeout_in_ms = -1);
    virtual ~communication_channel ();

    /* only the move operation is permitted */
    communication_channel (const communication_channel &comm) = delete;
    communication_channel &operator= (const communication_channel &comm) = delete;
    communication_channel (communication_channel &&comm);
    communication_channel &operator= (communication_channel &&comm);

    /* receive/send functions that use the created m_socket */
    css_error_code recv (char *buffer, std::size_t & maxlen_in_recvlen_out);
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

/* TODO[arnia] these will be added from razvan's branch repl_base */
typedef char BUFFER_UNIT;
typedef unsigned long long stream_position;

struct stream_handler
{
  virtual int handling_action (BUFFER_UNIT *ptr, std::size_t byte_count) = 0;
  virtual ~stream_handler() = default;
};

class packing_stream
{
public:
  virtual int write (std::size_t byte_count, stream_handler *handler) = 0;
  virtual int read (stream_position first_pos, std::size_t byte_count, stream_handler *handler) = 0;
  virtual ~packing_stream() = default;
};

/* TODO[arnia] end */

#endif /* _COMMUNICATION_CHANNEL_HPP_ */

