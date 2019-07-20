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
#include "communication_channel.hpp"
#include "heap_attrinfo.h"  /* for HEAP_CACHE_ATTRINFO */
#include "heap_file.h"      /* heap_attrinfo_transform_to_disk */
#include "locator_sr.h"     /* locator_ functions */
#include "replication_common.hpp"
#include "replication_master_node.hpp"
#include "replication_master_senders_manager.hpp"
#include "replication_node.hpp"
#include "replication_node_manager.hpp"
#include "replication_object.hpp"
#include "scan_manager.h"
#include "stream_transfer_sender.hpp"
#include "thread_entry_task.hpp"

namespace cubreplication
{

  int copy_class (cubthread::entry *thread_p, OID &class_oid);

  /* task executing the extraction of the rows from a single heap file */
  class heap_extract_worker_task : public cubthread::entry_task
  {
    public:
      heap_extract_worker_task (source_copy_context &src_copy_ctxt, const OID &class_oid)
        :m_src_copy_ctxt (src_copy_ctxt)
        ,m_class_oid (class_oid)
      {
      }

      void execute (cubthread::entry &thread_ref) final
      {
        int error = NO_ERROR;

        logtb_set_current_tran_index (&thread_ref, m_src_copy_ctxt.get_tran_index ());

        error = copy_class (&thread_ref, m_class_oid);
        if (error != NO_ERROR)
          {
            m_src_copy_ctxt.inc_error_cnt ();
          }

        m_src_copy_ctxt.dec_extract_running_thread ();
      }

    private:
      std::vector<stream_entry *> m_repl_stream_entries;
      source_copy_context &m_src_copy_ctxt;
      OID m_class_oid;
  };

  source_copy_context::source_copy_context ()
  {
    m_tran_index = -1;
    m_error_cnt = 0;
    m_running_extract_threads = 0;
    m_online_replication_start_pos = 0;

    m_stream = NULL;
    /* TODO[replication] : use file for replication copy stream */
    m_stream_file = NULL;
    m_transfer_sender = 0;
    m_heap_extract_workers_pool = NULL;
    m_state = NOT_STARTED;
    m_is_stop = false;


    m_stream = acquire_stream_for_copy ();
    /* TODO : single global pool or a pool for each context ? */
    m_heap_extract_workers_pool =
      cubthread::get_manager ()->create_worker_pool (EXTRACT_HEAP_WORKER_POOL_SIZE, EXTRACT_HEAP_WORKER_POOL_SIZE,
                                                     "replication_extract_heap_workers", NULL, 1, 1);
  }

  source_copy_context::~source_copy_context ()
  {
    cubthread::get_manager ()->destroy_worker_pool (m_heap_extract_workers_pool);
    detach_stream_for_copy ();

    m_stream_file = NULL;
    delete m_transfer_sender;
    m_transfer_sender = NULL;
    m_state = NOT_STARTED;
    m_error_cnt = 0;
  }

  void source_copy_context::pack_and_add_object (row_object* &obj)
  {
    if (obj->get_rec_cnt () == 0)
      {
        delete obj;
        obj = NULL;
	return;
      }

    stream_entry stream_entry (m_stream, MVCCID_FIRST, stream_entry_header::ACTIVE);

    stream_entry.add_packable_entry (obj);
    /* once packed, the object is owned by the stream entry */
    obj = NULL;

    stream_entry.pack ();
  }

  void source_copy_context::pack_and_add_statement (const std::string &str)
  {
    stream_entry stream_entry (m_stream, MVCCID_FIRST, stream_entry_header::ACTIVE);
    LOG_LSA null_LSA;
    LSA_SET_NULL (&null_LSA); 
    
    sbr_repl_entry *sbr = new sbr_repl_entry (str.c_str (), "dba", "", null_LSA);
    stream_entry.add_packable_entry (sbr);

    stream_entry.pack ();
  }

  void source_copy_context::pack_and_add_start_of_extract_heap (void)
  {
    stream_entry stream_entry (m_stream, MVCCID_FIRST, stream_entry_header::START_OF_EXTRACT_HEAP);

    stream_entry.pack ();
  }

  void source_copy_context::pack_and_add_end_of_extract_heap (void)
  {
    stream_entry stream_entry (m_stream, MVCCID_FIRST, stream_entry_header::END_OF_EXTRACT_HEAP);

    stream_entry.pack ();
  }

  void source_copy_context::pack_and_add_end_of_copy (void)
  {
    stream_entry stream_entry (m_stream, MVCCID_FIRST, stream_entry_header::END_OF_REPLICATION_COPY);

    stream_entry.pack ();
  }

  int source_copy_context::execute_and_transit_phase (copy_stage new_state)
  {
    int error = NO_ERROR;

    assert (new_state != m_state);

    er_log_debug_replication (ARG_FILE_LINE, "source_copy_context::execute_and_transit_phase "
                              "curr_state:%d, new_state:%d", m_state, new_state);

    std::unique_lock<std::mutex>  ulock_state (m_state_mutex);

    if ((int) m_state != ((int) new_state - 1))
      {
	return ER_FAILED;
      }

    m_state = new_state;

    ulock_state.unlock ();

    /* some of the new states require specific actions */
    if (new_state == SCHEMA_APPLY_CLASSES_FINISHED)
      {
	pack_and_add_statement (m_class_schema);
      }
    else if (new_state == SCHEMA_APPLY_TRIGGERS)
      {
	pack_and_add_statement (m_triggers);
      }
    else if (new_state == SCHEMA_APPLY_INDEXES)
      {
	pack_and_add_statement (m_indexes);
      }

    m_state_cv.notify_one ();

    return error;
  }

  int source_copy_context::wait_for_state (const copy_stage &desired_state)
  {
    er_log_debug_replication (ARG_FILE_LINE, "source_copy_context::wait_for_state %d", desired_state);
    std::unique_lock<std::mutex> ulock_state (m_state_mutex);
    m_state_cv.wait (ulock_state, [this, desired_state] { return m_state == desired_state || m_is_stop;});
    if (m_is_stop)
      {
        return ER_INTERRUPTED;
      }
    return NO_ERROR;
  }

  int source_copy_context::wait_receive_class_list (void)
  {
    return wait_for_state (SCHEMA_CLASSES_LIST_FINISHED);
  }

  int source_copy_context::wait_send_triggers_indexes (void)
  {
    return wait_for_state (SCHEMA_APPLY_INDEXES);
  }

  int source_copy_context::get_tran_index (void)
  {
    return m_tran_index;
  }

  void source_copy_context::inc_error_cnt (void)
  {
    m_error_cnt++;
  }

  void source_copy_context::append_class_schema (const char *buffer, const size_t buf_size)
  {
    m_class_schema.append (buffer, buf_size);
  }

  void source_copy_context::append_triggers_schema (const char *buffer, const size_t buf_size)
  {
    m_triggers.append (buffer, buf_size);
  }

  void source_copy_context::append_indexes_schema (const char *buffer, const size_t buf_size)
  {
    m_indexes.append (buffer, buf_size);
  }

  void source_copy_context::unpack_class_oid_list (const char *buffer, const size_t buf_size)
  {
    cubpacking::unpacker unpacker (buffer, buf_size);

    int class_oid_cnt;
    unpacker.unpack_all (class_oid_cnt);

    for (int i = 0; i < class_oid_cnt; i++)
      {
        m_class_oid_list.emplace_back ();
        OID &class_oid = m_class_oid_list.back ();
        unpacker.unpack_all (class_oid);
      }
  }

  /*
   * acquire_stream_for_copy : creates or reutilizes a stream for providing copy replication data 
   *
   * TODO[replication] : if a second slave asks for replication db copy in a short interval we may reuse an existing stream
   * instead of start another copy process
   */
  cubstream::multi_thread_stream *source_copy_context::acquire_stream_for_copy ()
  {
    INT64 buffer_size = prm_get_bigint_value (PRM_ID_REPL_BUFFER_SIZE);

    /* TODO[replication] : here, we create a new stream for each new slave copy, but as an optimization,
     * we should reuse a db copy stream which is recent enough (for slave doesn't matter) */

    /* TODO[replication] : max appenders in stream must be greater than number of parallel heap scanners */
    cubstream::multi_thread_stream *copy_db_stream =
      new cubstream::multi_thread_stream (buffer_size, 10 + EXTRACT_HEAP_WORKER_POOL_SIZE);
    const node_definition *myself = replication_node_manager::get_master_node ()->get_node_identity ();
    copy_db_stream->set_name ("repl_copy_" + std::string (myself->get_hostname ().c_str ()));
    copy_db_stream->set_trigger_min_to_read_size (stream_entry::compute_header_size ());
    copy_db_stream->init (0);

    return copy_db_stream;
  }

  void source_copy_context::detach_stream_for_copy ()
  {
    m_stream->stop ();
    delete m_stream;
    m_stream = NULL;
  }


  void source_copy_context::execute_db_copy (cubthread::entry &thread_ref, SOCKET fd)
  {
    cubcomm::channel chn;
    chn.set_channel_name (REPL_COPY_CHANNEL_NAME);

    LOG_TDES *tdes = LOG_FIND_CURRENT_TDES (&thread_ref);
    assert (tdes != NULL);
    if (tdes != NULL)
      {
        m_tran_index = tdes->tran_index;
      }

    css_error_code rc = chn.accept (fd);
    assert (rc == NO_ERRORS);

    if (setup_copy_protocol (chn) != NO_ERROR)
      {
        return;
      }

    m_transfer_sender = new cubstream::transfer_sender (std::move (chn), *m_stream);

    er_log_debug_replication (ARG_FILE_LINE, "new_slave_copy connected");

    /* extraction process : schema phase : start the client process */
    locator_repl_extract_schema (&thread_ref, "dba", "");

    if (wait_receive_class_list () == ER_INTERRUPTED)
      {
        return;
      }

    /* extraction process : heap extract phase */
    execute_and_transit_phase (HEAP_COPY);
    pack_and_add_start_of_extract_heap ();
    /* TODO[replication]: we may optimize this to have multiple threads for larger heaps */
    for (const OID class_oid : m_class_oid_list)
      {
        heap_extract_worker_task *task = new heap_extract_worker_task (*this, class_oid);
        inc_extract_running_thread ();
        cubthread::get_manager ()->push_task (m_heap_extract_workers_pool, task);
      }
        
    /* wait for all heap extract threads to end */
    while (get_extract_running_thread () > 0)
      {
        if (m_is_stop)
          {
            return;
          }
        thread_sleep (10);
      }
    execute_and_transit_phase (HEAP_COPY_FINISHED);
    pack_and_add_end_of_extract_heap ();

    execute_and_transit_phase (SCHEMA_APPLY_TRIGGERS);
    execute_and_transit_phase (SCHEMA_APPLY_INDEXES);
    /* wait for indexes and triggers schema */
    if (wait_send_triggers_indexes () == ER_INTERRUPTED)
      {
        return;
      }

    pack_and_add_end_of_copy ();

    if (wait_slave_receive_ack (chn) != NO_ERROR)
      {
        return;
      }
  }

  int source_copy_context::setup_copy_protocol (cubcomm::channel &chn)
  {
    UINT64 pos = 0, expected_magic;
    std::size_t max_len = sizeof (UINT64);
    cubstream::stream_position online_repl_pos;

    if (chn.recv ((char *) &expected_magic, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    if (expected_magic != replication_node::SETUP_COPY_REPLICATION_MAGIC)
      {
        er_log_debug_replication (ARG_FILE_LINE, "source_copy_context::setup_copy_protocol error in setup protocol");
        assert (false);
        return ER_FAILED;
      }

    if (chn.send ((char *) &replication_node::SETUP_COPY_REPLICATION_MAGIC, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    /* TODO */
    online_repl_pos = m_online_replication_start_pos;
    pos = htoni64 (online_repl_pos);
    if (chn.send ((char *) &pos, max_len) !=  css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }
    er_log_debug_replication (ARG_FILE_LINE, "source_copy_context::setup_copy_protocol online_repl_pos:%lld",
                              online_repl_pos);

    return NO_ERROR;
  }

  int source_copy_context::wait_slave_receive_ack (cubcomm::channel &chn)
  {
    UINT64 pos = 0, expected_magic;
    std::size_t max_len = sizeof (UINT64);

    if (chn.recv ((char *) &expected_magic, max_len) != css_error_code::NO_ERRORS)
      {
        return ER_FAILED;
      }

    if (expected_magic != replication_node::SETUP_COPY_END_REPLICATION_MAGIC)
      {
        er_log_debug_replication (ARG_FILE_LINE, "source_copy_context::setup_copy_protocol error in setup protocol");
        assert (false);
        return ER_FAILED;
      }
    er_log_debug_replication (ARG_FILE_LINE, "source_copy_context::wait_slave_receive_ack OK");

    return NO_ERROR;
  }

  void source_copy_context::stop ()
    {
      m_is_stop = true;
      m_state_cv.notify_all();
    }

  /* 
   * utility functions for source server extraction of database replication copy 
   *
   */

  int convert_to_last_representation (cubthread::entry *thread_p, RECDES &rec_des, record_descriptor &record,
				      const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info);
  /*
   * create_scan_for_replication_copy - creates a HEAP SCAN to be used by replication copy (no regu variables)
   *
   * thread_p (in):
   * s_id (s_id/out):
   * class_oid (in):
   * class_hfid (in):
   */
  static int
  create_scan_for_replication_copy (cubthread::entry *thread_p, SCAN_ID &s_id, OID &class_oid, HFID &class_hfid,
                                    HEAP_CACHE_ATTRINFO * cache_pred, HEAP_CACHE_ATTRINFO * cache_rest)
  {
    /* TODO : read lock is required by an assertion in heap_scan_pb_lock_and_fetch */
    const bool mvcc_select_lock_needed = true;
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
				 cache_pred, /* cache_pred */
				 0, /* num_attrs_rest */
				 NULL, /* attrids_rest*/
				 cache_rest, /* cache_rest */
				 scan_type,
				 NULL, /* cache_recordinfo */
				 NULL /* regu_list_recordinfo*/
				);

    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error;
      }

    error = scan_start_scan (thread_p, &s_id);

    if (error != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error;
      }

    return error;
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
    HEAP_CACHE_ATTRINFO dummy_cache_pred;
    HEAP_CACHE_ATTRINFO dummy_cache_rest;
    HFID class_hfid;
    bool attr_info_inited = false;
    int error_code = NO_ERROR;
    char *class_name = NULL;
    LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

    assert (tdes != NULL);
    assert (tdes->replication_copy_context != NULL);

    error_code = heap_get_class_name (thread_p, &class_oid, &class_name);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	return error_code;
      }

    row_object *heap_objects = new row_object (class_name);

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

    /* TODO[replication] : we may use some filters on rows;
     * for this reason we use a generic SCAN_ID which allows filter predicates instead of a low level heap scan
     * for now, we just use dummy attribute caches which should be initialized to empty values by scan_start_scan */
    error_code = create_scan_for_replication_copy (thread_p, s_id, class_oid, class_hfid,
                                                   &dummy_cache_pred, &dummy_cache_rest);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	goto end;
      }

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
	heap_objects->move_record (std::move (record));

	if (heap_objects->is_pack_needed ())
	  {
	    /* pack and add to stream */
	    tdes->replication_copy_context->pack_and_add_object (heap_objects);
	    assert (heap_objects == NULL);
            heap_objects = new row_object (class_name);
	  }
      }
    while (1);

    tdes->replication_copy_context->pack_and_add_object (heap_objects);

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
