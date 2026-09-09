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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <cstring>
#include <thread>
#include <sys/wait.h>
#include <cerrno>

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
  REQUIRE (server.start (root, "database", db));
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
  endpoint server;
  REQUIRE_FALSE (server.start (root, "database", db));
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
  struct socket_fixture
  {
    char root[64] = "/tmp/pgbuf-protocol-XXXXXX";
    identity db;
    endpoint server;
    socket_fixture ()
    {
      REQUIRE (mkdtemp (root));
      db.database_creation = 1700000000;
      db.volumes.push_back ({0, 1700000001, 42, 123});
      REQUIRE (server.start (root, "database", db));
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
  CHECK (f.exchange (fd, "{\"type\":\"scan_request\",\"incarnation\":\"00000000000000000000000000000000\"}\n").empty ());
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
