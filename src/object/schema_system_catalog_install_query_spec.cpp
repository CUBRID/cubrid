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
 * schema_system_catalog_install_query_spec.cpp
 */

#include "schema_system_catalog_install.hpp"

#include "authenticate.h"
#include "deduplicate_key.h"
#include "schema_system_catalog_constants.h"
#include "trigger_manager.h"

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

const char *
sm_define_view_class_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [c].[class_type] WHEN 0 THEN 'CLASS' WHEN 1 THEN 'VCLASS' ELSE 'UNKNOW' END AS [class_type], "
	  "CASE WHEN MOD ([c].[is_system_class], 2) = 1 THEN 'YES' ELSE 'NO' END AS [is_system_class], "
	  "CASE [c].[tde_algorithm] WHEN 0 THEN 'NONE' WHEN 1 THEN 'AES' WHEN 2 THEN 'ARIA' END AS [tde_algorithm], "
	  "CASE "
	    "WHEN [c].[sub_classes] IS NULL THEN 'NO' "
	    /* CT_PARTITION_NAME */
	    "ELSE NVL ((SELECT 'YES' FROM [%s] AS [p] WHERE [p].[class_of] = [c] AND [p].[pname] IS NULL), 'NO') "
	    "END AS [partitioned], "
	  "CASE WHEN MOD ([c].[is_system_class] / 8, 2) = 1 THEN 'YES' ELSE 'NO' END AS [is_reuse_oid_class], "
	  "[coll].[coll_name] AS [collation], "
	  "[c].[comment] AS [comment] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_COLLATION_NAME */
	  "[%s] AS [coll] "
	"WHERE "
	  "[c].[collation_id] = [coll].[coll_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_PARTITION_NAME,
	CT_CLASS_NAME,
	CT_COLLATION_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_super_class_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[s].[class_name] AS [super_class_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [super_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], TABLE ([c].[super_classes]) AS [t] ([s]) "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_vclass_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[q].[class_of].[class_name] AS [vclass_name], "
	  "CAST ([q].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[q].[spec] AS [vclass_def], "
	  "[c].[comment] AS [comment] "
	"FROM "
	  /* CT_QUERYSPEC_NAME */
	  "[%s] AS [q], "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c] "
	"WHERE "
	  "[q].[class_of] = [c] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[q].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[q].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_QUERYSPEC_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_attribute_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[a].[attr_name] AS [attr_name], "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [a].[attr_type] WHEN 0 THEN 'INSTANCE' WHEN 1 THEN 'CLASS' ELSE 'SHARED' END AS [attr_type], "
	  "[a].[def_order] AS [def_order], "
	  "[a].[from_class_of].[class_name] AS [from_class_name], "
	  "CAST ([a].[from_class_of].[owner].[name] AS VARCHAR(255)) AS [from_owner_name], " /* string -> varchar(255) */
	  "[a].[from_attr_name] AS [from_attr_name], "
	  "[t].[type_name] AS [data_type], "
	  "[d].[prec] AS [prec], "
	  "[d].[scale] AS [scale], "
	  "IF ("
	      "[a].[data_type] IN (4, 25, 26, 27, 35), "
	      /* CT_CHARSET_NAME */
	      "(SELECT [ch].[charset_name] FROM [%s] AS [ch] WHERE [d].[code_set] = [ch].[charset_id]), "
	      "'Not applicable'"
	    ") AS [charset], "
	  "IF ("
	      "[a].[data_type] IN (4, 25, 26, 27, 35), "
	      /* CT_COLLATION_NAME */
	      "(SELECT [coll].[coll_name] FROM [%s] AS [coll] WHERE [d].[collation_id] = [coll].[coll_id]), "
	      "'Not applicable'"
	    ") AS [collation], "
	  "[d].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([d].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name], " /* string -> varchar(255) */
	  "[a].[default_value] AS [default_value], "
	  "CASE WHEN [a].[is_nullable] = 1 THEN 'YES' ELSE 'NO' END AS [is_nullable], "
	  "[a].[comment] AS [comment] "
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_ATTRIBUTE_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [t] "
	"WHERE "
	  "[a].[class_of] = [c] "
	  "AND [d].[object_of] = [a] "
	  "AND [d].[data_type] = [t].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_CHARSET_NAME,
	CT_COLLATION_NAME,
	CT_CLASS_NAME,
	CT_ATTRIBUTE_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_attribute_set_domain_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[a].[attr_name] AS [attr_name], "
	  "[c].[class_name] AS [class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [a].[attr_type] WHEN 0 THEN 'INSTANCE' WHEN 1 THEN 'CLASS' ELSE 'SHARED' END AS [attr_type], "
	  "[et].[type_name] AS [data_type], "
	  "[e].[prec] AS [prec], "
	  "[e].[scale] AS [scale], "
	  "[e].[code_set] AS [code_set], "
	  "[e].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([e].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], "
	  /* CT_ATTRIBUTE_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], TABLE ([d].[set_domains]) AS [t] ([e]), "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [et] "
	"WHERE "
	  "[a].[class_of] = [c] "
	  "AND [d].[object_of] = [a] "
	  "AND [e].[data_type] = [et].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[c]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_CLASS_NAME,
	CT_ATTRIBUTE_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_method_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[m].[meth_name] AS [meth_name], "
	  "[m].[class_of].[class_name] AS [class_name], "
	  "CAST ([m].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [m].[meth_type] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [meth_type], "
	  "[m].[from_class_of].[class_name] AS [from_class_name], "
	  "CAST ([m].[from_class_of].[owner].[name] AS VARCHAR(255)) AS [from_owner_name], " /* string -> varchar(255) */
	  "[m].[from_meth_name] AS [from_meth_name], "
	  "[s].[func_name] AS [func_name] "
	"FROM "
	  /* CT_METHOD_NAME */
	  "[%s] AS [m], "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s] "
	"WHERE "
	  "[s].[meth_of] = [m] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[m].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[m].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHOD_NAME,
	CT_METHSIG_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_method_argument_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[meth_of].[meth_name] AS [meth_name], "
	  "[s].[meth_of].[class_of].[class_name] AS [class_name], "
	  "CAST ([s].[meth_of].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [s].[meth_of].[meth_type] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [meth_type], "
	  "[a].[index_of] AS [index_of], "
	  "[t].[type_name] AS [data_type], "
	  "[d].[prec] AS [prec], "
	  "[d].[scale] AS [scale], "
	  "[d].[code_set] AS [code_set], "
	  "[d].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([d].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s], "
	  /* CT_METHARG_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [t] "
	"WHERE "
	  "[a].[meth_sig_of] = [s] "
	  "AND [d].[object_of] = [a] "
	  "AND [d].[data_type] = [t].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHSIG_NAME,
	CT_METHARG_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_method_argument_set_domain_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[meth_of].[meth_name] AS [meth_name], "
	  "[s].[meth_of].[class_of].[class_name] AS [class_name], "
	  "CAST ([s].[meth_of].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "CASE [s].[meth_of].[meth_type] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [meth_type], "
	  "[a].[index_of] AS [index_of], "
	  "[et].[type_name] AS [data_type], "
	  "[e].[prec] AS [prec], "
	  "[e].[scale] AS [scale], "
	  "[e].[code_set] AS [code_set], "
	  "[e].[class_of].[class_name] AS [domain_class_name], "
	  "CAST ([e].[class_of].[owner].[name] AS VARCHAR(255)) AS [domain_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_METHSIG_NAME */
	  "[%s] AS [s], "
	  /* CT_METHARG_NAME */
	  "[%s] AS [a], "
	  /* CT_DOMAIN_NAME */
	  "[%s] AS [d], TABLE ([d].[set_domains]) AS [t] ([e]), "
	  /* CT_DATATYPE_NAME */
	  "[%s] AS [et] "
	"WHERE "
	  "[a].[meth_sig_of] = [s] "
	  "AND [d].[object_of] = [a] "
	  "AND [e].[data_type] = [et].[type_id] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[s].[meth_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_METHSIG_NAME,
	CT_METHARG_NAME,
	CT_DOMAIN_NAME,
	CT_DATATYPE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_method_file_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[f].[class_of].[class_name] AS [class_name], "
	  "CAST ([f].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[f].[path_name] AS [path_name], "
	  "[f].[from_class_of].[class_name] AS [from_class_name], "
	  "CAST ([f].[from_class_of].[owner].[name] AS VARCHAR(255)) AS [from_owner_name] " /* string -> varchar(255) */
	"FROM "
	  /* CT_METHFILE_NAME */
	  "[%s] AS [f] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[f].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[f].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_METHFILE_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_index_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[i].[index_name] AS [index_name], "
	  "CASE [i].[is_unique] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_unique], "
	  "CASE [i].[is_reverse] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_reverse], "
	  "[i].[class_of].[class_name] AS [class_name], "
	  "CAST ([i].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "NVL2 ("
	      "("
		"SELECT 1 "
		"FROM "
		  /* CT_INDEXKEY_NAME */
		  "[%s] [k] "
		"WHERE "
		  "[k].index_of.class_of = [i].class_of "
		  "AND [k].index_of.index_name = [i].[index_name] "
		  "AND [k].key_attr_name LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
	      "), "
	      "([i].[key_count] - 1), "
	      "[i].[key_count]"
	    ") AS [key_count], "        
	  "CASE [i].[is_primary_key] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_primary_key], "
	  "CASE [i].[is_foreign_key] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_foreign_key], "
#if 0 // Not yet, Disabled for QA verification convenience          
/* support for SUPPORT_DEDUPLICATE_KEY_MODE */
	  "CAST(NVL ("
                  "(" 
		      "SELECT 'YES' "
		      "FROM [%s] [k] "
		      "WHERE [k].index_of.class_of = [i].class_of "
			  "AND [k].index_of.index_name = [i].[index_name] "
			  "AND [k].key_attr_name LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
   		   "), "
                   "'NO') "
                   "AS VARCHAR(3)"
             ") AS [is_deduplicate], "
	  "CAST(NVL (" 
                  "("
		      "SELECT REPLACE([k].key_attr_name,'%s','') "
		      "FROM [%s] [k]"
		      "WHERE [k].index_of.class_of = [i].class_of "
			   "AND [k].index_of.index_name = [i].[index_name] "
			   "AND [k].key_attr_name LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
		   ")"
                   ", 0)" 
                  " AS SMALLINT" 
             ") AS [deduplicate_key_level], "
#endif
	  "[i].[filter_expression] AS [filter_expression], "
	  "CASE [i].[have_function] WHEN 0 THEN 'NO' ELSE 'YES' END AS [have_function], "
	  "[i].[comment] AS [comment], "
	  "CASE [i].[status] "
	    "WHEN 0 THEN 'NO_INDEX' "
	    "WHEN 1 THEN 'NORMAL INDEX' "
	    "WHEN 2 THEN 'INVISIBLE INDEX' "
	    "WHEN 3 THEN 'INDEX IS IN ONLINE BUILDING' "
	    "ELSE 'NULL' "
	    "END AS [status] "
	"FROM "
	  /* CT_INDEX_NAME */
	  "[%s] AS [i] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[i].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[i].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",            
	CT_INDEXKEY_NAME,
#if 0 // Not yet, Disabled for QA verification convenience        
        CT_INDEXKEY_NAME,
        DEDUPLICATE_KEY_ATTR_NAME_PREFIX,
        CT_INDEXKEY_NAME,
#endif                    
	CT_INDEX_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_index_key_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[k].[index_of].[index_name] AS [index_name], "
	  "[k].[index_of].[class_of].[class_name] AS [class_name], "
	  "CAST ([k].[index_of].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[k].[key_attr_name] AS [key_attr_name], "
	  "[k].[key_order] AS [key_order], "
	  "CASE [k].[asc_desc] WHEN 0 THEN 'ASC' WHEN 1 THEN 'DESC' ELSE 'UNKN' END AS [asc_desc], "
	  "[k].[key_prefix_length] AS [key_prefix_length], "
	  "[k].[func] AS [func] "
	"FROM "
	  /* CT_INDEXKEY_NAME */
	  "[%s] AS [k] "
	"WHERE "
          "("
              "[k].[key_attr_name] IS NULL " 
              "OR [k].[key_attr_name] NOT LIKE " DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN
          ")"
          " AND ("       
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[k].[index_of].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[k].[index_of].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_INDEXKEY_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_authorization_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "CAST ([a].[grantor].[name] AS VARCHAR(255)) AS [grantor_name], " /* string -> varchar(255) */
	  "CAST ([a].[grantee].[name] AS VARCHAR(255)) AS [grantee_name], " /* string -> varchar(255) */
	  "[a].[class_of].[class_name] AS [class_name], "
	  "CAST ([a].[class_of].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[a].[auth_type] AS [auth_type], "
	  "CASE [a].[is_grantable] WHEN 0 THEN 'NO' ELSE 'YES' END AS [is_grantable] "
	"FROM "
	  /* CT_CLASSAUTH_NAME */
	  "[%s] AS [a] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[a].[class_of].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[a].[class_of]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_trigger_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "CAST ([t].[name] AS VARCHAR (255)) AS [trigger_name], " /* string -> varchar(255) */
	  "CAST ([t].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[c].[class_name] AS [target_class_name], "
	  "CAST ([c].[owner].[name] AS VARCHAR(255)) AS [target_owner_name], " /* string -> varchar(255) */
	  "CAST ([t].[target_attribute] AS VARCHAR (255)) AS [target_attr_name], " /* string -> varchar(255) */
	  "CASE [t].[target_class_attribute] WHEN 0 THEN 'INSTANCE' ELSE 'CLASS' END AS [target_attr_type], "
	  "[t].[action_type] AS [action_type], "
	  "[t].[action_time] AS [action_time], "
	  "[t].[comment] AS [comment] "
	"FROM "
	  /* TR_CLASS_NAME */
	  "[%s] AS [t] "
	  /* CT_CLASS_NAME */
	  "LEFT OUTER JOIN [%s] AS [c] ON [t].[target_class] = [c].[class_of] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[t].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[c]} SUBSETEQ (" /* Why [c] and not [t].[target_class]? */
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	TR_CLASS_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_partition_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[class_name] AS [class_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [owner_name], " /* string -> varchar(255) */
	  "[p].[pname] AS [partition_name], "
	  "CONCAT ([s].[class_name], '__p__', [p].[pname]) AS [partition_class_name], " /* It can exceed varchar(255). */
	  "CASE [p].[ptype] WHEN 0 THEN 'HASH' WHEN 1 THEN 'RANGE' ELSE 'LIST' END AS [partition_type], "
	  "TRIM (SUBSTRING ([pp].[pexpr] FROM 8 FOR (POSITION (' FROM ' IN [pp].[pexpr]) - 8))) AS [partition_expr], "
	  "[p].[pvalues] AS [partition_values], "
	  "[p].[comment] AS [comment] "
	"FROM "
	  /* CT_PARTITION_NAME */
	  "[%s] AS [p], "
	  /* CT_CLASS_NAME */
	  "[%s] AS [c], TABLE ([c].[super_classes]) AS [t] ([s]), "
	  /* CT_CLASS_NAME */
	  "[%s] AS [cc], TABLE ([cc].[partition]) AS [tt] ([pp]) "
	"WHERE "
	  "[p].[class_of] = [c] "
	  "AND [s] = [cc] "
	  "AND ("
	      "{'DBA'} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[p].[class_of].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		") "
	      "OR {[p].[class_of]} SUBSETEQ ("
		  "SELECT "
		    "SUM (SET {[au].[class_of]}) "
		  "FROM "
		    /* CT_CLASSAUTH_NAME */
		    "[%s] AS [au] "
		  "WHERE "
		    "{[au].[grantee].[name]} SUBSETEQ ("
			"SELECT "
			  "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
			"FROM "
			  /* AU_USER_CLASS_NAME */
			  "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
			"WHERE "
			  "[u].[name] = CURRENT_USER"
		      ") "
		    "AND [au].[auth_type] = 'SELECT'"
		")"
	    ")",
	CT_PARTITION_NAME,
	CT_CLASS_NAME,
	CT_CLASS_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_stored_procedure_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[sp].[sp_name] AS [sp_name], "
          "[sp].[pkg_name] AS [pkg_name], "
	  "CASE [sp].[sp_type] WHEN 1 THEN 'PROCEDURE' ELSE 'FUNCTION' END AS [sp_type], "
	  "CASE [sp].[return_type] "
	    "WHEN 0 THEN 'void' "
	    "WHEN 28 THEN 'CURSOR' "
	    /* CT_DATATYPE_NAME */
	    "ELSE (SELECT [t].[type_name] FROM [%s] AS [t] WHERE [sp].[return_type] = [t].[type_id]) "
	    "END AS [return_type], "
	  "[sp].[arg_count] AS [arg_count], "
	  "CASE [sp].[lang] WHEN 0 THEN 'PLCSQL' WHEN 1 THEN 'JAVA' ELSE 'UNKNOWN' END AS [lang], "
          "CASE [sp].[directive] & 1 WHEN 0 THEN 'OWNER' ELSE 'CALLER' END AS [authid], "
	  "[sp].[target] AS [target], "
	  "CAST ([sp].[owner].[name] AS VARCHAR(255)) AS [owner], " /* string -> varchar(255) */
	  "[sp].[comment] AS [comment] "
	"FROM "
	  /* CT_STORED_PROC_NAME */
	  "[%s] AS [sp] "
        "WHERE "
          "[sp].[is_system_generated] = 0",
	CT_DATATYPE_NAME,
	CT_STORED_PROC_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_stored_procedure_arguments_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[sp].[sp_name] AS [sp_name], "
          "[sp].[pkg_name] AS [pkg_name], "
	  "[sp].[index_of] AS [index_of], "
	  "[sp].[arg_name] AS [arg_name], "
	  "CASE [sp].[data_type] "
	    "WHEN 28 THEN 'CURSOR' "
	    /* CT_DATATYPE_NAME */
	    "ELSE (SELECT [t].[type_name] FROM [%s] AS [t] WHERE [sp].[data_type] = [t].[type_id]) "
	    "END AS [data_type], "
	  "CASE [sp].[mode] WHEN 1 THEN 'IN' WHEN 2 THEN 'OUT' ELSE 'INOUT' END AS [mode], "
	  "[sp].[comment] AS [comment] "
	"FROM "
	  /* CT_STORED_PROC_ARGS_NAME */
	  "[%s] AS [sp] "
        "WHERE [sp].[is_system_generated] = 0 "
	"ORDER BY " /* Is it possible to remove ORDER BY? */
	  "[sp].[sp_name], "
	  "[sp].[index_of]",
	CT_DATATYPE_NAME,
	CT_STORED_PROC_ARGS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_db_collation_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[coll].[coll_id] AS [coll_id], "
	  "[coll].[coll_name] AS [coll_name], "
	  "[ch].[charset_name] AS [charset_name], "
	  "CASE [coll].[built_in] WHEN 0 THEN 'No' WHEN 1 THEN 'Yes' ELSE 'ERROR' END AS [is_builtin], "
	  "CASE [coll].[expansions] WHEN 0 THEN 'No' WHEN 1 THEN 'Yes' ELSE 'ERROR' END AS [has_expansions], "
	  "[coll].[contractions] AS [contractions], "
	  "CASE [coll].[uca_strength] "
	    "WHEN 0 THEN 'Not applicable' "
	    "WHEN 1 THEN 'Primary' "
	    "WHEN 2 THEN 'Secondary' "
	    "WHEN 3 THEN 'Tertiary' "
	    "WHEN 4 THEN 'Quaternary' "
	    "WHEN 5 THEN 'Identity' "
	    "ELSE 'Unknown' "
	    "END AS [uca_strength] "
	"FROM "
	  /* CT_COLLATION_NAME */
	  "[%s] AS [coll] "
	  /* CT_CHARSET_NAME */
	  "INNER JOIN [%s] AS [ch] ON [coll].[charset_id] = [ch].[charset_id] "
	"ORDER BY " /* Is it possible to remove ORDER BY? */
	  "[coll].[coll_id]",
	CT_COLLATION_NAME,
	CT_CHARSET_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_db_charset_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[ch].[charset_id] AS [charset_id], "
	  "[ch].[charset_name] AS [charset_name], "
	  "[coll].[coll_name] AS [default_collation], "
	  "[ch].[char_size] AS [char_size] " 
	"FROM "
	  /* CT_CHARSET_NAME */
	  "[%s] AS [ch], "
	  /* CT_COLLATION_NAME */
	  "[%s] AS [coll] "
	"WHERE "
	  "[ch].[default_collation] = [coll].[coll_id] "
	"ORDER BY " /* Is it possible to remove ORDER BY? */
	  "[ch].[charset_id]",
	CT_CHARSET_NAME,
	CT_COLLATION_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_synonym_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[s].[name] AS [synonym_name], "
	  "CAST ([s].[owner].[name] AS VARCHAR(255)) AS [synonym_owner_name], " /* string -> varchar(255) */
	  "CASE [s].[is_public] WHEN 1 THEN 'YES' ELSE 'NO' END AS [is_public_synonym], "
	  "[s].[target_name] AS [target_name], "
	  "CAST ([s].[target_owner].[name] AS VARCHAR(255)) AS [target_owner_name], " /* string -> varchar(255) */
	  "[s].[comment] AS [comment] "
	"FROM "
	  /* CT_SYNONYM_NAME */
	  "[%s] AS [s] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR [s].[is_public] = 1 "
	  "OR ("
	      "[s].[is_public] = 0 "
	      "AND {[s].[owner].[name]} SUBSETEQ ("
		  "SELECT "
		    "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		  "FROM "
		    /* AU_USER_CLASS_NAME */
		    "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		  "WHERE "
		    "[u].[name] = CURRENT_USER"
		")"
	    ")",
	CT_SYNONYM_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}

const char *
sm_define_view_db_server_spec (void)
{
  static char stmt [2048];

  // *INDENT-OFF*
  sprintf (stmt,
	"SELECT "
	  "[ds].[link_name] AS [link_name], "
	  "[ds].[host] AS [host], "
	  "[ds].[port] AS [port], "
	  "[ds].[db_name] AS [db_name], "
	  "[ds].[user_name] AS [user_name], "
	  "[ds].[properties] AS [properties], "
	  "CAST ([ds].[owner].[name] AS VARCHAR(255)) AS [owner], " /* string -> varchar(255) */
	  "[ds].[comment] AS [comment] "
	"FROM "
	  /* CT_DB_SERVER_NAME */
	  "[%s] AS [ds] "
	"WHERE "
	  "{'DBA'} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[ds].[owner].[name]} SUBSETEQ ("
	      "SELECT "
		"SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
	      "FROM "
		/* AU_USER_CLASS_NAME */
		"[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
	      "WHERE "
		"[u].[name] = CURRENT_USER"
	    ") "
	  "OR {[ds]} SUBSETEQ ("
	      "SELECT "
		"SUM (SET {[au].[class_of]}) "
	      "FROM "
		/* CT_CLASSAUTH_NAME */
		"[%s] AS [au] "
	      "WHERE "
		"{[au].[grantee].[name]} SUBSETEQ ("
		    "SELECT "
		      "SET {CURRENT_USER} + COALESCE (SUM (SET {[t].[g].[name]}), SET {}) "
		    "FROM "
		      /* AU_USER_CLASS_NAME */
		      "[%s] AS [u], TABLE ([u].[groups]) AS [t] ([g]) "
		    "WHERE "
		      "[u].[name] = CURRENT_USER"
		  ") "
		"AND [au].[auth_type] = 'SELECT'"
	    ")",
	CT_DB_SERVER_NAME,
	AU_USER_CLASS_NAME,
	AU_USER_CLASS_NAME,
	CT_CLASSAUTH_NAME,
	AU_USER_CLASS_NAME);
  // *INDENT-ON*

  return stmt;
}
