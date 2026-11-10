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

#include <cstdio>

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

  /* a config starts with its version, a big-endian int as or_pack_int () writes it */
  const int CONFIG_VERSION_SIZE = 4;

  int
  config_version (const char *config)
  {
    const unsigned char *p = (const unsigned char *) config;

    return (int) (((unsigned int) p[0] << 24) | ((unsigned int) p[1] << 16) | ((unsigned int) p[2] << 8) | p[3]);
  }

  bool
  kind_desc_is_consistent (const stream_kind_desc &desc)
  {
    if (desc.config_version_max < 1)
      {
	return false;
      }

    if (desc.direction == STREAM_UPLOAD)
      {
	return desc.sink != NULL && desc.source == NULL;
      }

    /* a download ends no statement */
    return desc.direction == STREAM_DOWNLOAD && desc.source != NULL && desc.sink == NULL && !desc.ends_unit_of_work;
  }

  void
  refuse_open (int *error_code, int code, const char *what)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, code, 1, what);
    *error_code = code;
  }

  /* The registered record of kind in direction, whose config the factory may read, or NULL with the error set:
   * ER_STREAM_NOT_SUPPORTED for a kind or a config version this server does not take, ER_STREAM_INVALID_USE for a
   * config without a version. */
  const stream_kind_desc *
  kind_open_desc (int kind, stream_direction direction, const char *config, int config_len, int *error_code)
  {
    const stream_kind_desc *desc = stream_kind_find (kind);
    char msg[96];
    int version;

    if (desc == NULL || desc->direction != direction)
      {
	refuse_open (error_code, ER_STREAM_NOT_SUPPORTED,
		     desc == NULL ? "unknown stream kind" : "stream kind opened in the wrong direction");
	return NULL;
      }

    if (config == NULL || config_len < CONFIG_VERSION_SIZE)
      {
	refuse_open (error_code, ER_STREAM_INVALID_USE, "stream config has no version");
	return NULL;
      }

    version = config_version (config);
    if (version < 1)
      {
	refuse_open (error_code, ER_STREAM_INVALID_USE, "stream config version must be positive");
	return NULL;
      }
    if (version > desc->config_version_max)
      {
	snprintf (msg, sizeof (msg), "stream config version %d is above %d, the highest this server accepts", version,
		  desc->config_version_max);
	refuse_open (error_code, ER_STREAM_NOT_SUPPORTED, msg);
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
  const stream_kind_desc *desc = kind_open_desc (kind, STREAM_UPLOAD, config, config_len, error_code);

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
  const stream_kind_desc *desc = kind_open_desc (kind, STREAM_DOWNLOAD, config, config_len, error_code);

  if (desc == NULL)
    {
      return NULL;
    }

  return desc->source (thread_p, config, config_len, error_code);
}
