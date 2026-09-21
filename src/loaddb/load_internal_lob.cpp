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
#include "object_representation.h"

#include <cassert>
#include <cstring>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubload
{

  static int
  internal_lob_load_set_error (const char *reason)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_SESSION_ERROR, 1, reason);
    return ER_STREAM_SESSION_ERROR;
  }

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
	return internal_lob_load_set_error ("invalid internal LOB load ring item");
      }

    std::unique_lock<std::mutex> ulock (m_mutex);
    m_not_full.wait (ulock, [this] () -> bool { return m_closed || m_count < m_items.size (); });
    if (m_closed)
      {
	/* the worker reported its own error on its own thread; the request thread needs a reason of its own */
	return internal_lob_load_set_error ("internal LOB load batch no longer accepts payload");
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
	return internal_lob_load_set_error ("internal LOB load stream is not active");
      }
    if (data == NULL || data_len < INTERNAL_LOB_LOAD_STREAM_FRAME_HEADER_SIZE)
      {
	return internal_lob_load_set_error ("invalid internal LOB load stream frame");
      }

    OR_GET_INT64 (data, &offset);
    if (offset < 0 || offset > DB_BIGINT_MAX - data_len)
      {
	return internal_lob_load_set_error ("invalid internal LOB load stream frame offset");
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
	return internal_lob_load_set_error ("internal LOB load stream is not active");
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
