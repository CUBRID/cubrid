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
 * schema_information_schema_install.hpp
 */

#ifndef _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_
#define _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_

#include <string>

namespace cubschema
{
  // forward definitions
  struct system_metadata_definition;

  namespace information_schema_initializer
  {
    system_metadata_definition get_view_column_privileges ();
    system_metadata_definition get_view_columns ();
    system_metadata_definition get_view_domains ();
    system_metadata_definition get_view_foreign_servers ();
    system_metadata_definition get_view_key_column_usage ();
    system_metadata_definition get_view_parameters ();
    system_metadata_definition get_view_partitions ();
    system_metadata_definition get_view_referential_constraints ();
    system_metadata_definition get_view_routine_privileges ();
    system_metadata_definition get_view_routines ();
    system_metadata_definition get_view_schemata ();
    system_metadata_definition get_view_sequences ();
    system_metadata_definition get_view_statistics ();
    system_metadata_definition get_view_synonyms ();
    system_metadata_definition get_view_table_constraints ();
    system_metadata_definition get_view_table_privileges ();
    system_metadata_definition get_view_tables ();
    system_metadata_definition get_view_triggers ();
    system_metadata_definition get_view_views ();
  }
}

std::string sm_define_view_column_privileges_spec (void);
std::string sm_define_view_columns_spec (void);
std::string sm_define_view_domains_spec (void);
std::string sm_define_view_foreign_servers_spec (void);
std::string sm_define_view_key_column_usage_spec (void);
std::string sm_define_view_parameters_spec (void);
std::string sm_define_view_partitions_spec (void);
std::string sm_define_view_referential_constraints_spec (void);
std::string sm_define_view_routine_privileges_spec (void);
std::string sm_define_view_routines_spec (void);
std::string sm_define_view_schemata_spec (void);
std::string sm_define_view_sequences_spec (void);
std::string sm_define_view_statistics_spec (void);
std::string sm_define_view_synonyms_spec (void);
std::string sm_define_view_table_constraints_spec (void);
std::string sm_define_view_table_privileges_spec (void);
std::string sm_define_view_tables_spec (void);
std::string sm_define_view_triggers_spec (void);
std::string sm_define_view_views_spec (void);

#endif /* _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_ */
