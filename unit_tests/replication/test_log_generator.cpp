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
#include "replication_object.hpp"
#include "replication_stream_entry.hpp"
#include "thread_compat.hpp"
#include "test_log_generator.hpp"
#include "thread_manager.hpp"
#include "thread_entry_task.hpp"
#include <iostream>

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



  int move_buffers (cubstream::multi_thread_stream *stream1, cubstream::multi_thread_stream *stream2)
  {
    stream_mover test_stream_mover;
    cubstream::stream_position curr_pos;
    cubstream::stream_position last_pos;
    last_pos = stream1->get_last_committed_pos ();

    size_t copy_chunk_size = test_stream_mover.get_buf_size ();
    int read_bytes, written_bytes;

    for (curr_pos = 0; curr_pos < last_pos;)
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

  cubthread::manager *cub_th_m;

  int init_common_cubrid_modules (void)
  {
    static bool initialized = false;
    THREAD_ENTRY *thread_p = NULL;

    if (initialized)
      {
	return 0;
      }

    lang_init ();
    tp_init ();
    er_init ("unit_test", 1);
    lang_set_charset_lang ("en_US.iso88591");
    //cub_th_m.set_max_thread_count (100);

    //cubthread::set_manager (&cub_th_m);
    cubthread::initialize (thread_p);
    cub_th_m = cubthread::get_manager ();
    cub_th_m->set_max_thread_count (100);

    cub_th_m->alloc_entries ();
    cub_th_m->init_entries (false);

    initialized = true;


    return NO_ERROR;
  }

  int test_log_generator1 (void)
  {
    int res = 0;

    init_common_cubrid_modules ();

    cubthread::entry *my_thread = thread_get_thread_entry_info ();

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
    rbr1->copy_and_add_changed_value (1, &new_att2_value);
    rbr1->copy_and_add_changed_value (2, &new_att1_value);
    rbr1->copy_and_add_changed_value (3, &new_att3_value);

    rbr2->set_key_value (&key_value);
    rbr2->copy_and_add_changed_value (1, &new_att1_value);
    rbr2->copy_and_add_changed_value (2, &new_att2_value);
    rbr2->copy_and_add_changed_value (3, &new_att3_value);

    cubreplication::log_generator::create_stream (0);

    cubreplication::log_generator *lg =
	    new cubreplication::log_generator (cubreplication::log_generator::get_stream ());

    lg->append_repl_object (sbr1);
    lg->append_repl_object (rbr1);
    lg->append_repl_object (sbr2);
    lg->append_repl_object (rbr2);

    lg->pack_stream_entry ();

    cubreplication::log_consumer *lc = cubreplication::log_consumer::new_instance (0);

    /* get stream from log_generator, get its buffer and attached it to log_consumer stream */
    cubstream::multi_thread_stream *lg_stream = lg->get_stream ();
    cubstream::multi_thread_stream *lc_stream = lc->get_stream ();

    move_buffers (lg_stream, lc_stream);

    cubreplication::stream_entry *se = NULL;

    lc->fetch_stream_entry (se);
    se->unpack ();

    res = se->is_equal (lg->get_stream_entry ());

    /* workaround for seq read position : force read position to append position to avoid stream destructor
     * assertion failure */
    lg_stream->force_set_read_position (lg_stream->get_last_committed_pos ());

    delete lg;
    delete lc;

    return res;
  }

  void generate_rbr (cubthread::entry *thread_p, cubreplication::log_generator *lg)
  {
    cubreplication::REPL_ENTRY_TYPE rbr_type = (cubreplication::REPL_ENTRY_TYPE) (std::rand () % 3);

    cubreplication::single_row_repl_entry *rbr = new cubreplication::single_row_repl_entry (rbr_type, "t1");

    DB_VALUE key_value;
    DB_VALUE new_att1_value;
    DB_VALUE new_att2_value;
    DB_VALUE new_att3_value;
    db_make_int (&key_value, 1 + thread_p->tran_index);
    db_make_int (&new_att1_value, 10 + thread_p->tran_index);
    db_make_int (&new_att2_value, 100 + thread_p->tran_index);
    db_make_int (&new_att3_value, 1000 + thread_p->tran_index);

    rbr->set_key_value (&key_value);
    rbr->copy_and_add_changed_value (1, &new_att2_value);
    rbr->copy_and_add_changed_value (2, &new_att1_value);
    rbr->copy_and_add_changed_value (3, &new_att3_value);

    lg->append_repl_object (rbr);
  }

  void generate_sbr (cubthread::entry *thread_p, cubreplication::log_generator *lg, int tran_chunk, int tran_obj)
  {
    std::string statement = std::string ("T") + std::to_string (thread_p->tran_index) + std::string ("P") + std::to_string (
				    tran_chunk)
			    + std::string ("O") + std::to_string (tran_obj);

    cubreplication::sbr_repl_entry *sbr = new cubreplication::sbr_repl_entry (statement);

    lg->append_repl_object (sbr);
  }

  void generate_tran_repl_data (cubthread::entry *thread_p, cubreplication::log_generator *lg)
  {
    int n_tran_chunks = std::rand () % 5;

    cubreplication::stream_entry *se = lg->get_stream_entry ();

    se->set_mvccid (thread_p->tran_index + 1);

    for (int j = 0; j < n_tran_chunks; j++)
      {
	int n_objects = std::rand () % 5;

	for (int i = 0; i < n_objects; i++)
	  {
	    generate_sbr (thread_p, lg, j, i);
	    std::this_thread::sleep_for (std::chrono::microseconds (std::rand () % 100));
	  }

	if (j == n_tran_chunks - 1)
	  {
	    /* commit entry : last commit or random */
	    lg->set_repl_state (cubreplication::stream_entry_header::COMMITTED);
	  }
	lg->pack_stream_entry ();
      }

    if (std::rand () % 100 > 90)
      {
	lg->pack_group_commit_entry ();
      }
  }

  class gen_repl_context_manager : public cubthread::entry_manager
  {

  };

  std::atomic<int> tasks_running (0);

  class gen_repl_task : public cubthread::entry_task
  {
    public:
      gen_repl_task (int tran_id)
      {
	m_thread_entry.tran_index = tran_id;
	m_lg = new cubreplication::log_generator (cubreplication::log_generator::get_stream ());
      }

      void execute (cubthread::entry &thread_ref) override
      {
	generate_tran_repl_data (&m_thread_entry, m_lg);
	tasks_running--;
      }

      cubthread::entry m_thread_entry;

    private:
      cubreplication::log_generator *m_lg;


  };


  void repl_object_sbr_apply_func (void)
  {
#if 0
    static std::mutex mutex;
    static std::vector<std::string> results;
    std::string to_insert = m_statement + "\n";

    mutex.lock ();
    if (std::find (results.begin (), results.end (), to_insert) != results.end ())
      {
	mutex.unlock ();
      }
    results.push_back (to_insert);
    mutex.unlock ();
#endif
  }

  int test_log_generator2 (void)
  {
#define GEN_THREAD_CNT 10
#define TASKS_CNT 100

    int res = 0;

    init_common_cubrid_modules ();

    cubreplication::log_generator *lg = new cubreplication::log_generator;
    cubreplication::log_generator::create_stream (0);

    cubreplication::log_consumer *lc = cubreplication::log_consumer::new_instance (0, true);


    std::cout << "Starting generating replication data .... ";

    gen_repl_context_manager ctx_m1;
    cubthread::entry_workpool *gen_worker_pool =
	    cub_th_m->create_worker_pool (GEN_THREAD_CNT, GEN_THREAD_CNT, "test_pool", &ctx_m1,
					  1,
					  1);
    tasks_running = TASKS_CNT;
    for (int i = 0; i < TASKS_CNT; i++)
      {
	gen_repl_task *task = new gen_repl_task (i);

	cub_th_m->push_task (gen_worker_pool, task);
      }

    lg->pack_group_commit_entry ();

    while (tasks_running > 0)
      {
	std::this_thread::sleep_for (std::chrono::microseconds (1));
      }

    std::cout << "Done" << std::endl;

    /* get stream from log_generator, get its buffer and attached it to log_consumer stream */
    std::cout << "Copying stream data from log_generator to log_consumer .... ";
    cubstream::multi_thread_stream *lg_stream = lg->get_stream ();
    cubstream::multi_thread_stream *lc_stream = lc->get_stream ();

    move_buffers (lg_stream, lc_stream);

    std::cout << "Done" << std::endl;


    std::this_thread::sleep_for (std::chrono::microseconds (5 * 1000 * 1000));

    /* workaround for seq read position : force read position to append position to avoid stream destructor
     * assertion failure */
    lg_stream->force_set_read_position (lg_stream->get_last_committed_pos ());

    delete lg;
    delete lc;

    return res;
  }
}
