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

//
// Unique b-tree definitions
//

#ifndef _BTREE_UNIQUE_HPP_
#define _BTREE_UNIQUE_HPP_

#include "storage_common.h"

#include <cstdint>
#include <map>

// forward definitions
class string_buffer;

class btree_unique_stats
{
  public:
    using stat_type = std::int64_t;

    btree_unique_stats ();
    btree_unique_stats (stat_type keys, stat_type nulls = 0);

    stat_type get_key_count () const;
    stat_type get_row_count () const;
    stat_type get_null_count () const;

    void insert_key_and_row ();
    void insert_null_and_row ();
    void add_row ();
    void delete_key_and_row ();
    void delete_null_and_row ();
    void delete_row ();

    bool is_zero () const;
    bool is_unique () const;   // rows == keys + nulls

    btree_unique_stats &operator= (const btree_unique_stats &us);
    void operator+= (const btree_unique_stats &us);
    void operator-= (const btree_unique_stats &us);

    void to_string (string_buffer &strbuf) const;

  private:
    // m_rows = m_keys + m_nulls
    stat_type m_rows;
    stat_type m_keys;
    stat_type m_nulls;
};

class multi_index_unique_stats
{
  private:
    struct btid_comparator
    {
      bool operator() (const BTID &a, const BTID &b) const
      {
	return a.root_pageid < b.root_pageid || (a.root_pageid == b.root_pageid && a.vfid.volid < b.vfid.volid);
      }
    };

  public:
    multi_index_unique_stats () = default;
    ~multi_index_unique_stats () = default;

    using container_type = std::map<BTID, btree_unique_stats, btid_comparator>;

    void construct ();
    void destruct ();

    void add_index_stats (const BTID &index, const btree_unique_stats &us);
    void add_empty (const BTID &index);
    void clear ();

    const container_type &get_map () const;
    bool empty () const;

    btree_unique_stats &get_stats_of (const BTID &index);

    void to_string (string_buffer &strbuf) const;

    multi_index_unique_stats &operator= (multi_index_unique_stats &&other);
    multi_index_unique_stats &operator= (const multi_index_unique_stats &other) = delete;
    void operator+= (const multi_index_unique_stats &other);

  private:
    container_type m_stats_map;
};

#endif // !_BTREE_UNIQUE_HPP_
