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
 * load_internal_lob.hpp - Internal LOB payload ring between the connection's request threads and a batch worker
 */

#ifndef _LOAD_INTERNAL_LOB_HPP_
#define _LOAD_INTERNAL_LOB_HPP_

#include "dbtype_def.h"
#include "stream_session.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace cubload
{
  class batch;

  /*
   * cubload::internal_lob_load_ring
   *
   * description
   *    Bounded ring of fixed-size slots between the request threads of a loaddb connection (producers) and the
   *    worker of one batch (consumer).  A request thread cuts every Internal LOB frame it receives into
   *    slot-sized pieces, from the end of the frame toward its start, and pushes them; the worker pops them in
   *    that same order and hands each one to the reverse chunk writer inside the batch transaction, so the
   *    payload never touches a staging file and the chain belongs to the transaction that inserts the rows.
   *    Control items (value begin/end/abort, the batch text) travel through the same ring, which keeps them
   *    ordered with the payload.
   *
   *    A full ring blocks the producer and an empty ring blocks the consumer; close () wakes both and fails
   *    every later push, so neither side can be left waiting for the other once the batch is over.
   */
  class internal_lob_load_ring
  {
    public:
      enum item_kind
      {
	ITEM_DATA,		/* payload piece: data_size bytes at offset */
	ITEM_LOB_BEGIN,		/* a value starts: lob_type, data_length, logical_length */
	ITEM_LOB_END,		/* the value's payload is complete */
	ITEM_LOB_ABORT,		/* the client gave up on the value */
	ITEM_BATCH		/* the batch text; ownership of batch_p moves to the consumer */
      };

      struct item
      {
	item_kind kind = ITEM_DATA;
	DB_TYPE lob_type = DB_TYPE_NULL;
	DB_BIGINT data_length = 0;
	DB_BIGINT logical_length = 0;
	DB_BIGINT offset = 0;
	int data_size = 0;
	const batch *batch_p = NULL;
      };

      internal_lob_load_ring (std::size_t slot_count, std::size_t slot_size);	/* may throw std::bad_alloc */
      ~internal_lob_load_ring ();

      internal_lob_load_ring (const internal_lob_load_ring &) = delete;
      internal_lob_load_ring &operator= (const internal_lob_load_ring &) = delete;

      std::size_t get_slot_size () const;

      /* producer: waits for a free slot; ITEM_DATA carries header.data_size bytes of data */
      int push (const item &header, const char *data);

      /* consumer: waits for the oldest item; data points into the ring until pop_front ().  false once closed. */
      bool wait_front (item &header, const char *&data);
      void pop_front ();

      /* end the exchange: wakes both sides, every later push fails */
      void close (int error);
      int get_error ();

    private:
      std::mutex m_mutex;
      std::condition_variable m_not_full;
      std::condition_variable m_not_empty;
      std::vector<item> m_items;
      std::vector<char> m_data;
      std::size_t m_slot_size;
      std::size_t m_head;
      std::size_t m_count;
      bool m_closed;
      int m_error;
  };

  /*
   * cubload::internal_lob_load_stream_session
   *
   * description
   *    STREAM_KIND_INTERNAL_LOB_LOAD binding: the connection-side end of one value's payload.  It only re-frames
   *    the bytes into the batch ring; nothing is written or staged on the request thread.  finish () reports
   *    the batch-local slot number the batch text refers to.
   */
  class internal_lob_load_stream_session : public stream_session
  {
    public:
      internal_lob_load_stream_session (std::shared_ptr<internal_lob_load_ring> ring, INT64 slot);
      ~internal_lob_load_stream_session () override;

      int receive_chunk (THREAD_ENTRY *thread_p, const char *data, int data_len) override;
      int finish (THREAD_ENTRY *thread_p, stream_result *result) override;
      void abort (THREAD_ENTRY *thread_p) override;

    private:
      std::shared_ptr<internal_lob_load_ring> m_ring;
      INT64 m_slot;
      bool m_active;
  };

} // namespace cubload

#endif /* _LOAD_INTERNAL_LOB_HPP_ */
