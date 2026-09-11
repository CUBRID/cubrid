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

  if (file == NULL || buf == NULL || size <= 0 || offset < 0)
    {
      return internal_lob_upload_set_error ("invalid internal LOB upload reader arguments");
    }

  if (fseeko (file, (off_t) offset, SEEK_SET) != 0 || fread (buf, 1, (size_t) size, file) != (size_t) size)
    {
      return internal_lob_upload_set_error ("failed to read staged internal LOB upload");
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
    entry = found->second;
    m_payloads.erase (found);
  }

  error = heap_internal_lob_insert_stream (thread_p, class_oid, internal_lob_upload_file_reader, entry.file,
	  entry.data_length, expected_type == DB_TYPE_BLOB ? entry.logical_length : -1,
	  &locator);
  clear (entry);
  return error;
}
