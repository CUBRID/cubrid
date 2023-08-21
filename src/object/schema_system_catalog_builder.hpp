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


/*
 * schema_system_catalog_builder.hpp
 */

#ifndef _SCHEMA_SYSTEM_CATALOG_BUILDER_HPP_
#define _SCHEMA_SYSTEM_CATALOG_BUILDER_HPP_

#include "schema_system_catalog.hpp"
#include "schema_system_catalog_definition.hpp"

namespace cubschema
{
  // legacy definitions
  typedef int (*DEF_CLASS_FUNCTION) (MOP);
  typedef int (*DEF_VCLASS_FUNCTION) ();

  struct catcls_function
  {
    const char *name;
    union
    {
      const DEF_CLASS_FUNCTION class_func;
      const DEF_VCLASS_FUNCTION vclass_func;
    };

    constexpr catcls_function (const char *n, DEF_CLASS_FUNCTION func)
      : name {n}, class_func (func)
    {}
    constexpr catcls_function (const char *n, DEF_VCLASS_FUNCTION func)
      : name {n}, vclass_func (func)
    {}
  };

  class system_catalog_builder
  {
    public:
      system_catalog_builder ();
      ~system_catalog_builder ();

      int build_class (const system_catalog_definition &def);
      int build_legacy ();
  };
}

#endif // _SCHEMA_SYSTEM_CATALOG_BUILDER_HPP_