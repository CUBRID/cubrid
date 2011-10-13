/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * Predicate evaluation
 */

#ifndef _QUERY_EVALUATOR_H_
#define _QUERY_EVALUATOR_H_

#ident "$Id$"

#if defined(WINDOWS)
#include "porting.h"
#else /* ! WINDOWS */
#include <stdlib.h>
#endif /* ! WINDOWS */

#include "storage_common.h"
#include "dbtype.h"
#include "object_domain.h"
#include "string_opfunc.h"
#include "query_list.h"
#include "parser_support.h"
#include "thread.h"

typedef struct qproc_db_value_list *QPROC_DB_VALUE_LIST;	/* TODO */
struct qproc_db_value_list
{
  QPROC_DB_VALUE_LIST next;
  DB_VALUE *val;
};

typedef struct val_list_node VAL_LIST;	/* value list */
struct val_list_node
{
  QPROC_DB_VALUE_LIST valp;	/* first value node */
  int val_cnt;			/* value count */
};

typedef enum
{
/* aggregate functions */
  PT_MIN = 900, PT_MAX, PT_SUM, PT_AVG,
  PT_STDDEV, PT_VARIANCE,
  PT_STDDEV_POP, PT_VAR_POP,
  PT_STDDEV_SAMP, PT_VAR_SAMP,
  PT_COUNT, PT_COUNT_STAR,
  PT_GROUPBY_NUM,
  PT_AGG_BIT_AND, PT_AGG_BIT_OR, PT_AGG_BIT_XOR,
  PT_GROUP_CONCAT,
  PT_TOP_AGG_FUNC,
/* only aggregate functions should be below PT_TOP_AGG_FUNC */

/* foreign functions */
  PT_GENERIC,

/* from here down are function code common to parser and xasl */
/* "table" functions argument(s) are tables */
  F_TABLE_SET, F_TABLE_MULTISET, F_TABLE_SEQUENCE,
  F_TOP_TABLE_FUNC,
  F_MIDXKEY,

/* "normal" functions, arguments are values */
  F_SET, F_MULTISET, F_SEQUENCE, F_VID, F_GENERIC, F_CLASS_OF,
  F_INSERT_SUBSTRING, F_ELT
} FUNC_TYPE;

#define NUM_F_GENERIC_ARGS 32
#define NUM_F_INSERT_SUBSTRING_ARGS 4

/* type definitions for predicate evaluation */

typedef enum
{
  Q_DISTINCT,			/* no duplicate values */
  Q_ALL				/* all values */
} QUERY_OPTIONS;

typedef enum
{
  /* types used by both XASL interpreter and regulator */
  TYPE_DBVAL,			/* use dbval */
  TYPE_CONSTANT,		/* use varptr */
  TYPE_ORDERBY_NUM,		/* to be updated by orderby_num() in output
				   list; act same as TYPE_CONSTANT */
  TYPE_INARITH,			/* use arithptr */
  TYPE_OUTARITH,		/* use arithptr */
  TYPE_AGGREGATE,		/* use aggptr */
  TYPE_ATTR_ID,			/* use attr_descr */
  TYPE_CLASS_ATTR_ID,		/* use attr_descr */
  TYPE_SHARED_ATTR_ID,		/* use attr_descr */
  TYPE_POSITION,		/* use pos_descr */
  TYPE_LIST_ID,			/* use srlist_id */
  TYPE_POS_VALUE,		/* use val_pos for host variable references */
  TYPE_OID,			/* does not have corresponding field
				   use current object identifier value */
  TYPE_CLASSOID,		/* does not have corresponding field
				   use current class identifier value */
  TYPE_FUNC			/* use funcp */
} REGU_DATATYPE;

typedef struct regu_variable_node REGU_VARIABLE;
struct regu_variable_node
{
  REGU_DATATYPE type;
  int hidden_column;		/* whether the value gets to the list file */
  TP_DOMAIN *domain;		/* domain of the value in this regu variable */
  DB_VALUE *vfetch_to;		/* src db_value to fetch into in qp_fetchvlist */
  struct xasl_node *xasl;	/* query xasl pointer */
  union regu_data_value
  {
    /* fields used by both XASL interpreter and regulator */
    DB_VALUE dbval;		/* for DB_VALUE values */
    DB_VALUE *dbvalptr;		/* for constant values */
    struct arith_list_node *arithptr;	/* arithmetic expression */
    struct aggregate_list_node *aggptr;	/* aggregate expression */
    ATTR_DESCR attr_descr;	/* attribute information */
    QFILE_TUPLE_VALUE_POSITION pos_descr;	/* list file columns */
    QFILE_SORTED_LIST_ID *srlist_id;	/* sorted list identifier for subquery results */
    int val_pos;		/* host variable references */
    struct function_node *funcp;	/* function */
  } value;
};

#define REGU_VARIABLE_XASL(r)      ((r)->xasl)

typedef struct regu_variable_list_node *REGU_VARIABLE_LIST;	/* TODO */
struct regu_variable_list_node
{
  REGU_VARIABLE_LIST next;	/* Next node */
  REGU_VARIABLE value;		/* Regulator variable */
};

typedef struct valptr_list_node VALPTR_LIST;
typedef struct valptr_list_node OUTPTR_LIST;
struct valptr_list_node
{
  REGU_VARIABLE_LIST valptrp;	/* value pointer list */
  int valptr_cnt;		/* value count */
};

typedef struct regu_varlist_list_node *REGU_VARLIST_LIST;	/* TODO */
struct regu_varlist_list_node
{
  REGU_VARLIST_LIST next;	/* Next mode */
  REGU_VARIABLE_LIST list;	/* Pointer of regular variable list */
};

typedef struct regu_ptr_list_node *REGU_PTR_LIST;	/* TODO */
struct regu_ptr_list_node
{
  REGU_PTR_LIST next;		/* Next node */
  REGU_VARIABLE *var_p;		/* Regulator variable pointer */
};

typedef enum
{
  T_ADD,
  T_SUB,
  T_MUL,
  T_DIV,
  T_UNPLUS,
  T_UNMINUS,
  T_PRIOR,
  T_CONNECT_BY_ROOT,
  T_QPRIOR,
  T_BIT_NOT,
  T_BIT_AND,
  T_BIT_OR,
  T_BIT_XOR,
  T_BIT_COUNT,
  T_BITSHIFT_LEFT,
  T_BITSHIFT_RIGHT,
  T_INTDIV,
  T_INTMOD,
  T_IF,
  T_IFNULL,
  T_ISNULL,
  T_ACOS,
  T_ASIN,
  T_ATAN,
  T_ATAN2,
  T_COS,
  T_SIN,
  T_TAN,
  T_COT,
  T_PI,
  T_DEGREES,
  T_RADIANS,
  T_FORMAT,
  T_CONCAT,
  T_CONCAT_WS,
  T_FIELD,
  T_LEFT,
  T_RIGHT,
  T_REPEAT,
  T_SPACE,
  T_LOCATE,
  T_MID,
  T_STRCMP,
  T_REVERSE,
  T_LN,
  T_LOG2,
  T_LOG10,
  T_ADDDATE,
  T_DATE_ADD,
  T_SUBDATE,
  T_DATE_SUB,
  T_DATE_FORMAT,
  T_STR_TO_DATE,
  T_MOD,
  T_POSITION,
  T_SUBSTRING,
  T_SUBSTRING_INDEX,
  T_OCTET_LENGTH,
  T_BIT_LENGTH,
  T_CHAR_LENGTH,
  T_MD5,
  T_LOWER,
  T_UPPER,
  T_LIKE_LOWER_BOUND,
  T_LIKE_UPPER_BOUND,
  T_TRIM,
  T_LTRIM,
  T_RTRIM,
  T_LPAD,
  T_RPAD,
  T_REPLACE,
  T_TRANSLATE,
  T_ADD_MONTHS,
  T_LAST_DAY,
  T_MONTHS_BETWEEN,
  T_SYS_DATE,
  T_SYS_TIME,
  T_SYS_TIMESTAMP,
  T_UTC_TIME,
  T_UTC_DATE,
  T_TIME_FORMAT,
  T_TIMESTAMP,
  T_UNIX_TIMESTAMP,
  T_FROM_UNIXTIME,
  T_SYS_DATETIME,
  T_YEAR,
  T_MONTH,
  T_DAY,
  T_HOUR,
  T_MINUTE,
  T_SECOND,
  T_QUARTER,
  T_WEEKDAY,
  T_DAYOFWEEK,
  T_DAYOFYEAR,
  T_TODAYS,
  T_FROMDAYS,
  T_TIMETOSEC,
  T_SECTOTIME,
  T_MAKEDATE,
  T_MAKETIME,
  T_WEEK,
  T_TO_CHAR,
  T_TO_DATE,
  T_TO_TIME,
  T_TO_TIMESTAMP,
  T_TO_DATETIME,
  T_TO_NUMBER,
  T_CURRENT_VALUE,
  T_NEXT_VALUE,
  T_CAST,
  T_CAST_NOFAIL,
  T_CASE,
  T_EXTRACT,
  T_LOCAL_TRANSACTION_ID,
  T_FLOOR,
  T_CEIL,
  T_SIGN,
  T_POWER,
  T_ROUND,
  T_LOG,
  T_EXP,
  T_SQRT,
  T_TRUNC,
  T_ABS,
  T_CHR,
  T_INSTR,
  T_LEAST,
  T_GREATEST,
  T_STRCAT,
  T_NULLIF,
  T_COALESCE,
  T_NVL,
  T_NVL2,
  T_DECODE,
  T_RAND,
  T_DRAND,
  T_RANDOM,
  T_DRANDOM,
  T_INCR,
  T_DECR,
  T_SYS_CONNECT_BY_PATH,
  T_DATE,
  T_TIME,
  T_DATEDIFF,
  T_TIMEDIFF,
  T_ROW_COUNT,
  T_LAST_INSERT_ID,
  T_DEFAULT,
  T_LIST_DBS,
  T_BIT_TO_BLOB,
  T_BLOB_TO_BIT,
  T_CHAR_TO_CLOB,
  T_CLOB_TO_CHAR,
  T_LOB_LENGTH,
  T_TYPEOF,
  T_INDEX_CARDINALITY,
  T_EVALUATE_VARIABLE,
  T_DEFINE_VARIABLE,
  T_PREDICATE,
  T_EXEC_STATS,
  T_ADDTIME,
  T_BIN,
  T_FINDINSET
} OPERATOR_TYPE;		/* arithmetic operator types */

typedef struct pred_expr PRED_EXPR;

typedef struct arith_list_node ARITH_TYPE;
struct arith_list_node
{
  ARITH_TYPE *next;		/* next arithmetic expression */
  TP_DOMAIN *domain;		/* resultant domain */
  DB_VALUE *value;		/* value of the subtree */
  REGU_VARIABLE *leftptr;	/* left operand */
  REGU_VARIABLE *rightptr;	/* right operand */
  REGU_VARIABLE *thirdptr;	/* third operand */
  OPERATOR_TYPE opcode;		/* operator value */
  MISC_OPERAND misc_operand;	/* currently used for trim qualifier
				 * and datetime extract field specifier */
  PRED_EXPR *pred;		/* predicate expression */

  /* NOTE: The following member is only used on server internally. */
  struct drand48_data *rand_seed;	/* seed to be used to generate
					 * pseudo-random sequence */
};

typedef struct aggregate_list_node AGGREGATE_TYPE;
struct aggregate_list_node
{
  AGGREGATE_TYPE *next;		/* next aggregate node */
  TP_DOMAIN *domain;		/* domain of the result */
  DB_VALUE *value;		/* value of the aggregate */
  DB_VALUE *value2;		/* for GROUP_CONCAT, STTDEV and VARIANCE */
  int curr_cnt;			/* current number of items */
  FUNC_TYPE function;		/* aggregate function name */
  QUERY_OPTIONS option;		/* DISTINCT/ALL option */
  DB_TYPE opr_dbtype;		/* Operand values data type */
  struct regu_variable_node operand;	/* operand */
  QFILE_LIST_ID *list_id;	/* used for distinct handling */
  int flag_agg_optimize;
  BTID btid;
  SORT_LIST *sort_list;		/* for sorting elements before aggregation;
				 * used by GROUP_CONCAT */
};

typedef struct function_node FUNCTION_TYPE;
struct function_node
{
  DB_VALUE *value;		/* value of the function */
  REGU_VARIABLE_LIST operand;	/* operands */
  FUNC_TYPE ftype;		/* function to call */
};

/*
 * typedefs related to the predicate expression structure
 */

#ifdef V_FALSE
#undef V_FALSE
#endif
#ifdef V_TRUE
#undef V_TRUE
#endif

typedef enum
{ V_FALSE = 0, V_TRUE = 1, V_UNKNOWN = 2, V_ERROR = -1 } DB_LOGICAL;

typedef enum
{ B_AND = 1, B_OR,
  B_XOR, B_IS, B_IS_NOT
} BOOL_OP;

typedef enum
{ R_EQ = 1, R_NE, R_GT, R_GE, R_LT, R_LE, R_NULL, R_EXISTS, R_LIKE,
  R_EQ_SOME, R_NE_SOME, R_GT_SOME, R_GE_SOME, R_LT_SOME,
  R_LE_SOME, R_EQ_ALL, R_NE_ALL, R_GT_ALL, R_GE_ALL, R_LT_ALL,
  R_LE_ALL, R_SUBSET, R_SUPERSET, R_SUBSETEQ, R_SUPERSETEQ, R_EQ_TORDER,
  R_NULLSAFE_EQ
} REL_OP;

typedef enum
{ F_ALL = 1, F_SOME } QL_FLAG;

typedef enum
{ T_PRED = 1, T_EVAL_TERM, T_NOT_TERM } TYPE_PRED_EXPR;

typedef enum
{ T_COMP_EVAL_TERM = 1, T_ALSM_EVAL_TERM, T_LIKE_EVAL_TERM } TYPE_EVAL_TERM;

typedef struct comp_eval_term COMP_EVAL_TERM;
struct comp_eval_term
{
  REGU_VARIABLE *lhs;
  REGU_VARIABLE *rhs;
  REL_OP rel_op;
  DB_TYPE type;
};

typedef struct alsm_eval_term ALSM_EVAL_TERM;
struct alsm_eval_term
{
  REGU_VARIABLE *elem;
  REGU_VARIABLE *elemset;
  QL_FLAG eq_flag;
  REL_OP rel_op;
  DB_TYPE item_type;
};

typedef struct like_eval_term LIKE_EVAL_TERM;
struct like_eval_term
{
  REGU_VARIABLE *src;
  REGU_VARIABLE *pattern;
  REGU_VARIABLE *esc_char;
};

typedef struct eval_term EVAL_TERM;
struct eval_term
{
  TYPE_EVAL_TERM et_type;
  union
  {
    COMP_EVAL_TERM et_comp;
    ALSM_EVAL_TERM et_alsm;
    LIKE_EVAL_TERM et_like;
  } et;
};

typedef struct pred PRED;
struct pred
{
  PRED_EXPR *lhs;
  PRED_EXPR *rhs;
  BOOL_OP bool_op;
};

struct pred_expr
{
  union
  {
    PRED pred;
    EVAL_TERM eval_term;
    PRED_EXPR *not_term;
  } pe;
  TYPE_PRED_EXPR type;
};

typedef DB_LOGICAL (*PR_EVAL_FNC) (THREAD_ENTRY * thread_p, PRED_EXPR *,
				   VAL_DESCR *, OID *);

/* predicates information of scan */
typedef struct scan_pred SCAN_PRED;
struct scan_pred
{
  REGU_VARIABLE_LIST regu_list;	/* regu list for predicates (or filters) */
  PRED_EXPR *pred_expr;		/* predicate expressions */
  PR_EVAL_FNC pr_eval_fnc;	/* predicate evaluation function */
};

/* attributes information of scan */
typedef struct scan_attrs SCAN_ATTRS;
struct scan_attrs
{
  ATTR_ID *attr_ids;		/* array of attributes id */
  HEAP_CACHE_ATTRINFO *attr_cache;	/* attributes access cache */
  int num_attrs;		/* number of attributes */
};

/* informations that are need for applying filter (predicate) */
typedef struct filter_info FILTER_INFO;
struct filter_info
{
  /* filter information */
  SCAN_PRED *scan_pred;		/* predicates of the filter */
  SCAN_ATTRS *scan_attrs;	/* attributes scanning info */
  VAL_LIST *val_list;		/* value list */
  VAL_DESCR *val_descr;		/* value descriptor */

  /* class information */
  OID *class_oid;		/* class OID */

  /* index information */
  ATTR_ID *btree_attr_ids;	/* attribute id array of the index key */
  int *num_vstr_ptr;		/* number pointer of variable string attrs */
  ATTR_ID *vstr_ids;		/* attribute id array of variable string */
  int btree_num_attrs;		/* number of attributes of the index key */
};

/* pseudocolumns offsets in tuple (from end) */
#define	PCOL_ISCYCLE_TUPLE_OFFSET	1
#define	PCOL_ISLEAF_TUPLE_OFFSET	2
#define	PCOL_LEVEL_TUPLE_OFFSET		3
#define	PCOL_INDEX_STRING_TUPLE_OFFSET	4
#define	PCOL_PARENTPOS_TUPLE_OFFSET	5
#define	PCOL_FIRST_TUPLE_OFFSET		PCOL_PARENTPOS_TUPLE_OFFSET

extern DB_LOGICAL eval_pred (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
			     VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp0 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp1 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp2 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp3 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm4 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm5 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_like6 (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
				   VAL_DESCR * vd, OID * obj_oid);
extern PR_EVAL_FNC eval_fnc (THREAD_ENTRY * thread_p, PRED_EXPR * pr,
			     DB_TYPE * single_node_type);
extern DB_LOGICAL eval_data_filter (THREAD_ENTRY * thread_p, OID * oid,
				    RECDES * recdes, FILTER_INFO * filter);
extern DB_LOGICAL eval_key_filter (THREAD_ENTRY * thread_p, DB_VALUE * value,
				   FILTER_INFO * filter);

#endif /* _QUERY_EVALUATOR_H_ */
