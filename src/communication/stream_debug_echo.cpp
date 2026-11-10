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
 * stream_debug_echo.cpp - Two stream kinds a debug build registers, so a driver can drive the transport end to end
 *			   before a real consumer lands.
 *
 * Kind 5 is an upload whose END finishes no statement: it counts the bytes and returns the count. Kind 6 is a
 * download that produces a byte sequence. A driver opens either directly (INIT, READ_OPEN). Both configs are
 * big-endian fields, and start with the version, 1:
 *   upload (5):   version int32 | fail_after int64 (-1: never) | delay_ms int32
 *		   a chunk that brings the total past fail_after is refused; each chunk first sleeps delay_ms
 *   download (6): version int32 | length int64 | seed int32
 *		   byte i is (seed + i) & 0xff
 * A negative version is refused by authorize, a version above 1 by the factory with ER_STREAM_NOT_SUPPORTED.
 */

#include "stream_debug_echo.hpp"

#include "stream_session.hpp"
#include "stream_client.h"

#include "error_code.h"
#include "error_manager.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined (NDEBUG)

namespace
{
  const int ECHO_VERSION = 1;
  const int ECHO_UPLOAD_CONFIG_SIZE = 4 + 8 + 4;
  const int ECHO_DOWNLOAD_CONFIG_SIZE = 4 + 8 + 4;

  std::int64_t
  echo_get_be (const char *p, int size)
  {
    std::uint64_t v = 0;

    for (int i = 0; i < size; i++)
      {
	v = (v << 8) | (unsigned char) p[i];
      }
    return size == 4 ? (std::int64_t) (std::int32_t) v : (std::int64_t) v;
  }

  int
  echo_refuse (int error_code, const char *what)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, what);
    return error_code;
  }

#if !defined (CS_MODE)
  /* the factory's check: the version the client half let through, and the fields this version has */
  int
  echo_check_config (const char *config, int config_len, int size)
  {
    char msg[64];

    if (config == NULL || config_len < size)
      {
	return echo_refuse (ER_STREAM_SESSION_ERROR, "debug echo: config too short");
      }
    if (echo_get_be (config, 4) > ECHO_VERSION)
      {
	snprintf (msg, sizeof (msg), "debug echo: config version above %d", ECHO_VERSION);
	return echo_refuse (ER_STREAM_NOT_SUPPORTED, msg);
      }
    return NO_ERROR;
  }

  class echo_session : public stream_session
  {
    public:
      echo_session (std::int64_t fail_after, int delay_ms)
	: m_total (0)
	, m_fail_after (fail_after)
	, m_delay_ms (delay_ms)
      {
      }

      int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override
      {
	char msg[64];

	(void) data;
	if (m_delay_ms > 0)
	  {
	    std::this_thread::sleep_for (std::chrono::milliseconds (m_delay_ms));
	  }
	m_total += data_len;
	if (m_fail_after >= 0 && m_total > m_fail_after)
	  {
	    snprintf (msg, sizeof (msg), "debug echo: refused past %lld bytes", (long long) m_fail_after);
	    return echo_refuse (ER_STREAM_SESSION_ERROR, msg);
	  }
	return NO_ERROR;
      }

      int finish (THREAD_ENTRY *thread_p, stream_result *result) override
      {
	result->count = m_total;
	return NO_ERROR;
      }

      void abort (THREAD_ENTRY *thread_p) override
      {
      }

    private:
      std::int64_t m_total;
      std::int64_t m_fail_after;
      int m_delay_ms;
  };

  class echo_source : public stream_source
  {
    public:
      echo_source (std::int64_t length, int seed)
	: m_left (length)
	, m_next ((unsigned char) seed)
      {
      }

      int read (THREAD_ENTRY *thread_p, char *buf, int max, int *nread, bool *eof) override
      {
	int n = (int) std::min<std::int64_t> (max, m_left);

	for (int i = 0; i < n; i++)
	  {
	    buf[i] = (char) m_next++;
	  }
	m_left -= n;
	*nread = n;
	*eof = (m_left == 0);
	return NO_ERROR;
      }

      void close (THREAD_ENTRY *thread_p) override
      {
      }

    private:
      std::int64_t m_left;
      unsigned char m_next;
  };

  stream_session *
  echo_session_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
  {
    *error_code = echo_check_config (config, config_len, ECHO_UPLOAD_CONFIG_SIZE);
    if (*error_code != NO_ERROR)
      {
	return NULL;
      }
    return new echo_session (echo_get_be (config + 4, 8), (int) echo_get_be (config + 12, 4));
  }

  stream_source *
  echo_source_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
  {
    std::int64_t length;

    *error_code = echo_check_config (config, config_len, ECHO_DOWNLOAD_CONFIG_SIZE);
    if (*error_code != NO_ERROR)
      {
	return NULL;
      }
    length = echo_get_be (config + 4, 8);
    if (length < 0)
      {
	*error_code = echo_refuse (ER_STREAM_SESSION_ERROR, "debug echo: negative length");
	return NULL;
      }
    return new echo_source (length, (int) echo_get_be (config + 12, 4));
  }

#endif /* !CS_MODE */

#if !defined (SERVER_MODE)
  /* a negative version stands for a privilege the user lacks */
  int
  echo_authorize (const char *config, int config_len)
  {
    if (config != NULL && config_len >= 4 && echo_get_be (config, 4) < 0)
      {
	return echo_refuse (ER_STREAM_SESSION_ERROR, "debug echo: refused by authorize");
      }
    return NO_ERROR;
  }

#endif /* !SERVER_MODE */
}

/* the server half needs the registry (stream_session.cpp), the client half stream_client.c */
#if !defined (CS_MODE)
int
stream_debug_echo_register_server (void)
{
  stream_kind_desc upload = { STREAM_UPLOAD, echo_session_create, NULL, false };
  stream_kind_desc download = { STREAM_DOWNLOAD, NULL, echo_source_create, false };
  int error_code = stream_kind_register (STREAM_KIND_DEBUG_ECHO_UPLOAD, upload);

  return error_code != NO_ERROR ? error_code : stream_kind_register (STREAM_KIND_DEBUG_ECHO_DOWNLOAD, download);
}

#endif /* !CS_MODE */

#if !defined (SERVER_MODE)
int
stream_debug_echo_register_client (void)
{
  stream_kind_client_desc desc = { true, echo_authorize };
  int error_code = stream_kind_client_register (STREAM_KIND_DEBUG_ECHO_UPLOAD, desc);

  return error_code != NO_ERROR ? error_code : stream_kind_client_register (STREAM_KIND_DEBUG_ECHO_DOWNLOAD, desc);
}
#endif /* !SERVER_MODE */

/* each half registers at load time in the binary it belongs to */
namespace
{
#if defined (SERVER_MODE)
  struct echo_registrar
  {
    echo_registrar ()
    {
      if (stream_debug_echo_register_server () != NO_ERROR)
	{
	  assert (false);
	}
    }
  } echo_registrar_instance;
#endif /* SERVER_MODE */

#if defined (CS_MODE)
  struct echo_client_registrar
  {
    echo_client_registrar ()
    {
      if (stream_debug_echo_register_client () != NO_ERROR)
	{
	  assert (false);
	}
    }
  } echo_client_registrar_instance;
#endif /* CS_MODE */
}

#endif /* !NDEBUG */
