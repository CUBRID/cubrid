/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * List files shared
 */

#ifndef _QP_LIST_H_
#define _QP_LIST_H_

#ident "$Id$"

#include "common.h"
#include "object_representation.h"
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

#define IS_OUTER_JOIN_TYPE(t) \
  ((t) == JOIN_LEFT || (t) == JOIN_RIGHT || (t) == JOIN_OUTER)

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

/* XASL FILE IDENTIFICATION */

typedef struct xasl_id XASL_ID;
struct xasl_id
{
  VPID first_vpid;		/* first page real identifier */
  VFID temp_vfid;		/* temp file volume and file identifier */
};				/* XASL plan file identifier */

#define XASL_ID_SET_NULL(xasl_id) \
    do { \
        (xasl_id)->first_vpid.pageid = NULL_PAGEID;\
        (xasl_id)->first_vpid.volid  = NULL_VOLID;\
        (xasl_id)->temp_vfid.fileid = NULL_FILEID;\
        (xasl_id)->temp_vfid.volid = NULL_VOLID;\
    } while (0)

#define XASL_ID_IS_NULL(xasl_id) \
    ((xasl_id)->first_vpid.pageid == NULL_PAGEID)

#define XASL_ID_COPY(xasl_id1, xasl_id2) \
    *(xasl_id1) = *(xasl_id2)

#define XASL_ID_EQ(xasl_id1, xasl_id2) \
    ((xasl_id1) == (xasl_id2) || \
      /*VPID_EQ(&((xasl_id1)->first_vpid), &((xasl_id2)->first_vpid))*/ \
     ((xasl_id1)->first_vpid.pageid == (xasl_id2)->first_vpid.pageid && \
       (xasl_id1)->first_vpid.volid == (xasl_id2)->first_vpid.volid && \
      /*VFID_EQ(&((xasl_id1)->temp_vfid), &((xasl_id2)->temp_vfid))*/ \
       (xasl_id1)->temp_vfid.fileid == (xasl_id2)->temp_vfid.fileid && \
        (xasl_id1)->temp_vfid.volid == (xasl_id2)->temp_vfid.volid))

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

/* Invalid offset value to the page */
#define QFILE_NULL_PAGE_OFFSET          -1

/*
 *       		READERS/WRITERS FOR PAGE FIELDS                        
 */

#define QFILE_GET_TUPLE_COUNT(ptr) \
  OR_GET_INT( (ptr) + QFILE_TUPLE_COUNT_OFFSET )

#define QFILE_GET_PREV_PAGE_ID(ptr) \
  (PAGEID) OR_GET_INT( (ptr) + QFILE_PREV_PAGE_ID_OFFSET )

#define QFILE_GET_NEXT_PAGE_ID(ptr) \
  (PAGEID) OR_GET_INT( (ptr) + QFILE_NEXT_PAGE_ID_OFFSET )

#define QFILE_GET_LAST_TUPLE_OFFSET(ptr) \
  (PAGEID) OR_GET_INT( (ptr) + QFILE_LAST_TUPLE_OFFSET )

#define QFILE_GET_OVERFLOW_PAGE_ID(ptr) \
  (PAGEID) OR_GET_INT( (ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET )

#define QFILE_GET_PREV_VOLUME_ID(ptr) \
  (VOLID) OR_GET_SHORT((ptr) + QFILE_PREV_VOL_ID_OFFSET )

#define QFILE_GET_NEXT_VOLUME_ID(ptr) \
  (VOLID) OR_GET_SHORT((ptr) + QFILE_NEXT_VOL_ID_OFFSET )

#define QFILE_GET_OVERFLOW_VOLUME_ID(ptr) \
  (VOLID) OR_GET_SHORT((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET )

#define QFILE_GET_PREV_VPID(des,ptr) \
  do { \
    (des)->pageid = (PAGEID) OR_GET_INT( (ptr) + QFILE_PREV_PAGE_ID_OFFSET ); \
    (des)->volid = (VOLID) OR_GET_SHORT( (ptr) + QFILE_PREV_VOL_ID_OFFSET); \
  } while(0)

#define QFILE_GET_NEXT_VPID(des,ptr) \
  do { \
    (des)->pageid = (PAGEID) OR_GET_INT( (ptr) + QFILE_NEXT_PAGE_ID_OFFSET ); \
    (des)->volid = (VOLID) OR_GET_SHORT( (ptr) + QFILE_NEXT_VOL_ID_OFFSET); \
  } while(0)

#define QFILE_GET_OVERFLOW_VPID(des,ptr) \
  do { \
    (des)->pageid = (PAGEID) OR_GET_INT( (ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET ); \
    (des)->volid = (VOLID) OR_GET_SHORT( (ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET); \
  } while(0)

#define QFILE_PUT_TUPLE_COUNT(ptr,val) \
   OR_PUT_INT( (ptr) + QFILE_TUPLE_COUNT_OFFSET, (val) )

#define QFILE_PUT_PREV_PAGE_ID(ptr,val) \
   OR_PUT_INT( (ptr) + QFILE_PREV_PAGE_ID_OFFSET, (val) )

#define QFILE_PUT_NEXT_PAGE_ID(ptr,val) \
   OR_PUT_INT( (ptr) + QFILE_NEXT_PAGE_ID_OFFSET, (val) )

#define QFILE_PUT_LAST_TUPLE_OFFSET(ptr,val) \
   OR_PUT_INT( (ptr) + QFILE_LAST_TUPLE_OFFSET, (val) )

#define QFILE_PUT_OVERFLOW_PAGE_ID(ptr,val) \
   OR_PUT_INT( (ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET, (val) )

#define QFILE_PUT_PREV_VOLUME_ID(ptr,val) \
   OR_PUT_SHORT( (ptr) + QFILE_PREV_VOL_ID_OFFSET, (val) )

#define QFILE_PUT_NEXT_VOLUME_ID(ptr,val) \
   OR_PUT_SHORT( (ptr) + QFILE_NEXT_VOL_ID_OFFSET, (val) )

#define QFILE_PUT_OVERFLOW_VOLUME_ID(ptr,val) \
   OR_PUT_SHORT( (ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET, (val) )

#define QFILE_PUT_PREV_VPID(ptr,vpid) \
  do { \
    OR_PUT_INT((ptr) + QFILE_PREV_PAGE_ID_OFFSET, (vpid)->pageid); \
    OR_PUT_SHORT((ptr) + QFILE_PREV_VOL_ID_OFFSET, (vpid)->volid); \
  } while(0)

#define QFILE_PUT_NEXT_VPID(ptr,vpid) \
  do { \
    OR_PUT_INT((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, (vpid)->pageid); \
    OR_PUT_SHORT((ptr) + QFILE_NEXT_VOL_ID_OFFSET, (vpid)->volid); \
  } while(0)

#define QFILE_PUT_OVERFLOW_VPID(ptr,vpid) \
  do { \
    OR_PUT_INT((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET, (vpid)->pageid); \
    OR_PUT_SHORT((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET, (vpid)->volid); \
  } while(0)

#define QFILE_PUT_PREV_VPID_NULL(ptr) \
  do { \
    OR_PUT_INT((ptr) + QFILE_PREV_PAGE_ID_OFFSET, NULL_PAGEID); \
    OR_PUT_SHORT((ptr) + QFILE_PREV_VOL_ID_OFFSET, NULL_VOLID); \
  } while(0)

#define QFILE_PUT_NEXT_VPID_NULL(ptr) \
  do { \
    OR_PUT_INT((ptr) + QFILE_NEXT_PAGE_ID_OFFSET, NULL_PAGEID); \
    OR_PUT_SHORT((ptr) + QFILE_NEXT_VOL_ID_OFFSET, NULL_VOLID); \
  } while(0)

#define QFILE_PUT_OVERFLOW_VPID_NULL(ptr) \
  do { \
    OR_PUT_INT((ptr) + QFILE_OVERFLOW_PAGE_ID_OFFSET, NULL_PAGEID); \
    OR_PUT_SHORT((ptr) + QFILE_OVERFLOW_VOL_ID_OFFSET, NULL_VOLID); \
  } while(0)

#define QFILE_COPY_VPID(ptr1, ptr2) \
  do { \
    (ptr1)->pageid = (ptr2)->pageid;\
    (ptr1)->volid  = (ptr2)->volid; \
  } while(0)

/* XASL TREE STORAGE CONSTANTS */

#define QFILE_XASL_PAGE_SIZE_OFFSET     12

#define QFILE_GET_XASL_PAGE_SIZE(ptr) \
  (int) OR_GET_INT( (ptr) + QFILE_XASL_PAGE_SIZE_OFFSET )

#define QFILE_PUT_XASL_PAGE_SIZE(ptr,val) \
  OR_PUT_INT( (ptr) + QFILE_XASL_PAGE_SIZE_OFFSET, (val) )

/* OVERFLOW PAGE CONSTANTS */

#define QFILE_OVERFLOW_TUPLE_PAGE_SIZE_OFFSET   12

#define QFILE_GET_OVERFLOW_TUPLE_PAGE_SIZE(ptr) \
  (int) OR_GET_INT( (ptr) + QFILE_OVERFLOW_TUPLE_PAGE_SIZE_OFFSET )

#define QFILE_PUT_OVERFLOW_TUPLE_PAGE_SIZE(ptr,val) \
  OR_PUT_INT( (ptr) + QFILE_OVERFLOW_TUPLE_PAGE_SIZE_OFFSET, (val) )

/* QFILE_TUPLE CONSTANTS */

#define QFILE_MAX_TUPLE_SIZE_IN_PAGE  (DB_PAGESIZE - QFILE_PAGE_HEADER_SIZE)

/* Each tuple start is aligned with MAX_ALIGN 
 * Each tuple value header is aligned with MAX_ALIGN,
 * Each tuple value is aligned with MAX_ALIGN 
 */

#define QFILE_TUPLE_LENGTH_SIZE                 8
#define QFILE_TUPLE_LENGTH_OFFSET               0
#define QFILE_TUPLE_PREV_LENGTH_OFFSET          4

#define QFILE_TUPLE_VALUE_HEADER_LENGTH         8
#define QFILE_TUPLE_VALUE_HEADER_SIZE           8
#define QFILE_TUPLE_VALUE_FLAG_SIZE             4
#define QFILE_TUPLE_VALUE_LENGTH_SIZE           4

#define QFILE_TUPLE_VALUE_FLAG_OFFSET           0
#define QFILE_TUPLE_VALUE_LENGTH_OFFSET         4

/* READERS/WRITERS FOR QFILE_TUPLE FIELDS */

#define QFILE_GET_TUPLE_LENGTH(tpl) \
  OR_GET_INT((tpl) + QFILE_TUPLE_LENGTH_OFFSET)

#define QFILE_GET_PREV_TUPLE_LENGTH(tpl) \
  OR_GET_INT((tpl) + QFILE_TUPLE_PREV_LENGTH_OFFSET)

#define QFILE_PUT_TUPLE_LENGTH(tpl,val) \
  OR_PUT_INT((tpl) + QFILE_TUPLE_LENGTH_OFFSET,val)

#define QFILE_PUT_PREV_TUPLE_LENGTH(tpl,val) \
  OR_PUT_INT((tpl) + QFILE_TUPLE_PREV_LENGTH_OFFSET,val)

#define QFILE_GET_TUPLE_VALUE_FLAG(ptr) \
  (QFILE_TUPLE_VALUE_FLAG) OR_GET_INT( (ptr) + QFILE_TUPLE_VALUE_FLAG_OFFSET )

#define QFILE_GET_TUPLE_VALUE_LENGTH(ptr) \
  (int) OR_GET_INT( (ptr) + QFILE_TUPLE_VALUE_LENGTH_OFFSET )

#define QFILE_PUT_TUPLE_VALUE_FLAG(ptr,val) \
  OR_PUT_INT((ptr) + QFILE_TUPLE_VALUE_FLAG_OFFSET, (val))

#define QFILE_PUT_TUPLE_VALUE_LENGTH(ptr,val) \
  OR_PUT_INT((ptr) + QFILE_TUPLE_VALUE_LENGTH_OFFSET, (val))

#define QFILE_GET_TUPLE_VALUE_HEADER_POSITION(tpl,ind,valp) \
  do { \
    int _k; \
    (valp) = (char*)(tpl) + QFILE_TUPLE_LENGTH_SIZE; \
    for (_k = 0; _k < (ind); _k++) \
      (valp) += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH((valp)); \
  } while(0)

/* Special flag set in the TUPLE_CNT field to indicate an overflow page */
#define QFILE_OVERFLOW_TUPLE_COUNT_FLAG -2

/*
 *       		    QFILE_TUPLE FORMAT DEFINITIONS
 */

typedef char *QFILE_TUPLE;	/* list file tuple */

/* tuple record descriptor */
typedef struct qfile_tuple_record QFILE_TUPLE_RECORD;
struct qfile_tuple_record
{
  int size;			/* area _allocated_ for tuple pointer */
  char *tpl;			/* tuple pointer */
};

typedef enum
{
  V_BOUND = 1,
  V_UNBOUND
} QFILE_TUPLE_VALUE_FLAG;

/* tuple value header */
typedef struct qfile_tuple_value_header QFILE_TUPLE_VALUE_HEADER;
struct qfile_tuple_value_header
{
  QFILE_TUPLE_VALUE_FLAG val_flag;	/* V_BOUND/V_UNBOUND? */
  int val_len;			/* length of tuple value */
};

/* Type list structure */
typedef struct qfile_tuple_value_type_list QFILE_TUPLE_VALUE_TYPE_LIST;
struct qfile_tuple_value_type_list
{
  int type_cnt;			/* number of data types */
  TP_DOMAIN **domp;		/* array of column domains */
};

/* tuple value position descriptor */
typedef struct qfile_tuple_value_position QFILE_TUPLE_VALUE_POSITION;
struct qfile_tuple_value_position
{
  int pos_no;			/* value position number */
  TP_DOMAIN *dom;		/* value domain */
};

/* List File Merge Information */
typedef struct qfile_list_merge_info QFILE_LIST_MERGE_INFO;
struct qfile_list_merge_info
{
  JOIN_TYPE join_type;		/* inner, left, right or outer */
  QPROC_SINGLE_FETCH single_fetch;	/* merge in single fetch mode */
  int ls_column_cnt;		/* join columns count */
  int *ls_outer_column;		/* outer list join columns number */
  int *ls_outer_unique;		/* outer column values unique? */
  /* currently, not used */
  int *ls_inner_column;		/* inner list join columns number */
  int *ls_inner_unique;		/* inner column values unique? */
  /* currently, not used */
  int ls_pos_cnt;		/* tuple value fetch count */
  int *ls_outer_inner_list;	/* outer/inner list indicators */
  int *ls_pos_list;		/* tuple value positions */
};

typedef enum
{
  T_UNKNOWN,			/* uninitialized: not used */
  T_SINGLE_BOUND_ITEM,		/* called by qfile_add_item_to_list() */
  T_NORMAL,			/* normal case */
  T_SORTKEY,			/* called by ls_sort_put_next() */
  T_MERGE			/* called by xs_add_mergetuple() */
} QFILE_TUPLE_TYPE;

/* tuple descriptor */
typedef struct qfile_tuple_descriptor QFILE_TUPLE_DESCRIPTOR;
struct qfile_tuple_descriptor
{
  /* T_SINGLE_BOUND_ITEM */
  char *item;			/* pointer of item (i.e, single bound field tuple) */
  int item_size;		/* item size */

  /* T_NORMAL */
  int tpl_size;			/* tuple size */
  int f_cnt;			/* number of field */
  DB_VALUE **f_valp;		/* pointer of field value pointer array */

  /* T_SORTKEY */
  void *sortkey_info;		/* casted pointer of (SORTKEY_INFO *) */
  void *sort_rec;		/* casted pointer of (SORT_REC *) */

  /* T_MERGE */
  QFILE_TUPLE_RECORD *tplrec1;	/* first tuple */
  QFILE_TUPLE_RECORD *tplrec2;	/* second tuple */
  QFILE_LIST_MERGE_INFO *merge_info;	/* tuple merge info */
};

/*
 *       	      SORTING RELATED DEFINITIONS
 */

typedef enum
{
  SORT_TEMP = 0,
  SORT_GROUPBY,
  SORT_ORDERBY,
  SORT_DISTINCT
} SORT_TYPE;

typedef enum
{
  S_ASC = 1,
  S_DESC
} SORT_ORDER;

typedef struct sort_list SORT_LIST;
struct sort_list
{
  struct sort_list *next;	/* Next sort item */
  QFILE_TUPLE_VALUE_POSITION pos_descr;	/* Value position descriptor */
  SORT_ORDER s_order;		/* Ascending/Descending Order */
};				/* Sort item list */

/*
 *       		     LIST FILE DEFINITIONS
 */

typedef struct qfile_list_id QFILE_LIST_ID;
struct qfile_list_id
{
  QFILE_TUPLE_VALUE_TYPE_LIST type_list;	/* data type of each column */
  SORT_LIST *sort_list;		/* sort info of each column */
  int tuple_cnt;		/* total number of tuples in the file */
  int page_cnt;			/* total number of pages in the list file */
  VPID first_vpid;		/* first real page identifier */
  VPID last_vpid;		/* last real page identifier */
  PAGE_PTR last_pgptr;		/* last page pointer */
  int last_offset;		/* mark current end of last page */
  int lasttpl_len;		/* length of the last tuple file identifier
				 * NOTE: A tuple can be larger than one page
				 *       therefore, this field must be int
				 *       instead of a short value
				 */
  int query_id;			/* Associated Query Id */
  VFID temp_vfid;		/* temp file id; duplicated from tfile_vfid */
  struct qmgr_temp_file *tfile_vfid;	/* Create a tmp file per list */
  QFILE_TUPLE_DESCRIPTOR tpl_descr;	/* tuple descriptor */
};				/* List file identifier */

#define QFILE_CLEAR_LIST_ID(list_id) \
  do { \
    (list_id)->type_list.type_cnt = 0; \
    (list_id)->type_list.domp = NULL; \
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
    (list_id)->tpl_descr.item = NULL; \
    (list_id)->tpl_descr.item_size = 0; \
    (list_id)->tpl_descr.tpl_size = 0; \
    (list_id)->tpl_descr.f_cnt = 0; \
    (list_id)->tpl_descr.f_valp = NULL; \
    (list_id)->tpl_descr.sortkey_info = NULL; \
    (list_id)->tpl_descr.sort_rec = NULL; \
    (list_id)->tpl_descr.tplrec1 = NULL; \
    (list_id)->tpl_descr.tplrec2 = NULL; \
    (list_id)->tpl_descr.merge_info = NULL; \
  } while (0)

/* Tuple position structure */
typedef struct qfile_tuple_position QFILE_TUPLE_POSITION;
struct qfile_tuple_position
{
  SCAN_STATUS status;		/* Scan status                    */
  SCAN_POSITION position;	/* Scan position                  */
  VPID vpid;			/* Real tuple page identifier     */
  int offset;			/* Tuple offset inside the page   */
  QFILE_TUPLE tpl;		/* Tuple pointer inside the page  */
  int tplno;			/* Tuple number inside the page   */
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
  int curr_offset;		/* current page offset */
  QFILE_TUPLE curr_tpl;		/* current tuple pointer */
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
  QFILE_FLAG_DISTINCT = 0x0200
};

#define QFILE_SET_FLAG(var, flag)          ((var) |= (flag))
#define QFILE_CLEAR_FLAG(var, flag)        ((var) &= (flag))
#define QFILE_IS_FLAG_SET(var, flag)       ((var) & (flag))
#define QFILE_IS_FLAG_SET_BOTH(var, flag1, flag2) \
  (((var) & (flag1)) && (var) & (flag2))

/* SORTING RELATED DEFINITIONS */

/* Sorted list identifier */
typedef struct qfile_sorted_list_id QFILE_SORTED_LIST_ID;
struct qfile_sorted_list_id
{
  int sorted;			/* Has file already been sorted? */
  QFILE_LIST_ID *list_id;	/* List File identifier */
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
  ASYNC_EXEC = 1
} QUERY_EXEC_MODE;

enum
{
  ASYNC_EXECUTABLE = 0x00,
  ASYNC_UNEXECUTABLE = 0x02,
  KEEP_PLAN_CACHE = 0x04,
  NOT_FROM_RESULT_CACHE = 0x08,
  RESULT_CACHE_REQUIRED = 0x10,
  RESULT_CACHE_INHIBITED = 0x20
};

#define IS_SYNC_EXEC_MODE(flag) (!((flag) & ASYNC_EXEC))
#define IS_ASYNC_EXEC_MODE(flag) ((flag) & ASYNC_EXEC)

#define IS_ASYNC_UNEXECUTABLE(flag) ((flag) & ASYNC_UNEXECUTABLE)
#define IS_ASYNC_EXECUTABLE(flag) (!((flag) & ASYNC_UNEXECUTABLE))

#define DO_KEEP_PLAN_CACHE(flag)        ((flag) & KEEP_PLAN_CACHE)
#define DO_NOT_KEEP_PLAN_CACHE(flag)    !((flag) & KEEP_PLAN_CACHE)

typedef int QUERY_FLAG;

/*
 * CACHE TIME RELATED DEFINITIONS
 */

typedef struct cache_time CACHE_TIME;
struct cache_time
{
  int sec;
  int usec;
};

#define CACHE_TIME_EQ(T1, T2)               \
        (((T1)->sec != 0) &&                \
         ((T1)->sec == (T2)->sec) &&        \
         ((T1)->usec == (T2)->usec))

#define CACHE_TIME_RESET(T)     \
        do {                    \
          (T)->sec = 0;         \
          (T)->usec = 0;        \
        } while (0)

#define CACHE_TIME_MAKE(CT, TV)         \
        do {                            \
          (CT)->sec = (TV)->tv_sec;     \
          (CT)->usec = (TV)->tv_usec;   \
        } while (0)

#define OR_CACHE_TIME_SIZE      (OR_INT_SIZE * 2)

#define OR_PACK_CACHE_TIME(PTR, T)                      \
        do {                                            \
          if (T) {                                      \
            PTR = or_pack_int(PTR, (T)->sec);        \
            PTR = or_pack_int(PTR, (T)->usec);       \
          }                                             \
          else {                                        \
            PTR = or_pack_int(PTR, 0);                  \
            PTR = or_pack_int(PTR, 0);                  \
          }                                             \
        } while (0)

#define OR_UNPACK_CACHE_TIME(PTR, T)                    \
        do {                                            \
          if (T) {                                      \
            PTR = or_unpack_int(PTR, &((T)->sec));      \
            PTR = or_unpack_int(PTR, &((T)->usec));     \
          }                                             \
        } while (0)

#endif /* _QP_LIST_H_ */
