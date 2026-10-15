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
 * schema_information_schema_builder.hpp
 */

#ifndef _SCHEMA_INFORMATION_SCHEMA_BUILDER_HPP_
#define _SCHEMA_INFORMATION_SCHEMA_BUILDER_HPP_

#include "schema_information_schema_definition.hpp"

#include <string>

namespace cubschema
{
  struct info_schema_function
  {
    const std::string name;
    const information_schema_definition definition;

    info_schema_function (const std::string &n, const information_schema_definition &def)
      : name {n}, definition {def}
    {}

    // copy
    info_schema_function (const info_schema_function &src) = delete;
    info_schema_function &operator= (const info_schema_function &x) = delete;

    // move constructor
    info_schema_function (info_schema_function &&src)
      : name {std::move (src.name)}
      , definition {std::move (src.definition)}
    {}
  };

  class information_schema_builder
  {
    private:
      information_schema_builder () = default;
      ~information_schema_builder () = default;

    public:
      static MOP create_and_mark_system_class (const std::string_view name);
      static int build_vclass (const MOP class_mop, const information_schema_definition &def);
  };
}

#endif /* _SCHEMA_INFORMATION_SCHEMA_BUILDER_HPP_ */