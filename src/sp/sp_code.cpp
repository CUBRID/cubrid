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
#include "intl_support.h"
#include "locale_support.h"
#include "log_impl.h"
#include "object_representation_sr.h"
#include "oid.h"
#include "schema_system_catalog_constants.h"
#include "sp_constants.hpp"
#include "xserver_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

ATTR_ID spcode_Attrs_id[NUM_SP_CODE_ATTR];
int spcode_Num_attrs = -1;

static int sp_load_sp_code_attribute_info (THREAD_ENTRY *thread_p);
static void sp_code_attr_init ();
static int sp_get_attrid (THREAD_ENTRY *thread_p, int attr_index, ATTR_ID &attrid);
static int sp_get_attr_idx (const std::string &attr_name);

using sp_code_attr_map_type = std::unordered_map <std::string, int>;
static sp_code_attr_map_type attr_idx_map;

static void
sp_code_attr_init ()
{
#define MAP_LIST_ITEM(item)     attr_idx_map [SP_CODE_ATTR_##item] = INDEX_SP_CODE_ATTR_##item;
  SP_CODE_ATTR_LIST
#undef MAP_LIST_ITEM
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
  int error = NO_ERROR;

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

  static const char *sp_code_attr_names[NUM_SP_CODE_ATTR] =
  {
#define MAP_LIST_ITEM(item)     SP_CODE_ATTR_##item,
    SP_CODE_ATTR_LIST
#undef MAP_LIST_ITEM
  };

  for (int attr_idx = 0; attr_idx < NUM_SP_CODE_ATTR; attr_idx++)
    {
      error = or_get_attrid (&class_record, sp_code_attr_names[attr_idx], &spcode_Attrs_id[attr_idx]);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}
      if (spcode_Attrs_id[attr_idx] == NULL_ATTRID)
	{
	  assert (false);
	  error = ER_FAILED;
	  goto exit_on_error;
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

/* the primary key of the stored procedure catalog, on its unique name */
#define SP_UNIQUE_NAME_INDEX "pk_db_stored_procedure_unique_name"

/* attribute ids read by sp_get_exec_rights (), resolved once per server */
enum
{
  SP_RIGHTS_ATTR_SP_NAME,
  SP_RIGHTS_ATTR_DIRECTIVE,
  SP_RIGHTS_ATTR_OWNER,
  SP_RIGHTS_ATTR_TARGET_CLASS,
  SP_RIGHTS_ATTR_TARGET_METHOD,
  SP_RIGHTS_NUM_ATTRS
};
static ATTR_ID sp_Rights_attr_ids[SP_RIGHTS_NUM_ATTRS];
static ATTR_ID sp_User_attr_name = NULL_ATTRID;
static bool sp_Rights_attrs_loaded = false;

/* one catalog entry, as far as sp_get_exec_rights () needs it */
struct sp_rights_entry
{
  int directive;
  OID owner_oid;
};

/*
 * sp_load_attrids () - resolve attribute ids of a catalog class by name.
 */
static int
sp_load_attrids (THREAD_ENTRY *thread_p, OID *class_oid, int num_attrs, const char **names, ATTR_ID *ids)
{
  HEAP_SCANCACHE scan;
  RECDES class_record;
  int i, error = NO_ERROR;

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, class_oid) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (heap_get_class_record (thread_p, class_oid, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      error = ER_FAILED;
    }

  for (i = 0; error == NO_ERROR && i < num_attrs; i++)
    {
      if (or_get_attrid (&class_record, names[i], &ids[i]) != NO_ERROR || ids[i] == NULL_ATTRID)
	{
	  error = ER_FAILED;
	}
    }

  (void) heap_scancache_end (thread_p, &scan);
  return error;
}

/*
 * sp_load_rights_attrids () - resolve the ids sp_get_exec_rights () reads. A race
 *   resolves the same ids twice; they are published only once all are known.
 */
static int
sp_load_rights_attrids (THREAD_ENTRY *thread_p)
{
  static const char *sp_names[SP_RIGHTS_NUM_ATTRS] =
  {
    SP_ATTR_SP_NAME, SP_ATTR_DIRECTIVE, SP_ATTR_OWNER, SP_ATTR_TARGET_CLASS, SP_ATTR_TARGET_METHOD
  };
  static const char *user_names[1] = { "name" };
  ATTR_ID sp_ids[SP_RIGHTS_NUM_ATTRS];
  ATTR_ID user_ids[1];

  if (sp_Rights_attrs_loaded)
    {
      return NO_ERROR;
    }

  if (sp_load_attrids (thread_p, oid_Sp_class_oid, SP_RIGHTS_NUM_ATTRS, sp_names, sp_ids) != NO_ERROR
      || sp_load_attrids (thread_p, oid_User_class_oid, 1, user_names, user_ids) != NO_ERROR)
    {
      return ER_FAILED;
    }

  memcpy (sp_Rights_attr_ids, sp_ids, sizeof (sp_Rights_attr_ids));
  sp_User_attr_name = user_ids[0];
  sp_Rights_attrs_loaded = true;

  return NO_ERROR;
}

/*
 * sp_value_equals_string () - does a string attribute hold exactly this text?
 */
static bool
sp_value_equals_string (const DB_VALUE *value, const char *text)
{
  const char *stored;
  int size;

  if (value == NULL || DB_IS_NULL (value) || !TP_IS_CHAR_TYPE (DB_VALUE_TYPE (value)))
    {
      return false;
    }

  stored = db_get_string (value);
  size = db_get_string_size (value);
  return (stored != NULL && size == (int) strlen (text) && memcmp (stored, text, size) == 0);
}

/*
 * sp_match_entry () - read one catalog record and check it is the procedure looked
 *   for: same target and, when sp_name is given, the same name.
 *   return: true if it matches; entry is filled then
 */
static bool
sp_match_entry (THREAD_ENTRY *thread_p, const OID *sp_oid, RECDES *recdes, HEAP_CACHE_ATTRINFO *attr_info,
		const char *sp_name, const char *target_class, const char *target_method, sp_rights_entry &entry)
{
  DB_VALUE *value;

  if (heap_attrinfo_read_dbvalues (thread_p, sp_oid, recdes, attr_info) != NO_ERROR)
    {
      return false;
    }

  if (!sp_value_equals_string (heap_attrinfo_access (sp_Rights_attr_ids[SP_RIGHTS_ATTR_TARGET_CLASS], attr_info),
			       target_class)
      || !sp_value_equals_string (heap_attrinfo_access (sp_Rights_attr_ids[SP_RIGHTS_ATTR_TARGET_METHOD],
				  attr_info), target_method))
    {
      return false;
    }

  if (sp_name != NULL
      && !sp_value_equals_string (heap_attrinfo_access (sp_Rights_attr_ids[SP_RIGHTS_ATTR_SP_NAME], attr_info),
				  sp_name))
    {
      return false;
    }

  value = heap_attrinfo_access (sp_Rights_attr_ids[SP_RIGHTS_ATTR_DIRECTIVE], attr_info);
  if (value == NULL || DB_IS_NULL (value) || DB_VALUE_TYPE (value) != DB_TYPE_INTEGER)
    {
      return false;
    }
  entry.directive = db_get_int (value);

  value = heap_attrinfo_access (sp_Rights_attr_ids[SP_RIGHTS_ATTR_OWNER], attr_info);
  if (value == NULL || DB_IS_NULL (value) || DB_VALUE_TYPE (value) != DB_TYPE_OID)
    {
      return false;
    }
  COPY_OID (&entry.owner_oid, db_get_oid (value));

  return true;
}

/*
 * sp_find_by_unique_name () - the catalog entry keyed by this unique name, if it
 *   has this target.
 */
static bool
sp_find_by_unique_name (THREAD_ENTRY *thread_p, const char *unique_name, const char *target_class,
			const char *target_method, sp_rights_entry &entry)
{
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES recdes = RECDES_INITIALIZER;
  BTID btid;
  OID sp_oid;
  DB_VALUE key;
  BTREE_SEARCH search;
  bool found = false;

  if (heap_get_index_with_name (thread_p, oid_Sp_class_oid, SP_UNIQUE_NAME_INDEX, &btid) != NO_ERROR
      || BTID_IS_NULL (&btid))
    {
      return false;
    }

  db_make_string (&key, unique_name);
  search = xbtree_find_unique (thread_p, &btid, S_SELECT, &key, oid_Sp_class_oid, &sp_oid, false);
  pr_clear_value (&key);
  if (search != BTREE_KEY_FOUND)
    {
      return false;
    }

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, oid_Sp_class_oid) != NO_ERROR)
    {
      return false;
    }
  scan.mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);

  if (heap_get_visible_version (thread_p, &sp_oid, oid_Sp_class_oid, &recdes, &scan, PEEK, NULL_CHN) == S_SUCCESS
      && heap_attrinfo_start (thread_p, oid_Sp_class_oid, SP_RIGHTS_NUM_ATTRS, sp_Rights_attr_ids,
			      &attr_info) == NO_ERROR)
    {
      found = sp_match_entry (thread_p, &sp_oid, &recdes, &attr_info, NULL, target_class, target_method, entry);
      heap_attrinfo_end (thread_p, &attr_info);
    }

  (void) heap_scancache_end (thread_p, &scan);
  return found;
}

/*
 * sp_find_by_name_and_target () - the catalog entry with this procedure name and
 *   target, for a name that is not the unique one (a call made inside PL/CSQL
 *   names the procedure without its owner). Entries that match but differ in whose
 *   rights they execute with leave the answer open.
 */
static bool
sp_find_by_name_and_target (THREAD_ENTRY *thread_p, const char *sp_name, const char *target_class,
			    const char *target_method, sp_rights_entry &entry)
{
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES recdes = RECDES_INITIALIZER;
  HFID hfid;
  OID class_oid, sp_oid;
  sp_rights_entry candidate;
  bool found = false, ambiguous = false;

  if (heap_get_class_hfid (thread_p, oid_Sp_class_oid, &hfid, NULL) != NO_ERROR || HFID_IS_NULL (&hfid))
    {
      return false;
    }

  if (heap_scancache_start (thread_p, &scan, &hfid, oid_Sp_class_oid, true, logtb_get_mvcc_snapshot (thread_p))
      != NO_ERROR)
    {
      return false;
    }

  if (heap_attrinfo_start (thread_p, oid_Sp_class_oid, SP_RIGHTS_NUM_ATTRS, sp_Rights_attr_ids, &attr_info)
      != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan);
      return false;
    }

  COPY_OID (&class_oid, oid_Sp_class_oid);
  OID_SET_NULL (&sp_oid);
  while (!ambiguous && heap_next (thread_p, &hfid, &class_oid, &sp_oid, &recdes, &scan, PEEK) == S_SUCCESS)
    {
      if (!sp_match_entry (thread_p, &sp_oid, &recdes, &attr_info, sp_name, target_class, target_method, candidate))
	{
	  continue;
	}

      if (!found)
	{
	  entry = candidate;
	  found = true;
	}
      else if (entry.directive != candidate.directive || !OID_EQ (&entry.owner_oid, &candidate.owner_oid))
	{
	  ambiguous = true;
	}
    }

  heap_attrinfo_end (thread_p, &attr_info);
  (void) heap_scancache_end (thread_p, &scan);

  return found && !ambiguous;
}

/*
 * sp_get_user_name () - read the name of an account from the catalog.
 */
static bool
sp_get_user_name (THREAD_ENTRY *thread_p, const OID *user_oid, char *name, int name_size)
{
  HEAP_SCANCACHE scan;
  HEAP_CACHE_ATTRINFO attr_info;
  RECDES recdes = RECDES_INITIALIZER;
  DB_VALUE *value;
  bool attrinfo_started = false, found = false;
  int size;

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, oid_User_class_oid) != NO_ERROR)
    {
      return false;
    }
  scan.mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);

  if (heap_get_visible_version (thread_p, user_oid, oid_User_class_oid, &recdes, &scan, PEEK, NULL_CHN) != S_SUCCESS
      || heap_attrinfo_start (thread_p, oid_User_class_oid, 1, &sp_User_attr_name, &attr_info) != NO_ERROR)
    {
      goto end;
    }
  attrinfo_started = true;

  if (heap_attrinfo_read_dbvalues (thread_p, user_oid, &recdes, &attr_info) != NO_ERROR)
    {
      goto end;
    }

  value = heap_attrinfo_access (sp_User_attr_name, &attr_info);
  if (value != NULL && !DB_IS_NULL (value) && TP_IS_CHAR_TYPE (DB_VALUE_TYPE (value)))
    {
      size = db_get_string_size (value);
      if (size > 0 && size < name_size)
	{
	  memcpy (name, db_get_string (value), size);
	  name[size] = '\0';
	  found = true;
	}
    }

end:
  if (attrinfo_started)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  (void) heap_scancache_end (thread_p, &scan);

  return found;
}

/*
 * sp_get_exec_rights () - whose rights a stored procedure executes with, read
 *   from the catalog rather than from what the client compiled.
 *   return: NO_ERROR, or ER_FAILED if the catalog does not name one procedure with
 *           this name and target
 *   name (in): the procedure as its signature names it: the unique name for a call
 *              from the client, possibly the bare name for a call inside PL/CSQL
 *   target_class, target_method (in): what is executed for it; must match the
 *                                    catalog, so the name and the code agree
 *   is_owner_rights (out): true if it executes with its owner's rights
 *   owner_name (out): the owner's account name
 */
int
sp_get_exec_rights (THREAD_ENTRY *thread_p, const char *name, const char *target_class, const char *target_method,
		    bool *is_owner_rights, char *owner_name, int owner_name_size)
{
  char lower[DB_MAX_IDENTIFIER_LENGTH * 4 * INTL_IDENTIFIER_CASING_SIZE_MULTIPLIER + 1];
  const char *last;
  sp_rights_entry entry;
  bool found;

  if (name == NULL || target_class == NULL || target_method == NULL || OID_ISNULL (oid_Sp_class_oid)
      || sp_load_rights_attrids (thread_p) != NO_ERROR
      || intl_identifier_lower_string_size (name) >= (int) sizeof (lower))
    {
      return ER_FAILED;
    }

  /* unique names are kept in lower case */
  intl_identifier_lower (name, lower);

  found = sp_find_by_unique_name (thread_p, name, target_class, target_method, entry);
  if (!found && strcmp (name, lower) != 0)
    {
      found = sp_find_by_unique_name (thread_p, lower, target_class, target_method, entry);
    }
  if (!found)
    {
      last = strrchr (lower, '.');
      found = sp_find_by_name_and_target (thread_p, (last != NULL) ? last + 1 : lower, target_class, target_method,
					  entry);
    }

  if (!found || !sp_get_user_name (thread_p, &entry.owner_oid, owner_name, owner_name_size))
    {
      return ER_FAILED;
    }

  *is_owner_rights = ((entry.directive & SP_DIRECTIVE_RIGHTS_CALLER) == 0);
  return NO_ERROR;
}
