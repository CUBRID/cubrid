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

#ifndef _TRANSACTION_TRANSIENT_HPP_
#define _TRANSACTION_TRANSIENT_HPP_

#include "lob_locator.hpp"
#include "dbtype_def.h"
#include "log_lsa.hpp"
#include "storage_common.h"

#include <forward_list>
#include <functional>

// todo - namespace cubtx

// forward declarations
struct log_tdes;
namespace cubthread
{
  class entry;
}

//
// Modified classes
//

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

//
// Lobs
//

// forward declarations
struct lob_locator_entry;

// matches RB_HEAD of rb_tree.h
struct lob_rb_root
{
  lob_locator_entry *rbh_root;

  void init ();
};

LOB_LOCATOR_STATE xtx_find_lob_locator (cubthread::entry *thread_p, const char *locator, char *real_locator);
int xtx_add_lob_locator (cubthread::entry *thread_p, const char *locator, LOB_LOCATOR_STATE state);
int xtx_change_state_of_locator (cubthread::entry *thread_p, const char *locator, const char *new_locator,
				 LOB_LOCATOR_STATE state);
int xtx_drop_lob_locator (cubthread::entry *thread_p, const char *locator);
void tx_lob_locator_clear (cubthread::entry *thread_p, log_tdes *tdes, bool at_commit, LOG_LSA *savept_lsa);

#endif // !_TRANSACTION_TRANSIENT_HPP_
