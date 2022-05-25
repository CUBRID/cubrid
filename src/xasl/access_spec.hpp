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

//
// access_spec - defines access on tables, lists, indexes
//

#ifndef _ACCESS_SPEC_HPP_
#define _ACCESS_SPEC_HPP_

#include "dbtype_def.h"
#include "storage_common.h"

// forward definitions
class regu_variable_node;

typedef enum			/* range search option */
{
  NA_NA,			/* v1 and v2 are N/A, so that no range is defined */
  GE_LE,			/* v1 <= key <= v2 */
  GE_LT,			/* v1 <= key < v2 */
  GT_LE,			/* v1 < key <= v2 */
  GT_LT,			/* v1 < key < v2 */
  GE_INF,			/* v1 <= key (<= the end) */
  GT_INF,			/* v1 < key (<= the end) */
  INF_LE,			/* (the beginning <=) key <= v2 */
  INF_LT,			/* (the beginning <=) key < v2 */
  INF_INF,			/* the beginning <= key <= the end */
  EQ_NA,			/* key = v1, v2 is N/A */

  /* following options are reserved for the future use */
  LE_GE,			/* key <= v1 || key >= v2 or NOT (v1 < key < v2) */
  LE_GT,			/* key <= v1 || key > v2 or NOT (v1 < key <= v2) */
  LT_GE,			/* key < v1 || key >= v2 or NOT (v1 <= key < v2) */
  LT_GT,			/* key < v1 || key > v2 or NOT (v1 <= key <= v2) */
  NEQ_NA			/* key != v1 */
} RANGE;

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
  regu_variable_node *key1;		/* pointer to first key value */
  regu_variable_node *key2;		/* pointer to second key value */
  RANGE range;			/* range spec; GE_LE, GT_LE, GE_LT, GT_LT, GE_INF, GT_INF, INF_LT, INF_LE, INF_INF */
};				/* key range structure */

typedef struct key_info KEY_INFO;
struct key_info
{
  key_range *key_ranges;	/* a list of key ranges */
  KEY_VAL_RANGE *key_vals;	/* a list of key values */
  int key_cnt;			/* key count */
  bool is_constant;		/* every key value is a constant */
  bool key_limit_reset;		/* should key limit reset at each range */
  bool is_user_given_keylimit;	/* true if user specifies key limit */
  regu_variable_node *key_limit_l;	/* lower key limit */
  regu_variable_node *key_limit_u;	/* upper key limit */
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

// TODO - move access specification code here; note - this is supposed to be common to both client and server.
//        access spec structures are now partly common and partly server/SA only. this requires some redesign &
//        refactoring

//////////////////////////////////////////////////////////////////////////
// inline/template implementation
//////////////////////////////////////////////////////////////////////////

inline void
range_reverse (RANGE &range)
{
  switch (range)
    {
    case GT_LE:
      range = GE_LT;
      break;
    case GE_LT:
      range = GT_LE;
      break;
    case GE_INF:
      range = INF_LE;
      break;
    case GT_INF:
      range = INF_LT;
      break;
    case INF_LE:
      range = GE_INF;
      break;
    case INF_LT:
      range = GT_INF;
      break;
    case NA_NA:
    case GE_LE:
    case GT_LT:
    case INF_INF:
    case EQ_NA:
    default:
      /* No change. */
      break;
    }
}
#endif // _ACCESS_SPEC_HPP_
