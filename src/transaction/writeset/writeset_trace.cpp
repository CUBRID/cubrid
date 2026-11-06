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

/*
 * writeset_trace.cpp - writeset diagnostic trace
 */

#ident "$Id$"

#include "config.h"

#include "writeset_trace.hpp"
#include "writeset_config.hpp"

#include "error_manager.h"
#include "log_impl.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if defined(SERVER_MODE) || defined(SA_MODE)

bool
wset_trace_is_enabled (int level)
{
  return wset_config_get_trace_level () >= level;
}

const char *
wset_trace_kind_name (LOG_WSET_KIND kind)
{
  return kind == LOG_WSET_KIND_WRITE ? "WRITE" : "REF";
}

void
wset_trace_key_source (LOG_TDES *tdes, const char *operation, const char *image, LOG_WSET_KIND kind,
		       const OID *table_oid, const OID *row_oid, const char *table_name, const char *constraint_name,
		       const OID *key_class_oid, const VFID *key_index_vfid)
{
  if (!wset_trace_is_enabled (3) || tdes == NULL)
    {
      return;
    }

  WSET_TRACE (3,
	      "SOURCE trid=%d operation=%s image=%s kind=%s table=%s table_oid=%d|%d|%d row_oid=%d|%d|%d constraint=%s key_class_oid=%d|%d|%d key_index_vfid=%d|%d\n",
	      tdes->trid, operation, image, wset_trace_kind_name (kind),
	      table_name != NULL ? table_name : "<unknown>", table_oid->volid, table_oid->pageid,
	      table_oid->slotid, row_oid->volid, row_oid->pageid, row_oid->slotid,
	      constraint_name != NULL ? constraint_name : "<unnamed>", key_class_oid->volid,
	      key_class_oid->pageid, key_class_oid->slotid, key_index_vfid->volid, key_index_vfid->fileid);
}

#endif /* SERVER_MODE || SA_MODE */
