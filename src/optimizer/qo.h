/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * qo.h - Interface header file
 * TODO: merge this file to optimizer.h
 */

#ifndef _QO_H_
#define _QO_H_

#ident "$Id$"

#include <stdarg.h>

#include "parser.h"
#include "qp_xasl.h"

#include "memory_manager_2.h"

typedef struct qo_plan QO_PLAN;

typedef struct qo_xasl_index_info QO_XASL_INDEX_INFO;

typedef enum
{
  QO_PARAM_LEVEL,
  QO_PARAM_COST
} QO_PARAM;

extern void qo_get_optimization_param (void *, QO_PARAM, ...);
extern void qo_set_optimization_param (void *, QO_PARAM, ...);
extern QO_PLAN *qo_optimize_query (PARSER_CONTEXT *, PT_NODE *);
extern XASL_NODE *qo_to_xasl (QO_PLAN *, XASL_NODE *);
extern void qo_plan_discard (QO_PLAN *);
extern void qo_plan_dump (QO_PLAN *, FILE *);
extern const char *qo_plan_set_cost_fn (const char *, int);
extern int qo_plan_get_cost_fn (const char *);
extern PT_NODE *qo_plan_iscan_sort_list (QO_PLAN *);
extern bool qo_plan_skip_orderby (QO_PLAN * plan);
extern void qo_set_cost (DB_OBJECT * target, DB_VALUE * result,
			 DB_VALUE * plan, DB_VALUE * cost);

/*
 *  QO_XASL support functions
 */
extern PT_NODE **qo_xasl_get_terms (QO_XASL_INDEX_INFO *);
extern int qo_xasl_get_num_terms (QO_XASL_INDEX_INFO * info);
extern BTID *qo_xasl_get_btid (MOP classop, QO_XASL_INDEX_INFO * info);
extern bool qo_xasl_get_multi_col (MOP class_mop, QO_XASL_INDEX_INFO * infop);
extern PT_NODE *qo_check_nullable_expr (PARSER_CONTEXT * parser,
					PT_NODE * node, void *arg,
					int *continue_walk);
extern PT_NODE *mq_optimize (PARSER_CONTEXT * parser, PT_NODE * statement);

#endif /* _QO_H_ */
