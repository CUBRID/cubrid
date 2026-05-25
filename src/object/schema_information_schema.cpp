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

#include "schema_information_schema.hpp"

#include "schema_information_schema_constants.h"
#include "identifier_store.hpp"

#include <string>
#include <vector>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubschema
{
  const std::vector<std::string> &
  get_information_schema_view_names ()
  {
    static const std::vector<std::string> names =
    {
      INFO_SCHEMA_COLUMN_PRIVILEGES_NAME,
      INFO_SCHEMA_COLUMNS_NAME,
      INFO_SCHEMA_DOMAINS_NAME,
      INFO_SCHEMA_FOREIGN_SERVERS_NAME,
      INFO_SCHEMA_KEY_COLUMN_USAGE_NAME,
      INFO_SCHEMA_PARAMETERS_NAME,
      INFO_SCHEMA_PARTITIONS_NAME,
      INFO_SCHEMA_REFERENTIAL_CONS_NAME,
      INFO_SCHEMA_ROUTINE_PRIVILEGES_NAME,
      INFO_SCHEMA_ROUTINES_NAME,
      INFO_SCHEMA_SCHEMATA_NAME,
      INFO_SCHEMA_SEQUENCES_NAME,
      INFO_SCHEMA_STATISTICS_NAME,
      INFO_SCHEMA_SYNONYMS_NAME,
      INFO_SCHEMA_TABLE_CONSTRAINTS_NAME,
      INFO_SCHEMA_TABLE_PRIVILEGES_NAME,
      INFO_SCHEMA_TABLES_NAME,
      INFO_SCHEMA_TRIGGERS_NAME,
      INFO_SCHEMA_VIEWS_NAME
    };
    return names;
  }

  static const cubbase::identifier_store sm_info_schema_view_names (get_information_schema_view_names (), false);
}

bool sm_is_information_schema_views (const std::string_view name)
{
  return cubschema::sm_info_schema_view_names.is_exists (name);
}