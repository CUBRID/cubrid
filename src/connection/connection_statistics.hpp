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
 * connection_statistics.hpp
 */

#ifndef _CONNECTION_STATISTICS_HPP_
#define _CONNECTION_STATISTICS_HPP_

#include <string>
#include <cstring>
#include <cstdint>

namespace cubconn::statistics
{
  enum class context : std::uint8_t
  {
    /* --------------------------------------------------------------------------- */
    /* network traffic								   */
    /* --------------------------------------------------------------------------- */
    BYTES_IN_TOTAL, /* bytes */
    BYTES_OUT_TOTAL, /* bytes */

    /* --------------------------------------------------------------------------- */
    /* activity									   */
    /* --------------------------------------------------------------------------- */
    OPEND_NS, /* ns */
    LAST_ACTIVE_NS, /* ns */
    LAST_MOVED_NS, /* ns */
    MOVE_COUNT, /* count */

    /* --------------------------------------------------------------------------- */
    /* load									   */
    /* --------------------------------------------------------------------------- */
    RECV_BUDGET_HIT, /* count */
    SEND_BUDGET_HIT, /* count */

    /* --------------------------------------------------------------------------- */
    /* stats count		 						   */
    /* --------------------------------------------------------------------------- */
    STATS_COUNT
  };

  enum class worker : std::uint8_t
  {
    /* --------------------------------------------------------------------------- */
    /* network									   */
    /* --------------------------------------------------------------------------- */
    PACKET_COUNT, /* count */
    CLIENT_NUM, /* count */

    /* --------------------------------------------------------------------------- */
    /* message queue								   */
    /* --------------------------------------------------------------------------- */
    MQ_REQUESTED, /* count */
    MQ_NEW_CLIENT, /* count */
    MQ_SHUTDOWN_CLIENT, /* count */
    MQ_SEND_PACKET, /* count */
    MQ_RELEASE_PACKET, /* count */

    /* --------------------------------------------------------------------------- */
    /* blocked									   */
    /* --------------------------------------------------------------------------- */
    BLOCKED_RMUTEX, /* us */

    /* --------------------------------------------------------------------------- */
    /* stats count		 						   */
    /* --------------------------------------------------------------------------- */
    STATS_COUNT
  };

  inline std::pair<std::string, std::string> worker_to_string[static_cast <std::size_t> (worker::STATS_COUNT)] =
  {
    { "PACKET_COUNT", "" },
    { "CLIENT_NUM", "" },

    { "MQ_REQUESTED", "" },
    { "MQ_NEW_CLIENT", "" },
    { "MQ_SHUTDOWN_CLIENT", "" },
    { "MQ_SEND_PACKET", "" },
    { "MQ_RELEASE_PACKET", "" },

    { "BLOCKED_RMUTEX", "us" },
  };

  template <class T>
  class metrics
  {
    public:
      metrics ();
      ~metrics ();

      metrics (const metrics &other);
      metrics &operator= (const metrics &other);

      metrics (metrics &&other) noexcept = default;
      metrics &operator= (metrics &&other) noexcept = default;

      inline metrics operator+ (const metrics &other);
      inline metrics operator- (const metrics &other);
      inline metrics operator* (double multiplier);

      inline void reset ();

      inline void add (T key, std::uint64_t value);
      inline void sub (T key, std::uint64_t value);
      inline std::uint64_t get (T key);
      inline void set (T key, std::uint64_t value);

      inline void copy_from (const metrics &src);

    private:
      std::uint64_t m_values[static_cast<std::size_t> (T::STATS_COUNT)];
  };

  template <class T>
  metrics<T>::metrics ()
  {
    this->reset ();
  }

  template <class T>
  metrics<T>::~metrics ()
  {
  }

  template <class T>
  metrics<T>::metrics (const metrics &other)
  {
    copy_from (other);
  }

  template <class T>
  metrics<T> &metrics<T>::operator= (const metrics &other)
  {
    if (this != &other)
      {
	copy_from (other);
      }
    return *this;
  }

  template <class T>
  metrics<T> metrics<T>::operator+ (const metrics &other)
  {
    metrics result;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	result.m_values[i] = m_values[i] + other.m_values[i];
      }

    return result;
  }

  template <class T>
  metrics<T> metrics<T>::operator- (const metrics &other)
  {
    metrics result;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	result.m_values[i] = m_values[i] - other.m_values[i];
      }

    return result;
  }

  template <class T>
  metrics<T> metrics<T>::operator* (double multiplier)
  {
    metrics result;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	result.m_values[i] = static_cast<std::uint64_t> (m_values[i] * multiplier);
      }

    return result;
  }

  template <class T>
  void metrics<T>::reset ()
  {
    std::memset (m_values, 0, sizeof (m_values));
  }

  template <class T>
  void metrics<T>::add (T key, std::uint64_t value)
  {
    m_values[static_cast<std::size_t> (key)] += value;
  }

  template <class T>
  void metrics<T>::sub (T key, std::uint64_t value)
  {
    m_values[static_cast<std::size_t> (key)] -= value;
  }

  template <class T>
  std::uint64_t metrics<T>::get (T key)
  {
    return m_values[static_cast<std::size_t> (key)];
  }

  template <class T>
  void metrics<T>::set (T key, std::uint64_t value)
  {
    m_values[static_cast<std::size_t> (key)] = value;
  }

  template <class T>
  void metrics<T>::copy_from (const metrics &src)
  {
    std::memcpy (m_values, src.m_values, sizeof (m_values));
  }
}

#endif
