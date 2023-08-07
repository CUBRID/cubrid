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
 * STATISTICS.h -
 */

#ifndef _STATISTICS_H_
#define _STATISTICS_H_

#ident "$Id$"

#include <stdio.h>
#include "dbtype_def.h"
#include "storage_common.h"
#include "object_domain.h"

#define STATS_WITH_FULLSCAN  true
#define STATS_WITH_SAMPLING  false

#define STATS_SAMPLING_THRESHOLD 50	/* sampling trial count */
#define STATS_SAMPLING_LEAFS_MAX   8	/* sampling leaf pages */

/* disk-resident elements of pkeys[] field */
#define BTREE_STATS_PKEYS_NUM      8
#define BTREE_STATS_RESERVED_NUM   4

#define STATS_MIN_MAX_SIZE    sizeof(DB_DATA)

/* free_and_init routine */
#define stats_free_statistics_and_init(stats) \
  do \
    { \
      stats_free_statistics ((stats)); \
      (stats) = NULL; \
    } \
  while (0)

/* B+tree statistical information */
typedef struct btree_stats BTREE_STATS;
struct btree_stats
{
  BTID btid;
  int leafs;			/* number of leaf pages including overflow pages */
  int pages;			/* number of total pages */
  int height;			/* the height of the B+tree */
  int keys;			/* number of keys */
  int has_function;		/* is a function index */
  TP_DOMAIN *key_type;		/* The key type for the B+tree */
  int pkeys_size;		/* pkeys array size */
  int *pkeys;			/* partial keys info for example: index (a, b, ..., x) pkeys[0] -> # of {a} pkeys[1] ->
				 * # of {a, b} ... pkeys[pkeys_size-1] -> # of {a, b, ..., x} */
#if defined(SUPPORT_DEDUPLICATE_KEY_MODE)
  int dedup_idx;
#endif

#if 0				/* reserved for future use */
  int reserved[BTREE_STATS_RESERVED_NUM];
#endif
};

/* Statistical Information about the attribute */
typedef struct attr_stats ATTR_STATS;
struct attr_stats
{
  int id;
  DB_TYPE type;
  int n_btstats;		/* number of B+tree statistics information */
  BTREE_STATS *bt_stats;	/* pointer to array of BTREE_STATS[n_btstats] */
};

/* Statistical Information about the class */
typedef struct class_stats CLASS_STATS;
struct class_stats
{
  unsigned int time_stamp;	/* the time stamped when the stat info updated; used to get up-to-date stat info */
  int heap_num_objects;		/* cardinality of the class; number of instances the class has */
  int heap_num_pages;		/* number of pages the class occupy */
  int n_attrs;			/* number of attributes; size of the attr_stats[] */
  ATTR_STATS *attr_stats;	/* pointer to the array of attribute statistics */
};

#if !defined(SERVER_MODE)
extern int stats_get_statistics (OID * classoid, unsigned int timestamp, CLASS_STATS ** stats_p);
extern void stats_free_statistics (CLASS_STATS * stats);
extern void stats_dump (const char *classname, FILE * fp);
#endif /* !SERVER_MODE */

#endif /* _STATISTICS_H_ */
