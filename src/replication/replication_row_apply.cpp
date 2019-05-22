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
#include "thread_manager.hpp"
#include "xserver_interface.h"

namespace cubreplication
{
  static int prepare_scan (const char *classname, OID &class_oid, HEAP_SCANCACHE &scan_cache);

  static int
  prepare_scan (cubthread::entry &thread_ref, const char *classname, OID &class_oid, HEAP_SCANCACHE &scan_cache)
  {
    if (xlocator_find_class_oid (&thread_ref, classname, &class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
      {
	assert (false);
	return ER_FAILED;
      }

    HFID hfid = HFID_INITIALIZER;
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
} // namespace cubreplication
