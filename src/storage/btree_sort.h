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

/* Every in-memory sort record is preceded by its length; the prefix is 8 bytes wide to keep records
 * MAX_ALIGNMENT-aligned. */
#define BTSORT_RECORD_LENGTH_SIZE (sizeof(INT64))	/* for 8byte align */
#define BTSORT_RECORD_LENGTH(item_p) (*((int *) ((item_p) - BTSORT_RECORD_LENGTH_SIZE)))

typedef enum
{
  BTSORT_REC_DOESNT_FIT,
  BTSORT_SUCCESS,
  BTSORT_NOMORE_RECS,
  BTSORT_ERROR_OCCURRED
} BTSORT_STATUS;

typedef BTSORT_STATUS BTSORT_GET_FUNC (THREAD_ENTRY * thread_p, RECDES *, void *);
typedef int BTSORT_PUT_FUNC (THREAD_ENTRY * thread_p, const RECDES *, void *);
typedef int BTSORT_CMP_FUNC (const void *, const void *, void *);

extern int btree_sort (THREAD_ENTRY * thread_p, BTSORT_GET_FUNC * get_fn, void *get_arg, BTSORT_PUT_FUNC * put_fn,
		       void *put_arg, BTSORT_CMP_FUNC * cmp_fn, void *cmp_arg, bool includes_tde_class);

#endif /* _BTREE_SORT_H_ */
