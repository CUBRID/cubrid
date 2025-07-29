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
 * connection_pool.hpp
 */

#ifndef _THREAD_CONNECTION_POOL_HPP_
#define _THREAD_CONNECTION_POOL_HPP_

#include <topology.hpp>

namespace cubthread
{
  class connection_pool
  {
  public:
    connection_pool ();
    ~connection_pool ();

  private:
  };
}

#endif
