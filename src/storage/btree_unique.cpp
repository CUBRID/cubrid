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

unique_stats::unique_stats (stat_type keys, stat_type nulls /* = 0 */)
  : m_rows (keys + nulls)
  , m_keys (keys)
  , m_nulls (nulls)
{
  check_consistent ();
}

void
unique_stats::check_consistent () const
{
  assert (m_rows == m_keys + m_nulls);
}

unique_stats::stat_type
unique_stats::get_key_count () const
{
  return m_keys;
}

unique_stats::stat_type
unique_stats::get_row_count () const
{
  return m_rows;
}

unique_stats::stat_type
unique_stats::get_null_count () const
{
  return m_nulls;
}

void
unique_stats::add_key ()
{
  ++m_keys;
  ++m_rows;

  check_consistent ();
}

void
unique_stats::add_null ()
{
  ++m_nulls;
  ++m_rows;

  check_consistent ();
}

void
unique_stats::delete_key ()
{
  --m_keys;
  --m_rows;
  check_consistent ();
}

void
unique_stats::delete_null ()
{
  --m_nulls;
  --m_rows;
  check_consistent ();
}

bool
unique_stats::is_zero () const
{
  return m_keys == 0 && m_nulls == 0;
}

unique_stats &
unique_stats::operator= (const unique_stats &us)
{
  m_rows = us.m_rows;
  m_keys = us.m_keys;
  m_nulls = us.m_nulls;

  check_consistent ();

  return *this;
}

void
unique_stats::operator+= (const unique_stats &us)
{
  m_rows += us.m_rows;
  m_keys += us.m_keys;
  m_nulls += us.m_nulls;

  check_consistent ();
}

void
unique_stats::operator-= (const unique_stats &us)
{
  m_rows -= us.m_rows;
  m_keys -= us.m_keys;
  m_nulls -= us.m_nulls;

  check_consistent ();
}

void
unique_stats::to_string (string_buffer &strbuf) const
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
multi_index_unique_stats::accumulate (const BTID &index, const unique_stats &us)
{
  m_stats_map[index] += us;
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
