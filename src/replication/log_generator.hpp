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

#include "log_lsa.hpp"
#include "replication_stream_entry.hpp"
#include "storage_common.h"
#include "recovery.h"

// forward definitions

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
  class replication_object;

  /*
   * class for producing log stream entries
   * only a global instance (per template class) is allowed
   *
   * The log_generator has an instance in each transaction descriptor.
   * It stores replication objects into a stream_entry.
   * Transactions append replication data by using dedicate interface:
   *  add_statement, add_delete_row, add_insert_row, add_update_row, add_attribute_change
   * Some of these methods (add_update_row, add_attribute_change) may not add final
   * replication objects; in such case a special storage is used for "pending replication objects";
   * this is the case when record descriptor or DB_VALUEs are changed in heap, and later, in request processing
   * the index key is updated : this transforms a "pending" replication object into a final replication object.
   *
   * From point of view of replication, transaction is finalized by:
   *  on_transaction_commit, on_transaction_abort
   * These fills transaction information (state, MVCCID) and packs all replication objects into replication stream.
   *
   * The replication stream is set at two levels:
   *  set_stream : this is a private instance method which sets the stream into the the aggregate
   *               stream_entry object of log_generator. The design of the CUBRID stream code requires that each
   *               stream_entry instance has its own stream reference (there may be multiple flavours of stream_entry)
   *  set_global_stream  : this is a static public method which sets stream for each instance of log_generator
   *                       (of each log transaction descriptor). This is called at replication_node level.
   */

  class log_generator
  {
    private:

      std::vector <changed_attrs_row_repl_entry *> m_pending_to_be_added;

      stream_entry m_stream_entry;

      bool m_has_stream;
      bool m_is_row_replication_disabled;
      static cubstream::multi_thread_stream *s_stream;

    public:

      log_generator ()
	: log_generator (NULL)
      {
      };

      log_generator (cubstream::multi_thread_stream *stream)
	: m_pending_to_be_added ()
	, m_stream_entry (stream)
	, m_has_stream (false)
	, m_is_row_replication_disabled (true)
      {
      };

      ~log_generator ();

      // act when trasaction is committed; replication entries are logged
      void on_transaction_commit (void);
      // act when sysop with HA info is committed; replication entries are logged
      void on_sysop_commit (LOG_LSA &start_lsa);
      // act when transaction is aborted; replication entries are logged
      void on_transaction_abort (void);
      // act when sysop is aborted
      void on_sysop_abort (LOG_LSA &start_lsa);
      // clear transaction data (e.g. logtb_clear_tdes)
      void clear_transaction (void);

      // statement-based replication
      void add_statement (repl_info_sbr &stmt_info);

      // row-based replication
      void add_delete_row (const DB_VALUE &key, const OID &class_oid);
      void add_insert_row (const DB_VALUE &key, const OID &class_oid, const RECDES &record);
      void add_update_row (const DB_VALUE &key, const OID &inst_oid, const OID &class_oid,
			   const RECDES *optional_recdes);
      void update_lsastamp_for_changed_repl_object (const OID &inst_oid);
      void add_attribute_change (const OID &class_oid, const OID &inst_oid, ATTR_ID col_id, const DB_VALUE &value);

      void remove_attribute_change (const OID &class_oid, const OID &inst_oid);

      void abort_pending_repl_objects ();

      stream_entry *get_stream_entry (void);

      void pack_stream_entry (void);

      void er_log_repl_obj (replication_object *obj, const char *message);

      void check_commit_end_tran (void);

      static void pack_group_commit_entry (void);

      static cubstream::multi_thread_stream *get_global_stream (void)
      {
	return s_stream;
      };

      cubstream::multi_thread_stream *get_stream (void)
      {
	return m_stream_entry.get_stream ();
      };

      static void set_global_stream (cubstream::multi_thread_stream *stream);

      void set_row_replication_disabled (bool disable_if_true);
      bool is_row_replication_disabled (void);
      void apply_tran_mvccid (void);

#if !defined(NDEBUG) && defined (SERVER_MODE)
      int abort_sysop_and_simulate_apply_repl_rbr_on_master (LOG_LSA &filter_replication_lsa);
      int abort_partial_and_simulate_apply_sbr_repl_on_master (const char *savepoint_name);
#endif

    private:

      void set_stream (cubstream::multi_thread_stream *stream)
      {
	m_stream_entry.set_stream (stream);
	m_has_stream = true;
      }

      void set_tran_repl_info (stream_entry_header::TRAN_STATE state);

      char *get_classname (const OID &class_oid);     // todo - optimize this step

      // common point for transaction commit/abort; replication entries are logged
      void on_transaction_finish (stream_entry_header::TRAN_STATE state);

      void append_repl_object (replication_object &object);

      bool is_replication_disabled ();
  };

} /* namespace cubreplication */

#endif /* _LOG_GENERATOR_HPP_ */
