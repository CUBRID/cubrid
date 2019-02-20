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
 * Regu variable - I don't even know what regu stands for.
 */

#ifndef _REGU_VAR_H_
#define _REGU_VAR_H_

#include "heap_attrinfo.h"
#include "object_domain.h"
#include "query_list.h"
#include "libregex38a/regex38a.h"
#include "string_opfunc.h"

// *INDENT-OFF*

// forward definitions
struct xasl_node;
namespace cubxasl
{
  struct aggregate_list_node;
} // namespace cubxasl

#define REGU_VARIABLE_XASL(r)      ((r)->xasl)

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

/* declare ahead REGU_VARIABLE */
typedef struct regu_variable_node REGU_VARIABLE;
typedef struct regu_variable_list_node *REGU_VARIABLE_LIST;	/* TODO */

/*
 *       	         CATALOG STRUCTURES
 */

typedef int CL_ATTR_ID;

typedef struct attr_descr_node ATTR_DESCR;
struct attr_descr_node
{
  CL_ATTR_ID id;		/* Attribute identifier */
  DB_TYPE type;			/* Attribute data type */
  HEAP_CACHE_ATTRINFO *cache_attrinfo;	/* used to cache catalog info */
  DB_VALUE *cache_dbvalp;	/* cached value for particular attr */
  /* in cache_attrinfo */
};				/* Attribute Descriptor */

#define UT_CLEAR_ATTR_DESCR(ptr) \
  do \
    { \
      (ptr)->id = -1; \
      (ptr)->type = DB_TYPE_NULL; \
      (ptr)->cache_dbvalp = NULL; \
    } \
  while (0)

#if defined (SERVER_MODE) || defined (SA_MODE)
/*
 *       	   ESQL POSITIONAL VALUES RELATED TYPEDEFS
 */
typedef struct xasl_state XASL_STATE;

typedef struct val_descr VAL_DESCR;
struct val_descr
{
  DB_VALUE *dbval_ptr;		/* Array of values */
  int dbval_cnt;		/* Value Count */
  DB_DATETIME sys_datetime;
  DB_TIMESTAMP sys_epochtime;
  long lrand;
  double drand;
  XASL_STATE *xasl_state;	/* XASL_STATE pointer */
};				/* Value Descriptor */

struct xasl_state
{
  VAL_DESCR vd;			/* Value Descriptor */
  QUERY_ID query_id;		/* Query associated with XASL */
  int qp_xasl_line;		/* Error line */
};
#endif /* defined (SERVER_MODE) || defined (SA_MODE) */

/************************************************************************/
/* Predicate                                                            */
/************************************************************************/

/* PRED */
typedef struct pred_expr PRED_EXPR;
typedef struct pred PRED;
typedef struct eval_term EVAL_TERM;

typedef enum
{
  T_PRED = 1,
  T_EVAL_TERM,
  T_NOT_TERM
} TYPE_PRED_EXPR;

typedef enum
{
  B_AND = 1,
  B_OR,
  B_XOR,
  B_IS,
  B_IS_NOT
} BOOL_OP;

struct pred
{
  PRED_EXPR *lhs;
  PRED_EXPR *rhs;
  BOOL_OP bool_op;
};

typedef enum
{
  T_COMP_EVAL_TERM = 1,
  T_ALSM_EVAL_TERM,
  T_LIKE_EVAL_TERM,
  T_RLIKE_EVAL_TERM
} TYPE_EVAL_TERM;

typedef enum
{
  R_NONE = 0,
  R_EQ = 1,
  R_NE,
  R_GT,
  R_GE,
  R_LT,
  R_LE,
  R_NULL,
  R_EXISTS,
  R_LIKE,
  R_EQ_SOME,
  R_NE_SOME,
  R_GT_SOME,
  R_GE_SOME,
  R_LT_SOME,
  R_LE_SOME,
  R_EQ_ALL,
  R_NE_ALL,
  R_GT_ALL,
  R_GE_ALL,
  R_LT_ALL,
  R_LE_ALL,
  R_SUBSET,
  R_SUPERSET,
  R_SUBSETEQ,
  R_SUPERSETEQ,
  R_EQ_TORDER,
  R_NULLSAFE_EQ
} REL_OP;

typedef enum
{
  F_ALL = 1,
  F_SOME
} QL_FLAG;

struct comp_eval_term
{
  REGU_VARIABLE *lhs;
  REGU_VARIABLE *rhs;
  REL_OP rel_op;
  DB_TYPE type;
};
typedef struct comp_eval_term COMP_EVAL_TERM;

struct alsm_eval_term
{
  REGU_VARIABLE *elem;
  REGU_VARIABLE *elemset;
  QL_FLAG eq_flag;
  REL_OP rel_op;
  DB_TYPE item_type;
};
typedef struct alsm_eval_term ALSM_EVAL_TERM;

struct like_eval_term
{
  REGU_VARIABLE *src;
  REGU_VARIABLE *pattern;
  REGU_VARIABLE *esc_char;
};
typedef struct like_eval_term LIKE_EVAL_TERM;

struct rlike_eval_term
{
  REGU_VARIABLE *src;
  REGU_VARIABLE *pattern;
  REGU_VARIABLE *case_sensitive;
  mutable cub_regex_t *compiled_regex;
  mutable char *compiled_pattern;
};
typedef struct rlike_eval_term RLIKE_EVAL_TERM;

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

/************************************************************************/
/* Regu stuff                                                           */
/************************************************************************/
typedef struct regu_value_item REGU_VALUE_ITEM;
struct regu_value_item
{
  REGU_VARIABLE *value;		/* REGU_VARIABLE */
  REGU_VALUE_ITEM *next;	/* next item */

  regu_value_item () = default;
};

typedef struct regu_value_list REGU_VALUE_LIST;
struct regu_value_list
{
  REGU_VALUE_ITEM *regu_list;	/* list head */
  REGU_VALUE_ITEM *current_value;	/* current used item */
  int count;

  regu_value_list () = default;
};

typedef struct valptr_list_node VALPTR_LIST;
typedef struct valptr_list_node OUTPTR_LIST;
struct valptr_list_node
{
  REGU_VARIABLE_LIST valptrp;	/* value pointer list */
  int valptr_cnt;		/* value count */

  valptr_list_node () = default;
};

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

typedef struct function_node FUNCTION_TYPE;
struct function_node
{
  DB_VALUE *value;		/* value of the function */
  REGU_VARIABLE_LIST operand;	/* operands */
  FUNC_TYPE ftype;		/* function to call */
};

/* regular variable flags */
const int REGU_VARIABLE_HIDDEN_COLUMN = 0x01;	/* does not go to list file */
const int REGU_VARIABLE_FIELD_COMPARE = 0x02;	/* for FIELD function, marks the bottom of regu tree */
const int REGU_VARIABLE_FIELD_NESTED = 0x04;	/* for FIELD function, reguvar is child in T_FIELD tree */
const int REGU_VARIABLE_APPLY_COLLATION = 0x08;	/* Apply collation from domain; flag used in context of COLLATE
						 * modifier */
const int REGU_VARIABLE_ANALYTIC_WINDOW = 0x10;	/* for analytic window func */
const int REGU_VARIABLE_INFER_COLLATION = 0x20;	/* infer collation for default parameter */
const int REGU_VARIABLE_FETCH_ALL_CONST = 0x40;	/* is all constant */
const int REGU_VARIABLE_FETCH_NOT_CONST = 0x80;	/* is not constant */
const int REGU_VARIABLE_CLEAR_AT_CLONE_DECACHE = 0x100;	/* clears regu variable at clone decache */

struct regu_variable_node
{
  REGU_DATATYPE type;

  int flags;			/* flags */



  TP_DOMAIN *domain;		/* domain of the value in this regu variable */
  TP_DOMAIN *original_domain;	/* original domain, used at execution in case of XASL clones */
  DB_VALUE *vfetch_to;		/* src db_value to fetch into in qp_fetchvlist */
  xasl_node *xasl;		/* query xasl pointer */
  union regu_data_value
  {
    /* fields used by both XASL interpreter and regulator */
    DB_VALUE dbval;		/* for DB_VALUE values */
    DB_VALUE *dbvalptr;		/* for constant values */
    ARITH_TYPE *arithptr;	/* arithmetic expression */
    cubxasl::aggregate_list_node *aggptr;	/* aggregate expression */
    ATTR_DESCR attr_descr;	/* attribute information */
    QFILE_TUPLE_VALUE_POSITION pos_descr;	/* list file columns */
    QFILE_SORTED_LIST_ID *srlist_id;	/* sorted list identifier for subquery results */
    int val_pos;		/* host variable references */
    struct function_node *funcp;	/* function */
    REGU_VALUE_LIST *reguval_list;	/* for "values" query */
    REGU_VARIABLE_LIST regu_var_list;	/* for CUME_DIST and PERCENT_RANK */
  } value;

  regu_variable_node () = default;
};

struct regu_variable_list_node
{
  REGU_VARIABLE_LIST next;	/* Next node */
  REGU_VARIABLE value;		/* Regulator variable */
};

typedef struct regu_varlist_list_node *REGU_VARLIST_LIST;	/* TODO */
struct regu_varlist_list_node
{
  REGU_VARLIST_LIST next;	/* Next mode */
  REGU_VARIABLE_LIST list;	/* Pointer of regular variable list */

  regu_varlist_list_node () = default;
};

typedef struct regu_ptr_list_node *REGU_PTR_LIST;	/* TODO */
struct regu_ptr_list_node
{
  REGU_PTR_LIST next;		/* Next node */
  REGU_VARIABLE *var_p;		/* Regulator variable pointer */
};

typedef struct qproc_db_value_list *QPROC_DB_VALUE_LIST;	/* TODO */
struct qproc_db_value_list
{
  QPROC_DB_VALUE_LIST next;
  DB_VALUE *val;
  TP_DOMAIN *dom;

  qproc_db_value_list () = default;
};

typedef struct val_list_node VAL_LIST;	/* value list */
struct val_list_node
{
  QPROC_DB_VALUE_LIST valp;	/* first value node */
  int val_cnt;			/* value count */

  val_list_node () = default;
};

typedef struct key_val_range KEY_VAL_RANGE;
struct key_val_range
{
  RANGE range;
  DB_VALUE key1;
  DB_VALUE key2;
  bool is_truncated;
  int num_index_term;		/* #terms associated with index key range */
};

typedef struct key_range KEY_RANGE;
struct key_range
{
  REGU_VARIABLE *key1;		/* pointer to first key value */
  REGU_VARIABLE *key2;		/* pointer to second key value */
  RANGE range;			/* range spec; GE_LE, GT_LE, GE_LT, GT_LT, GE_INF, GT_INF, INF_LT, INF_LE, INF_INF */
};				/* key range structure */

typedef struct key_info KEY_INFO;
struct key_info
{
  KEY_RANGE *key_ranges;	/* a list of key ranges */
  int key_cnt;			/* key count */
  bool is_constant;		/* every key value is a constant */
  bool key_limit_reset;		/* should key limit reset at each range */
  bool is_user_given_keylimit;	/* true if user specifies key limit */
  REGU_VARIABLE *key_limit_l;	/* lower key limit */
  REGU_VARIABLE *key_limit_u;	/* upper key limit */
};				/* key information structure */

typedef struct indx_info INDX_INFO;
struct indx_info
{
  BTID btid;			/* index identifier */
  int coverage;			/* index coverage state */
  OID class_oid;
  RANGE_TYPE range_type;	/* range type */
  KEY_INFO key_info;		/* key information */
  int orderby_desc;		/* first column of the order by is desc */
  int groupby_desc;		/* first column of the group by is desc */
  int use_desc_index;		/* using descending index */
  int orderby_skip;		/* order by skip information */
  int groupby_skip;		/* group by skip information */
  int use_iss;			/* flag set if using index skip scan */
  int func_idx_col_id;		/* function expression column position, if the index is a function index */
  KEY_RANGE iss_range;		/* placeholder range used for ISS; must be created on the broker */
  int ils_prefix_len;		/* index loose scan prefix length */
};				/* index information structure */

typedef enum
{
  METHOD_IS_NONE = 0,
  METHOD_IS_INSTANCE_METHOD = 1,
  METHOD_IS_CLASS_METHOD
} METHOD_TYPE;

typedef struct method_sig_node METHOD_SIG;
struct method_sig_node
{				/* method signature */
  METHOD_SIG *next;
  char *method_name;		/* method name */
  char *class_name;		/* class for the method */
  METHOD_TYPE method_type;	/* instance or class method */
  int num_method_args;		/* number of arguments */
  int *method_arg_pos;		/* arg position in list file */

  method_sig_node () = default;
};

struct method_sig_list
{				/* signature for methods */
  METHOD_SIG *method_sig;	/* one method signature */
  int num_methods;		/* number of signatures */

  method_sig_list () = default;
};
typedef struct method_sig_list METHOD_SIG_LIST;

// regular variable flag functions
// note - uppercase names are used because they used to be macros.
inline bool REGU_VARIABLE_IS_FLAGED (const regu_variable_node * regu, int flag);
inline void REGU_VARIABLE_SET_FLAG (regu_variable_node * regu, int flag);
inline void REGU_VARIABLE_CLEAR_FLAG (regu_variable_node * regu, int flag);

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

bool
REGU_VARIABLE_IS_FLAGED (const regu_variable_node * regu, int flag)
{
  return (regu->flags & flag) != 0;
}

void
REGU_VARIABLE_SET_FLAG (regu_variable_node * regu, int flag)
{
  regu->flags |= flag;
}

void
REGU_VARIABLE_CLEAR_FLAG (regu_variable_node * regu, int flag)
{
  regu->flags &= ~flag;
}

// *INDENT-ON*

#endif /* _REGU_VAR_H_ */
