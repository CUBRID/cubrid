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
 * Heap attribute info - interface between heap and query modules
 */

#ifndef _HEAP_ATTRINFO_H_
#define _HEAP_ATTRINFO_H_

#if defined (SERVER_MODE) || defined (SA_MODE)
#include "object_representation_sr.h"

typedef enum
{
  HEAP_READ_ATTRVALUE,
  HEAP_WRITTEN_ATTRVALUE,
  HEAP_UNINIT_ATTRVALUE,
  HEAP_WRITTEN_LOB_ATTRVALUE,
  HEAP_LAZY_ATTRVALUE		/* deferred: no dbvalue yet; read on demand from attr_info->lazy_recdes.
				 * Behaves like HEAP_UNINIT_ATTRVALUE everywhere except heap_attrinfo_access (). */
} HEAP_ATTRVALUE_STATE;

typedef enum
{
  HEAP_INSTANCE_ATTR,
  HEAP_SHARED_ATTR,
  HEAP_CLASS_ATTR
} HEAP_ATTR_TYPE;

typedef struct heap_attrvalue HEAP_ATTRVALUE;
struct heap_attrvalue
{
  ATTR_ID attrid;		/* attribute identifier */
  HEAP_ATTRVALUE_STATE state;	/* State of the attribute value. Either of has been read, has been updated, or is
				 * not initialized */
  int do_increment;
  HEAP_ATTR_TYPE attr_type;	/* Instance, class, or shared attribute */
  OR_ATTRIBUTE *last_attrepr;	/* Used for default values */
  OR_ATTRIBUTE *read_attrepr;	/* Pointer to a desired attribute information */
  DB_VALUE dbvalue;		/* DB values of the attribute in memory */
  bool lazy_always_eager;	/* lazy mode: read this attribute now instead of deferring (column of the
				 * first-evaluated predicate term, touched on nearly every row) */
};

typedef struct heap_cache_attrinfo HEAP_CACHE_ATTRINFO;
struct heap_cache_attrinfo
{
  OID class_oid;		/* Class object identifier */
  int last_cacheindex;		/* An index identifier when the last_classrepr was obtained from the classrepr cache.
				 * Otherwise, -1 */
  int read_cacheindex;		/* An index identifier when the read_classrepr was obtained from the classrepr cache.
				 * Otherwise, -1 */
  OR_CLASSREP *last_classrepr;	/* Currently cached catalog attribute info. */
  OR_CLASSREP *read_classrepr;	/* Currently cached catalog attribute info. */
  OID inst_oid;			/* Instance Object identifier */
  int inst_chn;			/* Current chn of instance object */
  int num_values;		/* Number of desired attribute values */
  HEAP_ATTRVALUE *values;	/* Value for the attributes */
  RECDES *lazy_recdes;		/* non-NULL: lazy mode; record from which HEAP_LAZY_ATTRVALUE attrs are read
				 * on demand by heap_attrinfo_access (). Set by heap_attrinfo_read_dbvalues_lazy (). */
  /* How much deferring actually saves depends on the data, so it is measured: the counters below accumulate
   * over the scan's first HEAP_LAZY_CALIBRATE_ROWS rows (see heap_attrinfo_read_dbvalues_lazy ()). */
  INT64 lazy_rows;		/* rows prepared in lazy mode */
  INT64 lazy_deferred;		/* cumulative slots marked HEAP_LAZY_ATTRVALUE (reads lazy could have skipped) */
  INT64 lazy_decoded;		/* cumulative slots of those actually read on demand (reads it did not skip) */
  bool lazy_disabled;		/* true: read predicate columns eagerly - set at scan setup when nothing can be
				 * deferred (eval_disable_lazy_read ()), or mid-scan by the measurement above */
};

extern DB_VALUE *heap_attrvalue_peek_lazy (HEAP_ATTRVALUE * slot, HEAP_CACHE_ATTRINFO * attr_info);

#else /* !defined (SERVER_MODE) && !defined (SA_MODE) */

/* XASL generation uses pointer to heap_cache_attrinfo. we need to just declare a dummy struct here. */
typedef struct heap_cache_attrinfo HEAP_CACHE_ATTRINFO;
struct heap_cache_attrinfo
{
  int dummy;
};

#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#endif /* _HEAP_ATTRINFO_H_ */
