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

#include "schema_information_schema_install.hpp"

#include "authenticate.h"
#include "dbtype_def.h"
#include "schema_information_schema_builder.hpp"
#include "schema_information_schema_constants.h"
#include "schema_information_schema_definition.hpp"
#include "schema_system_catalog_install.hpp"

/*
 * Column order policy for information_schema views
 * Goal: make SELECT * predictable and keep the layout close to PostgreSQL information_schema.
 * Reference: https://www.postgresql.org/docs/current/information-schema.html
 */

static std::vector<cubschema::info_schema_function> info_schema_list;

void
info_schema_init (void)
{
  using namespace cubschema;

  info_schema_list.emplace_back (INFO_SCHEMA_COLUMN_PRIVILEGES_NAME,
				 information_schema_initializer::get_view_column_privileges ());
  info_schema_list.emplace_back (INFO_SCHEMA_COLUMNS_NAME, information_schema_initializer::get_view_columns ());
  info_schema_list.emplace_back (INFO_SCHEMA_DOMAINS_NAME, information_schema_initializer::get_view_domains ());
  info_schema_list.emplace_back (INFO_SCHEMA_FOREIGN_SERVERS_NAME,
				 information_schema_initializer::get_view_foreign_servers ());
  info_schema_list.emplace_back (INFO_SCHEMA_KEY_COLUMN_USAGE_NAME,
				 information_schema_initializer::get_view_key_column_usage ());
  info_schema_list.emplace_back (INFO_SCHEMA_PARAMETERS_NAME, information_schema_initializer::get_view_parameters ());
  info_schema_list.emplace_back (INFO_SCHEMA_PARTITIONS_NAME, information_schema_initializer::get_view_partitions ());
  info_schema_list.emplace_back (INFO_SCHEMA_REFERENTIAL_CONS_NAME,
				 information_schema_initializer::get_view_referential_constraints ());
  info_schema_list.emplace_back (INFO_SCHEMA_ROUTINE_PRIVILEGES_NAME,
				 information_schema_initializer::get_view_routine_privileges ());
  info_schema_list.emplace_back (INFO_SCHEMA_ROUTINES_NAME, information_schema_initializer::get_view_routines ());
  info_schema_list.emplace_back (INFO_SCHEMA_SCHEMATA_NAME, information_schema_initializer::get_view_schemata ());
  info_schema_list.emplace_back (INFO_SCHEMA_SEQUENCES_NAME, information_schema_initializer::get_view_sequences ());
  info_schema_list.emplace_back (INFO_SCHEMA_STATISTICS_NAME, information_schema_initializer::get_view_statistics ());
  info_schema_list.emplace_back (INFO_SCHEMA_SYNONYMS_NAME, information_schema_initializer::get_view_synonyms ());
  info_schema_list.emplace_back (INFO_SCHEMA_TABLE_CONSTRAINTS_NAME,
				 information_schema_initializer::get_view_table_constraints ());
  info_schema_list.emplace_back (INFO_SCHEMA_TABLE_PRIVILEGES_NAME,
				 information_schema_initializer::get_view_table_privileges ());
  info_schema_list.emplace_back (INFO_SCHEMA_TABLES_NAME, information_schema_initializer::get_view_tables ());
  info_schema_list.emplace_back (INFO_SCHEMA_TRIGGERS_NAME, information_schema_initializer::get_view_triggers ());
  info_schema_list.emplace_back (INFO_SCHEMA_VIEWS_NAME, information_schema_initializer::get_view_views ());
}

int
info_schema_install (void)
{
  int error_code = NO_ERROR;
  const size_t num_vclasses = info_schema_list.size ();
  int save;

  AU_DISABLE (save);
  au_set_user (Au_dba_user);

  using info_builder = cubschema::information_schema_builder;

  for (size_t i = 0; i < num_vclasses; i++)
    {
      MOP class_mop = info_builder::create_and_mark_system_class (info_schema_list[i].name);
      if (class_mop == nullptr)
	{
	  assert (false);
	  error_code = er_errid();
	  goto end;
	}

      error_code = info_builder::build_vclass (class_mop, info_schema_list[i].definition);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);
  info_schema_list.clear ();

  return error_code;
}

namespace cubschema
{
  information_schema_definition
  information_schema_initializer::get_view_column_privileges ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_COLUMN_PRIVILEGES_NAME,
		   // columns
    {
      {"grantor", format_varchar (DB_MAX_USER_LENGTH)},
      {"grantee", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"column_name", format_varchar (255)},
      {"privilege_type", format_varchar (7)},
      {"is_grantable", format_varchar (3)},
      {attribute_kind::QUERY_SPEC, sm_define_view_column_privileges_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_columns ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_COLUMNS_NAME,
		   // columns
    {
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"column_name", format_varchar (255)},
      {"ordinal_position", "integer"},
      {"column_default", format_varchar (255)},
      {"is_nullable", format_varchar (3)},
      {"data_type", format_varchar (16)},
      {"character_maximum_length", "bigint"},
      {"character_octet_length", "bigint"},
      {"numeric_precision", "integer"},
      {"numeric_scale", "integer"},
      {"datetime_precision", "integer"},
      {"character_set_name", format_varchar (32)},
      {"collation_name", format_varchar (32)},
      {"domain_catalog", format_varchar (255)},
      {"domain_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"domain_name", format_varchar (255)},
      {"udt_catalog", format_varchar (255)},
      {"udt_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"udt_name", format_varchar (255)},
      {"extra", format_varchar (255)},
      {"privileges", format_varchar (512)},
      {"column_comment", format_varchar (2048)},
      {"is_generated", format_varchar (3)},
      {"generation_expression", "string"},
      {"is_updatable", format_varchar (3)},
      {"is_visible", format_varchar (3)},
      {attribute_kind::QUERY_SPEC, sm_define_view_columns_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_domains ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_DOMAINS_NAME,
		   // columns
    {
      {"domain_catalog", format_varchar (255)},
      {"domain_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"domain_name", format_varchar (255)},
      {"data_type", format_varchar (16)},
      {"character_maximum_length", "bigint"},
      {"character_octet_length", "bigint"},
      {"character_set_catalog", format_varchar (255)},
      {"character_set_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"character_set_name", format_varchar (32)},
      {"collation_catalog", format_varchar (255)},
      {"collation_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"collation_name", format_varchar (32)},
      {"numeric_precision", "integer"},
      {"numeric_precision_radix", "integer"},
      {"numeric_scale", "integer"},
      {"datetime_precision", "integer"},
      {"domain_default", format_varchar (255)},
      {"udt_catalog", format_varchar (255)},
      {"udt_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"udt_name", format_varchar (255)},
      {"domain_comment", format_varchar (2048)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_domains_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_foreign_servers ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_FOREIGN_SERVERS_NAME,
		   // columns
    {
      {"foreign_server_catalog", format_varchar (255)},
      {"foreign_server_name", format_varchar (255)},
      {"foreign_data_wrapper_catalog", format_varchar (255)},
      {"foreign_data_wrapper_name", format_varchar (255)},
      {"foreign_server_type", "string"},
      {"foreign_server_version", "string"},
      {"authorization_identifier", format_varchar (DB_MAX_USER_LENGTH)},
      {"server_host", format_varchar (255)},
      {"server_port", "integer"},
      {"server_database", format_varchar (255)},
      {"server_user", format_varchar (255)},
      {"server_properties", format_varchar (2048)},
      {"server_comment", format_varchar (1024)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_foreign_servers_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_key_column_usage ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_KEY_COLUMN_USAGE_NAME,
		   // columns
    {
      {"constraint_catalog", format_varchar (255)},
      {"constraint_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"constraint_name", format_varchar (255)},
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"column_name", format_varchar (255)},
      {"ordinal_position", "integer"},
      {"position_in_unique_constraint", "integer"},
      {attribute_kind::QUERY_SPEC, sm_define_view_key_column_usage_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_parameters ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_PARAMETERS_NAME,
		   // columns
    {
      {"specific_catalog", format_varchar (255)},
      {"specific_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"specific_name", format_varchar (255)},
      {"ordinal_position", "integer"},
      {"parameter_mode", format_varchar (5)},
      {"is_result", format_varchar (3)},
      {"parameter_name", format_varchar (255)},
      {"data_type", format_varchar (16)},
      {"character_maximum_length", "bigint"},
      {"character_octet_length", "bigint"},
      {"character_set_name", format_varchar (32)},
      {"collation_name", format_varchar (32)},
      {"numeric_precision", "integer"},
      {"numeric_scale", "integer"},
      {"datetime_precision", "integer"},
      {"dtd_identifier", format_varchar (1024)},
      {"routine_type", format_varchar (9)},
      {"parameter_default", format_varchar (255)},
      {"parameter_comment", format_varchar (1024)},
      {attribute_kind::QUERY_SPEC, sm_define_view_parameters_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_partitions ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_PARTITIONS_NAME,
		   // columns
    {
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"partition_name", format_varchar (255)},
      {"partition_ordinal_position", "integer"},
      {"partition_method", format_varchar (5)},
      {"partition_expression", format_varchar (2048)},
      {"partition_description", "string"},
      {"subpartition_name", format_varchar (255)},
      {"subpartition_ordinal_position", "integer"},
      {"subpartition_method", format_varchar (5)},
      {"subpartition_expression", format_varchar (2048)},
      {"table_rows", "bigint"},
      {"avg_row_length", "bigint"},
      {"data_length", "bigint"},
      {"data_free", "bigint"},
      {"tablespace_name", format_varchar (255)},
      {"partition_comment", format_varchar (1024)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_partitions_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_referential_constraints ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_REFERENTIAL_CONS_NAME,
		   // columns
    {
      {"constraint_catalog", format_varchar (255)},
      {"constraint_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"constraint_name", format_varchar (255)},
      {"unique_constraint_catalog", format_varchar (255)},
      {"unique_constraint_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"unique_constraint_name", format_varchar (255)},
      {"match_option", format_varchar (7)},
      {"update_rule", format_varchar (9)},
      {"delete_rule", format_varchar (9)},
      {"table_name", format_varchar (255)},
      {"referenced_table_name", format_varchar (255)},
      {attribute_kind::QUERY_SPEC, sm_define_view_referential_constraints_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_routine_privileges ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_ROUTINE_PRIVILEGES_NAME,
		   // columns
    {
      {"grantor", format_varchar (DB_MAX_USER_LENGTH)},
      {"grantee", format_varchar (DB_MAX_USER_LENGTH)},
      {"specific_catalog", format_varchar (255)},
      {"specific_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"specific_name", format_varchar (255)},
      {"routine_catalog", format_varchar (255)},
      {"routine_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"routine_name", format_varchar (255)},
      {"privilege_type", format_varchar (7)},
      {"is_grantable", format_varchar (3)},
      {attribute_kind::QUERY_SPEC, sm_define_view_routine_privileges_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_routines ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_ROUTINES_NAME,
		   // columns
    {
      {"specific_name", format_varchar (255)},
      {"routine_catalog", format_varchar (255)},
      {"routine_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"routine_name", format_varchar (255)},
      {"routine_type", format_varchar (9)},
      {"data_type", format_varchar (16)},
      {"character_maximum_length", "bigint"},
      {"character_octet_length", "bigint"},
      {"character_set_name", format_varchar (32)},
      {"collation_name", format_varchar (32)},
      {"numeric_precision", "integer"},
      {"numeric_scale", "integer"},
      {"datetime_precision", "integer"},
      {"dtd_identifier", format_varchar (255)},
      {"routine_body", format_varchar (8)},
      {"routine_definition", "string"},
      {"external_name", "string"},
      {"external_language", format_varchar (64)},
      {"parameter_style", format_varchar (3)},
      {"is_deterministic", format_varchar (3)},
      {"sql_data_access", format_varchar (17)},
      {"sql_path", "string"},
      {"security_type", format_varchar (7)},
      {"routine_comment", format_varchar (1024)},
      /* SQL:2016 standard names (other views use create_time/update_time) */
      {"created", "datetime"},
      {"last_altered", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_routines_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_schemata ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_SCHEMATA_NAME,
		   // columns
    {
      {"catalog_name", format_varchar (255)},
      {"schema_name", format_varchar (DB_MAX_USER_LENGTH)},
      {"schema_owner", format_varchar (DB_MAX_USER_LENGTH)},
      {"default_character_set_catalog", format_varchar (255)},
      {"default_character_set_schema", format_varchar (255)},
      {"default_character_set_name", format_varchar (32)},
      {"sql_path", "string"},
      {"schema_comment", format_varchar (1024)},
      {"create_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_schemata_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_sequences ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_SEQUENCES_NAME,
		   // columns
    {
      {"sequence_catalog", format_varchar (255)},
      {"sequence_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"sequence_name", format_varchar (255)},
      {"data_type", format_varchar (16)},
      {"numeric_precision", "integer"},
      {"numeric_precision_radix", "integer"},
      {"numeric_scale", "integer"},
      {"start_value", format_numeric (38, 0)},
      {"minimum_value", format_numeric (38, 0)},
      {"maximum_value", format_numeric (38, 0)},
      {"increment", format_numeric (38, 0)},
      {"cycle_option", format_varchar (3)},
      {"is_cached", format_varchar (3)},
      {"sequence_comment", format_varchar (1024)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_sequences_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_statistics ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_STATISTICS_NAME,
		   // columns
    {
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"is_unique", "integer"},
      {"index_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"index_name", format_varchar (255)},
      {"seq_in_index", "integer"},
      {"column_name", format_varchar (255)},
      {"collation", format_varchar (1)},
      {"cardinality", "integer"},
      {"sub_part", "integer"},
      {"nullable", format_varchar (3)},
      {"index_type", format_varchar (32)},
      {"comment", format_varchar (1024)},
      {"index_comment", format_varchar (1024)},
      {"is_visible", format_varchar (3)},
      {"expression", format_varchar (1023)},
      {"deduplicate_level", "integer"},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {"access_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_statistics_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_synonyms ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_SYNONYMS_NAME,
		   // columns
    {
      {"synonym_catalog", format_varchar (255)},
      {"synonym_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"synonym_name", format_varchar (255)},
      {"is_public_synonym", format_varchar (3)},
      {"target_catalog", format_varchar (255)},
      {"target_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"target_name", format_varchar (255)},
      {"synonym_comment", format_varchar (2048)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_synonyms_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }
  information_schema_definition
  information_schema_initializer::get_view_table_constraints ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_TABLE_CONSTRAINTS_NAME,
		   // columns
    {
      {"constraint_catalog", format_varchar (255)},
      {"constraint_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"constraint_name", format_varchar (255)},
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"constraint_type", format_varchar (11)},
      {"is_deferrable", format_varchar (3)},
      {"initially_deferred", format_varchar (3)},
      {"enforced", format_varchar (3)},
      {attribute_kind::QUERY_SPEC, sm_define_view_table_constraints_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_table_privileges ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_TABLE_PRIVILEGES_NAME,
		   // columns
    {
      {"grantor", format_varchar (DB_MAX_USER_LENGTH)},
      {"grantee", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"privilege_type", format_varchar (7)},
      {"is_grantable", format_varchar (3)},
      {attribute_kind::QUERY_SPEC, sm_define_view_table_privileges_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_tables ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_TABLES_NAME,
		   // columns
    {
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"table_type", format_varchar (12)},
      {"table_rows", "bigint"},
      {"avg_row_length", "bigint"},
      {"data_length", "bigint"},
      {"data_free", "bigint"},
      {"auto_increment", format_numeric (38, 0)},
      {"table_collation", format_varchar (32)},
      {"create_options", format_varchar (255)},
      {"is_temporary", format_varchar (3)},
      {"table_comment", format_varchar (2048)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {"statistics_strategy", format_varchar (8)},
      {"last_analyzed", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_tables_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_triggers ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_TRIGGERS_NAME,
		   // columns
    {
      {"trigger_catalog", format_varchar (255)},
      {"trigger_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"trigger_name", format_varchar (255)},
      {"event_manipulation", format_varchar (8)},
      {"event_object_catalog", format_varchar (255)},
      {"event_object_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"event_object_table", format_varchar (255)},
      {"event_object_column", format_varchar (255)},
      {"action_order", "integer"},
      {"action_condition", "string"},
      {"action_statement", "string"},
      {"action_orientation", format_varchar (9)},
      {"action_timing", format_varchar (8)},
      {"action_reference_old_table", format_varchar (3)},
      {"action_reference_new_table", format_varchar (3)},
      {"action_reference_old_row", format_varchar (3)},
      {"action_reference_new_row", format_varchar (3)},
      {"trigger_comment", format_varchar (1024)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_triggers_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }

  information_schema_definition
  information_schema_initializer::get_view_views ()
  {
    return information_schema_definition (
		   // name
		   INFO_SCHEMA_VIEWS_NAME,
		   // columns
    {
      {"table_catalog", format_varchar (255)},
      {"table_schema", format_varchar (DB_MAX_USER_LENGTH)},
      {"table_name", format_varchar (255)},
      {"view_definition", "string"},
      {"check_option", format_varchar (8)},
      {"is_updatable", format_varchar (3)},
      {"view_comment", format_varchar (2048)},
      {"create_time", "datetime"},
      {"update_time", "datetime"},
      {attribute_kind::QUERY_SPEC, sm_define_view_views_spec ()}
    },
    // constraint
    {},
    // authorization
    {
      // owner
      Au_information_schema_user,
      // grants
      {
	{Au_public_user, AU_SELECT, false}
      }
    },
    // initializer
    nullptr
	   );
  }
}