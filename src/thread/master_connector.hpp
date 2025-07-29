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
 * master_connector.hpp
 */

#ifndef _THREAD_MASTER_CONNECTOR_HPP_
#define _THREAD_MASTER_CONNECTOR_HPP_

#include "connection_globals.h"
#include "epoll.hpp"
#include "connection_sr.h"
#include "tcp.h"
#include "span.hpp"
#include "porting.h"

#include <iostream>
#include <string>
#include <type_traits>

namespace cubthread
{
  template <typename T>
  class master_connector
  {
    static_assert(
      std::is_same<T, cubsocket::epoll>::value,
      "T must be the child of cubsocket::nonblocking (cubsocket::epoll)"
    );

  public:
    master_connector ();
    ~master_connector ();
    
    bool connect (int port, std::string &server_name);

    bool send ();

  private:
    const int m_bufsize = 8;

    T m_events;
    std::vector<cubbase::span<std::byte>> m_sendbuf;
    std::vector<cubbase::span<std::byte>> m_recvbuf;

    int m_port;

    void set_proc_register (CSS_SERVER_PROC_REGISTER * proc_register, std::string &server_name);
  };

  template <typename T>
  master_connector<T>::master_connector ()
  {
    m_sendbuf.reserve (m_bufsize);
    m_recvbuf.reserve (m_bufsize);
  }

  template <typename T>
  master_connector<T>::~master_connector ()
  {
  }

  template <typename T>
  bool master_connector<T>::connect (int port, std::string &server_name)
  {
    CSS_SERVER_PROC_REGISTER proc_register = CSS_SERVER_PROC_REGISTER_INITIALIZER;
    char hostname[CUB_MAXHOSTNAMELEN];
    NET_HEADER header;
    SOCKET fd;

    if (GETHOSTNAME (hostname, CUB_MAXHOSTNAMELEN) != 0)
    {
      return false;
    }

    fd = css_tcp_client_open ((char *) hostname, port);
    if (IS_INVALID_SOCKET (fd))
    {
      return false;
    }

    assert (!this->m_events.is_nonblocking (fd));

    std::cout << "fd: " << fd << std::endl << "is nonblocking: " << this->m_events.is_nonblocking (fd) << std::endl;

    m_sendbuf.clear ();
    
    memset ((char *) &header, 0, sizeof (NET_HEADER));
    memcpy ((char *) &header, css_Net_magic, sizeof (css_Net_magic));
    m_sendbuf.push_back ({ reinterpret_cast<std::byte *> (&header), sizeof (NET_HEADER) });

    this->set_proc_register (&proc_register, server_name);
    //data = (const char *) &proc_register;
    //data_length = sizeof (proc_register);

//    conn = css_make_conn (0);
    close (fd);
    /*
    if (css_send_magic (conn) != NO_ERRORS)
    {
      return false;
    }

    if (css_send_request (conn, SERVER_REQUEST_FROM_SERVER, rid, server_name, server_name_length) != NO_ERRORS)
    {
      return false;
    }
    */
    return true;
  }

  template <typename T>
  bool master_connector<T>::send ()
  {
    //this->m_events.wait ();
  }
  
  template <typename T>
  void master_connector<T>::set_proc_register (CSS_SERVER_PROC_REGISTER * proc_register, std::string &server_name)
  {
    char *p, *last;
    char **argv;

    memcpy (proc_register->server_name, server_name.c_str (), server_name.length ());
    proc_register->server_name_length = server_name.length ();
    proc_register->pid = getpid (); 
    strncpy_bufsize (proc_register->exec_path, css_get_exec_path ());

    p = (char *) proc_register->args;
    last = p + proc_register->CSS_SERVER_MAX_SZ_PROC_ARGS;
    for (argv = css_get_argv (); *argv; argv++)
    {
      p += snprintf (p, MAX ((last - p), 0), "%s ", *argv);
    }
  }


}

#endif
