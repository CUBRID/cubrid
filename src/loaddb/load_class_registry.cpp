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

#include <algorithm>

namespace cubload
{

  // attribute
  attribute::attribute (const std::string &name, std::size_t index, or_attribute *repr)
    : m_name (name)
    , m_index (index)
    , m_repr (repr)
  {
    //
  }

  const char *
  attribute::get_name () const
  {
    return m_name.c_str ();
  }

  std::size_t
  attribute::get_index () const
  {
    return m_index;
  }

  const or_attribute &
  attribute::get_repr () const
  {
    return *m_repr;
  }

  const tp_domain &
  attribute::get_domain () const
  {
    return *m_repr->domain;
  }

  // class_entry
  class_entry::class_entry (std::string &class_name, OID &class_oid, class_id clsid,
			    std::vector<const attribute *> &attributes)
    : m_clsid (clsid)
    , m_class_oid (class_oid)
    , m_class_name (std::move (class_name))
    , m_attributes (attributes.size ())
    , m_is_ignored (false)
  {
    std::copy (attributes.begin (), attributes.end (), m_attributes.begin ());
  }

  class_entry::class_entry (std::string &class_name, class_id clsid, bool is_ignored)
    : m_clsid (clsid)
    , m_class_name (class_name)
    , m_is_ignored (is_ignored)
  {
    ; // Do nothing
  }

  class_entry::~class_entry ()
  {
    for (const attribute *attr : m_attributes)
      {
	delete attr;
      }
    m_attributes.clear ();
  }

  const OID &
  class_entry::get_class_oid () const
  {
    return m_class_oid;
  }

  const char *
  class_entry::get_class_name () const
  {
    return m_class_name.c_str ();
  }

  const attribute &
  class_entry::get_attribute (std::size_t index) const
  {
    // assert that index is within the range
    assert (index < m_attributes.size ());

    return *m_attributes[index];
  }

  size_t
  class_entry::get_attributes_size () const
  {
    return m_attributes.size ();
  }

  bool
  class_entry::is_ignored () const
  {
    return m_is_ignored;
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

  void
  class_registry::register_class (const char *class_name, class_id clsid, OID class_oid,
				  std::vector<const attribute *> &attributes)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    const class_entry *c_entry = get_class_entry_without_lock (clsid);
    if (c_entry != NULL)
      {
	// class was registered already
	return;
      }

    std::string c_name (class_name);
    c_entry = new class_entry (c_name, class_oid, clsid, attributes);

    m_class_by_id.insert (std::make_pair (clsid, c_entry));
  }

  void
  class_registry::register_ignored_class (class_entry *cls_entry, class_id cls_id)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    assert (cls_entry != NULL);
    assert (cls_entry->is_ignored ());

    m_class_by_id.insert (std::make_pair (cls_id, cls_entry));
  }

  const class_entry *
  class_registry::get_class_entry (class_id clsid)
  {
    std::unique_lock<std::mutex> ulock (m_mutex);

    return get_class_entry_without_lock (clsid);
  }

  void
  class_registry::get_all_class_entries (std::vector<const class_entry *> &entries) const
  {
    std::transform (m_class_by_id.begin (), m_class_by_id.end (), std::back_inserter (entries),
		    std::bind (&class_map::value_type::second, std::placeholders::_1));
  }

  const class_entry *
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
