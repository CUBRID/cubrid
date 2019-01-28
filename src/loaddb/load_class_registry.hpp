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
 * load_class_registry.hpp - class registry for server loaddb
 */

#ifndef _LOAD_CLASS_REGISTRY_HPP_
#define _LOAD_CLASS_REGISTRY_HPP_

#include "load_common.hpp"
#include "object_representation_sr.h"
#include "storage_common.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace cubload
{

  struct attribute
  {
    attribute (ATTR_ID attr_id, std::string attr_name, or_attribute *attr_repr)
      : m_attr_id (attr_id)
      , m_attr_name (std::move (attr_name))
      , m_attr_repr (attr_repr)
    {
      //
    }

    attribute (attribute &&other) noexcept
      : m_attr_id (other.m_attr_id)
      , m_attr_name (std::move (other.m_attr_name))
      , m_attr_repr (other.m_attr_repr)
    {
      //
    }

    attribute (const attribute &copy)
      : m_attr_id (copy.m_attr_id)
      , m_attr_name (copy.m_attr_name)
      , m_attr_repr (copy.m_attr_repr)
    {
      //
    }

    attribute &operator= (attribute &&other) noexcept
    {
      m_attr_id = other.m_attr_id;
      m_attr_name = std::move (other.m_attr_name);
      m_attr_repr = other.m_attr_repr;

      return *this;
    }

    attribute &operator= (const attribute &copy)
    {
      m_attr_id = copy.m_attr_id;
      m_attr_name = copy.m_attr_name;
      m_attr_repr = copy.m_attr_repr;

      return *this;
    }

    ATTR_ID m_attr_id;
    std::string m_attr_name;
    or_attribute *m_attr_repr;
  };

  class class_entry
  {
    public:
      class_entry (std::string &class_name, OID &class_oid, class_id clsid, int attr_count);
      ~class_entry () = default;

      class_entry (class_entry &&other) = delete;
      class_entry (const class_entry &copy) = delete;
      class_entry &operator= (class_entry &&other) = delete;
      class_entry &operator= (const class_entry &copy) = delete;

      void register_attribute (ATTR_ID attr_id, std::string attr_name, or_attribute *attr_repr);

      const OID &get_class_oid () const;
      const attribute &get_attribute (int index);

    private:
      class_id m_clsid;
      OID m_class_oid;
      std::string m_class_name;

      int m_attr_count;
      int m_attr_count_checker;
      std::vector<attribute> m_attributes;
  };

  class class_registry
  {
    public:
      class_registry ();
      ~class_registry ();

      class_registry (class_registry &&other) = delete; // Not MoveConstructible
      class_registry (const class_registry &copy) = delete; // Not CopyConstructible

      class_registry &operator= (class_registry &&other) = delete; // Not MoveAssignable
      class_registry &operator= (const class_registry &copy) = delete;  // Not CopyAssignable

      class_entry *get_class_entry (class_id clsid);
      class_entry *register_class (const char *class_name, class_id clsid, OID class_oid, int attr_count);

    private:
      std::mutex m_mutex;
      std::unordered_map<class_id, class_entry *> m_class_by_id;

      class_entry *get_class_entry_without_lock (class_id clsid);
  };

} // namespace cubload

#endif /* _LOAD_CLASS_REGISTRY_HPP_ */
