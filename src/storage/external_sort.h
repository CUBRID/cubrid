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
 * external_sort.h - External sorting module
 */

#ifndef _EXTERNAL_SORT_H_
#define _EXTERNAL_SORT_H_

#ident "$Id$"

#include "error_manager.h"
#include "storage_common.h"
#include "query_list.h"
#include "thread.h"

#define SORT_PUT_STOP     2

#define NO_SORT_LIMIT (-1)

#define SORT_RECORD_LENGTH_SIZE (sizeof(INT64))	/* for 8byte align */
#define SORT_RECORD_LENGTH(item_p) (*((int *) ((item_p) - SORT_RECORD_LENGTH_SIZE)))

typedef enum
{
  SORT_REC_DOESNT_FIT,
  SORT_SUCCESS,
  SORT_NOMORE_RECS,
  SORT_ERROR_OCCURRED
} SORT_STATUS;

typedef enum
{
  SORT_ELIM_DUP,		/* eliminate duplicate */
  SORT_DUP			/* allow duplicate */
} SORT_DUP_OPTION;

typedef SORT_STATUS SORT_GET_FUNC (THREAD_ENTRY * thread_p, RECDES *, void *);
typedef int SORT_PUT_FUNC (THREAD_ENTRY * thread_p, const RECDES *, void *);
typedef int SORT_CMP_FUNC (const void *, const void *, void *);

typedef struct SORT_REC SORT_REC;
typedef struct SUBKEY_INFO SUBKEY_INFO;
typedef struct SORTKEY_INFO SORTKEY_INFO;
typedef struct SORT_INFO SORT_INFO;

struct SORT_REC
{
  SORT_REC *next;		/* forward link for duplicate sort_key value */
  union
  {
    /* Bread crumbs back to the original tuple, so that we can go
       straight there after the keys have been sorted. */
    struct
    {
      INT32 pageid;		/* Page identifier */
      INT16 volid;		/* Volume identifier */
      INT16 offset;		/* offset in page */
      char body[1];		/* sort_key body start position */
    } original;

    /*
     * The offset vector.  A value of zero for an entry means that the
     * corresponding column is null, and that there are no data bytes for
     * the column.  A non-zero entry is interpreted as the offset from
     * the *start* of the SORT_REC to the data bytes for that column.
     */
    int offset[1];
  } s;
};

struct SUBKEY_INFO
{
  /* The actual column number in the list file tuple. */
  int col;


  int permuted_col;

  TP_DOMAIN *col_dom;

  TP_DOMAIN *cmp_dom;		/* for median sorting string
				 * in different domain
				 */

  int (*sort_f) (void *tplp1, void *tplp2, TP_DOMAIN * dom,
		 int do_coercion, int total_order, int *start_col);

  /*
   * Non-zero iff the sort on this column is descending.  Factoring
   * this decision out of the actual sort function allows to use only
   * one of those guys, at no particularly great cost in performance,
   * and a big win in maintainability.
   */
  int is_desc;

  int is_nulls_first;

  bool use_cmp_dom;		/* when true, use cmp_dom to make comparing */
};

struct SORTKEY_INFO
{
  int nkeys;			/* The number of columns in use today. */
  int use_original;		/* False iff the sort keys consist of all of
				   the input record fields, i.e., if we'll
				   reconstruct the input records from the keys
				   rather than look them up again in the
				   original file. */
  SUBKEY_INFO *key;		/* Points to `default_keys' if `nkeys' <= 8;
				   otherwise it points to malloc'ed space. */
  SUBKEY_INFO default_keys[8];	/* Default storage; this ought to work for
				   most cases. */
  int error;			/* median domain convert errors */
};

struct SORT_INFO
{
  SORTKEY_INFO key_info;	/* All of the interesting key information. */
  QFILE_SORT_SCAN_ID *s_id;	/* A SCAN_ID for the input list file.  This is
				   stateful, and records the current location
				   of the scan between calls to
				   ls_sort_get_next(). */
  QFILE_LIST_ID *output_file;	/* The name of the output file.  This is where
				   ls_sort_put_next_*() deposits its stuff. */
  RECDES output_recdes;		/* A working buffer for output of tuples; used
				   only when we're using
				   ls_sort_put_next_short() as the output
				   function. */
  void *extra_arg;		/* extra information supplied by the caller */
};

extern int sort_listfile (THREAD_ENTRY * thread_p, INT16 volid,
			  int est_inp_pg_cnt, SORT_GET_FUNC * get_fn,
			  void *get_arg, SORT_PUT_FUNC * put_fn,
			  void *put_arg, SORT_CMP_FUNC * cmp_fn,
			  void *cmp_arg, SORT_DUP_OPTION option, int limit);

#endif /* _EXTERNAL_SORT_H_ */
