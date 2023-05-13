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
 * Regular variable - functionality encoded into XASL
 */

#ifndef _REGU_VAR_HPP_
#define _REGU_VAR_HPP_

#include "heap_attrinfo.h"
#include "object_domain.h"
#include "query_list.h"
#include "string_opfunc.h"
#include "object_primitive.h"
#include "db_function.hpp"

#include <functional>

// forward definitions
struct xasl_node;
namespace cubxasl
{
  struct aggregate_list_node;
  struct pred_expr;
} // namespace cubxasl

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
typedef class regu_variable_node REGU_VARIABLE;
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

  void reset ()
  {
    id = -1;
    type = DB_TYPE_NULL;
    cache_dbvalp = NULL;
  }
};				/* Attribute Descriptor */

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
  TP_DOMAIN *domain;		/* resultant domain */
  TP_DOMAIN *original_domain;	/* original resultant domain, used at execution in case of XASL clones  */
  DB_VALUE *value;		/* value of the subtree */
  REGU_VARIABLE *leftptr;	/* left operand */
  REGU_VARIABLE *rightptr;	/* right operand */
  REGU_VARIABLE *thirdptr;	/* third operand */
  OPERATOR_TYPE opcode;		/* operator value */
  MISC_OPERAND misc_operand;	/* currently used for trim qualifier and datetime extract field specifier */
  cubxasl::pred_expr *pred;		/* predicate expression */

  /* NOTE: The following member is only used on server internally. */
  struct drand48_data *rand_seed;	/* seed to be used to generate pseudo-random sequence */
};

typedef struct function_node FUNCTION_TYPE;
struct function_node
{
  DB_VALUE *value;		/* value of the function */
  REGU_VARIABLE_LIST operand;	/* operands */
  FUNC_TYPE ftype;		/* function to call */
  mutable union function_tmp_obj *tmp_obj;
};

// NOTE: The following union is used when a function needs to store any object temporary in query execution
// please don't forget to deallocate it, refering regu_variable_node::clear_xasl_local() and qexec_clear_regu_var()
union function_tmp_obj
{
  cub_compiled_regex *compiled_regex;
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
const int REGU_VARIABLE_UPD_INS_LIST = 0x200;	/* for update or insert query */
const int REGU_VARIABLE_STRICT_TYPE_CAST = 0x400;/* for update or insert query */

class regu_variable_node
{
  public:
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

    using map_regu_func_type = std::function<void (regu_variable_node &regu, bool &stop)>;
    using map_xasl_func_type = std::function<void (xasl_node &xasl, bool &stop)>;
    // map_regu - recursive "walker" of regu variable tree applying function argument
    //
    // NOTE:
    //    stop argument may be used for interrupting mapper
    //
    //    !!! implementation is not mature; only arithmetic and function children are mapped.
    void map_regu (const map_regu_func_type &func);
    // map_regu_and_xasl - map regu variable and nested XASL's
    void map_regu_and_xasl (const map_regu_func_type &regu_func, const map_xasl_func_type &xasl_func);

    // free dynamically allocated memory from this node and all its children
    void clear_xasl ();

  private:
    void map_regu (const map_regu_func_type &func, bool &stop);

    // clear dynamically allocated memory from this node
    void clear_xasl_local ();
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

// regular variable flag functions
// note - uppercase names are used because they used to be macros.
inline bool REGU_VARIABLE_IS_FLAGED (const regu_variable_node *regu, int flag);
inline void REGU_VARIABLE_SET_FLAG (regu_variable_node *regu, int flag);
inline void REGU_VARIABLE_CLEAR_FLAG (regu_variable_node *regu, int flag);
inline DB_TYPE REGU_VARIABLE_GET_TYPE (const regu_variable_node *regu);

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

bool
REGU_VARIABLE_IS_FLAGED (const regu_variable_node *regu, int flag)
{
  return (regu->flags & flag) != 0;
}

void
REGU_VARIABLE_SET_FLAG (regu_variable_node *regu, int flag)
{
  regu->flags |= flag;
}

void
REGU_VARIABLE_CLEAR_FLAG (regu_variable_node *regu, int flag)
{
  regu->flags &= ~flag;
}

DB_TYPE
REGU_VARIABLE_GET_TYPE (const regu_variable_node *regu)
{
  if (regu)
    {
      return TP_DOMAIN_TYPE (regu->domain);
    }
  return DB_TYPE_UNKNOWN;
}
#endif /* _REGU_VAR_HPP_ */
