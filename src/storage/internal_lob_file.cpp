/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "internal_lob_file.hpp"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "dbtype.h"
#include "error_manager.h"
#include "file_manager.h"
#include "heap_file.h"
#include "internal_lob_marker.h"
#include "lock_manager.h"
#include "log_impl.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "system_parameter.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

int
internal_lob_create_file (THREAD_ENTRY *thread_p, const HFID &heap_hfid, const OID &class_oid, VFID &lob_vfid)
{
  return oos_create_file_with_type (thread_p, FILE_INTERNAL_LOB, heap_hfid, class_oid, lob_vfid);
}

#if defined (CUBRID_UNIT_TEST_ENABLED)
int
internal_lob_create_file (THREAD_ENTRY *thread_p, VFID &lob_vfid)
{
  const HFID test_owner_hfid = { { 1, 1 }, 1 };
  const OID test_owner_class_oid = { 1, 1, 1 };

  return internal_lob_create_file (thread_p, test_owner_hfid, test_owner_class_oid, lob_vfid);
}
#endif /* CUBRID_UNIT_TEST_ENABLED */

int
internal_lob_remove_file (THREAD_ENTRY *thread_p, const VFID &lob_vfid)
{
  return oos_remove_file (thread_p, lob_vfid);
}

