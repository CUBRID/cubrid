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

#include "catch2/catch.hpp"
#include "pgbuf_inspector_socket.hpp"
#include "pgbuf_inspector_wire.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <cstring>
#include <thread>
#include <sys/wait.h>
#include <cerrno>
#include <sys/ioctl.h>
#include <poll.h>
#include <utility>

using namespace cubpgbuf::inspector;

TEST_CASE ("Private attachment negotiates identity and removes its socket", "[pgbuf_inspector][socket]")
{
  char root[] = "/tmp/pgbuf-attach-XXXXXX";
  REQUIRE (mkdtemp (root) != nullptr);
  identity db;
  db.database_creation = 1700000000;
  db.shared_lru_count = 4;
  db.private_lru_count = 8;
  db.volumes.push_back ({0, 1700000001, 42, 123});
  endpoint server;
  REQUIRE (server.start (root, "database", db, {}, scan_source {}));
  struct stat st;
  REQUIRE (lstat (server.path ().c_str (), &st) == 0);
  CHECK ((st.st_mode & 0777) == 0600);
  REQUIRE (stat ((std::string (root) + "/pgbuf-inspector").c_str (), &st) == 0);
  CHECK ((st.st_mode & 0777) == 0700);
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  REQUIRE (fd >= 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy (address.sun_path, server.path ().c_str ());
  REQUIRE (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
  std::string request = "{\"type\":\"client_hello\",\"supported_majors\":[1]}\n";
  REQUIRE (send (fd, request.data (), request.size (), 0) == static_cast<ssize_t> (request.size ()));
  for (int i = 0; i < 4; ++i)
    {
      server.poll ();
    }
  char response[4096];
  auto size = recv (fd, response, sizeof (response), MSG_DONTWAIT);
  REQUIRE (size > 0);
  std::string hello (response, size);
  CHECK (hello.find ("\"database_creation\":\"1700000000\"") != std::string::npos);
  CHECK (hello.find ("\"inode\":\"123\"") != std::string::npos);
  CHECK (hello.find ("\"private_lru_count\":8") != std::string::npos);
  CHECK (hello.find (root) == std::string::npos);
  auto inc_start = hello.find ("\"incarnation\":\"") + 15;
  auto incarnation = hello.substr (inc_start, 32);
  request = "{\"type\":\"scan_request\",\"incarnation\":\"" + incarnation + "\"}\n";
  REQUIRE (send (fd, request.data (), request.size (), 0) == static_cast<ssize_t> (request.size ()));
  std::string capture;
  for (int i = 0; i < 10; ++i)
    {
      server.poll ();
      auto count = recv (fd, response, sizeof (response), MSG_DONTWAIT);
      if (count > 0)
	{
	  capture.append (response, count);
	}
    }
  CHECK (capture.find ("\"type\":\"scan_header\"") != std::string::npos);
  CHECK (capture.find ("\"record_count\":0,\"visited_slots\":0,\"truncated\":false") != std::string::npos);

  close (fd);
  auto path = server.path ();
  server.stop ();
  CHECK_FALSE (std::filesystem::exists (path));
  std::filesystem::remove_all (root);
}

TEST_CASE ("Unavailable paths preserve entries and cannot be retried", "[pgbuf_inspector][socket]")
{
  char root[] = "/tmp/pgbuf-path-XXXXXX";
  REQUIRE (mkdtemp (root));
  identity db;
  db.volumes.push_back ({0, 1, 2, 3});
  std::string directory = std::string (root) + "/pgbuf-inspector";
  REQUIRE (mkdir (directory.c_str (), 0700) == 0);
  std::string path = directory + "/database.sock";
  SECTION ("symlink")
  {
    REQUIRE (symlink ("missing", path.c_str ()) == 0);
  }
  SECTION ("regular file")
  {
    auto *file = fopen (path.c_str (), "w");
    REQUIRE (file);
    fclose (file);
  }
  SECTION ("unsafe directory permissions")
  {
    REQUIRE (chmod (directory.c_str (), 0755) == 0);
  }
  struct stat entry_before
  {
  }, entry_after {};
  bool has_entry = lstat (path.c_str (), &entry_before) == 0;
  endpoint server;
  REQUIRE_FALSE (server.start (root, "database", db));
  if (has_entry)
    {
      REQUIRE (lstat (path.c_str (), &entry_after) == 0);
      CHECK (entry_after.st_ino == entry_before.st_ino);
      CHECK (entry_after.st_mode == entry_before.st_mode);
    }
  struct stat before;
  REQUIRE (lstat (directory.c_str (), &before) == 0);
  CHECK (std::filesystem::exists (directory));
  std::filesystem::remove_all (directory);
  REQUIRE_FALSE (server.start (root, "database", db));
  CHECK_FALSE (std::filesystem::exists (directory));
  std::filesystem::remove_all (root);
}

TEST_CASE ("Shutdown preserves a replacement socket", "[pgbuf_inspector][socket]")
{
  char root[] = "/tmp/pgbuf-replace-XXXXXX";
  REQUIRE (mkdtemp (root));
  identity db;
  db.volumes.push_back ({0, 1, 2, 3});
  endpoint server;
  REQUIRE (server.start (root, "database", db));
  auto path = server.path ();
  REQUIRE (unlink (path.c_str ()) == 0);
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  REQUIRE (fd >= 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy (address.sun_path, path.c_str ());
  REQUIRE (bind (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
  server.stop ();
  CHECK (std::filesystem::exists (path));
  close (fd);
  std::filesystem::remove_all (root);
}

namespace
{
  // Test-owned clients close even when a fatal assertion unwinds the case.
  class socket_client
  {
    public:
      explicit socket_client (int fd) : m_fd (fd) {}
      ~socket_client ()
      {
	if (m_fd >= 0)
	  {
	    ::close (m_fd);
	  }
      }
      socket_client (const socket_client &) = delete;
      socket_client &operator= (const socket_client &) = delete;
      socket_client (socket_client &&other) noexcept : m_fd (std::exchange (other.m_fd, -1)) {}
      operator int () const
      {
	return m_fd;
      }
    private:
      int m_fd;
  };
  struct socket_fixture
  {
    char root[64] = "/tmp/pgbuf-protocol-XXXXXX";
    identity db;
    endpoint server;
    socket_fixture (scan_source source = {}, std::function<endpoint::clock::time_point ()> now = endpoint::clock::now)
      : server (std::move (now))
    {
      REQUIRE (mkdtemp (root));
      db.database_creation = 1700000000;
      db.volumes.push_back ({0, 1700000001, 42, 123});
      REQUIRE (server.start (root, "database", db, {}, source));
    }
    ~socket_fixture ()
    {
      server.stop ();
      std::filesystem::remove_all (root);
    }
    int connect_client ()
    {
      int fd = socket (AF_UNIX, SOCK_STREAM, 0);
      REQUIRE (fd >= 0);
      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      std::strcpy (address.sun_path, server.path ().c_str ());
      REQUIRE (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
      return fd;
    }
    std::string exchange (int fd, const std::string &request)
    {
      REQUIRE (send (fd, request.data (), request.size (), MSG_NOSIGNAL) == static_cast<ssize_t> (request.size ()));
      for (int i = 0; i < 4; ++i)
	{
	  server.poll ();
	}
      char bytes[65536];
      auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (n < 0 && errno == ECONNRESET)
	{
	  n = 0;
	}
      REQUIRE (n >= 0);
      return std::string (bytes, n);
    }
  };
  const std::string greeting = "{\"type\":\"client_hello\",\"supported_majors\":[1]}\n";
}

TEST_CASE ("Negotiation refuses unsupported versions and previous incarnations", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  int fd = f.connect_client ();
  SECTION ("unsupported version")
  {
    CHECK (f.exchange (fd, "{\"type\":\"client_hello\",\"supported_majors\":[2]}\n")
	   == "{\"type\":\"error\",\"code\":\"version-unsupported\",\"supported_majors\":[1]}\n");
  }
  SECTION ("old incarnation")
  {
    CHECK (f.exchange (fd,
		       "{\"type\":\"client_hello\",\"supported_majors\":[1],\"expected_incarnation\":\"00000000000000000000000000000000\"}\n")
	   == "{\"type\":\"error\",\"code\":\"incarnation-changed\"}\n");
  }
  close (fd);
}
TEST_CASE ("Two attachments bound admission and idle greetings expire", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  int first = f.connect_client (), second = f.connect_client ();
  f.server.poll ();
  f.server.poll ();
  int third = f.connect_client ();
  f.server.poll ();
  char bytes[128];
  auto n = recv (third, bytes, sizeof (bytes), MSG_DONTWAIT);
  REQUIRE (n > 0);
  CHECK (std::string (bytes, n) == "{\"type\":\"error\",\"code\":\"busy\"}\n");
  auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (550);
  while (std::chrono::steady_clock::now () < deadline)
    {
      f.server.poll ();
      std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
  CHECK (recv (first, bytes, sizeof (bytes), MSG_DONTWAIT) == 0);
  CHECK (recv (second, bytes, sizeof (bytes), MSG_DONTWAIT) == 0);
  close (first);
  close (second);
  close (third);
}
TEST_CASE ("Controls cannot exceed their frame or nesting budgets", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  int fd = f.connect_client ();
  SECTION ("4 KiB without delimiter")
  {
    CHECK (f.exchange (fd, std::string (4096, ' ')).empty ());
  }
  SECTION ("depth 17")
  {
    CHECK (f.exchange (fd, "{\"type\":\"client_hello\",\"supported_majors\":[1],\"extra\":" + std::string (16,
		       '[') + "0" + std::string (16, ']') + "}\n").empty ());
  }
  SECTION ("duplicate members")
  {
    CHECK (f.exchange (fd, "{\"type\":\"client_hello\",\"supported_majors\":[1],\"supported_majors\":[2]}\n").empty ());
  }
  close (fd);
}
TEST_CASE ("A scan request cannot fabricate complete absence", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  int fd = f.connect_client ();
  REQUIRE (f.exchange (fd, greeting).find ("server_hello") != std::string::npos);
  CHECK (f.exchange (fd, "{\"type\":\"scan_request\",\"incarnation\":\"00000000000000000000000000000000\"}\n")
	 == "{\"type\":\"error\",\"code\":\"incarnation-changed\"}\n");
  close (fd);
}
TEST_CASE ("Only refused stale sockets are reclaimed and active listeners survive", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  endpoint competing;
  REQUIRE_FALSE (competing.start (f.root, "database", f.db));
  CHECK (std::filesystem::exists (f.server.path ()));
  auto path = f.server.path ();
  f.server.stop ();
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  REQUIRE (fd >= 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy (address.sun_path, path.c_str ());
  REQUIRE (bind (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
  close (fd);
  endpoint replacement;
  REQUIRE (replacement.start (f.root, "database", f.db));
  replacement.stop ();
}
TEST_CASE ("Peer credentials reject another effective UID before any reply", "[.credential][socket]")
{
  // Run with unshare --map-auto --map-root-user to supply two real mapped identities.
  REQUIRE (geteuid () == 0);
  REQUIRE (getegid () == 0);
  socket_fixture f;
  REQUIRE (chmod (f.root, 0755) == 0);
  REQUIRE (chmod ((std::string (f.root) + "/pgbuf-inspector").c_str (), 0755) == 0);
  REQUIRE (chmod (f.server.path ().c_str (), 0666) == 0);
  int sync[2];
  REQUIRE (pipe (sync) == 0);
  pid_t child = fork ();
  REQUIRE (child >= 0);
  if (child == 0)
    {
      close (sync[0]);
      if (setuid (1) != 0)
	{
	  _exit (10);
	}
      // Retain the server's group deliberately: group membership grants no exception.
      if (getegid () != 0)
	{
	  _exit (13);
	}
      int fd = socket (AF_UNIX, SOCK_STREAM, 0);
      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      std::strcpy (address.sun_path, f.server.path ().c_str ());
      if (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
	{
	  _exit (11);
	}
      ::write (sync[1], "x", 1);
      char byte;
      auto n = recv (fd, &byte, 1, 0);
      _exit (n == 0 ? 0 : 12);
    }
  close (sync[1]);
  char byte;
  REQUIRE (read (sync[0], &byte, 1) == 1);
  close (sync[0]);
  f.server.poll ();
  int status;
  REQUIRE (waitpid (child, &status, 0) == child);
  CHECK (WIFEXITED (status));
  CHECK (WEXITSTATUS (status) == 0);
}

TEST_CASE ("A client that never drains a large handshake is disconnected after write stall",
	   "[pgbuf_inspector][socket]")
{
  char root[] = "/tmp/pgbuf-stall-XXXXXX";
  REQUIRE (mkdtemp (root));
  identity db;
  for (int i = 0; i < 500; ++i) db.volumes.push_back ({i, 1700000000, 18446744073709551615ULL, 18446744073709551615ULL});
  endpoint server;
  REQUIRE (server.start (root, "database", db));
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  REQUIRE (fd >= 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy (address.sun_path, server.path ().c_str ());
  REQUIRE (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
  REQUIRE (send (fd, greeting.data (), greeting.size (), 0) == static_cast<ssize_t> (greeting.size ()));
  auto end = std::chrono::steady_clock::now () + std::chrono::milliseconds (320);
  while (std::chrono::steady_clock::now () < end)
    {
      server.poll ();
      std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
  char buffer[65536];
  auto n = recv (fd, buffer, sizeof (buffer), MSG_DONTWAIT);
  REQUIRE (n > 0);
  CHECK (recv (fd, buffer, sizeof (buffer), MSG_DONTWAIT) == 0);
  close (fd);
  server.stop ();
  std::filesystem::remove_all (root);
}

TEST_CASE ("Metadata preparation cannot extend the attachment deadline", "[pgbuf_inspector][socket]")
{
  char root[] = "/tmp/pgbuf-deadline-XXXXXX";
  REQUIRE (mkdtemp (root));
  identity db;
  db.volumes.push_back ({0, 1, 2, 3});
  endpoint server;
  REQUIRE (server.start (root, "database", db, [&] (identity &out)
  {
    std::this_thread::sleep_for (std::chrono::milliseconds (510));
    out = db;
    return true;
  }));
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  REQUIRE (fd >= 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy (address.sun_path, server.path ().c_str ());
  REQUIRE (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
  REQUIRE (send (fd, greeting.data (), greeting.size (), 0) == static_cast<ssize_t> (greeting.size ()));
  server.poll ();
  char byte;
  CHECK (recv (fd, &byte, 1, MSG_DONTWAIT) == 0);
  close (fd);
  server.stop ();
  std::filesystem::remove_all (root);
}

TEST_CASE ("Identity proof is complete or refused by encoded bytes", "[pgbuf_inspector][socket]")
{
  char root[] = "/tmp/pgbuf-identity-XXXXXX";
  REQUIRE (mkdtemp (root));
  identity db;
  bool oversized = false;
  SECTION ("730 compact entries fit")
  {
    for (int i = 0; i < 730; ++i) db.volumes.push_back ({i, 1, 2, 3});
  }
  SECTION ("large proof never returns a subset")
  {
    for (int i = 0; i < 1500; ++i) db.volumes.push_back ({i, 1700000000, UINT64_MAX, UINT64_MAX});
    oversized = true;
  }
  endpoint server;
  REQUIRE (server.start (root, "database", db));
  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  REQUIRE (fd >= 0);
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  std::strcpy (address.sun_path, server.path ().c_str ());
  REQUIRE (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
  REQUIRE (send (fd, greeting.data (), greeting.size (), 0) == static_cast<ssize_t> (greeting.size ()));
  std::string response;
  for (int i = 0; i < 100 && response.find ('\n') == std::string::npos; ++i)
    {
      server.poll ();
      char bytes[4096];
      auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (n > 0)
	{
	  response.append (bytes, n);
	}
    }
  REQUIRE_FALSE (response.empty ());
  REQUIRE (response.back () == '\n');
  CHECK (response.size () <= 65536);
  if (oversized)
    {
      CHECK (response == "{\"type\":\"error\",\"code\":\"identity-oversized\"}\n");
    }
  else
    {
      CHECK (response.find ("\"volid\":729,") != std::string::npos);
      CHECK (response.find ("server_hello") != std::string::npos);
    }
  close (fd);
  server.stop ();
  std::filesystem::remove_all (root);
}

TEST_CASE ("Root receives no exception to exact effective UID authentication", "[.credential]")
{
  REQUIRE (geteuid () == 0);
  REQUIRE (seteuid (1) == 0);
  socket_fixture f;
  REQUIRE (chmod (f.root, 0755) == 0);
  REQUIRE (chmod ((std::string (f.root) + "/pgbuf-inspector").c_str (), 0755) == 0);
  REQUIRE (chmod (f.server.path ().c_str (), 0666) == 0);
  REQUIRE (seteuid (0) == 0);
  int fd = f.connect_client ();
  REQUIRE (seteuid (1) == 0);
  f.server.poll ();
  char byte;
  CHECK (recv (fd, &byte, 1, MSG_DONTWAIT) == 0);
  close (fd);
  REQUIRE (seteuid (0) == 0);
}
TEST_CASE ("An existing socket owned by another account is preserved", "[.credential]")
{
  REQUIRE (geteuid () == 0);
  socket_fixture f;
  auto path = f.server.path ();
  REQUIRE (chown (path.c_str (), 1, -1) == 0);
  endpoint other;
  CHECK_FALSE (other.start (f.root, "database", f.db));
  f.server.stop ();
  struct stat st;
  REQUIRE (lstat (path.c_str (), &st) == 0);
  CHECK (st.st_uid == 1);
}

namespace
{
  std::string scan_request (const std::string &hello)
  {
    const auto offset = hello.find ("\"incarnation\":\"");
    REQUIRE (offset != std::string::npos);
    return "{\"type\":\"scan_request\",\"incarnation\":\"" + hello.substr (offset + 15, 32) + "\"}\n";
  }
}

TEST_CASE ("The scan floor applies across both clients and preserves increasing sequences", "[pgbuf_inspector][socket]")
{
  auto now = endpoint::clock::now ();
  socket_fixture f ({}, [&now] { return now; });
  int first = f.connect_client (), second = f.connect_client ();
  auto hello = f.exchange (first, greeting);
  f.exchange (second, greeting);
  auto request = scan_request (hello);
  auto capture = f.exchange (first, request);
  CHECK (capture.find ("\"scan_seq\":\"1\"") != std::string::npos);
  now += std::chrono::microseconds (99999);
  CHECK (f.exchange (second, request) == "{\"type\":\"error\",\"code\":\"rate-limited\",\"retry_after_ms\":1}\n");
  now += std::chrono::microseconds (1);
  capture = f.exchange (second, request);
  CHECK (capture.find ("\"scan_seq\":\"2\"") != std::string::npos);
  now += std::chrono::microseconds (100001);
  capture = f.exchange (first, request);
  CHECK (capture.find ("\"scan_seq\":\"3\"") != std::string::npos);
  close (first);
  close (second);
}

TEST_CASE ("Socket captures publish only after valid footer and preserve duplicate ambiguity",
	   "[pgbuf_inspector][socket]")
{
  scan_source source;
  source.slots = 2;
  source.sample = [] (std::size_t, page_sample &page)
  {
    page.volid = 0;
    page.pageid = 42;
    return sample_status::RESIDENT;
  };
  socket_fixture f (source);
  int fd = f.connect_client ();
  auto hello = f.exchange (fd, greeting);
  auto request = scan_request (hello);
  auto capture = f.exchange (fd, request);
  exchange_validator validator;
  REQUIRE (validator.feed (greeting + hello + request));
  auto footer = capture.find ("{\"type\":\"scan_footer\"");
  REQUIRE (footer != std::string::npos);
  for (char byte : capture.substr (0, footer))
    {
      REQUIRE (validator.feed (std::string_view (&byte, 1)));
    }
  CHECK_FALSE (validator.published ());
  REQUIRE (validator.feed (capture.substr (footer)));
  CHECK (validator.published ());
  CHECK (validator.record_count () == 2);
  CHECK (validator.lookup (0, 42) == observation::AMBIGUOUS);
  CHECK (validator.lookup (0, 43) == observation::NOT_RESIDENT);
  close (fd);
}

TEST_CASE ("A scan cancels sampling on disconnect and shutdown", "[pgbuf_inspector][socket]")
{
  std::size_t visits = 0;
  scan_source source;
  source.slots = 100000;
  source.sample = [&visits] (std::size_t slot, page_sample &page)
  {
    ++visits;
    page.volid = 0;
    page.pageid = slot;
    return sample_status::RESIDENT;
  };
  socket_fixture f (source);
  int fd = f.connect_client ();
  auto request = scan_request (f.exchange (fd, greeting));
  REQUIRE (send (fd, request.data (), request.size (), 0) == static_cast<ssize_t> (request.size ()));
  f.server.poll ();
  REQUIRE (visits > 0);
  REQUIRE (visits < 65536);
  auto before = visits;
  SECTION ("disconnect")
  {
    close (fd);
  }
  SECTION ("shutdown")
  {
    f.server.stop ();
    close (fd);
  }
  for (int i = 0; i < 10; ++i)
    {
      f.server.poll ();
    }
  CHECK (visits == before);
}

TEST_CASE ("Stalled scan output is cancelled without a successful footer", "[pgbuf_inspector][socket]")
{
  scan_source source;
  source.slots = 100000;
  source.sample = [] (std::size_t slot, page_sample &page)
  {
    page.volid = 0;
    page.pageid = slot;
    return sample_status::RESIDENT;
  };
  socket_fixture f (source);
  int fd = f.connect_client ();
  auto request = scan_request (f.exchange (fd, greeting));
  REQUIRE (send (fd, request.data (), request.size (), 0) == static_cast<ssize_t> (request.size ()));
  auto start = endpoint::clock::now ();
  while (endpoint::clock::now () - start < std::chrono::milliseconds (320))
    {
      f.server.poll ();
      std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
  char bytes[65536];
  auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
  REQUIRE (n > 0);
  CHECK (std::string (bytes, n).find ("scan_footer") == std::string::npos);
  CHECK (recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT) == 0);
  close (fd);
}

TEST_CASE ("The whole exchange deadline is rechecked before writing", "[pgbuf_inspector][socket]")
{
  for (int elapsed :
       {
	       1999999, 2000000, 2000001
       })
    {
      auto now = endpoint::clock::now ();
      scan_source source;
      source.slots = 2;
      source.sample = [&now, elapsed] (std::size_t, page_sample &page)
      {
	now += std::chrono::microseconds (elapsed);
	page.volid = 0;
	page.pageid = 42;
	return sample_status::RESIDENT;
      };
      socket_fixture f (source, [&now] { return now; });
      int fd = f.connect_client ();
      auto request = scan_request (f.exchange (fd, greeting));
      auto result = f.exchange (fd, request);
      if (elapsed < 2000000)
	{
	  CHECK (result.find ("\"truncated\":true") != std::string::npos);
	}
      else
	{
	  CHECK (result.empty ());
	}
      close (fd);
    }
}

TEST_CASE ("Scan requests validate additive fields within frame and nesting limits", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  int fd = f.connect_client ();
  auto request = scan_request (f.exchange (fd, greeting));
  bool valid = true;
  SECTION ("exactly 4096 bytes")
  {
    request.insert (request.size () - 2, ",\"extra\":\"\"");
    request.insert (request.size () - 3, 4096 - request.size (), 'x');
    REQUIRE (request.size () == 4096);
  }
  SECTION ("4097 bytes")
  {
    request.insert (request.size () - 2, ",\"extra\":\"\"");
    request.insert (request.size () - 3, 4097 - request.size (), 'x');
    valid = false;
  }
  SECTION ("depth 16")
  {
    request.insert (request.size () - 2, ",\"extra\":" + std::string (15, '[') + "0" + std::string (15, ']'));
  }
  SECTION ("depth 17")
  {
    request.insert (request.size () - 2, ",\"extra\":" + std::string (16, '[') + "0" + std::string (16, ']'));
    valid = false;
  }
  auto result = f.exchange (fd, request);
  if (valid)
    {
      CHECK (result.find ("scan_footer") != std::string::npos);
    }
  else
    {
      CHECK (result.empty ());
    }
  close (fd);
}

TEST_CASE ("Premature requests are rejected while the completed traversal still drains", "[pgbuf_inspector][socket]")
{
  auto now = endpoint::clock::now ();
  std::size_t visits = 0;
  scan_source source;
  source.slots = 100;
  source.sample = [&visits] (std::size_t slot, page_sample &page)
  {
    ++visits;
    page.volid = 0;
    page.pageid = slot;
    return sample_status::RESIDENT;
  };
  socket_fixture f (source, [&now] { return now; });
  int fd = f.connect_client ();
  auto request = scan_request (f.exchange (fd, greeting));
  REQUIRE (send (fd, request.data (), request.size (), 0) == static_cast<ssize_t> (request.size ()));
  // This fits the producer queue but exceeds the socket send buffer. Do not drain it yet.
  for (int i = 0; i < 100; ++i)
    {
      f.server.poll ();
    }
  REQUIRE (visits == 100);
  now += std::chrono::milliseconds (101);
  REQUIRE (send (fd, request.data (), request.size (), 0) == static_cast<ssize_t> (request.size ()));
  bool closed = false;
  char bytes[65536];
  for (int i = 0; i < 100; ++i)
    {
      f.server.poll ();
      auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (n == 0 || (n < 0 && errno == ECONNRESET))
	{
	  closed = true;
	  break;
	}
    }
  CHECK (closed);
  CHECK (visits == 100);
  close (fd);
}

TEST_CASE ("Fragmented controls and coalesced premature requests use production framing",
	   "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  socket_client fd (f.connect_client ());
  SECTION ("one byte at a time, including the final delimiter")
  {
    for (char byte : greeting.substr (0, greeting.size () - 1))
      {
	REQUIRE (send (fd, &byte, 1, MSG_NOSIGNAL) == 1);
	f.server.poll ();
	char response;
	REQUIRE (recv (fd, &response, 1, MSG_DONTWAIT) == -1);
	REQUIRE ((errno == EAGAIN || errno == EWOULDBLOCK));
      }
    auto request = scan_request (f.exchange (fd, "\n"));
    for (char byte : request.substr (0, request.size () - 1))
      {
	REQUIRE (send (fd, &byte, 1, MSG_NOSIGNAL) == 1);
	f.server.poll ();
      }
    CHECK (f.exchange (fd, "\n").find ("scan_footer") != std::string::npos);
  }
  SECTION ("two requests in one read never start a scan")
  {
    auto request = scan_request (f.exchange (fd, greeting));
    CHECK (f.exchange (fd, request + request).empty ());
  }
  SECTION ("EOF in a fragmented greeting releases admission")
  {
    REQUIRE (send (fd, greeting.data (), 10, MSG_NOSIGNAL) == 10);
    REQUIRE (shutdown (fd, SHUT_WR) == 0);
    f.server.poll ();
    f.server.poll ();
    char response;
    CHECK (recv (fd, &response, 1, MSG_DONTWAIT) == 0);
  }

}

TEST_CASE ("Hello controls enforce each framing boundary through real sockets", "[pgbuf_inspector][socket]")
{
  for (int delta :
       {
	       -1, 0, 1
	       })
    {
      for (bool depth :
	   {
		   false, true
	   })
	{
	  CAPTURE (delta, depth);
	  socket_fixture f;
	  socket_client fd (f.connect_client ());
	  std::string request = greeting;
	  if (depth)
	    {
	      request.insert (request.size () - 2, ",\"extra\":" + std::string (15 + delta, '[')
			      + "0" + std::string (15 + delta, ']'));
	    }
	  else
	    {
	      request.insert (request.size () - 2, ",\"extra\":\"\"");
	      request.insert (request.size () - 3, 4096 + delta - request.size (), 'x');
	    }
	  auto reply = f.exchange (fd, request);
	  CHECK ((reply.find ("server_hello") != std::string::npos) == (delta <= 0));

	}
    }
}

TEST_CASE ("Handshake deadline is inclusive and fragmented input cannot renew it", "[pgbuf_inspector][socket]")
{
  for (int elapsed :
       {
	       499999, 500000, 500001
       })
    {
      auto now = endpoint::clock::now ();
      socket_fixture f ({}, [&now] { return now; });
      socket_client fd (f.connect_client ());
      f.server.poll ();
      REQUIRE (send (fd, greeting.data (), greeting.size () - 1, MSG_NOSIGNAL)
	       == static_cast<ssize_t> (greeting.size () - 1));
      now += std::chrono::microseconds (elapsed);
      f.server.poll ();
      char byte;
      auto n = recv (fd, &byte, 1, MSG_DONTWAIT);
      if (elapsed < 500000)
	{
	  CHECK (n == -1);
	  CHECK ((errno == EAGAIN || errno == EWOULDBLOCK));
	  CHECK (f.exchange (fd, "\n").find ("server_hello") != std::string::npos);
	}
      else
	{
	  CHECK ((n == 0 || (n == -1 && errno == ECONNRESET)));
	}

    }
}

TEST_CASE ("Backpressure bounds queued bytes and reserves a truthful footer", "[pgbuf_inspector][socket]")
{
  auto now = endpoint::clock::now ();
  std::size_t visits = 0;
  scan_source source;
  source.slots = 65537;
  source.sample = [&visits] (std::size_t, page_sample &page)
  {
    ++visits;
    page.volid = 0;
    page.pageid = 42;
    return sample_status::RESIDENT;
  };
  socket_fixture f (source, [&now] { return now; });
  socket_client fd (f.connect_client ());
  auto hello = f.exchange (fd, greeting);
  auto request = scan_request (hello);
  REQUIRE (send (fd, request.data (), request.size (), MSG_NOSIGNAL) == static_cast<ssize_t> (request.size ()));
  for (int i = 0; i < 100; ++i)
    {
      f.server.poll ();
    }
  auto stalled_visits = visits;
  REQUIRE (stalled_visits > 0);
  REQUIRE (stalled_visits < 65536);
  for (int i = 0; i < 100; ++i)
    {
      f.server.poll ();
    }
  REQUIRE (visits == stalled_visits);
  int kernel_bytes = 0;
  REQUIRE (ioctl (fd, FIONREAD, &kernel_bytes) == 0);
  REQUIRE (kernel_bytes > 0);
  char bytes[65536];
  auto n = recv (fd, bytes, sizeof (bytes), MSG_PEEK | MSG_DONTWAIT);
  REQUIRE (n == kernel_bytes);
  std::string prefix (bytes, n);
  auto header_size = prefix.find ('\n') + 1;
  auto page_size = prefix.find ('\n', header_size) + 1 - header_size;
  REQUIRE (header_size > 0);
  REQUIRE (page_size > 0);
  // Identical scalar records have identical wire lengths. Account independently
  // for frames generated, bytes still in the kernel, and the producer queue.
  auto queued = header_size + visits * page_size - kernel_bytes;
  CHECK (queued <= 65536);
  CHECK (queued > 65536 - 4096);
  now += std::chrono::milliseconds (100);
  std::string capture;
  for (int i = 0; i < 100; ++i)
    {
      auto count = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (count > 0)
	{
	  capture.append (bytes, count);
	}
      f.server.poll ();
    }
  exchange_validator validator;
  REQUIRE (validator.feed (greeting + hello + request + capture));
  REQUIRE (validator.published ());
  CHECK (validator.record_count () == visits);
  CHECK (visits == stalled_visits);
  CHECK (validator.lookup (0, 43) == observation::UNKNOWN);
  CHECK (capture.find ("\"truncated\":true") != std::string::npos);

}

TEST_CASE ("Write stall deadline closes at 250 ms without manufacturing a footer", "[pgbuf_inspector][socket]")
{
  for (int elapsed :
       {
	       249999, 250000, 250001
       })
    {
      auto now = endpoint::clock::now ();
      scan_source source;
      source.slots = 65537;
      source.sample = [] (std::size_t, page_sample &page)
      {
	page.volid = 0;
	page.pageid = 42;
	return sample_status::RESIDENT;
      };
      socket_fixture f (source, [&now] { return now; });
      socket_client fd (f.connect_client ());
      auto hello = f.exchange (fd, greeting);
      auto request = scan_request (hello);
      REQUIRE (send (fd, request.data (), request.size (), MSG_NOSIGNAL) == static_cast<ssize_t> (request.size ()));
      for (int i = 0; i < 100; ++i)
	{
	  f.server.poll ();
	}
      now += std::chrono::microseconds (elapsed);
      f.server.poll ();
      char bytes[65536];
      auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      REQUIRE (n > 0);
      exchange_validator validator;
      REQUIRE (validator.feed (greeting + hello + request + std::string (bytes, n)));
      CHECK_FALSE (validator.published ());
      CHECK_FALSE (validator.finish ());
      n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (elapsed < 250000)
	{
	  CHECK (n == -1);
	  CHECK ((errno == EAGAIN || errno == EWOULDBLOCK));
	}
      else
	{
	  CHECK (n == 0);
	}

    }
}

TEST_CASE ("An indeterminate backlog-full probe preserves the active socket", "[pgbuf_inspector][socket]")
{
  socket_fixture f;
  std::vector<socket_client> clients;
  bool full = false;
  for (int i = 0; i < 16; ++i)
    {
      int fd = socket (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
      REQUIRE (fd >= 0);
      clients.emplace_back (fd);
      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      std::strcpy (address.sun_path, f.server.path ().c_str ());
      if (connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
	{
	  REQUIRE (errno == EAGAIN);
	  full = true;
	  break;
	}
    }
  REQUIRE (full);
  struct stat before, after;
  REQUIRE (lstat (f.server.path ().c_str (), &before) == 0);
  endpoint other;
  REQUIRE_FALSE (other.start (f.root, "database", f.db));
  REQUIRE (lstat (f.server.path ().c_str (), &after) == 0);
  CHECK (before.st_ino == after.st_ino);
  CHECK (before.st_dev == after.st_dev);
}

TEST_CASE ("Real slow draining reaches the whole exchange deadline despite write progress",
	   "[pgbuf_inspector][socket]")
{
  std::size_t visits = 0;
  scan_source source;
  source.slots = 65537;
  source.sample = [&visits] (std::size_t, page_sample &page)
  {
    ++visits;
    page.volid = 0;
    page.pageid = 42;
    return sample_status::RESIDENT;
  };
  socket_fixture f (source);
  socket_client fd (f.connect_client ());
  auto hello = f.exchange (fd, greeting);
  auto request = scan_request (hello);
  REQUIRE (send (fd, request.data (), request.size (), MSG_NOSIGNAL) == static_cast<ssize_t> (request.size ()));
  auto start = endpoint::clock::now ();
  f.server.poll ();
  // Establish saturation before the 100 ms traversal cap; a slow test host
  // that cannot establish this condition must fail rather than claim timing proof.
  for (int i = 0; i < 100; ++i)
    {
      f.server.poll ();
    }
  REQUIRE (endpoint::clock::now () - start < std::chrono::milliseconds (100));
  REQUIRE (visits > 100);
  auto next_read = start + std::chrono::milliseconds (140);
  auto last_progress = start;
  int progress_events = 0;
  bool closed = false;
  std::string capture;
  while (endpoint::clock::now () - start < std::chrono::milliseconds (2200))
    {
      f.server.poll ();
      pollfd state {fd, POLLIN, 0};
      REQUIRE (::poll (&state, 1, 0) >= 0);
      if (state.revents & POLLHUP)
	{
	  closed = true;
	  break;
	}
      if (endpoint::clock::now () >= next_read)
	{
	  char bytes[4096];
	  auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
	  REQUIRE (n == sizeof (bytes));
	  capture.append (bytes, n);
	  int before = 0, after = 0;
	  REQUIRE (ioctl (fd, FIONREAD, &before) == 0);
	  f.server.poll ();
	  REQUIRE (ioctl (fd, FIONREAD, &after) == 0);
	  if (after > before)
	    {
	      REQUIRE (endpoint::clock::now () - last_progress < std::chrono::milliseconds (250));
	      last_progress = endpoint::clock::now ();
	      ++progress_events;
	    }
	  next_read += std::chrono::milliseconds (140);
	}
      std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
  auto elapsed = endpoint::clock::now () - start;
  INFO ("elapsed us: " << std::chrono::duration_cast<std::chrono::microseconds> (elapsed).count ());
  CHECK (closed);
  CHECK (elapsed >= std::chrono::seconds (2));
  CHECK (elapsed < std::chrono::milliseconds (2200));
  CHECK (progress_events >= 10);
  CHECK (endpoint::clock::now () - last_progress < std::chrono::milliseconds (250));
  char bytes[65536];
  for (;;)
    {
      auto n = recv (fd, bytes, sizeof (bytes), MSG_DONTWAIT);
      if (n <= 0)
	{
	  break;
	}
      capture.append (bytes, n);
    }
  exchange_validator validator;
  REQUIRE (validator.feed (greeting + hello + request + capture));
  CHECK_FALSE (validator.published ());
  CHECK_FALSE (validator.finish ());

}

TEST_CASE ("Wrong-owner private directories are preserved", "[.credential]")
{
  REQUIRE (geteuid () == 0);
  socket_fixture f;
  f.server.stop ();
  auto directory = std::string (f.root) + "/pgbuf-inspector";
  REQUIRE (chown (directory.c_str (), 1, -1) == 0);
  endpoint other;
  REQUIRE_FALSE (other.start (f.root, "database", f.db));
  struct stat st;
  REQUIRE (lstat (directory.c_str (), &st) == 0);
  CHECK (st.st_uid == 1);
  CHECK ((st.st_mode & 0777) == 0700);
}

TEST_CASE ("Repeated shutdown closes descriptors and never retries activation", "[pgbuf_inspector][socket]")
{
  auto descriptors = [] ()
  {
    return std::distance (std::filesystem::directory_iterator ("/proc/self/fd"),
			  std::filesystem::directory_iterator ());
  };
  auto before = descriptors ();
  for (int i = 0; i < 20; ++i)
    {
      socket_fixture f;
      socket_client first (f.connect_client ()), second (f.connect_client ());
      f.exchange (first, greeting);
      f.exchange (second, greeting);
      auto start = endpoint::clock::now ();
      f.server.stop ();
      CHECK (endpoint::clock::now () - start < std::chrono::milliseconds (100));
      char byte;
      CHECK (recv (first, &byte, 1, MSG_DONTWAIT) == 0);
      CHECK (recv (second, &byte, 1, MSG_DONTWAIT) == 0);


      f.server.stop ();
      CHECK_FALSE (f.server.start (f.root, "database", f.db));
    }
  CHECK (descriptors () == before);
}

TEST_CASE ("Admission overload leaves both admitted clients usable", "[pgbuf_inspector][socket]")
{
  auto now = endpoint::clock::now ();
  socket_fixture f ({}, [&now] { return now; });
  socket_client first (f.connect_client ()), second (f.connect_client ());
  auto request = scan_request (f.exchange (first, greeting));
  REQUIRE (f.exchange (second, greeting).find ("server_hello") != std::string::npos);
  for (int i = 0; i < 128; ++i)
    {
      socket_client excess (f.connect_client ());
      f.server.poll ();
      char bytes[128];
      auto n = recv (excess, bytes, sizeof (bytes), MSG_DONTWAIT);
      REQUIRE (n > 0);
      REQUIRE (std::string (bytes, n) == "{\"type\":\"error\",\"code\":\"busy\"}\n");

    }
  CHECK (f.exchange (first, request).find ("scan_footer") != std::string::npos);
  now += std::chrono::milliseconds (100);
  CHECK (f.exchange (second, request).find ("scan_footer") != std::string::npos);


}

TEST_CASE ("Malformed scan controls close without protocol data or manufactured success",
	   "[pgbuf_inspector][socket]")
{
  for (const std::string &malformed : std::vector<std::string>
  {
    "{bad json}\n", "[]\n", "{\"type\":\"scan_request\"}\n",
    "{\"type\":\"scan_request\",\"incarnation\":3}\n",
    "{\"type\":\"scan_request\",\"type\":\"client_hello\"}\n",
    std::string ("{\"extra\":\"") + char (0xff) + "\"}\n"
    })
  {
    CAPTURE (malformed);
    socket_fixture f;
    socket_client fd (f.connect_client ());
    REQUIRE (f.exchange (fd, greeting).find ("server_hello") != std::string::npos);
    CHECK (f.exchange (fd, malformed).empty ());

  }
}
