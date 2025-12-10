/*
 * Copyright 2008 Search Solution Corporation
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
 * misctype_def.h - Parse tree structures and types
 */

#ifndef _MISCTYPE_DEF_H_
#define _MISCTYPE_DEF_H_

#ident "$Id$"

/* Enumerated Misc Types */
typedef enum
{
  PT_MISC_NONE = 0,
  PT_MISC_DUMMY = 3000,
  PT_ALL,
  PT_ONLY,
  PT_DISTINCT,
  PT_SHARED,
  PT_DEFAULT,
  PT_ASC,
  PT_DESC,
  PT_GRANT_OPTION,
  PT_NO_GRANT_OPTION,
  PT_CLASS,
  PT_VCLASS,
  PT_VID_ATTR,
  PT_OID_ATTR,
  /* PT_CLASSOID_ATTR is no longer used.  The concept that it used to embody (the OID of the class of an instance is
   * now captured via a first class server function F_CLASS_OF which takes an arbitrary instance valued expression. */
  PT_CLASSOID_ATTR,
  PT_TRIGGER_OID,
  PT_NORMAL,
  /* PT_META_CLASS is used to embody the concept of a class OID reference that is constant at compile time.  (i.e. it
   * does not vary as instance OIDs vary across an inheritance hierarchy).  Contrast this with the F_CLASS_OF function
   * which returns the class OID for any instance valued expression.  F_CLASS_OF is a server side function. */
  PT_META_CLASS,
  PT_META_ATTR,
  PT_PARAMETER,
  PT_HINT_NAME,			/* hint argument name */
  PT_INDEX_NAME,
  PT_RESERVED,			/* reserved names for special attributes */
  PT_IS_SUBQUERY,		/* query is sub-query, not directly producing result */
  PT_IS_UNION_SUBQUERY,		/* in a union sub-query */
  PT_IS_UNION_QUERY,		/* query directly producing result in top level union */
  PT_IS_SET_EXPR,
  PT_IS_CSELECT,		/* query is CSELECT, not directly producing result */
  PT_IS_WHACKED_SPEC,		/* ignore this one in xasl generation, no cross product */
  PT_IS_SUBINSERT,		/* used by value clause of insert */
  PT_IS_VALUE,			/* used by value clause of insert */
  PT_IS_DEFAULT_VALUE,
  PT_ATTRIBUTE,
  PT_METHOD,
  PT_FUNCTION_RENAME,
  PT_FILE_RENAME,
  PT_NO_ISOLATION_LEVEL,	/* value for uninitialized isolation level */
  PT_SERIALIZABLE,
  PT_REPEATABLE_READ,
  PT_READ_COMMITTED,
  PT_ISOLATION_LEVEL,		/* get transaction option */
  PT_LOCK_TIMEOUT,
  PT_HOST_IN,			/* kind of host variable */
  PT_HOST_OUT,
  PT_HOST_OUT_DESCR,
  PT_ACTIVE,			/* trigger status */
  PT_INACTIVE,
  PT_BEFORE,			/* trigger time */
  PT_AFTER,
  PT_DEFERRED,
  PT_REJECT,			/* trigger action */
  PT_INVALIDATE_XACTION,
  PT_PRINT,
  PT_EXPRESSION,
  PT_TRIGGER_TRACE,		/* trigger options */
  PT_TRIGGER_DEPTH,
  PT_IS_CALL_STMT,		/* is the method a call statement */
  PT_IS_MTHD_EXPR,		/* is the method call part of an expr */
  PT_IS_CLASS_MTHD,		/* is the method a class method */
  PT_IS_INST_MTHD,		/* is the method an instance method */
  PT_METHOD_ENTITY,		/* this entity arose from a method call */
  PT_IS_SELECTOR_SPEC,		/* This is the 'real' correspondant of the whacked spec. down in the path entities
				 * portion. */
  PT_PATH_INNER,		/* types of join which may emulate path */
  PT_PATH_OUTER,
  PT_PATH_OUTER_WEASEL,
  PT_LOCAL,			/* local or cascaded view check option */
  PT_CASCADED,
  PT_CURRENT,

  PT_CHAR_STRING,		/* denotes the flavor of a literal string */
  PT_BIT_STRING,
  PT_HEX_STRING,

  PT_MATCH_REGULAR,
  PT_MATCH_FULL,		/* values to support triggered actions for */
  PT_MATCH_PARTIAL,		/* referential integrity constraints */
  PT_RULE_CASCADE,
  PT_RULE_RESTRICT,
  PT_RULE_SET_NULL,
  PT_RULE_SET_DEFAULT,
  PT_RULE_NO_ACTION,

  PT_LEADING,			/* trim operation qualifiers */
  PT_TRAILING,
  PT_BOTH,
  PT_NOPUT,
  PT_INPUT,
  PT_OUTPUT,
  PT_INPUTOUTPUT,

  PT_MILLISECOND,		/* datetime components for extract operation */
  PT_SECOND,
  PT_MINUTE,
  PT_HOUR,
  PT_DAY,
  PT_WEEK,
  PT_MONTH,
  PT_QUARTER,
  PT_YEAR,
  /* mysql units types */
  PT_SECOND_MILLISECOND,
  PT_MINUTE_MILLISECOND,
  PT_MINUTE_SECOND,
  PT_HOUR_MILLISECOND,
  PT_HOUR_SECOND,
  PT_HOUR_MINUTE,
  PT_DAY_MILLISECOND,
  PT_DAY_SECOND,
  PT_DAY_MINUTE,
  PT_DAY_HOUR,
  PT_YEAR_MONTH,

  PT_SIMPLE_CASE,
  PT_SEARCHED_CASE,

  PT_OPT_LVL,			/* Variants of "get/set optimization" statement */
  PT_OPT_COST,

  PT_SUBSTR_ORG,
  PT_SUBSTR,			/* substring qualifier */

  PT_EQ_TORDER,

  PT_SP_PROCEDURE,
  PT_SP_FUNCTION,
  PT_SP_IN,
  PT_SP_OUT,
  PT_SP_INOUT,

  PT_LOB_INTERNAL,
  PT_LOB_EXTERNAL,

  PT_FROM_LAST,
  PT_IGNORE_NULLS,

  PT_NULLS_DEFAULT,
  PT_NULLS_FIRST,
  PT_NULLS_LAST,

  PT_CONSTRAINT_NAME,

  PT_TRACE_ON,
  PT_TRACE_OFF,
  PT_TRACE_FORMAT_TEXT,
  PT_TRACE_FORMAT_JSON,

  PT_IS_SHOWSTMT,		/* query is SHOWSTMT */
  PT_IS_CTE_REC_SUBQUERY,
  PT_IS_CTE_NON_REC_SUBQUERY,

  PT_DERIVED_JSON_TABLE,	// json table spec derivation

  PT_DERIVED_DBLINK_TABLE,	// dblink table spec derivation

  PT_PRIVATE,
  PT_PUBLIC,
  PT_SYNONYM,

  PT_AUTHID_OWNER,
  PT_AUTHID_CALLER,
  PT_NOT_DETERMINISTIC,
  PT_DETERMINISTIC
    // todo: separate into relevant enumerations
} PT_MISC_TYPE;

#endif /* _MISCTYPE_DEF_H_ */
