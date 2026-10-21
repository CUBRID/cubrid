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
  struct kind_entry
  {
    stream_session_factory factory;
    bool ends_unit_of_work;
  };

  /* Held in a function-local static so a consumer registering from its own
   * load-time initializer cannot run before this table is constructed. */
  kind_entry *
  kind_table ()
  {
    static kind_entry table[STREAM_KIND_MAX] = {};

    return table;
  }
}

/*
 * stream_session_register () - Register the factory that builds sessions of one kind
 *   kind(in): the STREAM_KIND_* value the consumer owns
 *   factory(in): builder for that kind
 *   ends_unit_of_work(in): does END of this kind finish the statement?
 */
void
stream_session_register (int kind, stream_session_factory factory, bool ends_unit_of_work)
{
  assert (kind >= STREAM_KIND_MIN && kind < STREAM_KIND_MAX);
  assert (factory != NULL);

  kind_table ()[kind].factory = factory;
  kind_table ()[kind].ends_unit_of_work = ends_unit_of_work;
}

/*
 * stream_session_kind_ends_unit_of_work () - What the consumer declared at registration
 *   return: true if END of this kind finishes the statement, false otherwise
 *   kind(in): STREAM_KIND_* taken off the wire, not yet trusted
 */
bool
stream_session_kind_ends_unit_of_work (int kind)
{
  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX)
    {
      return false;
    }

  return kind_table ()[kind].ends_unit_of_work;
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

  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || kind_table ()[kind].factory == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, "unknown stream kind");
      *error_code = ER_STREAM_SESSION_ERROR;
      return NULL;
    }

  factory = kind_table ()[kind].factory;

  return factory (thread_p, config, config_len, error_code);
}
