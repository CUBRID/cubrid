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
 * communication_channel.hpp - wrapper for the CSS_CONN_ENTRY structure
 */

#ifndef _COMMUNICATION_CHANNEL_HPP
#define _COMMUNICATION_CHANNEL_HPP

#include <string>
#include <mutex>
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


    void create_initiator (const char *hostname, int port, CSS_COMMAND_TYPE command_type = NULL_REQUEST,
			   const char *server_name = NULL);

    /* receive/send functions that use the created CSS_CONN_ENTRY */
    int recv (char *buffer, int &received_length);
    int send (const std::string &message);

    template<typename T_CONST_CHAR, typename T_INT, typename... Targs>
    int send (T_CONST_CHAR message, T_INT message_length, Targs... args)
    {
      const int num_of_args = (2 + sizeof... (Targs));
      int templen;

      if (current_iov_size < num_of_args)
	{
	  iov_ptr = (struct iovec *) realloc (iov_ptr, sizeof (struct iovec) * num_of_args);
	  current_iov_size = num_of_args;
	  current_iov_pointer = 0;
	}

      css_set_io_vector (& (iov_ptr[current_iov_pointer]), & (iov_ptr[current_iov_pointer+1]), message, message_length,
			 &templen);
      current_iov_pointer += 2;

      return send (args...);
    }

    /* the non overridden connect function uses the
     * css_common_connect that connects to an endpoint
     * and also sends a command to the endpoint (m_command_type)
     */
    virtual int connect ();

    /* replaces existing information and connects */
    int connect (const char *hostname, int port, CSS_COMMAND_TYPE command_type = NULL_REQUEST,
		 const char *server_name = NULL);

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
    void set_command_type (CSS_COMMAND_TYPE cmd);
    const int &get_max_timeout_in_ms ();

  protected:
    const int m_max_timeout_in_ms;
    std::unique_ptr <char, communication_channel_c_free_deleter> m_hostname, m_server_name;
    int m_port;
    unsigned short m_request_id;
    CHANNEL_TYPE m_type;
    CSS_COMMAND_TYPE m_command_type;
    SOCKET m_socket;
    unsigned int current_iov_pointer, current_iov_size;
    struct iovec *iov_ptr;

  private:
    int send ();
};

#endif /* _COMMUNICATION_CHANNEL_HPP */

