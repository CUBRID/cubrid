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
 * The open paths carry a STREAM_KIND tag; the session or source it names is
 * built by the consumer that registered for that kind. Keeping the table here
 * is what lets the transport open what it cannot name.
 */

#include "stream_session.hpp"

#include "error_code.h"
#include "error_manager.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  struct kind_entry
  {
    bool registered;
    stream_kind_desc desc;
  };

  /* Held in a function-local static so a consumer registering from its own
   * load-time initializer cannot run before this table is constructed. */
  kind_entry *
  kind_table ()
  {
    static kind_entry table[STREAM_KIND_MAX] = {};

    return table;
  }

  bool
  kind_desc_is_consistent (const stream_kind_desc &desc)
  {
    if (desc.direction == STREAM_UPLOAD)
      {
	return desc.sink != NULL && desc.source == NULL;
      }

    /* nothing a download ends or stages, and its handle reads in one transaction: the client forgets its
     * downloads at every transaction end, so a handle that outlived one could never be released */
    return desc.direction == STREAM_DOWNLOAD && desc.source != NULL && desc.sink == NULL && !desc.ends_unit_of_work
	   && !desc.result_is_handle && desc.scope == STREAM_SCOPE_TRAN;
  }

  /* the registered record of kind in direction, or NULL with the transport-common error set */
  const stream_kind_desc *
  kind_open_desc (int kind, stream_direction direction, int *error_code)
  {
    const stream_kind_desc *desc = stream_kind_find (kind);

    if (desc == NULL || desc->direction != direction)
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1,
		desc == NULL ? "unknown stream kind" : "stream kind opened in the wrong direction");
	*error_code = ER_STREAM_SESSION_ERROR;
	return NULL;
      }

    return desc;
  }
}

/*
 * stream_kind_register () - Register the server half of one kind
 *   return: NO_ERROR, or ER_STREAM_SESSION_ERROR for a record that does not hold together
 *   kind(in): the STREAM_KIND_* value the consumer owns
 *   desc(in): its direction, factory and answers
 */
int
stream_kind_register (int kind, const stream_kind_desc &desc)
{
  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || !kind_desc_is_consistent (desc))
    {
      return ER_STREAM_SESSION_ERROR;
    }

  kind_table ()[kind].desc = desc;
  kind_table ()[kind].registered = true;

  return NO_ERROR;
}

/*
 * stream_kind_find () - The registered record of a kind
 *   return: the record, or NULL for an unknown or unregistered kind
 *   kind(in): STREAM_KIND_* taken off the wire, not yet trusted
 */
const stream_kind_desc *
stream_kind_find (int kind)
{
  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || !kind_table ()[kind].registered)
    {
      return NULL;
    }

  return &kind_table ()[kind].desc;
}

/*
 * stream_session_create () - Build the upload session named by a stream kind
 *   return: opened session on success, NULL on error
 *   kind(in): STREAM_KIND_* taken off the wire, not yet trusted
 *   config(in): the kind's config blob
 *   config_len(in): length of the config blob
 *   error_code(out): NO_ERROR or the failure code
 */
stream_session *
stream_session_create (THREAD_ENTRY *thread_p, int kind, const char *config, int config_len, int *error_code)
{
  const stream_kind_desc *desc = kind_open_desc (kind, STREAM_UPLOAD, error_code);

  if (desc == NULL)
    {
      return NULL;
    }

  return desc->sink (thread_p, config, config_len, error_code);
}

/*
 * stream_source_create () - Build the download source named by a stream kind
 *   return: opened source on success, NULL on error
 *   kind(in): STREAM_KIND_* taken off the wire, not yet trusted
 *   config(in): the kind's config blob
 *   config_len(in): length of the config blob
 *   error_code(out): NO_ERROR or the failure code
 */
stream_source *
stream_source_create (THREAD_ENTRY *thread_p, int kind, const char *config, int config_len, int *error_code)
{
  const stream_kind_desc *desc = kind_open_desc (kind, STREAM_DOWNLOAD, error_code);

  if (desc == NULL)
    {
      return NULL;
    }

  return desc->source (thread_p, config, config_len, error_code);
}
