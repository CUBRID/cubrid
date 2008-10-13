/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * optimizer.h - Prototypes for all public query optimizer declarations
 * TODO: include qo.h and remove it
 */

#ifndef _OPTIMIZER_H_
#define _OPTIMIZER_H_

#ident "$Id$"

#include "qo.h"

#include "error_manager.h"
#include "memory_manager_2.h"
#include "parser.h"
#include "release_string.h"

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

#ifdef ALLOCATE
#undef ALLOCATE
#endif /* ALLOCATE */

#ifdef NALLOCATE
#undef NALLOCATE
#endif /* NALLOCATE */

#ifdef DEALLOCATE
#undef DEALLOCATE
#endif /* DEALLOCATE */

#define ALLOCATE(env, x) \
    (x *)malloc(sizeof(x))

#define NALLOCATE(env, x, n) \
    ((n) ? (x *)malloc((n) * sizeof(x)) \
     : NULL)

#define BALLOCATE(env, s) \
    ((s > 0) ? malloc((s)) \
     : NULL)

#define DEALLOCATE(env, p) \
    do { \
	if (p) \
	    free_and_init(p); \
    } while (0)

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
  bool valid_limits;
  DB_DATA min_value;		/* minimum existing value */
  DB_DATA max_value;		/* maximum existing value */
  bool is_indexed;
  int leafs;			/* number of leaf pages including overflow pages */
  int pages;			/* number of total pages */
  int height;			/* the height of the B+tree */
  int keys;			/* number of keys */
  int oids;			/* number of OIDs */
  int nulls;			/* number of NULL values */
  int ukeys;			/* number of unique keys */
  TP_DOMAIN *key_type;		/* The key type for the B+tree */
  int key_size;			/* number of key columns */
  int *pkeys;			/* partial keys info
				   for example: index (a, b, ..., x)
				   pkeys[0]          -> # of {a}
				   pkeys[1]          -> # of {a, b}
				   ...
				   pkeys[key_size-1] -> # of {a, b, ..., x}
				 */
} QO_ATTR_CUM_STATS;

/* From build_graph.c */

extern QO_NODE *lookup_node (PT_NODE * attr, QO_ENV * env, PT_NODE ** entity);

extern QO_SEGMENT *lookup_seg (QO_NODE * head, PT_NODE * name, QO_ENV * env);

extern void qo_expr_segs (QO_ENV * env, PT_NODE * pt_expr, BITSET * result);

/* From env.c */

#if 0
extern void *qo_malloc (QO_ENV *, unsigned, const char *, int);
#endif
extern void qo_abort (QO_ENV *, const char *, int);


/* From selectivity.c */
extern unsigned char qo_type_qualifiers[];


/* From exprsel.c */

extern double qo_expr_selectivity (QO_ENV * env, PT_NODE * pt_expr);

#endif /* _OPTIMIZER_H_ */
