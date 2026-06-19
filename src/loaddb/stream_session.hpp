/*
 * Copyright 2024 CUBRID Corporation
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
 * kind, so a new consumer is added by implementing this interface plus an
 * open path -- no transport change.
 */

#ifndef _STREAM_SESSION_HPP_
#define _STREAM_SESSION_HPP_

#include "thread_compat.hpp"

#include <cstdint>

/* Result reported by finish(). The 64-bit count is interpreted by the binding:
 * rows_loaded for COPY, bytes written for internal-LOB. 64-bit so a 4GB LOB
 * value fits (CBRD-26780 wire length widening). */
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
