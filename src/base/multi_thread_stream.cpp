/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * multi_thread_stream.cpp
 */

#ident "$Id$"

#include "multi_thread_stream.hpp"

#include "error_code.h"
#include "error_manager.h"

namespace cubstream
{
  multi_thread_stream::multi_thread_stream (const size_t buffer_capacity, const int max_appenders)
    : m_bip_buffer (buffer_capacity),
      m_reserved_positions (max_appenders)
  {
    m_oldest_readable_position = 0;

    /* TODO : system parameter */
    m_trigger_flush_to_disk_size = buffer_capacity / 2;

    m_trigger_min_to_read_size = 16;

    m_stat_reserve_queue_spins = 0;
    m_stat_reserve_buffer_spins = 0;
    m_stat_read_not_enough_data_cnt = 0;
    m_stat_read_not_in_buffer_cnt = 0;
    m_stat_read_no_readable_pos_cnt = 0;
    m_stat_read_buffer_failed_cnt = 0;

    m_is_stopped = false;
  }

  multi_thread_stream::~multi_thread_stream ()
  {
    assert (m_append_position - m_read_position == 0);
  }

  /*
   * write API for stream:
   * 1. reserve position (with pointer in bip_buffer)
   * 2. call write action
   * 3. commit the reserve position
   *  the write_function is expected to return error code (negative value) or number of written bytes (positive)
   */
  int multi_thread_stream::write (const size_t byte_count, write_func_t &write_action)
  {
    int err = NO_ERROR;
    stream_reserve_context *reserve_context = NULL;
    char *ptr;
    stream_position reserved_pos;
    int written_bytes;

    ptr = reserve_with_buffer (byte_count, reserve_context);
    if (ptr == NULL)
      {
	err = ER_FAILED;
	return err;
      }
    reserved_pos = reserve_context->start_pos;

    written_bytes = write_action (reserved_pos, ptr, byte_count);

    reserve_context->written_bytes = written_bytes;

    err = commit_append (reserve_context);

    if (written_bytes < 0)
      {
	err = ER_FAILED;
      }

    return (err < 0) ? err : written_bytes;
  }

  /*
   * read API:
   * 1. acquire pointer to data range
   * 2. if actual range is same as requested continue with (7)
   * 3. (actual range < requested range) allocate new local memory buffer for requested amount
   * 4. copy partial range to local buffer
   * 5. acquire pointer to rest of data
   * 6. copy the remaining range to continuation of local buffer
   * 7. execute read action using local buffer or initial provided pointer
   * 8. release latch read (in case local buffer not used)
   */
  int multi_thread_stream::read (const stream_position first_pos, const size_t byte_count, read_func_t &read_action)
  {
    int err = NO_ERROR;
    char *ptr;
    size_t actual_read_bytes = 0;
    char *local_buffer = NULL;
    cubmem::buffer_latch_read_id read_latch_page_idx;
    int read_bytes;

    ptr = get_data_from_pos (first_pos, byte_count, actual_read_bytes, read_latch_page_idx);
    if (ptr == NULL)
      {
	err = ER_FAILED;
	return err;
      }

    /* the start of requested stream position may be located at the end of bip_buffer, use local buffer */
    if (actual_read_bytes < byte_count)
      {
	stream_position next_pos = first_pos + actual_read_bytes;
	size_t next_actual_read_bytes = 0;
	local_buffer = new char[byte_count];

	memcpy (local_buffer, ptr, actual_read_bytes);
	unlatch_read_data (read_latch_page_idx);

	ptr = get_data_from_pos (next_pos, byte_count - actual_read_bytes, next_actual_read_bytes,
				 read_latch_page_idx);
	if (ptr == NULL || next_actual_read_bytes != byte_count - actual_read_bytes)
	  {
	    err = ER_FAILED;

	    if (ptr != NULL)
	      {
		unlatch_read_data (read_latch_page_idx);
	      }

	    delete [] local_buffer;
	    return err;
	  }

	assert (next_actual_read_bytes + actual_read_bytes == byte_count);

	memcpy (local_buffer + actual_read_bytes, ptr, next_actual_read_bytes);

	unlatch_read_data (read_latch_page_idx);

	ptr = local_buffer;
      }

    read_bytes = read_action (ptr, byte_count);

    if (ptr != local_buffer)
      {
	err = unlatch_read_data (read_latch_page_idx);
      }
    else
      {
	delete [] local_buffer;
      }

    return (err < 0) ? err : read_bytes;
  }

  /*
   * read_serial API:
   * 1. wait for data to be committed for the whole requested range
   * 2. acquire pointer to data range
   * 3. if actual range is same as requested continue with (8)
   * 4. (actual range < requested range) allocate new local memory buffer for requested amount
   * 5. copy partial range to local buffer
   * 6. acquire pointer to rest of data
   * 7. copy the remaining range to continuation of local buffer
   * 8. execute read_prepare_action action using local buffer or initial provided pointer
   *    this is expected to read a header of data and return the size of payload
   * 9. release latch read (in case local buffer not used)
   * 10. wait for an amount of payload of data to be produced (committed)
   */
  int multi_thread_stream::read_serial (const size_t amount, read_prepare_func_t &read_prepare_action)
  {
    int err = NO_ERROR;
    char *ptr;
    size_t actual_read_bytes = 0;
    char *local_buffer = NULL;
    cubmem::buffer_latch_read_id read_latch_page_idx;
    stream_position to_read_pos;
    stream_position trail_pos;
    size_t payload_size;
    int read_bytes;

    /* wait for stream to receive data */
    if (m_read_position + amount > m_last_committed_pos)
      {
	err = wait_for_data (amount, STREAM_DONT_SKIP);

	/* fetch is expect to return NO_ERROR upon completion of fetch with success */
	if (err != NO_ERROR)
	  {
	    return err;
	  }
      }

    to_read_pos = m_read_position;

    ptr = get_data_from_pos (to_read_pos, amount, actual_read_bytes, read_latch_page_idx);
    if (ptr == NULL)
      {
	err = ER_FAILED;
	return err;
      }

    /* the start of requested stream position may be located at the end of bip_buffer, use local buffer */
    if (actual_read_bytes < amount)
      {
	stream_position next_pos = to_read_pos + actual_read_bytes;
	size_t next_actual_read_bytes = 0;
	local_buffer = new char[amount];

	memcpy (local_buffer, ptr, actual_read_bytes);
	unlatch_read_data (read_latch_page_idx);

	ptr = get_data_from_pos (next_pos, amount - actual_read_bytes, next_actual_read_bytes,
				 read_latch_page_idx);
	if (ptr == NULL || next_actual_read_bytes != amount - actual_read_bytes)
	  {
	    err = ER_FAILED;

	    if (ptr != NULL)
	      {
		unlatch_read_data (read_latch_page_idx);
	      }

	    delete [] local_buffer;
	    return err;
	  }

	assert (next_actual_read_bytes + actual_read_bytes == amount);

	memcpy (local_buffer + actual_read_bytes, ptr, next_actual_read_bytes);

	unlatch_read_data (read_latch_page_idx);

	ptr = local_buffer;
      }

    trail_pos = to_read_pos + amount;
    read_bytes = read_prepare_action (trail_pos, ptr, amount, payload_size);

    if (ptr != local_buffer)
      {
	unlatch_read_data (read_latch_page_idx);
      }
    else
      {
	delete [] local_buffer;
      }

    err = wait_for_data (amount + payload_size, STREAM_SKIP);

    return (err < 0) ? err : read_bytes;
  }

  int multi_thread_stream::read_partial (const stream_position first_pos, const size_t byte_count,
					 size_t &actual_read_bytes,
					 read_partial_func_t &read_partial_action)
  {
    int err = NO_ERROR;
    char *ptr;
    size_t contiguous_bytes_in_buffer = 0;
    cubmem::buffer_latch_read_id read_latch_page_idx;
    int read_bytes;

    ptr = get_data_from_pos (first_pos, byte_count, contiguous_bytes_in_buffer, read_latch_page_idx);
    if (ptr == NULL)
      {
	err = ER_FAILED;
	return err;
      }

    read_bytes = read_partial_action (ptr, contiguous_bytes_in_buffer, actual_read_bytes);

    err = unlatch_read_data (read_latch_page_idx);

    return (err < 0) ? err : read_bytes;
  }

  /*
   * Commit phase of a stream append (all operations are protected by same mutex) :
   *  1. queue->consume : marks the reserved context as completed and if the context is in the head
   *     of queue, it collapses all elements until a non-completed context is reached (or the queue is emptied);
   *  2. (only if queue is collapsed) buffer->commit : advances the commit pointer of the bip_buffer
   *  3. notifies the m_ready_pos_handler handler that new amount is ready to be read
   *     (only if threshold is reached compared to previous notified position)
   */
  int multi_thread_stream::commit_append (stream_reserve_context *reserve_context)
  {
    stream_reserve_context *last_used_context = NULL;
    bool collapsed_reserve;
    char *ptr_commit;
    int err = NO_ERROR;
    stream_position new_completed_position = reserve_context->start_pos + reserve_context->reserved_amount;
    bool signal_data_ready = false;

    std::unique_lock<std::mutex> ulock (m_buffer_mutex);

    collapsed_reserve = m_reserved_positions.consume (reserve_context, last_used_context);
    if (collapsed_reserve)
      {
	assert (last_used_context != NULL);

	assert (new_completed_position <= last_used_context->start_pos + last_used_context->reserved_amount);
	new_completed_position = last_used_context->start_pos + last_used_context->reserved_amount;

	ptr_commit = last_used_context->ptr + last_used_context->reserved_amount;
	m_bip_buffer.commit (ptr_commit);

	assert (new_completed_position > m_last_committed_pos);
	m_last_committed_pos = new_completed_position;
      }
    if (m_last_committed_pos >= m_serial_read_wait_pos)
      {
	signal_data_ready = true;
      }

    ulock.unlock ();

    if (signal_data_ready)
      {
	m_serial_read_cv.notify_one ();
      }

    /* notify readers of the new completed position */
    if (m_ready_pos_handler && new_completed_position > m_last_notified_committed_pos + m_trigger_min_to_read_size)
      {
	stream_position save_last_notified_commited_pos = m_last_notified_committed_pos;
	size_t committed_bytes = new_completed_position - m_last_notified_committed_pos;

	err = m_ready_pos_handler (save_last_notified_commited_pos, committed_bytes);
      }

    return err;
  }

  /*
   * Reserve phase of append:
   *  1. queue->produce : tries adds slot in queue (further data is filled later);
   *     it repeats this steps until successful
   *  2. buffer->reserve : tries to reserve the amount in buffer;
   *     if unsuccessful, reverts the first step (still under mutex), release the mutex and restarts from (1)
   *     (this allows other threads to unlatch reads, advance commit pointer - to unblock the situation)
   *  3. increase the m_append_position of stream
   *  4. init/fills-in reserve context data (reserved position, reserved pointer, amount)
   *  5. release mutex
   *  6. if needed, notifies m_filled_stream_handler that too much data was appended since drop position
   *     drop_position is a logical position in past of stream; this is set by an aggregator of "interested" clients
   *     of stream data and instructs the stream up to which position is safe to drop the data;
   *     the interested clients may be : normal readers or the readers which saves the data to disk (for persistence)
   */
  char *multi_thread_stream::reserve_with_buffer (const size_t amount, stream_reserve_context *&reserved_context)
  {
    char *ptr = NULL;

    assert (amount > 0);

    m_buffer_mutex.lock ();
    while (1)
      {
	reserved_context = m_reserved_positions.produce ();
	if (reserved_context == NULL)
	  {
	    /* this may happen due to a very slow committee, which may cause to fill up the queue */
	    m_buffer_mutex.unlock ();
	    std::this_thread::sleep_for (std::chrono::microseconds (100));
	    m_stat_reserve_queue_spins++;
	    m_buffer_mutex.lock ();
	    continue;
	  }

	ptr = (char *) m_bip_buffer.reserve (amount);
	if (ptr != NULL)
	  {
	    break;
	  }
	m_reserved_positions.undo_produce (reserved_context);

	m_buffer_mutex.unlock ();
	std::this_thread::sleep_for (std::chrono::microseconds (100));
	m_stat_reserve_buffer_spins++;
	m_buffer_mutex.lock ();
      }

    assert (reserved_context != NULL);

    reserved_context->start_pos = m_append_position;

    m_append_position += amount;

    /* set pointer under mutex lock, a "commit" call may collapse until this slot */
    reserved_context->ptr = ptr;
    reserved_context->reserved_amount = amount;
    reserved_context->written_bytes = 0;

    float stream_fill = stream_fill_factor ();
    stream_position drop_pos = m_last_dropable_pos;
    size_t produced_since_drop = m_append_position - m_last_dropable_pos;

    m_buffer_mutex.unlock ();

    /* notify that stream content needs to be saved, otherwise it may be overwritten in bip_buffer */
    if (m_filled_stream_handler && stream_fill >= 1.0f)
      {
	m_filled_stream_handler (drop_pos, produced_since_drop);
      }

    return ptr;
  }

  /*
   * This is used in context of serial reading and performs an action when not enough data is committed.
   * The action may be : block (until data is committed) or actively fetch data (from disk or other on-demand producer)
   * skip_mode argument indicates which action to take after data becomes available.
   */
  int multi_thread_stream::wait_for_data (const size_t amount, const STREAM_SKIP_MODE skip_mode)
  {
    int err = NO_ERROR;

    if (m_read_position + amount <= m_last_committed_pos)
      {
	if (skip_mode == STREAM_SKIP)
	  {
	    m_read_position += amount;
	  }
	return NO_ERROR;
      }

    m_stat_read_not_enough_data_cnt++;

    std::unique_lock<std::mutex> local_lock (m_buffer_mutex);
    m_serial_read_wait_pos = m_read_position + amount;
    m_serial_read_cv.wait (local_lock,
			   [&] { return m_is_stopped || m_last_committed_pos >= m_serial_read_wait_pos; });
    m_serial_read_wait_pos = std::numeric_limits<stream_position>::max ();
    local_lock.unlock ();

    if (m_is_stopped)
      {
	err = ER_STREAM_NO_MORE_DATA;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_NO_MORE_DATA, 3, this->name ().c_str (), m_read_position, amount);
	return err;
      }

    assert (m_read_position + amount <= m_last_committed_pos);

    if (skip_mode == STREAM_SKIP)
      {
	assert (err == NO_ERROR);
	m_read_position += amount;
      }

    return err;
  }

  /*
   * starts a range read from a position in stream
   * this must be in the same scope with a unlatch_read_data call (to make sure the read latch is released)
   * if portion of range is still in buffer, it reads the available amount, returning the actual read amount and
   * a read latch id
   * if no range can be found in bip_buffer, the stream_io is used to provide the range from disk
   * TODO: read_latch_page_idx type will change to account of reading from stream_file source.
   *
   *  1. if needed, wait for data to be committed
   *  2. if part of range is not in bip_buffer, use stream_io to get range
   *  3. acquire mutex
   *  4. bip_buffer.get_read_ranges : get available readable ranges from bip_buffer
   *  5. convert logical range into physical range
   *  6. try to read-latch the read range in bip_buffer
   *  7. release mutex
   */
  char *multi_thread_stream::get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
      size_t &actual_read_bytes, cubmem::buffer_latch_read_id &read_latch_page_idx)
  {
    int err = NO_ERROR;
    char *ptr = NULL;

    assert (amount > 0);

    if (req_start_pos + amount > m_last_committed_pos)
      {
	m_stat_read_not_enough_data_cnt++;
	/* not yet produced */
	assert (false);
	err = ER_STREAM_NO_MORE_DATA;
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_NO_MORE_DATA, 3, this->name ().c_str (), m_read_position,
		amount);

	return NULL;
      }

    if (req_start_pos < m_oldest_readable_position)
      {
	m_stat_read_not_in_buffer_cnt++;
	/* not in buffer anymore request it from stream_io */
	/* TODO[arnia] */
	// m_io->read (req_start_pos, buf, amount);
	return NULL;
      }

    std::unique_lock<std::mutex> ulock (m_buffer_mutex);
    /* update mapped positions according to buffer pointers */
    const char *ptr_trail_a;
    const char *ptr_trail_b;
    size_t amount_trail_a, amount_trail_b, total_in_buffer;

    m_bip_buffer.get_read_ranges (ptr_trail_b, amount_trail_b, ptr_trail_a, amount_trail_a);
    if (amount_trail_a == 0 && amount_trail_b == 0)
      {
	/* no readable regions */
	m_stat_read_no_readable_pos_cnt++;
	return NULL;
      }

    total_in_buffer = amount_trail_b + amount_trail_a;

    m_oldest_readable_position = m_last_committed_pos - total_in_buffer;

    if (req_start_pos < m_oldest_readable_position)
      {
	m_stat_read_not_in_buffer_cnt++;
	/* not in buffer anymore request it from stream_io */
	/* TODO: */
	// m_io->read (req_start_pos, buf, amount);
	return NULL;
      }

    if (m_last_committed_pos - req_start_pos <= amount_trail_b)
      {
	/* the area may be found in trail of region B
	 * this includes case when B does not exit - from start of buffer) */
	ptr = (char *) ptr_trail_b + amount_trail_b - (m_last_committed_pos - req_start_pos);
	assert (ptr_trail_b + amount_trail_b > ptr);
	actual_read_bytes = MIN ((int) amount, ptr_trail_b + amount_trail_b - ptr);
      }
    else
      {
	ptr = (char *) ptr_trail_a + amount_trail_a - (m_last_committed_pos - req_start_pos - amount_trail_b);
	assert (ptr_trail_a + amount_trail_a > ptr);
	actual_read_bytes = MIN ((int) amount, ptr_trail_a + amount_trail_a - ptr);
      }

    err = m_bip_buffer.start_read (ptr, actual_read_bytes, read_latch_page_idx);
    if (err != NO_ERROR)
      {
	m_stat_read_buffer_failed_cnt++;
	return NULL;
      }

    return ptr;
  }

  int multi_thread_stream::unlatch_read_data (const cubmem::buffer_latch_read_id &read_latch_page_idx)
  {
    std::unique_lock<std::mutex> ulock (m_buffer_mutex);
    m_bip_buffer.end_read (read_latch_page_idx);

    return NO_ERROR;
  }

} /* namespace cubstream */
