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
 * inner_table.cpp - Inner Table POC, see inner_table.hpp
 *
 * VARBIT values move the same way: a large VARBIT is 0xFF, int bit_length, then the bits.
 *
 * Reference format. A large VARCHAR is written as byte 0xFF, int compressed_len, int decompressed_len, then the
 * payload; compressed_len is never negative. A reference is byte 0xFF, int -1, int decompressed_len, bigint value_id,
 * int total_len (the bytes of the original encoding) and int chunk_count, padded to INNER_REF_SIZE. The chunks hold
 * the original encoding bytes, so expansion restores the record byte for byte.
 */

#include "inner_table.hpp"

#include "btree.h"
#include "dbtype.h"
#include "error_manager.h"
#include "heap_file.h"
#include "lock_manager.h"
#include "locator_sr.h"
#include "log_impl.h"
#include "mvcc.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "system_parameter.h"
#include "xserver_interface.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory_wrapper.hpp"

namespace
{
  const int INNER_REF_SIZE = 28;
  const int INNER_REF_MARK = -1;
  const char *const INNER_SUFFIX = "__inner";

  struct companion
  {
    bool opted = false;
    OID class_oid = OID_INITIALIZER;
    HFID hfid = HFID_INITIALIZER;
    BTID btid = BTID_INITIALIZER;
    ATTR_ID key_attr = -1;
    ATTR_ID chunk_attr = -1;
    std::mutex seed_mutex;
    bool seeded = false;
    std::atomic<INT64> next_id { 1 };
  };

  struct oid_key
  {
    int pageid, slotid, volid;
    bool operator== (const oid_key &o) const
    {
      return pageid == o.pageid && slotid == o.slotid && volid == o.volid;
    }
  };

  struct oid_key_hash
  {
    size_t operator() (const oid_key &k) const
    {
      return std::hash<long long> () (((long long) k.pageid << 24) ^ ((long long) k.volid << 8) ^ k.slotid);
    }
  };

  std::mutex classes_mutex;
  std::unordered_map<oid_key, std::shared_ptr<companion>, oid_key_hash> classes;

  /* Inside inner_table_collect_refs, fetches return records with their references. */
  thread_local int raw_depth = 0;

  /* What the last expansion of this thread found in the record of LAST_OID. A writer that fetched the old version
   * just before can skip reading it again to collect references. */
  thread_local OID last_oid = OID_INITIALIZER;
  thread_local bool last_had_refs = true;

  void remember (const OID *oid, bool had_refs)
  {
    if (oid != NULL)
      {
	last_oid = *oid;
	last_had_refs = had_refs;
      }
    else
      {
	OID_SET_NULL (&last_oid);
      }
  }

  /* A reference starts with 0xFF and the int -1; a record without these five bytes has no reference. */
  bool may_hold_reference (const RECDES *recdes)
  {
    static const char mark[5] = { (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF, (char) 0xFF };
    return memmem (recdes->data, recdes->length, mark, sizeof (mark)) != NULL;
  }

  struct ref
  {
    ATTR_ID attr_id;
    INT64 value_id;
    int decompressed_len;
    int total_len;
    int chunk_count;
    bool kept;
  };

  /* A record's variable values, as offsets relative to the variable offset table. */
  struct layout
  {
    int header_size = 0;
    int offset_size = 0;
    int n_var = 0;
    std::vector<int> off;	/* n_var + 1 offsets */
    std::vector<ATTR_ID> string_attr;	/* attribute id of each VARCHAR variable value, or -1 */
  };

  int chunk_bytes ()
  {
    return DB_PAGESIZE / 2 - 256;
  }

  int set_error (const char *what)
  {
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UNEXPECTED, 1, what);
    return ER_UNEXPECTED;
  }

  bool is_large_string (const char *p, int len)
  {
    return len >= 1 + 2 * OR_INT_SIZE && (unsigned char) p[0] == 0xFF;
  }

  bool is_reference (const char *p, int len)
  {
    return len >= INNER_REF_SIZE && is_large_string (p, len) && OR_GET_INT (p + 1) == INNER_REF_MARK;
  }

  void put_reference (char *p, const ref &r)
  {
    memset (p, 0, INNER_REF_SIZE);
    p[0] = (char) 0xFF;
    OR_PUT_INT (p + 1, INNER_REF_MARK);
    OR_PUT_INT (p + 5, r.decompressed_len);
    OR_PUT_INT (p + 9, (int) (r.value_id >> 32));
    OR_PUT_INT (p + 13, (int) (r.value_id & 0xFFFFFFFF));
    OR_PUT_INT (p + 17, r.total_len);
    OR_PUT_INT (p + 21, r.chunk_count);
  }

  ref get_reference (const char *p, ATTR_ID attr_id)
  {
    ref r;
    r.attr_id = attr_id;
    r.decompressed_len = OR_GET_INT (p + 5);
    r.value_id = ((INT64) OR_GET_INT (p + 9) << 32) | (INT64) (unsigned int) OR_GET_INT (p + 13);
    r.total_len = OR_GET_INT (p + 17);
    r.chunk_count = OR_GET_INT (p + 21);
    r.kept = false;
    return r;
  }

  INT64 chunk_key (INT64 value_id, int ord)
  {
    return (value_id << INNER_TABLE_ORD_BITS) | ord;
  }

  int read_layout (THREAD_ENTRY *thread_p, const OID *class_oid, RECDES *recdes, layout &l)
  {
    int idx = -1;
    REPR_ID repid = or_rep_id (recdes);
    OR_CLASSREP *repr;

    if (repid == NULL_REPRID)
      {
	return set_error ("Inner Table: record without representation");
      }
    repr = heap_classrepr_get (thread_p, class_oid, NULL, repid, &idx);
    if (repr == NULL)
      {
	ASSERT_ERROR ();
	return er_errid ();
      }
    l.header_size = OR_HEADER_SIZE (recdes->data);
    l.offset_size = OR_GET_OFFSET_SIZE (recdes->data);
    l.n_var = repr->n_variable;
    l.off.resize (l.n_var + 1);
    l.string_attr.assign (l.n_var, -1);
    char *vt = recdes->data + l.header_size;
    for (int i = 0; i <= l.n_var && l.n_var > 0; i++)
      {
	l.off[i] = OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL (vt, i, l.offset_size);
      }
    for (int a = 0; a < repr->n_attributes; a++)
      {
	const OR_ATTRIBUTE &att = repr->attributes[a];
	/* VARCHAR and VARBIT values of 255 bytes or more both start with 0xFF and an int that is never negative */
	if (!att.is_fixed && (att.type == DB_TYPE_STRING || att.type == DB_TYPE_VARBIT) && att.location >= 0
	    && att.location < l.n_var)
	  {
	    l.string_attr[att.location] = att.id;
	  }
      }
    heap_classrepr_free_and_init (repr, &idx);
    if (l.n_var > 0 && l.header_size + l.off[l.n_var] > recdes->length)
      {
	return set_error ("Inner Table: variable offsets beyond the record");
      }
    return NO_ERROR;
  }

  /* Build OUT from SRC: variable value I becomes REPLACEMENT[I] when it is not null. */
  void rebuild (const RECDES *src, const layout &l, const std::vector<std::string *> &replacement,
		std::vector<char> &out)
  {
    const char *vt = src->data + l.header_size;
    out.assign (src->data, src->data + l.header_size + l.off[0]);
    std::vector<int> off (l.n_var + 1);
    off[0] = l.off[0];
    for (int i = 0; i < l.n_var; i++)
      {
	if (replacement[i] != NULL)
	  {
	    out.insert (out.end (), replacement[i]->begin (), replacement[i]->end ());
	  }
	else
	  {
	    out.insert (out.end (), vt + l.off[i], vt + l.off[i + 1]);
	  }
	off[i + 1] = (int) out.size () - l.header_size;
      }
    /* bytes after the last variable value stay as they are */
    out.insert (out.end (), vt + l.off[l.n_var], (const char *) src->data + src->length);
    char *nvt = out.data () + l.header_size;
    for (int i = 0; i <= l.n_var; i++)
      {
	char *e = OR_VAR_TABLE_ELEMENT_PTR (nvt, i, l.offset_size);
	if (l.offset_size == OR_BYTE_SIZE)
	  {
	    OR_PUT_BYTE (e, off[i]);
	  }
	else if (l.offset_size == OR_SHORT_SIZE)
	  {
	    OR_PUT_SHORT (e, off[i]);
	  }
	else
	  {
	    OR_PUT_INT (e, off[i]);
	  }
      }
  }

  std::shared_ptr<companion> resolve (THREAD_ENTRY *thread_p, const OID *class_oid)
  {
    auto c = std::make_shared<companion> ();
    char *name = NULL;

    if (heap_get_class_name (thread_p, class_oid, &name) != NO_ERROR || name == NULL)
      {
	er_clear ();
	return c;
      }
    std::string cname (name);
    free_and_init (name);
    size_t sl = strlen (INNER_SUFFIX);
    if (cname.size () > sl && cname.compare (cname.size () - sl, sl, INNER_SUFFIX) == 0)
      {
	return c;		/* a companion class has no companion */
      }
    cname += INNER_SUFFIX;

    OID coid;
    if (xlocator_find_class_oid (thread_p, cname.c_str (), &coid, NULL_LOCK) != LC_CLASSNAME_EXIST)
      {
	er_clear ();
	return c;
      }
    /* Read the HFID from the class record: TRUNCATE gives the class a new heap before the HFID cache learns it. */
    HFID hfid;
    RECDES class_rec;
    HEAP_SCANCACHE root_scan;
    (void) heap_scancache_quick_start_root_hfid (thread_p, &root_scan);
    bool found = heap_get_class_record (thread_p, &coid, &class_rec, &root_scan, PEEK) == S_SUCCESS;
    if (found)
      {
	or_class_hfid (&class_rec, &hfid);
      }
    heap_scancache_end (thread_p, &root_scan);
    if (!found)
      {
	er_clear ();
	return c;
      }
    int idx = -1;
    OR_CLASSREP *repr = heap_classrepr_get (thread_p, &coid, NULL, NULL_REPRID, &idx);
    if (repr == NULL)
      {
	er_clear ();
	return c;
      }
    for (int a = 0; a < repr->n_attributes; a++)
      {
	if (repr->attributes[a].type == DB_TYPE_BIGINT && c->key_attr < 0)
	  {
	    c->key_attr = repr->attributes[a].id;
	  }
	else if (repr->attributes[a].type == DB_TYPE_VARBIT && c->chunk_attr < 0)
	  {
	    c->chunk_attr = repr->attributes[a].id;
	  }
      }
    for (int i = 0; i < repr->n_indexes; i++)
      {
	const OR_INDEX &ix = repr->indexes[i];
	if (ix.n_atts == 1 && ix.atts[0]->id == c->key_attr && (ix.type == BTREE_UNIQUE || ix.type == BTREE_PRIMARY_KEY))
	  {
	    c->btid = ix.btid;
	  }
      }
    heap_classrepr_free_and_init (repr, &idx);
    if (c->key_attr < 0 || c->chunk_attr < 0 || BTID_IS_NULL (&c->btid))
      {
	return c;
      }
    c->class_oid = coid;
    c->hfid = hfid;
    c->opted = true;
    return c;
  }

  std::shared_ptr<companion> companion_of (THREAD_ENTRY *thread_p, const OID *class_oid)
  {
    if (!inner_table_enabled () || class_oid == NULL || OID_ISNULL (class_oid) || oid_is_system_class (class_oid))
      {
	return nullptr;
      }
    oid_key k = { class_oid->pageid, class_oid->slotid, class_oid->volid };
    {
      std::lock_guard<std::mutex> g (classes_mutex);
      auto it = classes.find (k);
      if (it != classes.end ())
	{
	  return it->second->opted ? it->second : nullptr;
	}
    }
    std::shared_ptr<companion> c = resolve (thread_p, class_oid);
    {
      std::lock_guard<std::mutex> g (classes_mutex);
      auto it = classes.emplace (k, c).first;
      return it->second->opted ? it->second : nullptr;
    }
  }

  int next_value_id (THREAD_ENTRY *thread_p, companion &c, INT64 *value_id)
  {
    {
      std::lock_guard<std::mutex> g (c.seed_mutex);
      if (!c.seeded)
	{
	  DB_VALUE key;
	  db_make_null (&key);
	  if (btree_find_min_or_max_key (thread_p, &c.btid, &key, false) != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return er_errid ();
	    }
	  INT64 maxk = DB_IS_NULL (&key) ? -1 : db_get_bigint (&key);
	  pr_clear_value (&key);
	  c.next_id = (maxk < 0) ? 1 : (maxk >> INNER_TABLE_ORD_BITS) + 1;
	  c.seeded = true;
	}
    }
    *value_id = c.next_id.fetch_add (1);
    return NO_ERROR;
  }

  int lock_class (THREAD_ENTRY *thread_p, companion &c, LOCK lock)
  {
    if (thread_p->type == TT_LOADDB)
      {
	/* A load worker takes no lock. inner_table_lock_for_load gave the session a BU lock on the companion. */
	if (lock_has_lock_on_object (&c.class_oid, oid_Root_class_oid, BU_LOCK))
	  {
	    return NO_ERROR;
	  }
	return set_error ("Inner Table: the load session has no lock on the companion class");
      }
    if (lock_object (thread_p, &c.class_oid, oid_Root_class_oid, lock, LK_UNCOND_LOCK) != LK_GRANTED)
      {
	return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
      }
    return NO_ERROR;
  }

  /* Insert BYTES as chunk rows of a new value; fill R. */
  int write_chunks (THREAD_ENTRY *thread_p, companion &c, const char *bytes, int len, ref &r)
  {
    HEAP_CACHE_ATTRINFO attr_info;
    HEAP_SCANCACHE scan_cache;
    ATTR_ID ids[2] = { c.key_attr, c.chunk_attr };
    int error, force_count;
    int cb = chunk_bytes ();

    r.total_len = len;
    r.chunk_count = (len + cb - 1) / cb;
    if (r.chunk_count >= (1 << INNER_TABLE_ORD_BITS))
      {
	return set_error ("Inner Table: value has too many chunks");
      }
    error = next_value_id (thread_p, c, &r.value_id);
    if (error != NO_ERROR)
      {
	return error;
      }
    error = lock_class (thread_p, c, IX_LOCK);
    if (error != NO_ERROR)
      {
	return error;
      }
    error = heap_attrinfo_start (thread_p, &c.class_oid, 2, ids, &attr_info);
    if (error != NO_ERROR)
      {
	return error;
      }
    error = locator_start_force_scan_cache (thread_p, &scan_cache, &c.hfid, &c.class_oid, SINGLE_ROW_INSERT);
    if (error != NO_ERROR)
      {
	heap_attrinfo_end (thread_p, &attr_info);
	return error;
      }
    for (int ord = 0; ord < r.chunk_count && error == NO_ERROR; ord++)
      {
	int n = std::min (cb, len - ord * cb);
	DB_VALUE key, chunk;
	OID oid = OID_INITIALIZER;

	db_make_bigint (&key, chunk_key (r.value_id, ord));
	db_make_varbit (&chunk, DB_MAX_VARBIT_PRECISION, bytes + (size_t) ord * cb, n * 8);
	error = heap_attrinfo_set (NULL, c.key_attr, &key, &attr_info);
	if (error == NO_ERROR)
	  {
	    error = heap_attrinfo_set (NULL, c.chunk_attr, &chunk, &attr_info);
	  }
	if (error == NO_ERROR && thread_p->type == TT_LOADDB)
	  {
	    /* Like the parent records of a load, insert under the session's BU lock without an MVCCID. */
	    RECDES rec = RECDES_INITIALIZER;
	    LC_COPYAREA *area = locator_allocate_copy_area_by_attr_info (thread_p, &attr_info, NULL, &rec, -1,
				LOB_FLAG_INCLUDE_LOB);
	    if (area == NULL)
	      {
		error = er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
	      }
	    else
	      {
		error = locator_insert_force (thread_p, &c.hfid, &c.class_oid, &oid, &rec, true, SINGLE_ROW_INSERT,
					      &scan_cache, &force_count, DB_NOT_PARTITIONED_CLASS, NULL, NULL,
					      UPDATE_INPLACE_NONE, NULL, true, true, false);
		locator_free_copy_area (area);
	      }
	  }
	else if (error == NO_ERROR)
	  {
	    error = locator_attribute_info_force (thread_p, &c.hfid, &oid, &attr_info, NULL, 0, LC_FLUSH_INSERT,
						  SINGLE_ROW_INSERT, &scan_cache, &force_count, false,
						  REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL,
						  UPDATE_INPLACE_NONE, NULL, LOCATOR_LOCK_AT_SELECT);
	  }
      }
    locator_end_force_scan_cache (thread_p, &scan_cache);
    heap_attrinfo_end (thread_p, &attr_info);
    return error;
  }

  /* Append the bytes of R's chunks, as SNAPSHOT sees them, to OUT. */
  int read_chunks (THREAD_ENTRY *thread_p, companion &c, const ref &r, MVCC_SNAPSHOT *snapshot, std::string &out)
  {
    HEAP_CACHE_ATTRINFO attr_info;
    HEAP_SCANCACHE scan_cache;
    MVCC_SNAPSHOT dirty;
    ATTR_ID ids[1] = { c.chunk_attr };
    RECDES rec = RECDES_INITIALIZER;
    int error;
    size_t start = out.size ();

    if (snapshot == NULL)
      {
	dirty.snapshot_fnc = mvcc_satisfies_dirty;
	snapshot = &dirty;
      }
    error = heap_attrinfo_start (thread_p, &c.class_oid, 1, ids, &attr_info);
    if (error != NO_ERROR)
      {
	return error;
      }
    error = heap_scancache_start (thread_p, &scan_cache, &c.hfid, &c.class_oid, true, snapshot);
    if (error != NO_ERROR)
      {
	heap_attrinfo_end (thread_p, &attr_info);
	return error;
      }
    for (int ord = 0; ord < r.chunk_count && error == NO_ERROR; ord++)
      {
	DB_VALUE key;
	OID oid;
	db_make_bigint (&key, chunk_key (r.value_id, ord));
	BTREE_SEARCH found = btree_find_unique_visible (thread_p, &c.btid, &key, &c.class_oid, &oid, snapshot);
	if (found == BTREE_ERROR_OCCURRED)
	  {
	    ASSERT_ERROR_AND_SET (error);
	    break;
	  }
	if (found != BTREE_KEY_FOUND)
	  {
	    error = set_error ("Inner Table: a chunk of a visible value is missing");
	    break;
	  }
	rec.data = NULL;
	if (heap_get_visible_version (thread_p, &oid, &c.class_oid, &rec, &scan_cache, COPY, NULL_CHN) != S_SUCCESS)
	  {
	    error = er_errid () != NO_ERROR ? er_errid () : set_error ("Inner Table: a chunk row is not visible");
	    break;
	  }
	error = heap_attrinfo_read_dbvalues (thread_p, &oid, &rec, &attr_info);
	if (error != NO_ERROR)
	  {
	    break;
	  }
	DB_VALUE *v = heap_attrinfo_access (c.chunk_attr, &attr_info);
	int bits = 0;
	const char *bytes = (v == NULL || DB_IS_NULL (v)) ? NULL : db_get_bit (v, &bits);
	if (bytes == NULL || bits % 8 != 0)
	  {
	    error = set_error ("Inner Table: malformed chunk row");
	    break;
	  }
	out.append (bytes, bits / 8);
      }
    heap_scancache_end (thread_p, &scan_cache);
    heap_attrinfo_end (thread_p, &attr_info);
    if (error == NO_ERROR && (int) (out.size () - start) != r.total_len)
      {
	error = set_error ("Inner Table: chunk bytes do not match the reference length");
      }
    return error;
  }

  bool in_att_list (ATTR_ID id, const ATTR_ID *att_id, int n_att_id)
  {
    for (int i = 0; i < n_att_id; i++)
      {
	if (att_id[i] == id)
	  {
	    return true;
	  }
      }
    return false;
  }
}

struct inner_table_refs
{
  std::shared_ptr<companion> owner;
  std::vector<ref> refs;
};

bool
inner_table_enabled (void)
{
  return prm_get_integer_value (PRM_ID_INNER_TABLE_THRESHOLD) > 0;
}

/*
 * inner_table_is_opted_in () - Whether records of a class can hold references. Readers that bypass the heap fetch
 *				 functions must not read such a class.
 */
bool
inner_table_is_opted_in (THREAD_ENTRY *thread_p, const OID *class_oid)
{
  return companion_of (thread_p, class_oid) != nullptr;
}

/*
 * inner_table_reads_movable () - Whether a reader of ATTR_IDS of CLASS_OID can meet an Inner Table reference: one of
 *				   the attributes has a type whose large values move to chunk rows.
 */
bool
inner_table_reads_movable (THREAD_ENTRY *thread_p, const OID *class_oid, int n_attrs, const ATTR_ID *attr_ids)
{
  int idx = -1;
  OR_CLASSREP *repr = heap_classrepr_get (thread_p, class_oid, NULL, NULL_REPRID, &idx);
  bool movable = false;

  if (repr == NULL)
    {
      er_clear ();
      return true;
    }
  for (int i = 0; i < n_attrs && !movable; i++)
    {
      for (int a = 0; a < repr->n_attributes; a++)
	{
	  if (repr->attributes[a].id == attr_ids[i])
	    {
	      movable = repr->attributes[a].type == DB_TYPE_STRING || repr->attributes[a].type == DB_TYPE_VARBIT;
	      break;
	    }
	}
    }
  heap_classrepr_free_and_init (repr, &idx);
  return movable;
}

/*
 * inner_table_lock_for_load () - Give the load session of CLASS_OID the BU lock on its companion class too. The load
 *				   workers that insert the class write chunk rows under this lock.
 */
int
inner_table_lock_for_load (THREAD_ENTRY *thread_p, const OID *class_oid)
{
  std::shared_ptr<companion> c = companion_of (thread_p, class_oid);
  if (c == nullptr)
    {
      return NO_ERROR;
    }
  if (lock_object (thread_p, &c->class_oid, oid_Root_class_oid, BU_LOCK, LK_UNCOND_LOCK) != LK_GRANTED)
    {
      return er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * inner_table_rewrite () - Move the large VARCHAR values of a record that will be written into chunk rows.
 *
 * return	    : Error code.
 * class_oid (in)   : Class of the record.
 * recdes (in)	    : Complete record. It is not changed; index maintenance keeps using it.
 * old_refs (in/out): References of the replaced version, or NULL. A reference of an attribute that is not in ATT_ID
 *		      is kept instead of writing new chunks, and marked kept so that it is not retired.
 * out (out)	    : Rewritten record, valid when *rewritten. Free with inner_table_free_record.
 */
int
inner_table_rewrite (THREAD_ENTRY *thread_p, const OID *class_oid, RECDES *recdes, INNER_TABLE_REFS *old_refs,
		     const ATTR_ID *att_id, int n_att_id, RECDES *out, bool *rewritten)
{
  *rewritten = false;
  if (recdes == NULL || recdes->data == NULL
      || recdes->length < std::max (prm_get_integer_value (PRM_ID_INNER_TABLE_THRESHOLD), INNER_REF_SIZE + 1))
    {
      /* no value of a record shorter than the threshold moves, and none of OLD_REFS is kept */
      return NO_ERROR;
    }
  std::shared_ptr<companion> c = companion_of (thread_p, class_oid);
  if (c == nullptr)
    {
      return NO_ERROR;
    }

  layout l;
  int error = read_layout (thread_p, class_oid, recdes, l);
  if (error != NO_ERROR || l.n_var == 0)
    {
      return error;
    }
  int threshold = std::max (prm_get_integer_value (PRM_ID_INNER_TABLE_THRESHOLD), INNER_REF_SIZE + 1);
  const char *vt = recdes->data + l.header_size;
  std::vector<std::string> refs_bytes (l.n_var);
  std::vector<std::string *> replacement (l.n_var, NULL);

  for (int i = 0; i < l.n_var; i++)
    {
      const char *p = vt + l.off[i];
      int len = l.off[i + 1] - l.off[i];
      if (l.string_attr[i] < 0 || len < threshold || !is_large_string (p, len) || is_reference (p, len))
	{
	  continue;
	}
      ref r;
      r.attr_id = l.string_attr[i];
      r.kept = false;
      r.decompressed_len = OR_GET_INT (p + 1 + OR_INT_SIZE);

      const ref *keep = NULL;
      if (old_refs != NULL && att_id != NULL && !in_att_list (r.attr_id, att_id, n_att_id))
	{
	  for (ref &o:old_refs->refs)
	    {
	      if (o.attr_id == r.attr_id && o.total_len == len)
		{
		  o.kept = true;
		  keep = &o;
		  break;
		}
	    }
	}
      if (keep != NULL)
	{
	  r = *keep;
	}
      else
	{
	  error = write_chunks (thread_p, *c, p, len, r);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
      refs_bytes[i].assign (INNER_REF_SIZE, '\0');
      put_reference (&refs_bytes[i][0], r);
      replacement[i] = &refs_bytes[i];
    }

  bool any = false;
  for (int i = 0; i < l.n_var; i++)
    {
      any = any || replacement[i] != NULL;
    }
  if (!any)
    {
      return NO_ERROR;
    }

  std::vector<char> bytes;
  rebuild (recdes, l, replacement, bytes);
  /* the heap can grow the header in place, by up to the MVCC fields an insert or update adds */
  int area = (int) bytes.size () + OR_MVCC_MAX_HEADER_SIZE;
  out->data = (char *) db_private_alloc (thread_p, area);
  if (out->data == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) area);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memcpy (out->data, bytes.data (), bytes.size ());
  out->length = (int) bytes.size ();
  out->area_size = area;
  out->type = recdes->type;
  *rewritten = true;
  return NO_ERROR;
}

void
inner_table_free_record (RECDES *rec)
{
  if (rec != NULL && rec->data != NULL)
    {
      db_private_free_and_init (NULL, rec->data);
    }
}

/*
 * inner_table_collect_refs () - Read the references of the last version of a row.
 *
 * return     : Error code. *refs is NULL when the class is not opted in or the row has no reference.
 */
int
inner_table_collect_refs (THREAD_ENTRY *thread_p, const OID *class_oid, const OID *oid, INNER_TABLE_REFS **refs)
{
  *refs = NULL;
  if (!last_had_refs && OID_EQ (&last_oid, oid))
    {
      /* the caller just fetched this version and it held no reference */
      return NO_ERROR;
    }
  std::shared_ptr<companion> c = companion_of (thread_p, class_oid);
  if (c == nullptr)
    {
      return NO_ERROR;
    }

  HEAP_SCANCACHE scan_cache;
  RECDES rec = RECDES_INITIALIZER;
  OID cls = *class_oid;
  HFID hfid;
  bool found = false;
  int error = heap_get_class_info (thread_p, class_oid, &hfid, NULL, &found);
  if (error != NO_ERROR)
    {
      return error;
    }
  if (!found)
    {
      return NO_ERROR;
    }
  error = heap_scancache_start (thread_p, &scan_cache, &hfid, class_oid, true, NULL);
  if (error != NO_ERROR)
    {
      return error;
    }

  raw_depth++;
  HEAP_GET_CONTEXT context;
  heap_init_get_context (thread_p, &context, oid, &cls, &rec, &scan_cache, COPY, NULL_CHN);
  SCAN_CODE scan = heap_get_last_version (thread_p, &context);
  heap_clean_get_context (thread_p, &context);
  raw_depth--;

  if (scan == S_SUCCESS)
    {
      layout l;
      error = read_layout (thread_p, class_oid, &rec, l);
      if (error == NO_ERROR)
	{
	  std::unique_ptr<INNER_TABLE_REFS> found (new INNER_TABLE_REFS ());
	  found->owner = c;
	  const char *vt = rec.data + l.header_size;
	  for (int i = 0; i < l.n_var; i++)
	    {
	      const char *p = vt + l.off[i];
	      if (l.string_attr[i] >= 0 && is_reference (p, l.off[i + 1] - l.off[i]))
		{
		  found->refs.push_back (get_reference (p, l.string_attr[i]));
		}
	    }
	  if (!found->refs.empty ())
	    {
	      *refs = found.release ();
	    }
	}
    }
  else if (scan != S_DOESNT_EXIST && scan != S_SNAPSHOT_NOT_SATISFIED)
    {
      error = er_errid () != NO_ERROR ? er_errid () : ER_FAILED;
    }
  heap_scancache_end (thread_p, &scan_cache);
  return error;
}

/*
 * inner_table_retire_refs () - MVCC-delete the chunk rows of every reference that was not kept.
 */
int
inner_table_retire_refs (THREAD_ENTRY *thread_p, INNER_TABLE_REFS *refs)
{
  if (refs == NULL)
    {
      return NO_ERROR;
    }
  companion &c = *refs->owner;
  HEAP_SCANCACHE scan_cache;
  int error = lock_class (thread_p, c, IX_LOCK);
  if (error != NO_ERROR)
    {
      return error;
    }
  error = locator_start_force_scan_cache (thread_p, &scan_cache, &c.hfid, &c.class_oid, SINGLE_ROW_DELETE);
  if (error != NO_ERROR)
    {
      return error;
    }
  for (const ref &r:refs->refs)
    {
      for (int ord = 0; ord < r.chunk_count && error == NO_ERROR && !r.kept; ord++)
	{
	  DB_VALUE key;
	  OID oid;
	  int force_count;
	  db_make_bigint (&key, chunk_key (r.value_id, ord));
	  BTREE_SEARCH found = btree_find_unique_visible (thread_p, &c.btid, &key, &c.class_oid, &oid, NULL);
	  if (found == BTREE_ERROR_OCCURRED)
	    {
	      ASSERT_ERROR_AND_SET (error);
	      break;
	    }
	  if (found != BTREE_KEY_FOUND)
	    {
	      error = set_error ("Inner Table: a chunk to retire is missing");
	      break;
	    }
	  error = locator_attribute_info_force (thread_p, &c.hfid, &oid, NULL, NULL, 0, LC_FLUSH_DELETE,
						SINGLE_ROW_DELETE, &scan_cache, &force_count, false,
						REPL_INFO_TYPE_RBR_NORMAL, DB_NOT_PARTITIONED_CLASS, NULL, NULL, NULL,
						UPDATE_INPLACE_NONE, NULL, LOCATOR_LOCK_AT_FORCE);
	}
    }
  locator_end_force_scan_cache (thread_p, &scan_cache);
  return error;
}

void
inner_table_free_refs (INNER_TABLE_REFS *refs)
{
  delete refs;
}

/*
 * inner_table_expand () - Replace the references of a fetched record with the bytes of their chunks.
 *
 * return	    : S_SUCCESS, S_DOESNT_FIT (recdes->length is minus the needed size) or S_ERROR.
 * oid (in)	    : Object of the record, or NULL.
 * recdes (in/out)  : Fetched record.
 * scan_cache (in)  : Scan cache whose area may hold the expanded record, or NULL.
 * ispeeking (in)   : PEEK or COPY, as the caller fetched.
 * snapshot (in)    : Snapshot that selected the record version, or NULL for the last version.
 */
SCAN_CODE
inner_table_expand (THREAD_ENTRY *thread_p, const OID *oid, const OID *class_oid, RECDES *recdes,
		    HEAP_SCANCACHE *scan_cache, int ispeeking, MVCC_SNAPSHOT *snapshot)
{
  if (raw_depth > 0 || recdes == NULL || recdes->data == NULL || recdes->length <= 0)
    {
      return S_SUCCESS;
    }
  if (scan_cache != NULL && scan_cache->inner_table_skip_expand)
    {
      remember (NULL, true);
      return S_SUCCESS;
    }
  if (!may_hold_reference (recdes))
    {
      remember (oid, false);
      return S_SUCCESS;
    }
  std::shared_ptr<companion> c = companion_of (thread_p, class_oid);
  if (c == nullptr)
    {
      remember (oid, false);
      return S_SUCCESS;
    }

  /* Copy the record first: reading chunks can fix other pages, and the caller's page may be released meanwhile. */
  std::vector<char> src (recdes->data, recdes->data + recdes->length);
  RECDES s = { recdes->length, recdes->length, recdes->type, src.data () };
  layout l;
  if (read_layout (thread_p, class_oid, &s, l) != NO_ERROR)
    {
      return S_ERROR;
    }
  const char *vt = s.data + l.header_size;
  std::vector<std::string> values (l.n_var);
  std::vector<std::string *> replacement (l.n_var, NULL);
  bool any = false;
  for (int i = 0; i < l.n_var; i++)
    {
      const char *p = vt + l.off[i];
      if (l.string_attr[i] < 0 || !is_reference (p, l.off[i + 1] - l.off[i]))
	{
	  continue;
	}
      ref r = get_reference (p, l.string_attr[i]);
      if (read_chunks (thread_p, *c, r, snapshot, values[i]) != NO_ERROR)
	{
	  return S_ERROR;
	}
      replacement[i] = &values[i];
      any = true;
    }
  remember (oid, any);
  if (!any)
    {
      return S_SUCCESS;
    }

  std::vector<char> bytes;
  rebuild (&s, l, replacement, bytes);
  int size = (int) bytes.size ();
  if (scan_cache != NULL
      && (ispeeking == PEEK || recdes->data == NULL || scan_cache->is_recdes_assigned_to_area (*recdes)))
    {
      scan_cache->assign_recdes_to_area (*recdes, (size_t) size);
    }
  else if (recdes->area_size < size)
    {
      recdes->length = -size;
      return S_DOESNT_FIT;
    }
  memcpy (recdes->data, bytes.data (), size);
  recdes->length = size;
  return S_SUCCESS;
}

/*
 * inner_table_forget_classes () - Drop the cached opt-in decisions; a schema change can make them stale.
 */
void
inner_table_forget_classes (void)
{
  std::lock_guard<std::mutex> g (classes_mutex);
  classes.clear ();
}

#if !defined(NDEBUG)
/*
 * Page trace for the projection measurement, in debug builds only. When the server environment has
 * INNER_TABLE_PAGE_TRACE_FILE, every page a transaction fixes is remembered, and each commit appends one line with
 * the distinct pages it fixed: heap pages of user classes, chunk heap pages, overflow pages, index pages, catalog
 * heap pages and the rest.
 */
namespace
{
  struct tran_pages
  {
    std::map<std::tuple<int, int, int>, std::set<INT64>> heap;	/* by class OID */
    std::set<INT64> overflow, index, other;
  };

  std::mutex trace_mutex;
  std::unordered_map<int, tran_pages> trace_by_tran;

  const char *trace_path ()
  {
    static const char *path = std::getenv ("INNER_TABLE_PAGE_TRACE_FILE");
    return path;
  }
}

void
inner_table_trace_fix (THREAD_ENTRY *thread_p, PAGE_PTR pgptr)
{
  if (trace_path () == NULL || thread_p == NULL || pgptr == NULL || thread_p->tran_index <= LOG_SYSTEM_TRAN_INDEX)
    {
      return;
    }
  VPID vpid;
  pgbuf_get_vpid (pgptr, &vpid);
  INT64 key = ((INT64) vpid.volid << 32) | (unsigned int) vpid.pageid;
  PAGE_TYPE type = pgbuf_get_page_ptype (thread_p, pgptr);
  OID class_oid = OID_INITIALIZER;
  if (type == PAGE_HEAP && heap_get_class_oid_from_page (thread_p, pgptr, &class_oid) != NO_ERROR)
    {
      er_clear ();
      type = PAGE_UNKNOWN;
    }

  std::lock_guard<std::mutex> g (trace_mutex);
  tran_pages &t = trace_by_tran[thread_p->tran_index];
  switch (type)
    {
    case PAGE_HEAP:
      t.heap[std::make_tuple (class_oid.volid, class_oid.pageid, class_oid.slotid)].insert (key);
      break;
    case PAGE_OVERFLOW:
      t.overflow.insert (key);
      break;
    case PAGE_BTREE:
      t.index.insert (key);
      break;
    default:
      t.other.insert (key);
      break;
    }
}

void
inner_table_trace_commit (THREAD_ENTRY *thread_p)
{
  if (trace_path () == NULL || thread_p == NULL)
    {
      return;
    }
  tran_pages t;
  {
    std::lock_guard<std::mutex> g (trace_mutex);
    auto it = trace_by_tran.find (thread_p->tran_index);
    if (it == trace_by_tran.end ())
      {
	return;
      }
    t = std::move (it->second);
    trace_by_tran.erase (it);
  }
  size_t heap = 0, chunk = 0, catalog = 0;
  std::string classes;
  for (const auto &e : t.heap)
    {
      OID oid = { std::get<1> (e.first), (short) std::get<2> (e.first), (short) std::get<0> (e.first) };
      char *name = NULL;
      bool is_catalog = OID_ISNULL (&oid) || OID_IS_ROOTOID (&oid) || oid_is_system_class (&oid);
      if (!is_catalog && heap_get_class_name (thread_p, &oid, &name) == NO_ERROR && name != NULL)
	{
	  std::string n (name);
	  free_and_init (name);
	  size_t sl = strlen (INNER_SUFFIX);
	  bool is_chunk = n.size () > sl && n.compare (n.size () - sl, sl, INNER_SUFFIX) == 0;
	  (is_chunk ? chunk : heap) += e.second.size ();
	  classes += " " + n + ":" + std::to_string (e.second.size ());
	}
      else
	{
	  er_clear ();
	  catalog += e.second.size ();
	}
    }
  FILE *f = std::fopen (trace_path (), "a");
  if (f != NULL)
    {
      std::fprintf (f, "tran=%d heap=%zu chunk=%zu overflow=%zu index=%zu catalog=%zu other=%zu classes=%s\n",
		    thread_p->tran_index, heap, chunk, t.overflow.size (), t.index.size (), catalog, t.other.size (),
		    classes.c_str ());
      std::fclose (f);
    }
}
#endif /* !NDEBUG */
