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
 * Please follow the rules below when writing query specifications for information schema views
 *
 *  1. First indent 1 tab, then 2 spaces.
 *     - The CASE statement indents 2 spaces until the END.
 *  2. All lines do not start with a space.
 *  3. All lines end with a space. However, the following case does not end with a space.
 *     - If the current line ends with "(", it ends without a space.
 *     - If the next line starts with ")", the current line ends without a space.
 *  4. Add a space before "(" and after ")", "{" and "}". Remove spaces after "(" and before ")".
 *  5. Add a space before and after "+" and "=" operators.
 *  6. Change the line.
 *     - In the SELECT, FROM, WHERE, and ORDER BY clauses, change the line.
 *     - After the SELECT, FROM, WHERE, and ORDER BY keywords, change the line.
 *     - In the AND and OR clauses, change the line.
 *     - In more than one TABLE expression, change the line.
 *  7. Do not change the line.
 *     - If the expression length is less than 120 characters, write it on one line.
 *     - In the CASE statement, write the WHEN and THEN clauses on one line.
 *     - In the FROM clause, write a TABLE expression and related tables on one line.
 *  8. In the SELECT and FROM clauses, use the AS keyword before alias.
 *  9. If the CAST function is used, write a comment about the data type being changed.
 * 10. If column are compared without changing in the CASE statement, write the column name after the CASE keyword.
 * 11. If %s (Format Specifier) is used in the FROM clause, write a comment about the value to be used.
 * 12. Because path expression cannot be used in ANSI style, write a join condition in the WHERE clause.
 */

#include "class_object.h"
#include "dbtype_def.h"
#include "schema_system_catalog_constants.h"
#include "authenticate.h"
#include "deduplicate_key.h"
#include "sp_catalog.hpp"
#include "sp_constants.hpp"
#include "tde.h"
#include "trigger_manager.h"

#include <cstdio>

/* ============================================================================= */
/* Authorization check macros */
/* ============================================================================= */

/* ----------------------------------------------------------------------------- */
/* Building blocks (used internally by other macros) */
/* ----------------------------------------------------------------------------- */

#define CURRENT_USER_GROUPS_SUBQUERY \
  "SELECT SET {CURRENT_USER} + COALESCE (SUM (SET {[grp].[g].[name]}), SET {}) " \
  "FROM [" AU_USER_CLASS_NAME "] AS [usr], TABLE ([usr].[groups]) AS [grp] ([g]) " \
  "WHERE [usr].[name] = CURRENT_USER"

#define AUTH_CHECK_DBA \
  "{'DBA'} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ")"

#define AUTH_CHECK_OWNER(owner_name_expr) \
  "{" owner_name_expr "} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ")"

#define AUTH_CHECK_ANY_GRANT(class_of_expr) \
  "{" class_of_expr "} SUBSETEQ (" \
    "SELECT SUM (SET {[auth].[object_of]}) FROM [" CT_CLASSAUTH_NAME "] AS [auth] " \
    "WHERE {[auth].[grantee].[name]} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ")" \
  ")"

#define AUTH_CHECK_WRITE_GRANT(class_of_expr) \
  "{" class_of_expr "} SUBSETEQ (" \
    "SELECT SUM (SET {[auth].[object_of]}) FROM [" CT_CLASSAUTH_NAME "] AS [auth] " \
    "WHERE {[auth].[grantee].[name]} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ") " \
    "AND [auth].[auth_type] IN ('INSERT', 'UPDATE', 'DELETE', 'ALTER')" \
  ")"

#define AUTH_CHECK_EXECUTE_GRANT(sp_of_expr) \
  "{" sp_of_expr "} SUBSETEQ (" \
    "SELECT SUM (SET {[auth].[object_of]}) FROM [" CT_CLASSAUTH_NAME "] AS [auth] " \
    "WHERE {[auth].[grantee].[name]} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ") " \
    "AND [auth].[auth_type] = 'EXECUTE'" \
  ")"

/* ----------------------------------------------------------------------------- */
/* Composite macros (used in query specs) */
/* ----------------------------------------------------------------------------- */

#define AUTH_CHECK_OBJECT_ANY(owner_name_expr, object_of_expr) \
  "(" \
    AUTH_CHECK_DBA " " \
    "OR " AUTH_CHECK_OWNER(owner_name_expr) " " \
    "OR " AUTH_CHECK_ANY_GRANT(object_of_expr) \
  ")"

#define AUTH_CHECK_OBJECT_WRITE(owner_name_expr, object_of_expr) \
  "(" \
    AUTH_CHECK_DBA " " \
    "OR " AUTH_CHECK_OWNER(owner_name_expr) " " \
    "OR " AUTH_CHECK_WRITE_GRANT(object_of_expr) \
  ")"

#define AUTH_CHECK_SCHEMA(user_name_expr, user_expr) \
  "(" \
    AUTH_CHECK_DBA " " \
    "OR " AUTH_CHECK_OWNER(user_name_expr) " " \
    "OR EXISTS (" \
      "SELECT 1 FROM [" CT_CLASSAUTH_NAME "] AS [auth] " \
      "WHERE [auth].[grantor] = " user_expr " " \
      "AND {[auth].[grantee].[name]} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ")" \
    ")" \
  ")"

#define AUTH_CHECK_PRIVILEGE(grantee_name_expr, grantor_name_expr) \
  "(" \
    "{'DBA', " grantee_name_expr ", " grantor_name_expr "} * (" \
      CURRENT_USER_GROUPS_SUBQUERY \
    ") SETNEQ {}" \
  ")"

#define AUTH_CHECK_SYNONYM(is_public_expr, owner_name_expr) \
  "(" \
    AUTH_CHECK_DBA " " \
    "OR " is_public_expr " = 1 " \
    "OR (" is_public_expr " = 0 AND " AUTH_CHECK_OWNER(owner_name_expr) ")" \
  ")"

#define AUTH_CHECK_STORED_PROC(owner_name_expr, sp_of_expr) \
  "(" \
    AUTH_CHECK_DBA " " \
    "OR " AUTH_CHECK_OWNER(owner_name_expr) " " \
    "OR " AUTH_CHECK_EXECUTE_GRANT(sp_of_expr) \
  ")"

/* CUBRID does not currently support column privilege.
 * This view returns empty results until column privilege support is implemented.
 */
const char *sm_define_view_column_privileges_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "NULL AS [grantor], "
      "NULL AS [grantee], "
      "NULL AS [table_catalog], "
      "NULL AS [table_schema], "
      "NULL AS [table_name], "
      "NULL AS [column_name], "
      "NULL AS [privilege_type], "
      "NULL AS [is_grantable] "
    "FROM "
      "[%s] "
    "WHERE "
      "FALSE",
    CT_DUAL_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_columns_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[cls].[owner].[name] AS [table_schema], "
      "[cls].[class_name] AS [table_name], "
      "[attr].[attr_name] AS [column_name], "
      "([attr].[def_order] + 1) AS [ordinal_position], "
      "[attr].[default_value] AS [column_default], "
      "IF ([attr].[is_nullable] = 1, 'YES', 'NO') AS [is_nullable], "
      "[dt].[type_name] AS [data_type], "
      /* DB_TYPE_STRING/VARCHAR, DB_TYPE_BIT, DB_TYPE_VARBIT, DB_TYPE_CHAR */
      "IF ([attr].[data_type] IN (%d, %d, %d, %d), [dom].[prec], NULL) AS [character_maximum_length], "
      /* DB_TYPE_STRING/VARCHAR, DB_TYPE_CHAR */
      "IF ([attr].[data_type] IN (%d, %d), CAST ([dom].[prec] AS BIGINT) * [charset].[char_size], NULL) AS [character_octet_length], "
      /* DB_TYPE_INTEGER, DB_TYPE_FLOAT, DB_TYPE_DOUBLE, DB_TYPE_SHORT/SMALLINT, DB_TYPE_NUMERIC, DB_TYPE_BIGINT */
      "IF ([attr].[data_type] IN (%d, %d, %d, %d, %d, %d), [dom].[prec], NULL) AS [numeric_precision], "
      /* DB_TYPE_INTEGER, DB_TYPE_SHORT/SMALLINT, DB_TYPE_NUMERIC, DB_TYPE_BIGINT */
      "IF ([attr].[data_type] IN (%d, %d, %d, %d), [dom].[scale], NULL) AS [numeric_scale], "
      /* DB_TYPE_TIME, DB_TYPE_TIMESTAMP, DB_TYPE_DATE, DB_TYPE_DATETIME, DB_TYPE_TIMESTAMPTZ, DB_TYPE_TIMESTAMPLTZ, DB_TYPE_DATETIMETZ, DB_TYPE_DATETIMELTZ */
      "IF ([attr].[data_type] IN (%d, %d, %d, %d, %d, %d, %d, %d), [dom].[prec], NULL) AS [datetime_precision], "
      /* DB_TYPE_STRING/VARCHAR, DB_TYPE_CHAR */
      "IF ([attr].[data_type] IN (%d, %d), [charset].[charset_name], NULL) AS [character_set_name], "
      /* DB_TYPE_STRING/VARCHAR, DB_TYPE_CHAR */
      "IF ([attr].[data_type] IN (%d, %d), [coll].[coll_name], NULL) AS [collation_name], "
      "NULL AS [domain_catalog], "
      "NULL AS [domain_schema], "
      "NULL AS [domain_name], "
      "NULL AS [udt_catalog], "
      "NULL AS [udt_schema], "
      "NULL AS [udt_name], "
      "CONCAT_WS (' ', "
        "IF (([attr].[flags] & %d) <> 0, 'auto_increment', NULL), "
        "IF (([attr].[flags] & %d) <> 0, 'partition_key', NULL)"
      ") AS [extra], "
      "NULL AS [privileges], "
      "[attr].[comment] AS [column_comment], "
      "'NO' AS [is_generated], "
      "NULL AS [generation_expression], "
      "'YES' AS [is_updatable], "
      "IF (([attr].[flags] & %d) = 0, 'YES', 'NO') AS [is_visible] "
    "FROM "
      /* CT_CLASS_NAME */
      "[%s] AS [cls] "
      /* CT_ATTRIBUTE_NAME */
      "INNER JOIN [%s] AS [attr] ON [attr].[class_of] = [cls] "
      /* CT_DOMAIN_NAME */
      "INNER JOIN [%s] AS [dom] ON [dom].[object_of] = [attr] "
      /* CT_DATATYPE_NAME */
      "INNER JOIN [%s] AS [dt] ON [dt].[type_id] = [dom].[data_type] "
      /* CT_CHARSET_NAME */
      "LEFT OUTER JOIN [%s] AS [charset] ON [charset].[charset_id] = [dom].[code_set] "
      /* CT_COLLATION_NAME */
      "INNER JOIN [%s] AS [coll] ON [coll].[coll_id] = [dom].[collation_id] "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[cls].[owner].[name]", "[cls].[class_of]"),
    DB_TYPE_STRING, DB_TYPE_BIT, DB_TYPE_VARBIT, DB_TYPE_CHAR,
    DB_TYPE_STRING, DB_TYPE_CHAR,
    DB_TYPE_INTEGER, DB_TYPE_FLOAT, DB_TYPE_DOUBLE, DB_TYPE_SHORT, DB_TYPE_NUMERIC, DB_TYPE_BIGINT,
    DB_TYPE_INTEGER, DB_TYPE_SHORT, DB_TYPE_NUMERIC, DB_TYPE_BIGINT,
    DB_TYPE_TIME, DB_TYPE_TIMESTAMP, DB_TYPE_DATE, DB_TYPE_DATETIME, DB_TYPE_TIMESTAMPTZ, DB_TYPE_TIMESTAMPLTZ, DB_TYPE_DATETIMETZ, DB_TYPE_DATETIMELTZ,
    DB_TYPE_STRING, DB_TYPE_CHAR,
    DB_TYPE_STRING, DB_TYPE_CHAR,
    DB_ATTOPT_AUTO_INCREMENT, DB_ATTOPT_PARTITION_KEY,
    DB_ATTOPT_INVISIBLE_COLUMN,
    CT_CLASS_NAME,
    CT_ATTRIBUTE_NAME,
    CT_DOMAIN_NAME,
    CT_DATATYPE_NAME,
    CT_CHARSET_NAME,
    CT_COLLATION_NAME);
  // *INDENT-ON*

  return stmt;
}

/* CUBRID does not currently support domains.
 * This view returns empty results until domain support is implemented.
 */
const char *sm_define_view_domains_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "NULL AS [domain_catalog], "
      "NULL AS [domain_schema], "
      "NULL AS [domain_name], "
      "NULL AS [data_type], "
      "NULL AS [character_maximum_length], "
      "NULL AS [character_octet_length], "
      "NULL AS [character_set_catalog], "
      "NULL AS [character_set_schema], "
      "NULL AS [character_set_name], "
      "NULL AS [collation_catalog], "
      "NULL AS [collation_schema], "
      "NULL AS [collation_name], "
      "NULL AS [numeric_precision], "
      "NULL AS [numeric_precision_radix], "
      "NULL AS [numeric_scale], "
      "NULL AS [datetime_precision], "
      "NULL AS [domain_default], "
      "NULL AS [udt_catalog], "
      "NULL AS [udt_schema], "
      "NULL AS [udt_name], "
      "NULL AS [domain_comment], "
      "NULL AS [create_time], "
      "NULL AS [update_time] "
    "FROM "
      "[%s] "
    "WHERE "
      "FALSE",
    CT_DUAL_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_foreign_servers_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [foreign_server_catalog], " /* string -> varchar(255) */
      "[srv].[link_name] AS [foreign_server_name], "
      "NULL AS [foreign_data_wrapper_catalog], "
      "NULL AS [foreign_data_wrapper_name], "
      "NULL AS [foreign_server_type], "
      "NULL AS [foreign_server_version], "
      "[srv].[owner].[name] AS [authorization_identifier], "
      "[srv].[host] AS [server_host], "
      "[srv].[port] AS [server_port], "
      "[srv].[db_name] AS [server_database], "
      "[srv].[user_name] AS [server_user], "
      "[srv].[properties] AS [server_properties], "
      "[srv].[comment] AS [server_comment], "
      "[srv].[created_time] AS [create_time], "
      "[srv].[updated_time] AS [update_time] "
    "FROM "
      /* CT_SERVER_NAME */
      "[%s] AS [srv] "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[srv].[owner].[name]", "[srv]"),
    CT_SERVER_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_key_column_usage_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [constraint_catalog], " /* string -> varchar(255) */
      "[idx].[class_of].[owner].[name] AS [constraint_schema], "
      "[idx].[index_name] AS [constraint_name], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[idx].[class_of].[owner].[name] AS [table_schema], "
      "[idx].[class_of].[class_name] AS [table_name], "
      "[idx_key].[key_attr_name] AS [column_name], "
      "([idx_key].[key_order] + 1) AS [ordinal_position], "
      "([ref_key].[key_order] + 1) AS [position_in_unique_constraint] "
    "FROM "
      /* CT_INDEXKEY_NAME */
      "[%s] AS [idx_key] "
      /* CT_INDEX_NAME */
      "INNER JOIN [%s] AS [idx] ON [idx] = [idx_key].[index_of] "
      /* CT_INDEXKEY_NAME */
      "LEFT OUTER JOIN [%s] AS [ref_key] ON [ref_key].[index_of] = [idx].[referential_index] AND [ref_key].[key_order] = [idx_key].[key_order] "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[idx].[class_of].[owner].[name]", "[idx].[class_of].[class_of]") " "
      "AND ([idx_key].[key_attr_name] IS NULL OR [idx_key].[key_attr_name] NOT LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN ") "
      "AND ([idx].[is_primary_key] = 1 OR [idx].[is_unique] = 1 OR [idx].[is_foreign_key] = 1)",
    CT_INDEXKEY_NAME,
    CT_INDEX_NAME,
    CT_INDEXKEY_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_parameters_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [specific_catalog], " /* string -> varchar(255) */
      "[sp_args].[sp_of].[owner].[name] AS [specific_schema], "
      "IF ([sp_args].[sp_of].[pkg_name] IS NOT NULL, CONCAT ([sp_args].[sp_of].[pkg_name], '.', [sp_args].[sp_of].[sp_name]), [sp_args].[sp_of].[sp_name]) AS [specific_name], "
      "([sp_args].[index_of] + 1) AS [ordinal_position], "
      /* SP_MODE_IN, SP_MODE_OUT, SP_MODE_INOUT */
      "DECODE ([sp_args].[mode], %d, 'IN', %d, 'OUT', %d, 'INOUT') AS [parameter_mode], "
      "'NO' AS [is_result], "
      "[sp_args].[arg_name] AS [parameter_name], "
      /* DB_TYPE_RESULTSET has no row in _db_data_type; surface it as 'CURSOR' */
      "CASE [sp_args].[data_type] WHEN %d THEN 'CURSOR' ELSE [dt].[type_name] END AS [data_type], "
      "NULL AS [character_maximum_length], "
      "NULL AS [character_octet_length], "
      "NULL AS [character_set_name], "
      "NULL AS [collation_name], "
      "NULL AS [numeric_precision], "
      "NULL AS [numeric_scale], "
      /* DB_TYPE_DATETIME -> 3, DB_TYPE_TIME, DB_TYPE_TIMESTAMP, DB_TYPE_DATE -> 0, else NULL */
      "CASE "
        "WHEN [sp_args].[data_type] = %d THEN 3 "
        "WHEN [sp_args].[data_type] IN (%d, %d, %d) THEN 0 "
        "ELSE NULL "
      "END AS [datetime_precision], "
      "NULL AS [dtd_identifier], "
      /* SP_TYPE_PROCEDURE, SP_TYPE_FUNCTION */
      "DECODE ([sp_args].[sp_of].[sp_type], %d, 'PROCEDURE', %d, 'FUNCTION') AS [routine_type], "
      "[sp_args].[default_value] AS [parameter_default], "
      "[sp_args].[comment] AS [parameter_comment] "
    "FROM "
      /* CT_STORED_PROC_ARGS_NAME */
      "[%s] AS [sp_args] "
      /* CT_DATATYPE_NAME — LEFT JOIN so OUT CURSOR (type_id=28, not in _db_data_type) rows survive */
      "LEFT OUTER JOIN [%s] AS [dt] ON [dt].[type_id] = [sp_args].[data_type] "
    "WHERE "
      AUTH_CHECK_STORED_PROC("[sp_args].[sp_of].[owner].[name]", "[sp_args].[sp_of]") " "
      "AND [sp_args].[sp_of].[is_system_generated] = 0",
    SP_MODE_IN, SP_MODE_OUT, SP_MODE_INOUT,
    DB_TYPE_RESULTSET,
    DB_TYPE_DATETIME,
    DB_TYPE_TIME, DB_TYPE_TIMESTAMP, DB_TYPE_DATE,
    SP_TYPE_PROCEDURE, SP_TYPE_FUNCTION,
    CT_STORED_PROC_ARGS_NAME,
    CT_DATATYPE_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_partitions_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[super].[owner].[name] AS [table_schema], "
      "[super].[class_name] AS [table_name], "
      "[part].[pname] AS [partition_name], "
      "NULL AS [partition_ordinal_position], "
      /* DB_PARTITION_HASH, DB_PARTITION_RANGE, DB_PARTITION_LIST */
      "DECODE ([part].[ptype], %d, 'HASH', %d, 'RANGE', %d, 'LIST') AS [partition_method], "
      "[part_info].[pexpr] AS [partition_expression], "
      /* DB_PARTITION_RANGE, DB_PARTITION_LIST */
      "IF ([part].[ptype] IN (%d, %d), COLLECTION_TO_STRING ([part].[pvalues]), NULL) AS [partition_description], "
      "NULL AS [subpartition_name], "
      "NULL AS [subpartition_ordinal_position], "
      "NULL AS [subpartition_method], "
      "NULL AS [subpartition_expression], "
      "estimated_table_rows ([part_cls].[unique_name]) AS [table_rows], "
      "estimated_avg_row_length ([part_cls].[unique_name]) AS [avg_row_length], "
      "estimated_data_length ([part_cls].[unique_name]) AS [data_length], "
      "estimated_data_free ([part_cls].[unique_name]) AS [data_free], "
      "NULL AS [tablespace_name], "
      "[part].[comment] AS [partition_comment], "
      "[part_cls].[created_time] AS [create_time], "
      "[part_cls].[updated_time] AS [update_time] "
    "FROM "
      /* CT_PARTITION_NAME */
      "[%s] AS [part] "
      /* CT_CLASS_NAME - partition class */
      "INNER JOIN [%s] AS [part_cls] ON [part_cls] = [part].[class_of], "
      "TABLE ([part_cls].[super_classes]) AS [t] ([super]), "
      "TABLE ([super].[partition]) AS [t2] ([part_info]) "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[super].[owner].[name]", "[super].[class_of]"),
    DB_PARTITION_HASH,
    DB_PARTITION_RANGE,
    DB_PARTITION_LIST,
    DB_PARTITION_RANGE,
    DB_PARTITION_LIST,
    CT_PARTITION_NAME,
    CT_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_referential_constraints_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [constraint_catalog], " /* string -> varchar(255) */
      "[idx].[class_of].[owner].[name] AS [constraint_schema], "
      "[idx].[index_name] AS [constraint_name], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [unique_constraint_catalog], " /* string -> varchar(255) */
      "[idx].[referential_index].[class_of].[owner].[name] AS [unique_constraint_schema], "
      "[idx].[referential_index].[index_name] AS [unique_constraint_name], "
      /* SM_FK_MATCH_NONE, SM_FK_MATCH_PARTIAL, SM_FK_MATCH_FULL */
      "DECODE ([idx].[referential_match_option], %d, 'NONE', %d, 'PARTIAL', %d, 'FULL') AS [match_option], "
      /* SM_FOREIGN_KEY_RESTRICT, SM_FOREIGN_KEY_NO_ACTION, SM_FOREIGN_KEY_SET_NULL */
      "DECODE ([idx].[update_rule], %d, 'RESTRICT', %d, 'NO ACTION', %d, 'SET NULL') AS [update_rule], "
      /* SM_FOREIGN_KEY_CASCADE, SM_FOREIGN_KEY_RESTRICT, SM_FOREIGN_KEY_NO_ACTION, SM_FOREIGN_KEY_SET_NULL */
      "DECODE ([idx].[delete_rule], %d, 'CASCADE', %d, 'RESTRICT', %d, 'NO ACTION', %d, 'SET NULL') AS [delete_rule], "
      "[idx].[class_of].[class_name] AS [table_name], "
      "[idx].[referential_index].[class_of].[class_name] AS [referenced_table_name] "
    "FROM "
      /* CT_INDEX_NAME */
      "[%s] AS [idx] "
    "WHERE "
      AUTH_CHECK_OBJECT_WRITE("[idx].[class_of].[owner].[name]", "[idx].[class_of].[class_of]") " "
      "AND [idx].[is_foreign_key] = 1",
    SM_FK_MATCH_NONE,
    SM_FK_MATCH_PARTIAL,
    SM_FK_MATCH_FULL,
    SM_FOREIGN_KEY_RESTRICT,
    SM_FOREIGN_KEY_NO_ACTION,
    SM_FOREIGN_KEY_SET_NULL,
    SM_FOREIGN_KEY_CASCADE,
    SM_FOREIGN_KEY_RESTRICT,
    SM_FOREIGN_KEY_NO_ACTION,
    SM_FOREIGN_KEY_SET_NULL,
    CT_INDEX_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_routine_privileges_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "[auth].[grantor].[name] AS [grantor], "
      "[auth].[grantee].[name] AS [grantee], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [specific_catalog], " /* string -> varchar(255) */
      "[sp].[owner].[name] AS [specific_schema], "
      "IF ([sp].[pkg_name] IS NOT NULL, CONCAT ([sp].[pkg_name], '.', [sp].[sp_name]), [sp].[sp_name]) AS [specific_name], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [routine_catalog], " /* string -> varchar(255) */
      "[sp].[owner].[name] AS [routine_schema], "
      "[sp].[sp_name] AS [routine_name], "
      "'EXECUTE' AS [privilege_type], "
      "'NO' AS [is_grantable] "
    "FROM "
      /* CT_CLASSAUTH_NAME */
      "[%s] AS [auth] "
      /* CT_STORED_PROC_NAME */
      "INNER JOIN [%s] AS [sp] ON [sp] = [auth].[object_of] "
    "WHERE "
      AUTH_CHECK_PRIVILEGE("[auth].[grantee].[name]", "[auth].[grantor].[name]") " "
      /* DB_OBJECT_PROCEDURE */
      "AND [auth].[object_type] = %d",
    CT_CLASSAUTH_NAME,
    CT_STORED_PROC_NAME,
    DB_OBJECT_PROCEDURE);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_routines_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "IF ([sp].[pkg_name] IS NOT NULL, CONCAT ([sp].[pkg_name], '.', [sp].[sp_name]), [sp].[sp_name]) AS [specific_name], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [routine_catalog], " /* string -> varchar(255) */
      "[sp].[owner].[name] AS [routine_schema], "
      "[sp].[sp_name] AS [routine_name], "
      /* SP_TYPE_PROCEDURE, SP_TYPE_FUNCTION */
      "DECODE ([sp].[sp_type], %d, 'PROCEDURE', %d, 'FUNCTION') AS [routine_type], "
      /* SP_TYPE_FUNCTION; DB_TYPE_RESULTSET has no row in _db_data_type, surface it as 'CURSOR' */
      "IF ([sp].[sp_type] = %d, CASE [sp].[return_type] WHEN %d THEN 'CURSOR' ELSE [dt].[type_name] END, NULL) AS [data_type], "
      "NULL AS [character_maximum_length], "
      "NULL AS [character_octet_length], "
      /* DB_TYPE_STRING/VARCHAR, DB_TYPE_CHAR */
      "IF ([sp].[return_type] IN (%d, %d), [ch].[charset_name], NULL) AS [character_set_name], "
      "NULL AS [collation_name], "
      "NULL AS [numeric_precision], "
      "NULL AS [numeric_scale], "
      /* DB_TYPE_DATETIME -> 3, DB_TYPE_TIME, DB_TYPE_TIMESTAMP, DB_TYPE_DATE -> 0, else NULL */
      "CASE "
        /* SP_TYPE_PROCEDURE */
        "WHEN [sp].[sp_type] = %d THEN NULL "
        "WHEN [sp].[return_type] = %d THEN 3 "
        "WHEN [sp].[return_type] IN (%d, %d, %d) THEN 0 "
        "ELSE NULL "
      "END AS [datetime_precision], "
      "NULL AS [dtd_identifier], "
      "'EXTERNAL' AS [routine_body], "
      /* SP_LANG_PLCSQL */
      "IF ([sp].[lang] = %d "
          "AND (" AUTH_CHECK_DBA " OR " AUTH_CHECK_OWNER ("[sp].[owner].[name]") "), "
          "[sp_code].[scode], NULL) AS [routine_definition], "
      /* SP_LANG_JAVA */
      "IF ([sp].[lang] = %d, [sp].[target_class], NULL) AS [external_name], "
      /* SP_LANG_PLCSQL, SP_LANG_JAVA */
      "DECODE ([sp].[lang], %d, 'PLCSQL', %d, 'JAVA') AS [external_language], "
      "'SQL' AS [parameter_style], "
      /* SP_DIRECTIVE_DETERMINISTIC */
      "IF (([sp].[directive] & %d) <> 0, 'YES', 'NO') AS [is_deterministic], "
      "CASE [sp].[sql_data_access] "
        "WHEN %d THEN 'NO SQL' "
        "WHEN %d THEN 'CONTAINS SQL' "
        "WHEN %d THEN 'READS SQL DATA' "
        "WHEN %d THEN 'MODIFIES SQL DATA' "
        "ELSE NULL "
      "END AS [sql_data_access], "
      "NULL AS [sql_path], "
      /* SP_DIRECTIVE_RIGHTS_CALLER */
      "IF (([sp].[directive] & %d) <> 0, 'INVOKER', 'DEFINER') AS [security_type], "
      "[sp].[comment] AS [routine_comment], "
      "[sp].[created_time] AS [created], "
      "[sp].[updated_time] AS [last_altered] "
    "FROM "
      /* CT_STORED_PROC_NAME */
      "[%s] AS [sp] "
      /* CT_DATATYPE_NAME */
      "LEFT OUTER JOIN [%s] AS [dt] ON [dt].[type_id] = [sp].[return_type] "
      /* CT_STORED_PROC_CODE_NAME */
      "LEFT OUTER JOIN [%s] AS [sp_code] ON [sp_code].[name] = [sp].[target_class], "
      /* CT_ROOT_NAME */
      "[%s] AS [root], "
      /* CT_CHARSET_NAME */
      "[%s] AS [ch] "
    "WHERE "
      "[ch].[charset_id] = [root].[charset] "
      "AND " AUTH_CHECK_STORED_PROC("[sp].[owner].[name]", "[sp]") " "
      "AND [sp].[is_system_generated] = 0",
    SP_TYPE_PROCEDURE, SP_TYPE_FUNCTION,
    SP_TYPE_FUNCTION, DB_TYPE_RESULTSET,
    DB_TYPE_STRING, DB_TYPE_CHAR,
    SP_TYPE_PROCEDURE,
    DB_TYPE_DATETIME,
    DB_TYPE_TIME, DB_TYPE_TIMESTAMP, DB_TYPE_DATE,
    SP_LANG_PLCSQL,
    SP_LANG_JAVA,
    SP_LANG_PLCSQL, SP_LANG_JAVA,
    SP_DIRECTIVE_DETERMINISTIC,
    SP_SQL_TYPE_NO_SQL,
    SP_SQL_TYPE_CONTAINS_SQL,
    SP_SQL_TYPE_READS_SQL_DATA,
    SP_SQL_TYPE_MODIFIES_SQL_DATA,
    SP_DIRECTIVE_RIGHTS_CALLER,
    CT_STORED_PROC_NAME,
    CT_DATATYPE_NAME,
    CT_STORED_PROC_CODE_NAME,
    CT_ROOT_NAME,
    CT_CHARSET_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_schemata_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [catalog_name], " /* string -> varchar(255) */
      "[usr].[name] AS [schema_name], "
      "[usr].[name] AS [schema_owner], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [default_character_set_catalog], " /* string -> varchar(255) */
      "NULL AS [default_character_set_schema], "
      "CAST ([ch].[charset_name] AS VARCHAR (32)) AS [default_character_set_name], " /* string -> varchar(32) */
      "NULL AS [sql_path], "
      "[usr].[comment] AS [schema_comment], "
      "[usr].[created_time] AS [create_time] "
    "FROM "
      /* AU_USER_CLASS_NAME */
      "[%s] AS [usr], "
      /* CT_ROOT_NAME */
      "[%s] AS [root], "
      /* CT_CHARSET_NAME */
      "[%s] AS [ch] "
    "WHERE "
      "[ch].[charset_id] = [root].[charset] "
      "AND " AUTH_CHECK_SCHEMA("[usr].[name]", "[usr]"),
    AU_USER_CLASS_NAME,
    CT_ROOT_NAME,
    CT_CHARSET_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_sequences_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [sequence_catalog], " /* string -> varchar(255) */
      "[serial].[owner].[name] AS [sequence_schema], "
      "[serial].[name] AS [sequence_name], "
      "'NUMERIC' AS [data_type], "
      /* DB_MAX_NUMERIC_PRECISION */
      "%d AS [numeric_precision], "
      "10 AS [numeric_precision_radix], "
      /* DB_DEFAULT_NUMERIC_SCALE */
      "%d AS [numeric_scale], "
      "[serial].[start_val] AS [start_value], "
      "[serial].[min_val] AS [minimum_value], "
      "[serial].[max_val] AS [maximum_value], "
      "[serial].[increment_val] AS [increment], "
      "IF ([serial].[cyclic] = 1, 'YES', 'NO') AS [cycle_option], "
      "IF ([serial].[cached_num] > 0, 'YES', 'NO') AS [is_cached], "
      "[serial].[comment] AS [sequence_comment], "
      "[serial].[created_time] AS [create_time], "
      "[serial].[updated_time] AS [update_time] "
    "FROM "
      /* CT_SERIAL_NAME */
      "[%s] AS [serial] "
    "WHERE "
      "[serial].[class_name] IS NULL "
      "AND " AUTH_CHECK_OBJECT_ANY("[serial].[owner].[name]", "[serial]"),
    DB_MAX_NUMERIC_PRECISION,
    DB_DEFAULT_NUMERIC_SCALE,
    CT_SERIAL_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_statistics_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[cls].[owner].[name] AS [table_schema], "
      "[cls].[class_name] AS [table_name], "
      "[idx].[is_unique] AS [is_unique], "
      "[cls].[owner].[name] AS [index_schema], "
      "[idx].[index_name] AS [index_name], "
      "([idx_key].[key_order] + 1) AS [seq_in_index], "
      "[idx_key].[key_attr_name] AS [column_name], "
      "CAST (CASE "
        "WHEN [idx_key].[asc_desc] = 0 THEN 'A' "
        "WHEN [idx_key].[asc_desc] = 1 THEN 'D' "
        "ELSE NULL "
      "END AS VARCHAR (1)) AS [collation], "
      "CAST (INDEX_CARDINALITY ("
        "[cls].[unique_name], "
        "[idx].[index_name], "
        "[idx_key].[key_order]"
      ") AS INTEGER) AS [cardinality], "
      "[idx_key].[key_prefix_length] AS [sub_part], "
      "IF ([idx_key].[key_attr_name] IS NULL, 'YES', "
        "IF ([attr].[is_nullable] = 1, 'YES', 'NO')) AS [nullable], "
      "'BTREE' AS [index_type], "
      "NULL AS [comment], "
      "[idx].[comment] AS [index_comment], "
      "IF ([idx].[status] = 1, 'YES', 'NO') AS [is_visible], "
      "[idx_key].[func] AS [expression], "
      "[idx].[options] & %d AS [deduplicate_level], "
      "[idx].[created_time] AS [create_time], "
      "[idx].[updated_time] AS [update_time], "
      "NULL AS [access_time] "
    "FROM "
      /* CT_INDEXKEY_NAME */
      "[%s] AS [idx_key] "
      /* CT_INDEX_NAME */
      "INNER JOIN [%s] AS [idx] ON [idx] = [idx_key].[index_of] "
      /* CT_CLASS_NAME */
      "INNER JOIN [%s] AS [cls] ON [cls] = [idx].[class_of] "
      /* CT_ATTRIBUTE_NAME */
      "LEFT OUTER JOIN [%s] AS [attr] ON [attr].[class_of] = [cls] AND [attr].[attr_name] = [idx_key].[key_attr_name] "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[cls].[owner].[name]", "[cls].[class_of]") " "
      "AND ([idx_key].[key_attr_name] IS NULL OR [idx_key].[key_attr_name] NOT LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN ")",
    OPTION_DEDUPLICATE_MASK,
    CT_INDEXKEY_NAME,
    CT_INDEX_NAME,
    CT_CLASS_NAME,
    CT_ATTRIBUTE_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_synonyms_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [synonym_catalog], " /* string -> varchar(255) */
      "[syn].[owner].[name] AS [synonym_schema], "
      "[syn].[name] AS [synonym_name], "
      "IF ([syn].[is_public] = 1, 'YES', 'NO') AS [is_public_synonym], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [target_catalog], " /* string -> varchar(255) */
      "[syn].[target_owner].[name] AS [target_schema], "
      "[syn].[target_name] AS [target_name], "
      "[syn].[comment] AS [synonym_comment], "
      "[syn].[created_time] AS [create_time], "
      "[syn].[updated_time] AS [update_time] "
    "FROM "
      /* CT_SYNONYM_NAME */
      "[%s] AS [syn] "
    "WHERE "
      AUTH_CHECK_SYNONYM("[syn].[is_public]", "[syn].[owner].[name]"),
    CT_SYNONYM_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_table_constraints_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [constraint_catalog], " /* string -> varchar(255) */
      "[idx].[class_of].[owner].[name] AS [constraint_schema], "
      "[idx].[index_name] AS [constraint_name], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[idx].[class_of].[owner].[name] AS [table_schema], "
      "[idx].[class_of].[class_name] AS [table_name], "
      "CASE "
        "WHEN [idx].[is_primary_key] = 1 THEN 'PRIMARY KEY' "
        "WHEN [idx].[is_foreign_key] = 1 THEN 'FOREIGN KEY' "
        "WHEN [idx].[is_unique] = 1 THEN 'UNIQUE' "
        "ELSE NULL "
      "END AS [constraint_type], "
      "'NO' AS [is_deferrable], "
      "'NO' AS [initially_deferred], "
      "IF ([idx].[status] = 0, 'NO', 'YES') AS [enforced] "
    "FROM "
      /* CT_INDEX_NAME */
      "[%s] AS [idx] "
    "WHERE "
      AUTH_CHECK_OBJECT_WRITE("[idx].[class_of].[owner].[name]", "[idx].[class_of].[class_of]") " "
      "AND ([idx].[is_primary_key] = 1 OR [idx].[is_unique] = 1 OR [idx].[is_foreign_key] = 1)",
    CT_INDEX_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_table_privileges_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "[auth].[grantor].[name] AS [grantor], "
      "[auth].[grantee].[name] AS [grantee], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[cls].[owner].[name] AS [table_schema], "
      "[cls].[class_name] AS [table_name], "
      "[auth].[auth_type] AS [privilege_type], "
      "IF ([auth].[is_grantable] = 0, 'NO', 'YES') AS [is_grantable] "
    "FROM "
      /* CT_CLASSAUTH_NAME */
      "[%s] AS [auth] "
      /* CT_CLASS_NAME */
      "INNER JOIN [%s] AS [cls] ON [cls].[class_of] = [auth].[object_of] "
    "WHERE "
      AUTH_CHECK_PRIVILEGE("[auth].[grantee].[name]", "[auth].[grantor].[name]") " "
      /* DB_OBJECT_CLASS */
      "AND [auth].[object_type] = %d",
    CT_CLASSAUTH_NAME,
    CT_CLASS_NAME,
    DB_OBJECT_CLASS);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_tables_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[cls].[owner].[name] AS [table_schema], "
      "CAST ([cls].[class_name] AS VARCHAR (255)) AS [table_name], " /* string -> varchar(255) */
      /* SM_CLASS_CT */
      "CASE "
        "WHEN [cls].[class_type] = %d AND [cls].[is_system_class] = 1 THEN 'SYSTEM TABLE' "
        "WHEN [cls].[class_type] <> %d AND [cls].[is_system_class] = 1 THEN 'SYSTEM VIEW' "
        "WHEN [cls].[class_type] = %d AND [cls].[is_system_class] <> 1 THEN 'BASE TABLE' "
        "WHEN [cls].[class_type] <> %d AND [cls].[is_system_class] <> 1 THEN 'VIEW' "
        "ELSE NULL "
      "END AS [table_type], "
      "estimated_table_rows ([cls].[unique_name]) AS [table_rows], "
      "estimated_avg_row_length ([cls].[unique_name]) AS [avg_row_length], "
      "estimated_data_length ([cls].[unique_name]) AS [data_length], "
      "estimated_data_free ([cls].[unique_name]) AS [data_free], "
      "CASE "
        "WHEN [serial].[current_val] IS NULL THEN NULL "
        "WHEN [serial].[started] = 0 THEN [serial].[current_val] "
        /* Check overflow: increment_val > 0 and current_val > max_val - increment_val */
        "WHEN [serial].[increment_val] > 0 "
          "AND [serial].[current_val] > [serial].[max_val] - [serial].[increment_val] THEN NULL "
        /* Check underflow: increment_val < 0 and current_val < min_val - increment_val */
        "WHEN [serial].[increment_val] < 0 "
          "AND [serial].[current_val] < [serial].[min_val] - [serial].[increment_val] THEN NULL "
        "ELSE [serial].[current_val] + [serial].[increment_val] "
      "END AS [auto_increment], "
      "[coll].[coll_name] AS [table_collation], "
      /* SM_CLASSFLAG_REUSE_OID */
      "IF (([cls].[flags] & %d) <> 0, 'REUSE_OID', 'DONT_REUSE_OID') || "
      /* TDE_ALGORITHM_NONE, TDE_ALGORITHM_AES, TDE_ALGORITHM_ARIA */
      "' ENCRYPT=' || DECODE ([cls].[tde_algorithm], %d, 'NONE', %d, 'AES', %d, 'ARIA') AS [create_options], "
      "'NO' AS [is_temporary], "
      "[cls].[comment] AS [table_comment], "
      "[cls].[created_time] AS [create_time], "
      "[cls].[updated_time] AS [update_time], "
      "DECODE ([cls].[statistics_strategy], 0, 'SAMPLING', 1, 'FULLSCAN') AS [statistics_strategy], "
      "[cls].[checked_time] AS [last_analyzed] "
    "FROM "
      /* CT_CLASS_NAME */
      "[%s] AS [cls] "
      /* CT_COLLATION_NAME */
      "INNER JOIN [%s] AS [coll] ON [coll].[coll_id] = [cls].[collation_id] "
      /* CT_SERIAL_NAME */
      "LEFT OUTER JOIN [%s] AS [serial] "
        "ON [serial].[class_name] = [cls].[class_name] AND [serial].[owner] = [cls].[owner] "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[cls].[owner].[name]", "[cls].[class_of]"),
    SM_CLASS_CT,
    SM_CLASS_CT,
    SM_CLASS_CT,
    SM_CLASS_CT,
    SM_CLASSFLAG_REUSE_OID,
    TDE_ALGORITHM_NONE,
    TDE_ALGORITHM_AES,
    TDE_ALGORITHM_ARIA,
    CT_CLASS_NAME,
    CT_COLLATION_NAME,
    CT_SERIAL_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_triggers_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [trigger_catalog], " /* string -> varchar(255) */
      "[tr].[owner].[name] AS [trigger_schema], "
      "CAST ([tr].[name] AS VARCHAR (255)) AS [trigger_name], " /* string -> varchar(255) */
      /* TR_EVENT_UPDATE/STATEMENT_UPDATE, TR_EVENT_DELETE/STATEMENT_DELETE, TR_EVENT_INSERT/STATEMENT_INSERT */
      "CASE "
        "WHEN [tr].[event] IN (%d, %d) THEN 'UPDATE' "
        "WHEN [tr].[event] IN (%d, %d) THEN 'DELETE' "
        "WHEN [tr].[event] IN (%d, %d) THEN 'INSERT' "
        "ELSE NULL "
      "END AS [event_manipulation], "
      "CAST (DATABASE () AS VARCHAR (255)) AS [event_object_catalog], " /* string -> varchar(255) */
      "[cls].[owner].[name] AS [event_object_schema], "
      "[cls].[class_name] AS [event_object_table], "
      "[tr].[target_attribute] AS [event_object_column], "
      "NULL AS [action_order], "
      "[tr].[condition] AS [action_condition], "
      "[tr].[action_definition] AS [action_statement], "
      /* row events: TR_EVENT_UPDATE/DELETE/INSERT; statement events: TR_EVENT_STATEMENT_*; NULL for COMMIT/ROLLBACK */
      "CASE "
        "WHEN [tr].[event] IN (%d, %d, %d) THEN 'ROW' "
        "WHEN [tr].[event] IN (%d, %d, %d) THEN 'STATEMENT' "
        "ELSE NULL "
      "END AS [action_orientation], "
      /* TR_TIME_BEFORE, TR_TIME_AFTER, TR_TIME_DEFERRED */
      "DECODE ([tr].[action_time], %d, 'BEFORE', %d, 'AFTER', %d, 'DEFERRED') AS [action_timing], "
      "NULL AS [action_reference_old_table], "
      "NULL AS [action_reference_new_table], "
      "'OLD' AS [action_reference_old_row], "
      "'NEW' AS [action_reference_new_row], "
      "[tr].[comment] AS [trigger_comment], "
      "[tr].[created_time] AS [create_time], "
      "[tr].[updated_time] AS [update_time] "
    "FROM "
      /* TR_CLASS_NAME */
      "[%s] AS [tr] "
      /* CT_CLASS_NAME */
      "INNER JOIN [%s] AS [cls] ON [cls].[class_of] = [tr].[target_class] "
    "WHERE "
      AUTH_CHECK_OBJECT_WRITE("[tr].[owner].[name]", "[cls].[class_of]") " "
      /* DML row/statement triggers per spec */
      "AND [tr].[event] BETWEEN %d AND %d",
    TR_EVENT_UPDATE, TR_EVENT_STATEMENT_UPDATE,
    TR_EVENT_DELETE, TR_EVENT_STATEMENT_DELETE,
    TR_EVENT_INSERT, TR_EVENT_STATEMENT_INSERT,
    TR_EVENT_UPDATE, TR_EVENT_DELETE, TR_EVENT_INSERT,
    TR_EVENT_STATEMENT_UPDATE, TR_EVENT_STATEMENT_DELETE, TR_EVENT_STATEMENT_INSERT,
    TR_TIME_BEFORE, TR_TIME_AFTER, TR_TIME_DEFERRED,
    TR_CLASS_NAME,
    CT_CLASS_NAME,
    TR_EVENT_UPDATE, TR_EVENT_STATEMENT_INSERT);
  // *INDENT-ON*

  return stmt;
}

const char *sm_define_view_views_spec (void)
{
  static char stmt [4096];

  // *INDENT-OFF*
  snprintf (stmt, sizeof (stmt),
    "SELECT "
      "CAST (DATABASE () AS VARCHAR (255)) AS [table_catalog], " /* string -> varchar(255) */
      "[q].[class_of].[owner].[name] AS [table_schema], "
      "[q].[class_of].[class_name] AS [table_name], "
      "[q].[spec] AS [view_definition], "
      "CASE "
        /* SM_CLASSFLAG_WITHCHECKOPTION */
        "WHEN ([q].[class_of].[flags] & %d) <> 0 THEN 'CASCADED' "
        /* SM_CLASSFLAG_LOCALCHECKOPTION */
        "WHEN ([q].[class_of].[flags] & %d) <> 0 THEN 'LOCAL' "
        "ELSE 'NONE' "
      "END AS [check_option], "
      "NULL AS [is_updatable], "
      "[q].[class_of].[comment] AS [view_comment], "
      "[q].[class_of].[created_time] AS [create_time], "
      "[q].[class_of].[updated_time] AS [update_time] "
    "FROM "
      /* CT_QUERYSPEC_NAME */
      "[%s] AS [q] "
    "WHERE "
      AUTH_CHECK_OBJECT_ANY("[q].[class_of].[owner].[name]", "[q].[class_of].[class_of]"),
    SM_CLASSFLAG_WITHCHECKOPTION,
    SM_CLASSFLAG_LOCALCHECKOPTION,
    CT_QUERYSPEC_NAME);
  // *INDENT-ON*

  return stmt;
}

