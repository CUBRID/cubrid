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

enum CHANNEL_TYPE
{
  INITIATOR = 0,
  LISTENER
};

class communication_channel
{
  public:
    /* this constructor makes an initiator type channel,
     * it connects using the connect function and the
     * provided information (hostname, port etc)
     */
    communication_channel (const char *hostname, int port, int max_timeout_in_ms = -1,
			   CSS_COMMAND_TYPE command_type = NULL_REQUEST, const char *server_name = NULL);
    /* this constructor creates a listener channel,
     * it receives a socket created by an "accept" call
     */
    communication_channel (int sock_fd = -1, int max_timeout_in_ms = -1);
    virtual ~communication_channel ();

    /* only the move operation is permitted */
    communication_channel (const communication_channel &comm) = delete;
    communication_channel &operator= (const communication_channel &comm) = delete;
    communication_channel (communication_channel &&comm);
    communication_channel &operator= (communication_channel &&comm);

    /* receive/send functions that use the created CSS_CONN_ENTRY */
    int recv (char *buffer, int &received_length);
    int send (const char *message, int message_length);
    int send (const std::string &message);

    /* the non overridden connect function uses the
     * css_common_connect that connects to an endpoint
     * and also sends a command to the endpoint (m_command_type)
     */
    virtual int connect ();

    /* this function waits for events such as EPOLLIN, EPOLLOUT,
     * if (revents & EPOLLIN) != 0 it means that we have an "in" event
     */
    int wait_for (unsigned short int events, unsigned short int &revents);

    bool is_connection_alive ();
    CSS_CONN_ENTRY *get_conn_entry ();
    void close_connection ();

    /* this is the command that the non overridden connect will send */
    void set_command_type (CSS_COMMAND_TYPE cmd);
    const int &get_max_timeout_in_ms ();
  protected:
    CSS_CONN_ENTRY *m_conn_entry;
    const int m_max_timeout_in_ms;
    std::unique_ptr <char> m_hostname, m_server_name;
    int m_port;
    unsigned short m_request_id;
    CHANNEL_TYPE m_type;
    CSS_COMMAND_TYPE m_command_type;
};

#endif /* _COMMUNICATION_CHANNEL_HPP */
