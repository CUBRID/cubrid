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

#include "log_generator.hpp"
#include "log_consumer.hpp"
#include "replication_entry.hpp"
#include "replication_stream_entry.hpp"
#include "thread_compat.hpp"
#include "test_log_generator.hpp"
#include "thread_manager.hpp"

namespace test_replication
{
  class stream_mover
  {
    private:
      static const int BUF_SIZE = 1024;
      char m_buffer[BUF_SIZE];

      int read_action (char *ptr, const size_t byte_count)
      {
	memcpy (m_buffer, ptr, byte_count);
	return (int) byte_count;
      };

      int write_action (const cubstream::stream_position pos, char *ptr, const size_t byte_count)
      {
	memcpy (ptr, m_buffer, byte_count);
	return (int) byte_count;
      };

    public:

      stream_mover ()
      {
	m_reader_func = std::bind (&stream_mover::read_action, std::ref (*this),
				   std::placeholders::_1, std::placeholders::_2);
	m_writer_func = std::bind (&stream_mover::write_action, std::ref (*this),
				   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

      };

      size_t get_buf_size (void)
      {
	return sizeof (m_buffer);
      };

      cubstream::stream::read_func_t m_reader_func;
      cubstream::stream::write_func_t m_writer_func;
  };



  int move_buffers (cubstream::packing_stream *stream1, cubstream::packing_stream *stream2)
  {
    stream_mover test_stream_mover;
    cubstream::stream_position curr_pos;
    cubstream::stream_position last_pos;
    last_pos = stream1->get_last_committed_pos ();

    size_t copy_chunk_size = test_stream_mover.get_buf_size ();
    int read_bytes, written_bytes;

    for (curr_pos = 0; curr_pos <= last_pos;)
      {
	copy_chunk_size = MIN (copy_chunk_size, last_pos - curr_pos);

	read_bytes = stream1->read (curr_pos, copy_chunk_size, test_stream_mover.m_reader_func);
	if (read_bytes <= 0)
	  {
	    break;
	  }

	written_bytes = stream2->write (read_bytes, test_stream_mover.m_writer_func);
	assert (read_bytes == written_bytes);
	if (written_bytes <= 0)
	  {
	    break;
	  }

	curr_pos += read_bytes;
      }

    return NO_ERROR;
  }

  int init_common_cubrid_modules (void)
  {
    int res;
    THREAD_ENTRY *thread_p = NULL;


    lang_init ();
    tp_init ();
    lang_set_charset_lang ("en_US.iso88591");

    cubthread::initialize (thread_p);
    res = cubthread::initialize_thread_entries ();
    if (res != NO_ERROR)
      {
	ASSERT_ERROR ();
	return res;
      }
    return NO_ERROR;
  }

  int test_stream_packing (void)
  {
    int res = 0;

    init_common_cubrid_modules ();

    cubreplication::sbr_repl_entry *sbr1 = new cubreplication::sbr_repl_entry;
    cubreplication::sbr_repl_entry *sbr2 = new cubreplication::sbr_repl_entry;
    cubreplication::single_row_repl_entry *rbr1 =
	    new cubreplication::single_row_repl_entry (cubreplication::REPL_UPDATE, "t1");
    cubreplication::single_row_repl_entry *rbr2 =
	    new cubreplication::single_row_repl_entry (cubreplication::REPL_INSERT, "t2");

    DB_VALUE key_value;
    DB_VALUE new_att1_value;
    DB_VALUE new_att2_value;
    DB_VALUE new_att3_value;

    db_make_int (&key_value, 123);
    db_make_int (&new_att1_value, 2);
    db_make_char (&new_att2_value, 4, "test", 4, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);
    db_make_char (&new_att3_value, 5, "test2", 5, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);

    sbr1->set_statement ("CREATE TABLE t1 (i1 int)");
    sbr2->set_statement ("CREATE TABLE t2 (i1 int)");

    rbr1->set_key_value (&key_value);
    rbr1->add_changed_value (1, &new_att2_value);
    rbr1->add_changed_value (2, &new_att1_value);
    rbr1->add_changed_value (3, &new_att3_value);

    rbr2->set_key_value (&key_value);
    rbr2->add_changed_value (1, &new_att1_value);
    rbr2->add_changed_value (2, &new_att2_value);
    rbr2->add_changed_value (3, &new_att3_value);

    cubreplication::log_generator<cubreplication::replication_stream_entry> *lg =
      cubreplication::log_generator<cubreplication::replication_stream_entry>::new_instance (0);

    lg->append_repl_entry (NULL, sbr1);
    lg->append_repl_entry (NULL, rbr1);
    lg->append_repl_entry (NULL, sbr2);
    lg->append_repl_entry (NULL, rbr2);

    lg->set_ready_to_pack (NULL);

    lg->pack_stream_entries (NULL);

    cubreplication::log_consumer *lc = cubreplication::log_consumer::new_instance (0);

    /* get stream from log_generator, get its buffer and attached it to log_consumer stream */
    cubstream::packing_stream *lg_stream = lg->get_stream ();
    cubstream::packing_stream *lc_stream = lc->get_stream ();

    move_buffers (lg_stream, lc_stream);

    cubreplication::replication_stream_entry *se = NULL;

    lc->fetch_stream_entry (se);
    se->unpack ();

    res = se->is_equal (lg->get_stream_entry (NULL));

    /* workaround for seq read position : force read position to append position to avoid stream destructor
     * assertion failure */
    lg_stream->force_set_read_position (lg_stream->get_last_committed_pos ());

    delete lg;
    delete lc;

    return res;
  }
}
