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
 * btree_load_bulk.cpp - B+-Tree Loader Bulk
 */


//===============================================================
// sort
//===============================================================

namespace cubsort {

    struct sort_config {
        int32_t page_size { DB_PAGESIZE };

        int32_t max_half_files { 4 };
        int32_t min_half_files { 2 };
        int32_t initial_dyn_array_size { 30 };
        int32_t expand_dyn_array_ratio { 1.5 };
        int32_t max_record_length { DB_PAGESIZE - sizeof(SLOTTED_PAGE_HEADER) - sizeof(SLOT) };
        
        int32_t max_workers { 0 }; // 0=auto (cpu), 1=serial
        
        bool eliminate_duplicates { false };
    };

}

// 