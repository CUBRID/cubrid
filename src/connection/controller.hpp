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
 * controller.hpp
 */

#ifndef _CONTROLLER_HPP_
#define _CONTROLLER_HPP_

#include "buffer.hpp"
#include "system_parameter.h"
#include "error_manager.h"

#include <string>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace cubconn::connection
{
  template <typename RX, typename TX>
  class controller
  {
    public:
      controller ();
      ~controller ();

      controller (const controller &) = delete;
      controller &operator= (const controller &) = delete;
      controller (controller &&) noexcept;
      controller &operator= (controller &&) noexcept;

      bool open (std::string path, int flags);
      int get_fd ();

      result recv (RX &data, sockaddr_un &peer, socklen_t &peerlen);
      result send (TX &data, sockaddr_un &peer, socklen_t &peerlen);

    private:
      int m_ctrlfd;
      std::string m_path;
  };

  template <typename RX, typename TX>
  controller<RX, TX>::controller () :
    m_ctrlfd (-1),
    m_path ()
  {
  }

  template <typename RX, typename TX>
  controller<RX, TX>::controller (controller &&other) noexcept :
    m_ctrlfd (other.m_ctrlfd),
    m_path (std::move (other.m_path))
  {
    other.m_ctrlfd = -1;
    other.m_path.clear ();
  }

  template <typename RX, typename TX>
  controller<RX, TX> &controller<RX, TX>::operator= (controller &&other) noexcept
  {
    if (this != &other)
      {
	if (m_ctrlfd >= 0)
	  {
	    ::close (m_ctrlfd);
	  }
	if (!m_path.empty ())
	  {
	    ::unlink (m_path.c_str());
	  }

	m_ctrlfd = other.m_ctrlfd;
	m_path = std::move (other.m_path);

	other.m_ctrlfd = -1;
	other.m_path.clear ();
      }

    return *this;
  }

  template <typename RX, typename TX>
  controller<RX, TX>::~controller ()
  {
    if (m_ctrlfd >= 0)
      {
	::close (m_ctrlfd);
      }
    if (!m_path.empty ())
      {
	::unlink (m_path.c_str());
      }
  }

  template <typename RX, typename TX>
  bool controller<RX, TX>::open (std::string path, int flags)
  {
    sockaddr_un addr;

    assert (flags & SOCK_NONBLOCK);

    if (m_ctrlfd >= 0)
      {
	::close (m_ctrlfd);
	m_ctrlfd = -1;
      }
    if (!m_path.empty ())
      {
	::unlink (m_path.c_str());
	m_path.clear ();
      }

    /* remove the first one if a previous exists */
    ::unlink (path.c_str());

    m_ctrlfd = ::socket (AF_UNIX, SOCK_DGRAM | flags, 0);
    if (m_ctrlfd < 0)
      {
	er_log_debug (__FILE__, __LINE__, "controller: failed to make new socket: %s\n", strerror (errno));
	return false;
      }

    addr.sun_family = AF_UNIX;
    std::snprintf (addr.sun_path, sizeof (addr.sun_path), "%s", path.c_str ());

    if (::bind (m_ctrlfd, reinterpret_cast<sockaddr *> (&addr), sizeof (addr)) < 0)
      {
	er_log_debug (__FILE__, __LINE__, "controller: bind failed: %s\n", strerror (errno));
	::close (m_ctrlfd);
	m_ctrlfd = -1;
	return false;
      }
    m_path = path;

    er_log_debug (__FILE__, __LINE__, "controller: bind unix to %s\n", m_path.c_str ());

    return true;
  }

  template <typename RX, typename TX>
  int controller<RX, TX>::get_fd ()
  {
    return m_ctrlfd;
  }

  template <typename RX, typename TX>
  result controller<RX, TX>::recv (RX &data, sockaddr_un &peer, socklen_t &peerlen)
  {
    ssize_t bytes;

    peerlen = sizeof (peer);
    bytes = ::recvfrom (m_ctrlfd, &data, sizeof (data), MSG_TRUNC, reinterpret_cast<sockaddr *> (&peer), &peerlen);
    if (bytes < 0)
      {
	if (errno == EAGAIN || errno == EWOULDBLOCK)
	  {
	    return result::Pending;
	  }
	return result::Error;
      }

    if (bytes != sizeof (RX))
      {
	return result::Error;
      }
    return result::Ok;
  }

  template <typename RX, typename TX>
  result controller<RX, TX>::send (TX &data, sockaddr_un &peer, socklen_t &peerlen)
  {
    ssize_t bytes;

    bytes = ::sendto (m_ctrlfd, &data, sizeof (data), 0, reinterpret_cast<const sockaddr *> (&peer), peerlen);
    if (bytes < 0)
      {
	if (errno == EAGAIN || errno == EWOULDBLOCK)
	  {
	    return result::Pending;
	  }
	return result::Error;
      }

    if (bytes != sizeof (TX))
      {
	return result::Error;
      }
    return result::Ok;
  }
}

#endif
