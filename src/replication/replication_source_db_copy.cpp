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
 * replication_source_db_copy.cpp
 */

#ident "$Id$"

#include "replication_source_db_copy.hpp"
#include "heap_attrinfo.h"  /* for HEAP_CACHE_ATTRINFO */
#include "heap_file.h"      /* heap_attrinfo_transform_to_disk */
#include "replication_master_node.hpp"
#include "replication_node.hpp"
#include "replication_object.hpp"
#include "scan_manager.h"
#include "thread_entry_task.hpp"

namespace cubreplication
{

  int convert_to_last_representation (cubthread::entry *thread_p, RECDES &rec_des, record_descriptor &record,
				      const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info);
  source_copy_context::source_copy_context ()
  {
    m_stream = NULL;
    m_stream_file = NULL;
    m_transfer_sender = NULL;
    m_state = NOT_STARTED;
  }

  void source_copy_context::set_credentials (const char *user, const char *password)
  {
    m_class_schema.set_params ("", user, password, "");
    m_triggers.set_params ("", user, password, "");
    m_indexes.set_params ("", user, password, "");
  }

  void source_copy_context::pack_and_add_object (row_object &obj)
  {
    if (obj.get_rec_cnt () == 0)
      {
	return;
      }

    stream_entry stream_entry (m_stream);

    stream_entry.add_packable_entry (&obj);

    stream_entry.pack ();
  }

  void source_copy_context::pack_and_add_sbr (sbr_repl_entry &sbr)
  {
    stream_entry stream_entry (m_stream);

    stream_entry.add_packable_entry (&sbr);

    stream_entry.pack ();
  }

  int source_copy_context::transit_state (copy_stage new_state)
  {
    int error = NO_ERROR;

    /* TODO: extend with copy daemon interaction */

    if ((int) m_state != ((int) new_state - 1))
      {
	return ER_FAILED;
      }

    m_state = new_state;

    if (m_state == SCHEMA_APPLY_CLASSES_FINISHED)
      {
	pack_and_add_sbr (m_class_schema);
      }

    return error;
  }

  void source_copy_context::append_class_schema (const char *buffer, const size_t buf_size)
  {
    m_class_schema.append_statement (buffer, buf_size);
  }

  void source_copy_context::append_triggers_schema (const char *buffer, const size_t buf_size)
  {
    m_triggers.append_statement (buffer, buf_size);
  }

  void source_copy_context::append_indexes_schema (const char *buffer, const size_t buf_size)
  {
    m_indexes.append_statement (buffer, buf_size);
  }

  cubstream::multi_thread_stream *source_copy_context::get_stream_for_copy ()
  {
    INT64 buffer_size = 1 * 1024 * 1024;
    int num_max_appenders = log_Gl.trantable.num_total_indices + 1;

    /* TODO[replication] : here, we create a new stream for each new slave copy, but as an optimization,
     * we should reuse a db copy stream which is recent enough (for slave doesn't matter) */

    cubstream::multi_thread_stream *copy_db_stream = new cubstream::multi_thread_stream (buffer_size, 2);
    const node_definition *myself = master_node::get_instance (NULL)->get_node_identity ();
    copy_db_stream->set_name ("repl_copy_" + std::string (myself->get_hostname ().c_str ()));
    copy_db_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    copy_db_stream->init (0);

    return copy_db_stream;
  }


  class source_copy_daemon_task : public cubthread::entry_task
  {
    public:
      source_copy_daemon_task ()
      {
      };

      void execute (cubthread::entry &thread_ref) override
      {
        /* - create transaction 
         * - start DDL proxy
         * - scan heaps
         * - terminate
         */
        //locator_repl_start_tran (&thread_ref);


        //locator_repl_extract_schema ();

      };

    private:

  };


  /*
   * create_scan_for_replication_copy - creates a HEAP SCAN to be used by replication copy (no regu variables)
   *
   * thread_p (in):
   * s_id (s_id/out):
   * class_oid (in):
   * class_hfid (in):
   */
  static void
  create_scan_for_replication_copy (cubthread::entry *thread_p, SCAN_ID &s_id, OID &class_oid, HFID &class_hfid)
  {
    const bool mvcc_select_lock_needed = false;
    SCAN_OPERATION_TYPE scan_op_type = S_SELECT;
    int fixed = true;
    int grouped = false;
    QPROC_SINGLE_FETCH single_fetch = QPROC_NO_SINGLE_INNER;
    SCAN_TYPE scan_type = S_HEAP_SCAN;
    int error = NO_ERROR;

    error = scan_open_heap_scan (thread_p, &s_id, mvcc_select_lock_needed, scan_op_type, fixed, grouped, single_fetch,
				 NULL, /* join_dbval */
				 NULL, /* val_list */
				 NULL, /* vd */
				 &class_oid, &class_hfid,
				 NULL, /* regu_variable_list_node*/
				 NULL, /* pr */
				 NULL, /* regu_list_rest */
				 0, /* num_attrs_pred */
				 NULL, /* attrids_pred */
				 NULL, /* cache_pred */
				 0, /* num_attrs_rest */
				 NULL, /* attrids_rest*/
				 NULL, /* cache_rest */
				 scan_type,
				 NULL, /* cache_recordinfo */
				 NULL /* regu_list_recordinfo*/
				);

    assert (error == NO_ERROR);
  }

  /*
   * copy_class - scans the heap of a class and pushes row records into the replication copy stream (context on TDES)
   *
   * thread_p (in):
   * class_oid (in):
   */
  int copy_class (cubthread::entry *thread_p, OID &class_oid)
  {
    SCAN_ID s_id;
    SCAN_CODE sc_scan;
    HEAP_CACHE_ATTRINFO attr_info;
    HFID class_hfid;
    bool attr_info_inited = false;
    int error_code = NO_ERROR;
    char *class_name = NULL;
    LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

    assert (tdes != NULL);

    error_code = heap_get_class_name (thread_p, &class_oid, &class_name);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error_code;
      }

    row_object heap_objects (class_name);

    error_code = heap_get_hfid_from_class_oid (thread_p, &class_oid, &class_hfid);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto end;
      }

    error_code = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto end;
      }

    attr_info_inited = true;

    create_scan_for_replication_copy (thread_p, s_id, class_oid, class_hfid);

    do
      {
	sc_scan = scan_next_scan (thread_p, &s_id);
	if (sc_scan == S_END)
	  {
	    break;
	  }

	if (sc_scan != S_SUCCESS)
	  {
	    ASSERT_ERROR_AND_SET (error_code);
	    goto end;
	  }

	record_descriptor record;
	error_code = convert_to_last_representation (thread_p, s_id.s.hsid.row_recdes, record,
		     s_id.s.hsid.curr_oid, attr_info);
	if (error_code != NO_ERROR)
	  {
	    ASSERT_ERROR ();
	    goto end;
	  }
	heap_objects.move_record (std::move (record));

	if (heap_objects.is_pack_needed ())
	  {
	    /* pack and add to stream */
	    tdes->replication_copy_context.pack_and_add_object (heap_objects);
	    heap_objects.reset ();
	  }
      }
    while (1);

    tdes->replication_copy_context.pack_and_add_object (heap_objects);

end:
    if (class_name != NULL)
      {
	free_and_init (class_name);
      }

    if (attr_info_inited)
      {
	heap_attrinfo_end (thread_p, &attr_info);
	attr_info_inited = false;
      }

    scan_end_scan (thread_p, &s_id);

    return error_code;
  }

  /*
   * convert_to_last_representation - converts a row record to last representation
   *
   * thread_p (in):
   * rec_des (in): input recdes
   * record (out): output record_descriptor containing a copy of converted RECDES
   * inst_oid(in): instance OID of record
   * attr_info(in/out): cache attributes storing representations and attribute values
   */
  int convert_to_last_representation (cubthread::entry *thread_p, RECDES &rec_des, record_descriptor &record,
				      const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info)
  {
    int error_code = NO_ERROR;
    const int reprid = or_rep_id (&rec_des);

    if (reprid == attr_info.last_classrepr->id)
      {
	/* create by copying the rec_des */
	record.set_recdes (rec_des);
	return error_code;
      }

    error_code = heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &rec_des, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error_code;
      }

    /* old_recdes.data maybe be PEEKed (buffer in page) or COPYed (buffer managed by heap SCAN_CACHE),
     * we don't care about its buffer */
    SCAN_CODE scan_code = heap_attrinfo_transform_to_disk (thread_p, &attr_info, &rec_des, &record);
    if (scan_code != S_SUCCESS)
      {
	ASSERT_ERROR ();
	error_code = ER_FAILED;
	return error_code;
      }

    return error_code;
  }

} /* namespace cubreplication */
