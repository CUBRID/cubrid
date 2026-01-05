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

    MQ_COMPLETED, /* count */
    MQ_NEW_CLIENT, /* count */
    MQ_HANDOFF_CLIENT, /* count */
    MQ_TAKEOVER_CLIENT, /* count */
    MQ_SHUTDOWN_CLIENT, /* count */
    MQ_SEND_PACKET, /* count */
    MQ_RELEASE_PACKET, /* count */

    /* --------------------------------------------------------------------------- */
    /* blocked									   */
    /* --------------------------------------------------------------------------- */
    BLOCKED_RMUTEX, /* us */

    /* --------------------------------------------------------------------------- */
    /* stats count (will not be allocated)					   */
    /* --------------------------------------------------------------------------- */
    STATS_COUNT,
    NA
  };

  inline std::pair<std::string, std::string> worker_to_string[static_cast <std::size_t> (worker::STATS_COUNT)] =
  {
    { "PACKET_COUNT", "" },
    { "CLIENT_NUM", "" },

    { "MQ_REQUESTED", "" },

    { "MQ_COMPLETED", "" },
    { "MQ_NEW_CLIENT", "" },
    { "MQ_HANDOFF_CLIENT", "" },
    { "MQ_TAKEOVER_CLIENT", "" },
    { "MQ_SHUTDOWN_CLIENT", "" },
    { "MQ_SEND_PACKET", "" },
    { "MQ_RELEASE_PACKET", "" },

    { "BLOCKED_RMUTEX", "us" },
  };

  template <class T, typename VT = std::uint64_t>
  class metrics
  {
      template <class U, typename V>
      friend class metrics;

    public:
      metrics ();
      ~metrics ();

      metrics (const metrics &other);
      inline metrics &operator= (const metrics &other);

      metrics (metrics &&other) noexcept = default;
      inline metrics &operator= (metrics &&other) noexcept = default;

      template <typename OtherVT>
      metrics (metrics<T, OtherVT> &other);
      template <typename OtherVT>
      inline metrics &operator= (metrics<T, OtherVT> &other);
      inline metrics &operator+= (metrics &other);

      inline metrics operator+ (const metrics &other);
      inline metrics<T, double> operator- (const metrics &other);
      inline metrics<T, double> operator* (double multiplier);

      inline void reset ();

      inline void add (T key, VT value);
      inline void sub (T key, VT value);
      inline VT get (T key);
      inline void set (T key, VT value);

      inline void copy_from (const metrics &src);

    private:
      VT m_values[static_cast<std::size_t> (T::STATS_COUNT)];
  };

  template <class T, typename VT>
  metrics<T, VT>::metrics ()
  {
    this->reset ();
  }

  template <class T, typename VT>
  metrics<T, VT>::~metrics ()
  {
  }

  template <class T, typename VT>
  metrics<T, VT>::metrics (const metrics &other)
  {
    copy_from (other);
  }

  template <class T, typename VT>
  inline metrics<T, VT> &metrics<T, VT>::operator= (const metrics &other)
  {
    if (this != &other)
      {
	copy_from (other);
      }
    return *this;
  }

  template <class T, typename VT>
  template <typename OtherVT>
  metrics<T, VT>::metrics (metrics<T, OtherVT> &other)
  {
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	m_values[i] = static_cast<VT> (other.m_values[i]);
      }
  }

  template <class T, typename VT>
  template <typename OtherVT>
  inline metrics<T, VT> &metrics<T, VT>::operator= (metrics<T, OtherVT> &other)
  {
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	m_values[i] = static_cast<VT> (other.m_values[i]);
      }
    return *this;
  }

  template <class T, typename VT>
  inline metrics<T, VT> &metrics<T, VT>::operator+= (metrics &other)
  {
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	m_values[i] += other.m_values[i];
      }

    return *this;
  }

  template <class T, typename VT>
  inline metrics<T, VT> metrics<T, VT>::operator+ (const metrics &other)
  {
    metrics result;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	result.m_values[i] = m_values[i] + other.m_values[i];
      }

    return result;
  }

  template <class T, typename VT>
  inline metrics<T, double> metrics<T, VT>::operator- (const metrics &other)
  {
    metrics<T, double> result;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	result.m_values[i] = static_cast<double> (m_values[i]) - static_cast<double> (other.m_values[i]);
      }

    return result;
  }

  template <class T, typename VT>
  inline metrics<T, double> metrics<T, VT>::operator* (double multiplier)
  {
    metrics<T, double> result;
    std::size_t i;

    for (i = 0; i < static_cast<std::size_t> (T::STATS_COUNT); i++)
      {
	result.m_values[i] = m_values[i] * multiplier;
      }

    return result;
  }

  template <class T, typename VT>
  inline void metrics<T, VT>::reset ()
  {
    std::memset (m_values, 0, sizeof (m_values));
  }

  template <class T, typename VT>
  inline void metrics<T, VT>::add (T key, VT value)
  {
    m_values[static_cast<std::size_t> (key)] += value;
  }

  template <class T, typename VT>
  inline void metrics<T, VT>::sub (T key, VT value)
  {
    m_values[static_cast<std::size_t> (key)] -= value;
  }

  template <class T, typename VT>
  inline VT metrics<T, VT>::get (T key)
  {
    return m_values[static_cast<std::size_t> (key)];
  }

  template <class T, typename VT>
  inline void metrics<T, VT>::set (T key, VT value)
  {
    m_values[static_cast<std::size_t> (key)] = value;
  }

  template <class T, typename VT>
  inline void metrics<T, VT>::copy_from (const metrics &src)
  {
    std::memcpy (m_values, src.m_values, sizeof (m_values));
  }
}

#endif
