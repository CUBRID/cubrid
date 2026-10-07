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
 * stream_session.hpp - Server-side seam for the shared client->server
 *                      binary byte-stream transport.
 *
 * The transport (CAS function codes, CCI API, broker forwarding, client
 * chunking) carries opaque bytes into the connection's single active stream
 * session. The session kind is fixed when it is opened by a binding (COPY
 * opens a copy_session; internal-LOB will open a lob_input_session). The
 * SEND_DATA / END handlers route bytes through this seam without knowing the
 * kind, so a new consumer is added by implementing this interface, naming its
 * own kind tag and registering a factory for it -- no transport change.
 */

#ifndef _STREAM_SESSION_HPP_
#define _STREAM_SESSION_HPP_

#include "thread_compat.hpp"

#include <cstdint>

/* Consumer kind tag carried on the wire by the generic open path
 * (NET_SERVER_STREAM_INIT). The server factory dispatches on this to build the
 * matching session. The tag values belong to the consumers -- each one names its
 * own in its own header and registers a factory for it -- so this branch, which
 * carries the transport alone, names none of them and declares only the bound
 * the wire check and the factory table need. */
enum STREAM_KIND
{
  STREAM_KIND_MIN = 0,

  /* Allocated so far -- a consumer takes the next free value and names it in
   * its own header, and this list is what stops two of them colliding:
   *   0  COPY               (src/loaddb/copy_stream_kind.h)
   *   1  internal-LOB upload    (not landed; CBRD-26780)
   *   2  internal-LOB DML       (not landed; CBRD-26780)
   */
  STREAM_KIND_MAX = 4		/* factory slots; raise when a fifth consumer lands */
};

/* What END reports back. A struct rather than a bare count because the second
 * consumer is being written against this shape (CBRD-26780) and because a
 * consumer that has more to report should not change every other one's
 * signature to say it. */
struct stream_result
{
  std::int64_t count;		/* rows for COPY, bytes for a value stream */
};

class stream_session
{
  public:
    virtual ~stream_session () {}

    /* Consume one opaque chunk. Any framing / chunk-boundary reassembly the
     * payload needs is the implementation's concern. */
    virtual int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) = 0;

    /* Flush pending work and report the binding's result: rows_loaded for COPY,
     * bytes written for internal-LOB. The count is 64-bit so a 4GB LOB value fits. */
    virtual int finish (THREAD_ENTRY *thread_p, stream_result *result) = 0;

    /* Discard in-flight state so no partial result survives an error. */
    virtual void abort (THREAD_ENTRY *thread_p) = 0;
};

/* Build a session of one kind from that kind's config blob. The blob comes
 * straight off the wire, so the factory decodes it bounded by config_len. */
using stream_session_factory = stream_session * (*) (THREAD_ENTRY *thread_p, const char *config, int config_len,
			       int *error_code);

/* A consumer registers the factory for its own kind; the transport dispatches
 * through the table and never names a concrete session type. Registration
 * happens once, at load time, before any connection can open a session.
 *
 * ends_unit_of_work says whether END finishes the statement the bytes belong
 * to. COPY's does -- the transfer is the whole of it. A stream that only stages
 * bytes for a later statement to consume (internal-LOB upload) does not, and
 * committing at its END would commit that later statement's transaction early.
 * It is declared here, once per consumer, rather than read off the wire,
 * because the answer is the consumer's and not the client's to assert. */
extern void stream_session_register (int kind, stream_session_factory factory, bool ends_unit_of_work);

/* Does a stream of this kind finish a unit of work? Answered for an unknown or
 * unregistered kind with false: nothing was opened, so nothing is owed. */
extern bool stream_session_kind_ends_unit_of_work (int kind);

/* Open path, called by the transport: dispatch to the registered factory. */
extern stream_session *stream_session_create (THREAD_ENTRY *thread_p, int kind, const char *config, int config_len,
    int *error_code);

#endif /* _STREAM_SESSION_HPP_ */
