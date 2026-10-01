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
 * btree_sort.h - External sort dedicated to the b+tree bulk loader
 */

#ifndef _BTREE_SORT_H_
#define _BTREE_SORT_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "error_manager.h"
#include "storage_common.h"
#include "thread_compat.hpp"

typedef enum
{
  BTSORT_REC_DOESNT_FIT,
  BTSORT_SUCCESS,
  BTSORT_NOMORE_RECS,
  BTSORT_ERROR_OCCURRED
} BTSORT_STATUS;

/*
 * sort record starts with this 4-byte header: the loader (btree_load.c) fills key_off and the
 * flags, the sort fills the length. It travels with the record into the run pages.
 */
typedef struct btsort_rec_header BTSORT_REC_HEADER;
struct btsort_rec_header
{
  UINT16 length:14;		/* length of the record, header included */
  UINT16 is_bigone:1;		/* the record holds the VPID of a record of the multipage file */
  UINT16 has_null:1;		/* the key is NULL or a multi-column key with a NULL column */
  UINT8 key_off;		/* offset of the key from the start of the record */
  UINT8 flags;			/* BTSORT_REC_HAS_INSID | BTSORT_REC_HAS_DELID */
};
static_assert (sizeof (BTSORT_REC_HEADER) == 4, "the sort record header must stay 4 bytes wide");

#define BTSORT_REC_HEADER_SIZE      ((int) sizeof (BTSORT_REC_HEADER))
#define BTSORT_REC_MAX_LENGTH       0x3FFF	/* the length field of the header is 14 bits wide */
#define BTSORT_REC_HAS_INSID        ((UINT8) 0x40)	/* insert MVCCID stored (row not all-visible) */
#define BTSORT_REC_HAS_DELID        ((UINT8) 0x80)	/* delete MVCCID stored */

#define BTSORT_REC_HDR(rec)         ((BTSORT_REC_HEADER *) (rec))
#define BTSORT_REC_KEY(rec)         ((char *) (rec) + BTSORT_REC_HDR (rec)->key_off)
#define BTSORT_REC_BODY(rec)        ((char *) (rec) + BTSORT_REC_HEADER_SIZE)
#define BTSORT_REC_OID(rec)         BTSORT_REC_BODY (rec)	/* not on a BIGONE record */

typedef BTSORT_STATUS BTSORT_GET_FUNC (THREAD_ENTRY * thread_p, RECDES *, void *);
typedef int BTSORT_PUT_FUNC (THREAD_ENTRY * thread_p, const RECDES *, void *);

typedef struct sort_args SORT_ARGS;

extern int btree_sort (THREAD_ENTRY * thread_p, BTSORT_GET_FUNC * get_fn, SORT_ARGS * sort_args,
		       BTSORT_PUT_FUNC * put_fn, void *put_arg, bool includes_tde_class);

/*
 * In-memory run sorters (btree_sort_<name>.cpp)
 *
 * btsort_inphase_sort () starts a run with init () once, fills it with reserve ()/accept (), orders it
 * with sort (), reads it back with next () and starts the next one with reset (). The sorter owns the
 * buffer layout, and the driver reads only numrecs.
 */

/* record comparison (btsort_compare_records) */
typedef int BTSORT_CMP_FUNC (const void *, const void *, void *);

typedef struct btsort_run_sorter BTSORT_RUN_SORTER;

typedef struct btsort_run BTSORT_RUN;
struct btsort_run
{
  char *base;			/* sort buffer start */
  char *top;			/* sort buffer end (= output buffer) */
  BTSORT_CMP_FUNC *cmp_fn;
  void *cmp_arg;		/* SORT_ARGS */
  long numrecs;			/* records collected */

  /* sorter-private, kept here so a run needs no allocation */
  void *sorter_ctx;		/* what init () set aside, or NULL when the sorter keeps none */
  char *item_ptr;		/* next record position */
  char *slot_low;		/* lowest byte claimed above the records */
  char *slot_ptr;		/* next free slot, growing downward */

  /* the last sorted run, which next () reads and reset () keeps */
  const char *sorted;		/* sorted slots */
  long readrecs;		/* number of sorted slots */
  long cursor;			/* read position */
};

/* parameters end in '_' so that they do not replace the field names */
#define BTSORT_RUN_INIT(run, base_, top_, cmp_fn_, cmp_arg_) \
  do \
    { \
      memset ((run), 0, sizeof (*(run))); \
      (run)->base = (base_); \
      (run)->top = (top_); \
      (run)->cmp_fn = (cmp_fn_); \
      (run)->cmp_arg = (cmp_arg_); \
    } \
  while (0)

/* record offset from the sort buffer start */
typedef UINT32 BTSORT_RUN_SLOT;

/* bounded by the 32-bit slot offset */
#define BTSORT_RUN_MAX_BUFFERS ((int) (INT_MAX / DB_PAGESIZE))

struct btsort_run_sorter
{
  const char *name;

  /* once, after BTSORT_RUN_INIT (), to set the sorter's context up, if any, and start the first run as reset ()
   * does */
  void (*init) (BTSORT_RUN * run, const struct tp_domain * key_domain);

  /* starts a new run, and the last sorted run stays readable until accept () */
  void (*reset) (BTSORT_RUN * run);

  /* where to write the next record and how much (room), or NULL when the run is full */
  char *(*reserve) (BTSORT_RUN * run, int maxlen, int *room);

  /* adds the record written at reserve () */
  void (*accept) (BTSORT_RUN * run, int length);

  /* orders the run */
  int (*sort) (THREAD_ENTRY * thread_p, BTSORT_RUN * run);

  /* next record in order, or NULL at the end */
  const char *(*next) (BTSORT_RUN * run);
};

/* natural merge sort (btree_sort_merge.cpp) */
extern const BTSORT_RUN_SORTER btsort_merge_sorter;

#endif /* _BTREE_SORT_H_ */
