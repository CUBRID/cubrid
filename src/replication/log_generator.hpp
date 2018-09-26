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
 * log_generator.hpp
 */

#ident "$Id$"

#ifndef _LOG_GENERATOR_HPP_
#define _LOG_GENERATOR_HPP_

#include "replication_stream_entry.hpp"
#include "storage_common.h"
#include "recovery.h"

namespace cubstream
{
  class multi_thread_stream;
}

namespace cubthread
{
  class entry;
}

struct repl_info_sbr;

namespace cubreplication
{
  extern bool enable_log_generator_logging;

  class replication_object;

  /*
   * class for producing log stream entries
   * only a global instance (per template class) is allowed
   */

  class log_generator
  {
    private:
      std::vector <changed_attrs_row_repl_entry *> m_pending_to_be_added;

      stream_entry m_stream_entry;

      bool m_is_initialized;

      static cubstream::multi_thread_stream *g_stream;

    public:

      log_generator ()
	: log_generator (NULL)
      {
      };

      log_generator (cubstream::multi_thread_stream *stream)
	: m_stream_entry (stream),
	  m_is_initialized (false)
      {
      };

      ~log_generator ();

      int start_tran_repl (MVCCID mvccid);

      int set_repl_state (stream_entry_header::TRAN_STATE state);

      int append_repl_object (replication_object *object);
      int append_pending_repl_object (cubthread::entry &thread_entry, const OID *class_oid, const OID *inst_oid,
				      ATTR_ID col_id, DB_VALUE *value);
      int set_key_to_repl_object (DB_VALUE *key, const OID *inst_oid, char *class_name, RECDES *optional_recdes);
      int set_key_to_repl_object (DB_VALUE *key, const OID *inst_oid, const OID *class_oid, RECDES *optional_recdes);
      void abort_pending_repl_objects ();

      stream_entry *get_stream_entry (void);

      int pack_stream_entry (void);

      void er_log_repl_obj (replication_object *obj, const char *message);

      void check_commit_end_tran (void);

      static void pack_group_commit_entry (void);

      static cubstream::multi_thread_stream *get_global_stream (void)
      {
	return g_stream;
      };

      cubstream::multi_thread_stream *get_stream (void)
      {
	return m_stream_entry.get_stream ();
      };

      static void set_global_stream (cubstream::multi_thread_stream *stream);

      void set_stream (cubstream::multi_thread_stream *stream)
      {
	m_stream_entry.set_stream (stream);
	m_is_initialized = true;
      }
  };

  int repl_log_insert_with_recdes (THREAD_ENTRY *thread_p, const char *class_name,
                                   cubreplication::repl_entry_type rbr_type, DB_VALUE * key_dbvalue, RECDES *recdes);
  int repl_log_insert_statement (THREAD_ENTRY *thread_p, REPL_INFO_SBR *repl_info);

} /* namespace cubreplication */

#endif /* _LOG_GENERATOR_HPP_ */
