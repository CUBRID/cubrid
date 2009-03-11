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
 * STATISTICS.h -
 */

#ifndef _STATISTICS_H_
#define _STATISTICS_H_

#ident "$Id$"

#include <stdio.h>
#include "dbtype.h"
#include "storage_common.h"
#include "object_domain.h"

/* disk-resident elements of pkeys[] field */
#define BTREE_STATS_PKEYS_NUM      8
#define BTREE_STATS_RESERVED_NUM   2

#define STATS_MIN_MAX_SIZE    sizeof(DB_DATA)

/* B+tree statistical information */
typedef struct btree_stats BTREE_STATS;
struct btree_stats
{
  BTID btid;
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
  int reserved[BTREE_STATS_RESERVED_NUM];	/* reserved space for future use */
};

/* Statistical Information about the attribute */
typedef struct attr_stats ATTR_STATS;
struct attr_stats
{
  int id;
  DB_TYPE type;
  DB_DATA min_value;		/* minimum existing value */
  DB_DATA max_value;		/* maximum existing value */
  int n_btstats;		/* number of B+tree statistics information */
  BTREE_STATS *bt_stats;	/* pointer to array of BTREE_STATS[n_btstats] */
};

/* Statistical Information about the class */
typedef struct class_stats CLASS_STATS;
struct class_stats
{
  unsigned int time_stamp;	/* the time stamped when the stat info updated;
				   used to get up-to-date stat info */
  int num_objects;		/* cardinality of the class;
				   number of instances the class has */
  int heap_size;		/* number of pages the class occupy */
  int n_attrs;			/* number of attributes; size of the
				   attr_stats[] */
  ATTR_STATS *attr_stats;	/* pointer to the array of attribute
				   statistics */
};

#if !defined(SERVER_MODE)
extern CLASS_STATS *stats_get_statistics (OID * classoid,
					  unsigned int timestamp);
extern void stats_free_statistics (CLASS_STATS * stats);
extern void stats_dump (const char *classname, FILE * fp);
#endif /* !SERVER_MODE */

#endif /* _STATISTICS_H_ */
