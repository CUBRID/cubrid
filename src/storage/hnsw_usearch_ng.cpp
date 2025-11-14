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
#include "heap_file.h"
#include <cstddef>
#include <fstream>
#include <filesystem>

#include <usearch/index.hpp>
#include <usearch/index_dense.hpp>
#include <usearch/index_plugins.hpp>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

struct hnsw_root_header
{
    UINT16 dimensions;
    UINT16 m;
    UINT16 ef_construction;
    UINT16 entry_level;
    VPID entry_vpid;
};

struct hnsw_node_header
{
    UINT8 level;
    INT64 id;

};

struct hnsw_node_data
{
    VPID 
};

class hnsw_index_t
{
  public:
    struct hnsw_add_result_t
    {
      void tmp;
    };

    hnsw_add_result_t add(OID *oid, const void *vector, size_t n_vectors)
    {
      
    }

  private:
    
};
