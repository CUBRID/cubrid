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
#include "thread_compat.hpp"
#include "test_log_generator.hpp"
#include "thread_manager.hpp"

namespace test_replication
{

int move_buffers (cubstream::packing_stream *stream1, cubstream::packing_stream *stream2)
{


  return NO_ERROR;
}

int init_common_cubrid_modules (void)
{
  int res;
  THREAD_ENTRY * thread_p = NULL;


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

  cubreplication::sbr_repl_entry sbr1, sbr2;
  cubreplication::single_row_repl_entry rbr1 (cubreplication::REPL_UPDATE, "t1");
  cubreplication::single_row_repl_entry rbr2 (cubreplication::REPL_INSERT, "t2");

  DB_VALUE key_value;
  DB_VALUE new_att1_value;
  DB_VALUE new_att2_value;
  DB_VALUE new_att3_value;

  db_make_int (&key_value, 123);
  db_make_int (&new_att1_value, 2);
  db_make_char (&new_att2_value, 4, "test", 4, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);
  db_make_char (&new_att3_value, 5, "test2", 5, INTL_CODESET_ISO88591, LANG_COLL_ISO_BINARY);

  sbr1.set_statement ("CREATE TABLE t1 (i1 int)");
  sbr2.set_statement ("CREATE TABLE t2 (i1 int)");

  rbr1.set_key_value (&key_value);
  rbr1.add_changed_value (1, &new_att2_value);
  rbr1.add_changed_value (2, &new_att1_value);
  rbr1.add_changed_value (3, &new_att3_value);

  rbr2.set_key_value (&key_value);
  rbr2.add_changed_value (1, &new_att1_value);
  rbr2.add_changed_value (2, &new_att2_value);
  rbr2.add_changed_value (3, &new_att3_value);

  cubreplication::log_generator *lg = cubreplication::log_generator::new_instance (NULL, 0);

  lg->append_repl_entry (NULL, &sbr1);
  lg->append_repl_entry (NULL, &rbr1);
  lg->append_repl_entry (NULL, &sbr2);
  lg->append_repl_entry (NULL, &rbr2);
  lg->append_repl_entry (NULL, &rbr2);

  lg->set_ready_to_pack (NULL);

  lg->pack_stream_entries (NULL);

  cubreplication::log_consumer *lc =
    cubreplication::log_consumer::new_instance (cubreplication::REPLICATION_DATA_APPLIER, 0);
  
  /* get stream from log_generator, get its buffer and attached it to log_consumer stream */
  cubstream::packing_stream *lg_stream = lg->get_write_stream ();
  cubstream::packing_stream *lc_stream = lc->get_write_stream ();

  move_buffers (lg_stream, lc_stream);

  cubreplication::replication_stream_entry *se = NULL;

  lc->fetch_stream_entry (&se);
  se->unpack ();

  res = se->is_equal (lg->get_stream_entry (NULL));

  return res;
}
}
