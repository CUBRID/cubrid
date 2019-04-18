/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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

    btree_unique_stats () = default;
    btree_unique_stats (stat_type keys, stat_type nulls = 0);

    stat_type get_key_count () const;
    stat_type get_row_count () const;
    stat_type get_null_count () const;

    void add_key_and_row ();
    void add_null_and_row ();
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
	return a.root_pageid < b.root_pageid || a.vfid.volid < b.vfid.volid;
      }
    };

  public:
    multi_index_unique_stats () = default;
    ~multi_index_unique_stats () = default;

    using container_type = std::map<BTID, btree_unique_stats, btid_comparator>;

    void construct ();
    void destruct ();

    void accumulate (const BTID &index, const btree_unique_stats &us);
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
