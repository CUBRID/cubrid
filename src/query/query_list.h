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
 * List files shared
 */

#ifndef _QUERY_LIST_H_
#define _QUERY_LIST_H_

#ident "$Id$"

#ifdef __cplusplus
#include <atomic>
#include <stdint.h>
#include "thread_compat.hpp"
#endif

#include "storage_common.h"
#include "object_domain.h"

typedef enum
{
  NO_JOIN = -1,
  JOIN_INNER = 0,
  JOIN_LEFT,
  JOIN_RIGHT,
  JOIN_OUTER,
  JOIN_CSELECT
} JOIN_TYPE;

#define IS_OUTER_JOIN_TYPE(t) ((t) == JOIN_LEFT || (t) == JOIN_RIGHT || (t) == JOIN_OUTER)

/* PAGE CONSTANTS */

/* aligned size of the field */
#define QFILE_PAGE_HEADER_SIZE          32

/* offset values to access fields */
#define QFILE_TUPLE_COUNT_OFFSET        0
#define QFILE_PREV_PAGE_ID_OFFSET       4
#define QFILE_NEXT_PAGE_ID_OFFSET       8
#define QFILE_LAST_TUPLE_OFFSET         12
#define QFILE_OVERFLOW_PAGE_ID_OFFSET   16
#define QFILE_PREV_VOL_ID_OFFSET        20
#define QFILE_NEXT_VOL_ID_OFFSET        22
#define QFILE_OVERFLOW_VOL_ID_OFFSET    24
#define QFILE_RESERVED_OFFSET		26

/* Invalid offset value to the page */
#define QFILE_NULL_PAGE_OFFSET          -1

/*
 *       		READERS/WRITERS FOR PAGE FIELDS
 */

#define QFILE_GET_TUPLE_COUNT(ptr) \
  OR_GET_INT ((ptr) + QFILE_TUPLE_COUNT_OFFSET)

#define QFILE_GET_PREV_PAGE_ID(ptr) \
  (PAGEID) OR_GET_INT ((ptr) + QFILE_PREV_PAGE_ID_OFFSET)

#define QFILE_GET_NEXT_PAGE_ID(ptr) \
  (PAGEID) OR_GET_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET)

#define QFILE_GET_LAST_TUPLE_OFFSET(ptr) \
  (PAGEID) OR_GET_INT ((ptr) + QFILE_LAST_TUPLE_OFFSET)

#define QFILE_GET_OVERFLOW_PAGE_ID(ptr) \
  (PAGEID) OR_GET_INT ((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET)

#define QFILE_GET_PREV_VOLUME_ID(ptr) \
  (VOLID) OR_GET_SHORT ((ptr) + QFILE_PREV_VOL_ID_OFFSET)

#define QFILE_GET_NEXT_VOLUME_ID(ptr) \
  (VOLID) OR_GET_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET)

#define QFILE_GET_OVERFLOW_VOLUME_ID(ptr) \
  (VOLID) OR_GET_SHORT ((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET)

/*
 * Don't change the order of reading VPID's member in 'GET_XXX_VPID' series.
 * It is arranged for synchronization of async query execution.
 */

#define QFILE_GET_PREV_VPID(des,ptr) \
  do \
    { \
      (des)->pageid = (PAGEID) OR_GET_INT ((ptr) + QFILE_PREV_PAGE_ID_OFFSET); \
      (des)->volid = (VOLID) OR_GET_SHORT ((ptr) + QFILE_PREV_VOL_ID_OFFSET); \
    } \
  while (0)

#define QFILE_GET_NEXT_VPID(des,ptr) \
  do \
    { \
      (des)->pageid = (PAGEID) OR_GET_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET); \
      (des)->volid = (VOLID) OR_GET_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET); \
    } \
  while (0)

#define QFILE_GET_OVERFLOW_VPID(des,ptr) \
  do \
    { \
      (des)->pageid = (PAGEID) OR_GET_INT ((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET); \
      (des)->volid = (VOLID) OR_GET_SHORT ((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET); \
    } \
  while (0)

#define QFILE_PUT_TUPLE_COUNT(ptr,val) \
   OR_PUT_INT ((ptr) + QFILE_TUPLE_COUNT_OFFSET, (val))

#define QFILE_PUT_PREV_PAGE_ID(ptr,val) \
   OR_PUT_INT ((ptr) + QFILE_PREV_PAGE_ID_OFFSET, (val))

#define QFILE_PUT_NEXT_PAGE_ID(ptr,val) \
   OR_PUT_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, (val))

#define QFILE_PUT_LAST_TUPLE_OFFSET(ptr,val) \
   OR_PUT_INT ((ptr) + QFILE_LAST_TUPLE_OFFSET, (val))

#define QFILE_PUT_OVERFLOW_PAGE_ID(ptr,val) \
   OR_PUT_INT ((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET, (val))

#define QFILE_PUT_PREV_VOLUME_ID(ptr,val) \
   OR_PUT_SHORT ((ptr) + QFILE_PREV_VOL_ID_OFFSET, (val))

#define QFILE_PUT_NEXT_VOLUME_ID(ptr,val) \
   OR_PUT_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET, (val))

#define QFILE_PUT_OVERFLOW_VOLUME_ID(ptr,val) \
   OR_PUT_SHORT ((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET, (val))

/*
 * Don't change the order of writing VPID's member in 'PUT_XXX_VPID' series.
 * It is arranged for synchronization of async query execution.
 */

#define QFILE_PUT_PREV_VPID(ptr,vpid) \
  do \
    { \
      OR_PUT_SHORT ((ptr) + QFILE_PREV_VOL_ID_OFFSET, (vpid)->volid); \
      OR_PUT_INT ((ptr) + QFILE_PREV_PAGE_ID_OFFSET, (vpid)->pageid); \
    } \
  while (0)

#define QFILE_PUT_NEXT_VPID(ptr,vpid) \
  do \
    { \
      OR_PUT_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET, (vpid)->volid); \
      OR_PUT_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, (vpid)->pageid); \
    } \
  while (0)

#define QFILE_PUT_OVERFLOW_VPID(ptr,vpid) \
  do \
    { \
      OR_PUT_SHORT ((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET, (vpid)->volid); \
      OR_PUT_INT ((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET, (vpid)->pageid); \
    } \
  while (0)

#define QFILE_PUT_PREV_VPID_NULL(ptr) \
  do \
    { \
      OR_PUT_SHORT ((ptr) + QFILE_PREV_VOL_ID_OFFSET, NULL_VOLID); \
      OR_PUT_INT ((ptr) + QFILE_PREV_PAGE_ID_OFFSET, NULL_PAGEID); \
    } \
  while (0)

#define QFILE_PUT_NEXT_VPID_NULL(ptr) \
  do \
    { \
      OR_PUT_SHORT ((ptr) + QFILE_NEXT_VOL_ID_OFFSET, NULL_VOLID); \
      OR_PUT_INT ((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, NULL_PAGEID); \
    } \
  while (0)

#define QFILE_PUT_OVERFLOW_VPID_NULL(ptr) \
  do \
    { \
      OR_PUT_SHORT ((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET, NULL_VOLID); \
      OR_PUT_INT ((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET, NULL_PAGEID); \
    } \
  while (0)

#define QFILE_COPY_VPID(ptr1, ptr2) \
  do \
    { \
      (ptr1)->pageid = (ptr2)->pageid; \
      (ptr1)->volid  = (ptr2)->volid; \
    } \
  while (0)

/* OVERFLOW PAGE CONSTANTS */

#define QFILE_OVERFLOW_TUPLE_PAGE_SIZE_OFFSET   12

#define QFILE_GET_OVERFLOW_TUPLE_PAGE_SIZE(ptr) \
  (int) OR_GET_INT ((ptr) + QFILE_OVERFLOW_TUPLE_PAGE_SIZE_OFFSET)

#define QFILE_PUT_OVERFLOW_TUPLE_PAGE_SIZE(ptr,val) \
  OR_PUT_INT ((ptr) + QFILE_OVERFLOW_TUPLE_PAGE_SIZE_OFFSET, (val))

/* QFILE_TUPLE CONSTANTS */

#define QFILE_MAX_TUPLE_SIZE_IN_PAGE  (DB_PAGESIZE - QFILE_PAGE_HEADER_SIZE)

/*
 * Tuple byte format (CBRD-27365, ADR 0016 section 1.1, PG MinimalTuple style):
 *
 *   [len 4B][prev_len 4B, backward capable lists only][null bitmap, has-null tuples only][pad][values][pad]
 *
 *   len       : network order; bit 31 = has-null, low 31 bits = tuple length including padding (multiple of 4)
 *   prev_len  : length of the previous tuple in the page (qfile_scan_prev / cursor_prev_tuple); present iff the
 *               list's type_list.hdr_size == 8
 *   bitmap    : ceil (type_cnt / 8) bytes, bit i = byte[i >> 3] & (1 << (i & 7)), 1 = bound, 0 = NULL
 *   values    : logical column order from data_off = ALIGN4 (hdr_size + bitmap size). NULL = 0 bytes.
 *               FIXED column: ALIGN (alignby in {2,4}) then disksize bytes of data_writeval (8-byte values are read
 *               by memcpy). VAR column: no alignment, 1-byte (<= 127) or 4-byte (bit 7 of the first byte set,
 *               ntohl & 0x7FFFFFFF) body length header, then the body (index_* encoding for string/BIT/NUMERIC,
 *               data_* encoding copied through an aligned scratch for the rest).
 * Every tuple start is 4-byte aligned (page header 32 bytes, every tuple length a multiple of 4).
 * The accessors and the assembler live in qfile_tuple_layout.h; nothing else interprets these bytes.
 */

#define QFILE_TUPLE_HDR_SIZE_FORWARD            4
#define QFILE_TUPLE_HDR_SIZE_BACKWARD           8
#define QFILE_TUPLE_LENGTH_OFFSET               0
#define QFILE_TUPLE_PREV_LENGTH_OFFSET          4
#define QFILE_TUPLE_ALIGNMENT                   INT_ALIGNMENT	/* D-180-3: tuple_alignby = 4 */

/* READERS/WRITERS FOR QFILE_TUPLE FIELDS */

#define QFILE_TUPLE_LENGTH_HAS_NULL_BIT         0x80000000
#define QFILE_TUPLE_LENGTH_MASK                 0x7FFFFFFF

#define QFILE_GET_TUPLE_LENGTH(tpl) \
  ((int) ((unsigned int) OR_GET_INT ((tpl) + QFILE_TUPLE_LENGTH_OFFSET) & QFILE_TUPLE_LENGTH_MASK))

#define QFILE_GET_TUPLE_HAS_NULL(tpl) \
  (((unsigned int) OR_GET_INT ((tpl) + QFILE_TUPLE_LENGTH_OFFSET) & QFILE_TUPLE_LENGTH_HAS_NULL_BIT) != 0)

#define QFILE_GET_PREV_TUPLE_LENGTH(tpl) \
  OR_GET_INT ((tpl) + QFILE_TUPLE_PREV_LENGTH_OFFSET)

/* the length word carries the has-null flag: written by the assembler and by the header rewrite of
 * qfile_add_tuple_to_list_from (); every other writer copies whole tuples and keeps the word as is */
#define QFILE_PUT_TUPLE_LENGTH(tpl,len,has_null) \
  OR_PUT_INT ((tpl) + QFILE_TUPLE_LENGTH_OFFSET, \
	      (int) ((unsigned int) (len) | ((has_null) ? QFILE_TUPLE_LENGTH_HAS_NULL_BIT : 0)))

#define QFILE_PUT_PREV_TUPLE_LENGTH(tpl,val) \
  OR_PUT_INT ((tpl) + QFILE_TUPLE_PREV_LENGTH_OFFSET,val)

/* null bitmap (D-180-2): 1 = bound */
#define QFILE_TUPLE_BITMAP(tpl, hdr_size)       ((const unsigned char *) (tpl) + (hdr_size))
#define QFILE_BITMAP_IS_BOUND(bm, i)            ((((const unsigned char *) (bm))[(i) >> 3] >> ((i) & 7)) & 1)
#define QFILE_BITMAP_SET_BOUND(bm, i)           (((unsigned char *) (bm))[(i) >> 3] |= (unsigned char) (1 << ((i) & 7)))

/* variable value length header (D-180-6) */
#define QFILE_VAR_HDR_LONG_BIT                  0x80
#define QFILE_VAR_HDR_SHORT_MAX                 127
#define QFILE_VAR_HDR_SIZE(len)                 ((len) <= QFILE_VAR_HDR_SHORT_MAX ? 1 : 4)

/* Special flag set in the TUPLE_CNT field to indicate an overflow page */
#define QFILE_OVERFLOW_TUPLE_COUNT_FLAG -2

/*
 *       		    QFILE_TUPLE FORMAT DEFINITIONS
 */

typedef char *QFILE_TUPLE;	/* list file tuple */

/* tuple record descriptor == tuple slot (CBRD-27365, ADR 0016 D-182-2).
 * The record keeps its historical owning/non-owning meaning (size > 0: private buffer owned by the record,
 * size == 0: tpl PEEKs into a list page). The slot fields bind the layout descriptor of the list the tuple
 * belongs to and cache the deform position (PG tts_nvalid/off); they are reset by qfile_slot_set_tuple (),
 * the only sanctioned way to point the record at another tuple (D-182-5). */
struct qfile_tuple_value_type_list;
typedef struct qfile_tuple_record QFILE_TUPLE_RECORD;
struct qfile_tuple_record
{
  char *tpl;			/* tuple pointer */
  int size;			/* area _allocated_ for tuple pointer */
  const struct qfile_tuple_value_type_list *tl;	/* layout descriptor, bound once per scan (D-182-6) */
  int16_t nvalid;		/* columns deformed so far (PG tts_nvalid); -1 = position cache not started for this tuple */
  int16_t fast_limit;		/* end of the constant-offset prefix for this tuple (D-182-4) */
  int16_t data_off;		/* tl->data_off[has_null] of this tuple */
  bool has_null;		/* has-null bit of this tuple's length word */
  int32_t off;			/* start offset (unaligned) of column nvalid, from tuple start (PG off) */
};

/* Per-column layout entry of the tuple layout descriptor (CBRD-27365, ADR 0016 D-181-3).
 * 8 bytes; PG CompactAttribute precedent. Growing it means revisiting D-181-3. */
typedef struct qfile_col_layout QFILE_COL_LAYOUT;
struct qfile_col_layout
{
  int16_t off;			/* constant offset from data_off; -1 when not cached (after the first VAR column or > INT16_MAX) */
  int16_t size;			/* FIXED: disksize (max 12). VAR: -1 */
  uint8_t kind;			/* QFILE_COL_FIXED | QFILE_COL_VAR */
  uint8_t var_access;		/* VAR only: QFILE_VAR_DIRECT | QFILE_VAR_SCRATCH */
  uint8_t alignby;		/* FIXED: 2 | 4. VAR: 1 */
  uint8_t _pad;
};

/* Type list structure == tuple layout descriptor (CBRD-27365, ADR 0016 D-181-1/2/5).
 *
 * Two states. An INPUT type list (locals built by the executor before qfile_open_list) only fills domp/type_cnt
 * and has finalized == false; the descriptor fields below are not read. A FINALIZED type list (every
 * QFILE_LIST_ID) was allocated by qfile_type_list_alloc () as ONE block [domp[type_cnt] | col[type_cnt]]
 * (so the existing free (domp) sites are untouched) and had qfile_type_list_finalize () run after its last
 * domp mutation (mutator-owns-finalize, D-181-6). Copies inherit the block by memcpy (qfile_type_list_copy).
 *
 * The descriptor IS the layout: kind/size/alignby of every column come from domp[] (qfile_type_list_finalize),
 * hdr_size from the QFILE_FLAG_BACKWARD flag of qfile_open_list () (D-181-8). */
typedef struct qfile_tuple_value_type_list QFILE_TUPLE_VALUE_TYPE_LIST;
struct qfile_tuple_value_type_list
{
  TP_DOMAIN **domp;		/* array of column domains; head of the [domp | col] block when finalized */
  int type_cnt;			/* number of data types */
  QFILE_COL_LAYOUT *col;	/* == (QFILE_COL_LAYOUT *) (domp + type_cnt); convenience pointer, not a separate allocation */
  int first_non_cached_col;	/* min (first VAR column, first column with off > INT16_MAX); type_cnt if none */
  int16_t data_off[2];		/* [0] = no-null, [1] = has-null : ALIGN4 (hdr_size + bitmap) */
  int16_t bitmap_size;		/* (type_cnt + 7) >> 3 */
  uint8_t hdr_size;		/* 4 | 8 ; 8 <=> backward capable (D-181-8, the only truth) */
  bool finalized;
};

/* QFILE_COL_LAYOUT.kind / .var_access */
enum
{
  QFILE_COL_FIXED = 0,
  QFILE_COL_VAR = 1
};
enum
{
  QFILE_VAR_DIRECT = 0,
  QFILE_VAR_SCRATCH = 1
};

/* tuple value position descriptor */
typedef struct qfile_tuple_value_position QFILE_TUPLE_VALUE_POSITION;
struct qfile_tuple_value_position
{
  TP_DOMAIN *dom;		/* value domain */
  TP_DOMAIN *original_domain;	/* original domain */
  int pos_no;			/* value position number */
};

/*
 *                          SCAN FETCH MODE
 */

typedef enum
{
  QPROC_NO_SINGLE_INNER = 0,	/* 0 or n qualified rows */
  QPROC_SINGLE_INNER,		/* 0 or 1 qualified row - currently, not used */
  QPROC_SINGLE_OUTER,		/* 1 NULL row or 1 qualified row */
  QPROC_NO_SINGLE_OUTER		/* 1 NULL row or n qualified rows */
} QPROC_SINGLE_FETCH;

/* List File Merge Information */
typedef struct qfile_list_merge_info QFILE_LIST_MERGE_INFO;
struct qfile_list_merge_info
{
  JOIN_TYPE join_type;		/* inner, left, right or outer */
  QPROC_SINGLE_FETCH single_fetch;	/* merge in single fetch mode */
  int ls_column_cnt;		/* join columns count */
  int ls_pos_cnt;		/* tuple value fetch count */
  int *ls_outer_column;		/* outer list join columns number */
  int *ls_outer_unique;		/* outer column values unique?
				 * currently, not used */
  int *ls_inner_column;		/* inner list join columns number */
  int *ls_inner_unique;		/* inner column values unique?
				 * currently, not used */
  int *ls_outer_inner_list;	/* outer/inner list indicators */
  int *ls_pos_list;		/* tuple value positions */
};

typedef enum
{
  T_UNKNOWN,			/* uninitialized: not used */
  T_NORMAL,			/* f_valp[]: one DB_VALUE per column (tuple descriptor path) */
  T_COL_SRC			/* col_src[]: per-column sources (sort key output, merge output, raw item, counters) */
} QFILE_TUPLE_TYPE;

/*
 * Tuple assembler column source (CBRD-27365 D-182-11). One entry per output column.
 *   val != NULL : encode the DB_VALUE with its type's data_writeval
 *   val == NULL : copy data[0..len) verbatim as the stored body (what qfile_slot_locate () returned for a column of
 *                 the same domain; the source and destination columns must share the layout kind)
 * is_null makes the column NULL regardless of val/data. qfile_tuple_size () writes the disk size of a val source
 * into len so qfile_tuple_fill () does not compute it again.
 */
typedef struct qfile_tuple_col_src QFILE_TUPLE_COL_SRC;
struct qfile_tuple_col_src
{
  const DB_VALUE *val;
  const char *data;
  int len;
  bool is_null;
};

/* tuple descriptor: the per-list staging area for qfile_generate_tuple_into_list () */
typedef struct qfile_tuple_descriptor QFILE_TUPLE_DESCRIPTOR;
struct qfile_tuple_descriptor
{
  int tpl_size;			/* exact tuple size, from the assembler size pass */
  bool has_null;		/* size pass output, consumed by the fill pass */

  /* T_NORMAL */
  int f_cnt;			/* number of field */
  DB_VALUE **f_valp;		/* pointer of field value pointer array (owned by the list) */

  /* T_COL_SRC */
  QFILE_TUPLE_COL_SRC *col_src;	/* owned by the list; grown on demand by qfile_tpl_descr_col_src () */
  int col_src_cap;
  int col_src_cnt;
};

/*
 *       	      SORTING RELATED DEFINITIONS
 */

typedef enum
{
  SORT_TEMP = 0,
  SORT_GROUPBY,
  SORT_ORDERBY,
  SORT_DISTINCT,
  SORT_LIMIT
} SORT_TYPE;

typedef enum
{
  S_ASC = 1,
  S_DESC
} SORT_ORDER;

typedef enum
{
  S_NULLS_FIRST = 1,
  S_NULLS_LAST
} SORT_NULLS;

typedef struct sort_list SORT_LIST;
struct sort_list
{
  UINT64 del_id;		/* for latch-free freelist */
  struct sort_list *local_next;	/* for latch-free freelist */
  struct sort_list *next;	/* Next sort item */
  QFILE_TUPLE_VALUE_POSITION pos_descr;	/* Value position descriptor */
  SORT_ORDER s_order;		/* Ascending/Descending Order */
  SORT_NULLS s_nulls;		/* NULLS as First/Last position */
};				/* Sort item list */

/*
 *       		     LIST FILE DEFINITIONS
 */

typedef struct qfile_list_id QFILE_LIST_ID;
struct qfile_list_id
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;	/* data type of each column */
  SORT_LIST *sort_list;		/* sort info of each column */
  INT64 tuple_cnt;		/* total number of tuples in the file */
  int page_cnt;			/* total number of pages in the list file */
  VPID first_vpid;		/* first real page identifier */
  VPID last_vpid;		/* last real page identifier */
  PAGE_PTR last_pgptr;		/* last page pointer */
  int last_offset;		/* mark current end of last page */
  int lasttpl_len;		/* length of the last tuple file identifier NOTE: A tuple can be larger than one page
				 * therefore, this field must be int instead of a short value */
  QUERY_ID query_id;		/* Associated Query Id */
  VFID temp_vfid;		/* temp file id; duplicated from tfile_vfid */
  struct qmgr_temp_file *tfile_vfid;	/* Create a tmp file per list */
  QFILE_TUPLE_DESCRIPTOR tpl_descr;	/* tuple descriptor */
  bool is_domain_resolved;	/* domains for host var is resolved or not */
  bool is_result_cached;	/* for subquery result cache */
  QFILE_LIST_ID *dependent_list_id;	/* Linked as dependent by qfile_connect_list; cleared together. */
};

#define QFILE_CLEAR_LIST_ID(list_id) \
  do \
    { \
      (list_id)->type_list.type_cnt = 0; \
      (list_id)->type_list.domp = NULL; \
      (list_id)->type_list.col = NULL; \
      (list_id)->type_list.first_non_cached_col = 0; \
      (list_id)->type_list.data_off[0] = 0; \
      (list_id)->type_list.data_off[1] = 0; \
      (list_id)->type_list.bitmap_size = 0; \
      (list_id)->type_list.hdr_size = 0; \
      (list_id)->type_list.finalized = false; \
      (list_id)->sort_list = NULL; \
      (list_id)->tuple_cnt = 0; \
      (list_id)->page_cnt = 0; \
      (list_id)->first_vpid.pageid = NULL_PAGEID; \
      (list_id)->first_vpid.volid  = NULL_VOLID; \
      (list_id)->last_vpid.pageid = NULL_PAGEID; \
      (list_id)->last_vpid.volid  = NULL_VOLID; \
      (list_id)->last_pgptr = NULL; \
      (list_id)->last_offset = QFILE_NULL_PAGE_OFFSET; \
      (list_id)->lasttpl_len = 0; \
      (list_id)->query_id = 0; \
      (list_id)->temp_vfid.fileid = NULL_PAGEID; \
      (list_id)->temp_vfid.volid = NULL_VOLID; \
      (list_id)->tfile_vfid = NULL; \
      (list_id)->tpl_descr.tpl_size = 0; \
      (list_id)->tpl_descr.has_null = false; \
      (list_id)->tpl_descr.f_cnt = 0; \
      (list_id)->tpl_descr.f_valp = NULL; \
      (list_id)->tpl_descr.col_src = NULL; \
      (list_id)->tpl_descr.col_src_cap = 0; \
      (list_id)->tpl_descr.col_src_cnt = 0; \
      (list_id)->is_domain_resolved = false; \
      (list_id)->is_result_cached = false; \
      (list_id)->dependent_list_id = NULL; \
    } \
  while (0)

/* Tuple position structure */
typedef struct qfile_tuple_position QFILE_TUPLE_POSITION;
struct qfile_tuple_position
{
  SCAN_STATUS status;		/* Scan status */
  SCAN_POSITION position;	/* Scan position */
  VPID vpid;			/* Real tuple page identifier */
  int offset;			/* Tuple offset inside the page */
  QFILE_TUPLE tpl;		/* Tuple pointer inside the page */
  int tplno;			/* Tuple number inside the page */
};

#define QFILE_OUTER_LIST  0	/* outer list file indicator */
#define QFILE_INNER_LIST  1	/* inner list file indicator */

/* List File Scan Identifier */
typedef struct qfile_list_scan_id QFILE_LIST_SCAN_ID;
struct qfile_list_scan_id
{
  SCAN_STATUS status;		/* Scan Status */
  SCAN_POSITION position;	/* Scan Position */
  VPID curr_vpid;		/* current real page identifier */
  PAGE_PTR curr_pgptr;		/* current page pointer */
  QFILE_TUPLE curr_tpl;		/* current tuple pointer */
  bool keep_page_on_finish;	/* flag; when set, does not free page when scan ends */
  bool is_read_only;		/* flag; when set, does not latch write */
  int curr_offset;		/* current page offset */
  int curr_tplno;		/* current tuple number */
  QFILE_TUPLE_RECORD tplrec;	/* used for overflow tuple peeking */
  QFILE_LIST_ID list_id;	/* list file identifier */
};

/* list file flag; denoting type and/or operation of the list file */
enum
{
  QFILE_FLAG_RESULT_FILE = 0x0001,
  QFILE_FLAG_UNION = 0x0010,
  QFILE_FLAG_INTERSECT = 0x0020,
  QFILE_FLAG_DIFFERENCE = 0x0040,
  QFILE_FLAG_ALL = 0x0100,
  QFILE_FLAG_DISTINCT = 0x0200,
  QFILE_FLAG_USE_KEY_BUFFER = 0x0400,
  QFILE_NOT_USE_MEMBUF = 0x0800,
  QFILE_FLAG_BACKWARD = 0x1000	/* list may be scanned backward (qfile_scan_prev / cursor_prev_tuple), #184 A/B/C:
				 * its tuples carry the 8-byte [len][prev_len] header (type_list.hdr_size, D-181-8) */
};

/* hdr_size is the only truth about backward capability (D-181-8) */
#define QFILE_LIST_IS_BACKWARD(list_id)    ((list_id)->type_list.hdr_size == QFILE_TUPLE_HDR_SIZE_BACKWARD)
/* qfile_open_list () flag that gives a new list the tuple header of an existing one (raw tuple copies between the
 * two then need no header rewrite, see qfile_add_tuple_to_list_from) */
#define QFILE_LIST_BACKWARD_FLAG(list_id)  (QFILE_LIST_IS_BACKWARD (list_id) ? QFILE_FLAG_BACKWARD : 0)

#define QFILE_SET_FLAG(var, flag)          ((var) |= (flag))
#define QFILE_CLEAR_FLAG(var, flag)        ((var) &= (flag))
#define QFILE_IS_FLAG_SET(var, flag)       ((var) & (flag))
#define QFILE_IS_FLAG_SET_BOTH(var, flag1, flag2) (((var) & (flag1)) && ((var) & (flag2)))

#ifdef __cplusplus
/* Sector-based data page info for QFILE_LIST_ID.
 * membuf_tfile: membuf exists only in the first list_id (not in dependent_list_id).
 * sectors/tfiles: parallel arrays, one entry per disk sector across all dependent list_ids. */
typedef struct qfile_list_sector_info QFILE_LIST_SECTOR_INFO;
struct qfile_list_sector_info
{
  // *INDENT-OFF*
  struct qmgr_temp_file *membuf_tfile;	/* tfile owning membuf pages (NULL = none) */
  struct file_partial_sector *sectors;	/* data page sectors (FTAB excluded) */
  void **tfiles;			/* parallel array: tfile per sector */
  int sector_cnt;

  qfile_list_sector_info ()
    : membuf_tfile (NULL)
    , sectors (NULL)
    , tfiles (NULL)
    , sector_cnt (0)
  {
    //
  }

  // *INDENT-ON*
};
#endif /*  __cplusplus */

#ifdef __cplusplus
/* Sector-based parallel page scan distribution state.
 * Wraps QFILE_LIST_SECTOR_INFO with the atomic cursors workers use to coordinate. */
typedef struct qfile_list_sector_scan_info QFILE_LIST_SECTOR_SCAN_INFO;
struct qfile_list_sector_scan_info
{
  // *INDENT-OFF*
  QFILE_LIST_SECTOR_INFO sector_info;	/* sector layout (from qfile_collect_list_sector_info) */
  std::atomic<bool> membuf_claimed;	/* atomic flag: one worker claims all membuf pages */
  std::atomic<int> next_sector_index;	/* atomic cursor for sector distribution */

  qfile_list_sector_scan_info ()
    : sector_info ()
    , membuf_claimed (false)
    , next_sector_index (0)
  {
    //
  }
  // *INDENT-ON*
};
#endif /*  __cplusplus */

#ifdef __cplusplus
/*
 * sector_page_iterator
 *
 * Per-thread sector-based page iterator over a QFILE_LIST_ID's data pages.
 * Phase 1: one worker (the CAS winner of membuf_claimed) iterates the
 *          membuf region sequentially.
 * Phase 2: all workers split disk pages by atomically claiming sectors
 *          via next_sector_index and walking each sector's bitmap.
 */
// *INDENT-OFF*
class sector_page_iterator
{
  public:
    sector_page_iterator ();

    PAGE_PTR get_next_page (THREAD_ENTRY *thread_p, QFILE_LIST_SECTOR_SCAN_INFO &sector_scan);

    inline struct qmgr_temp_file *get_current_tfile () const { return m_current_tfile; }
    inline VPID get_current_vpid () const { return m_last_vpid; }

  private:
    int m_membuf_index;		/* -1 = initial, >=0 = CAS winner iterating membuf, -2 = not winner */
    int m_sector_index;
    UINT64 m_current_bitmap;
    VSID m_current_vsid;
    VPID m_last_vpid;		/* VPID of the last returned page */
    struct qmgr_temp_file *m_current_tfile;
};
// *INDENT-ON*
#endif /* __cplusplus */

/* SORTING RELATED DEFINITIONS */

/* Sorted list identifier */
typedef struct qfile_sorted_list_id QFILE_SORTED_LIST_ID;
struct qfile_sorted_list_id
{
  QFILE_LIST_ID *list_id;	/* List File identifier */
  int sorted;			/* Has file already been sorted? */
};

/* Sorting Scan Identifier */
typedef struct qfile_sort_scan_id QFILE_SORT_SCAN_ID;
struct qfile_sort_scan_id
{
  QFILE_LIST_SCAN_ID *s_id;	/* Scan Identifier */
  QFILE_TUPLE_RECORD tplrec;	/* Tuple Descriptor used for sorting */
};


typedef enum
{
  SYNC_EXEC = 0,
  DEFAULT_EXEC_MODE = SYNC_EXEC
} QUERY_EXEC_MODE;

enum
{
  NOT_FROM_RESULT_CACHE = 0x1 << 0,
  RESULT_CACHE_REQUIRED = 0x1 << 1,
  RESULT_CACHE_INHIBITED = 0x1 << 2,
  RESULT_HOLDABLE = 0x1 << 3,
  DONT_COLLECT_EXEC_STATS = 0x1 << 4,
  MRO_CANDIDATE = 0x1 << 5,
  MRO_IS_USED = 0x1 << 6,
  SORT_LIMIT_CANDIDATE = 0x1 << 7,
  SORT_LIMIT_USED = 0x1 << 8,
  XASL_TRACE_TEXT = 0x1 << 9,
  XASL_TRACE_JSON = 0x1 << 10,
  TRIGGER_IS_INVOLVED = 0x1 << 11,
  RETURN_GENERATED_KEYS = 0x1 << 12,
  XASL_CACHE_PINNED_REFERENCE = 0x1 << 13,
  EXECUTE_QUERY_WITHOUT_DATA_BUFFERS = 0x1 << 14,
  EXECUTE_QUERY_WITH_COMMIT = 0x1 << 15,
  TRAN_AUTO_COMMIT = 0x1 << 16,
  LIKE_RECOMPILE_CANDIDATE = 0x1 << 17,
  HV_PRED_PLAN_UNPEEKED = 0x1 << 18	/* plan built with unbound host-var predicate markers; the
					 * first execution replans under the actual bind values */
};

#define DO_NOT_COLLECT_EXEC_STATS(flag)    ((flag) & DONT_COLLECT_EXEC_STATS)

#define IS_XASL_TRACE_TEXT(flag)    (((flag) & XASL_TRACE_TEXT) != 0)
#define IS_XASL_TRACE_JSON(flag)    (((flag) & XASL_TRACE_JSON) != 0)

#define IS_TRIGGER_INVOLVED(flag)   (((flag) & TRIGGER_IS_INVOLVED) != 0)

#define IS_XASL_CACHE_PINNED_REFERENCE(flag)   (((flag) & XASL_CACHE_PINNED_REFERENCE) != 0)
#define IS_QUERY_EXECUTED_WITHOUT_DATA_BUFFERS(flag)   (((flag) & EXECUTE_QUERY_WITHOUT_DATA_BUFFERS) != 0)
#define IS_QUERY_EXECUTE_WITH_COMMIT(flag)   (((flag) & EXECUTE_QUERY_WITH_COMMIT) != 0)
#define IS_TRAN_AUTO_COMMIT(flag)   (((flag) & TRAN_AUTO_COMMIT) != 0)

typedef int QUERY_FLAG;

#endif /* _QUERY_LIST_H_ */
