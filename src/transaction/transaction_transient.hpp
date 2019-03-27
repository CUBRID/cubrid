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

#ifndef _TRANSACTION_TRANSIENT_HPP_
#define _TRANSACTION_TRANSIENT_HPP_

#include "dbtype_def.h"
#include "log_lsa.hpp"
#include "storage_common.h"

#include <forward_list>
#include <functional>

// todo - namespace cubtx

struct tx_transient_class_entry
{
  OID m_class_oid;
  LOG_LSA m_last_modified_lsa;
  std::string m_class_name;

  tx_transient_class_entry () = default;
  tx_transient_class_entry (const char *class_name, const OID &class_oid, const LOG_LSA &lsa);

  const char *get_classname () const;
};

class tx_transient_class_registry
{
  private:
    using list_type = std::forward_list<tx_transient_class_entry>;
    list_type m_list;

  public:
    using map_func_type = std::function<void (const tx_transient_class_entry &, bool &)>;

    tx_transient_class_registry () = default;
    ~tx_transient_class_registry () = default;

    // inspection functions
    bool empty () const;
    bool has_class (const OID &class_oid) const;

    // map all entries
    void map (const map_func_type &func) const;

    // utility functions
    char *to_string () const;   // private allocated

    // manipulation functions
    void add (const char *classname, const OID &class_oid, const LOG_LSA &lsa);
    void decache_heap_repr (const LOG_LSA &downto_lsa);
    void clear ();
};

#endif // !_TRANSACTION_TRANSIENT_HPP_
