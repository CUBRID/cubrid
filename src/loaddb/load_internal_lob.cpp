/*
 * Copyright 2008 Search Solution Corporation
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
 * load_internal_lob.cpp - Internal LOB payload ring between the connection's request threads and a batch worker
 */

#include "load_internal_lob.hpp"

#include "error_manager.h"
#include "load_common.hpp"
#include "load_session.hpp"
#include "object_representation.h"
#include "session.h"

#include <cassert>
#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubload
{

  internal_lob_load_ring::internal_lob_load_ring (std::size_t slot_count, std::size_t slot_size)
    : m_mutex ()
    , m_not_full ()
    , m_not_empty ()
    , m_items (slot_count)
    , m_data (slot_count * slot_size)
    , m_slot_size (slot_size)
    , m_head (0)
    , m_count (0)
    , m_closed (false)
    , m_error (NO_ERROR)
  {
    assert (slot_count > 0 && slot_size > 0);
  }

  internal_lob_load_ring::~internal_lob_load_ring ()
  {
    /* a batch text nobody consumed belongs to nobody else */
    for (std::size_t i = 0; i < m_count; i++)
      {
	item &it = m_items[ (m_head + i) % m_items.size ()];

	if (it.kind == ITEM_BATCH)
	  {
	    delete it.batch_p;
	    it.batch_p = NULL;
	  }
      }
  }

  std::size_t
  internal_lob_load_ring::get_slot_size () const
  {
    return m_slot_size;
  }

  int
  internal_lob_load_ring::push (const item &header, const char *data)
  {
    if (header.data_size < 0 || (std::size_t) header.data_size > m_slot_size || (header.data_size > 0 && data == NULL))
      {
	return stream_session_set_error ("invalid internal LOB load ring item");
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
    m_not_full.wait (ulock, [this] () -> bool { return m_closed || m_count < m_items.size (); });
    if (m_closed)
      {
	/* the worker reported its own error on its own thread; the request thread needs a reason of its own */
	return stream_session_set_error ("internal LOB load batch no longer accepts payload");
      }

    std::size_t index = (m_head + m_count) % m_items.size ();
    m_items[index] = header;
    if (header.data_size > 0)
      {
	memcpy (&m_data[index * m_slot_size], data, (std::size_t) header.data_size);
      }
    ++m_count;
    ulock.unlock ();

    m_not_empty.notify_one ();
    return NO_ERROR;
  }

  bool
  internal_lob_load_ring::wait_front (item &header, const char *&data)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    m_not_empty.wait (ulock, [this] () -> bool { return m_closed || m_count > 0; });
    if (m_closed)
      {
	return false;
      }

    header = m_items[m_head];
    data = &m_data[m_head * m_slot_size];
    return true;
  }

  void
  internal_lob_load_ring::pop_front ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    assert (m_count > 0);
    if (m_count == 0)
      {
	return;
      }

    m_items[m_head].batch_p = NULL;	/* consumed: the batch text is the consumer's now */
    m_head = (m_head + 1) % m_items.size ();
    --m_count;
    ulock.unlock ();

    m_not_full.notify_one ();
  }

  void
  internal_lob_load_ring::close (int error)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    if (!m_closed)
      {
	m_closed = true;
	m_error = error;
      }
    ulock.unlock ();

    m_not_full.notify_all ();
    m_not_empty.notify_all ();
  }

  int
  internal_lob_load_ring::get_error ()
  {
    std::unique_lock<std::mutex> ulock (m_mutex);
    return m_error;
  }

  internal_lob_load_stream_session::internal_lob_load_stream_session (std::shared_ptr<internal_lob_load_ring> ring,
      INT64 slot)
    : m_ring (std::move (ring))
    , m_slot (slot)
    , m_active (true)
  {
  }

  internal_lob_load_stream_session::~internal_lob_load_stream_session ()
  {
  }

  int
  internal_lob_load_stream_session::receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len)
  {
    INT64 offset;
    DB_BIGINT end;
    const char *payload;
    std::size_t slot_size = m_ring->get_slot_size ();

    (void) thread_p;

    if (!m_active)
      {
	return stream_session_set_error ("internal LOB load stream is not active");
      }
    if (data == NULL || data_len < INTERNAL_LOB_LOAD_STREAM_FRAME_HEADER_SIZE)
      {
	return stream_session_set_error ("invalid internal LOB load stream frame");
      }

    OR_GET_INT64 (data, &offset);
    if (offset < 0 || offset > DB_BIGINT_MAX - data_len)
      {
	return stream_session_set_error ("invalid internal LOB load stream frame offset");
      }

    payload = data + INTERNAL_LOB_LOAD_STREAM_FRAME_HEADER_SIZE;
    end = (DB_BIGINT) offset + (data_len - INTERNAL_LOB_LOAD_STREAM_FRAME_HEADER_SIZE);

    /* cut the frame from its end: the reverse writer takes the highest offsets first */
    while (end > offset)
      {
	std::size_t piece = (end - offset) > (DB_BIGINT) slot_size ? slot_size : (std::size_t) (end - offset);
	DB_BIGINT start = end - (DB_BIGINT) piece;
	internal_lob_load_ring::item it;
	int error;

	it.kind = internal_lob_load_ring::ITEM_DATA;
	it.offset = start;
	it.data_size = (int) piece;
	error = m_ring->push (it, payload + (std::size_t) (start - offset));
	if (error != NO_ERROR)
	  {
	    return error;
	  }
	end = start;
      }

    return NO_ERROR;
  }

  int
  internal_lob_load_stream_session::finish (THREAD_ENTRY *thread_p, stream_result *result)
  {
    internal_lob_load_ring::item it;
    int error;

    (void) thread_p;

    if (!m_active || result == NULL)
      {
	return stream_session_set_error ("internal LOB load stream is not active");
      }

    it.kind = internal_lob_load_ring::ITEM_LOB_END;
    error = m_ring->push (it, NULL);
    if (error != NO_ERROR)
      {
	return error;
      }

    result->count = m_slot;
    m_active = false;
    return NO_ERROR;
  }

  void
  internal_lob_load_stream_session::abort (THREAD_ENTRY *thread_p)
  {
    (void) thread_p;

    if (m_active)
      {
	internal_lob_load_ring::item it;

	it.kind = internal_lob_load_ring::ITEM_LOB_ABORT;
	(void) m_ring->push (it, NULL);
	m_active = false;
      }
  }

} // namespace cubload

/*
 * internal_lob_load_stream_session_create () - Decode the loaddb payload config blob and open the value in the
 *                                              connection's load session.
 *   config(in): see INTERNAL_LOB_LOAD_STREAM_CONFIG_SIZE
 *   config_len(in): length of the config blob
 *   error_code(out): NO_ERROR or the failure code
 *   return: opened session on success, NULL on error
 *
 * The load session routes the payload to the batch's worker, see load_internal_lob.hpp.
 */
static stream_session *
internal_lob_load_stream_session_create (THREAD_ENTRY *thread_p, const char *config, int config_len, int *error_code)
{
  int type;
  INT64 data_length;
  INT64 logical_length;
  int clsid;
  INT64 id;
  load_session *load_session_p = NULL;
  stream_session *session = NULL;
  /* or_unpack_* take a mutable pointer although they only read through it */
  char *ptr = const_cast<char *> (config);

  if (config == NULL || config_len != INTERNAL_LOB_LOAD_STREAM_CONFIG_SIZE)
    {
      *error_code = stream_session_set_error ("invalid internal LOB load stream configuration");
      return NULL;
    }
  ptr = or_unpack_int (ptr, &type);
  OR_GET_INT64 (ptr, &data_length);
  ptr += OR_INT64_SIZE;
  OR_GET_INT64 (ptr, &logical_length);
  ptr += OR_INT64_SIZE;
  ptr = or_unpack_int (ptr, &clsid);
  OR_GET_INT64 (ptr, &id);
  if (type != INTERNAL_LOB_STREAM_TYPE_BLOB && type != INTERNAL_LOB_STREAM_TYPE_CLOB)
    {
      *error_code = stream_session_set_error ("invalid internal LOB load stream type");
      return NULL;
    }

  *error_code = session_get_load_session (thread_p, load_session_p);
  if (*error_code != NO_ERROR || load_session_p == NULL)
    {
      *error_code = stream_session_set_error ("no active load session");
      return NULL;
    }

  *error_code = load_session_p->internal_lob_stream_open (*thread_p, (cubload::class_id) clsid, (cubload::batch_id) id,
		type == INTERNAL_LOB_STREAM_TYPE_BLOB ? DB_TYPE_BLOB : DB_TYPE_CLOB,
		(DB_BIGINT) data_length, (DB_BIGINT) logical_length, session);
  return *error_code == NO_ERROR ? session : NULL;
}

/* The loaddb payload registers itself with the transport, as COPY does (copy_session.cpp). Runs at load time,
 * before any connection can open a session. */
namespace
{
  struct internal_lob_load_stream_session_registrar
  {
    internal_lob_load_stream_session_registrar ()
    {
      /* END hands back a slot for the batch text; the batch commits the rows, so END ends no unit of work */
      stream_session_register (STREAM_KIND_INTERNAL_LOB_LOAD, internal_lob_load_stream_session_create, false);
    }
  };

  internal_lob_load_stream_session_registrar internal_lob_load_stream_session_registrar_instance;
}
