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
// hnsw.cpp - common implementation of HNSW index
//

#include "hnsw.hpp"

#include <fstream>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "error_manager.h"
#include "system_parameter.h"
#include "vector_opfunc.hpp"
#include "boot_sr.h"
#include "file_io.h"
#include "system_parameter.h"
#include "dbtype.h"
#include "db_vector.hpp"
#include "porting.h"
#include "vector_distance_enum.h"
#include "heap_file.h"
#include "hnsw_api.hpp"
#include "log_manager.h"
#include "mvcc.h"
#include "recovery.h"

#include "slotted_page.h"
#include "page_buffer.h"
#include "object_representation.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// =====================================================================
// statics
// =====================================================================
static int hnsw_create_file (THREAD_ENTRY *thread_p, OID *class_oid, int attr_id, BTID *btid);
static int hnsw_initalize_new_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args);
static int hnsw_init_header (THREAD_ENTRY *thread_p, VFID *vfid, PAGE_PTR page_ptr, HNSW_HEADER *hnsw_header);
static int hnsw_pack_header (RECDES *rec, HNSW_HEADER *hnsw_header);
static HNSW_HEADER *hnsw_get_header (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr);
static int hnsw_print_index_info (BTID *btid, HNSW_HEADER *hnsw_header);

// =====================================================================
// hnsw_index_manager declaration
// =====================================================================

namespace fs = std::filesystem;

class hnsw_index_manager
{
  public:

    static hnsw_index_manager &instance()
    {
      static hnsw_index_manager inst;
      return inst;
    }

    fs::path get_index_file_path (const std::string &prefix, const BTID *btid) const;
    fs::path get_index_meta_file_path (const std::string &prefix, const BTID *btid) const;
    fs::path get_index_directory_path() const;

    void create_index_directory();
    bool is_index_file_exists (const std::string &prefix, const BTID *btid) const;
    bool is_index_meta_file_exists (const std::string &prefix, const BTID *btid) const;

    BTID create_btid (THREAD_ENTRY *thread_p, const hnsw_index_backend *backend, const OID *class_oid, int attr_id,
		      const hnsw_build_params &params);

    // index management on memory
    bool is_index_loaded (const BTID *btid) const;
    int add_index (const BTID *btid, hnsw_index *index);
    hnsw_index *get_index (const BTID *btid) const;
    int delete_index (const BTID *btid);

    void print_index_info (THREAD_ENTRY *thread_p, const BTID *btid);

    // index management on disk
    int save_index (THREAD_ENTRY *thread_p, hnsw_index *index);
    int load_index (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index *&index);
    int load_or_create_index_for_recovery (THREAD_ENTRY *thread_p, const BTID *btid,
					   const hnsw_build_params &params, hnsw_index *&index);
    int save_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, const hnsw_index_meta &meta);
    int load_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index_meta &meta);
    int save_all_indices (THREAD_ENTRY *thread_p);
    int delete_index_on_disk (THREAD_ENTRY *thread_p, const std::string &prefix, const BTID *btid);

    void finalize ();

    // backend management
    void register_backend (std::unique_ptr<hnsw_index_backend> backend);
    const hnsw_index_backend *get_backend () const;
    hnsw_index_backend *get_backend ();

    ~hnsw_index_manager() = default;

  private:
    fs::path get_vindex_root_path() const;

    /* singleton */
    hnsw_index_manager();

    hnsw_index_manager (const hnsw_index_manager &) = delete;
    hnsw_index_manager &operator= (const hnsw_index_manager &) = delete;
    hnsw_index_manager (hnsw_index_manager &&) = delete;
    hnsw_index_manager &operator= (hnsw_index_manager &&) = delete;

    /* index directory root path */
    fs::path m_root_path;
    int m_last_index_id;

    std::unordered_map<BTID, std::unique_ptr<hnsw_index>> m_index_map;
    std::unique_ptr<hnsw_index_backend> m_backend;
};

// singleton instances
// TODO: dynamically load backend implementations
static hnsw_index_manager *index_manager = nullptr;

// =====================================================================
// high-level APIs
// =====================================================================

int
xhnsw_initialize (THREAD_ENTRY *thread_p)
{
  if (index_manager == nullptr)
    {
      index_manager = &hnsw_index_manager::instance();
      index_manager->create_index_directory();
    }

  return NO_ERROR;
}

int
xhnsw_finalize (THREAD_ENTRY *thread_p)
{
  assert (index_manager != nullptr);

  index_manager->save_all_indices (thread_p);

  index_manager->finalize ();
  return NO_ERROR;
}

int
xhnsw_add_index (THREAD_ENTRY *thread_p, const OID *class_oid, const int attrid, const hnsw_build_params &params,
		 BTID &btid_out)
{
  hnsw_index_backend *backend_instance = index_manager->get_backend ();
  if (!backend_instance)
    {
      assert (false);
      return ER_FAILED;
    }

  bool is_metric_supported = backend_instance->is_metric_supported (params.metric);
  if (!is_metric_supported)
    {
      ASSERT_CUBVEC (false);
      return ER_FAILED;
    }

  btid_out = index_manager->create_btid (thread_p, backend_instance, class_oid, attrid, params);
  if (BTID_IS_NULL (&btid_out))
    {
      // TODO: error handling
      assert (false);
      return ER_FAILED;
    }

  hnsw_index *index = backend_instance->create_index (thread_p, &btid_out, "", params);
  if (index == nullptr)
    {
      // failed to create index
      assert (false);
      return ER_FAILED;
    }

  if (index_manager->add_index (&btid_out, index) != NO_ERROR)
    {
      // failed to add index
      assert (false);
      return ER_FAILED;
    }

#if !defined(NDEBUG)
  _er_log_debug (ARG_FILE_LINE, "HNSW Index added with ID %d", btid_out.root_pageid);

  index_manager->print_index_info (thread_p, &btid_out);
#endif

  return NO_ERROR;
}

int
xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
  return index_manager->delete_index_on_disk (thread_p,index_manager->get_backend()->get_id(), btid);
}

int
xhnsw_load_index (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
		  HFID *hfids, const hnsw_build_params &params)
{
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan_result;
  RECDES in_recdes;
  DB_VALUE *key_dbvalue;
  HEAP_CACHE_ATTRINFO attr_info;
  OID cur_oid;
  int cur_class = 0;
  int attr_offset = 0;
  OID_SET_NULL (&cur_oid);
  BTID new_btid;

  if (xhnsw_add_index (thread_p, oid, attr_ids[0], params, new_btid) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  while (cur_class < n_classes && HFID_IS_NULL (&hfids[cur_class]))
    {
      cur_class++;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &hfids[cur_class], &oid[cur_class], true, NULL) != NO_ERROR)
    {
      return ER_FAILED;
    }

  attr_offset = cur_class * n_attrs;

  if (heap_attrinfo_start (thread_p, &oid[cur_class], n_attrs, &attr_ids[attr_offset], &attr_info) != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  /* -------- Batch buffers --------
  - oids:    growable array of OID (count elements)
  - vectors: contiguous float buffer of size (capacity * dimension)
  */
  int dimension = params.dimension;
  int capacity = 1024;
  int count = 0;
  OID *oids = (OID *) malloc ((size_t) capacity * sizeof (OID));
  float *vectors = (float *) malloc ((size_t) capacity * (size_t) dimension * sizeof (float));
  if (oids == NULL || vectors == NULL)
    {
      if (oids)
	{
	  free (oids);
	}
      if (vectors)
	{
	  free (vectors);
	}
      heap_attrinfo_end (thread_p, &attr_info);
      (void) heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  auto ensure_capacity = [&] (void) -> bool
  {
    if (count < capacity)
      {
	return true;
      }
    int new_cap = capacity * 2;
    OID *new_oids = (OID *) realloc (oids, (size_t) new_cap * sizeof (OID));
    float *new_vectors = (float *) realloc (vectors, (size_t) new_cap * (size_t) dimension * sizeof (float));
    if (new_oids == NULL || new_vectors == NULL)
      {
	if (new_oids)
	  {
	    oids = new_oids;
	  }
	if (new_vectors)
	  {
	    vectors = new_vectors;
	  }
	return false;
      }
    oids = new_oids;
    vectors = new_vectors;
    capacity = new_cap;
    return true;
  };

  do
    {
      scan_result = heap_next (thread_p, &hfids[cur_class], &oid[cur_class], &cur_oid,
			       &in_recdes, &scan_cache,
			       scan_cache.cache_last_fix_page ? PEEK : COPY);

      switch (scan_result)
	{
	case S_SUCCESS:
	  heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &in_recdes, &attr_info);

	  key_dbvalue = &attr_info.values[0].dbvalue;
	  assert (db_value_type (key_dbvalue) == DB_TYPE_VECTOR);

	  {
	    const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
	    assert (vf != NULL && vf->dim == dimension);

	    if (!ensure_capacity ())
	      {
		if (oids)
		  {
		    free (oids);
		  }
		if (vectors)
		  {
		    free (vectors);
		  }
		heap_attrinfo_end (thread_p, &attr_info);
		(void) heap_scancache_end (thread_p, &scan_cache);
		return ER_FAILED;
	      }

	    oids[count] = cur_oid;
	    float *dst = vectors + ((size_t) count * (size_t) dimension);
	    memcpy (dst, vf->float_array, (size_t) dimension * sizeof (float));

	    count++;
	  }
	  continue;

	case S_END:
	{
	  hnsw_add_element (thread_p, &new_btid, oids, vectors, count);

	  if (oids)
	    {
	      free (oids);
	    }
	  if (vectors)
	    {
	      free (vectors);
	    }

	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);

	  *btid = new_btid;
	  return NO_ERROR;
	}

	default:
	  if (oids)
	    {
	      free (oids);
	    }
	  if (vectors)
	    {
	      free (vectors);
	    }
	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  assert (false);
	  return ER_FAILED;
	}
    }
  while (true);

  return NO_ERROR;
}

// =====================================================================
// HNSW WAL Logging Structures and Functions
// =====================================================================

/*
 * Packed structure for temporary HNSW vector insertion logging.
 * Format: [oid][btid]
 */
typedef struct hnsw_insert_log_data
{
  OID oid;
  BTID btid;
} HNSW_INSERT_LOG_DATA;

static int
hnsw_get_build_params_from_root_page (THREAD_ENTRY *thread_p, PAGE_PTR root_page, hnsw_build_params &params)
{
  RECDES header_record;
  HNSW_HEADER *header = NULL;

  if (root_page == NULL)
    {
      return ER_FAILED;
    }

#if !defined(NDEBUG)
  if (pgbuf_get_page_ptype (thread_p, root_page) != PAGE_HNSW)
    {
      return ER_FAILED;
    }
#endif

  if (spage_get_record (thread_p, root_page, HNSW_HEADER_NUM, &header_record, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  header = (HNSW_HEADER *) header_record.data;
  params = hnsw_build_params (header->dimension, header->hnsw_M, header->hnsw_efConstruction,
			      static_cast<DB_VECTOR_DISTANCE_METRIC> (header->metric));
  return NO_ERROR;
}

/*
 * hnsw_get_vector_by_oid() - Fetch vector value from heap for temporary logical insert redo.
 *
 * NOTE: This is intentionally limited to the temporary insert-redo mechanism. The final recovery model should log
 * physical HNSW page changes instead of reading heap during REDO.
 */
static int
hnsw_get_vector_by_oid (THREAD_ENTRY *thread_p, const BTID *btid, const OID *oid, std::vector<float> &out_vector)
{
  OID class_oid = OID_INITIALIZER;
  HFID class_hfid = HFID_INITIALIZER;
  VPID oid_vpid = VPID_INITIALIZER;
  PAGE_PTR oid_page = NULL;
  RECDES recdes = RECDES_INITIALIZER;
  HEAP_SCANCACHE scan_cache;
  HEAP_CACHE_ATTRINFO attr_info;
  ATTR_ID *attr_ids = NULL;
  DB_VALUE *key_dbvalue = NULL;
  const DB_VECTOR_FLOAT *vf = NULL;
  int num_attrs = 0;
  int error = NO_ERROR;
  bool scan_cache_started = false;
  bool attr_info_started = false;

  oid_vpid.volid = oid->volid;
  oid_vpid.pageid = oid->pageid;
  error = pgbuf_fix_if_not_deallocated (thread_p, &oid_vpid, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH, &oid_page);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  if (oid_page == NULL)
    {
      error = ER_FAILED;
      goto exit;
    }
  pgbuf_unfix_and_init (thread_p, oid_page);
  oid_page = NULL;

  if (heap_get_class_oid (thread_p, oid, &class_oid) != S_SUCCESS)
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_get_indexinfo_of_btid (thread_p, &class_oid, btid, NULL, &num_attrs, &attr_ids, NULL, NULL, NULL);
  if (error != NO_ERROR || num_attrs <= 0 || attr_ids == NULL)
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_get_class_info (thread_p, &class_oid, &class_hfid, NULL, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = heap_scancache_quick_start_with_class_hfid (thread_p, &scan_cache, &class_hfid);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  scan_cache_started = true;
  HEAP_SCANCACHE_SET_NODE (&scan_cache, &class_oid, &class_hfid);
  scan_cache.mvcc_disabled_class = mvcc_is_mvcc_disabled_class (&class_oid);

  recdes.data = NULL;
  if (heap_get_visible_version (thread_p, oid, &class_oid, &recdes, &scan_cache, COPY, NULL_CHN) != S_SUCCESS)
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_attrinfo_start (thread_p, &class_oid, 1, attr_ids, &attr_info);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  attr_info_started = true;

  error = heap_attrinfo_read_dbvalues (thread_p, oid, &recdes, &attr_info);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  key_dbvalue = &attr_info.values[0].dbvalue;
  if (db_value_type (key_dbvalue) != DB_TYPE_VECTOR)
    {
      error = ER_FAILED;
      goto exit;
    }

  vf = db_get_vector_float (key_dbvalue);
  if (vf == NULL || vf->float_array == NULL || vf->dim <= 0)
    {
      error = ER_FAILED;
      goto exit;
    }

  out_vector.assign (vf->float_array, vf->float_array + vf->dim);
  error = NO_ERROR;

exit:
  if (attr_info_started)
    {
      heap_attrinfo_end (thread_p, &attr_info);
    }
  if (oid_page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, oid_page);
    }
  if (scan_cache_started)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
    }
  if (attr_ids != NULL)
    {
      db_private_free_and_init (thread_p, attr_ids);
    }
  return error;
}

/*
 * hnsw_pack_insert_data() - Pack vector insertion identifier into WAL log
 *
 * return      : Total packed size
 * buffer(out) : Output buffer (must be pre-allocated)
 * btid        : B-tree ID identifying the HNSW index
 * oid         : Object ID of the vector record
 */
static int
hnsw_pack_insert_data (char *buffer, int buffer_size, const BTID *btid, const OID *oid)
{
  HNSW_INSERT_LOG_DATA *log_data = (HNSW_INSERT_LOG_DATA *)buffer;
  int total_size = (int) sizeof (HNSW_INSERT_LOG_DATA);

  if (total_size > buffer_size)
    {
      return 0;  // Buffer too small
    }

  log_data->btid = *btid;
  log_data->oid = *oid;

  return total_size;
}

int
hnsw_add_element (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, float *vector, int n_vectors)
{
  assert (oid);
  assert (vector);
  assert (n_vectors > 0);

  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  if (index_manager == nullptr && xhnsw_initialize (thread_p) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  auto *index = index_manager->get_index (btid);
  if (index == nullptr)
    {
      if (index_manager->load_index (thread_p, btid, index) != NO_ERROR)
	{
	  // failed to load index
	  assert (false);
	  return ER_FAILED;
	}
    }

  if (index->prepare_to_add (thread_p, n_vectors, oid, vector) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  if (!log_is_in_crash_recovery ())
    {
      // Temporary logical insert REDO record anchored to HNSW root page.
      int wal_data_size = (int) sizeof (HNSW_INSERT_LOG_DATA);

      VPID root_vpid;
      root_vpid.volid = btid->vfid.volid;
      root_vpid.pageid = btid->root_pageid;

      PAGE_PTR root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (root_page == NULL)
	{
	  return ER_FAILED;
	}

      LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
      addr.vfid = &btid->vfid;
      addr.pgptr = root_page;
      addr.offset = 0;

      for (int idx = 0; idx < n_vectors; idx++)
	{
	  char *wal_data = static_cast<char *> (malloc (wal_data_size));
	  if (wal_data == NULL)
	    {
	      pgbuf_unfix_and_init (thread_p, root_page);
	      return ER_FAILED;
	    }

	  const OID *cur_oid = &oid[idx];
	  int packed_size = hnsw_pack_insert_data (wal_data, wal_data_size, btid, cur_oid);
	  if (packed_size != wal_data_size)
	    {
	      free (wal_data);
	      pgbuf_unfix_and_init (thread_p, root_page);
	      return ER_FAILED;
	    }

	  fprintf (stderr,
		   "[HNSW_DEBUG] WAL logging insert redo: BTID(vfid=%d:%d, pageid=%d), "
		   "OID(%d:%d:%d), wal_data_size=%d\n",
		   btid->vfid.volid, btid->vfid.fileid, btid->root_pageid,
		   cur_oid->volid, cur_oid->pageid, cur_oid->slotid, wal_data_size);
	  fflush (stderr);

	  log_append_redo_data (thread_p, RVHNSW_INSERT_ELEMENT, &addr, wal_data_size, wal_data);
	  free (wal_data);
	}

      pgbuf_unfix_and_init (thread_p, root_page);
    }

  return index->add (thread_p, n_vectors, oid, vector);
}

/*
 * hnsw_rv_redo_insert_element() - Recovery function for HNSW vector insertion.
 *
 * return      : Error code.
 * thread_p(in) : Thread entry.
 * rcv(in)     : Recovery data containing BTID and OID.
 *
 * NOTE: This is a REDO-only recovery function following CUBRID patterns.
 *
 *       Recovery flow (to be implemented):
 *       1. Unpack BTID and OID from log data
 *       2. Fetch vector from heap using OID
 *       3. Re-insert vector into the index
 */
int
hnsw_rv_redo_insert_element (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  fprintf (stderr, "[HNSW_DEBUG] REDO called. rcv_length=%d\n", (rcv != NULL) ? rcv->length : -1);

  if (rcv == NULL || rcv->data == NULL)
    {
      fprintf (stderr, "[HNSW_DEBUG] REDO payload invalid (got=%d)\n", (rcv != NULL) ? rcv->length : -1);
      fflush (stderr);
      return ER_FAILED;
    }

  const BTID *btid = NULL;
  const OID *oid = NULL;
  hnsw_build_params params;
  std::vector<float> vector_data;

  if (rcv->length == (int) sizeof (HNSW_INSERT_LOG_DATA))
    {
      const HNSW_INSERT_LOG_DATA *log_data = (const HNSW_INSERT_LOG_DATA *) rcv->data;
      btid = &log_data->btid;
      oid = &log_data->oid;
    }

  if (btid == NULL || oid == NULL)
    {
      fprintf (stderr, "[HNSW_DEBUG] REDO payload invalid for OID/BTID decode (got=%d)\n", rcv->length);
      fflush (stderr);
      return ER_FAILED;
    }

  int error = hnsw_get_vector_by_oid (thread_p, btid, oid, vector_data);
  if (error != NO_ERROR)
    {
      fprintf (stderr,
	       "[HNSW_DEBUG] REDO failed to fetch vector from OID(%d:%d:%d), error=%d\n",
	       oid->volid, oid->pageid, oid->slotid, error);
      fflush (stderr);
      return error;
    }
  if (vector_data.empty ())
    {
      fprintf (stderr, "[HNSW_DEBUG] REDO fetched empty vector for OID(%d:%d:%d)\n",
	       oid->volid, oid->pageid, oid->slotid);
      fflush (stderr);
      return ER_FAILED;
    }

  error = hnsw_get_build_params_from_root_page (thread_p, rcv->pgptr, params);
  if (error != NO_ERROR)
    {
      params = hnsw_build_params ((int) vector_data.size (), 16, 64, DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE);
      if (rcv->pgptr != NULL && pgbuf_get_page_ptype (thread_p, rcv->pgptr) == PAGE_HNSW)
	{
	  HNSW_HEADER header;
	  header.dimension = params.dimension;
	  header.hnsw_M = params.m;
	  header.hnsw_efConstruction = params.ef_construction;
	  header.metric = static_cast<int> (params.metric);
	  (void) hnsw_init_header (thread_p, const_cast<VFID *> (&btid->vfid), rcv->pgptr, &header);
	}
      fprintf (stderr,
	       "[HNSW_DEBUG] REDO root header unavailable; using temporary defaults for OID(%d:%d:%d), dimension=%d\n",
	       oid->volid, oid->pageid, oid->slotid, params.dimension);
      fflush (stderr);
    }

  if (params.dimension != (int) vector_data.size ())
    {
      fprintf (stderr,
	       "[HNSW_DEBUG] REDO vector dimension mismatch for OID(%d:%d:%d), root=%d, heap=%d\n",
	       oid->volid, oid->pageid, oid->slotid, params.dimension, (int) vector_data.size ());
      fflush (stderr);
      return ER_FAILED;
    }

  fprintf (stderr,
           "[HNSW_DEBUG] REDO payload BTID(vfid=%d:%d, pageid=%d), OID(%d:%d:%d), dimension=%d\n",
           btid->vfid.volid, btid->vfid.fileid, btid->root_pageid,
           oid->volid, oid->pageid, oid->slotid, (int) vector_data.size ());

  if (index_manager == nullptr && xhnsw_initialize (thread_p) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  hnsw_index *index = NULL;
  error = index_manager->load_or_create_index_for_recovery (thread_p, btid, params, index);
  if (error != NO_ERROR || index == NULL)
    {
      fprintf (stderr,
	       "[HNSW_DEBUG] REDO failed to load/create index for BTID(vfid=%d:%d, pageid=%d), error=%d\n",
	       btid->vfid.volid, btid->vfid.fileid, btid->root_pageid, error);
      fflush (stderr);
      return error == NO_ERROR ? ER_FAILED : error;
    }

  if (index->prepare_to_add (thread_p, 1, oid, vector_data.data ()) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  error = index->add (thread_p, 1, oid, vector_data.data ());
  if (error != NO_ERROR)
    {
      fprintf (stderr,
               "[HNSW_DEBUG] REDO failed to add element for OID(%d:%d:%d), error=%d\n",
               oid->volid, oid->pageid, oid->slotid, error);
      fflush (stderr);
      return error;
    }

  if (rcv->pgptr != NULL)
    {
      pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
    }

  fprintf (stderr, "[HNSW_DEBUG] REDO succeeded for OID(%d:%d:%d)\n",
           oid->volid, oid->pageid, oid->slotid);
  fflush (stderr);
  return NO_ERROR;
}

int
hnsw_search_element (THREAD_ENTRY *thread_p, BTID *btid, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances)
{
  assert (btid);
  assert (key_dbvalue);
  assert (rec_oids);
  assert (distances);
  assert (k > 0);

  if (index_manager == nullptr && xhnsw_initialize (thread_p) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  hnsw_index *index = index_manager->get_index (btid);
  if (index == nullptr)
    {
      if (index_manager->load_index (thread_p, btid, index) != NO_ERROR)
	{
	  // failed to load index
	  assert (false);
	  return ER_FAILED;
	}
    }

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
  assert (vf != NULL && vf->dim == index->get_dimension());

  int ef_search = prm_get_integer_value (PRM_ID_VECTOR_INDEX_EF_SEARCH);
  return index->search (thread_p, vf->float_array, k, ef_search, rec_oids, distances);
}

// =====================================================================
// hnsw_index_manager implementation
// =====================================================================

hnsw_index_manager::hnsw_index_manager()
{
  auto &f = ::hnsw_backend_registry::factories();
  if (!f.empty())
    {
      assert (f.size() == 1 && "Only one HNSW backend factory must be registered.");
      auto it = f.begin();
      auto id = it->first;
      auto &fn = it->second;
      register_backend (fn ()); // unique_ptr<hnsw_index_backend>
      assert (m_backend != nullptr && "Only one HNSW backend instance must exist.");
    }

}

fs::path
hnsw_index_manager::get_vindex_root_path() const
{
  char db_path[PATH_MAX];
  fileio_get_directory_path (db_path, boot_db_full_name());
  fs::path root_path = fs::path (db_path) / "vindex";

  return root_path;
}

void
hnsw_index_manager::create_index_directory()
{
  if (m_root_path.empty())
    {
      m_root_path = get_vindex_root_path();
    }

  if (!fs::exists (m_root_path))
    {
      fs::create_directory (m_root_path);
    }
}

fs::path
hnsw_index_manager::get_index_file_path (const std::string &prefix, const BTID *btid) const
{
  return m_root_path / (prefix + "_" + std::to_string (btid->root_pageid) + ".bin");
}

fs::path
hnsw_index_manager::get_index_meta_file_path (const std::string &prefix, const BTID *btid) const
{
  return m_root_path / (prefix + "_" + std::to_string (btid->root_pageid) + ".meta");
}

fs::path
hnsw_index_manager::get_index_directory_path() const
{
  return m_root_path;
}

bool
hnsw_index_manager::is_index_file_exists (const std::string &prefix, const BTID *btid) const
{
  return fs::exists (get_index_file_path (prefix, btid));
}

bool
hnsw_index_manager::is_index_meta_file_exists (const std::string &prefix, const BTID *btid) const
{
  return fs::exists (get_index_meta_file_path (prefix, btid));
}

bool
hnsw_index_manager::is_index_loaded (const BTID *btid) const
{
  return m_index_map.find (*btid) != m_index_map.end();
}

int
hnsw_index_manager::add_index (const BTID *btid, hnsw_index *index)
{
  if (is_index_loaded (btid))
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "HNSW Index already exists with ID %d", btid->root_pageid);
      return ER_FAILED;
    }

  m_index_map[*btid] = std::unique_ptr<hnsw_index> (index);
  return NO_ERROR;
}

hnsw_index *
hnsw_index_manager::get_index (const BTID *btid) const
{
  if (is_index_loaded (btid))
    {
      return m_index_map.at (*btid).get();
    }
  return nullptr;
}

int
hnsw_index_manager::delete_index (const BTID *btid)
{
  m_index_map.erase (*btid);
  return NO_ERROR;
}

void
hnsw_index_manager::print_index_info (THREAD_ENTRY *thread_p, const BTID *btid)
{
  if (is_index_loaded (btid))
    {
      m_index_map.at (*btid)->dump (thread_p, stdout);
    }
}

int
hnsw_index_manager::save_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, const hnsw_index_meta &meta)
{
  // if meta file exists, do not overwrite it
  if (is_index_meta_file_exists (meta.backend_id, btid))
    {
      return NO_ERROR;
    }

  // Write meta to file in text mode (with newline) and check for errors
  const auto meta_path = get_index_meta_file_path (meta.backend_id, btid);
  std::ofstream meta_file (meta_path, std::ios::out | std::ios::trunc);
  if (!meta_file)
    {
      // Could not open file for writing
      return ER_FAILED;
    }
  meta_file << meta << std::endl;
  if (!meta_file)
    {
      // Write failed
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
hnsw_index_manager::load_index_meta (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index_meta &meta)
{
  // The backend_id is not known before reading the file, so we must try all possible meta files.
  // For now, try all registered backends.
  for (const auto &pair : hnsw_backend_registry::factories())
    {
      const std::string &backend_id = pair.first;
      const auto meta_path = get_index_meta_file_path (backend_id, btid);
      std::ifstream meta_file (meta_path, std::ios::in);
      if (!meta_file.is_open())
	{
	  continue;
	}
      hnsw_index_meta temp_meta;
      meta_file >> temp_meta;
      if (!meta_file)
	{
	  meta_file.close();
	  continue;
	}
      meta_file.close();
      // Found and successfully read meta
      meta = temp_meta;
      return NO_ERROR;
    }
  hnsw_index_backend *backend = get_backend ();
  if (backend == nullptr)
    {
      assert (false);
      return ER_FAILED;
    }

  VPID root_vpid;
  root_vpid.volid = btid->vfid.volid;
  root_vpid.pageid = btid->root_pageid;

  PAGE_PTR root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (root_page == NULL)
    {
      return ER_FAILED;
    }

  HNSW_HEADER *header = hnsw_get_header (thread_p, root_page);
  if (header == NULL)
    {
      pgbuf_unfix_and_init (thread_p, root_page);
      return ER_FAILED;
    }

  meta.backend_id = backend->get_id ();
  meta.build_params = hnsw_build_params (header->dimension, header->hnsw_M, header->hnsw_efConstruction,
					 static_cast<DB_VECTOR_DISTANCE_METRIC> (header->metric));
  pgbuf_unfix_and_init (thread_p, root_page);
  return NO_ERROR;
}

int
hnsw_index_manager::save_all_indices (THREAD_ENTRY *thread_p)
{
  for (const auto &pair : m_index_map)
    {
      const BTID *btid = &pair.first;

      hnsw_index_meta meta;
      meta.backend_id = pair.second->get_backend().get_id();
      meta.build_params = pair.second->get_build_params();

      save_index_meta (thread_p, btid, meta);
      hnsw_index *index = pair.second.get();
      save_index (thread_p, index);
    }
  return NO_ERROR;
}
int hnsw_index_manager::save_index (THREAD_ENTRY *thread_p, hnsw_index *index)
{
  std::string prefix = index->get_backend().get_id();
  const BTID &btid = index->get_id();
  index->save (thread_p, get_index_file_path (prefix, &btid).string());
  return NO_ERROR;
}

int hnsw_index_manager::load_index (THREAD_ENTRY *thread_p, const BTID *btid, hnsw_index *&index_out)
{
  if (is_index_loaded (btid))
    {
      return NO_ERROR;
    }

  hnsw_index_meta meta;
  int error = load_index_meta (thread_p, btid, meta);
  if (error != NO_ERROR)
    {
      _er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index meta with ID %d", btid->root_pageid);
      return error;
    }

  hnsw_index_backend *backend = get_backend ();
  if (backend == nullptr)
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index backend with ID %s", meta.backend_id.c_str());
      return ER_FAILED;
    }

  index_out = backend->create_index (thread_p, btid, meta.backend_id, meta.build_params);
  if (!index_out)
    {
      return ER_FAILED;
    }
  if (is_index_file_exists (meta.backend_id, btid)
      && index_out->load (thread_p, get_index_file_path (meta.backend_id, btid).string()) != NO_ERROR)
    {
      return ER_FAILED;
    }
  add_index (btid, index_out);
  return NO_ERROR;
}

int
hnsw_index_manager::load_or_create_index_for_recovery (THREAD_ENTRY *thread_p, const BTID *btid,
						       const hnsw_build_params &params,
						       hnsw_index *&index_out)
{
  index_out = get_index (btid);
  if (index_out != NULL)
    {
      return NO_ERROR;
    }

  hnsw_index_backend *backend = get_backend ();
  if (backend == nullptr)
    {
      assert (false);
      return ER_FAILED;
    }

  std::string backend_id = backend->get_id ();
  if (log_is_in_crash_recovery ())
    {
      index_out = backend->create_index (thread_p, btid, backend_id, params);
      if (index_out == NULL)
	{
	  return ER_FAILED;
	}

      return add_index (btid, index_out);
    }

  int error = load_index (thread_p, btid, index_out);
  if (error == NO_ERROR)
    {
      return NO_ERROR;
    }

  index_out = backend->create_index (thread_p, btid, backend_id, params);
  if (index_out == NULL)
    {
      return ER_FAILED;
    }

  return add_index (btid, index_out);
}

int hnsw_index_manager::delete_index_on_disk (THREAD_ENTRY *thread_p, const std::string &prefix, const BTID *btid)
{
  if (is_index_file_exists (prefix, btid))
    {
      fs::remove (get_index_file_path (prefix, btid));
    }
  if (is_index_meta_file_exists (prefix, btid))
    {
      fs::remove (get_index_meta_file_path (prefix, btid));
    }

  delete_index (btid);

  file_postpone_destroy (thread_p, &btid->vfid);

  return NO_ERROR;
}


BTID
hnsw_index_manager::create_btid (THREAD_ENTRY *thread_p, const hnsw_index_backend *backend, const OID *class_oid,
				 int attr_id, const hnsw_build_params &params)
{
  BTID btid_out = {.vfid = VFID_INITIALIZER, .root_pageid = -1};
  BTID *btid = &btid_out;

  if (backend->is_disk_index ())
    {
      if (hnsw_create_file (thread_p, const_cast<OID *> (class_oid), attr_id, btid) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  // TODO: null BTID
	  return btid_out;
	}

      PAGE_PTR page_ptr = NULL;
      VPID root_vpid;

      root_vpid.volid = btid->vfid.volid;
      root_vpid.pageid = btid->root_pageid;

      page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_ptr == NULL)
	{
	  // TODO
	  ASSERT_ERROR ();
	  return btid_out;
	}

#if !defined(NDEBUG)
      pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_HNSW);
#endif /* !NDEBUG */

      HNSW_HEADER hnsw_header;
      hnsw_header.dimension = params.dimension;
      hnsw_header.hnsw_M = params.m;
      hnsw_header.hnsw_efConstruction = params.ef_construction;
      hnsw_header.metric = static_cast<int> (params.metric);

      if (hnsw_init_header (thread_p, &btid->vfid, page_ptr, &hnsw_header) != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  // TODO: null BTID
	  return btid_out;
	}

      pgbuf_set_dirty (thread_p, page_ptr, FREE);
      page_ptr = NULL;

      hnsw_print_index_info (btid, &hnsw_header);
    }
  else
    {
      btid->root_pageid = m_last_index_id;
      while (is_index_loaded (btid) || is_index_meta_file_exists (backend->get_id(), btid))
	{
	  btid->root_pageid = ++m_last_index_id;
	}
    }

  return btid_out;
}

void hnsw_index_manager::finalize ()
{
  m_index_map.clear ();
}

void hnsw_index_manager::register_backend (std::unique_ptr<hnsw_index_backend> backend)
{
  m_backend = std::move (backend);
}

const hnsw_index_backend *hnsw_index_manager::get_backend () const
{
  return m_backend.get();
}

hnsw_index_backend *
hnsw_index_manager::get_backend ()
{
  return m_backend.get();
}

static int
hnsw_create_file (THREAD_ENTRY *thread_p, OID *class_oid, int attr_id, BTID *btid)
{
  FILE_DESCRIPTORS des;
  VPID vpid_root;
  int error_code = NO_ERROR;

  memset (&des, 0, sizeof (des));
  des.btree.class_oid = *class_oid;
  des.btree.attr_id = attr_id;

  error_code = file_create_with_npages (thread_p, FILE_HNSW, 1, &des, &btid->vfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  log_sysop_start (thread_p);
  error_code = file_alloc_sticky_first_page (thread_p, &btid->vfid, hnsw_initalize_new_page, NULL, &vpid_root, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      return error_code;
    }
  if (vpid_root.volid != btid->vfid.volid)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      return ER_FAILED;
    }
  btid->root_pageid = vpid_root.pageid;

  log_sysop_commit (thread_p);
  return NO_ERROR;
}

static int
hnsw_initalize_new_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
{
  pgbuf_set_page_ptype (thread_p, page, PAGE_HNSW);
  spage_initialize (thread_p, page, UNANCHORED_KEEP_SEQUENCE, HNSW_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
  pgbuf_set_dirty (thread_p, page, DONT_FREE);

  return NO_ERROR;
}

static int
hnsw_init_header (THREAD_ENTRY *thread_p, VFID *vfid, PAGE_PTR page_ptr, HNSW_HEADER *hnsw_header)
{
  RECDES rec;
  char copy_rec_buf[IO_MAX_PAGE_SIZE + HNSW_MAX_ALIGN];

  rec.area_size = DB_PAGESIZE;
  rec.data = PTR_ALIGN (copy_rec_buf, HNSW_MAX_ALIGN);

  if (hnsw_pack_header (&rec, hnsw_header) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }

  if (spage_insert_at (thread_p, page_ptr, HNSW_HEADER_NUM, &rec) != SP_SUCCESS)
    {
      ASSERT_ERROR ();
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
hnsw_pack_header (RECDES *rec, HNSW_HEADER *hnsw_header)
{
  int fixed_size = (int) sizeof (HNSW_HEADER);

  memcpy (rec->data, hnsw_header, fixed_size);

  rec->length = fixed_size;
  rec->type = REC_HOME;
  return NO_ERROR;
}

static HNSW_HEADER *
hnsw_get_header (THREAD_ENTRY *thread_p, PAGE_PTR page_ptr)
{
  RECDES header_record;
  HNSW_HEADER *hnsw_header = NULL;

  assert (page_ptr != NULL);

#if !defined(NDEBUG)
  (void) pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_HNSW);
#endif

  if (spage_get_record (thread_p, page_ptr, HNSW_HEADER_NUM, &header_record, PEEK) != S_SUCCESS)
    {
      assert_release (false);
      return NULL;
    }

  hnsw_header = (HNSW_HEADER *) header_record.data;

  return hnsw_header;
}

static int
hnsw_print_index_info (BTID *btid, HNSW_HEADER *hnsw_header)
{
  std::ostringstream oss;

  oss << "HNSW Index Information for ID: (" << btid->vfid.volid << ":" << btid->vfid.fileid << ":" << btid->root_pageid <<
      ")\n";
  oss << "  - Dimension: " << hnsw_header->dimension << "\n";
  oss << "  - Metric Type: " << hnsw_header->metric << "\n";
  oss << "  - HNSW efConstruction: " << hnsw_header->hnsw_efConstruction << "\n";

  fprintf (stdout, "%s", oss.str().c_str());
  fflush (stdout);

  return NO_ERROR;
}
