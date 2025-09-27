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
 * connection_stats.hpp
 */

#ifndef _CONNECTION_STATS_HPP_
#define _CONNECTION_STATS_HPP_

#include <string>
#include <cstdint>

namespace cubconn
{
  enum stats : std::uint8_t
  {
    /* --------------------------------------------------------------------------- */
    /* blocked section								   */
    /* --------------------------------------------------------------------------- */
    BLOCKED_RMUTEX, /* us, handle reception, rmutex */
    BLOCKED_WAIT_WORKER, /* us, handle connection error */

    /* --------------------------------------------------------------------------- */
    /* network									   */
    /* --------------------------------------------------------------------------- */
    NET_RECV, /* bytes */
    NET_SEND, /* bytes */

    NET_PACKET_COUNT, /* count */

    NET_CLIENTS, /* count */

    /* --------------------------------------------------------------------------- */
    /* memory									   */
    /* --------------------------------------------------------------------------- */
    MEM_POOL_COMMIT, /* count, receiver */
    MEM_POOL_RELEASE, /* count, receiver */

    MEM_ALLOCATE, /* count, receiver */
    MEM_DELETE, /* count, receiver */

    /* --------------------------------------------------------------------------- */
    /* message queue								   */
    /* --------------------------------------------------------------------------- */
    MQ_REQUESTED, /* count */
    MQ_NEW_CLIENT, /* count */
    MQ_SHUTDOWN_CLIENT, /* count */
    MQ_SEND_PACKET, /* count */
    MQ_RELEASE_PACKET, /* count */

    /* --------------------------------------------------------------------------- */
    /* stats count		 						   */
    /* --------------------------------------------------------------------------- */
    STATS_COUNT
  };

  inline std::string stats_name[STATS_COUNT] =
  {
    /* --------------------------------------------------------------------------- */
    /* blocked section								   */
    /* --------------------------------------------------------------------------- */
    "BLOCKED_RMUTEX", /* us, handle reception, rmutex */
    "BLOCKED_WAIT_WORKER", /* us, handle connection error */

    /* --------------------------------------------------------------------------- */
    /* network									   */
    /* --------------------------------------------------------------------------- */
    "NET_RECV", /* bytes */
    "NET_SEND", /* bytes */

    "NET_PACKET_COUNT", /* count */

    "NET_CLIENTS", /* count */

    /* --------------------------------------------------------------------------- */
    /* memory									   */
    /* --------------------------------------------------------------------------- */
    "MEM_POOL_COMMIT", /* count, receiver */
    "MEM_POOL_RELEASE", /* count, receiver */

    "MEM_ALLOCATE", /* count, receiver */
    "MEM_DELETE", /* count, receiver */

    /* --------------------------------------------------------------------------- */
    /* message queue								   */
    /* --------------------------------------------------------------------------- */
    "MQ_REQUESTED", /* count */
    "MQ_NEW_CLIENT", /* count */
    "MQ_SHUTDOWN_CLIENT", /* count */
    "MQ_SEND_PACKET", /* count */
    "MQ_RELEASE_PACKET" /* count */
  };

  class connection_stats
  {
    public:
      connection_stats ();
      ~connection_stats ();

      inline void add (stats key, std::uint64_t value)
      {
	m_values[key] += value;
      }

      inline void sub (stats key, std::uint64_t value)
      {
	m_values[key] -= value;
      }

      inline std::uint64_t get (stats key)
      {
	return m_values[key];
      }

    private:
      std::uint64_t m_values[STATS_COUNT];
  };
}

#endif
