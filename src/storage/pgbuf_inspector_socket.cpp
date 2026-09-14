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

#include "config.h"
#include "pgbuf_inspector_socket.hpp"
#include "pgbuf_inspector_wire.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/random.h>
#include <unistd.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubpgbuf
{
  namespace inspector
  {
    namespace
    {
      std::uint64_t monotonic_us (std::chrono::steady_clock::time_point now)
      {
	return std::chrono::duration_cast<std::chrono::microseconds> (
		       now.time_since_epoch ()).count ();
      }
      std::uint64_t wall_us ()
      {
	auto value = std::chrono::duration_cast<std::chrono::microseconds> (
			     std::chrono::system_clock::now ().time_since_epoch ()).count ();
	return value < 0 ? 0 : static_cast<std::uint64_t> (value);
      }
      bool same_socket (const struct stat &a, const struct stat &b)
      {
	return S_ISSOCK (a.st_mode) && a.st_uid == b.st_uid && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
      }
      std::string refusal (refusal_code code)
      {
	error_frame frame;
	frame.code = code;
	if (code == refusal_code::VERSION_UNSUPPORTED) frame.supported_majors = {1};
	std::string result;
	encode_error_frame (frame, result);
	return result;
      }
    }
    endpoint::~endpoint ()
    {
      stop ();
    }
    void endpoint::close_client (client &c)
    {
      if (c.fd >= 0)
	{
	  ::close (c.fd);
	}
      c = client {};
    }
    void endpoint::stop ()
    {
      for (auto &c : m_clients)
	{
	  close_client (c);
	}
      if (m_listener >= 0)
	{
	  ::close (m_listener);
	}
      m_listener = -1;
      struct stat now;
      if (m_created && fstatat (m_directory, m_key.c_str (), &now, AT_SYMLINK_NOFOLLOW) == 0
	  && same_socket (now, m_socket))
	{
	  unlinkat (m_directory, m_key.c_str (), 0);
	}
      m_created = false;
      if (m_directory >= 0)
	{
	  ::close (m_directory);
	}
      m_directory = -1;
    }
    bool endpoint::start (const std::string &root, const std::string &key, const identity &db,
			  std::function<bool (identity &)> refresh, std::optional<scan_source> source)
    {
      if (m_attempted)
	{
	  return false;
	}
      m_attempted = true;
      m_refresh = std::move (refresh);
      m_has_source = source.has_value ();
      if (source)
	{
	  m_source = std::move (*source);
	}
      m_source.shared = db.shared_lru_count;
      m_source.private_count = db.private_lru_count;
      if (key.empty () || key.size () > 64
	  || key.find_first_not_of ("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
	{
	  return false;
	}
      m_key = key + ".sock";
      m_path = root + "/pgbuf-inspector/" + m_key;
      sockaddr_un address {};
      if (m_path.size () >= sizeof (address.sun_path))
	{
	  return false;
	}
      address.sun_family = AF_UNIX;
      std::memcpy (address.sun_path, m_path.c_str (), m_path.size () + 1);
      unsigned char random[16];
      std::size_t filled = 0;
      while (filled < sizeof (random))
	{
	  auto n = getrandom (random + filled, sizeof (random) - filled, GRND_NONBLOCK);
	  if (n < 0 && errno == EINTR)
	    {
	      continue;
	    }
	  if (n <= 0)
	    {
	      return false;
	    }
	  filled += n;
	}
      const char *hex = "0123456789abcdef";
      for (auto byte : random)
	{
	  m_incarnation += hex[byte >> 4];
	  m_incarnation += hex[byte & 15];
	}
      if (!make_hello (db))
	{
	  return false;
	}
      int parent = open (root.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (parent < 0)
	{
	  return false;
	}
      if (mkdirat (parent, "pgbuf-inspector", 0700) != 0 && errno != EEXIST)
	{
	  ::close (parent);
	  return false;
	}
      m_directory = openat (parent, "pgbuf-inspector", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      ::close (parent);
      struct stat directory;
      if (m_directory < 0 || fstat (m_directory, &directory) != 0 || directory.st_uid != geteuid ()
	  || (directory.st_mode & 07777) != 0700)
	{
	  stop ();
	  return false;
	}
      struct stat existing;
      if (fstatat (m_directory, m_key.c_str (), &existing, AT_SYMLINK_NOFOLLOW) == 0)
	{
	  if (!S_ISSOCK (existing.st_mode) || existing.st_uid != geteuid ())
	    {
	      stop ();
	      return false;
	    }
	  int probe = socket (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	  if (probe < 0)
	    {
	      stop ();
	      return false;
	    }
	  int result = connect (probe, reinterpret_cast<sockaddr *> (&address), sizeof (address));
	  int failure = errno;
	  ::close (probe);
	  struct stat current;
	  if (result == 0 || failure != ECONNREFUSED
	      || fstatat (m_directory, m_key.c_str (), &current, AT_SYMLINK_NOFOLLOW) != 0
	      || !same_socket (current, existing) || unlinkat (m_directory, m_key.c_str (), 0) != 0)
	    {
	      stop ();
	      return false;
	    }
	}
      else if (errno != ENOENT)
	{
	  stop ();
	  return false;
	}
      m_listener = socket (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      if (m_listener < 0 || bind (m_listener, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
	{
	  stop ();
	  return false;
	}
      if (fstatat (m_directory, m_key.c_str (), &m_socket, AT_SYMLINK_NOFOLLOW) != 0)
	{
	  stop ();
	  return false;
	}
      m_created = true;
      if (fchmodat (m_directory, m_key.c_str (), 0600, 0) != 0 || listen (m_listener, 2) != 0)
	{
	  stop ();
	  return false;
	}
      return true;
    }
    bool endpoint::make_hello (const identity &db)
    {
      m_hello.clear ();
      std::string semantic = "{\"type\":\"server_hello\",\"protocol_major\":1,\"protocol_minor\":0,\"incarnation\":\""
			     + m_incarnation + "\",\"database_creation\":\"" + std::to_string (db.database_creation)
			     + "\",\"shared_lru_count\":" + std::to_string (db.shared_lru_count)
			     + ",\"private_lru_count\":" + std::to_string (db.private_lru_count) + ",\"volumes\":[";
      bool oversized = db.oversized;
      for (const auto &v : db.volumes)
	{
	  std::string entry = (semantic.back () == '[' ? "" : ",") + std::string ("{\"volid\":")
			      + std::to_string (v.volid) + ",\"volume_creation\":\"" + std::to_string (v.creation)
			      + "\",\"device\":\"" + std::to_string (v.device) + "\",\"inode\":\"" + std::to_string (v.inode) + "\"}";
	  if (semantic.size () + entry.size () + 3 > WIRE_HANDSHAKE_FRAME_MAX_BYTES)
	    {
	      oversized = true;
	      break;
	    }
	  semantic += entry;
	}
      if (oversized)
	{
	  m_hello = refusal (refusal_code::IDENTITY_OVERSIZED);
	}
      else
	{
	  semantic += "]}";
	  if (encode_frame (semantic, m_hello) != encode_status::OK)
	    {
	      return false;
	    }
	}
      return true;
    }
    void endpoint::receive (client &c)
    {
      char bytes[4096];
      auto n = recv (c.fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
	{
	  return;
	}
      if (n <= 0 || c.input.size () + n > WIRE_CONTROL_FRAME_MAX_BYTES)
	{
	  close_client (c);
	  return;
	}
      c.input.append (bytes, n);
      auto lf = c.input.find ('\n');
      if (lf == std::string::npos)
	{
	  if (c.input.size () == WIRE_CONTROL_FRAME_MAX_BYTES)
	    {
	      close_client (c);
	    }
	  return;
	}
      if (c.ready)
	{
	  std::string incarnation;
	  if (!decode_scan_request (std::string_view (c.input.data (), lf + 1), incarnation)
	      || lf + 1 != c.input.size ())
	    {
	      close_client (c);
	      return;
	    }
	  c.input.clear ();
	  auto now = m_now ();
	  c.progress = now;
	  if (incarnation != m_incarnation)
	    {
	      c.output = refusal (refusal_code::INCARNATION_CHANGED);
	      c.closing = true;
	    }
	  else if (m_sequence && now - m_last_scan < std::chrono::milliseconds (100))
	    {
	      error_frame frame;
	      frame.code = refusal_code::RATE_LIMITED;
	      auto remaining = std::chrono::milliseconds (100) - (now - m_last_scan);
	      frame.retry_after_ms = 1 + std::chrono::duration_cast<std::chrono::milliseconds> (remaining).count ();
	      encode_error_frame (frame, c.output);
	    }
	  else
	    {
	      if (!m_has_source || m_sequence == UINT64_MAX)
		{
		  close_client (c);
		  return;
		}
	      m_last_scan = c.scan_started = now;
	      c.output.reserve (OUTPUT_BUFFER_BYTES);
	      c.scanning = true;
	      if (!c.scan.begin (m_source, m_incarnation, ++m_sequence, m_next_start,
				 monotonic_us (m_now ()), wall_us (), c.output))
		{
		  close_client (c);
		}
	    }
	  return;
	}
      client_hello hello;
      if (!decode_client_hello (std::string_view (c.input.data (), lf + 1), hello))
	{
	  close_client (c);
	  return;
	}
      c.input.erase (0, lf + 1);
      if (!hello.supports_v1)
	{
	  c.output = refusal (refusal_code::VERSION_UNSUPPORTED);
	  c.closing = true;
	}
      else if (!hello.expected_incarnation.empty () && hello.expected_incarnation != m_incarnation)
	{
	  c.output = refusal (refusal_code::INCARNATION_CHANGED);
	  c.closing = true;
	}
      else
	{
	  if (m_refresh)
	    {
	      identity current;
	      if (!m_refresh (current) || !make_hello (current))
		{
		  close_client (c);
		  return;
		}
	    }
	  c.output = m_hello;
	  c.closing = m_hello.find ("\"error\"") != std::string::npos;
	}
      c.progress = m_now ();
    }
    void endpoint::write (client &c)
    {
      if ((!c.ready && m_now () - c.attached >= std::chrono::milliseconds (500))
	  || (c.scanning && m_now () - c.scan_started >= std::chrono::seconds (2)))
	{
	  close_client (c);
	  return;
	}
      auto n = send (c.fd, c.output.data () + c.written, c.output.size () - c.written, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
	{
	  return;
	}
      if (n <= 0)
	{
	  close_client (c);
	  return;
	}
      c.written += n;
      c.progress = m_now ();
      if (c.written == c.output.size ())
	{
	  if (c.closing || !c.input.empty ())
	    {
	      close_client (c);
	      return;
	    }
	  c.output.clear ();
	  c.written = 0;
	  c.ready = true;
	  if (c.scanning && c.scan.done ())
	    {
	      c.scanning = false;
	    }
	}
    }
    void endpoint::advance_scan (client &c)
    {
      // A poll turn is bounded too, so shutdown need not wait for a scan deadline.
      auto turn_end = clock::now () + std::chrono::milliseconds (2);
      while (c.fd >= 0 && c.scanning && clock::now () < turn_end)
	{
	  char unexpected;
	  auto n = recv (c.fd, &unexpected, 1, MSG_PEEK | MSG_DONTWAIT);
	  if (n == 0 || n > 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
	    {
	      close_client (c);
	      return;
	    }
	  if (c.scan.done ())
	    {
	      return;
	    }
	  // Compact only the bounded queue; never retain the scan's already sent frames.
	  if (c.written)
	    {
	      c.output.erase (0, c.written);
	      c.written = 0;
	    }
	  if (c.output.size () + WIRE_PAGE_FRAME_MAX_BYTES > OUTPUT_BUFFER_BYTES)
	    {
	      return;
	    }
	  if (!c.scan.step (monotonic_us (m_now ()), wall_us (), c.output))
	    {
	      close_client (c);
	      return;
	    }
	  if (c.scan.done ())
	    {
	      m_next_start = c.scan.next_start ();
	    }
	}
    }

    void endpoint::poll ()
    {
      if (m_listener < 0)
	{
	  return;
	}
      int fd = accept4 (m_listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
      if (fd >= 0)
	{
	  ucred peer {};
	  socklen_t length = sizeof (peer);
	  if (getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0 || length != sizeof (peer) || peer.uid != geteuid ())
	    {
	      ::close (fd);
	    }
	  else
	    {
	      auto slot = std::find_if (m_clients.begin (), m_clients.end (), [] (const client &c)
	      {
		return c.fd < 0;
	      });
	      if (slot == m_clients.end ())
		{
		  auto busy = refusal (refusal_code::BUSY);
		  send (fd, busy.data (), busy.size (), MSG_DONTWAIT | MSG_NOSIGNAL);
		  ::close (fd);
		}
	      else
		{
		  int send_budget = 4096;
		  if (setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &send_budget, sizeof (send_budget)) != 0)
		    {
		      ::close (fd);
		    }
		  else
		    {
		      slot->fd = fd;
		      slot->attached = slot->progress = m_now ();
		    }
		}
	    }
	}
      for (auto &c : m_clients)
	{
	  if (c.fd < 0)
	    {
	      continue;
	    }
	  auto now = m_now ();
	  if ((!c.ready && now - c.attached >= std::chrono::milliseconds (500))
	      || (!c.output.empty () && now - c.progress >= std::chrono::milliseconds (250))
	      || (c.scanning && now - c.scan_started >= std::chrono::seconds (2)))
	    {
	      close_client (c);
	      continue;
	    }
	  if (c.output.empty () && !c.scanning)
	    {
	      receive (c);
	    }
	  if (c.fd >= 0 && c.scanning)
	    {
	      advance_scan (c);
	    }
	  if (c.fd >= 0 && !c.output.empty ())
	    {
	      write (c);
	    }
	}
    }
  }
}
