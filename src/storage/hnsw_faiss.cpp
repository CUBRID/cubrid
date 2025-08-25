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
// hnsw_faiss.cpp - implementation of HNSW index using FAISS
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
#include <fstream>
#include <filesystem>

#include "faiss/IndexHNSW.h"
#include "faiss/IndexIDMap.h"
#include "faiss/index_io.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#include "strict_warnings_on.hpp"
// TODO : When cub_server terminates, hnsw_index_id will be reset to 0.
//        This is not a problem in current implementation, but it may be a problem in the future,
//        such as duplicate hnsw_index_id when cub_server restarts.
//        We need to consider a better way to identify the hnsw index.
int hnsw_index_id = 0;
std::unordered_map<int, std::unique_ptr<faiss::IndexIDMap>> hnsw_index_map;
char hnsw_index_directory[PATH_MAX] = {0};
bool hnsw_index_directory_created = false;
static std::mutex hnsw_elem_mutex;

static int dump_hnsw_index (int hnsw_id, faiss::IndexIDMap *index);
static int get_hnsw_index_file_path (int hnsw_id, char *out_path);
static int create_hnsw_index_directory ();
static bool is_hnsw_index_file_exists (int hnsw_id);
static int load_hnsw_index_from_file (int hnsw_id);
static int hnsw_check_and_load_index (int hnsw_id);
static void get_new_hnsw_index_id ();
static faiss::idx_t encode_oid (const OID &oid);
static OID decode_oid (faiss::idx_t encoded_oid);

BTID *
xhnsw_add_index (THREAD_ENTRY *thread_p, BTID *btid, int dimension = 10, int hnsw_M = 16, int hnsw_efConstruction = 64,
		 enum faiss::MetricType metric_type = faiss::METRIC_L2)
{
  get_new_hnsw_index_id ();

  auto hnsw_index = new faiss::IndexHNSWFlat (dimension, hnsw_M, metric_type);
  hnsw_index->hnsw.efConstruction = hnsw_efConstruction;

  auto index = std::make_unique<faiss::IndexIDMap> (hnsw_index);

  btid->vfid.volid = -1;
  btid->vfid.fileid = -1;
  btid->root_pageid = hnsw_index_id;

  hnsw_index_map[hnsw_index_id] = std::move (index);
  er_log_debug (ARG_FILE_LINE, "HNSW Index added with ID %d", hnsw_index_id);
  hnsw_print_index_info (btid);

  return btid;
}

int xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
  char filepath[PATH_MAX];

  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  int hnsw_id = btid->root_pageid;
  auto it = hnsw_index_map.find (hnsw_id);

  if (it != hnsw_index_map.end())
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index deleted with ID %d", hnsw_id);
      hnsw_print_index_info (btid);

      hnsw_index_map.erase (it);
    }

  if (hnsw_index_directory_created)
    {
      if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
	{
	  _er_log_debug (ARG_FILE_LINE, "Failed to get HNSW Index file path for ID %d", hnsw_id);
	  return ER_FAILED;
	}

      std::filesystem::remove (filepath);
    }

  return NO_ERROR;
}

int hnsw_print_index_info (BTID *btid)
{
  if (!btid)
    {
      return ER_FAILED;
    }

  int hnsw_id = btid->root_pageid;

  if (hnsw_check_and_load_index (hnsw_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  auto it = hnsw_index_map.find (hnsw_id);
  if (it == hnsw_index_map.end() || it->second == nullptr)
    {
      return ER_FAILED;
    }

  std::unique_ptr<faiss::IndexIDMap> &index = it->second;
  auto *hnsw_index = static_cast<faiss::IndexHNSWFlat *> (index->index);

  std::ostringstream oss;

  oss << "HNSW Index Information for ID " << hnsw_id << ":\n";
  oss << "  - Dimension: " << index->d << "\n";
  oss << "  - Metric Type: " << index->metric_type << "\n";
  oss << "  - Total Elements: " << index->ntotal << "\n";
  oss << "  - HNSW efConstruction: " << hnsw_index->hnsw.efConstruction << "\n";
  oss << "  - HNSW efSearch: " << hnsw_index->hnsw.efSearch << "\n";

  /*
  * This works because, in faiss/impl/HNSW.cpp, HNSW is initialized with
  * set_default_probas(M, ...);

  // initialize the assign_probas and cum_nneighbor_per_level to
  // have 2*M links on level 0 and M links on levels > 0
  void HNSW::set_default_probas(int M, float levelMult) {
    int nn = 0;
    cum_nneighbor_per_level.push_back(0);
    for (int level = 0;; level++) {
        float proba = exp(-level / levelMult) * (1 - exp(-1 / levelMult));
        if (proba < 1e-9)
            break;
        assign_probas.push_back(proba);
        nn += level == 0 ? M * 2 : M;
        cum_nneighbor_per_level.push_back(nn);
    }
  }
  */

  // Print neighbor counts
  const auto &neighbor_counts = hnsw_index->hnsw.cum_nneighbor_per_level;
  oss << "  - HNSW neighbor_counts: [";
  for (size_t i = 0; i < neighbor_counts.size(); ++i)
    {
      oss << neighbor_counts[i];
      if (i + 1 < neighbor_counts.size())
	{
	  oss << ", ";
	}
    }
  oss << "]\n";

  if (neighbor_counts.size() > 1)
    {
      oss << "  - HNSW M is assumed to be " << (neighbor_counts[1] / 2) << "\n";
    }

  er_log_debug (ARG_FILE_LINE, "%s", oss.str().c_str());

  return NO_ERROR;
}

void dump_all_hnsw_indices_to_files ()
{
  if (!hnsw_index_directory_created)
    {
      if (create_hnsw_index_directory () != NO_ERROR)
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Failed to create HNSW Index directory");
	  return;
	}
    }

  for (const auto &pair : hnsw_index_map)
    {
      int hnsw_id = pair.first;
      auto &index = pair.second;

      if (dump_hnsw_index (hnsw_id, index.get()) != NO_ERROR)
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Failed to dump HNSW Index with ID %d", hnsw_id);
	}
    }
}

void init_hnsw_index_path ()
{
  char db_path[PATH_MAX];
  fileio_get_directory_path (db_path, boot_db_full_name());
  int written = snprintf (hnsw_index_directory, PATH_MAX, "%s%cvindex", db_path, PATH_SEPARATOR);
  if (written < 0 || written >= PATH_MAX)
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "Failed to create path for HNSW Index directory since path is too long");
    }

  if (std::filesystem::exists (hnsw_index_directory))
    {
      hnsw_index_directory_created = true;
    }
  else
    {
      hnsw_index_directory_created = false;
    }
}

static int dump_hnsw_index (int hnsw_id, faiss::IndexIDMap *index)
{
  char filepath[PATH_MAX];

  if (!index)
    {
      return ER_FAILED;
    }

  if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
    {
      return ER_FAILED;
    }

  faiss::write_index (index, filepath);

  return NO_ERROR;
}

static int get_hnsw_index_file_path (int hnsw_id, char *out_path)
{
  int written = snprintf (out_path, PATH_MAX, "%s%c%s_hnsw_%d.bin", hnsw_index_directory, PATH_SEPARATOR, boot_db_name(),
			  hnsw_id);
  if (written < 0 || written >= PATH_MAX)
    {
      er_log_debug (ARG_FILE_LINE, "Failed to create path for dumping HNSW Index %d since path is too long", hnsw_id);
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int create_hnsw_index_directory ()
{
  char db_path[PATH_MAX];

  if (hnsw_index_directory_created)
    {
      return NO_ERROR;
    }
  else
    {
      fileio_get_directory_path (db_path, boot_db_full_name());
      int written = snprintf (hnsw_index_directory, PATH_MAX, "%s%cvindex", db_path, PATH_SEPARATOR);
      if (written < 0 || written >= PATH_MAX)
	{
	  er_log_debug (ARG_FILE_LINE, "Failed to create path for HNSW Index directory since path is too long");
	  return ER_FAILED;
	}

      if (std::filesystem::exists (hnsw_index_directory))
	{
	  hnsw_index_directory_created = true;
	  return NO_ERROR;
	}

      if (std::filesystem::create_directory (hnsw_index_directory))
	{
	  hnsw_index_directory_created = true;
	  return NO_ERROR;
	}
      else
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Failed to create HNSW Index directory");
	  return ER_FAILED;
	}
    }
}

static bool is_hnsw_index_file_exists (int hnsw_id)
{
  char filepath[PATH_MAX];

  if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
    {
      return false;
    }

  return std::filesystem::exists (filepath);
}

static int load_hnsw_index_from_file (int hnsw_id)
{
  char filepath[PATH_MAX];

  if (!hnsw_index_directory_created)
    {
      if (create_hnsw_index_directory () != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (!std::filesystem::exists (filepath))
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "HNSW Index file does not exist for ID %d in %s", hnsw_id, filepath);
      return ER_FAILED;
    }

  try
    {
      faiss::Index *raw_index = faiss::read_index (filepath);

      faiss::IndexIDMap *idmap = dynamic_cast<faiss::IndexIDMap *> (raw_index);
      if (idmap == nullptr)
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Invalid HNSW Index file format for ID %d in %s", hnsw_id, filepath);

	  delete raw_index;
	  return ER_FAILED;
	}

      hnsw_index_map[hnsw_id] = std::unique_ptr<faiss::IndexIDMap> (idmap);
      er_log_debug (ARG_FILE_LINE, "HNSW Index loaded from file for ID %d in %s", hnsw_id, filepath);

      return NO_ERROR;
    }
  catch (const faiss::FaissException &e)
    {
      er_log_debug (ARG_FILE_LINE, "Failed to load/create HNSW Index %d: %s", hnsw_id, e.what());
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
hnsw_add_element (BTID *btid, OID *oid, float *vector, int n_vectors)
{
  int hnsw_id;
  faiss::idx_t encoded_oid;

  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  //const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);

  hnsw_id = btid->root_pageid;

  if (hnsw_check_and_load_index (hnsw_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  auto it = hnsw_index_map.find (hnsw_id);
  if (it == hnsw_index_map.end() || it->second == nullptr)
    {
      return ER_FAILED;
    }

  try
    {
      std::unique_ptr<faiss::IndexIDMap> &index = it->second;


      std::lock_guard<std::mutex> lock (hnsw_elem_mutex);

      index->add_with_ids (1, vector, &encoded_oid);

      er_log_debug (ARG_FILE_LINE, "Added element with OID %lld to HNSW Index ID %d.",
		    static_cast<long long> (encoded_oid), hnsw_id);
    }
  catch (const faiss::FaissException &e)
    {
      er_log_debug (ARG_FILE_LINE, "FAISS exception during add_with_ids: %s", e.what());
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int hnsw_check_and_load_index (int hnsw_id)
{
  if (hnsw_index_map.find (hnsw_id) == hnsw_index_map.end())
    {
      assert (hnsw_index_directory_created);
      if (load_hnsw_index_from_file (hnsw_id) != NO_ERROR)
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index with ID %d", hnsw_id);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

BTID *xhnsw_load_index (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
			HFID *hfids, int dimension, int m, int ef_construction, int metric)
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

  BTID *new_btid = xhnsw_add_index (thread_p, btid, dimension, m, ef_construction, metric);

  while (cur_class < n_classes && HFID_IS_NULL (&hfids[cur_class]))
    {
      cur_class++;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &hfids[cur_class], &oid[cur_class], true, NULL) != NO_ERROR)
    {
      return NULL;
    }

  attr_offset = cur_class * n_attrs;

  if (heap_attrinfo_start (thread_p, &oid[cur_class], n_attrs, &attr_ids[attr_offset], &attr_info) != NO_ERROR)
    {
      return NULL;
    }

  do
    {
      attr_offset = cur_class * n_attrs;

      scan_result = heap_next (thread_p, &hfids[cur_class], &oid[cur_class], &cur_oid,
			       &in_recdes, &scan_cache,
			       scan_cache.cache_last_fix_page ? PEEK : COPY);

      switch (scan_result)
	{
	case S_SUCCESS:
	  heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &in_recdes, &attr_info);

	  key_dbvalue = &attr_info.values[0].dbvalue;
	  assert (db_value_type (key_dbvalue) == DB_TYPE_VECTOR);

	  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
	  hnsw_add_element (new_btid, &cur_oid, vf->float_array, 1);
	  continue;
	case S_END:
	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  return new_btid;
	  break;
	default:
	  assert (false);
	  return NULL;
	}
    }
  while (true);

  return new_btid;
}

BTID *xhnsw_load_index_batch (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
			      HFID *hfids, int dimension, int m, int ef_construction, int metric)
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

  BTID *new_btid = xhnsw_add_index (thread_p, btid, dimension, m, ef_construction, metric);
  if (new_btid == NULL)
    {
      return NULL;
    }

  /* Find first non-null HFID */
  while (cur_class < n_classes && HFID_IS_NULL (&hfids[cur_class]))
    {
      cur_class++;
    }
  if (cur_class >= n_classes)
    {
      /* Nothing to index */
      return new_btid;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &hfids[cur_class], &oid[cur_class], true, NULL) != NO_ERROR)
    {
      return NULL;
    }

  attr_offset = cur_class * n_attrs;
  if (heap_attrinfo_start (thread_p, &oid[cur_class], n_attrs, &attr_ids[attr_offset], &attr_info) != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      return NULL;
    }

  /* -------- Batch buffers --------
  - oids:    growable array of OID (count elements)
  - vectors: contiguous float buffer of size (capacity * dimension)
  - For API (B) we’ll build vec_ptrs at the end without extra copies.
  */
  int capacity = 1024;                 /* start modestly; will grow as needed */
  int count = 0;
  OID *oids = (OID *) malloc ((size_t) capacity * sizeof (OID));
  float *vectors = (float *) malloc ((size_t) capacity * (size_t) dimension * sizeof (float));
  if (oids == NULL || vectors == NULL)
    {
      free (oids);
      free (vectors);
      heap_attrinfo_end (thread_p, &attr_info);
      (void) heap_scancache_end (thread_p, &scan_cache);
      return NULL;
    }

  /* Utility: ensure capacity for one more item */
  auto ensure_capacity = [&](void) -> bool
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
	/* if one realloc fails, avoid losing original pointers when the other succeeded */
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

  /* -------- Scan & collect -------- */
  do
    {
      attr_offset = cur_class * n_attrs;

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
	    /* Defensive: ensure dimension matches what index expects */
	    assert (vf != NULL && vf->size == dimension);

	    if (!ensure_capacity ())
	      {
		/* OOM during accumulation */
		free (oids);
		free (vectors);
		heap_attrinfo_end (thread_p, &attr_info);
		(void) heap_scancache_end (thread_p, &scan_cache);
		return NULL;
	      }

	    /* Append OID */
	    oids[count] = cur_oid;
	    /* Append vector (contiguous write) */
	    float *dst = vectors + ((size_t) count * (size_t) dimension);
	    memcpy (dst, vf->float_array, (size_t) dimension * sizeof (float));

	    count++;
	  }
	  continue;

	case S_END:
	{
	  hnsw_add_element (new_btid, oids, vectors, count);

	  free (oids);
	  free (vectors);

	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);

	  if (rc != 0)
	    {
	      /* Bulk insert failed */
	      return NULL;
	    }

	  return new_btid;
	}

	default:
	  /* Unexpected scan result */
	  free (oids);
	  free (vectors);
	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  assert (false);
	  return NULL;
	}
    }
  while (true);

  /* Unreachable */
  return new_btid;
}

int hnsw_search_element (int hnsw_id, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances)
{
  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);

  assert (hnsw_id >= 0);

  if (hnsw_check_and_load_index (hnsw_id) != NO_ERROR)
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", hnsw_id);
      assert (false);
      return ER_FAILED;
    }

  auto it = hnsw_index_map.find (hnsw_id);

  if (it == hnsw_index_map.end())
    {
      er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", hnsw_id);
      assert (false);
      return ER_FAILED;
    }

  std::unique_ptr<faiss::IndexIDMap> &index = it->second;

  if (index->metric_type == faiss::METRIC_INNER_PRODUCT && db_vector_is_all_zeros (vf))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  auto *hnsw_index = static_cast<faiss::IndexHNSWFlat *> (index->index);
  hnsw_index->hnsw.efSearch = prm_get_integer_value (PRM_ID_VECTOR_INDEX_EF_SEARCH);

  int64_t *uids = new int64_t[k * 1];
  index->search (1, vf->float_array, k, distances, uids);

  if (uids != nullptr)
    {
      for (int i = 0; i < k; i++)
	{
	  rec_oids[i] = decode_oid (uids[i]);
	}
      delete[] uids;
    }
  return NO_ERROR;
}

static void get_new_hnsw_index_id ()
{
  while (true)
    {
      if (is_hnsw_index_file_exists (hnsw_index_id))
	{
	  hnsw_index_id++;
	  continue;
	}
      else
	{
	  if (hnsw_index_map.find (hnsw_index_id) == hnsw_index_map.end())
	    {
	      break;
	    }
	  else
	    {
	      hnsw_index_id++;
	      continue;
	    }
	}
    }
}

static faiss::idx_t encode_oid (const OID &oid)
{
  return (static_cast<int64_t> (oid.pageid) << 32) |
	 (static_cast<uint32_t> (oid.slotid) << 16) |
	 (static_cast<uint16_t> (oid.volid));
}

static OID decode_oid (faiss::idx_t encoded_oid)
{
  OID oid;
  oid.pageid = static_cast<int32_t> (encoded_oid >> 32);
  oid.slotid = static_cast<int16_t> ((encoded_oid >> 16) & 0xFFFF);
  oid.volid = static_cast<int16_t> (encoded_oid & 0xFFFF);

  return oid;
}

#include "strict_warnings_off.hpp"
