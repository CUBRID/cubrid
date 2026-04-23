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
 * thread_compat - interface for compatibility with cubrid projects.
 *
 * NOTE - normally thread module belongs to server stand-alone projects. however, some functions may require thread
 *        entries on server module, but are also used on client module
 */

#ifndef _THREAD_COMPAT_HPP_
#define _THREAD_COMPAT_HPP_

// forward definition for THREAD_ENTRY
#if defined (SERVER_MODE) || (defined (SA_MODE) && defined (__cplusplus))
#include <thread>
namespace cubthread
{
  class entry;

  enum class stats : bool
  {
    on = true,
    off = false
  };

  enum class pool_type
  {
    basic,
    elastic
  };

} // namespace cubthread
typedef cubthread::stats stats_t;
typedef cubthread::pool_type pool_t;
#ifndef _THREAD_ENTRY_HPP_
typedef cubthread::entry THREAD_ENTRY;
typedef std::thread::id thread_id_t;
#endif // _THREAD_ENTRY_HPP_

#else // not SERVER_MODE and not SA_MODE-C++
// client or SA_MODE annoying grammars
typedef void THREAD_ENTRY;
typedef bool stats_t;
typedef unsigned int pool_t;
typedef unsigned long int thread_id_t;
#endif // not SERVER_MODE and not SA_MODE

#endif // _THREAD_COMPAT_HPP_
