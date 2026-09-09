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

#ifndef _PGBUF_INSPECTOR_SOCKET_HPP_
#define _PGBUF_INSPECTOR_SOCKET_HPP_

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace cubpgbuf
{
  namespace inspector
  {
    struct volume_identity
    {
      int volid;
      std::uint64_t creation, device, inode;
    };
    struct identity
    {
      std::uint64_t database_creation = 0;
      int shared_lru_count = 0, private_lru_count = 0;
      std::vector<volume_identity> volumes;
      bool oversized = false;
    };

// Owned by one daemon. All socket operations are nonblocking; poll performs bounded work.
// start is attempted once per incarnation, including unsuccessful activation.
    class endpoint
    {
      public:
	endpoint () = default;
	~endpoint ();
	endpoint (const endpoint &) = delete;
	endpoint &operator= (const endpoint &) = delete;
	bool start (const std::string &root, const std::string &key, const identity &db,
		    std::function<bool (identity &)> refresh = {});
	void poll ();
	void stop ();
	const std::string &path () const
	{
	  return m_path;
	}
      private:
	using clock = std::chrono::steady_clock;
	struct client
	{
	  int fd = -1;
	  bool ready = false, closing = false;
	  std::string input, output;
	  std::size_t written = 0;
	  clock::time_point attached, progress;
	};
	bool m_attempted = false;
	int m_listener = -1, m_directory = -1;
	struct stat m_socket {};
	bool m_created = false;
	std::string m_path, m_key, m_incarnation, m_hello;
	std::array<client, 2> m_clients;
	std::function<bool (identity &)> m_refresh;
	bool make_hello (const identity &db);
	void close_client (client &c);
	void receive (client &c);
	void write (client &c);
    };
  }
}
#endif
