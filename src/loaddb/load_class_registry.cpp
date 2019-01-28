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

/*
 * load_class_registry.cpp - class registry for server loaddb
 */

#include "load_class_registry.hpp"

namespace cubload
{

  // class_entry
  class_entry::class_entry (std::string &class_name, OID &class_oid, class_id clsid, int attr_count)
    : m_clsid (clsid)
    , m_class_oid (class_oid)
    , m_class_name (std::move (class_name))
    , m_attr_count (attr_count)
    , m_attr_count_checker (0)
    , m_attributes ()
  {
    //
  }

  void
  class_entry::register_attribute (ATTR_ID attr_id, std::string attr_name, or_attribute *attr_repr)
  {
    assert (m_attr_count_checker < m_attr_count);

    m_attributes.emplace_back (attr_id, attr_name, attr_repr);
    m_attr_count_checker++;
  }

  const OID &
  class_entry::get_class_oid () const
  {
    return m_class_oid;
  }

  const attribute &
  class_entry::get_attribute (int index)
  {
    // check that all attributes were registered
    assert (m_attr_count_checker == m_attr_count);

    // assert that index is within the range
    assert (0 <= index && ((std::size_t) index) < m_attributes.size ());

    return m_attributes[index];
  }

  // class_registry
  class_registry::class_registry ()
    : m_mutex ()
    , m_class_by_id ()
  {
    //
  }

  class_registry::~class_registry ()
  {
    for (auto &it : m_class_by_id)
      {
	delete it.second;
      }
    m_class_by_id.clear ();
  }

  class_entry *
  class_registry::register_class (const char *class_name, class_id clsid, OID class_oid, int attr_count)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    class_entry *c_entry = get_class_entry_without_lock (clsid);
    if (c_entry != NULL)
      {
	return c_entry;
      }

    std::string c_name (class_name);
    c_entry  = new class_entry (c_name, class_oid, clsid, attr_count);

    m_class_by_id.insert (std::make_pair (clsid, c_entry));

    return c_entry;
  }

  class_entry *
  class_registry::get_class_entry (class_id clsid)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    return get_class_entry_without_lock (clsid);
  }

  class_entry *
  class_registry::get_class_entry_without_lock (class_id clsid)
  {
    auto found = m_class_by_id.find (clsid);
    if (found != m_class_by_id.end ())
      {
	return found->second;
      }
    else
      {
	return NULL;
      }
  }

} // namespace cubload
