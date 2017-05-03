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
 * parser_support.h - Query processor memory management module
 */

#ifndef _PARSER_SUPPORT_H_
#define _PARSER_SUPPORT_H_

#ident "$Id$"

#include "config.h"
#include "error_manager.h"
#include "oid.h"
#include "storage_common.h"
#include "dbtype.h"
#include "heap_file.h"

/*
 *
 *       	         Miscellaneous macros
 *
 */

#define OID_BLOCK_ARRAY_SIZE    10
typedef struct oid_block_list
{
  struct oid_block_list *next;
  int last_oid_idx;
  OID oid_array[OID_BLOCK_ARRAY_SIZE];
} OID_BLOCK_LIST;

#define SORT_SPEC_EQ(a, b) \
  ((a)->info.sort_spec.pos_descr.pos_no == (b)->info.sort_spec.pos_descr.pos_no \
   && (a)->info.sort_spec.asc_or_desc == (b)->info.sort_spec.asc_or_desc \
   && (a)->info.sort_spec.nulls_first_or_last == (b)->info.sort_spec.nulls_first_or_last)

/*
 *       	         CATALOG STRUCTURES
 */

typedef int CL_REPR_ID;
typedef int CL_ATTR_ID;

typedef struct
{
  CL_ATTR_ID id;		/* Attribute identifier */
  DB_TYPE type;			/* Attribute data type */
  HEAP_CACHE_ATTRINFO *cache_attrinfo;	/* used to cache catalog info */
  DB_VALUE *cache_dbvalp;	/* cached value for particular attr */
  /* in cache_attrinfo */
} ATTR_DESCR;			/* Attribute Descriptor */

#define UT_CLEAR_ATTR_DESCR(ptr) \
  do \
    { \
      (ptr)->id = -1; \
      (ptr)->type = DB_TYPE_NULL; \
      (ptr)->cache_dbvalp = NULL; \
    } \
  while (0)

/*
 *       	         INDEX STRUCTURES
 */

typedef enum
{ T_BTID = 1, T_EHID } INDX_ID_TYPE;

typedef struct index_id_node
{
  INDX_ID_TYPE type;		/* Index Type */
  union
  {
    BTID btid;			/* B+tree index identifier */
    EHID ehid;			/* Extendible Hash index identifier */
  } i;
} INDX_ID;			/* Index Identifier */

typedef enum
{
  R_KEY = 1,			/* key value search */
  R_RANGE,			/* range search with the two key values and range spec */
  R_KEYLIST,			/* a list of key value searches */
  R_RANGELIST			/* a list of range searches */
} RANGE_TYPE;

/*
 * Forward declaration (defined in query_opfunc.h)
 */
struct regu_variable_node;

typedef struct key_range
{
  struct regu_variable_node *key1;	/* pointer to first key value */
  struct regu_variable_node *key2;	/* pointer to second key value */
  RANGE range;			/* range spec; GE_LE, GT_LE, GE_LT, GT_LT, GE_INF, GT_INF, INF_LT, INF_LE, INF_INF */
} KEY_RANGE;			/* key range structure */

typedef struct key_info
{
  KEY_RANGE *key_ranges;	/* a list of key ranges */
  int key_cnt;			/* key count */
  bool is_constant;		/* every key value is a constant */
  bool key_limit_reset;		/* should key limit reset at each range */
  bool is_user_given_keylimit;	/* true if user specifies key limit */
  struct regu_variable_node *key_limit_l;	/* lower key limit */
  struct regu_variable_node *key_limit_u;	/* upper key limit */
} KEY_INFO;			/* key information structure */

typedef struct indx_info
{
  INDX_ID indx_id;		/* index identifier */
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
} INDX_INFO;			/* index information structure */


/*
 *       	   ESQL POSITIONAL VALUES RELATED TYPEDEFS
 */

typedef struct val_descr
{
  DB_VALUE *dbval_ptr;		/* Array of values */
  int dbval_cnt;		/* Value Count */
  DB_DATETIME sys_datetime;
  DB_TIMESTAMP sys_epochtime;
  long lrand;
  double drand;
  struct xasl_state *xasl_state;	/* XASL_STATE pointer */
} VAL_DESCR;			/* Value Descriptor */

/*
 *       	      STRUCTURE FOR QUERY PROCESSOR DOMAIN
 */

/* The following structure is semantically similar to the structure DOMAIN
 * except that every single node represents a physical class instead of a
 * class hierarchy.
 */

struct qp_domain
{
  struct qp_domain *next;
  struct qp_domain *setdomain;
  OID classid;
  DB_TYPE type;
};

/*
 *       		STRUCTURES FOR STRING LISTS
 */

typedef struct qproc_string_list QPROC_STRING_LIST;
struct qproc_string_list
{
  QPROC_STRING_LIST *next;
  char *name;
};

extern int qp_Packing_er_code;

/* Memory Buffer Related Routines */
extern char *pt_alloc_packing_buf (int size);
extern void pt_final_packing_buf (void);
extern void pt_enter_packing_buf (void);
extern void pt_exit_packing_buf (void);

#endif /* _PARSER_SUPPORT_H_ */
