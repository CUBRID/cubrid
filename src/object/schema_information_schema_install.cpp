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

#include <vector>

#include "schema_information_schema_install.hpp"
#include "schema_system_catalog_builder.hpp"
#include "schema_system_catalog_install.hpp"
#include "authenticate.h"
#include "schema_system_catalog_definition.hpp"

// TODO: move to header file to use macros with system catalog
#define BIGINT "bigint"
#define INTEGER "integer"
#define MAX_STRING_LEN 1073741823

static std::vector<cubschema::catcls_function> vclist;

void info_schema_init (void)
{
  using namespace cubschema;

  // TODO: for late initialization (for au_init () to retrieve MOPs: Au_information_schema_user and Au_public_user)
#define ADD_VIEW_DEFINITION(name,def) vclist.emplace_back(name, def)

  ADD_VIEW_DEFINITION (INFO_SCHEMA_COLUMNS, system_catalog_initializer::get_view_columns());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_DOMAINS, system_catalog_initializer::get_view_domains());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_KEY_COLUMN_USAGE, system_catalog_initializer::get_view_key_column_usage());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_PARAMETERS, system_catalog_initializer::get_view_parameters());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_PARTITIONS, system_catalog_initializer::get_view_partitions());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_REFERENTIAL_CONSTRAINTS,
		       system_catalog_initializer::get_view_referential_constratins());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_ROUTINES, system_catalog_initializer::get_view_routines());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_SCHEMATA, system_catalog_initializer::get_view_schemata());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_SEQUENCES, system_catalog_initializer::get_view_sequences());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_SYNONYMS, system_catalog_initializer::get_view_synonym2());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_TABLE_CONSTRAINTS, system_catalog_initializer::get_view_table_constraints());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_TABLE_PRIVILEGES, system_catalog_initializer::get_view_table_privileges());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_TABLES, system_catalog_initializer::get_view_tables());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_TRIGGERS, system_catalog_initializer::get_view_triggers());
  ADD_VIEW_DEFINITION (INFO_SCHEMA_VIEWS, system_catalog_initializer::get_view_views());
}

int info_schema_install (void)
{
  int error_code = NO_ERROR;

  const size_t num_vclasses = vclist.size ();
  int save;
  size_t i;
  AU_DISABLE (save);

  using catalog_builder = cubschema::system_catalog_builder;

  au_set_user (Au_dba_user); // change to dba user
//   au_set_user (Au_information_schema_user); // change to dba user


  for (i = 0; i < num_vclasses; i++)
    {
      // new routine
      MOP class_mop = catalog_builder::create_and_mark_system_class (vclist[i].name);
      if (class_mop != nullptr)
	{
	  error_code = catalog_builder::build_vclass (class_mop, vclist[i].definition);
	}

      if (er_errid () != NO_ERROR)
	{
	  error_code = er_errid ();
	}


      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);

  vclist.clear ();

  return error_code;

}

// TODO
// 1. change types
namespace cubschema
{
  const inline std::string format_varchar (const int size)
  {
    std::string s ("varchar(");
    s += std::to_string (size);
    s += ")";
    return s;
  }

  system_catalog_definition
  system_catalog_initializer::get_view_columns()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_COLUMNS,
		   // columns
    {
      {"character_maximum_length", format_varchar (64)},
      {"character_octet_length", format_varchar (64)},
      {"character_set_name", format_varchar (64)},
      {"collation_name", format_varchar (64)},
      {"column_default", format_varchar (64)},
      {"data_type", format_varchar (64)},
      {"datatime_precision", format_varchar (64)},
      {"generation_expression", format_varchar (64)},
      {"is_nullable", format_varchar (64)},
      {"numeric_precision", format_varchar (64)},
      {"numeric_scale", format_varchar (64)},
      {"ordinal_position", format_varchar (64)},
      {"table_catalog", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_schema", format_varchar (64)},

      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_columns_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_domains()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_DOMAINS,
		   // columns
    {
      {"character_set_catalog", format_varchar (64)},
      {"character_set_name", format_varchar (64)},
      {"collation_catalog", format_varchar (64)},
      {"collation_name", format_varchar (64)},
      {"data_type", format_varchar (64)},
      {"domain_catalog", format_varchar (64)},
      {"domain_schema", format_varchar (64)},
      {"numeric_precision", format_varchar (64)},
      {"numeric_scale", format_varchar (64)},
      {"udt_catalog", format_varchar (64)},
      {"udt_name", format_varchar (64)},
      {"udt_schema", format_varchar (64)},
      {"character_maximum_length", format_varchar (64)},
      {"character_octet_length", format_varchar (64)},
      {"character_set_schema", format_varchar (64)},
      {"collation_schema", format_varchar (64)},
      {"datetime_precision", format_varchar (64)},
      {"domain_default", format_varchar (64)},
      {"domain_name", format_varchar (64)},
      {"numeric_precision_radix", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_domains_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_key_column_usage()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_KEY_COLUMN_USAGE,
		   // columns
    {
      {"column_name", format_varchar (64)},
      {"constraint_catalog", format_varchar (64)},
      {"constraint_name", format_varchar (64)},
      {"constraint_schema", format_varchar (64)},
      {"ordinal_position", format_varchar (64)},
      {"position_in_unique_constraint", format_varchar (64)},
      {"table_catalog", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_schema", format_varchar (64)},


      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_key_column_usage_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_parameters()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_PARAMETERS,
		   // columns
    {
      {"character_maximum_length", format_varchar (64)},
      {"character_octet_length", format_varchar (64)},
      {"character_set_name", format_varchar (64)},
      {"collation_name", format_varchar (64)},
      {"data_type", format_varchar (64)},
      {"datetime_precision", format_varchar (64)},
      {"dtd_identifier", format_varchar (64)},
      {"numeric_precision", format_varchar (64)},
      {"numeric_scale", format_varchar (64)},
      {"specific_name", format_varchar (64)},


      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_parameters_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_partitions()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_PARAMETERS,
		   // columns
    {
      {"avg_row_length", format_varchar (64)},
      {"checksum", format_varchar (64)},
      {"check_time", format_varchar (64)},
      {"create_time", format_varchar (64)},
      {"data_free", format_varchar (64)},
      {"data_length", format_varchar (64)},
      {"index_length", format_varchar (64)},
      {"max_data_length", format_varchar (64)},
      {"nodegroup", format_varchar (64)},
      {"partition_comment", format_varchar (64)},
      {"partition_description", format_varchar (64)},
      {"partition_expression", format_varchar (64)},
      {"partition_method", format_varchar (64)},
      {"partition_name", format_varchar (64)},
      {"partition_ordinal_position", format_varchar (64)},
      {"subpartition_expression", format_varchar (64)},
      {"subpartition_method", format_varchar (64)},
      {"subpartition_name", format_varchar (64)},
      {"subpartition_ordinal_position", format_varchar (64)},
      {"table_catalog", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_rows", format_varchar (64)},
      {"table_schema", format_varchar (64)},
      {"tablespace_name", format_varchar (64)},
      {"update_time", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_partitions_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_referential_constratins()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_REFERENTIAL_CONSTRAINTS,
		   // columns
    {
      {"constraint_catalog", format_varchar (64)},
      {"constraint_name", format_varchar (64)},
      {"constraint_schema", format_varchar (64)},
      {"delete_rule", format_varchar (64)},
      {"match_option", format_varchar (64)},
      {"unique_constraint_catalog", format_varchar (64)},
      {"unique_constraint_name", format_varchar (64)},
      {"unique_constraint_schema", format_varchar (64)},
      {"update_rule", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_referential_constratins_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_routines()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_ROUTINES,
		   // columns
    {
      {"character_maximum_length", format_varchar (64)},
      {"character_octet_length", format_varchar (64)},
      {"character_set_name", format_varchar (64)},
      {"collation_name", format_varchar (64)},
      {"created", format_varchar (64)},
      {"data_type", format_varchar (64)},
      {"datetime_precision", format_varchar (64)},
      {"dtd_identifier", format_varchar (64)},
      {"external_language", format_varchar (64)},
      {"external_name", format_varchar (64)},
      {"is_deterministic", format_varchar (64)},
      {"last_altered", format_varchar (64)},
      {"numeric_precision", format_varchar (64)},
      {"numeric_scale", format_varchar (64)},
      {"parameter_style", format_varchar (64)},
      {"routine_body", format_varchar (64)},
      {"routine_catalog", format_varchar (64)},
      {"routine_definition", format_varchar (64)},
      {"routine_name", format_varchar (64)},
      {"routine_schema", format_varchar (64)},
      {"routine_type", format_varchar (64)},
      {"security_type", format_varchar (64)},
      {"specific_name", format_varchar (64)},
      {"sql_data_access", format_varchar (64)},
      {"sql_path", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_routines_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_schemata()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_SCHEMATA,
		   // columns
    {
      {"catalog_name", format_varchar (64)},
      {"default_character_set_name", format_varchar (64)},
      {"schema_name", format_varchar (64)},
      {"sql_path", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_schemata_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_sequences()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_SEQUENCES,
		   // columns
    {
      {"cycle_option", format_varchar (64)},
      {"data_type", format_varchar (64)},
      {"increment", format_varchar (64)},
      {"maximum_value", format_varchar (64)},
      {"minimum_value", format_varchar (64)},
      {"numeric_precision", format_varchar (64)},
      {"numeric_precision_radix", format_varchar (64)},
      {"numeric_scale", format_varchar (64)},
      {"sequence_catalog", format_varchar (64)},
      {"sequence_name", format_varchar (64)},
      {"sequence_schema", format_varchar (64)},
      {"start_value", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_sequences_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_synonym2()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_SYNONYMS,
		   // columns
    {
      {"comment", format_varchar (64)},
      {"is_public_synonym", format_varchar (64)},
      {"synonym_catalog", format_varchar (64)},
      {"synonym_name", format_varchar (64)},
      {"synonym_schema", format_varchar (64)},
      {"target_name", format_varchar (64)},
      {"target_owner", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_synonym_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_table_constraints()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_TABLE_CONSTRAINTS,
		   // columns
    {
      {"constraint_catalog", format_varchar (64)},
      {"constraint_name", format_varchar (64)},
      {"constraint_schema", format_varchar (64)},
      {"constraint_type", format_varchar (64)},
      {"enforced", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_schema", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_table_constraints_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_table_privileges()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_TABLE_PRIVILEGES,
		   // columns
    {
      {"grantee", format_varchar (64)},
      {"is_grantable", format_varchar (64)},
      {"privilege_type", format_varchar (64)},
      {"table_catalog", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_schema", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_table_privileges_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_tables()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_TABLES,
		   // columns
    {
      {"table_catalog", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_schema", format_varchar (64)},
      {"table_type", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_tables_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_triggers()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_TRIGGERS,
		   // columns
    {
      {"action_condition", format_varchar (64)},
      {"action_order", format_varchar (64)},
      {"action_orientation", format_varchar (64)},
      {"action_reference_new_row", format_varchar (64)},
      {"action_reference_new_table", format_varchar (64)},
      {"action_reference_old_row", format_varchar (64)},
      {"action_reference_old_table", format_varchar (64)},
      {"action_statement", format_varchar (64)},
      {"action_timing", format_varchar (64)},
      {"created", format_varchar (64)},
      {"event_manipulation", format_varchar (64)},
      {"event_object_catalog", format_varchar (64)},
      {"event_object_schema", format_varchar (64)},
      {"event_object_table", format_varchar (64)},
      {"trigger_catalog", format_varchar (64)},
      {"trigger_name", format_varchar (64)},
      {"trigger_schema", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_triggers_spec()}
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
    nullptr);
  }
  system_catalog_definition system_catalog_initializer::get_view_views()
  {
    return system_catalog_definition (
		   // name
		   INFO_SCHEMA_VIEWS,
		   // columns
    {
      {"check_option", format_varchar (64)},
      {"is_updatable", format_varchar (64)},
      {"table_catalog", format_varchar (64)},
      {"table_name", format_varchar (64)},
      {"table_schema", format_varchar (64)},
      {"view_definition", format_varchar (64)},
      // query specs
      {attribute_kind::QUERY_SPEC, sm_define_info_schema_views_spec()}
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
    nullptr);
  }
}