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
#include "thread_manager.hpp"
#include "xserver_interface.h"

namespace cubreplication
{
  static int prepare_scan (cubthread::entry &thread_ref, const std::string &classname, HEAP_SCANCACHE &scan_cache);
  static void overwrite_last_reprid (cubthread::entry &thread_ref, const OID &class_oid, RECDES &recdes);

  int
  row_apply_insert (const std::string &classname, const db_value &value, const record_descriptor &record)
  {
    cubthread::entry &thread_ref = cubthread::get_entry ();
    /* monitor */
    // TODO: We need another stat.
    perfmon_inc_stat (&thread_ref, PSTAT_QM_NUM_INSERTS);

    HEAP_SCANCACHE scan_cache;

    int error_code = prepare_scan (thread_ref, classname, scan_cache);
    if (error_code != NO_ERROR)
      {
	assert (false);
	return error_code;
      }
    assert (!OID_ISNULL (&scan_cache.node.class_oid));
    assert (!HFID_IS_NULL (&scan_cache.node.hfid));

    record_descriptor record_copy (record.get_recdes ());
    RECDES recdes_copy = record_copy.get_recdes ();

    overwrite_last_reprid (thread_ref, scan_cache.node.class_oid, recdes_copy);

    OID oid_out = OID_INITIALIZER;
    // todo: pruning
    error_code = locator_insert_record (thread_ref, scan_cache, recdes_copy, oid_out);
    assert (error_code == NO_ERROR);

    heap_scancache_end_modify (&thread_ref, &scan_cache);

    return error_code;
  }

  static int
  prepare_scan (cubthread::entry &thread_ref, const std::string &classname, HEAP_SCANCACHE &scan_cache)
  {
    OID class_oid;
    HFID hfid;

    if (xlocator_find_class_oid (&thread_ref, classname.c_str (), &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
      {
	assert (false);
	return ER_FAILED;
      }

    int error_code = heap_get_hfid_from_class_oid (&thread_ref, &class_oid, &hfid);
    if (error_code != NO_ERROR)
      {
	assert (false); // can we expect errors? e.g. interrupt
	return error_code;
      }

    error_code = heap_scancache_start_modify (&thread_ref, &scan_cache, &hfid, &class_oid, SINGLE_ROW_MODIFY, NULL);
    if (error_code != NO_ERROR)
      {
	assert (false);
	return error_code;
      }

    return NO_ERROR;
  }

  static void overwrite_last_reprid (cubthread::entry &thread_ref, const OID &class_oid, RECDES &recdes)
  {
    int last_reprid = heap_get_class_repr_id (&thread_ref, &class_oid);
    if (last_reprid == 0)
      {
	assert (false);
	return;
      }

    if (or_replace_rep_id (&recdes, last_reprid) != NO_ERROR)
      {
	// never happens; we should make or_replace_rep_id return void
	assert (false);
	return;
      }
  }
} // namespace cubreplication
