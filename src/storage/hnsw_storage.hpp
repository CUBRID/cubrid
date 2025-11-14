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

#pragma once

#include "thread_entry.hpp"
#include "oid.h"

namespace cubhnsw
{
    class storage_t
    {
    public:
        storage_t (cubthread::entry *thread_p) : m_thread_p (thread_p)
        {}
        
        
        float* vector_at_ (const OID& oid);

    protected:
        virtual std::byte* get_new_block (VFID& vfid) = 0;
    
        cubthread::entry *m_thread_p;
    };

    class disk_storage_t : public storage_t
    {

    };

    // for mockup
    class memory_storage_t : public storage_t
    {
      public:  

      private:
        std::vector<std::byte*> blocks;
        std::unordered_map <VPID, std::size_t> block_table;
    };
}