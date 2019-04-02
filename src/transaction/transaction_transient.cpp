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

#include "transaction_transient.hpp"

#include "heap_file.h"
#include "oid.h"
#include "memory_private_allocator.hpp"
#include "string_buffer.hpp"

tx_transient_class_entry::tx_transient_class_entry (const char *class_name, const OID &class_oid, const LOG_LSA &lsa)
  : m_class_oid (class_oid)
  , m_last_modified_lsa (lsa)
  , m_class_name (class_name)
{
}

const char *
tx_transient_class_entry::get_classname () const
{
  return m_class_name.c_str ();
}

void
tx_transient_class_registry::add (const char *classname, const OID &class_oid, const LOG_LSA &lsa)
{
  assert (classname != NULL);
  assert (!OID_ISNULL (&class_oid));
  assert (class_oid.volid >= 0);      // is not temp volume

  for (auto &it : m_list)
    {
      if (it.m_class_name == classname && OID_EQ (&it.m_class_oid, &class_oid))
	{
	  it.m_last_modified_lsa = lsa;
	  return;
	}
    }
  m_list.emplace_front (classname, class_oid, lsa);
}

bool
tx_transient_class_registry::has_class (const OID &class_oid) const
{
  for (auto &it : m_list)
    {
      if (OID_EQ (&class_oid, &it.m_class_oid))
	{
	  return true;
	}
    }
  return false;
}

char *
tx_transient_class_registry::to_string () const
{
  const size_t DEFAULT_STRBUF_SIZE = 128;
  string_buffer strbuf (cubmem::PRIVATE_BLOCK_ALLOCATOR, DEFAULT_STRBUF_SIZE);

  strbuf ("{");
  for (list_type::const_iterator it = m_list.cbegin (); it != m_list.cend (); it++)
    {
      if (it != m_list.cbegin ())
	{
	  strbuf (", ");
	}
      strbuf ("name=%s, oid=%d|%d|%d, lsa=%lld|%d", it->m_class_name.c_str (), OID_AS_ARGS (&it->m_class_oid),
	      LSA_AS_ARGS (&it->m_last_modified_lsa));
    }
  strbuf ("}");
  return strbuf.release_ptr ();
}

void
tx_transient_class_registry::map (const map_func_type &func) const
{
  bool stop = false;
  for (const auto &it : m_list)
    {
      func (it, stop);
      if (stop)
	{
	  return;
	}
    }
}

bool
tx_transient_class_registry::empty () const
{
  return m_list.empty ();
}

void
tx_transient_class_registry::decache_heap_repr (const LOG_LSA &downto_lsa)
{
  for (auto &it : m_list)
    {
      if (it.m_last_modified_lsa > downto_lsa)
	{
	  (void) heap_classrepr_decache (NULL, &it.m_class_oid);
	  it.m_last_modified_lsa.set_null ();
	}
    }
}

void
tx_transient_class_registry::clear ()
{
  m_list.clear ();
}
