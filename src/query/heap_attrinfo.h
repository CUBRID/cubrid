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
#include "object_representation.h"
#include "object_representation_sr.h"

struct pr_type;

typedef enum
{
  HEAP_READ_ATTRVALUE,
  HEAP_WRITTEN_ATTRVALUE,
  HEAP_UNINIT_ATTRVALUE,
  HEAP_WRITTEN_LOB_ATTRVALUE,
  HEAP_LAZY_ATTRVALUE		/* deferred: no dbvalue yet; read on demand from attr_info->lazy_recdes
				 * by heap_attrvalue_peek_lazy () */
} HEAP_ATTRVALUE_STATE;

typedef enum
{
  HEAP_INSTANCE_ATTR,
  HEAP_SHARED_ATTR,
  HEAP_CLASS_ATTR
} HEAP_ATTR_TYPE;

/* Per-record decoding layout.  Locating any attribute inside a record needs the object
 * header size, the width of a variable-offset entry, the start of the fixed area and the
 * bound-bit vector -- all properties of the record as a whole, so a caller that decodes
 * several attributes of the same record builds this once and hands it down
 * (heap_rec_layout_init () in heap_file.c). */
typedef struct heap_rec_layout HEAP_REC_LAYOUT;
struct heap_rec_layout
{
  char *var_table;		/* first entry of the variable-offset table */
  char *fixed_base;		/* first byte of the fixed-attribute area */
  char *bound_bits;		/* bound-bit vector, NULL when the record carries none */
  int offset_size;		/* width of one variable-offset table entry */
};

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

  /* Decoding plan for this attribute, resolved once per representation instead of once
   * per row.  The type handler, the disk width of a fixed attribute and whether the value
   * slot can end up owning heap memory are all properties of the attribute's descriptor,
   * but heap_attrvalue_read () used to look them up again for every record.  rd_attrepr
   * is the descriptor the three below were resolved for; a mismatch (or NULL) means the
   * plan must be rebuilt. */
  const OR_ATTRIBUTE *rd_attrepr;
  const struct pr_type *rd_type;
  /* type-fixed decode kernel: writes the DB_VALUE straight from the record bytes, doing
   * none of the per-row work the generic path repeats (OR_BUF setup, handler dispatch,
   * domain re-validation).  NULL when the type keeps the generic path. */
  int (*rd_readval) (DB_VALUE * out, const char *disk, int size, const OR_ATTRIBUTE * attrepr);
  int rd_disk_size;		/* fixed attributes: disk width; -1 when not applicable */
  bool rd_owns_memory;		/* the decoded value can hold a pointer that must be released */
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
  RECDES *lazy_recdes;		/* lazy mode when non-NULL: record the deferred attrs are read from */
  HEAP_REC_LAYOUT lazy_layout;	/* lazy_recdes's decoding layout, computed once when it is armed;
				 * meaningful only while lazy_recdes != NULL */
  /* deferring is worth it or not depending on the data, so it is measured over the first
   * HEAP_LAZY_CALIBRATE_ROWS rows (see heap_attrinfo_read_dbvalues_lazy ()). */
  INT64 lazy_rows;		/* rows prepared in lazy mode */
  INT64 lazy_deferred;		/* slots marked HEAP_LAZY_ATTRVALUE (reads lazy could have skipped) */
  INT64 lazy_decoded;		/* those actually read on demand (reads it did not skip) */
  bool lazy_disabled;		/* true: read predicate columns eagerly - nothing to defer, or not worth it */
};

extern DB_VALUE *heap_attrvalue_peek_lazy (HEAP_ATTRVALUE * slot, HEAP_CACHE_ATTRINFO * attr_info);

/* heap_rec_layout_init_repr_hdr () - the record layout from the representation constants and the header size
 *   layout(out): the layout
 *   obj(in): the record bytes (RECDES.data)
 *   hdr_size(in): the record header's bytes, OR_HEADER_SIZE (obj), when the caller has it already (a page
 *                 batch's rows carry it from the collector's read of the MVCC header, HEAP_PAGE_ROW.hdr_size)
 *   n_variable(in): OR_CLASSREP.n_variable of the record's representation
 *   fixed_length(in): OR_CLASSREP.fixed_length of the same representation
 *
 * The two representation constants are all a caller that has already matched the record's
 * representation id needs (a page batch decoding every record with one representation,
 * expr_batch.c).  heap_rec_layout_init () in heap_file.c computes the same four fields from the
 * attribute cache for the row path's lazy read.
 */
STATIC_INLINE void
heap_rec_layout_init_repr_hdr (HEAP_REC_LAYOUT * layout, const char *obj, int hdr_size, int n_variable,
			       int fixed_length)
{
  layout->offset_size = OR_GET_OFFSET_SIZE (obj);
  layout->var_table = (char *) obj + hdr_size;
  layout->fixed_base = layout->var_table + OR_VAR_TABLE_SIZE_INTERNAL (n_variable, layout->offset_size);
  layout->bound_bits = OR_GET_BOUND_BIT_FLAG (obj) ? layout->fixed_base + fixed_length : NULL;
}

/* Where one attribute sits in records of the class's latest representation: the numbers a
 * reader that decodes straight from record bytes needs, and nothing of the catalog they came
 * from (heap_attrinfo_raw_locator () in heap_file.c). */
typedef struct heap_raw_locator HEAP_RAW_LOCATOR;
struct heap_raw_locator
{
  REPR_ID repid;		/* the representation these offsets belong to */
  int n_variable;		/* representation constants heap_rec_layout_init_repr_hdr () takes */
  int fixed_length;
  bool is_fixed;		/* fixed area (location = byte offset) or variable area (location = table index) */
  int location;
  int position;			/* bound-bit index (fixed attributes) */
  DB_TYPE type;			/* the attribute's disk type */
  int precision;		/* NUMERIC: the domain's precision and scale */
  int scale;
  int collation;		/* VARCHAR, CHAR: the domain's collation and codeset */
  int codeset;
};

#else /* !defined (SERVER_MODE) && !defined (SA_MODE) */

/* XASL generation uses pointer to heap_cache_attrinfo. we need to just declare a dummy struct here. */
typedef struct heap_cache_attrinfo HEAP_CACHE_ATTRINFO;
struct heap_cache_attrinfo
{
  int dummy;
};

#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#endif /* _HEAP_ATTRINFO_H_ */
