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
 * packing_stream.cpp
 */

#ident "$Id$"

#include "error_code.h"
#include "packing_stream.hpp"
#include "stream_packer.hpp"
#include <algorithm>

namespace cubstream
{

  entry::entry (packing_stream *stream)
  {
    m_stream = stream;
    m_data_start_position = 0;
    set_packable (false);
  }

  int entry::pack (void)
  {
    size_t total_stream_entry_size;
    size_t data_size, header_size;
    char *stream_start_ptr;
    int i;

    assert (m_is_packable == true);
    if (m_packable_entries.size() == 0)
      {
	return NO_ERROR;
      }

    stream_packer *serializator = get_packer ();
    header_size = get_header_size ();
    data_size = get_entries_size ();
    total_stream_entry_size = DB_ALIGN (header_size, MAX_ALIGNMENT) + data_size;

    stream_start_ptr = serializator->start_packing_range (total_stream_entry_size);
    if (stream_start_ptr == NULL)
      {
        assert (false);
        return ER_FAILED;
      }

    set_header_data_size (data_size);

    pack_stream_entry_header ();

    for (i = 0; i < m_packable_entries.size(); i++)
      {
	serializator->align (MAX_ALIGNMENT);
#if !defined (NDEBUG)
	const char *old_ptr = serializator->get_curr_ptr ();
	const char *curr_ptr;
	size_t entry_size = m_packable_entries[i]->get_packed_size (serializator);
#endif

	m_packable_entries[i]->pack (serializator);

#if !defined (NDEBUG)
	curr_ptr = serializator->get_curr_ptr ();
	assert (curr_ptr - old_ptr == entry_size);
#endif
      }
    serializator->align (MAX_ALIGNMENT);

    /* set last packed stream position */
    serializator->packing_completed ();

    return NO_ERROR;
  }

  /*
   * this is pre-unpack method : it fetches enough data to unpack stream header contents,
   * then fetches (receive from socket) the actual amount of data without unpacking it.
   */
  int entry::prepare (void)
  {
    char *stream_start_ptr;
    stream_packer *serializator = get_packer ();
    size_t stream_entry_header_size = get_header_size ();
    size_t packed_data_size;
    int error_code;

    stream_start_ptr = serializator->start_unpacking_range (stream_entry_header_size);
    if (stream_start_ptr == NULL)
      {
        return ER_FAILED;
      }

    error_code = unpack_stream_entry_header ();

    if (error_code != NO_ERROR)
      {
	return error_code;
      }
    serializator->unpacking_completed ();

    m_data_start_position = serializator->get_next_read_position ();
    /* force read (fetch data from stream source) */
    packed_data_size = get_data_packed_size ();
    /* TODO : this should be a a seek-only method: we don't need actual data here,
     * but for some stream we need to fetch data (from socket)
     * for a stream fetching from file, we don't need to read anything */
    stream_start_ptr = serializator->start_unpacking_range (packed_data_size);
    if (stream_start_ptr == NULL)
      {
	error_code = ER_FAILED;
	return error_code;
      }

    /* the stream entry header is unpacked, and its contents are ready to be unpacked */
    serializator->unpacking_completed ();

    return error_code;
  }

  /* this is called only right before applying the replication data */
  int entry::unpack (void)
  {
    int i;
    int error_code = NO_ERROR;
    int object_id;

    /* position the serializator range to data contents */

    stream_packer *serializator = get_packer ();
    size_t count_packable_entries = get_packable_entry_count_from_header ();
    char *stream_start_ptr = serializator->start_unpacking_range_from_pos (m_data_start_position,
			     get_data_packed_size ());
    if (stream_start_ptr == NULL)
      {
        /* no more data */
        assert (er_errid () == ER_STREAM_NO_MORE_DATA);
        return ER_STREAM_NO_MORE_DATA;
      }

    for (i = 0 ; i < count_packable_entries; i++)
      {
        serializator->align (MAX_ALIGNMENT);
        /* peek type of object : it will be consumed by object's unpack */
	serializator->peek_unpack_int (&object_id);
	
	cubpacking::packable_object *packable_entry = get_builder()->create_object (object_id);
	if (packable_entry == NULL)
	  {
	    error_code = ER_STREAM_UNPACKING_INV_OBJ_ID;
            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_STREAM_UNPACKING_INV_OBJ_ID, 1, object_id);
	    return error_code;
	  }

	assert (packable_entry != NULL);

	/* unpack one replication entry */
	add_packable_entry (packable_entry);
	packable_entry->unpack (serializator);
      }
    serializator->align (MAX_ALIGNMENT);
    serializator->unpacking_completed ();

    return error_code;
  }

  int entry::add_packable_entry (cubpacking::packable_object *entry)
  {
    m_packable_entries.push_back (entry);

    return NO_ERROR;
  }

  size_t entry::get_entries_size (void)
  {
    size_t entry_size, total_size = 0;
    int i;

    stream_packer *serializator = get_packer ();
    for (i = 0; i < m_packable_entries.size (); i++)
      {
        total_size = DB_ALIGN (total_size, MAX_ALIGNMENT);
	entry_size = m_packable_entries[i]->get_packed_size (serializator);
	total_size += entry_size;
      }

    return total_size;
  }


////////////////////////////////////////

  packing_stream::packing_stream (const size_t buffer_capacity, const int max_appenders)
    : m_bip_buffer (buffer_capacity), m_reserved_positions (max_appenders)
  {
    m_oldest_readable_position = 0;

    /* TODO : system parameter */
    m_trigger_flush_to_disk_size = buffer_capacity / 2;

    m_trigger_min_to_read_size = 1024;

    m_stat_reserve_queue_spins = 0;
    m_stat_reserve_buffer_spins = 0;
    m_stat_read_not_enough_data_cnt = 0;
    m_stat_read_not_in_buffer_cnt = 0;
    m_stat_read_no_readable_pos_cnt = 0;
    m_stat_read_buffer_failed_cnt = 0;
  }

  packing_stream::~packing_stream ()
  {
    assert (m_append_position - m_read_position == 0);
  }

  int packing_stream::write (const size_t byte_count, write_handler *handler)
  {
    int err = NO_ERROR;
    stream_reserve_context *reserve_context = NULL;
    char *ptr;
    stream_position reserved_pos, new_completed_position;

    ptr = reserve_with_buffer (byte_count, reserve_context);
    if (ptr == NULL )
      {
	err = ER_FAILED;
	return err;
      }
    reserved_pos = reserve_context->start_pos;

    err = handler->write_action (reserved_pos, ptr, byte_count);
    if (err < 0)
      {
        return ER_FAILED;
      }
    reserve_context->written_bytes = err;

    commit_append (reserve_context);

    return err;
  }

  int packing_stream::read (const stream_position first_pos, const size_t byte_count, read_handler *handler)
  {
    int err = NO_ERROR;
    char *ptr;
    size_t actual_read_bytes = 0;
    char *local_buffer = NULL;
    mem::buffer_latch_read_id read_latch_page_idx;

    ptr = get_data_from_pos (first_pos, byte_count, actual_read_bytes, read_latch_page_idx);
    if (ptr == NULL )
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

            delete []local_buffer;
	    return err;
          }

        assert (next_actual_read_bytes + actual_read_bytes == byte_count);

        memcpy (local_buffer + actual_read_bytes, ptr, next_actual_read_bytes);

        unlatch_read_data (read_latch_page_idx);

        ptr = local_buffer;
      }

    err = handler->read_action (first_pos, ptr, byte_count);

    if (ptr != local_buffer)
      {
        unlatch_read_data (read_latch_page_idx);
      }
    else
      {
        delete[] local_buffer;
      }

    return err;
  }

  int packing_stream::read_partial (const stream_position first_pos, const size_t byte_count, size_t *actual_read_bytes,
				    partial_read_handler *handler)
  {
    int err = NO_ERROR;
    char *ptr;
    size_t contiguous_bytes_in_buffer = 0;
    mem::buffer_latch_read_id read_latch_page_idx;

    ptr = get_data_from_pos (first_pos, byte_count, contiguous_bytes_in_buffer, read_latch_page_idx);
    if (ptr == NULL )
      {
	err = ER_FAILED;
	return err;
      }

    err = handler->read_action (first_pos, ptr, contiguous_bytes_in_buffer, actual_read_bytes);

    unlatch_read_data (read_latch_page_idx);

    return err;
  }

  int packing_stream::commit_append (stream_reserve_context *reserve_context)
  {
    stream_reserve_context *last_used_context = NULL;
    bool collapsed_reserve;
    char *ptr_commit;
    stream_position new_completed_position = reserve_context->start_pos + reserve_context->reserved_amount;

    m_buffer_mutex.lock ();
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

    /* notify readers of the new completed position */
    if (new_completed_position > m_last_notified_committed_pos + m_trigger_min_to_read_size)
      {
	if (m_ready_pos_handler != NULL)
	  { 
            int err;
	    err = m_ready_pos_handler->notify (m_last_notified_committed_pos,
					       new_completed_position - m_last_notified_committed_pos);
	    if (err != NO_ERROR)
	      {
		return err;
	      }
	  }
	m_last_notified_committed_pos = new_completed_position;
      }

    m_buffer_mutex.unlock ();

    return NO_ERROR;
  }

  char *packing_stream::reserve_with_buffer (const size_t amount, stream_reserve_context* &reserved_context)
  {
    int err;
    char *ptr = NULL;
    stream_reserve_context context;
    
    assert (amount > 0);

    m_buffer_mutex.lock ();
    while (1)
      {
        reserved_context = m_reserved_positions.produce ();
        if (reserved_context == NULL)
          {
            /* this may happen due to a very slow commiter, which may cause to fill up the queue */
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
    if (m_filled_stream_handler != NULL
	&& stream_fill >= 1.0f)
      {
	m_filled_stream_handler->notify (drop_pos, produced_since_drop);
      }

    return ptr;
  }

  int packing_stream::fetch_data_from_provider (const stream_position &pos, const size_t amount)
  {
    int err = NO_ERROR;

    assert (m_fetch_data_handler != NULL);

    err = m_fetch_data_handler->fetch_action (pos, NULL, amount, NULL);
    if (err != NO_ERROR)
      {
	return ER_FAILED;
      }

    return NO_ERROR;
  }

  char *packing_stream::get_more_data_with_buffer (const size_t amount, size_t &actual_read_bytes,
                                                   stream_position &trail_pos,
                                                   mem::buffer_latch_read_id &read_latch_page_idx)
  {
    char *ptr;
    int err = NO_ERROR;
    stream_position to_read_pos;

    to_read_pos = m_read_position;

    if (to_read_pos + amount > m_last_committed_pos)
      {
        m_stat_read_not_enough_data_cnt++;
        if (m_fetch_data_handler != NULL)
          {
            err = fetch_data_from_provider (to_read_pos, amount);
          }

        /* tell user to wait */
        return NULL;
      }

    m_read_position = to_read_pos + amount;

    ptr = get_data_from_pos (to_read_pos, amount, actual_read_bytes, read_latch_page_idx);
    if (ptr == NULL)
      {
	return ptr;
      }
    trail_pos = to_read_pos + amount;

    return ptr;
  }

  char *packing_stream::get_data_from_pos (const stream_position &req_start_pos, const size_t amount,
    size_t &actual_read_bytes, int &read_latch_page_idx)
  {
    int i;
    int err = NO_ERROR;
    char *ptr = NULL;

    if (req_start_pos + amount > m_last_committed_pos
        && m_fetch_data_handler != NULL)
      {
        m_stat_read_not_enough_data_cnt++;
	/* not yet produced */
        /* try asking for more data : */
        size_t actual_read_bytes;
        err = m_fetch_data_handler->fetch_action (req_start_pos, ptr, amount, &actual_read_bytes);
        if (err != NO_ERROR)
          {
	    return NULL;
          }

        if (actual_read_bytes != amount)
          {
	    /* TODO [arnia] : what could be the reason ? */
	    return NULL;
          }

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
    stream_position start_active_region;

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
        actual_read_bytes = MIN (amount, amount_trail_b);
      }
    else
      {
        ptr = (char *) ptr_trail_a + amount_trail_a - (m_last_committed_pos - req_start_pos - amount_trail_b);
        actual_read_bytes = MIN (amount, amount_trail_a);
      }
    err = m_bip_buffer.start_read (ptr, actual_read_bytes, read_latch_page_idx);
    if (err != NO_ERROR)
      {
        m_stat_read_buffer_failed_cnt++;
        return NULL;
      }

    return ptr;
  }

  int packing_stream::unlatch_read_data (const int &read_latch_page_idx)
    {
      std::unique_lock<std::mutex> ulock (m_buffer_mutex);
      m_bip_buffer.end_read (read_latch_page_idx);

      return NO_ERROR;
    }

} /* namespace cubstream */
