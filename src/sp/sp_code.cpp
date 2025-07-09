/*
 *
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

//
// sp_code.cpp
//

#include "sp_code.hpp"

#include <unordered_map>
#include <string>

#include "dbtype.h"
#include "heap_file.h"
#include "object_representation_sr.h"
#include "sp_constants.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

ATTR_ID spcode_Attrs_id[SPC_ATTR_MAX_INDEX];
int spcode_Num_attrs = -1;

static int sp_load_sp_code_attribute_info (THREAD_ENTRY *thread_p);
static void sp_code_attr_init ();
static int sp_get_attrid (THREAD_ENTRY *thread_p, int attr_index, ATTR_ID &attrid);
static int sp_get_attr_idx (const std::string &attr_name);

using sp_code_attr_map_type = std::unordered_map <std::string, SP_CODE_ATTRIBUTES>;
static sp_code_attr_map_type attr_idx_map;

static void
sp_code_attr_init ()
{
  attr_idx_map [SP_ATTR_CLS_NAME] = SPC_ATTR_NAME_INDEX;
  attr_idx_map [SP_ATTR_TIMESTAMP] = SPC_ATTR_CREATED_TIME;
  attr_idx_map [SP_ATTR_OWNER] = SPC_ATTR_OWNER_INDEX;
  attr_idx_map [SP_ATTR_IS_STATIC] = SPC_ATTR_IS_STATIC_INDEX;
  attr_idx_map [SP_ATTR_IS_SYSTEM_GENERATED] = SPC_ATTR_IS_SYSTEM_GENERATED_INDEX;
  attr_idx_map [SP_ATTR_SOURCE_TYPE] = SPC_ATTR_STYPE_INDEX;
  attr_idx_map [SP_ATTR_SOURCE_CODE] = SPC_ATTR_SCODE_INDEX;
  attr_idx_map [SP_ATTR_OBJECT_TYPE] = SPC_ATTR_OTYPE_INDEX;
  attr_idx_map [SP_ATTR_OBJECT_CODE] = SPC_ATTR_OCODE_INDEX;
}

static int
sp_get_attr_idx (const std::string &attr_name)
{
  if (attr_idx_map.size () == 0)
    {
      sp_code_attr_init ();
    }

  auto idx_it = attr_idx_map.find (attr_name);
  if (idx_it == attr_idx_map.end ())
    {
      return -1;
    }
  else
    {
      return idx_it->second;
    }
}

int
sp_get_code_attr (THREAD_ENTRY *thread_p, const std::string &attr_name, const OID *sp_oidp, DB_VALUE *result)
{
  int ret = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  RECDES recdesc = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  ATTR_ID attrid;
  DB_VALUE *cur_val;
  OID *sp_class_oid = oid_Sp_code_class_oid;
  int idx = -1;

  heap_scancache_quick_start_with_class_oid (thread_p, &scan_cache, sp_class_oid);
  /* get record into record desc */
  scan = heap_get_visible_version (thread_p, sp_oidp, sp_class_oid, &recdesc, &scan_cache, PEEK, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, sp_oidp->volid, sp_oidp->pageid,
		  sp_oidp->slotid);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL, 0);
	}
      goto exit_on_error;
    }

  /* retrieve attribute */
  idx = sp_get_attr_idx (attr_name);
  if (idx == -1)
    {
      goto exit_on_error;
    }

  if (sp_get_attrid (thread_p, idx, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  assert (attrid != -1);

  ret = heap_attrinfo_start (thread_p, sp_class_oid, 1, &attrid, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  attr_info_p = &attr_info;

  ret = heap_attrinfo_read_dbvalues (thread_p, sp_oidp, &recdesc, attr_info_p);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  cur_val = heap_attrinfo_access (attrid, attr_info_p);

  db_value_clone (cur_val, result);

  heap_attrinfo_end (thread_p, attr_info_p);

  heap_scancache_end (thread_p, &scan_cache);

  return NO_ERROR;

exit_on_error:

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  heap_scancache_end (thread_p, &scan_cache);

  ret = (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
  return ret;
}

static int
sp_get_attrid (THREAD_ENTRY *thread_p, int attr_index, ATTR_ID &attrid)
{
  attrid = -1; // NOT FOUND

  if (spcode_Num_attrs < 0)
    {
      int error = sp_load_sp_code_attribute_info (thread_p);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
    }

  if (attr_index >= 0 && attr_index <= spcode_Num_attrs)
    {
      attrid = spcode_Attrs_id[attr_index];
    }
  return NO_ERROR;
}

static int
sp_load_sp_code_attribute_info (THREAD_ENTRY *thread_p)
{
  HEAP_SCANCACHE scan;
  RECDES class_record;
  HEAP_CACHE_ATTRINFO attr_info;
  int i, error = NO_ERROR;
  char *attr_name_p, *string = NULL;
  int alloced_string = 0;
  int attr_idx = -1;

  if (spcode_Num_attrs != -1)
    {
      // already retrived
      return error;
    }

  OID *sp_code_oid_class = oid_Sp_code_class_oid;
  spcode_Num_attrs = -1;

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, sp_code_oid_class) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (heap_get_class_record (thread_p, sp_code_oid_class, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan);
      return ER_FAILED;
    }

  error = heap_attrinfo_start (thread_p, sp_code_oid_class, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan);
      return error;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      string = NULL;
      alloced_string = 0;

      error = or_get_attrname (&class_record, i, &string, &alloced_string);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      attr_name_p = string;
      if (attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit_on_error;
	}

      attr_idx = sp_get_attr_idx (attr_name_p);
      if (attr_idx != -1)
	{
	  spcode_Attrs_id [attr_idx] = i;
	}

      if (string != NULL && alloced_string)
	{
	  db_private_free_and_init (NULL, string);
	}

    }
  spcode_Num_attrs = attr_info.num_values;

  heap_attrinfo_end (thread_p, &attr_info);
  error = heap_scancache_end (thread_p, &scan);

  return error;

exit_on_error:

  heap_attrinfo_end (thread_p, &attr_info);
  (void) heap_scancache_end (thread_p, &scan);

  return error;
}
