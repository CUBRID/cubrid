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
 * schema_information_schema_install_query_spec.cpp
 */


// TODO: Add checking the following rules in compile time (@hgryoo)

/*
 * Please follow the rules below when writing query specifications for system virtual classes.
 *
 *  1. First indent 1 tab, then 2 spaces.
 *     - The CASE statement indents 2 spaces until the END.
 *  2. All lines do not start with a space.
 *  3. All lines end with a space. However, the following case does not end with a space.
 *     - If the current line ends with "(", it ends without a space.
 *     - If the next line starts with ")", the current line ends without a space.
 *  4. Add a space before "(" and after ")". Remove spaces after "(" and before ")".
 *  5. Add a space before "{" and after "}". Remove spaces after "{" and before "}".
 *  6. Add a space before and after "+" and "=" operators.
 *  7. Change the line.
 *     - In the SELECT, FROM, WHERE, and ORDER BY clauses, change the line.
 *     - After the SELECT, FROM, WHERE, and ORDER BY keywords, change the line.
 *     - In the AND and OR clauses, change the line.
 *     - In more than one TABLE expression, change the line.
 *  8. Do not change the line.
 *     - If the expression length is less than 120 characters, write it on one line.
 *     - In the CASE statement, write the WHEN and THEN clauses on one line.
 *     - In the FROM clause, write a TABLE expression and related tables on one line.
 *  9. In the SELECT and FROM clauses, use the AS keyword before alias.
 * 10. If the CAST function is used, write a comment about the data type being changed.
 * 11. If column are compared without changing in the CASE statement, write the column name after the CASE keyword.
 * 12. If %s (Format Specifier) is used in the FROM clause, write a comment about the value to be used.
 * 13. Because path expression cannot be used in ANSI style, write a join condition in the WHERE clause.
 *
 */
#include <cstdio>

const char *sm_define_info_schema_columns_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf(stmt, 
        "SELECT "
         " 'test' as character_maximum_length,"
         " 'test' as character_octet_length,"
         " 'test' as character_set_name,"
         " 'test' as collation_name,"
         " 'test' as column_default,"
         " 'test' as data_type,"
         " 'test' as datatime_precision,"
         " 'test' as generation_expression,"
         " 'test' as is_nullable,"
         " 'test' as numeric_precision,"
         " 'test' as numeric_scale,"
         " 'test' as ordinal_position,"
         " 'test' as table_catalog,"
         " 'test' as table_name,"
         " 'test' as table_schema"
         " FROM dual;"
         );
         return stmt;
}

const char *sm_define_info_schema_domains_spec(void)
{
    static char stmt[2048];

    // *INDENT-OFF*
    sprintf(stmt, 
        "SELECT "
        " 'test' as character_set_catalog,"
        " 'test' as character_set_name,"
        " 'test' as collation_catalog,"
        " 'test' as collation_name,"
        " 'test' as data_type,"
        " 'test' as domain_catalog,"
        " 'test' as domain_schema,"
        " 'test' as numeric_precision,"
        " 'test' as numeric_scale,"
        " 'test' as udt_catalog,"
        " 'test' as udt_name,"
        " 'test' as udt_schema,"
        " 'test' as character_maximum_length,"
        " 'test' as character_octet_length,"
        " 'test' as character_set_schema,"
        " 'test' as collation_schema,"
        " 'test' as datetime_precision,"
        " 'test' as domain_default,"
        " 'test' as domain_name,"
        " 'test' as numeric_precision_radix"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_key_column_usage_spec(void)
{
    static char stmt[2048];

    // *INDENT-OFF*
    sprintf(stmt, 
        "SELECT "
        " 'test' as column_name,"
        " 'test' as constraint_catalog,"
        " 'test' as constraint_name,"
        " 'test' as constraint_schema,"
        " 'test' as ordinal_position,"
        " 'test' as position_in_unique_constraint,"
        " 'test' as table_catalog,"
        " 'test' as table_name,"
        " 'test' as table_schema"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_parameters_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as character_maximum_length,"
        " 'test' as character_octet_length,"
        " 'test' as character_set_name,"
        " 'test' as collation_name,"
        " 'test' as data_type,"
        " 'test' as datetime_precision,"
        " 'test' as dtd_identifier,"
        " 'test' as numeric_precision,"
        " 'test' as numeric_scale,"
        " 'test' as specific_name"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_partitions_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as avg_row_length,"
        " 'test' as checksum,"
        " 'test' as check_time,"
        " 'test' as create_time,"
        " 'test' as data_free,"
        " 'test' as data_length,"
        " 'test' as index_length,"
        " 'test' as max_data_length,"
        " 'test' as nodegroup,"
        " 'test' as partition_comment,"
        " 'test' as partition_description,"
        " 'test' as partition_expression,"
        " 'test' as partition_method,"
        " 'test' as partition_name,"
        " 'test' as partition_ordinal_position,"
        " 'test' as subpartition_expression,"
        " 'test' as subpartition_method,"
        " 'test' as subpartition_name,"
        " 'test' as subpartition_ordinal_position,"
        " 'test' as table_catalog,"
        " 'test' as table_name,"
        " 'test' as table_rows,"
        " 'test' as table_schema,"
        " 'test' as tablespace_name,"
        " 'test' as update_time"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_referential_constratins_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as constraint_catalog,"
        " 'test' as constraint_name,"
        " 'test' as constraint_schema,"
        " 'test' as delete_rule,"
        " 'test' as match_option,"
        " 'test' as unique_constraint_catalog,"
        " 'test' as unique_constraint_name,"
        " 'test' as unique_constraint_schema,"
        " 'test' as update_rule"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_routines_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as character_maximum_length,"
        " 'test' as character_octet_length,"
        " 'test' as character_set_name,"
        " 'test' as collation_name,"
        " 'test' as created,"
        " 'test' as data_type,"
        " 'test' as datetime_precision,"
        " 'test' as dtd_identifier,"
        " 'test' as external_language,"
        " 'test' as external_name,"
        " 'test' as is_deterministic,"
        " 'test' as last_altered,"
        " 'test' as numeric_precision,"
        " 'test' as numeric_scale,"
        " 'test' as parameter_style,"
        " 'test' as routine_body,"
        " 'test' as routine_catalog,"
        " 'test' as routine_definition,"
        " 'test' as routine_name,"
        " 'test' as routine_schema,"
        " 'test' as routine_type,"
        " 'test' as security_type,"
        " 'test' as specific_name,"
        " 'test' as sql_data_access,"
        " 'test' as sql_path"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_schemata_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as catalog_name,"
        " 'test' as default_character_set_name,"
        " 'test' as schema_name,"
        " 'test' as sql_path"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_sequences_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as cycle_option,"
        " 'test' as data_type,"
        " 'test' as increment,"
        " 'test' as maximum_value,"
        " 'test' as minimum_value,"
        " 'test' as numeric_precision,"
        " 'test' as numeric_precision_radix,"
        " 'test' as numeric_scale,"
        " 'test' as sequence_catalog,"
        " 'test' as sequence_name,"
        " 'test' as sequence_schema,"
        " 'test' as start_value"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_synonym_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as comment,"
        " 'test' as is_public_synonym,"
        " 'test' as synonym_catalog,"
        " 'test' as synonym_name,"
        " 'test' as synonym_schema,"
        " 'test' as target_name,"
        " 'test' as target_owner"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_table_constraints_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as constraint_catalog,"
        " 'test' as constraint_name,"
        " 'test' as constraint_schema,"
        " 'test' as constraint_type,"
        " 'test' as enforced,"
        " 'test' as table_name,"
        " 'test' as table_schema"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_table_privileges_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as grantee,"
        " 'test' as is_grantable,"
        " 'test' as privilege_type,"
        " 'test' as table_catalog,"
        " 'test' as table_name,"
        " 'test' as table_schema"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_tables_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as table_catalog,"
        " 'test' as table_name,"
        " 'test' as table_schema,"
        " 'test' as table_type"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_triggers_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as action_condition,"
        " 'test' as action_order,"
        " 'test' as action_orientation,"
        " 'test' as action_reference_new_row,"
        " 'test' as action_reference_new_table,"
        " 'test' as action_reference_old_row,"
        " 'test' as action_reference_old_table,"
        " 'test' as action_statement,"
        " 'test' as action_timing,"
        " 'test' as created,"
        " 'test' as event_manipulation,"
        " 'test' as event_object_catalog,"
        " 'test' as event_object_schema,"
        " 'test' as event_object_table,"
        " 'test' as trigger_catalog,"
        " 'test' as trigger_name,"
        " 'test' as trigger_schema"
        " FROM dual;"
    );

    return stmt;
}
const char *sm_define_info_schema_views_spec(void)
{
    static char stmt[2048];

    sprintf(stmt, 
        "SELECT "
        " 'test' as check_option,"
        " 'test' as is_updatable,"
        " 'test' as table_catalog,"
        " 'test' as table_name,"
        " 'test' as table_schema,"
        " 'test' as view_definition"
        " FROM dual;"
    );

    return stmt;
}
