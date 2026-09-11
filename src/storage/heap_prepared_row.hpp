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

#ifndef _HEAP_PREPARED_ROW_HPP_
#define _HEAP_PREPARED_ROW_HPP_

#include "heap_file.h"

/* Owns canonical attribute bytes until destination routing and storage finish.
 * Destruction releases memory only; the caller owns transaction rollback. */
class heap_prepared_row
{
  public:
    heap_prepared_row () noexcept;
    ~heap_prepared_row ();
    heap_prepared_row (const heap_prepared_row &) = delete;
    heap_prepared_row &operator= (const heap_prepared_row &) = delete;
    heap_prepared_row (heap_prepared_row &&other) noexcept;
    heap_prepared_row &operator= (heap_prepared_row &&other) noexcept;

    /* REPLACE probes serialize LOB locators without copying the external LOB. */
    int prepare (THREAD_ENTRY *thread_p, HEAP_CACHE_ATTRINFO *attr_info, RECDES *old_recdes = nullptr,
		 bool copy_lobs = true);
    /* Copies supplied canonical bytes; resolves OOS per attribute, without SQL/LOB effects. */
    int prepare_serialized (THREAD_ENTRY *thread_p, const OID *source_class, RECDES *source);
    int read_values (HEAP_CACHE_ATTRINFO *attr_info) const;
    /* Returns an owned logical value from canonical bytes, including selected OOS attributes. */
    int read_value (OR_ATTRIBUTE *attribute, DB_VALUE *value) const;
    int finalize (THREAD_ENTRY *thread_p, const OID *destination);
    RECDES *record ();
    /* Owned allocation sizes, including payloads retained outside the compact record. */
    std::size_t retained_bytes () const noexcept;

  private:
    int prepare_internal (THREAD_ENTRY *thread_p, HEAP_CACHE_ATTRINFO *attr_info, RECDES *old_recdes,
			  bool copy_lobs, RECDES *serialized);
    struct storage;
    storage *m_storage;
};

#if defined(CUBRID_UNIT_TEST_ENABLED)
enum class heap_prepared_row_allocation
{
  owner = 1,
  record = 2
};
void heap_prepared_row_test_fail_allocation_once (heap_prepared_row_allocation boundary);
#endif

#endif /* _HEAP_PREPARED_ROW_HPP_ */
