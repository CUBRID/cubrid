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
 * stream_handle.cpp - Handle table and download operations of the byte-stream transport.
 */

#include "stream_handle.hpp"

#include "error_code.h"
#include "error_manager.h"

#include <cassert>
#include <chrono>
#include <cstdint>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace
{
  /* One counter for the whole server, so that a handle kept across a reconnect
   * cannot resolve to what a new session issued under the same number. Seeded
   * from the clock in nanoseconds: a value comes back only after the 63-bit
   * counter wraps or after a restart whose clock is behind the previous one. */
  std::int64_t
  next_handle_value ()
  {
    static std::atomic<std::int64_t> counter (std::chrono::duration_cast<std::chrono::nanoseconds> (
		std::chrono::system_clock::now ().time_since_epoch ()).count ());
    std::int64_t value;

    do
      {
	value = counter.fetch_add (1) & INT64_MAX;
      }
    while (value == 0);

    return value;
  }

  int
  set_handle_error (int error_code, const char *what)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 1, what);
    return error_code;
  }

  void
  close_source (THREAD_ENTRY *thread_p, stream_source *source)
  {
    source->close (thread_p);
    delete source;
  }

  const char *const TOO_MANY_HANDLES = "too many stream handles open in this transaction";
}

stream_handle_table::stream_handle_table ()
  : m_mutex ()
  , m_entries ()
  , m_tran_counts ()
  , m_size (0)
{
}

stream_handle_table::~stream_handle_table ()
{
  /* closing needs a thread: the owner clears the table first */
  assert (m_entries.empty ());
}

int
stream_handle_table::issue (THREAD_ENTRY *thread_p, int kind, int tran_index, int client_id, stream_source *source,
			    std::int64_t *handle)
{
  const stream_kind_desc *desc = stream_kind_find (kind);
  int error_code = NO_ERROR;

  *handle = 0;

  {
    std::lock_guard<std::mutex> lock (m_mutex);

    if (desc == NULL || desc->direction != STREAM_DOWNLOAD)
      {
	error_code = set_handle_error (ER_STREAM_NOT_SUPPORTED, "unknown stream kind");
      }
    else if (m_tran_counts[tran_index] >= STREAM_HANDLE_MAX_PER_TRAN)
      {
	error_code = set_handle_error (ER_STREAM_INVALID_USE, TOO_MANY_HANDLES);
      }
    else
      {
	std::int64_t value;

	do
	  {
	    value = next_handle_value ();
	  }
	while (m_entries.find (value) != m_entries.end ());

	entry &e = m_entries[value];

	e.tran_index = tran_index;
	e.client_id = client_id;
	e.counted = true;
	e.release_pending = false;
	e.pins = 0;
	e.source = source;

	m_tran_counts[tran_index]++;
	m_size++;

	*handle = value;
	return NO_ERROR;
      }
  }

  close_source (thread_p, source);
  return error_code;
}

bool
stream_handle_table::has_room (int tran_index)
{
  std::lock_guard<std::mutex> lock (m_mutex);
  std::map<int, int>::const_iterator count = m_tran_counts.find (tran_index);

  return count == m_tran_counts.end () || count->second < STREAM_HANDLE_MAX_PER_TRAN;
}

stream_source *
stream_handle_table::get_source (std::int64_t handle, int tran_index, int *error_code)
{
  std::lock_guard<std::mutex> lock (m_mutex);
  entry_map::iterator it = m_entries.find (handle);

  if (it == m_entries.end () || it->second.tran_index != tran_index || it->second.release_pending)
    {
      *error_code = set_handle_error (ER_STREAM_NOT_OPEN, "unknown stream handle");
      return NULL;
    }

  /* a client that sends a request before the previous reply arrives: refused rather than serialized */
  if (it->second.pins > 0)
    {
      *error_code = set_handle_error (ER_STREAM_INVALID_USE, "stream handle is in use");
      return NULL;
    }

  it->second.pins++;
  return it->second.source;
}

void
stream_handle_table::put (THREAD_ENTRY *thread_p, std::int64_t handle)
{
  std::vector<stream_source *> to_close;

  {
    std::lock_guard<std::mutex> lock (m_mutex);
    entry_map::iterator it = m_entries.find (handle);

    /* a pinned entry is never erased under its pin */
    assert (it != m_entries.end () && it->second.pins > 0);
    if (it == m_entries.end ())
      {
	return;
      }

    if (--it->second.pins == 0 && it->second.release_pending)
      {
	release_locked (it, to_close);
      }
  }

  close_all (thread_p, to_close);
}

void
stream_handle_table::release (THREAD_ENTRY *thread_p, std::int64_t handle, int tran_index)
{
  std::vector<stream_source *> to_close;

  {
    std::lock_guard<std::mutex> lock (m_mutex);
    entry_map::iterator it = m_entries.find (handle);

    /* as get_source (): another connection or transaction of the session cannot end a download it did not open */
    if (it != m_entries.end () && it->second.tran_index == tran_index)
      {
	release_locked (it, to_close);
      }
  }

  close_all (thread_p, to_close);
}

void
stream_handle_table::end_tran (THREAD_ENTRY *thread_p, int tran_index)
{
  std::vector<stream_source *> to_close;

  if (m_size.load () == 0)
    {
      return;
    }

  {
    std::lock_guard<std::mutex> lock (m_mutex);

    for (entry_map::iterator it = m_entries.begin (); it != m_entries.end ();)
      {
	entry_map::iterator next = std::next (it);

	if (it->second.tran_index == tran_index)
	  {
	    release_locked (it, to_close);
	  }
	it = next;
      }
  }

  close_all (thread_p, to_close);
}

void
stream_handle_table::end_client (THREAD_ENTRY *thread_p, int client_id)
{
  std::vector<stream_source *> to_close;

  if (m_size.load () == 0)
    {
      return;
    }

  {
    std::lock_guard<std::mutex> lock (m_mutex);

    for (entry_map::iterator it = m_entries.begin (); it != m_entries.end ();)
      {
	entry_map::iterator next = std::next (it);

	/* by connection, not by tran_index: a late close must not reach a handle of a reused index */
	if (it->second.client_id == client_id)
	  {
	    release_locked (it, to_close);
	  }
	it = next;
      }
  }

  close_all (thread_p, to_close);
}

void
stream_handle_table::clear (THREAD_ENTRY *thread_p)
{
  std::vector<stream_source *> to_close;

  {
    std::lock_guard<std::mutex> lock (m_mutex);

    for (entry_map::iterator it = m_entries.begin (); it != m_entries.end ();)
      {
	entry_map::iterator next = std::next (it);

	release_locked (it, to_close);
	it = next;
      }
  }

  close_all (thread_p, to_close);
}

void
stream_handle_table::uncount (entry &e)
{
  if (!e.counted)
    {
      return;
    }
  e.counted = false;

  std::map<int, int>::iterator count = m_tran_counts.find (e.tran_index);

  if (count != m_tran_counts.end () && --count->second <= 0)
    {
      m_tran_counts.erase (count);
    }
}

/* the caller holds m_mutex; the source is closed by the caller after it lets the mutex go */
void
stream_handle_table::release_locked (entry_map::iterator it, std::vector<stream_source *> &to_close)
{
  /* off the limits at once, so that a reused tran_index is never charged for it */
  uncount (it->second);

  if (it->second.pins > 0)
    {
      it->second.release_pending = true;
      return;
    }

  to_close.push_back (it->second.source);
  m_entries.erase (it);
  m_size--;
}

/* outside the mutex: a consumer's close may take as long as it needs */
void
stream_handle_table::close_all (THREAD_ENTRY *thread_p, std::vector<stream_source *> &to_close)
{
  for (stream_source *source : to_close)
    {
      close_source (thread_p, source);
    }
}

int
stream_read_bound (int max)
{
  return max < STREAM_READ_MAX_CHUNK ? max : STREAM_READ_MAX_CHUNK;
}

/* one read() with the contract checked, so that a broken source cannot overrun buf or keep a client reading forever */
static int
stream_source_read_checked (THREAD_ENTRY *thread_p, stream_source *source, char *buf, int max, int *nread, bool *eof)
{
  int error_code;

  *nread = 0;
  *eof = false;

  error_code = source->read (thread_p, buf, max, nread, eof);
  if (error_code != NO_ERROR)
    {
      *nread = 0;
      return error_code;
    }

  if (*nread < 0 || *nread > max || (*nread == 0 && !*eof))
    {
      *nread = 0;
      return set_handle_error (ER_STREAM_SESSION_ERROR, "stream source broke its read contract");
    }

  return NO_ERROR;
}

/*
 * stream_download_open () - Open a download, with its first piece in the same call
 *   return: error code
 *   table(in): the session's handle table
 *   kind(in): STREAM_KIND_* taken off the wire, not yet trusted
 *   config(in), config_len(in): the kind's config blob
 *   tran_index(in): the transaction the handle belongs to
 *   client_id(in): the connection that opened it
 *   first_max(in): 0 to open only, or the bytes to read at once
 *   flags(in): STREAM_FLAG_RELEASE_AT_EOF
 *   buf(out): stream_read_bound (first_max) bytes
 *   nread(out), reply_flags(out): what the first read gave, STREAM_FLAG_EOF / STREAM_FLAG_RELEASED
 *   handle(out): the handle, or 0 when nothing is left open
 */
int
stream_download_open (THREAD_ENTRY *thread_p, stream_handle_table &table, int kind, const char *config,
		      int config_len, int tran_index, int client_id, int first_max, int flags, char *buf, int *nread,
		      int *reply_flags, std::int64_t *handle)
{
  stream_source *source;
  int error_code = NO_ERROR;
  bool eof = false;

  *nread = 0;
  *reply_flags = 0;
  *handle = 0;

  if (first_max < 0)
    {
      return set_handle_error (ER_STREAM_INVALID_USE, "negative read size");
    }
  if (!table.has_room (tran_index))
    {
      return set_handle_error (ER_STREAM_INVALID_USE, TOO_MANY_HANDLES);
    }

  source = stream_source_create (thread_p, kind, config, config_len, &error_code);
  if (source == NULL)
    {
      if (error_code == NO_ERROR)
	{
	  /* a factory lives outside the transport; without this the client would read "opened" */
	  assert (false);
	  error_code = set_handle_error (ER_STREAM_SESSION_ERROR, "stream source was not opened");
	}
      return error_code;
    }

  if (first_max > 0)
    {
      error_code = stream_source_read_checked (thread_p, source, buf, stream_read_bound (first_max), nread, &eof);
      if (error_code != NO_ERROR)
	{
	  close_source (thread_p, source);
	  return error_code;
	}

      if (eof)
	{
	  *reply_flags |= STREAM_FLAG_EOF;
	  if (flags & STREAM_FLAG_RELEASE_AT_EOF)
	    {
	      close_source (thread_p, source);
	      *reply_flags |= STREAM_FLAG_RELEASED;
	      return NO_ERROR;
	    }
	}
    }

  error_code = table.issue (thread_p, kind, tran_index, client_id, source, handle);
  if (error_code != NO_ERROR)
    {
      /* the bytes already read go with the refusal */
      *nread = 0;
      *reply_flags = 0;
    }

  return error_code;
}

/*
 * stream_download_read () - Read the next piece of an open download
 *   return: error code; a failed read of a resolved handle releases it and says so in reply_flags
 *   table(in): the session's handle table
 *   handle(in): what READ_OPEN returned
 *   tran_index(in): the transaction asking
 *   max(in): bytes the client can take
 *   flags(in): STREAM_FLAG_RELEASE_AT_EOF
 *   buf(out): stream_read_bound (max) bytes
 *   nread(out), reply_flags(out): STREAM_FLAG_EOF / STREAM_FLAG_RELEASED
 */
int
stream_download_read (THREAD_ENTRY *thread_p, stream_handle_table &table, std::int64_t handle, int tran_index,
		      int max, int flags, char *buf, int *nread, int *reply_flags)
{
  stream_source *source;
  int error_code;
  bool eof = false;

  *nread = 0;
  *reply_flags = 0;

  if (max <= 0)
    {
      return set_handle_error (ER_STREAM_INVALID_USE, "read size must be positive");
    }

  source = table.get_source (handle, tran_index, &error_code);
  if (source == NULL)
    {
      return error_code;
    }

  error_code = stream_source_read_checked (thread_p, source, buf, stream_read_bound (max), nread, &eof);
  if (error_code != NO_ERROR || (eof && (flags & STREAM_FLAG_RELEASE_AT_EOF)))
    {
      /* deferred to the put below, which closes the source; the client is told on a failure too */
      table.release (thread_p, handle, tran_index);
      *reply_flags |= STREAM_FLAG_RELEASED;
    }
  table.put (thread_p, handle);

  if (eof)
    {
      *reply_flags |= STREAM_FLAG_EOF;
    }

  return error_code;
}
