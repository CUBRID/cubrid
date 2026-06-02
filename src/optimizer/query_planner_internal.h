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

#ifndef _QUERY_PLANNER_INTERNAL_H_
#define _QUERY_PLANNER_INTERNAL_H_

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#if !defined(WINDOWS)
#include <values.h>
#endif /* !WINDOWS */
#include "jansson.h"

#include "parser.h"
#include "object_primitive.h"
#include "optimizer.h"
#include "query_planner.h"
#include "query_graph.h"
#include "environment_variable.h"
#include "misc_string.h"
#include "system_parameter.h"
#include "parser.h"
#include "parser_message.h"
#include "intl_support.h"
#include "storage_common.h"
#include "xasl_analytic.hpp"
#include "xasl_generation.h"
#include "schema_manager.h"
#include "network_interface_cl.h"
#include "dbtype.h"
#include "regu_var.hpp"
#include "histogram_cl.hpp"
#include "query_planner_constants.h"

double qo_or_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);
double qo_and_selectivity (QO_ENV * env, double lhs_sel, double rhs_sel);
double qo_not_selectivity (QO_ENV * env, double sel);
double qo_equal_selectivity (QO_ENV * env, PT_NODE * pt_expr);
double qo_comp_selectivity (QO_ENV * env, PT_NODE * pt_expr);
double qo_between_selectivity (QO_ENV * env, PT_NODE * pt_expr);
double qo_range_selectivity (QO_ENV * env, PT_NODE * pt_expr);
double qo_all_some_in_selectivity (QO_ENV * env, PT_NODE * pt_expr);
double qo_like_selectivity (QO_ENV * env, PT_NODE * pt_expr);
int qo_index_cardinality (QO_ENV * env, PT_NODE * attr);

#endif /* _QUERY_PLANNER_INTERNAL_H_ */
