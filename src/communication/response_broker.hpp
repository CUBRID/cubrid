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

      void register_request (response_sequence_number a_rsn);
      void register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
      void register_error (response_sequence_number a_rsn, T_ERROR &&a_error);

      std::tuple<T_PAYLOAD, T_ERROR> get_response (response_sequence_number a_rsn);

      void notify_terminate_and_wait ();

    private:
      struct bucket
      {
	  bucket (T_ERROR a_no_error, T_ERROR a_error);
	  ~bucket ();

	  bucket (const bucket &);
	  bucket (bucket &&) = delete;

	  bucket &operator = (const bucket &) = delete;
	  bucket &operator = (bucket &&) = delete;

	  void register_request (response_sequence_number a_rsn);
	  void register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
	  void register_error (response_sequence_number a_rsn, T_ERROR &&a_error);

	  std::tuple<T_PAYLOAD, T_ERROR> get_response (response_sequence_number a_rsn);

	  void notify_terminate_and_wait ();

	private:
	  struct payload_or_error_type
	  {
	    T_PAYLOAD m_payload;
	    T_ERROR m_error;
	    bool m_response_or_error_present = false;
	  };

	  using response_payload_container_type = std::unordered_map<response_sequence_number, payload_or_error_type>;

	private:
	  const T_ERROR m_no_error;
	  const T_ERROR m_error;

	  // mutex and condition variable protecting the response payloads container
	  std::mutex m_mutex;
	  std::condition_variable m_condvar;
	  response_payload_container_type m_response_payloads;

	  bool m_terminate;
	  std::condition_variable m_terminate_condvar;
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
  response_broker<T_PAYLOAD, T_ERROR>::register_request (response_sequence_number a_rsn)
  {
    get_bucket (a_rsn).register_request (a_rsn);
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
  std::tuple<T_PAYLOAD, T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::get_response (response_sequence_number a_rsn)
  {
    return get_bucket (a_rsn).get_response (a_rsn);
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::notify_terminate_and_wait ()
  {
    for (auto &bucket : m_buckets)
      {
	bucket.notify_terminate_and_wait ();
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
    // NOTE:
    //  - might not hold in the event that a peer server crashes before providing a response;
    //  - currently this is either implemented (or not) in the application layers above;
    //  - if it were to be sorted out at this level, a timeout must be added after which an
    //    error is returned to the waiting thread
    assert (m_response_payloads.empty ());
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::bucket::register_request (response_sequence_number a_rsn)
  {
    {
      std::lock_guard<std::mutex> lk_guard (m_mutex);

      assert (!m_terminate);
      assert (m_response_payloads.find (a_rsn) == m_response_payloads.cend ());

      payload_or_error_type &ent = m_response_payloads[a_rsn];
      ent.m_response_or_error_present = false;
    }
    // don't notify anything because there is no-one interested in this (the request has not even been flown out yet)
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::bucket::register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload)
  {
    {
      std::lock_guard<std::mutex> lk_guard (m_mutex);

      if (m_terminate)
	{
	  // it has been terminated. the entry may have been erased or will be erased soon.
	  return;
	}

      payload_or_error_type &ent = m_response_payloads[a_rsn];
      assert (!ent.m_response_or_error_present);
      ent.m_payload = std::move (a_payload);
      ent.m_error = m_no_error;
      ent.m_response_or_error_present = true;
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

      if (m_terminate)
	{
	  // it has been terminated. the entry may have been erased or will be erased soon.
	  return;
	}

      payload_or_error_type &ent = m_response_payloads[a_rsn];
      assert (!ent.m_response_or_error_present);
      ent.m_error = std::move (a_error);
      ent.m_response_or_error_present = true;
    }
    // notify all because there is more than one thread waiting for data on the same bucket
    // ideally, with an adequately sized bucket pool, the contention should be minimal
    m_condvar.notify_all ();
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  std::tuple<T_PAYLOAD, T_ERROR>
  response_broker<T_PAYLOAD, T_ERROR>::bucket::get_response (response_sequence_number a_rsn)
  {
    constexpr std::chrono::milliseconds millis_100 { 100 };

    {
      std::unique_lock<std::mutex> ulock (m_mutex);

      // nobody else but us should access this payload container but must be accessed locked
      typename response_payload_container_type::iterator found_it = m_response_payloads.find (a_rsn);
      payload_or_error_type &ent = (*found_it).second;

      auto condvar_pred = [&ent] ()
      {
	return ent.m_response_or_error_present;
      };

      // a way out in case neither value nor error is registered as a response
      // which can happen in case - eg - that the connection is dropped
      while (!m_terminate)
	{
	  if (m_condvar.wait_for (ulock, millis_100, condvar_pred))
	    {
	      assert (ent.m_response_or_error_present);
	      std::tuple<T_PAYLOAD, T_ERROR> payload_or_error
	      {
		std::move (ent.m_payload ),
		std::move (ent.m_error)
	      };

	      m_response_payloads.erase (found_it);
	      return payload_or_error;
	    }
	}

      // terminate called; from here on - damage control

      // NOTE: when terminate is invoked there is the possibility to implement a configurable behaviour:
      //  - either abort and return error - as done below
      //  - or continue waiting - with a timeout - until the request is serviced by the peer server

      // still under lock, erase the entry from the container to validate termination condition
      m_response_payloads.erase (found_it);

      // NOTE: a [benign] race condition can happen here between the moment the container is emptied
      // and the error result is actually returned to the waiting thread; however, the occurence of that
      // depends very much on the way the resources are stopped globally in the upper application layers (eg:
      // maybe the client transactions - threads - are stopped and waited for before the communication
      // infrastructure is torn down
    }

    // if here, terminate was specified and there is at most one thread to notify
    m_terminate_condvar.notify_one ();

    // upon terminate, error response will be returned
    return std::make_tuple (T_PAYLOAD (), m_error);
  }

  template <typename T_PAYLOAD, typename T_ERROR>
  void
  response_broker<T_PAYLOAD, T_ERROR>::bucket::notify_terminate_and_wait ()
  {
    {
      std::lock_guard<std::mutex> lockg (m_mutex);
      if (m_terminate)
	{
	  return; // it has been terminated. the entry may have been erased or will be erased soon.
	}
      m_terminate = true;
    }
    // notify all because there is more than one thread waiting for data on the same bucket
    // ideally, with an adequately sized bucket pool, the contention should be minimal
    m_condvar.notify_all ();

    // wait for all requests to either be serviced or timeout
    constexpr std::chrono::milliseconds millis_50 { 50 };

    auto condvar_pred = [this] ()
    {
      return m_response_payloads.empty ();
    };

    std::unique_lock<std::mutex> payloads_container_ulock (m_mutex);
    while (!m_terminate_condvar.wait_for (payloads_container_ulock, millis_50, condvar_pred))
      {
	// NOTE: a configurable timeout can be introduced here
      }
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
