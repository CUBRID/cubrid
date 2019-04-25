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
 * replication_db_copy.cpp
 */

#ident "$Id$"

#include "replication_db_copy.hpp"
#include "heap_attrinfo.h"  /* for HEAP_CACHE_ATTRINFO */
#include "locator_sr.h"
#include "scan_manager.h"

namespace cubreplication
{

  int convert_to_last_representation (cubthread::entry *thread_p, RECDES &rec_des, RECDES &new_rec_des,
				      const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info);
  copy_context::copy_context ()
  {
    m_stream = NULL;
  }

  void copy_context::pack_and_add_object (row_object &obj)
  {
    if (obj.get_rec_cnt () == 0)
      {
	return;
      }

    stream_entry stream_entry (m_stream);

    stream_entry.add_packable_entry (&obj);

    stream_entry.pack ();
  }

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
    char *class_name;
    LOG_TDES *tdes = LOG_FIND_TDES (LOG_FIND_THREAD_TRAN_INDEX (thread_p));

    assert (tdes != NULL);

    error_code = heap_get_class_name (thread_p, &class_oid, &class_name);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    row_object heap_objects (class_name);

    error_code = heap_get_hfid_from_class_oid (thread_p, &class_oid, &class_hfid);
    if (error_code != NO_ERROR)
      {
	goto end;
      }

    error_code = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
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
	    error_code = ER_FAILED;
	    goto end;
	  }

	RECDES new_recdes;
	error_code = convert_to_last_representation (thread_p, s_id.s.hsid.row_recdes, new_recdes,
		     s_id.s.hsid.curr_oid, attr_info);
	if (error_code != NO_ERROR)
	  {
	    goto end;
	  }
	heap_objects.add_copied_recdes (new_recdes);

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
   * rec_des (in/out): recdes be changed converted
   * new_rec_des (in/out): new recdes
   * inst_oid(in): instance OID of record
   * attr_info(in/out): cache attributes storing representations and attribute values
   */
  int convert_to_last_representation (cubthread::entry *thread_p, RECDES &rec_des, RECDES &new_rec_des,
				      const OID &inst_oid, HEAP_CACHE_ATTRINFO &attr_info)
  {
    int error_code = NO_ERROR;
    const int reprid = or_rep_id (&rec_des);
    RECDES new_recdes;
    LC_COPYAREA *copyarea = NULL;

    if (reprid == attr_info.last_classrepr->id)
      {
	new_rec_des = rec_des;
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
    char *recdes_mem;
    recdes_mem = locator_allocate_private_by_attr_info (thread_p, &attr_info, &rec_des, &new_recdes, -1,
		 LOB_FLAG_EXCLUDE_LOB);
    if (recdes_mem == NULL)
      {
	ASSERT_ERROR ();
	error_code = ER_FAILED;
	return error_code;
      }

    return error_code;
  }


} /* namespace cubreplication */
