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

#ifndef _QUERY_PLANNER_CONSTANTS_H_
#define _QUERY_PLANNER_CONSTANTS_H_

#define TEST_DUMP_PLAN_SCAN_COST 0
#define TEST_DUMP_PLAN_SORT_COST 0
#define TEST_DUMP_PLAN_JOIN_COST 0
#define TEST_DUMP_PLAN_FOLLOW_COST 0

#define TEST_HASH_JOIN_ENABLE 0
#define TEST_HASH_JOIN_FORCE_ENABLE 0

#define INDENT_INCR		4
#define INDENT_FMT		"%*c"
#define TITLE_WIDTH		7
#define TITLE_FMT		"%-" __STR(TITLE_WIDTH) "s"
#define INDENTED_TITLE_FMT	INDENT_FMT TITLE_FMT
#define __STR(n)		__VAL(n)
#define __VAL(n)		#n
#define SORT_SPEC_FMT(spec)   "%d %s %s", (spec)->pos_descr.pos_no + 1,   ((spec)->s_order == S_ASC ? "asc" : "desc"),   ((spec)->s_nulls == S_NULLS_FIRST ? "nulls first" : "nulls last")

#define TEMP_SETUP_COST 5.0
#define QO_CPU_WEIGHT 0.0025
#define ISCAN_OID_ACCESS_OVERHEAD 20	/* need to be adjusted */
#define MJ_CPU_OVERHEAD_FACTOR 20
#define HJ_BUILD_CPU_OVERHEAD_FACTOR 30
#define HJ_PROBE_CPU_OVERHEAD_FACTOR 20
#define HJ_FILE_IO_WEIGHT 0.5	/* Unused */
#define ISCAN_IO_HIT_RATIO 0.5
#define QO_NLJOIN_MEMOIZE_HIT_COST_RATIO 0.01
#define SSCAN_DEFAULT_CARD 100
#define GUESSED_BIND_LIMIT_CARD 2000	/* When limit is a bind variable, assume that fewer rows will be assigned. */

#define RBO_CHECK_COST 50
#define RBO_CHECK_RATIO 1.2
#define RBO_CHECK_LIMIT_RATIO 10

/* generic predicate evaluation */
#define QO_COST_WEIGHT_PRED_DEFAULT            1.00

/* numeric/date equality or simple scalar comparison */
#define QO_COST_WEIGHT_NUMERIC_COMPARE         1.00
#define QO_COST_WEIGHT_TEMPORAL_COMPARE        1.05

/* string comparisons */
#define QO_COST_WEIGHT_STRING_EQUAL            3.00
#define QO_COST_WEIGHT_STRING_RANGE            3.25
#define QO_COST_WEIGHT_STRING_COLLATION        3.50

/* LIKE family */
#define QO_COST_WEIGHT_LIKE_PREFIX             3.50	/* e.g. 'abc%' */
#define QO_COST_WEIGHT_LIKE_CONTAINS           10.00	/* e.g. '%abc%' */
#define QO_COST_WEIGHT_LIKE_COMPLEX            14.00	/* mixed %, _, escapes */

/* joins */
#define QO_COST_WEIGHT_JOIN_DEFAULT            1.00
#define QO_COST_WEIGHT_JOIN_STRING_EQUAL       1.10
#define QO_COST_WEIGHT_JOIN_STRING_RANGE       1.25

/* optional guard rails */
#define QO_COST_WEIGHT_MIN                     0.50
#define QO_COST_WEIGHT_MAX                     5.00

/* sequential scan */
#define QO_SSCAN_FILTER_CPU_FACTOR             1.00

#define DEFAULT_NULL_SELECTIVITY (double) 0.01
#define DEFAULT_EXISTS_SELECTIVITY (double) 0.1
#define DEFAULT_SELECTIVITY (double) 0.1
#define DEFAULT_EQUAL_SELECTIVITY (double) 0.001
#define DEFAULT_EQUIJOIN_SELECTIVITY (double) 0.001
#define DEFAULT_COMP_SELECTIVITY (double) 0.1
#define DEFAULT_BETWEEN_SELECTIVITY (double) 0.01
#define DEFAULT_IN_SELECTIVITY (double) 0.01
#define DEFAULT_RANGE_SELECTIVITY (double) 0.1

#endif /* _QUERY_PLANNER_CONSTANTS_H_ */
