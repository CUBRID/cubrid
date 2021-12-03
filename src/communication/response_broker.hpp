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
  constexpr NO_RESPONSE_SEQUENCE_NUMBER = std::numeric_limits<request_sync_client_server>::max ();

  class response_sequence_number_generator
  {
    public:
      response_sequence_number get_unique_number ();

    private:
      std::atomic<response_sequence_number> m_next_number = 0;
  };

  template<typename T_PAYLOAD>
  class response_broker
  {
    public:
      response_broker () = delete;
      response_broker (size_t a_bucket_count);

      void register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
      T_PAYLOAD get_response (response_sequence_number a_rsn);

    private:

      struct bucket
      {
	std::mutex m_mutex;
	std::condition_variable m_condvar;
	std::unordered_map<response_sequence_number, T_PAYLOAD> m_response_payloads;

	void register_response (response_sequence_number a_rsn, T_PAYLOAD &&a_payload);
	T_PAYLOAD get_response (response_sequence_number a_rsn);
      };

      bucket &get_bucket ();

      std::vector<bucket> m_buckets;
  };
}

#endif // !_RESPONSE_BROKER_HPP_
