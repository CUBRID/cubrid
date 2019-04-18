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

#include "btree_unique.hpp"

#include "string_buffer.hpp"

#include <utility>

btree_unique_stats::btree_unique_stats (stat_type keys, stat_type nulls /* = 0 */)
  : m_rows (keys + nulls)
  , m_keys (keys)
  , m_nulls (nulls)
{
}

btree_unique_stats::btree_unique_stats ()
  : btree_unique_stats (0, 0)
{
}

btree_unique_stats::stat_type
btree_unique_stats::get_key_count () const
{
  return m_keys;
}

btree_unique_stats::stat_type
btree_unique_stats::get_row_count () const
{
  return m_rows;
}

btree_unique_stats::stat_type
btree_unique_stats::get_null_count () const
{
  return m_nulls;
}

void
btree_unique_stats::add_key_and_row ()
{
  ++m_keys;
  ++m_rows;
}

void
btree_unique_stats::add_null_and_row ()
{
  ++m_nulls;
  ++m_rows;
}

void
btree_unique_stats::add_row ()
{
  ++m_rows;
}

void
btree_unique_stats::delete_key_and_row ()
{
  --m_keys;
  --m_rows;
}

void
btree_unique_stats::delete_null_and_row ()
{
  --m_nulls;
  --m_rows;
}

void
btree_unique_stats::delete_row ()
{
  --m_rows;
}

bool
btree_unique_stats::is_zero () const
{
  return m_keys == 0 && m_nulls == 0;
}

bool
btree_unique_stats::is_unique () const
{
  return m_rows == m_keys + m_nulls;
}

btree_unique_stats &
btree_unique_stats::operator= (const btree_unique_stats &us)
{
  m_rows = us.m_rows;
  m_keys = us.m_keys;
  m_nulls = us.m_nulls;

  return *this;
}

void
btree_unique_stats::operator+= (const btree_unique_stats &us)
{
  m_rows += us.m_rows;
  m_keys += us.m_keys;
  m_nulls += us.m_nulls;
}

void
btree_unique_stats::operator-= (const btree_unique_stats &us)
{
  m_rows -= us.m_rows;
  m_keys -= us.m_keys;
  m_nulls -= us.m_nulls;
}

void
btree_unique_stats::to_string (string_buffer &strbuf) const
{
  strbuf ("oids=%d keys=%d nulls=%d", m_rows, m_keys, m_nulls);
}

void
multi_index_unique_stats::construct ()
{
  new (this) multi_index_unique_stats ();
}

void
multi_index_unique_stats::destruct ()
{
  this->~multi_index_unique_stats ();
}

void
multi_index_unique_stats::accumulate (const BTID &index, const btree_unique_stats &us)
{
  m_stats_map[index] += us;
}

void
multi_index_unique_stats::add_empty (const BTID &index)
{
  m_stats_map[index] = btree_unique_stats ();
}

void
multi_index_unique_stats::clear ()
{
  m_stats_map.clear ();
}

const multi_index_unique_stats::container_type &
multi_index_unique_stats::get_map () const
{
  return m_stats_map;
}

bool
multi_index_unique_stats::empty () const
{
  return m_stats_map.empty ();
}

btree_unique_stats &
multi_index_unique_stats::get_stats_of (const BTID &index)
{
  return m_stats_map[index];
}

void
multi_index_unique_stats::to_string (string_buffer &strbuf) const
{
  strbuf ("{");
  for (container_type::const_iterator it = m_stats_map.cbegin (); it != m_stats_map.cend (); ++it)
    {
      if (it != m_stats_map.cbegin ())
	{
	  strbuf (", ");
	}
      strbuf ("{btid=%d|%d|%d, ", it->first.root_pageid, it->first.vfid.volid, it->first.vfid.fileid);
      it->second.to_string (strbuf);
      strbuf ("}");
    }
  strbuf ("}");
}

multi_index_unique_stats &
multi_index_unique_stats::operator= (multi_index_unique_stats &&other)
{
  m_stats_map = std::move (other.m_stats_map);
  return *this;
}

void
multi_index_unique_stats::operator+= (const multi_index_unique_stats &other)
{
  // collector all stats from other.m_stats_map
  for (const auto &it : other.m_stats_map)
    {
      m_stats_map[it.first] += it.second;
    }
}
