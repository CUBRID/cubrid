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
 * stream_session.cpp - Consumer registry for the shared byte-stream transport.
 *
 * The open path carries a STREAM_KIND tag; the session it names is built by
 * the consumer that registered for that kind. Keeping the table here is what
 * lets the transport open a session it cannot name.
 */

#include "stream_session.hpp"

#include "error_code.h"
#include "error_manager.h"

#include <cassert>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  /* Held in a function-local static so a consumer registering from its own
   * load-time initializer cannot run before this table is constructed. */
  stream_session_factory *
  factory_table ()
  {
    static stream_session_factory table[STREAM_KIND_MAX] = { NULL };

    return table;
  }
}

/*
 * stream_session_register () - Register the factory that builds sessions of one kind
 *   kind(in): STREAM_KIND_* the consumer owns
 *   factory(in): builder for that kind
 */
void
stream_session_register (STREAM_KIND kind, stream_session_factory factory)
{
  assert (kind >= STREAM_KIND_MIN && kind < STREAM_KIND_MAX);
  assert (factory != NULL);

  factory_table ()[kind] = factory;
}

/*
 * stream_session_create () - Build the session named by a stream kind
 *   return: opened session on success, NULL on error
 *   kind(in): STREAM_KIND_* taken off the wire, not yet trusted
 *   config(in): the kind's config blob
 *   config_len(in): length of the config blob
 *   error_code(out): NO_ERROR or the failure code
 */
stream_session *
stream_session_create (THREAD_ENTRY *thread_p, int kind, const char *config, int config_len, int *error_code)
{
  stream_session_factory factory;

  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || factory_table ()[kind] == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, "unknown stream kind");
      *error_code = ER_STREAM_SESSION_ERROR;
      return NULL;
    }

  factory = factory_table ()[kind];

  return factory (thread_p, config, config_len, error_code);
}
