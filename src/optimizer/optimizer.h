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
 * optimizer.h - Prototypes for all public query optimizer declarations
 */

#ifndef _OPTIMIZER_H_
#define _OPTIMIZER_H_

#ident "$Id$"

#include <stdarg.h>

#include "error_manager.h"
#include "memory_alloc.h"
#include "parser.h"
#include "release_string.h"
#include "parser.h"
#include "query_executor.h"

/*
 * These #defines are used in conjunction with assert() to announce
 * unexpected conditions.
 */
#define UNEXPECTED_CASE	0
#define UNREACHABLE	0

#define QO_ASSERT(env, cond) \
    do { \
	if (!(cond)) \
	    qo_abort((env), __FILE__, __LINE__); \
    } while(0)


#define QO_ABORT(env) qo_abort((env), __FILE__, __LINE__)

#ifdef USE_ER_SET
#define QO_WARN(code)		er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 0)
#define QO_WARN1(code, x)	er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, (code), 1, x)
#define QO_ERROR(code)		er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 0)
#define QO_ERROR1(code, x)	er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 1, x)
#define QO_ERROR2(code, x, y)	er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, (code), 2, x, y)
#else /* USE_ER_SET */
#define QO_WARN(code)
#define QO_WARN1(code, x)
#define QO_ERROR(code)
#define QO_ERROR1(code, x)
#define QO_ERROR2(code, x, y)
#endif /* USE_ER_SET */

#define DB_INTEGRAL_TYPE(t)	(qo_type_qualifiers[(t)] & _INT)
#define DB_NUMERIC_TYPE(t)	(qo_type_qualifiers[(t)] & _NUM)
#define _INT	0x1
#define _NUM	0x2


/*
 * The PRM_OPTIMIZATION_LEVEL parameter actually encodes (at least) two
 * different pieces of information: the actual level of optimization
 * desired, and whether we're supposed to dump a readable version of the
 * plan.  These macros encapsulate the representation decisions for that
 * information.
 */
#define OPT_LEVEL(level)		((level) & 0xff)
#define OPTIMIZATION_ENABLED(level)	(OPT_LEVEL(level) != 0)
#define PLAN_DUMP_ENABLED(level)	((level) >= 0x100)
#define SIMPLE_DUMP(level)		((level) & 0x100)
#define DETAILED_DUMP(level)		((level) & 0x200)

typedef struct qo_env QO_ENV;
typedef struct qo_node QO_NODE;
typedef struct qo_segment QO_SEGMENT;
typedef struct qo_term QO_TERM;
typedef struct qo_eqclass QO_EQCLASS;
typedef struct qo_subquery QO_SUBQUERY;
typedef struct qo_planner QO_PLANNER;
typedef struct qo_info QO_INFO;
typedef struct qo_partition QO_PARTITION;
typedef struct qo_class_info QO_CLASS_INFO;
typedef struct qo_attr_info QO_ATTR_INFO;
typedef struct qo_summary QO_SUMMARY;
typedef struct qo_index QO_INDEX;
typedef struct qo_index_entry QO_INDEX_ENTRY;
typedef struct qo_node_index QO_NODE_INDEX;
typedef struct qo_node_index_entry QO_NODE_INDEX_ENTRY;
typedef struct qo_index_xasl_info QO_INDEX_XASL_INFO;
typedef struct qo_using_index QO_USING_INDEX;
typedef struct qo_using_index_entry QO_USING_INDEX_ENTRY;
typedef struct bitset BITSET;

struct qo_summary
{
  double fixed_cpu_cost, fixed_io_cost;
  double variable_cpu_cost, variable_io_cost;
  double cardinality;
  XASL_NODE *xasl;
};

typedef struct
{
  DB_TYPE type;			/* data type of the attribute */
  int leafs;			/* number of leaf pages including overflow pages */
  int pages;			/* number of total pages */
  int height;			/* the height of the B+tree */
  int keys;			/* number of keys */
  TP_DOMAIN *key_type;		/* The key type for the B+tree */
  int pkeys_size;		/* pkeys array size */
  int *pkeys;			/* partial keys info
				   for example: index (a, b, ..., x)
				   pkeys[0]          -> # of {a}
				   pkeys[1]          -> # of {a, b}
				   ...
				   pkeys[key_size-1] -> # of {a, b, ..., x}
				 */
  bool valid_limits;
  bool is_indexed;
} QO_ATTR_CUM_STATS;

typedef struct qo_plan QO_PLAN;

typedef struct qo_xasl_index_info QO_XASL_INDEX_INFO;

typedef enum
{
  QO_PARAM_LEVEL,
  QO_PARAM_COST
} QO_PARAM;

typedef struct qo_limit_info
{
  REGU_VARIABLE *lower;
  REGU_VARIABLE *upper;
} QO_LIMIT_INFO;

extern QO_NODE *lookup_node (PT_NODE * attr, QO_ENV * env, PT_NODE ** entity);

extern QO_SEGMENT *lookup_seg (QO_NODE * head, PT_NODE * name, QO_ENV * env);

extern void qo_expr_segs (QO_ENV * env, PT_NODE * pt_expr, BITSET * result);

extern void qo_get_optimization_param (void *, QO_PARAM, ...);
extern bool qo_need_skip_execution (void);
extern void qo_set_optimization_param (void *, QO_PARAM, ...);
extern QO_PLAN *qo_optimize_query (PARSER_CONTEXT *, PT_NODE *);
extern XASL_NODE *qo_to_xasl (QO_PLAN *, XASL_NODE *);
extern void qo_plan_discard (QO_PLAN *);
extern void qo_plan_dump (QO_PLAN *, FILE *);
extern const char *qo_plan_set_cost_fn (const char *, int);
extern int qo_plan_get_cost_fn (const char *);
extern PT_NODE *qo_plan_iscan_sort_list (QO_PLAN *);
extern bool qo_plan_skip_orderby (QO_PLAN * plan);
extern bool qo_plan_skip_groupby (QO_PLAN * plan);
extern bool qo_is_index_cover_scan (QO_PLAN * plan);
extern bool qo_plan_multi_range_opt (QO_PLAN * plan);
extern bool qo_plan_filtered_index (QO_PLAN * plan);
extern void qo_set_cost (DB_OBJECT * target, DB_VALUE * result,
			 DB_VALUE * plan, DB_VALUE * cost);

/*
 *  QO_XASL support functions
 */
extern int qo_xasl_get_num_terms (QO_XASL_INDEX_INFO * info);
extern PT_NODE **qo_xasl_get_terms (QO_XASL_INDEX_INFO *);
extern BTID *qo_xasl_get_btid (MOP classop, QO_XASL_INDEX_INFO * info);
extern bool qo_xasl_get_multi_col (MOP class_mop, QO_XASL_INDEX_INFO * infop);
extern PT_NODE *qo_xasl_get_key_limit (MOP class_mop,
				       QO_XASL_INDEX_INFO * infop);
extern bool qo_xasl_get_coverage (MOP class_mop, QO_XASL_INDEX_INFO * infop);
extern PT_NODE *qo_check_nullable_expr (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);
extern PT_NODE *mq_optimize (PARSER_CONTEXT * parser, PT_NODE * statement);

#if 0
extern void *qo_malloc (QO_ENV *, unsigned, const char *, int);
#endif
extern void qo_abort (QO_ENV *, const char *, int);


extern unsigned char qo_type_qualifiers[];

extern double qo_expr_selectivity (QO_ENV * env, PT_NODE * pt_expr);

extern XASL_NODE *qo_add_hq_iterations_access_spec (QO_PLAN * plan,
						    XASL_NODE * xasl);

extern QO_LIMIT_INFO *qo_get_key_limit_from_instnum (PARSER_CONTEXT * parser,
						     QO_PLAN * plan,
						     XASL_NODE * xasl);

extern QO_LIMIT_INFO *qo_get_key_limit_from_ordbynum (PARSER_CONTEXT * parser,
						      QO_PLAN * plan,
						      XASL_NODE * xasl,
						      bool ignore_lower);

extern bool qo_check_iscan_for_multi_range_opt (QO_PLAN * plan);
extern bool qo_check_join_for_multi_range_opt (QO_PLAN * plan);
extern int qo_find_subplan_using_multi_range_opt (QO_PLAN * plan,
						  QO_PLAN ** result,
						  int *join_idx);
extern void qo_top_plan_print_json (PARSER_CONTEXT * parser,
				    XASL_NODE * xasl, PT_NODE * select,
				    QO_PLAN * plan);
extern void qo_top_plan_print_text (PARSER_CONTEXT * parser,
				    XASL_NODE * xasl, PT_NODE * select,
				    QO_PLAN * plan);
#endif /* _OPTIMIZER_H_ */
