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

namespace cubschema
{
  /*
   * In charge of truncating a class.
   * During that, it can truncate several classes related to the class with FK or partitioning.
   */
  class class_truncator
  {
    public:
      class_truncator (MOP class_mop) : m_class_mop (class_mop) {}

      int truncate (const bool is_cascade);

    private:
      int collect_trun_classes (MOP class_mop, const bool is_cascade);

      MOP m_class_mop;
      std::unordered_set<OID> m_trun_classes;
  };
}

#endif /* _SCHEMA_CLASS_TRUNCATOR_HPP_ */
