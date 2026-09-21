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

#include "config.h"

#include "internal_lob_upload.hpp"

#include "error_manager.h"
#include "heap_file.h"
#include "internal_lob_file.hpp"
#include "stream_session.hpp"
#include <algorithm>
#include <new>
#include <unistd.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

struct internal_lob_upload_reader_context
{
  FILE *spool;
  DB_BIGINT base;
};

static int
internal_lob_upload_file_reader (void *ctx, DB_BIGINT offset, char *buf, int size)
{
  internal_lob_upload_reader_context *reader = (internal_lob_upload_reader_context *) ctx;
  int fd;
  int total;

  if (reader == NULL || reader->spool == NULL || buf == NULL || size <= 0 || offset < 0)
    {
      return stream_session_set_error ("invalid internal LOB upload reader arguments");
    }

  /* A reused token can be read by several consume () calls at once on different threads: pread () takes an
   * explicit offset, not the FILE*'s shared position, so concurrent readers cannot move each other's seek. */
  fd = fileno (reader->spool);
  for (total = 0; total < size;)
    {
      ssize_t n = pread (fd, buf + total, (size_t) (size - total), (off_t) (reader->base + offset + total));

      if (n <= 0)
	{
	  return stream_session_set_error ("failed to read staged internal LOB upload");
	}
      total += (int) n;
    }

  return NO_ERROR;
}

internal_lob_upload_store::internal_lob_upload_store ()
  : m_next_token (1)
  , m_spool (NULL)
  , m_spool_end (0)
{
}

internal_lob_upload_store::~internal_lob_upload_store ()
{
  if (m_spool != NULL)
    {
      fclose (m_spool);
      m_spool = NULL;
    }
}

internal_lob_upload_store::payload_map::iterator
internal_lob_upload_store::erase (payload_map::iterator it)
{
  payload_map::iterator next;

  if (it->second.base + it->second.received == m_spool_end)
    {
      /* the most recent payload: its range can be written again by the next upload */
      m_spool_end = it->second.base;
    }
  next = m_payloads.erase (it);

  if (m_payloads.empty () && m_spool != NULL)
    {
      /* nothing is staged any more, and nothing reads the spool (a reader pins its payload through
       * use_count, so the map is not empty while one runs): give the space back */
      if (ftruncate (fileno (m_spool), 0) == 0)
	{
	  m_spool_end = 0;
	}
    }
  return next;
}

int
internal_lob_upload_store::begin (DB_TYPE type, DB_BIGINT data_length, DB_BIGINT logical_length, INT64 &token)
{
  payload entry;
  char error_message[192];

  token = 0;
  if ((type != DB_TYPE_BLOB && type != DB_TYPE_CLOB) || data_length < -1
      || data_length > DB_MAX_INTERNAL_LOB_LENGTH || logical_length < -1)
    {
      return stream_session_set_error ("invalid internal LOB upload metadata");
    }
  if ((data_length < 0) != (logical_length < 0)
      || (data_length >= 0 && type == DB_TYPE_CLOB && logical_length != data_length)
      || (data_length >= 0 && type == DB_TYPE_BLOB && ((logical_length + 7) / 8) != data_length))
    {
      snprintf (error_message, sizeof (error_message),
		"inconsistent internal LOB upload lengths (type=%d, data=%lld, logical=%lld)", (int) type,
		(long long) data_length, (long long) logical_length);
      return stream_session_set_error (error_message);
    }

  /* A new upload means the previous one can no longer be bound, so its staged range is released here. */
  purge_consumed ();

  std::lock_guard<std::mutex> guard (m_mutex);
  if (m_spool == NULL)
    {
      m_spool = tmpfile ();
      if (m_spool == NULL)
	{
	  return stream_session_set_error ("failed to create internal LOB upload staging file");
	}
      m_spool_end = 0;
    }

  entry.type = type;
  entry.data_length = data_length;
  entry.logical_length = logical_length;
  entry.base = m_spool_end;

  token = m_next_token++;
  try
    {
      m_payloads.emplace (token, entry);
    }
  catch (std::bad_alloc &)
    {
      /* Called from a C request handler: convert instead of letting the exception terminate the server. */
      token = 0;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (payload));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  return NO_ERROR;
}

int
internal_lob_upload_store::append (INT64 token, const char *data, int data_size)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);
  int written;

  if (token <= 0 || data_size < 0 || (data == NULL && data_size > 0) || found == m_payloads.end ()
      || m_spool == NULL || found->second.complete
      || found->second.received > DB_MAX_INTERNAL_LOB_LENGTH - data_size
      || (found->second.data_length >= 0 && found->second.received > found->second.data_length - data_size)
      || found->second.base + found->second.received != m_spool_end)
    {
      return stream_session_set_error ("invalid internal LOB upload chunk or token");
    }

  for (written = 0; written < data_size;)
    {
      ssize_t n = pwrite (fileno (m_spool), data + written, (size_t) (data_size - written),
			  (off_t) (m_spool_end + written));

      if (n <= 0)
	{
	  erase (found);
	  return stream_session_set_error ("failed to write internal LOB upload staging file");
	}
      written += (int) n;
    }
  found->second.received += data_size;
  m_spool_end += data_size;
  return NO_ERROR;
}

int
internal_lob_upload_store::end (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (token <= 0 || found == m_payloads.end () || m_spool == NULL
      || (found->second.data_length >= 0 && found->second.received != found->second.data_length))
    {
      return stream_session_set_error ("cannot finish incomplete internal LOB upload");
    }
  if (found->second.data_length < 0)
    {
      found->second.data_length = found->second.received;
      found->second.logical_length = (found->second.type == DB_TYPE_BLOB)
				     ? found->second.received * 8 : found->second.received;
    }

  found->second.complete = true;
  return NO_ERROR;
}

int
internal_lob_upload_store::abort (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (token <= 0 || found == m_payloads.end ())
    {
      return stream_session_set_error ("cannot abort unknown internal LOB upload token");
    }
  if (found->second.use_count > 0)
    {
      /* A consume () for this token is reading its range on another thread; same deferred-erase
       * reasoning as purge_consumed (). */
      found->second.pending_erase = true;
      return NO_ERROR;
    }
  erase (found);
  return NO_ERROR;
}

/*
 * note_executed () - A statement that bound this token has run.  Its payload is then released with the next upload
 *   like a stored one, even when the statement stored it in no row (an UPDATE that matched nothing).
 */
void
internal_lob_upload_store::note_executed (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (found != m_payloads.end () && found->second.complete)
    {
      found->second.consumed = true;
    }
}

int
internal_lob_upload_store::consume (THREAD_ENTRY *thread_p, INT64 token, const OID *class_oid, DB_TYPE expected_type,
				    INTERNAL_LOB_LOCATOR &locator)
{
  payload entry;
  internal_lob_upload_reader_context reader = { NULL, 0 };
  int error;

  {
    std::lock_guard<std::mutex> guard (m_mutex);
    auto found = m_payloads.find (token);
    if (token <= 0 || class_oid == NULL)
      {
	return stream_session_set_error ("invalid internal LOB upload consume arguments");
      }
    if (found == m_payloads.end ())
      {
	return stream_session_set_error ("internal LOB upload token does not belong to this session");
      }
    if (!found->second.complete || m_spool == NULL)
      {
	return stream_session_set_error ("internal LOB upload is not complete");
      }
    if (found->second.type != expected_type)
      {
	return stream_session_set_error ("internal LOB upload type does not match the target value");
      }
    /* The staged range stays: one token can be bound to several records of the same statement (UPDATE of
     * many rows, one value in two columns), and the reader addresses it by absolute offset. */
    entry = found->second;
    reader.spool = m_spool;
    reader.base = found->second.base;
    found->second.consumed = true;
    found->second.use_count++;
  }

  error = heap_internal_lob_insert_stream (thread_p, class_oid, internal_lob_upload_file_reader, &reader,
	  entry.data_length, expected_type == DB_TYPE_BLOB ? entry.logical_length : -1,
	  &locator);

  {
    std::lock_guard<std::mutex> guard (m_mutex);
    auto found = m_payloads.find (token);

    /* use_count kept this node pinned for the whole read above: a concurrent purge_consumed () or
     * abort () could only have set pending_erase, never erased its staged range while it was being read. */
    if (found != m_payloads.end ())
      {
	found->second.use_count--;
	if (found->second.use_count == 0 && found->second.pending_erase)
	  {
	    erase (found);
	  }
      }
  }

  return error;
}

void
internal_lob_upload_store::purge_consumed ()
{
  std::lock_guard<std::mutex> guard (m_mutex);

  for (auto it = m_payloads.begin (); it != m_payloads.end ();)
    {
      if (!it->second.consumed)
	{
	  ++it;
	  continue;
	}
      if (it->second.use_count > 0)
	{
	  /* A consume () for this (possibly reused) token is still reading its staged range on another thread;
	   * releasing the range now would let the next upload overwrite it, or empty the spool, under that
	   * reader.  Defer the erase to the moment that consume () finishes and finds use_count back at zero. */
	  it->second.pending_erase = true;
	  ++it;
	}
      else
	{
	  it = erase (it);
	}
    }

  /* erase () moves the spool end back only for the most recent payload; give back everything behind the last
   * payload still staged, so a payload kept for a later statement does not pin the space of released ones */
  DB_BIGINT staged_end = 0;
  for (const auto &entry : m_payloads)
    {
      staged_end = std::max (staged_end, entry.second.base + entry.second.received);
    }
  if (staged_end < m_spool_end)
    {
      m_spool_end = staged_end;
      if (m_spool != NULL)
	{
	  (void) ftruncate (fileno (m_spool), (off_t) staged_end);
	}
    }
}

