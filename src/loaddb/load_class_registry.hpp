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

  class attribute
  {
    public:
      attribute () = delete; // Not DefaultConstructible
      attribute (const std::string &name, std::size_t index, or_attribute *repr);

      attribute (attribute &&other) = delete; // Not MoveConstructible
      attribute (const attribute &copy) = delete; // Not CopyConstructible

      attribute &operator= (attribute &&other) = delete; // Not MoveAssignable
      attribute &operator= (const attribute &copy) = delete; // Not CopyAssignable

      const char *get_name () const;
      std::size_t get_index () const;
      const or_attribute &get_repr () const;
      const tp_domain &get_domain () const;

    private:
      const std::string m_name;
      const std::size_t m_index;
      const or_attribute *m_repr;
  };

  class class_entry
  {
    public:
      class_entry () = delete; // Not DefaultConstructible
      class_entry (std::string &class_name, OID &class_oid, class_id clsid, std::vector<const attribute *> &attributes);
      class_entry (std::string &class_name, class_id clsid, bool is_ignored);
      ~class_entry ();

      class_entry (class_entry &&other) = delete; // Not MoveConstructible
      class_entry (const class_entry &copy) = delete; // Not CopyConstructible
      class_entry &operator= (class_entry &&other) = delete; // Not MoveAssignable
      class_entry &operator= (const class_entry &copy) = delete; // Not CopyAssignable

      const OID &get_class_oid () const;
      const char *get_class_name () const;
      const attribute &get_attribute (std::size_t index) const;
      size_t get_attributes_size () const;
      bool is_ignored () const;

    private:
      class_id m_clsid;
      OID m_class_oid;
      std::string m_class_name;
      std::vector<const attribute *> m_attributes;
      const bool m_is_ignored;
  };

  class class_registry
  {
    public:
      class_registry ();
      ~class_registry ();

      class_registry (class_registry &&other) = delete; // Not MoveConstructible
      class_registry (const class_registry &copy) = delete; // Not CopyConstructible

      class_registry &operator= (class_registry &&other) = delete; // Not MoveAssignable
      class_registry &operator= (const class_registry &copy) = delete; // Not CopyAssignable

      const class_entry *get_class_entry (class_id clsid);
      void get_all_class_entries (std::vector<const class_entry *> &entries) const;
      void register_class (const char *class_name, class_id clsid, OID class_oid,
			   std::vector<const attribute *> &attributes);
      void register_ignored_class (class_entry *cls_entry, class_id cls_id);

    private:
      using class_map = std::map<class_id, const class_entry *, std::greater<class_id>>;

      std::mutex m_mutex;
      class_map m_class_by_id;

      const class_entry *get_class_entry_without_lock (class_id clsid) ;
  };

} // namespace cubload

#endif /* _LOAD_CLASS_REGISTRY_HPP_ */
