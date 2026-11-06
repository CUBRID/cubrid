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
 * stream_client.c - Client half of a stream kind's registration.
 */

#include "stream_client.h"

#include "error_code.h"
#include "error_manager.h"
#include "stream_session.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* Held in a function-local static so a consumer registering from its own
 * load-time initializer cannot run before this table is constructed. */
static stream_kind_client_desc *
stream_client_kind_table (void)
{
  static stream_kind_client_desc table[STREAM_KIND_MAX] = { };

  return table;
}

static int
stream_client_refuse (const char *what)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, what);
  return ER_STREAM_SESSION_ERROR;
}

/*
 * stream_kind_client_register () - Register the client half of one kind
 *   return: NO_ERROR, or ER_STREAM_SESSION_ERROR for a record that does not hold together
 *   kind(in): the STREAM_KIND_* value the consumer owns
 *   desc(in): who may open it
 */
int
stream_kind_client_register (int kind, const stream_kind_client_desc & desc)
{
  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || (desc.driver_openable && desc.authorize == NULL))
    {
      return ER_STREAM_SESSION_ERROR;
    }

  stream_client_kind_table ()[kind] = desc;

  return NO_ERROR;
}

/*
 * stream_kind_client_check_driver_open () - May a driver open this kind directly?
 *   return: NO_ERROR, or ER_STREAM_SESSION_ERROR (set) for a kind no statement checks for it
 *   kind(in): STREAM_KIND_* taken off the driver wire, not yet trusted
 */
int
stream_kind_client_check_driver_open (int kind)
{
  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || !stream_client_kind_table ()[kind].driver_openable)
    {
      return stream_client_refuse ("stream kind cannot be opened by a driver");
    }

  return NO_ERROR;
}

/*
 * stream_kind_client_authorize () - May the current user open this config of a kind?
 *   return: NO_ERROR, or the error the kind's authorize set
 *   kind(in): STREAM_KIND_* not yet trusted
 *   config(in), config_len(in): the config blob the open will carry
 */
int
stream_kind_client_authorize (int kind, const char *config, int config_len)
{
  if (kind < STREAM_KIND_MIN || kind >= STREAM_KIND_MAX || stream_client_kind_table ()[kind].authorize == NULL)
    {
      return stream_client_refuse ("stream kind has no authorize check");
    }

  return stream_client_kind_table ()[kind].authorize (config, config_len);
}
