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
#include "regex38a.h"
#include "binaryheap.h"

typedef struct qproc_db_value_list *QPROC_DB_VALUE_LIST;	/* TODO */
struct qproc_db_value_list
{
  QPROC_DB_VALUE_LIST next;
  DB_VALUE *val;
  TP_DOMAIN *dom;
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
  PT_ROW_NUMBER,
  PT_RANK,
  PT_DENSE_RANK,
  PT_NTILE,
  PT_TOP_AGG_FUNC,
/* only aggregate functions should be below PT_TOP_AGG_FUNC */

/* analytic only functions */
  PT_LEAD, PT_LAG,

/* foreign functions */
  PT_GENERIC,

/* from here down are function code common to parser and xasl */
/* "table" functions argument(s) are tables */
  F_TABLE_SET, F_TABLE_MULTISET, F_TABLE_SEQUENCE,
  F_TOP_TABLE_FUNC,
  F_MIDXKEY,

/* "normal" functions, arguments are values */
  F_SET, F_MULTISET, F_SEQUENCE, F_VID, F_GENERIC, F_CLASS_OF,
  F_INSERT_SUBSTRING, F_ELT,

/* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
  PT_FIRST_VALUE, PT_LAST_VALUE, PT_NTH_VALUE,
  /* aggregate and analytic functions */
  PT_MEDIAN,
  PT_CUME_DIST,
  PT_PERCENT_RANK,
  PT_PERCENTILE_CONT,
  PT_PERCENTILE_DISC
} FUNC_TYPE;

typedef enum
{
  SHOWSTMT_START = 0,
  SHOWSTMT_NULL = SHOWSTMT_START,
  SHOWSTMT_ACCESS_STATUS,
  SHOWSTMT_VOLUME_HEADER,
  SHOWSTMT_ACTIVE_LOG_HEADER,
  SHOWSTMT_ARCHIVE_LOG_HEADER,
  SHOWSTMT_SLOTTED_PAGE_HEADER,
  SHOWSTMT_SLOTTED_PAGE_SLOTS,
  SHOWSTMT_HEAP_HEADER,
  SHOWSTMT_ALL_HEAP_HEADER,
  SHOWSTMT_HEAP_CAPACITY,
  SHOWSTMT_ALL_HEAP_CAPACITY,
  SHOWSTMT_INDEX_HEADER,
  SHOWSTMT_INDEX_CAPACITY,
  SHOWSTMT_ALL_INDEXES_HEADER,
  SHOWSTMT_ALL_INDEXES_CAPACITY,
  SHOWSTMT_GLOBAL_CRITICAL_SECTIONS,
  SHOWSTMT_JOB_QUEUES,
  SHOWSTMT_TIMEZONES,
  SHOWSTMT_FULL_TIMEZONES,
  SHOWSTMT_TRAN_TABLES,
  SHOWSTMT_THREADS,

  /* append the new show statement types in here */

  SHOWSTMT_END
} SHOWSTMT_TYPE;

#define QPROC_IS_INTERPOLATION_FUNC(func_p) \
  (((func_p)->function == PT_MEDIAN) \
   || ((func_p)->function == PT_PERCENTILE_CONT) \
   || ((func_p)->function == PT_PERCENTILE_DISC))

typedef enum
{
  KILLSTMT_TRAN = 0,
  KILLSTMT_QUERY = 1,
} KILLSTMT_TYPE;

#define QPROC_ANALYTIC_IS_OFFSET_FUNCTION(func_p) \
    (((func_p) != NULL) \
    && (((func_p)->function == PT_LEAD) \
        || ((func_p)->function == PT_LAG) \
        || ((func_p)->function == PT_NTH_VALUE)))

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
  TYPE_ORDERBY_NUM,		/* to be updated by orderby_num() in output list; act same as TYPE_CONSTANT */
  TYPE_INARITH,			/* use arithptr */
  TYPE_OUTARITH,		/* use arithptr */
  TYPE_ATTR_ID,			/* use attr_descr */
  TYPE_CLASS_ATTR_ID,		/* use attr_descr */
  TYPE_SHARED_ATTR_ID,		/* use attr_descr */
  TYPE_POSITION,		/* use pos_descr */
  TYPE_LIST_ID,			/* use srlist_id */
  TYPE_POS_VALUE,		/* use val_pos for host variable references */
  TYPE_OID,			/* does not have corresponding field use current object identifier value */
  TYPE_CLASSOID,		/* does not have corresponding field use current class identifier value */
  TYPE_FUNC,			/* use funcp */
  TYPE_REGUVAL_LIST,		/* use reguval_list */
  TYPE_REGU_VAR_LIST		/* use regu_variable_list for 'CUME_DIST' and 'PERCENT_RANK' */
} REGU_DATATYPE;

typedef struct regu_value_list REGU_VALUE_LIST;

typedef struct regu_variable_node REGU_VARIABLE;
struct regu_variable_node
{
  REGU_DATATYPE type;

  /* regu variable flags */
#define REGU_VARIABLE_HIDDEN_COLUMN		0x01	/* does not go to list file */
#define REGU_VARIABLE_FIELD_COMPARE		0x02	/* for FIELD function, marks the bottom of regu tree */
#define REGU_VARIABLE_FIELD_NESTED		0x04	/* for FIELD function, reguvar is child in T_FIELD tree */
#define REGU_VARIABLE_APPLY_COLLATION		0x08	/* Apply collation from domain; flag used in context of COLLATE
							 * modifier */
#define REGU_VARIABLE_ANALYTIC_WINDOW		0x10	/* for analytic window func */
#define REGU_VARIABLE_INFER_COLLATION		0x20	/* infer collation for default parameter */
#define REGU_VARIABLE_FETCH_ALL_CONST		0x40	/* is all constant */
#define REGU_VARIABLE_FETCH_NOT_CONST		0x80	/* is not constant */
#define REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE   0x100	/* clears regu variable at clone decache */
  int flags;			/* flags */
#define REGU_VARIABLE_IS_FLAGED(e, f)    ((e)->flags & (short) (f))
#define REGU_VARIABLE_SET_FLAG(e, f)     (e)->flags |= (short) (f)
#define REGU_VARIABLE_CLEAR_FLAG(e, f)   (e)->flags &= (short) ~(f)

  TP_DOMAIN *domain;		/* domain of the value in this regu variable */
  TP_DOMAIN *original_domain;	/* original domain, used at execution in case of XASL clones */
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
    REGU_VALUE_LIST *reguval_list;	/* for "values" query */
    struct regu_variable_list_node *regu_var_list;	/* for CUME_DIST and PERCENT_RANK */
  } value;
};

typedef struct regu_value_item REGU_VALUE_ITEM;
struct regu_value_item
{
  REGU_VARIABLE *value;		/* REGU_VARIABLE */
  REGU_VALUE_ITEM *next;	/* next item */
};

struct regu_value_list
{
  REGU_VALUE_ITEM *regu_list;	/* list head */
  REGU_VALUE_ITEM *current_value;	/* current used item */
  int count;
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
  T_DISK_SIZE,
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
  T_FINDINSET,
  T_HEX,
  T_ASCII,
  T_CONV,
  T_INET_ATON,
  T_INET_NTOA,
  T_TO_ENUMERATION_VALUE,
  T_CHARSET,
  T_COLLATION,
  T_WIDTH_BUCKET,
  T_TRACE_STATS,
  T_AES_ENCRYPT,
  T_AES_DECRYPT,
  T_SHA_ONE,
  T_SHA_TWO,
  T_INDEX_PREFIX,
  T_TO_BASE64,
  T_FROM_BASE64,
  T_SYS_GUID,
  T_SLEEP,
  T_DBTIMEZONE,
  T_SESSIONTIMEZONE,
  T_TZ_OFFSET,
  T_NEW_TIME,
  T_FROM_TZ,
  T_TO_DATETIME_TZ,
  T_TO_TIMESTAMP_TZ,
  T_TO_TIME_TZ,
  T_UTC_TIMESTAMP,
  T_CRC32,
  T_CURRENT_DATETIME,
  T_CURRENT_TIMESTAMP,
  T_CURRENT_DATE,
  T_CURRENT_TIME,
} OPERATOR_TYPE;		/* arithmetic operator types */

typedef struct pred_expr PRED_EXPR;

typedef struct arith_list_node ARITH_TYPE;
struct arith_list_node
{
  ARITH_TYPE *next;		/* next arithmetic expression */
  TP_DOMAIN *domain;		/* resultant domain */
  TP_DOMAIN *original_domain;	/* original resultant domain, used at execution in case of XASL clones  */
  DB_VALUE *value;		/* value of the subtree */
  REGU_VARIABLE *leftptr;	/* left operand */
  REGU_VARIABLE *rightptr;	/* right operand */
  REGU_VARIABLE *thirdptr;	/* third operand */
  OPERATOR_TYPE opcode;		/* operator value */
  MISC_OPERAND misc_operand;	/* currently used for trim qualifier and datetime extract field specifier */
  PRED_EXPR *pred;		/* predicate expression */

  /* NOTE: The following member is only used on server internally. */
  struct drand48_data *rand_seed;	/* seed to be used to generate pseudo-random sequence */
};

typedef struct aggregate_accumulator AGGREGATE_ACCUMULATOR;
struct aggregate_accumulator
{
  DB_VALUE *value;		/* value of the aggregate */
  DB_VALUE *value2;		/* for GROUP_CONCAT, STTDEV and VARIANCE */
  int curr_cnt;			/* current number of items */
  bool clear_value_at_clone_decache;	/* true, if need to clear value at clone decache */
  bool clear_value2_at_clone_decache;	/* true, if need to clear value2 at clone decache */
};

typedef struct aggregate_accumulator_domain AGGREGATE_ACCUMULATOR_DOMAIN;
struct aggregate_accumulator_domain
{
  TP_DOMAIN *value_dom;		/* domain of value */
  TP_DOMAIN *value2_dom;	/* domain of value2 */
};

typedef struct aggregate_dist_percent_info AGGREGATE_DIST_PERCENT_INFO;
struct aggregate_dist_percent_info
{
  DB_VALUE **const_array;
  int list_len;
  int nlargers;
};

typedef struct aggregate_percentile_info AGGREGATE_PERCENTILE_INFO;
struct aggregate_percentile_info
{
  double cur_group_percentile;	/* current percentile value */
  REGU_VARIABLE *percentile_reguvar;
};

typedef union aggregate_specific_function_info AGGREGATE_SPECIFIC_FUNCTION_INFO;
union aggregate_specific_function_info
{
  AGGREGATE_DIST_PERCENT_INFO dist_percent;	/* CUME_DIST and PERCENT_RANK */
  AGGREGATE_PERCENTILE_INFO percentile;	/* PERCENTILE_CONT and PERCENTILE_DISC */
};

typedef struct aggregate_list_node AGGREGATE_TYPE;
struct aggregate_list_node
{
  AGGREGATE_TYPE *next;		/* next aggregate node */
  TP_DOMAIN *domain;		/* domain of the result */
  TP_DOMAIN *original_domain;	/* original domain of the result */
  FUNC_TYPE function;		/* aggregate function name */
  QUERY_OPTIONS option;		/* DISTINCT/ALL option */
  DB_TYPE opr_dbtype;		/* Operand values data type */
  DB_TYPE original_opr_dbtype;	/* Original operand values data type */
  struct regu_variable_node operand;	/* operand */
  QFILE_LIST_ID *list_id;	/* used for distinct handling */
  int flag_agg_optimize;
  BTID btid;
  SORT_LIST *sort_list;		/* for sorting elements before aggregation; used by GROUP_CONCAT */
  AGGREGATE_SPECIFIC_FUNCTION_INFO info;	/* variables for specific functions */
  AGGREGATE_ACCUMULATOR accumulator;	/* holds runtime values, only for evaluation */
  AGGREGATE_ACCUMULATOR_DOMAIN accumulator_domain;	/* holds domain info on accumulator */
};

/* aggregate evaluation hash key */
typedef struct aggregate_hash_key AGGREGATE_HASH_KEY;
struct aggregate_hash_key
{
  int val_count;		/* key size */
  bool free_values;		/* true if values need to be freed */
  DB_VALUE **values;		/* value array */
};

/* aggregate evaluation hash value */
typedef struct aggregate_hash_value AGGREGATE_HASH_VALUE;
struct aggregate_hash_value
{
  int curr_size;		/* last computed size of structure */
  int tuple_count;		/* # of tuples aggregated in structure */
  int func_count;		/* # of functions (i.e. accumulators) */
  AGGREGATE_ACCUMULATOR *accumulators;	/* function accumulators */
  QFILE_TUPLE_RECORD first_tuple;	/* first aggregated tuple */
};

typedef struct analytic_ntile_function_info ANALYTIC_NTILE_FUNCTION_INFO;
struct analytic_ntile_function_info
{
  bool is_null;			/* is function result NULL? */
  int bucket_count;		/* number of required buckets */
};

typedef struct analytic_cume_percent_function_info ANALYTIC_CUME_PERCENT_FUNCTION_INFO;
struct analytic_cume_percent_function_info
{
  int last_pos;			/* record the current position of the rows that are no larger than the current row */
  double last_res;		/* record the last result */
};

typedef struct analytic_percentile_function_info ANALYTIC_PERCENTILE_FUNCTION_INFO;
struct analytic_percentile_function_info
{
  double cur_group_percentile;	/* current percentile value */
  REGU_VARIABLE *percentile_reguvar;	/* percentile value of the new tuple if this is not the same as
					 * cur_gourp_percentile, an error is raised. */
};

typedef union analytic_function_info ANALYTIC_FUNCTION_INFO;
union analytic_function_info
{
  ANALYTIC_NTILE_FUNCTION_INFO ntile;
  ANALYTIC_PERCENTILE_FUNCTION_INFO percentile;
  ANALYTIC_CUME_PERCENT_FUNCTION_INFO cume_percent;
};

typedef struct analytic_list_node ANALYTIC_TYPE;
struct analytic_list_node
{
  ANALYTIC_TYPE *next;		/* next analytic node */

  /* constant fields, XASL serialized */
  FUNC_TYPE function;		/* analytic function type */
  QUERY_OPTIONS option;		/* DISTINCT/ALL option */
  TP_DOMAIN *domain;		/* domain of the result */
  TP_DOMAIN *original_domain;	/* domain of the result */

  DB_TYPE opr_dbtype;		/* operand data type */
  DB_TYPE original_opr_dbtype;	/* original operand data type */
  REGU_VARIABLE operand;	/* operand */

  int flag;			/* flags */
  int sort_prefix_size;		/* number of PARTITION BY cols in sort list */
  int sort_list_size;		/* the total size of the sort list */
  int offset_idx;		/* index of offset value in select list (for LEAD/LAG/NTH_value functions) */
  int default_idx;		/* index of default value in select list (for LEAD/LAG functions) */
  bool from_last;		/* begin at the last or first row */
  bool ignore_nulls;		/* ignore or respect NULL values */
  bool is_const_operand;	/* is the operand a constant or a host var for MEDIAN function */

  /* runtime values */
  ANALYTIC_FUNCTION_INFO info;	/* custom function runtime values */
  QFILE_LIST_ID *list_id;	/* used for distinct handling */
  DB_VALUE *value;		/* value of the aggregate */
  DB_VALUE *value2;		/* for STTDEV and VARIANCE */
  DB_VALUE *out_value;		/* DB_VALUE used for output */
  DB_VALUE part_value;		/* partition temporary accumulator */
  int curr_cnt;			/* current number of items */
  bool is_first_exec_time;	/* the fist time to be executed */
};

#define ANALYTIC_ADVANCE_RANK 1	/* advance rank */
#define ANALYTIC_KEEP_RANK    2	/* keep current rank */

#define ANALYTIC_FUNC_IS_FLAGED(x, f)        ((x)->flag & (int) (f))
#define ANALYTIC_FUNC_SET_FLAG(x, f)         (x)->flag |= (int) (f)
#define ANALYTIC_FUNC_CLEAR_FLAG(x, f)       (x)->flag &= (int) ~(f)

typedef struct analytic_eval_type ANALYTIC_EVAL_TYPE;
struct analytic_eval_type
{
  ANALYTIC_EVAL_TYPE *next;	/* next eval group */
  ANALYTIC_TYPE *head;		/* analytic type list */
  SORT_LIST *sort_list;		/* partition sort */
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
{ T_COMP_EVAL_TERM = 1, T_ALSM_EVAL_TERM, T_LIKE_EVAL_TERM, T_RLIKE_EVAL_TERM
} TYPE_EVAL_TERM;

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

typedef struct rlike_eval_term RLIKE_EVAL_TERM;
struct rlike_eval_term
{
  REGU_VARIABLE *src;
  REGU_VARIABLE *pattern;
  REGU_VARIABLE *case_sensitive;
  cub_regex_t *compiled_regex;
  char *compiled_pattern;
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
    RLIKE_EVAL_TERM et_rlike;
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

typedef DB_LOGICAL (*PR_EVAL_FNC) (THREAD_ENTRY * thread_p, PRED_EXPR *, VAL_DESCR *, OID *);

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
  int func_idx_col_id;		/* function expression column position, if this is a function index */
};

/* pseudocolumns offsets in tuple (from end) */
#define	PCOL_ISCYCLE_TUPLE_OFFSET	1
#define	PCOL_ISLEAF_TUPLE_OFFSET	2
#define	PCOL_LEVEL_TUPLE_OFFSET		3
#define	PCOL_INDEX_STRING_TUPLE_OFFSET	4
#define	PCOL_PARENTPOS_TUPLE_OFFSET	5
#define	PCOL_FIRST_TUPLE_OFFSET		PCOL_PARENTPOS_TUPLE_OFFSET

extern DB_LOGICAL eval_limit_count_is_0 (THREAD_ENTRY * thread_p, REGU_VARIABLE * rv, VAL_DESCR * vd);
extern DB_LOGICAL eval_pred (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp0 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp1 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp2 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_comp3 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm4 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_alsm5 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_like6 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern DB_LOGICAL eval_pred_rlike7 (THREAD_ENTRY * thread_p, PRED_EXPR * pr, VAL_DESCR * vd, OID * obj_oid);
extern PR_EVAL_FNC eval_fnc (THREAD_ENTRY * thread_p, PRED_EXPR * pr, DB_TYPE * single_node_type);
extern DB_LOGICAL eval_data_filter (THREAD_ENTRY * thread_p, OID * oid, RECDES * recdes, HEAP_SCANCACHE * scan_cache,
				    FILTER_INFO * filter);
extern DB_LOGICAL eval_key_filter (THREAD_ENTRY * thread_p, DB_VALUE * value, FILTER_INFO * filter);
extern DB_LOGICAL update_logical_result (THREAD_ENTRY * thread_p, DB_LOGICAL ev_res, int *qualification,
					 FILTER_INFO * key_filter, RECDES * recdes, const OID * oid);

#endif /* _QUERY_EVALUATOR_H_ */
