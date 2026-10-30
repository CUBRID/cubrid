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
 * stream_handle.hpp - The handles a client holds between stream requests.
 *
 * One table per session state, next to the stream-session slot, for every
 * kind and both directions: a download's source, and an upload's staged result
 * that a later statement consumes. A handle resolves only in the session that
 * issued it, only for the kind it was issued for, and a STREAM_SCOPE_TRAN
 * handle only in the transaction that opened it.
 */

#ifndef _STREAM_HANDLE_HPP_
#define _STREAM_HANDLE_HPP_

#include "stream_session.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

/* A client that never releases gets a refused open, not unbounded server memory. */
const int STREAM_HANDLE_MAX_PER_TRAN = 1024;	/* STREAM_SCOPE_TRAN, per transaction */
const int STREAM_HANDLE_MAX_OWNER = 1024;	/* STREAM_SCOPE_OWNER, per session */
const std::int64_t STREAM_HANDLE_MAX_OWNER_BYTES = 16LL * 1024 * 1024 * 1024;

class stream_handle_table
{
  public:
    stream_handle_table ();
    ~stream_handle_table ();

    stream_handle_table (const stream_handle_table &) = delete;
    stream_handle_table &operator= (const stream_handle_table &) = delete;

    /* Take object over and return the number the client sends back. object is
     * closed on a refusal too, so the caller never keeps it. size counts against
     * the STREAM_SCOPE_OWNER byte limit. */
    int issue (THREAD_ENTRY *thread_p, int kind, int tran_index, std::int64_t size, stream_handle_object *object,
	       std::int64_t *handle);

    /* Pin a handle for one call; put () ends the pin. A download pin is
     * exclusive, because a reader is not thread-safe; an upload pin is shared.
     * NULL, with the transport-common error set, for a handle that does not
     * resolve, is being released or is pinned exclusively already. */
    stream_handle_object *get (std::int64_t handle, int kind, int tran_index);
    stream_source *get_source (std::int64_t handle, int tran_index);
    void put (THREAD_ENTRY *thread_p, std::int64_t handle);

    /* Release one handle, or every handle of a scope. A handle that does not
     * resolve is not an error -- a purge may have run first. A release during a
     * pin is deferred to the last put () and never blocks. */
    void release (THREAD_ENTRY *thread_p, std::int64_t handle);
    void end_tran (THREAD_ENTRY *thread_p, int tran_index);
    void clear (THREAD_ENTRY *thread_p);

  private:
    struct entry
    {
      int kind;
      int tran_index;
      stream_handle_scope scope;
      bool exclusive;
      bool counted;		/* against the limits; dropped as soon as a release is requested */
      bool release_pending;
      int pins;
      std::int64_t size;
      stream_handle_object *object;
    };

    using entry_map = std::map<std::int64_t, entry>;

    stream_handle_object *pin (std::int64_t handle, int kind, bool any_download, int tran_index);
    void uncount (entry &e);
    void release_locked (entry_map::iterator it, std::vector<stream_handle_object *> &to_close);
    static void close_all (THREAD_ENTRY *thread_p, std::vector<stream_handle_object *> &to_close);

    std::mutex m_mutex;
    entry_map m_entries;
    std::map<int, int> m_tran_counts;
    int m_owner_count;
    std::int64_t m_owner_bytes;

    /* readable without m_mutex, so a transaction end with nothing here -- the usual case -- takes no lock */
    std::atomic<int> m_size;
};

/* Open a download of kind. With first_max > 0 the first piece comes back in the
 * same call, and if it is the whole value and the client asked for
 * STREAM_FLAG_RELEASE_AT_EOF, no handle is issued (*handle is 0). buf holds
 * stream_read_bound (first_max) bytes. */
extern int stream_download_open (THREAD_ENTRY *thread_p, stream_handle_table &table, int kind, const char *config,
				 int config_len, int tran_index, int first_max, int flags, char *buf, int *nread,
				 int *reply_flags, std::int64_t *handle);

/* The next piece of an open download; buf holds stream_read_bound (max) bytes.
 * A read that fails releases its handle. */
extern int stream_download_read (THREAD_ENTRY *thread_p, stream_handle_table &table, std::int64_t handle,
				 int tran_index, int max, int flags, char *buf, int *nread, int *reply_flags);

/* What the core allocates for a read the client asked max bytes of. */
extern int stream_read_bound (int max);

#endif /* _STREAM_HANDLE_HPP_ */
