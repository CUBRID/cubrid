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

#include <cstdint>
#include <limits>

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
}

#endif // !_RESPONSE_BROKER_HPP_
