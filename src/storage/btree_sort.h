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

typedef BTSORT_STATUS BTSORT_GET_FUNC (THREAD_ENTRY * thread_p, RECDES *, void *);
typedef int BTSORT_PUT_FUNC (THREAD_ENTRY * thread_p, const RECDES *, void *);
typedef int BTSORT_CMP_FUNC (const void *, const void *, void *);

extern int btree_sort (THREAD_ENTRY * thread_p, BTSORT_GET_FUNC * get_fn, void *get_arg, BTSORT_PUT_FUNC * put_fn,
		       void *put_arg, BTSORT_CMP_FUNC * cmp_fn, void *cmp_arg, bool includes_tde_class);

#endif /* _BTREE_SORT_H_ */
