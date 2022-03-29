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

#ifndef _ATOMIC_REPLICATION_HELPER_HPP_
#define _ATOMIC_REPLICATION_HELPER_HPP_

#include "log_lsa.hpp"
#include "log_record.hpp"

namespace cublog
{

  class atomic_replication_helper
  {
    public:

    private:

      class atomic_replication_unit
      {
	public:

	private:
	  log_lsa record_lsa;
	  log_rectype record_type;
	  VPID vpid;
	  LOG_RCVINDEX record_index;
      };

      //Hashmap
  };
}

#endif // _ATOMIC_REPLICATION_HELPER_HPP_
