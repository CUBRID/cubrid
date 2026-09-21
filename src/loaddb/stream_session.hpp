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
 * stream_session.hpp - Server-side seam for the shared client->server binary byte-stream transport.
 *
 * The transport carries opaque bytes into the connection's single active stream session, whose kind is fixed by the
 * binding that opens it.  SEND_DATA / END route bytes without knowing the kind, so a new consumer needs only this
 * interface plus an open path -- no transport change.
 */

#ifndef _STREAM_SESSION_HPP_
#define _STREAM_SESSION_HPP_

#include "error_manager.h"
#include "object_representation.h"
#include "thread_compat.hpp"

#include <cstdint>

/* Consumer kind tag sent by the generic open path (NET_SERVER_STREAM_INIT); the server factory dispatches on it. */
enum STREAM_KIND
{
  STREAM_KIND_COPY = 0,
  STREAM_KIND_INTERNAL_LOB = 1,
  /* COPY-style Internal LOB DML: the session owns the target DML and reports affected rows at finish(); unlike
   * STREAM_KIND_INTERNAL_LOB it never returns an upload token. */
  STREAM_KIND_INTERNAL_LOB_DML = 2,
  /* loaddb (client/server) Internal LOB payload of one batch, written by the batch's load worker inside the batch
   * transaction; END returns the slot number the batch text refers to.  Frames arrive tail-first. */
  STREAM_KIND_INTERNAL_LOB_LOAD = 3
};

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

/* Result reported by finish(). COPY and DML-owning streams return affected
 * rows; STREAM_KIND_INTERNAL_LOB returns an upload token and
 * STREAM_KIND_INTERNAL_LOB_LOAD the batch-local slot number. */
struct stream_result
{
  std::int64_t count;
};

class stream_session
{
  public:
    virtual ~stream_session () {}

    /* Consume one opaque chunk. Any framing / chunk-boundary reassembly the
     * payload needs is the implementation's concern. */
    virtual int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) = 0;

    /* Flush pending work and report the binding's result. */
    virtual int finish (THREAD_ENTRY *thread_p, stream_result *result) = 0;

    /* Discard in-flight state so no partial result survives an error. */
    virtual void abort (THREAD_ENTRY *thread_p) = 0;
};

#endif /* _STREAM_SESSION_HPP_ */
