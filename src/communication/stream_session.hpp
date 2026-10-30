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
 * stream_session.hpp - Server-side seams for the shared binary byte-stream transport.
 *
 * The transport (CAS function codes, CCI API, broker forwarding, client
 * chunking) carries opaque bytes between a client and a consumer on the server,
 * in either direction. An upload goes into the connection's single active
 * stream_session; a download comes out of a stream_source the client holds by
 * a handle. The kind of either is fixed when it is opened, and the handlers
 * route bytes through these seams without knowing it, so a new consumer is
 * added by implementing a seam, naming its own kind tag and registering it --
 * no transport change.
 */

#ifndef _STREAM_SESSION_HPP_
#define _STREAM_SESSION_HPP_

#include "thread_compat.hpp"

#include <cstdint>

/* Consumer kind tag carried on the wire by the open requests
 * (NET_SERVER_STREAM_INIT, NET_SERVER_STREAM_READ_OPEN). One registry serves
 * both directions, so a number names one consumer whatever its direction. The
 * tag values belong to the consumers -- each one names its own in its own header
 * and registers it -- so the transport names none of them and declares only the
 * bound the wire check and the registry need. */
enum STREAM_KIND
{
  STREAM_KIND_MIN = 0,

  /* Allocated so far -- a consumer takes the next free value and names it in
   * its own header, and this list is what stops two of them colliding:
   *   0  COPY                   (src/loaddb/copy_stream_kind.h)
   *   1  internal-LOB upload    (not landed; CBRD-26780)
   *   2  internal-LOB DML       (not landed; CBRD-26780)
   *   3  loaddb                 (not landed; CBRD-26780)
   *   4  internal-LOB read      (not landed; CBRD-26780)
   */
  STREAM_KIND_MAX = 5		/* registry slots; raise when a sixth consumer lands */
};

/* Flags of the bundled forms on the server requests (see the CAS_STREAM_FLAG_* masks for the driver wire). */
const int STREAM_FLAG_FINAL = 0x01;	/* upload request: the stream ends with this chunk */
const int STREAM_FLAG_RELEASE_AT_EOF = 0x01;	/* download request: release the handle when eof comes back */
const int STREAM_FLAG_EOF = 0x01;	/* download reply */
const int STREAM_FLAG_RELEASED = 0x02;	/* download reply: the core released the handle */

/* The core owns the buffer a download fills and bounds it here, whatever the client asked for. */
const int STREAM_READ_MAX_CHUNK = 1024 * 1024;

enum stream_direction
{
  STREAM_UPLOAD,
  STREAM_DOWNLOAD
};

/* How long the handles a kind issues live (stream_handle.hpp). */
enum stream_handle_scope
{
  STREAM_SCOPE_TRAN,		/* released at the end of the transaction that opened it */
  STREAM_SCOPE_OWNER		/* released by its owner, at the latest when the session ends */
};

/* Anything the client keeps between requests: a download's source, an upload's staged result. */
class stream_handle_object
{
  public:
    virtual ~stream_handle_object () {}

    /* Drop it. Called once: on release, at scope end, or at session teardown. */
    virtual void close (THREAD_ENTRY *thread_p) = 0;
};

/* What END reports back. A struct rather than a bare count so that a consumer
 * with more to report does not change every other one's signature to say it. */
struct stream_result
{
  std::int64_t count;		/* rows for COPY, bytes for a value stream */
  stream_handle_object *staged;	/* result_is_handle kinds: what END issues the client a handle for */
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

/* A download is a pull: the client asks for at most max bytes at a time. Seek
 * is not the transport's; a reader that starts at an offset carries it in its
 * config. */
class stream_source : public stream_handle_object
{
  public:
    /* Fill up to max bytes. nread is 0 only together with eof, and eof comes
     * back with the last bytes, so a reader of unknown length needs no extra call. */
    virtual int read (THREAD_ENTRY *thread_p, char *buf, int max, int *nread, bool *eof) = 0;
};

/* Build a session or a source of one kind from that kind's config blob. The
 * blob comes straight off the wire, so the factory decodes it bounded by
 * config_len. It starts with the kind's own version int: a factory accepts
 * every version up to its own and refuses a newer one naming the highest it
 * accepts, which is the only way a consumer grows without a new function code. */
using stream_session_factory = stream_session * (*) (THREAD_ENTRY *thread_p, const char *config, int config_len,
			       int *error_code);
using stream_source_factory = stream_source * (*) (THREAD_ENTRY *thread_p, const char *config, int config_len,
			      int *error_code);

/* The server half of a kind's registration. The client half -- who may open
 * the kind from a driver -- is registered in the client library
 * (stream_client.h), because privilege checks run there. */
struct stream_kind_desc
{
  stream_direction direction;
  stream_session_factory sink;	/* upload: builds the stream_session */
  stream_source_factory source;	/* download: builds the stream_source */

  /* Upload only. Does END finish the statement the bytes belong to? COPY's
   * does -- the transfer is the whole of it. A kind that only stages bytes for
   * a later statement does not, and committing at its END would commit that
   * statement's transaction early. Declared here rather than read off the wire,
   * because a client cannot claim a commit its consumer never promised. */
  bool ends_unit_of_work;

  /* Upload only: END issues a handle for stream_result::staged and returns it in the count. */
  bool result_is_handle;

  stream_handle_scope scope;	/* of the handles this kind issues */
};

/* A consumer registers its own kind once, at load time, before any connection
 * can open it. A record that does not hold together is refused and the kind
 * stays unregistered. */
extern int stream_kind_register (int kind, const stream_kind_desc &desc);

/* The record of a registered kind, or NULL for a tag the registry does not hold. */
extern const stream_kind_desc *stream_kind_find (int kind);

/* Open paths, called by the transport: dispatch to the registered factory of
 * that direction. A kind opened in the other direction is refused before its
 * factory runs. */
extern stream_session *stream_session_create (THREAD_ENTRY *thread_p, int kind, const char *config, int config_len,
    int *error_code);
extern stream_source *stream_source_create (THREAD_ENTRY *thread_p, int kind, const char *config, int config_len,
    int *error_code);

#endif /* _STREAM_SESSION_HPP_ */
