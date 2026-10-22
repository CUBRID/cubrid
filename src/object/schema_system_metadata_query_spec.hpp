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
 * schema_system_metadata_query_spec.hpp
 */

#ifndef _SCHEMA_SYSTEM_METADATA_QUERY_SPEC_HPP_
#define _SCHEMA_SYSTEM_METADATA_QUERY_SPEC_HPP_

#include "authenticate.h"
#include "schema_system_catalog_constants.h"

// TODO: Add checking the following rules in compile time (@hgryoo)
/*
 * Please follow the rules below when writing query specifications for system virtual classes
 * and information_schema views.
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

/* ============================================================================= */
/* Authorization check macros: both checks let DBA through */
/* ============================================================================= */

#define CURRENT_USER_GROUPS_SUBQUERY \
  "SELECT SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) " \
  "FROM [" AU_USER_CLASS_NAME "] AS [u], TABLE ([u].[groups]) AS [t] ([g]) " \
  "WHERE [u].[name] = CURRENT_USER"

#define AUTH_CHECK_USER(user_names_expr) \
  "({'DBA', " user_names_expr "} * (" CURRENT_USER_GROUPS_SUBQUERY ") SETNEQ {})"

#define AUTH_CHECK_PRIVILEGE(owner_name_expr, object_of_expr, auth_type) \
  "(" \
    AUTH_CHECK_USER (owner_name_expr) " " \
    "OR {" object_of_expr "} SUBSETEQ (" \
      "SELECT SUM (SET {[au].[object_of]}) FROM [" CT_CLASSAUTH_NAME "] AS [au] " \
      "WHERE {[au].[grantee].[name]} SUBSETEQ (" CURRENT_USER_GROUPS_SUBQUERY ") " \
      "AND [au].[auth_type] = '" auth_type "'" \
    ")" \
  ")"

#endif // _SCHEMA_SYSTEM_METADATA_QUERY_SPEC_HPP_
