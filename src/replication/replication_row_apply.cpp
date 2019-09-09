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

//
// Applying row replication
//

#include "replication_row_apply.hpp"

#include "btree.h"                  // SINGLE_ROW_MODIFY
#include "dbtype_def.h"
#include "heap_file.h"
#include "locator_sr.h"
#include "object_representation_sr.h"
#include "perf_monitor.h"
#include "record_descriptor.hpp"
#include "storage_common.h"                   // SCAN_OPERATION_TYPE
#include "thread_manager.hpp"
#include "xserver_interface.h"

namespace cubreplication
{
  static int prepare_scan (cubthread::entry &thread_ref, const std::string &classname, HEAP_SCANCACHE &scan_cache);
  static void overwrite_last_reprid (cubthread::entry &thread_ref, const OID &class_oid, record_descriptor &record);
  static int find_instance_oid (cubthread::entry &thread_ref, const OID &class_oid, const db_value &key_value,
				HEAP_SCANCACHE &scan_cache, SCAN_OPERATION_TYPE op_type, OID &instance_oid);
  static int generate_updated_record (cubthread::entry &thread_ref, const OID &class_oid, const RECDES &old_recdes,
				      record_descriptor &generated_record, const record_descriptor &new_record);
  static int generate_updated_record (cubthread::entry &thread_ref, const OID &class_oid, const RECDES &old_recdes,
				      record_descriptor &generated_record, const std::vector<int> &attr_ids,
				      const std::vector<db_value> &attr_values);
  template <typename ... Args>
  static int row_apply_update_internal (const std::string &classname, const db_value &key_value, Args &&... args);

  int
  row_apply_insert (const std::string &classname, const record_descriptor &record)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    /* monitor */
    // TODO: We need another stat.
    perfmon_inc_stat (&thread_ref, PSTAT_QM_NUM_INSERTS);

    HEAP_SCANCACHE scan_cache;

    int error_code = prepare_scan (thread_ref, classname, scan_cache);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	return error_code;
      }
    assert (!OID_ISNULL (&scan_cache.node.class_oid));
    assert (!HFID_IS_NULL (&scan_cache.node.hfid));

    record_descriptor record_copy (record.get_recdes ());
    // record may be resized by heap_update_adjust_recdes_header; make sure there is enough space.
    heap_record_reserve_for_adjustments (record_copy);

    overwrite_last_reprid (thread_ref, scan_cache.node.class_oid, record_copy);

    OID oid_out = OID_INITIALIZER;
    RECDES recdes_copy = record_copy.get_recdes ();

    log_sysop_start (&thread_ref);
    // todo: pruning
    error_code = locator_insert_record (thread_ref, scan_cache, recdes_copy, oid_out);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	log_sysop_abort (&thread_ref);
      }
    else
      {
	log_sysop_attach_to_outer (&thread_ref);
      }

    heap_scancache_end_modify (&thread_ref, &scan_cache);

    return error_code;
  }

  int
  row_apply_delete (const std::string &classname, const db_value &key_value)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    /* monitor */
    // TODO: We need another stat.
    perfmon_inc_stat (&thread_ref, PSTAT_QM_NUM_DELETES);

    HEAP_SCANCACHE scan_cache;
    OID instance_oid = OID_INITIALIZER;

    int error_code = prepare_scan (thread_ref, classname, scan_cache);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	return error_code;
      }
    assert (!OID_ISNULL (&scan_cache.node.class_oid));
    assert (!HFID_IS_NULL (&scan_cache.node.hfid));

    error_code = find_instance_oid (thread_ref, scan_cache.node.class_oid, key_value, scan_cache, S_DELETE, instance_oid);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	heap_scancache_end_modify (&thread_ref, &scan_cache);
	return error_code;
      }

    log_sysop_start (&thread_ref);
    error_code = locator_delete_record (thread_ref, scan_cache, instance_oid);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	log_sysop_abort (&thread_ref);
      }
    else
      {
	log_sysop_attach_to_outer (&thread_ref);
      }

    heap_scancache_end_modify (&thread_ref, &scan_cache);
    return NO_ERROR;
  }

  template <typename ... Args>
  static int
  row_apply_update_internal (const std::string &classname, const db_value &key_value, Args &&... args)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    /* monitor */
    // TODO: We need another stat.
    perfmon_inc_stat (&thread_ref, PSTAT_QM_NUM_DELETES);

    HEAP_SCANCACHE scan_cache;
    OID instance_oid = OID_INITIALIZER;

    int error_code = prepare_scan (thread_ref, classname, scan_cache);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	return error_code;
      }
    assert (!OID_ISNULL (&scan_cache.node.class_oid));
    assert (!HFID_IS_NULL (&scan_cache.node.hfid));

    error_code = find_instance_oid (thread_ref, scan_cache.node.class_oid, key_value, scan_cache, S_DELETE, instance_oid);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	heap_scancache_end_modify (&thread_ref, &scan_cache);
	return error_code;
      }

    RECDES old_recdes;
    if (heap_get_visible_version (&thread_ref, &instance_oid, &scan_cache.node.class_oid, &old_recdes, &scan_cache,
				  PEEK, NULL_CHN) != S_SUCCESS)
      {
	// todo - reactivate: assert (false);
	heap_scancache_end_modify (&thread_ref, &scan_cache);
	return ER_FAILED;
      }

    // copy new recdes
    record_descriptor generated_record;
    error_code = generate_updated_record (thread_ref, scan_cache.node.class_oid, old_recdes, generated_record,
					  std::forward<Args> (args)...);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	heap_scancache_end_modify (&thread_ref, &scan_cache);
	return ER_FAILED;
      }

    // record may be resized by heap_update_adjust_recdes_header; make sure there is enough space.
    heap_record_reserve_for_adjustments (generated_record);

    log_sysop_start (&thread_ref);
    RECDES new_recdes = generated_record.get_recdes ();
    error_code = locator_update_record (thread_ref, scan_cache, instance_oid, old_recdes, new_recdes, true);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	log_sysop_abort (&thread_ref);
      }
    else
      {
	log_sysop_attach_to_outer (&thread_ref);
      }

    heap_scancache_end_modify (&thread_ref, &scan_cache);
    return NO_ERROR;
  }

  int
  row_apply_update (const std::string &classname, const db_value &key_value, const std::vector<int> &attr_ids,
		    const std::vector<db_value> &attr_values)
  {
    return row_apply_update_internal (classname, key_value, attr_ids, attr_values);
  }

  int
  row_apply_update (const std::string &classname, const db_value &key_value, const record_descriptor &record)
  {
    return row_apply_update_internal (classname, key_value, record);
  }

  static int
  prepare_scan (cubthread::entry &thread_ref, const std::string &classname, HEAP_SCANCACHE &scan_cache)
  {
    OID class_oid;
    HFID hfid;

    if (xlocator_find_class_oid (&thread_ref, classname.c_str (), &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
      {
	// todo - reactivate: assert (false);
	return ER_FAILED;
      }

    int error_code = heap_get_class_info (&thread_ref, &class_oid, &hfid, NULL, NULL);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false); // can we expect errors? e.g. interrupt
	return error_code;
      }

    error_code = heap_scancache_start_modify (&thread_ref, &scan_cache, &hfid, &class_oid, SINGLE_ROW_MODIFY, NULL);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	return error_code;
      }

    return NO_ERROR;
  }

  static void
  overwrite_last_reprid (cubthread::entry &thread_ref, const OID &class_oid, record_descriptor &record)
  {
    int last_reprid = heap_get_class_repr_id (&thread_ref, &class_oid);
    if (last_reprid == 0)
      {
	// todo - reactivate: assert (false);
	return;
      }

    or_set_repid (record.get_data_for_modify (), last_reprid);
  }

  static int
  find_instance_oid (cubthread::entry &thread_ref, const OID &class_oid, const db_value &key_value,
		     HEAP_SCANCACHE &scan_cache, SCAN_OPERATION_TYPE op_type, OID &instance_oid)
  {
    BTID pkey_btid;
    int error_code = btree_get_pkey_btid (&thread_ref, &class_oid, &pkey_btid);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	return error_code;
      }

    if (BTID_IS_NULL (&pkey_btid))
      {
	// todo - reactivate: assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_OBJECT_NOT_FOUND, 0);
	return ER_OBJ_OBJECT_NOT_FOUND;
      }

    if (xbtree_find_unique (&thread_ref, &pkey_btid, op_type,
			    const_cast<db_value *> (&key_value) /* todo: fix xbtree_find_unique signature */,
			    &class_oid, &instance_oid, true) != BTREE_KEY_FOUND)
      {
	// todo - reactivate: assert (false);
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_OBJECT_NOT_FOUND, 0);
	return ER_OBJ_OBJECT_NOT_FOUND;
      }
    return NO_ERROR;
  }

  int
  generate_updated_record (cubthread::entry &thread_ref, const OID &class_oid, const RECDES &old_recdes,
			   record_descriptor &generated_record, const record_descriptor &new_record)
  {
    generated_record.set_recdes (new_record.get_recdes ());
    overwrite_last_reprid (thread_ref, class_oid, generated_record);
    return or_replace_chn (const_cast<RECDES *> (&generated_record.get_recdes ()), or_chn (&old_recdes) + 1);
  }

  int
  generate_updated_record (cubthread::entry &thread_ref, const OID &class_oid, const RECDES &old_recdes,
			   record_descriptor &generated_record, const std::vector<int> &attr_ids,
			   const std::vector<db_value> &attr_values)
  {
    HEAP_CACHE_ATTRINFO attr_info;

    int error_code = heap_attrinfo_start (&thread_ref, &class_oid, -1, NULL, &attr_info);
    if (error_code != NO_ERROR)
      {
	// todo - reactivate: assert (false);
	return error_code;
      }

    assert (attr_ids.size () == attr_values.size ());
    for (size_t i = 0; i < attr_ids.size (); i++)
      {
	error_code = heap_attrinfo_set (NULL, attr_ids[i], &attr_values[i], &attr_info);
	if (error_code != NO_ERROR)
	  {
	    // todo - reactivate: assert (false);
	    return error_code;
	  }
      }

    if (heap_attrinfo_transform_to_disk (&thread_ref, &attr_info,
					 const_cast<RECDES *> (&old_recdes) /* fix heap_attrinfo_transform_to_disk */,
					 &generated_record) != S_SUCCESS)
      {
	// todo - reactivate: assert (false);
	error_code = ER_FAILED;
      }

    heap_attrinfo_end (&thread_ref, &attr_info);
    return error_code;
  }

} // namespace cubreplication
