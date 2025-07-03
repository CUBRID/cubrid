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
// hnsw_usearch.cpp - implementation of HNSW index using USearch
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
#include <fstream>
#include <filesystem>

/// Include immintrin. Otherwise `simsimd` fails to build: `unknown type name '__bfloat16'`
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif
#include "usearch/index.hpp"
#include "usearch/index_dense.hpp"
#include "usearch/index_plugins.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


#include "strict_warnings_on.hpp"// TODO : When cub_server terminates, hnsw_index_id will be reset to 0.
//        This is not a problem in current implementation, but it may be a problem in the future,
//        such as duplicate hnsw_index_id when cub_server restarts.
//        We need to consider a better way to identify the hnsw index.


using namespace unum;

int hnsw_index_id = 0;
std::unordered_map<int, std::unique_ptr<usearch::index_dense_t>> hnsw_index_map;
char hnsw_index_directory[PATH_MAX] = {0};
bool hnsw_index_directory_created = false;
static std::mutex hnsw_elem_mutex;

static int dump_hnsw_index (int hnsw_id, const std::unique_ptr<usearch::index_dense_t> &index);
static int get_hnsw_index_file_path (int hnsw_id, char *out_path);
static int create_hnsw_index_directory ();
static bool is_hnsw_index_file_exists (int hnsw_id);
static int load_hnsw_index_from_file (int hnsw_id);
static int hnsw_check_and_load_index (int hnsw_id);
static void get_new_hnsw_index_id ();
static int64_t encode_oid (const OID &oid);
static OID decode_oid (int64_t encoded_oid);

BTID *
xhnsw_add_index (THREAD_ENTRY *thread_p, BTID *btid, int dimension = 10, int hnsw_M = 16, int hnsw_efConstruction = 64,
		 int metric = DB_VECTOR_DISTANCE_METRIC::METRIC_EUCLIDEAN)
{
  usearch::metric_kind_t metric_kind = usearch::metric_kind_t::unknown_k;
  switch (metric)
    {
    case METRIC_UNKNOWN:
      ASSERT_CUBVEC (false);
    case METRIC_COSINE:
      metric_kind = usearch::metric_kind_t::cos_k;
      break;

    case METRIC_DOT:
      metric_kind = usearch::metric_kind_t::ip_k;
      break;

    case METRIC_EUCLIDEAN:
      metric_kind = usearch::metric_kind_t::l2sq_k;
      break;

    case METRIC_MANHATTAN:
      // unsupported metric
      ASSERT_CUBVEC (false);
      break;

    default:
      ASSERT_CUBVEC (false);
    }

  get_new_hnsw_index_id ();

  usearch::metric_punned_t metric_punned (static_cast <std::size_t> (dimension), metric_kind,
					  usearch::scalar_kind_t::f32_k);

  unum::usearch::index_dense_config_t config(hnsw_M, hnsw_efConstruction, 64 /* default */);
  config.enable_key_lookups = false;

  auto index_ptr = std::make_unique<usearch::index_dense_t> (
			   usearch::index_dense_t::make (metric_punned, config)
		   );

  index_ptr->reserve (10000);

  hnsw_index_map[hnsw_index_id] = std::move (index_ptr);

  btid->vfid.volid = -1;
  btid->vfid.fileid = -1;
  btid->root_pageid = hnsw_index_id;

  _er_log_debug (ARG_FILE_LINE, "HNSW Index added with ID %d", hnsw_index_id);
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

  std::unique_ptr<usearch::index_dense_t> &index = it->second;

  std::ostringstream oss;

  oss << "HNSW Index Information for ID: " << hnsw_id << "\n";
  oss << "  - Dimension: " << index->dimensions() << "\n";
  oss << "  - Metric Type: " << metric_kind_name (index->metric_kind()) << "\n";
  oss << "  - Total Elements: " << index->size() << "\n";
  oss << "  - HNSW efConstruction: " << index->expansion_add() << "\n";
  oss << "  - HNSW efSearch: " << index->expansion_search() << "\n";

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

      if (dump_hnsw_index (hnsw_id, index) != NO_ERROR)
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
      return;
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

static int dump_hnsw_index (int hnsw_id, const std::unique_ptr<usearch::index_dense_t> &index)
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

  index->save (filepath);

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
      auto index = std::make_unique<usearch::index_dense_t> ();
      index->load (filepath);
      // index->view (filepath);

      hnsw_index_map[hnsw_id] = std::move (index);
      er_log_debug (ARG_FILE_LINE, "HNSW Index loaded from file for ID %d in %s", hnsw_id, filepath);

      return NO_ERROR;
    }
  catch (const std::runtime_error &e)
    {
      er_log_debug (ARG_FILE_LINE, "Failed to load/create HNSW Index %d: %s", hnsw_id, e.what());
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
hnsw_add_element (BTID *btid, OID *oid, DB_VALUE *key_dbvalue)
{
  int hnsw_id;
  int64_t encoded_oid = encode_oid (*oid);

  if (!btid)
    {
      assert (false);
      return ER_FAILED;
    }

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);

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
      std::unique_ptr<usearch::index_dense_t> &index = it->second;
      if (index->metric_kind() == usearch::metric_kind_t::cos_k && db_vector_is_all_zeros (vf))
	{
	  er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping add");
	  return NO_ERROR;
	}

      std::lock_guard<std::mutex> lock (hnsw_elem_mutex);
      if (index->size() >= index->capacity())
	{
	  size_t new_cap = index->capacity() * 2;
	  index->reserve (new_cap);
	}

      index->add (encoded_oid, vf->float_array);

      er_log_debug (ARG_FILE_LINE, "Added element with OID %lld to HNSW Index ID %d.",
		    static_cast<long long> (encoded_oid), hnsw_id);
    }
  catch (const std::runtime_error &e)
    {
      er_log_debug (ARG_FILE_LINE, "USearch exception during add_with_ids: %s", e.what());
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

  std::unique_ptr<usearch::index_dense_t> &index = it->second;

  if (index->metric_kind() == usearch::metric_kind_t::cos_k && db_vector_is_all_zeros (vf))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  index->change_expansion_search (prm_get_integer_value (PRM_ID_VECTOR_INDEX_EF_SEARCH));
  auto results = index->search (vf->float_array, k);
  for (std::size_t i = 0; i != results.size(); ++i)
    {
      rec_oids[i] = decode_oid (results[i].member.key);
      distances[i] = results[i].distance;
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

static int64_t encode_oid (const OID &oid)
{
  return (static_cast<int64_t> (oid.pageid) << 32) |
	 (static_cast<uint32_t> (oid.slotid) << 16) |
	 (static_cast<uint16_t> (oid.volid));
}

static OID decode_oid (int64_t encoded_oid)
{
  OID oid;
  oid.pageid = static_cast<int32_t> (encoded_oid >> 32);
  oid.slotid = static_cast<int16_t> ((encoded_oid >> 16) & 0xFFFF);
  oid.volid = static_cast<int16_t> (encoded_oid & 0xFFFF);

  return oid;
}

#include "strict_warnings_off.hpp"
