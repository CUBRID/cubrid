/*
 *
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

#ifndef _SCHEMA_CLASS_TRUNCATOR_HPP_
#define _SCHEMA_CLASS_TRUNCATOR_HPP_

#include "class_object.h"
#include "schema_manager.h"

#include <unordered_set>

extern int sm_truncate_class_internal (std::unordered_set<OID>&& trun_classes);
namespace cubschema
{
  class class_truncator final
  {
  public:
    using cons_predicate = std::function<bool(const SM_CLASS_CONSTRAINT&)>;
    using saved_cons_predicate = std::function<bool(const SM_CONSTRAINT_INFO&)>;

    int save_constraints_or_clear (cons_predicate pred);
    int drop_saved_constraints (saved_cons_predicate pred);
    int truncate_heap ();
    int restore_constraints (saved_cons_predicate pred);
    int reset_serials ();

    class_truncator (const OID& class_oid);
    class_truncator (class_truncator&& other);
    ~class_truncator ();

    class_truncator (const class_truncator& other) = delete;
    class_truncator& operator=(const class_truncator& other) = delete;
    class_truncator& operator=(const class_truncator&& other) = delete;

  private:
    MOP m_mop = NULL;
    SM_CLASS* m_class = NULL;
    SM_CONSTRAINT_INFO* m_unique_info = NULL;
    SM_CONSTRAINT_INFO* m_fk_info = NULL;
    SM_CONSTRAINT_INFO* m_index_info = NULL;
  };
}

#endif /* _SCHEMA_CLASS_TRUNCATOR_HPP_ */
