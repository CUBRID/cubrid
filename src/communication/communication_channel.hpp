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

#ifndef _COMMUNICATION_CHANNEL_HPP
#define _COMMUNICATION_CHANNEL_HPP

#include <string>
#include <mutex>
#include "connection_support.h"
#include "connection_defs.h"
#include <memory>

#if !defined (WINDOWS)
#include <sys/uio.h>
#else
#include <winsock2.h>
#endif

enum CHANNEL_TYPE
{
  NO_TYPE = 0,
  INITIATOR,
  LISTENER
};

/* so that unique_ptr knows to "free (char *)" and not to "delete char *" */
struct communication_channel_c_free_deleter
{
  void operator() (char *ptr)
  {
    if (ptr != NULL)
      {
	free (ptr);
      }
  }
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
    int recv (char *buffer, int &received_length);
    int send (const std::string &message);
    int send (const char *buffer, int length);

    /* simple connect */
    virtual int connect (const char *hostname, int port);

    /* creates a listener channel */
    int accept (SOCKET socket);

    /* this function waits for events such as EPOLLIN, EPOLLOUT,
     * if (revents & EPOLLIN) != 0 it means that we have an "in" event
     */
    int wait_for (unsigned short int events, unsigned short int &revents);

    bool is_connection_alive ();
    SOCKET get_socket ();
    void close_connection ();

    /* this is the command that the non overridden connect will send */
    const int &get_max_timeout_in_ms ();

  protected:
    const int m_max_timeout_in_ms;
    CHANNEL_TYPE m_type;
    SOCKET m_socket;
};

#endif /* _COMMUNICATION_CHANNEL_HPP */

