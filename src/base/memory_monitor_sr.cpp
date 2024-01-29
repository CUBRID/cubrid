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
 * memory_monitor_sr.cpp - Implementation of memory monitor module
 */

#include <cstring>
#include <algorithm>

#include "openssl/md5.h"
#include "memory_monitor_sr.hpp"

namespace cubmem
{
  memory_monitor::memory_monitor (const char *server_name)
    : m_server_name {server_name} {}

  std::string memory_monitor::make_tag_name (const char *file, const int line)
  {
    std::string filecopy (file);
    std::string ret;

    // Find the last occurrence of "src" in the path
    size_t pos = filecopy.rfind ("src");

    if (pos != std::string::npos)
      {
	filecopy = filecopy.substr (pos);
      }

    ret = filecopy + ':' + std::to_string (line);
    return ret;
  }

  int memory_monitor::generate_checksum (int tag_id, uint64_t size)
  {
    char input[32]; // INT_MAX digits 10 +  ULLONG_MAX digits 20
    unsigned char digest[MD5_DIGEST_LENGTH];
    int ret;

    memset (input, 0, sizeof (input));
    memset (digest, 0, sizeof (digest));
    sprintf (input, "%d%lu", tag_id, size);
    (void) MD5 (reinterpret_cast<const unsigned char *> (input), strlen (input), digest);
    memcpy (&ret, digest, sizeof (int));
    return ret;
  }
