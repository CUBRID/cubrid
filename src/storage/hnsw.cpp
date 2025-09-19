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

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// singleton instances
// TODO: dynamically load backend implementations
static hnsw_index_manager *index_manager = nullptr;

int
xhnsw_initialize (THREAD_ENTRY *thread_p)
{
  index_manager = &hnsw_index_manager::instance();
  index_manager->create_index_directory();

  return NO_ERROR;
}

int
xhnsw_finalize (THREAD_ENTRY *thread_p)
{
  index_manager->save_all_indices (thread_p);
  return NO_ERROR;
}

int
xhnsw_add_index (THREAD_ENTRY *thread_p, const hnsw_build_params &params, BTID &btid_out)
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

  btid_out = index_manager->create_btid (backend_instance);

  hnsw_index *index = backend_instance->create_index (thread_p, &btid_out, "", params);
  if (index == nullptr)
    {
      // failed to create index
      assert (false);
      return ER_FAILED;
    }

  int error = index_manager->add_index (&btid_out, index);
  if (error != NO_ERROR)
    {
      // failed to add index
      assert (false);
      return ER_FAILED;
    }

#if !defined(NDEBUG)
  _er_log_debug (ARG_FILE_LINE, "HNSW Index added with ID %d", btid_out.root_pageid);

  index_manager->print_index_info (&btid_out);
#endif

  return error;
}

int
xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
  return index_manager->delete_index_on_disk (index_manager->get_backend()->get_id(), btid);
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

  if (xhnsw_add_index (thread_p, params, new_btid) != NO_ERROR)
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

  if (index->prepare_to_add (n_vectors, oid, vector) != NO_ERROR)
    {
      assert (false);
      return ER_FAILED;
    }

  return index->add (n_vectors, oid, vector);
}

int
hnsw_search_element (THREAD_ENTRY *thread_p, BTID *btid, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances)
{
  assert (btid);
  assert (key_dbvalue);
  assert (rec_oids);
  assert (distances);
  assert (k > 0);

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
  return index->search (vf->float_array, k, ef_search, rec_oids, distances);
}