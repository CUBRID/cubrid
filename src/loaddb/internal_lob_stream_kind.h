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
 * internal_lob_stream_kind.h - Internal LOB's tags on the byte-stream transport.
 *
 * The transport declares only the bound of the tag space; these values are
 * internal LOB's to name. They are shared because the two halves of each binding
 * sit on opposite sides of the wire: the client packs the open request
 * (network_interface_cl.c, load_common.cpp) and the server registers the factory
 * that answers it (internal_lob_stream_session.cpp, internal_lob_dml_executor.cpp,
 * load_internal_lob.cpp). The values are reserved in the allocation list beside
 * STREAM_KIND_MAX in stream_session.hpp.
 */

#ifndef _INTERNAL_LOB_STREAM_KIND_H_
#define _INTERNAL_LOB_STREAM_KIND_H_

#include "error_manager.h"
#include "object_representation.h"

/* Upload: one value is staged in the session's upload store for a later statement of the same transaction, which
 * binds the token END returns.  END does not end a unit of work -- committing there would commit that later
 * statement's transaction early.
 *   open config: INTERNAL_LOB_STREAM_TYPE (int), data length (int64), logical length (int64)
 *   data frame:  the next bytes of the value, in order */
#define STREAM_KIND_INTERNAL_LOB 1

/* COPY-style Internal LOB DML: the session owns the target DML and reports affected rows at finish(); unlike
 * STREAM_KIND_INTERNAL_LOB it never returns an upload token.  END runs the statement, so it ends a unit of work.
 *   open config: see internal_lob_dml_protocol.hpp (config version, XASL id, parameters, slot configs)
 *   data frame:  slot (int), absolute byte offset (int64), payload -- INTERNAL_LOB_DML_FRAME_HEADER_SIZE */
#define STREAM_KIND_INTERNAL_LOB_DML 2

/* loaddb (client/server) Internal LOB payload of one batch, written by the batch's load worker inside the batch
 * transaction; END returns the slot number the batch text refers to.  The batch commits the rows, not END, so END
 * does not end a unit of work.  Frames arrive tail-first. */
#define STREAM_KIND_INTERNAL_LOB_LOAD 3

/* STREAM_KIND_INTERNAL_LOB_LOAD open config:
 *   INTERNAL_LOB_STREAM_TYPE (int), data length (int64), logical length (int64), class id (int), batch id (int64) */
#define INTERNAL_LOB_LOAD_STREAM_CONFIG_SIZE (OR_INT_SIZE + OR_INT64_SIZE * 2 + OR_INT_SIZE + OR_INT64_SIZE)
/* STREAM_KIND_INTERNAL_LOB_LOAD data frame: absolute byte offset (int64) of the payload that follows.  The
 * payload of a value is sent from its end toward offset 0, the order the reverse chunk writer consumes. */
#define INTERNAL_LOB_LOAD_STREAM_FRAME_HEADER_SIZE (OR_INT64_SIZE)

enum INTERNAL_LOB_STREAM_TYPE
{
  INTERNAL_LOB_STREAM_TYPE_BLOB = 0,
  INTERNAL_LOB_STREAM_TYPE_CLOB = 1
};

/* Sets ER_STREAM_SESSION_ERROR with reason as its detail and returns it. */
inline int
stream_session_set_error (const char *reason)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, reason);
  return ER_STREAM_SESSION_ERROR;
}

#endif /* _INTERNAL_LOB_STREAM_KIND_H_ */
