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
 * One table per session state, next to the stream-session slot, for the
 * downloads its client has open. A handle resolves only in the session that
 * issued it and only in the transaction that opened it, and is released at the
 * latest when that transaction ends.
 */

#ifndef _STREAM_HANDLE_HPP_
#define _STREAM_HANDLE_HPP_

#include "stream_session.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

/* A client that never releases gets a refused open, not unbounded server memory. */
const int STREAM_HANDLE_MAX_PER_TRAN = 1024;

class stream_handle_table
{
  public:
    stream_handle_table ();
    ~stream_handle_table ();

    stream_handle_table (const stream_handle_table &) = delete;
    stream_handle_table &operator= (const stream_handle_table &) = delete;

    /* Take a download's source over and return the number the client sends
     * back. source is closed on a refusal too, so the caller never keeps it.
     * client_id is the connection that asked (end_client). */
    int issue (THREAD_ENTRY *thread_p, int kind, int tran_index, int client_id, stream_source *source,
	       std::int64_t *handle);

    /* Start an open of tran_index, before its source is built; end_open ()
     * ends it, after issue () or the refusal. Refused, with the error set and
     * in error_code, when the transaction has no room for one more handle, so
     * that a refused open costs no factory; issue () still decides. */
    bool begin_open (THREAD_ENTRY *thread_p, int tran_index, int *error_code);
    void end_open (THREAD_ENTRY *thread_p);

    /* Pin a handle for one call; put () ends the pin. The pin is exclusive,
     * because a reader is not thread-safe. NULL, with the error set and in
     * error_code: ER_STREAM_NOT_OPEN for a handle that does not resolve or is
     * being released, ER_STREAM_INVALID_USE for one pinned already. */
    stream_source *get_source (THREAD_ENTRY *thread_p, std::int64_t handle, int tran_index, int *error_code);
    void put (THREAD_ENTRY *thread_p, std::int64_t handle);

    /* Release one handle of tran_index, or every handle of a transaction, of a
     * connection or of the session. A handle that does not resolve for
     * tran_index is not an error -- a purge may have run first. A release
     * during a pin is deferred to the last put () and never blocks.
     * end_tran () first waits for the opens and reads of the transaction that
     * other workers are running -- a client that does not wait for replies can
     * send the transaction's end behind them -- so that no source reads, and
     * no handle is issued, after its transaction is gone. end_client () is a
     * connection's close, which runs after the connection's workers. */
    void release (THREAD_ENTRY *thread_p, std::int64_t handle, int tran_index);
    void end_tran (THREAD_ENTRY *thread_p, int tran_index);
    void end_client (THREAD_ENTRY *thread_p, int client_id);
    void clear (THREAD_ENTRY *thread_p);

  private:
    struct entry
    {
      int tran_index;
      int client_id;
      bool counted;		/* against the limit; dropped as soon as a release is requested */
      bool release_pending;
      int pins;
      stream_source *source;
    };

    using entry_map = std::map<std::int64_t, entry>;

    /* an open (handle 0) or a read running on a worker, between begin_open () and end_open () or get_source () and
     * put () */
    struct request
    {
      int tran_index;
      THREAD_ENTRY *thread_p;
      std::int64_t handle;
    };

    void add_request_locked (int tran_index, THREAD_ENTRY *thread_p, std::int64_t handle);
    void remove_request_locked (THREAD_ENTRY *thread_p, std::int64_t handle);
    bool has_other_request_locked (int tran_index, THREAD_ENTRY *thread_p) const;
    void uncount (entry &e);
    void release_locked (entry_map::iterator it, std::vector<stream_source *> &to_close);
    static void close_all (THREAD_ENTRY *thread_p, std::vector<stream_source *> &to_close);

    std::mutex m_mutex;
    entry_map m_entries;
    std::map<int, int> m_tran_counts;
    std::vector<request> m_requests;
    std::condition_variable m_requests_done;

    /* readable without m_mutex, so a transaction end with nothing here -- the usual case -- takes no lock */
    std::atomic<int> m_size;
    std::atomic<int> m_request_count;
};

/* Open a download of kind. With first_max > 0 the first piece comes back in the
 * same call, and if it is the whole value and the client asked for
 * STREAM_FLAG_RELEASE_AT_EOF, no handle is issued (*handle is 0). buf holds
 * stream_read_bound (first_max) bytes. */
extern int stream_download_open (THREAD_ENTRY *thread_p, stream_handle_table &table, int kind, const char *config,
				 int config_len, int tran_index, int client_id, int first_max, int flags, char *buf,
				 int *nread, int *reply_flags, std::int64_t *handle);

/* The next piece of an open download; buf holds stream_read_bound (max) bytes.
 * A read that fails releases its handle, with STREAM_FLAG_RELEASED. */
extern int stream_download_read (THREAD_ENTRY *thread_p, stream_handle_table &table, std::int64_t handle,
				 int tran_index, int max, int flags, char *buf, int *nread, int *reply_flags);

/* What the core allocates for a read the client asked max bytes of. */
extern int stream_read_bound (int max);

#endif /* _STREAM_HANDLE_HPP_ */
