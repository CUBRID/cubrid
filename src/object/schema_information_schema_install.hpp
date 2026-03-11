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

#include "schema_information_schema_definition.hpp"

namespace cubschema
{
  class information_schema_initializer
  {
    public:
      static information_schema_definition get_view_column_privileges ();
      static information_schema_definition get_view_columns ();
      static information_schema_definition get_view_domains ();
      static information_schema_definition get_view_foreign_servers ();
      static information_schema_definition get_view_key_column_usage ();
      static information_schema_definition get_view_parameters ();
      static information_schema_definition get_view_partitions ();
      static information_schema_definition get_view_referential_constraints ();
      static information_schema_definition get_view_routine_privileges ();
      static information_schema_definition get_view_routines ();
      static information_schema_definition get_view_schemata ();
      static information_schema_definition get_view_sequences ();
      static information_schema_definition get_view_statistics ();
      static information_schema_definition get_view_synonyms ();
      static information_schema_definition get_view_table_constraints ();
      static information_schema_definition get_view_table_privileges ();
      static information_schema_definition get_view_tables ();
      static information_schema_definition get_view_triggers ();
      static information_schema_definition get_view_views ();
  };
}

const char *sm_define_view_column_privileges_spec (void);
const char *sm_define_view_columns_spec (void);
const char *sm_define_view_domains_spec (void);
const char *sm_define_view_foreign_servers_spec (void);
const char *sm_define_view_key_column_usage_spec (void);
const char *sm_define_view_parameters_spec (void);
const char *sm_define_view_partitions_spec (void);
const char *sm_define_view_referential_constraints_spec (void);
const char *sm_define_view_routine_privileges_spec (void);
const char *sm_define_view_routines_spec (void);
const char *sm_define_view_schemata_spec (void);
const char *sm_define_view_sequences_spec (void);
const char *sm_define_view_statistics_spec (void);
const char *sm_define_view_synonyms_spec (void);
const char *sm_define_view_table_constraints_spec (void);
const char *sm_define_view_table_privileges_spec (void);
const char *sm_define_view_tables_spec (void);
const char *sm_define_view_triggers_spec (void);
const char *sm_define_view_views_spec (void);

#endif /* _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_ */
