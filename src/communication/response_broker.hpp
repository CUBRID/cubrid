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

#ifndef _RESPONSE_BROKER_HPP_
#define _RESPONSE_BROKER_HPP_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace cubcomm
{
  using response_sequence_number = std::uint64_t;
  constexpr response_sequence_number NO_RESPONSE_SEQUENCE_NUMBER =
	  std::numeric_limits<response_sequence_number>::max ();

  class response_sequence_number_generator
  {
    public:
      response_sequence_number get_unique_number ()
      {
	return ++m_next_number;
      }

    private:
      std::atomic<response_sequence_number> m_next_number = 0;
  };

  template<typename T_PAYLOAD, typename T_ERROR>
  class response_broker
  {
    public:
      response_broker () = delete;
      response_broker (size_t a_bucket_count, T_ERROR a_no_error, T_ERROR a_error);

      response_broker (const response_broker &) = delete;
      response_broker (response_broker &&) = delete;

      response_broker &operator = (const response_broker &) = delete;
      response_broker &operator = (response_broker &&) = delete;

      void register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
      void register_error (response_sequence_number a_rsn, T_ERROR &&a_error);

      std::tuple<T_PAYLOAD, T_ERROR> get_response (response_sequence_number a_rsn);

      void terminate ();

    private:
      struct bucket
      {
	  bucket (T_ERROR a_no_error, T_ERROR a_error);
	  ~bucket ();

	  bucket (const bucket &);
	  bucket (bucket &&) = delete;

	  bucket &operator = (const bucket &) = delete;
	  bucket &operator = (bucket &&) = delete;

	  void register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
	  void register_error (response_sequence_number a_rsn, T_ERROR &&a_error);

	  std::tuple<T_PAYLOAD, T_ERROR> get_response (response_sequence_number a_rsn);

	  void terminate ();

	private:
	  struct payload_or_error_type
	  {
	    T_PAYLOAD m_payload;
	    T_ERROR m_error;
	  };

	  const T_ERROR m_no_error;
	  const T_ERROR m_error;

	  std::mutex m_mutex;
	  std::condition_variable m_condvar;
	  std::unordered_map<response_sequence_number, payload_or_error_type> m_response_payloads;

	  bool m_terminate;
      };

      bucket &get_bucket (response_sequence_number rsn);

      const size_t m_bucket_count;
      std::vector<bucket> m_buckets;
  };
}

//
// Template implementation
//

namespace cubcomm
{
  template <typename T_PAYLOAD, typename T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::response_broker (size_t a_bucket_count, T_ERROR a_no_error, T_ERROR a_error)
    : m_bucket_count { a_bucket_count }
    , m_buckets { a_bucket_count, { a_no_error, a_error } }
  {
    assert (a_bucket_count > 0);
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  typename response_broker<T_PAYLOAD, T_ERROR>::bucket &
  response_broker<T_PAYLOAD, T_ERROR>::get_bucket (response_sequence_number rsn)
  {
    return m_buckets[rsn % m_bucket_count];
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload)
  {
    get_bucket (a_rsn).register_response (a_rsn, std::move (a_payload));
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::register_error (response_sequence_number a_rsn, T_ERROR &&a_error)
  {
    get_bucket (a_rsn).register_error (a_rsn, std::move (a_error));
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::terminate ()
  {
    for (auto &bucket : m_buckets)
      {
	bucket.terminate ();
      }
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::bucket::bucket (T_ERROR a_no_error, T_ERROR a_error)
    : m_no_error { a_no_error }
    , m_error { a_error }
    , m_terminate { false }
  {
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::bucket::~bucket ()
  {
    // NOTE: might not hold in the event that a peer server crashes before consuming a response;
    // in which case the response will linger on in the queue indefinitely
    assert (m_response_payloads.empty ());
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::bucket::register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload)
  {
    {
      std::lock_guard<std::mutex> lk_guard (m_mutex);

      payload_or_error_type &ent = m_response_payloads[a_rsn];
      ent.m_payload = std::move (a_payload);
      ent.m_error = m_no_error;
    }
    // notify all because there is more than one thread waiting for data on the same bucket
    // ideally, with an adequately sized bucket pool, the contention should be minimal
    m_condvar.notify_all ();
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::bucket::register_error (response_sequence_number a_rsn, T_ERROR &&a_error)
  {
    {
      std::lock_guard<std::mutex> lockg (m_mutex);

      payload_or_error_type &ent = m_response_payloads[a_rsn];
      ent.m_error = std::move (a_error);
    }
    // notify all because there is more than one thread waiting for data on the same bucket
    // ideally, with an adequately sized bucket pool, the contention should be minimal
    m_condvar.notify_all ();
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  std::tuple<T_PAYLOAD, T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::get_response (response_sequence_number a_rsn)
  {
    return get_bucket (a_rsn).get_response (a_rsn);
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  std::tuple<T_PAYLOAD, T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::bucket::get_response (response_sequence_number a_rsn)
  {
    std::tuple<T_PAYLOAD, T_ERROR> payload_or_error { T_PAYLOAD (), m_error };

    auto condvar_pred = [this, &payload_or_error, a_rsn] ()
    {
      auto it = m_response_payloads.find (a_rsn);
      if (it == m_response_payloads.end ())
	{
	  return false;
	}
      payload_or_error = { std::move ((*it).second.m_payload), std::move ((*it).second.m_error) };
      m_response_payloads.erase (it);

      return true;
    };

    constexpr std::chrono::milliseconds millis_100 { 100 };
    {
      std::unique_lock<std::mutex> ulock (m_mutex);
      // a way out in case neither value nor error is registered as a response
      // which can happen in case - eg - that the connection is dropped
      while (!m_terminate)
	{
	  if (m_condvar.wait_for (ulock, millis_100, condvar_pred))
	    {
	      return payload_or_error;
	    }
	}
    }

    // upon terminate, error response will be returned
    return payload_or_error;
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::bucket::terminate ()
  {
    {
      std::lock_guard<std::mutex> lockg (m_mutex);
      m_terminate = true;
    }
    // notify all because there is more than one thread waiting for data on the same bucket
    // ideally, with an adequately sized bucket pool, the contention should be minimal
    m_condvar.notify_all ();
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::bucket::bucket (const bucket &that)
    : m_no_error { that.m_no_error }
    , m_error { that.m_error }
    , m_terminate { that.m_terminate }
  {
    // Bucket copy constructor may be required only during the response_broker initialization.
    // The copied bucket is empty and no synchronization is required.
    assert (!m_terminate);
    assert (that.m_response_payloads.empty ());
  }
}

#endif // !_RESPONSE_BROKER_HPP_
