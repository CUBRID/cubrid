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
#include <new>
#include <unistd.h>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

static int
internal_lob_upload_set_error (const char *reason)
{
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, reason);
  return ER_STREAM_SESSION_ERROR;
}

static int
internal_lob_upload_file_reader (void *ctx, DB_BIGINT offset, char *buf, int size)
{
  FILE *file = (FILE *) ctx;
  int fd;
  int total;

  if (file == NULL || buf == NULL || size <= 0 || offset < 0)
    {
      return internal_lob_upload_set_error ("invalid internal LOB upload reader arguments");
    }

  /* A reused token (the same staged upload bound to more than one row) can be read by more than one
   * consume () call at once, on different threads.  pread () addresses the file by an explicit offset
   * instead of the FILE*'s shared position, so concurrent readers of the same fd cannot make each other
   * seek out from under themselves the way a fseeko ()+fread () pair would. */
  fd = fileno (file);
  for (total = 0; total < size;)
    {
      ssize_t n = pread (fd, buf + total, (size_t) (size - total), (off_t) (offset + total));

      if (n <= 0)
	{
	  return internal_lob_upload_set_error ("failed to read staged internal LOB upload");
	}
      total += (int) n;
    }

  return NO_ERROR;
}

internal_lob_upload_store::internal_lob_upload_store ()
  : m_next_token (1)
{
}

internal_lob_upload_store::~internal_lob_upload_store ()
{
  for (auto &entry : m_payloads)
    {
      clear (entry.second);
    }
}

void
internal_lob_upload_store::clear (payload &entry)
{
  if (entry.file != NULL)
    {
      fclose (entry.file);
      entry.file = NULL;
    }
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
      return internal_lob_upload_set_error ("invalid internal LOB upload metadata");
    }
  if ((data_length < 0) != (logical_length < 0)
      || (data_length >= 0 && type == DB_TYPE_CLOB && logical_length != data_length)
      || (data_length >= 0 && type == DB_TYPE_BLOB && ((logical_length + 7) / 8) != data_length))
    {
      snprintf (error_message, sizeof (error_message),
		"inconsistent internal LOB upload lengths (type=%d, data=%lld, logical=%lld)", (int) type,
		(long long) data_length, (long long) logical_length);
      return internal_lob_upload_set_error (error_message);
    }

  /* A new upload means the previous one can no longer be bound, so its staging file is released here.  This
   * keeps a row-at-a-time loader (which stages one payload per row) down to a single open descriptor. */
  purge_consumed ();

  entry.file = tmpfile ();
  if (entry.file == NULL)
    {
      return internal_lob_upload_set_error ("failed to create internal LOB upload staging file");
    }
  entry.type = type;
  entry.data_length = data_length;
  entry.logical_length = logical_length;

  std::lock_guard<std::mutex> guard (m_mutex);
  token = m_next_token++;
  try
    {
      m_payloads.emplace (token, entry);
    }
  catch (std::bad_alloc &)
    {
      /* Called from a C request handler: convert instead of letting the exception terminate the server. */
      clear (entry);
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

  if (token <= 0 || data_size < 0 || (data == NULL && data_size > 0) || found == m_payloads.end ()
      || found->second.file == NULL || found->second.complete
      || found->second.received > DB_MAX_INTERNAL_LOB_LENGTH - data_size
      || (found->second.data_length >= 0 && found->second.received > found->second.data_length - data_size))
    {
      return internal_lob_upload_set_error ("invalid internal LOB upload chunk or token");
    }

  if (data_size > 0 && fwrite (data, 1, (size_t) data_size, found->second.file) != (size_t) data_size)
    {
      clear (found->second);
      m_payloads.erase (found);
      return internal_lob_upload_set_error ("failed to write internal LOB upload staging file");
    }
  found->second.received += data_size;
  return NO_ERROR;
}

int
internal_lob_upload_store::end (INT64 token)
{
  std::lock_guard<std::mutex> guard (m_mutex);
  auto found = m_payloads.find (token);

  if (token <= 0 || found == m_payloads.end () || found->second.file == NULL
      || (found->second.data_length >= 0 && found->second.received != found->second.data_length))
    {
      return internal_lob_upload_set_error ("cannot finish incomplete internal LOB upload");
    }
  if (found->second.data_length < 0)
    {
      found->second.data_length = found->second.received;
      found->second.logical_length = (found->second.type == DB_TYPE_BLOB)
				     ? found->second.received * 8 : found->second.received;
    }
  if (fflush (found->second.file) != 0 || fseek (found->second.file, 0, SEEK_SET) != 0)
    {
      return internal_lob_upload_set_error ("failed to rewind internal LOB upload staging file");
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
      return internal_lob_upload_set_error ("cannot abort unknown internal LOB upload token");
    }
  if (found->second.use_count > 0)
    {
      /* A consume () for this token is reading entry.file on another thread; same deferred-erase
       * reasoning as purge_consumed (). */
      found->second.pending_erase = true;
      return NO_ERROR;
    }
  clear (found->second);
  m_payloads.erase (found);
  return NO_ERROR;
}

int
internal_lob_upload_store::consume (THREAD_ENTRY *thread_p, INT64 token, const OID *class_oid, DB_TYPE expected_type,
				    INTERNAL_LOB_LOCATOR &locator)
{
  payload entry;
  int error;

  {
    std::lock_guard<std::mutex> guard (m_mutex);
    auto found = m_payloads.find (token);
    if (token <= 0 || class_oid == NULL)
      {
	return internal_lob_upload_set_error ("invalid internal LOB upload consume arguments");
      }
    if (found == m_payloads.end ())
      {
	return internal_lob_upload_set_error ("internal LOB upload token does not belong to this session");
      }
    if (!found->second.complete || found->second.file == NULL)
      {
	return internal_lob_upload_set_error ("internal LOB upload is not complete");
      }
    if (found->second.type != expected_type)
      {
	return internal_lob_upload_set_error ("internal LOB upload type does not match the target value");
      }
    /* The staged file stays: one token can be bound to several records of the same statement (UPDATE matching
     * many rows, or the same value written to two columns), and the reader addresses it by absolute offset, so
     * storing it again needs nothing but the descriptor.  Erasing it here made the second record fail with
     * "token does not belong to this session" after the first had already been written. */
    entry = found->second;
    found->second.consumed = true;
    found->second.use_count++;
  }

  error = heap_internal_lob_insert_stream (thread_p, class_oid, internal_lob_upload_file_reader, entry.file,
	  entry.data_length, expected_type == DB_TYPE_BLOB ? entry.logical_length : -1,
	  &locator);

  {
    std::lock_guard<std::mutex> guard (m_mutex);
    auto found = m_payloads.find (token);

    /* use_count kept this node pinned for the whole read above: a concurrent purge_consumed () or
     * abort () could only have set pending_erase, never erased it out from under entry.file. */
    if (found != m_payloads.end ())
      {
	found->second.use_count--;
	if (found->second.use_count == 0 && found->second.pending_erase)
	  {
	    clear (found->second);
	    m_payloads.erase (found);
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
	  /* A consume () for this (possibly reused) token is still reading entry.file on another thread;
	   * closing it here would be a use-after-free.  Defer the erase to the moment that consume ()
	   * finishes and finds use_count back at zero. */
	  it->second.pending_erase = true;
	  ++it;
	}
      else
	{
	  clear (it->second);
	  it = m_payloads.erase (it);
	}
    }
}

