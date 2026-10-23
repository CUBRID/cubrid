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
 * schema_system_metadata_builder.hpp
 */

#ifndef _SCHEMA_SYSTEM_METADATA_BUILDER_HPP_
#define _SCHEMA_SYSTEM_METADATA_BUILDER_HPP_

#include "schema_system_metadata_definition.hpp"

namespace cubschema
{

  namespace system_metadata_builder
  {
    MOP create_and_mark_system_class (const std::string_view name);
    int build_class (const MOP class_mop, const system_metadata_definition &def);
    int build_vclass (const MOP class_mop, const system_metadata_definition &def);
    int install_vclasses (const std::vector<system_metadata_definition> &defs);
    int rebuild_vclasses (const std::vector<system_metadata_definition> &defs, const std::string &owner_prefix);
  }
}

#endif // _SCHEMA_SYSTEM_METADATA_BUILDER_HPP_