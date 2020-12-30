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
 * storage_common.h - Definitions and data types of disk related stuffs
 *          such as pages, file structures, and so on.
 */

#ifndef _STORAGE_COMMON_H_
#define _STORAGE_COMMON_H_

#ident "$Id$"

#include "config.h"

#include <limits.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>

#include "porting.h"
#include "porting_inline.hpp"
#include "dbtype_def.h"
#include "sha1.h"
#include "cache_time.h"

  /* LIMITS AND NULL VALUES ON DISK RELATED DATATYPES */

#define NULL_VOLID  (-1)	/* Value of an invalid volume identifier */
#define NULL_SECTID (-1)	/* Value of an invalid sector identifier */
#define NULL_PAGEID (-1)	/* Value of an invalid page identifier */
#define NULL_SLOTID (-1)	/* Value of an invalid slot identifier */
#define NULL_OFFSET (-1)	/* Value of an invalid offset */
#define NULL_FILEID (-1)	/* Value of an invalid file identifier */

#define VOLID_MAX       SHRT_MAX
#define PAGEID_MAX      INT_MAX
#define SECTID_MAX      INT_MAX
#define PGLENGTH_MAX    SHRT_MAX
#define VOL_MAX_NPAGES(page_size) \
  ((sizeof(off_t) == 4) ? (INT_MAX / (page_size)) : INT_MAX)

#define LOGPAGEID_MAX   0x7fffffffffffLL	/* 6 bytes length */

/* NULL_CHN is a special value for an unspecified cache coherency number.
 * It should only be used in error conditions.  This should never be
 * found as a stored chn in a disk or memory object.
 */
enum
{ NULL_CHN = -1, CHN_UNKNOWN_ATCLIENT = -2 };

/* Type definitions related to disk information	*/

typedef INT16 VOLID;		/* Volume identifier */
typedef VOLID DKNVOLS;		/* Number of volumes */

typedef INT32 PAGEID;		/* Data page identifier */
typedef PAGEID DKNPAGES;	/* Number of disk pages */

typedef INT64 LOG_PAGEID;	/* Log page identifier */
typedef PAGEID LOG_PHY_PAGEID;	/* physical log page identifier */

typedef INT32 SECTID;
typedef SECTID DKNSECTS;

typedef INT16 PGSLOTID;		/* Page slot identifier */
typedef PGSLOTID PGNSLOTS;	/* Number of slots on a page */
typedef INT16 PGLENGTH;		/* Page length */

typedef PAGEID FILEID;		/* File identifier */
typedef INT32 LOLENGTH;		/* Length for a large object */

/* BOTH IO_PAGESIZE AND DB_PAGESIZE MUST BE MULTIPLE OF sizeof(int) */

#define IO_DEFAULT_PAGE_SIZE    (16 * ONE_K)
#define IO_MIN_PAGE_SIZE        (4 * ONE_K)
#define IO_MAX_PAGE_SIZE        (16 * ONE_K)

#define LOG_PAGESIZE            (db_log_page_size())
#define IO_PAGESIZE             (db_io_page_size())
#define DB_PAGESIZE             (db_page_size())

#define IS_POWER_OF_2(x)        (((x) & ((x) - 1)) == 0)

/*
 * Sector
 */
/* Number of pages in a sector. Careful about changing this size. The whole file manager depends on this size. */
#define DISK_SECTOR_NPAGES 64
#define IO_SECTORSIZE           (DISK_SECTOR_NPAGES * IO_PAGESIZE)
#define DB_SECTORSIZE		(DISK_SECTOR_NPAGES * DB_PAGESIZE)

#define VOL_MAX_NSECTS(page_size)  (VOL_MAX_NPAGES(page_size) / DISK_SECTOR_NPAGES)

#define SECTOR_FIRST_PAGEID(sid) ((sid) * DISK_SECTOR_NPAGES)
#define SECTOR_LAST_PAGEID(sid) ((((sid) + 1) * DISK_SECTOR_NPAGES) - 1)
#define SECTOR_FROM_PAGEID(pageid) ((pageid) / DISK_SECTOR_NPAGES)

#define VSID_FROM_VPID(vsid, vpid) (vsid)->volid = (vpid)->volid; (vsid)->sectid = SECTOR_FROM_PAGEID ((vpid)->pageid)
#define VSID_IS_SECTOR_OF_VPID(vsid, vpid) \
  ((vsid)->volid == (vpid)->volid && (vsid)->sectid == SECTOR_FROM_PAGEID ((vpid)->pageid))

#define DB_MAX_PATH_LENGTH      PATH_MAX

#define DISK_VFID_SIZE (OR_INT_SIZE + OR_SHORT_SIZE)
#define DISK_VPID_SIZE (OR_INT_SIZE + OR_SHORT_SIZE)

#define DISK_VFID_ALIGNED_SIZE (DISK_VFID_SIZE + OR_SHORT_SIZE)
#define DISK_VPID_ALIGNED_SIZE (DISK_VPID_SIZE + OR_SHORT_SIZE)

/* BTREE definitions */

/* Non_Leaf Node Record Size */
#define NON_LEAF_RECORD_SIZE (DISK_VPID_ALIGNED_SIZE)
/* Leaf Node Record Size */
#define LEAF_RECORD_SIZE (0)
#define SPLIT_INFO_SIZE (OR_FLOAT_SIZE + OR_INT_SIZE)

typedef struct btree_node_split_info BTREE_NODE_SPLIT_INFO;
struct btree_node_split_info
{
  float pivot;			/* pivot = split_slot_id / num_keys */
  int index;			/* number of key insert after node split */
};

typedef char *PAGE_PTR;		/* Pointer to a page */

/* TODO - PAGE_TYPE is used for debugging */
typedef enum
{
  PAGE_UNKNOWN = 0,		/* used for initialized page */
  PAGE_FTAB,			/* file allocset table page */
  PAGE_HEAP,			/* heap page */
  PAGE_VOLHEADER,		/* volume header page */
  PAGE_VOLBITMAP,		/* volume bitmap page */
  PAGE_QRESULT,			/* query result page */
  PAGE_EHASH,			/* ehash bucket/dir page */
  PAGE_OVERFLOW,		/* overflow page (with ovf_keyval) */
  PAGE_AREA,			/* area page */
  PAGE_CATALOG,			/* catalog page */
  PAGE_BTREE,			/* b+tree index page (with ovf_OIDs) */
  PAGE_LOG,			/* NONE - log page (unused) */
  PAGE_DROPPED_FILES,		/* Dropped files page.  */
  PAGE_VACUUM_DATA,		/* Vacuum data. */
  PAGE_LAST = PAGE_VACUUM_DATA
} PAGE_TYPE;

/* Index scan OID buffer size as set by system parameter. */
#define ISCAN_OID_BUFFER_SIZE \
  ((((int) (IO_PAGESIZE * prm_get_float_value (PRM_ID_BT_OID_NBUFFERS))) \
    / OR_OID_SIZE) \
    * OR_OID_SIZE)
#define ISCAN_OID_BUFFER_COUNT \
  (ISCAN_OID_BUFFER_SIZE / OR_OID_SIZE)
/* Minimum capacity of OID buffer.
 * It should include at least one overflow page and on b-tree leaf record.
 * It was set to roughly two pages.
 */
#define ISCAN_OID_BUFFER_MIN_CAPACITY (2 * DB_PAGESIZE)
/* OID buffer capacity. It is the maximum value between the size set by
 * system parameter and the minimum required capacity.
 */
#define ISCAN_OID_BUFFER_CAPACITY \
  (MAX (ISCAN_OID_BUFFER_MIN_CAPACITY, ISCAN_OID_BUFFER_SIZE))

typedef UINT64 MVCCID;		/* MVCC ID */



/* File structure identifiers */

typedef struct hfid HFID;	/* FILE HEAP IDENTIFIER */
struct hfid
{
  VFID vfid;			/* Volume and file identifier */
  INT32 hpgid;			/* First page identifier (the header page) */
};
#define HFID_INITIALIZER \
  { VFID_INITIALIZER, NULL_PAGEID }
#define HFID_AS_ARGS(hfid) (hfid)->hpgid, VFID_AS_ARGS (&(hfid)->vfid)

typedef struct btid BTID;	/* B+tree identifier */
struct btid
{
  VFID vfid;			/* B+tree index volume identifier */
  INT32 root_pageid;		/* Root page identifier */
};
#define BTID_INITIALIZER \
  { VFID_INITIALIZER, NULL_PAGEID }
#define BTID_AS_ARGS(btid) (btid)->root_pageid, VFID_AS_ARGS (&(btid)->vfid)

typedef struct ehid EHID;	/* EXTENDIBLE HASHING IDENTIFIER */
struct ehid
{
  VFID vfid;			/* Volume and Directory file identifier */
  INT32 pageid;			/* The first (root) page of the directory */
};

typedef struct recdes RECDES;	/* RECORD DESCRIPTOR */
struct recdes
{
  int area_size;		/* Length of the allocated area. It includes only the data field. The value is negative
				 * if data is inside buffer. For example, peeking in a slotted page. */
  int length;			/* Length of the data. Does not include the length and type fields */
  INT16 type;			/* Type of record */
  char *data;			/* The data */
};
/* Replace existing data in record at offset_to_data and size old_data_size
 * with new_data of size new_data_size.
 */
#define RECORD_REPLACE_DATA(record, offset_to_data, old_data_size, \
			    new_data_size, new_data) \
  do \
    { \
      assert ((record) != NULL); \
      assert ((record)->data != NULL); \
      assert ((offset_to_data) >= 0 && (offset_to_data) <= (record)->length); \
      assert ((old_data_size) >= 0 && (new_data_size) >= 0); \
      assert ((offset_to_data) + (old_data_size) <= (record)->length); \
      if ((old_data_size) != (new_data_size)) \
        { \
	  /* We may need to move data inside record. */ \
	  if ((offset_to_data) + (old_data_size) < (record)->length) \
	    { \
	      /* Move data inside record. */ \
	      memmove ((record)->data + (offset_to_data) + (new_data_size), \
		       (record)->data + (offset_to_data) + (old_data_size), \
		       (record)->length - (offset_to_data) - (old_data_size)); \
	    } \
	  /* Update record length. */ \
	  (record)->length += (new_data_size) - (old_data_size); \
	} \
      /* Copy new data (if any). */ \
      if ((new_data_size) > 0) \
       { \
	 memcpy ((record)->data + (offset_to_data), new_data, new_data_size); \
       } \
    } \
  while (false)
/* Move the data inside a record */
#define RECORD_MOVE_DATA(rec, dest_offset, src_offset)  \
  do {	\
    assert ((rec) != NULL && (dest_offset) >= 0 && (src_offset) >= 0); \
    assert (((rec)->length - (src_offset)) >= 0);  \
    assert (((rec)->area_size <= 0) || ((rec)->area_size >= (rec)->length));  \
    assert (((rec)->area_size <= 0) \
	    || (((rec)->length + ((dest_offset) - (src_offset)))  \
		<= (rec)->area_size));  \
    if ((dest_offset) != (src_offset))  \
      { \
	if ((rec)->length != (src_offset)) \
	  { \
	    memmove ((rec)->data + (dest_offset), (rec)->data + (src_offset), \
		     (rec)->length - (src_offset)); \
	    (rec)->length = (rec)->length + ((dest_offset) - (src_offset)); \
	  } \
	else \
	  { \
	    (rec)->length = (dest_offset); \
	  } \
      } \
  } while (false)

typedef struct lorecdes LORECDES;	/* Work area descriptor */
struct lorecdes
{
  LOLENGTH length;		/* The length of data in the area */
  LOLENGTH area_size;		/* The size of the area */
  char *data;			/* Pointer to the beginning of the area */
};

#define RECDES_INITIALIZER { 0, -1, REC_UNKNOWN, NULL }

#define HFID_SET_NULL(hfid) \
  do { \
    (hfid)->vfid.fileid = NULL_FILEID; \
    (hfid)->hpgid = NULL_PAGEID; \
  } while(0)

#define HFID_COPY(hfid_ptr1, hfid_ptr2) *(hfid_ptr1) = *(hfid_ptr2)

#define HFID_IS_NULL(hfid)  (((hfid)->vfid.fileid == NULL_FILEID) ? 1 : 0)

#define BTID_SET_NULL(btid) \
  do { \
    (btid)->vfid.fileid = NULL_FILEID; \
    (btid)->vfid.volid = NULL_VOLID; \
    (btid)->root_pageid = NULL_PAGEID; \
  } while(0)

#define BTID_COPY(btid_ptr1, btid_ptr2) *(btid_ptr1) = *(btid_ptr2)

#define BTID_IS_NULL(btid)  (((btid)->vfid.fileid == NULL_FILEID) ? 1 : 0)

#define BTID_IS_EQUAL(b1,b2) \
  (((b1)->vfid.fileid == (b2)->vfid.fileid) && \
   ((b1)->vfid.volid == (b2)->vfid.volid))

#define DISK_VOLPURPOSE DB_VOLPURPOSE

/* Types and defines of transaction management */

typedef int TRANID;		/* Transaction identifier */

#define NULL_TRANID     (-1)
#define NULL_TRAN_INDEX (-1)
#define MVCCID_NULL (0)

#define MVCCID_ALL_VISIBLE    ((MVCCID) 3)	/* visible for all transactions */
#define MVCCID_FIRST	      ((MVCCID) 4)

/* is MVCC ID valid? */
#define MVCCID_IS_VALID(id)	  ((id) != MVCCID_NULL)
/* is MVCC ID normal? */
#define MVCCID_IS_NORMAL(id)	  ((id) >= MVCCID_FIRST)
/* are MVCC IDs equal? */
#define MVCCID_IS_EQUAL(id1,id2)	  ((id1) == (id2))
/* are MVCC IDs valid, not all visible? */
#define MVCCID_IS_NOT_ALL_VISIBLE(id) \
  (MVCCID_IS_VALID (id) && ((id) != MVCCID_ALL_VISIBLE))

/* advance MVCC ID */
#define MVCCID_FORWARD(id) \
  do \
    { \
      (id)++; \
      if ((id) < MVCCID_FIRST) \
        (id) = MVCCID_FIRST; \
    } \
  while (0)

/* back up MVCC ID */
#define MVCCID_BACKWARD(id) \
  do \
    { \
      (id)--; \
    } \
  while ((id) < MVCCID_FIRST)


#define COMPOSITE_LOCK(scan_op_type)	(scan_op_type != S_SELECT)
#define READONLY_SCAN(scan_op_type)	(scan_op_type == S_SELECT)

typedef enum
{
  LOCK_COMPAT_NO = 0,
  LOCK_COMPAT_YES,
  LOCK_COMPAT_UNKNOWN,
} LOCK_COMPATIBILITY;

typedef enum
{
  /* Don't change the initialization since they reflect the elements of lock_Conv and lock_Comp */
  NA_LOCK = 0,			/* N/A lock */
  INCON_NON_TWO_PHASE_LOCK = 1,	/* Incompatible 2 phase lock. */
  NULL_LOCK = 2,		/* NULL LOCK */
  SCH_S_LOCK = 3,		/* Schema Stability Lock */
  IS_LOCK = 4,			/* Intention Shared lock */
  S_LOCK = 5,			/* Shared lock */
  IX_LOCK = 6,			/* Intention exclusive lock */
  BU_LOCK = 7,			/* Bulk Update Lock */
  SIX_LOCK = 8,			/* Shared and intention exclusive lock */
  U_LOCK = 9,			/* Update lock */
  X_LOCK = 10,			/* Exclusive lock */
  SCH_M_LOCK = 11		/* Schema Modification Lock */
} LOCK;

extern LOCK lock_Conv[12][12];

#define LOCK_TO_LOCKMODE_STRING(lock) \
  (((lock) == NULL_LOCK)  ? "  NULL_LOCK" : \
   ((lock) == IS_LOCK)    ? "    IS_LOCK" : \
   ((lock) == S_LOCK)     ? "     S_LOCK" : \
   ((lock) == IX_LOCK)    ? "    IX_LOCK" : \
   ((lock) == SIX_LOCK)   ? "   SIX_LOCK" : \
   ((lock) == U_LOCK)     ? "     U_LOCK" : \
   ((lock) == BU_LOCK)    ? "    BU_LOCK" : \
   ((lock) == SCH_S_LOCK) ? " SCH_S_LOCK" : \
   ((lock) == SCH_M_LOCK) ? " SCH_M_LOCK" : \
   ((lock) == X_LOCK)     ? "     X_LOCK" : "UNKNOWN")

/* CLASSNAME TO OID RETURN VALUES */

typedef enum
{
  LC_CLASSNAME_RESERVED,
  LC_CLASSNAME_DELETED,
  LC_CLASSNAME_EXIST,
  LC_CLASSNAME_ERROR,
  LC_CLASSNAME_RESERVED_RENAME,
  LC_CLASSNAME_DELETED_RENAME
} LC_FIND_CLASSNAME;

#define LC_EXIST              1
#define LC_DOESNOT_EXIST      2
#define LC_ERROR              3

/* Enumeration type for the result of ehash_search function */
typedef enum
{
  EH_KEY_FOUND,
  EH_KEY_NOTFOUND,
  EH_ERROR_OCCURRED
} EH_SEARCH;

/* BTREE_SEARCH - Result for b-tree key or OID search. */
typedef enum
{
  BTREE_KEY_FOUND,		/* Found key (one visible or dirty version). */
  BTREE_KEY_NOTFOUND,		/* Key was not found (or no usable version found in key). */
  BTREE_ERROR_OCCURRED,		/* Error while searching key/OID. */
  BTREE_ACTIVE_KEY_FOUND,	/* Found key but the version inserter/deleter did not commit/abort. */
  BTREE_KEY_SMALLER,		/* Key was not found and it is smaller than all the keys it was compared to. */
  BTREE_KEY_BIGGER,		/* Key was not found and it is bigger than all the keys it was compared to. */
  BTREE_KEY_BETWEEN		/* Key was not found and it's value is between the smallest and the biggest keys it was
				 * compared to. */
} BTREE_SEARCH;



/* Magic default values */
#define CUBRID_MAGIC_MAX_LENGTH                 25
#define CUBRID_MAGIC_PREFIX			"CUBRID/"
#define CUBRID_MAGIC_DATABASE_VOLUME            "CUBRID/Volume"
#define CUBRID_MAGIC_LOG_ACTIVE                 "CUBRID/LogActive"
#define CUBRID_MAGIC_LOG_ARCHIVE                "CUBRID/LogArchive"
#define CUBRID_MAGIC_LOG_INFO                   "CUBRID/LogInfo"
#define CUBRID_MAGIC_DATABASE_BACKUP            "CUBRID/Backup_v2"
#define CUBRID_MAGIC_DATABASE_BACKUP_OLD        "CUBRID/Backup"
#define CUBRID_MAGIC_KEYS                       "CUBRID/Keys"

/*
 * Typedefs related to the scan data structures
 */

typedef enum
{
  S_OPENED = 1,
  S_STARTED,
  S_ENDED,
  S_CLOSED
} SCAN_STATUS;

typedef enum
{
  S_FORWARD = 1,
  S_BACKWARD
} SCAN_DIRECTION;

typedef enum
{
  S_BEFORE = 1,
  S_ON,
  S_AFTER
} SCAN_POSITION;

typedef enum
{
  S_ERROR = -1,
  S_END = 0,
  S_SUCCESS = 1,
  S_SUCCESS_CHN_UPTODATE,	/* only for slotted page */
  S_DOESNT_FIT,			/* only for slotted page */
  S_DOESNT_EXIST,		/* only for slotted page */
  S_SNAPSHOT_NOT_SATISFIED
} SCAN_CODE;

typedef enum
{
  S_SELECT,			/* By default MVCC requires no locks for select operations. */
  S_SELECT_WITH_LOCK,		/* Read operation that doesn't plan to modify the object, but has to know the exact
				 * fate of last version. Can be used for foreign key and unique constraint checks. */
  S_DELETE,			/* Delete object operation. */
  S_UPDATE			/* Update object operation. */
} SCAN_OPERATION_TYPE;

#define IS_WRITE_EXCLUSIVE_LOCK(lock) ((lock) == X_LOCK || (lock) == SCH_M_LOCK)


typedef enum
{
  HEAP_RECORD_INFO_INVALID = -1,
  HEAP_RECORD_INFO_T_PAGEID = 0,
  HEAP_RECORD_INFO_T_SLOTID,
  HEAP_RECORD_INFO_T_VOLUMEID,
  HEAP_RECORD_INFO_T_OFFSET,
  HEAP_RECORD_INFO_T_LENGTH,
  HEAP_RECORD_INFO_T_REC_TYPE,
  HEAP_RECORD_INFO_T_REPRID,
  HEAP_RECORD_INFO_T_CHN,
  HEAP_RECORD_INFO_T_MVCC_INSID,
  HEAP_RECORD_INFO_T_MVCC_DELID,
  HEAP_RECORD_INFO_T_MVCC_FLAGS,
  HEAP_RECORD_INFO_T_MVCC_PREV_VERSION,

  /* leave this last */
  HEAP_RECORD_INFO_COUNT,

  HEAP_RECORD_INFO_FIRST = HEAP_RECORD_INFO_T_PAGEID
} HEAP_RECORD_INFO_ID;

typedef enum
{
  HEAP_PAGE_INFO_INVALID = -1,
  HEAP_PAGE_INFO_CLASS_OID = 0,
  HEAP_PAGE_INFO_PREV_PAGE,
  HEAP_PAGE_INFO_NEXT_PAGE,
  HEAP_PAGE_INFO_NUM_SLOTS,
  HEAP_PAGE_INFO_NUM_RECORDS,
  HEAP_PAGE_INFO_ANCHOR_TYPE,
  HEAP_PAGE_INFO_ALIGNMENT,
  HEAP_PAGE_INFO_TOTAL_FREE,
  HEAP_PAGE_INFO_CONT_FREE,
  HEAP_PAGE_INFO_OFFSET_TO_FREE_AREA,
  HEAP_PAGE_INFO_IS_SAVING,
  HEAP_PAGE_INFO_UPDATE_BEST,

  /* leave this last */
  HEAP_PAGE_INFO_COUNT,

  HEAP_PAGE_INFO_FIRST = HEAP_PAGE_INFO_CLASS_OID
} HEAP_PAGE_INFO_ID;

typedef enum
{
  BTREE_KEY_INFO_INVALID = -1,
  BTREE_KEY_INFO_VOLUMEID = 0,
  BTREE_KEY_INFO_PAGEID,
  BTREE_KEY_INFO_SLOTID,
  BTREE_KEY_INFO_KEY,
  BTREE_KEY_INFO_OID_COUNT,
  BTREE_KEY_INFO_FIRST_OID,
  BTREE_KEY_INFO_OVERFLOW_KEY,
  BTREE_KEY_INFO_OVERFLOW_OIDS,

  /* leave this last */
  BTREE_KEY_INFO_COUNT
} BTREE_KEY_INFO_ID;

typedef enum
{
  BTREE_NODE_INFO_INVALID = -1,
  BTREE_NODE_INFO_VOLUMEID = 0,
  BTREE_NODE_INFO_PAGEID,
  BTREE_NODE_INFO_NODE_TYPE,
  BTREE_NODE_INFO_KEY_COUNT,
  BTREE_NODE_INFO_FIRST_KEY,
  BTREE_NODE_INFO_LAST_KEY,

  /* leave this last */
  BTREE_NODE_INFO_COUNT
} BTREE_NODE_INFO_ID;

typedef enum
{
  LOG_ERROR_IF_DELETED,		/* set error when locking deleted objects */
  LOG_WARNING_IF_DELETED	/* set warning when locking deleted objects - the case when it is expected and
				 * accepted to find a deleted object; for example when er_clear() is used afterwards if
				 * ER_HEAP_UNKNOWN_OBJECT is set in er_errid */
} NON_EXISTENT_HANDLING;

/************************************************************************/
/* spacedb                                                              */
/************************************************************************/

/* database space info */
typedef enum
{
  SPACEDB_PERM_PERM_ALL,
  SPACEDB_PERM_TEMP_ALL,
  SPACEDB_TEMP_TEMP_ALL,

  SPACEDB_TOTAL_ALL,
  SPACEDB_ALL_COUNT,
} SPACEDB_ALL_TYPE;

typedef struct spacedb_all SPACEDB_ALL;
struct spacedb_all
{
  DKNVOLS nvols;
  DKNPAGES npage_used;
  DKNPAGES npage_free;
};

typedef struct spacedb_onevol SPACEDB_ONEVOL;
struct spacedb_onevol
{
  VOLID volid;
  DB_VOLTYPE type;
  DB_VOLPURPOSE purpose;
  DKNPAGES npage_used;
  DKNPAGES npage_free;
  char name[DB_MAX_PATH_LENGTH];
};

/* files */
typedef enum
{
  SPACEDB_INDEX_FILE,
  SPACEDB_HEAP_FILE,
  SPACEDB_SYSTEM_FILE,
  SPACEDB_TEMP_FILE,

  SPACEDB_TOTAL_FILE,
  SPACEDB_FILE_COUNT,
} SPACEDB_FILE_TYPE;

typedef struct spacedb_files SPACEDB_FILES;
struct spacedb_files
{
  int nfile;
  DKNPAGES npage_ftab;
  DKNPAGES npage_user;
  DKNPAGES npage_reserved;
};

/************************************************************************/
/* client & catalog common                                              */
/************************************************************************/

typedef int REPR_ID;		/* representation identifier */
typedef int ATTR_ID;		/* attribute identifier */

#define NULL_REPRID       -1	/* Null Representation Identifier */
#define NULL_ATTRID       -1	/* Null Attribute Identifier */

/************************************************************************/
/* b-tree common                                                        */
/************************************************************************/

typedef enum
{
  BTREE_CONSTRAINT_UNIQUE = 0x01,
  BTREE_CONSTRAINT_PRIMARY_KEY = 0x02
} BTREE_CONSTRAINT_TYPE;

typedef enum
{
  BTREE_UNIQUE,
  BTREE_INDEX,
  BTREE_REVERSE_UNIQUE,
  BTREE_REVERSE_INDEX,
  BTREE_PRIMARY_KEY,
  BTREE_FOREIGN_KEY
} BTREE_TYPE;

/************************************************************************/
/* storage common functions                                             */
/************************************************************************/
extern INT16 db_page_size (void);
extern INT16 db_io_page_size (void);
extern INT16 db_log_page_size (void);
extern int db_set_page_size (INT16 io_page_size, INT16 log_page_size);
extern INT16 db_network_page_size (void);
extern void db_print_data (DB_TYPE type, DB_DATA * data, FILE * fd);

extern int recdes_allocate_data_area (RECDES * rec, int size);
extern void recdes_free_data_area (RECDES * rec);
extern void recdes_set_data_area (RECDES * rec, char *data, int size);

extern char *oid_to_string (char *buf, int buf_size, OID * oid);
extern char *vpid_to_string (char *buf, int buf_size, VPID * vpid);
extern char *vfid_to_string (char *buf, int buf_size, VFID * vfid);
extern char *hfid_to_string (char *buf, int buf_size, HFID * hfid);
extern char *btid_to_string (char *buf, int buf_size, BTID * btid);

/************************************************************************/
/* next has nothing to do with storage. however, we lack a clear place  */
/* for global stuff that affect everything. this is closest...          */
/************************************************************************/

typedef enum
{
  T_ADD,
  T_SUB,
  T_MUL,
  T_DIV,
  T_UNPLUS,
  T_UNMINUS,
  T_PRIOR,
  T_CONNECT_BY_ROOT,
  T_QPRIOR,
  T_BIT_NOT,
  T_BIT_AND,
  T_BIT_OR,
  T_BIT_XOR,
  T_BIT_COUNT,
  T_BITSHIFT_LEFT,
  T_BITSHIFT_RIGHT,
  T_INTDIV,
  T_INTMOD,
  T_IF,
  T_IFNULL,
  T_ISNULL,
  T_ACOS,
  T_ASIN,
  T_ATAN,
  T_ATAN2,
  T_COS,
  T_SIN,
  T_TAN,
  T_COT,
  T_PI,
  T_DEGREES,
  T_RADIANS,
  T_FORMAT,
  T_CONCAT,
  T_CONCAT_WS,
  T_FIELD,
  T_LEFT,
  T_RIGHT,
  T_REPEAT,
  T_SPACE,
  T_LOCATE,
  T_MID,
  T_STRCMP,
  T_REVERSE,
  T_DISK_SIZE,
  T_LN,
  T_LOG2,
  T_LOG10,
  T_ADDDATE,
  T_DATE_ADD,
  T_SUBDATE,
  T_DATE_SUB,
  T_DATE_FORMAT,
  T_STR_TO_DATE,
  T_MOD,
  T_POSITION,
  T_SUBSTRING,
  T_SUBSTRING_INDEX,
  T_OCTET_LENGTH,
  T_BIT_LENGTH,
  T_CHAR_LENGTH,
  T_MD5,
  T_LOWER,
  T_UPPER,
  T_LIKE_LOWER_BOUND,
  T_LIKE_UPPER_BOUND,
  T_TRIM,
  T_LTRIM,
  T_RTRIM,
  T_LPAD,
  T_RPAD,
  T_REPLACE,
  T_TRANSLATE,
  T_ADD_MONTHS,
  T_LAST_DAY,
  T_MONTHS_BETWEEN,
  T_SYS_DATE,
  T_SYS_TIME,
  T_SYS_TIMESTAMP,
  T_UTC_TIME,
  T_UTC_DATE,
  T_TIME_FORMAT,
  T_TIMESTAMP,
  T_UNIX_TIMESTAMP,
  T_FROM_UNIXTIME,
  T_SYS_DATETIME,
  T_YEAR,
  T_MONTH,
  T_DAY,
  T_HOUR,
  T_MINUTE,
  T_SECOND,
  T_QUARTER,
  T_WEEKDAY,
  T_DAYOFWEEK,
  T_DAYOFYEAR,
  T_TODAYS,
  T_FROMDAYS,
  T_TIMETOSEC,
  T_SECTOTIME,
  T_MAKEDATE,
  T_MAKETIME,
  T_WEEK,
  T_TO_CHAR,
  T_TO_DATE,
  T_TO_TIME,
  T_TO_TIMESTAMP,
  T_TO_DATETIME,
  T_TO_NUMBER,
  T_CURRENT_VALUE,
  T_NEXT_VALUE,
  T_CAST,
  T_CAST_NOFAIL,
  T_CAST_WRAP,
  T_CASE,
  T_EXTRACT,
  T_LOCAL_TRANSACTION_ID,
  T_FLOOR,
  T_CEIL,
  T_SIGN,
  T_POWER,
  T_ROUND,
  T_LOG,
  T_EXP,
  T_SQRT,
  T_TRUNC,
  T_ABS,
  T_CHR,
  T_INSTR,
  T_LEAST,
  T_GREATEST,
  T_STRCAT,
  T_NULLIF,
  T_COALESCE,
  T_NVL,
  T_NVL2,
  T_DECODE,
  T_RAND,
  T_DRAND,
  T_RANDOM,
  T_DRANDOM,
  T_INCR,
  T_DECR,
  T_SYS_CONNECT_BY_PATH,
  T_DATE,
  T_TIME,
  T_DATEDIFF,
  T_TIMEDIFF,
  T_ROW_COUNT,
  T_LAST_INSERT_ID,
  T_DEFAULT,
  T_LIST_DBS,
  T_BIT_TO_BLOB,
  T_BLOB_TO_BIT,
  T_CHAR_TO_CLOB,
  T_CLOB_TO_CHAR,
  T_LOB_LENGTH,
  T_TYPEOF,
  T_INDEX_CARDINALITY,
  T_EVALUATE_VARIABLE,
  T_DEFINE_VARIABLE,
  T_PREDICATE,
  T_EXEC_STATS,
  T_ADDTIME,
  T_BIN,
  T_FINDINSET,
  T_HEX,
  T_ASCII,
  T_CONV,
  T_INET_ATON,
  T_INET_NTOA,
  T_TO_ENUMERATION_VALUE,
  T_CHARSET,
  T_COLLATION,
  T_WIDTH_BUCKET,
  T_TRACE_STATS,
  T_AES_ENCRYPT,
  T_AES_DECRYPT,
  T_SHA_ONE,
  T_SHA_TWO,
  T_INDEX_PREFIX,
  T_TO_BASE64,
  T_FROM_BASE64,
  T_SYS_GUID,
  T_SLEEP,
  T_DBTIMEZONE,
  T_SESSIONTIMEZONE,
  T_TZ_OFFSET,
  T_NEW_TIME,
  T_FROM_TZ,
  T_TO_DATETIME_TZ,
  T_TO_TIMESTAMP_TZ,
  T_UTC_TIMESTAMP,
  T_CRC32,
  T_CURRENT_DATETIME,
  T_CURRENT_TIMESTAMP,
  T_CURRENT_DATE,
  T_CURRENT_TIME,
  T_CONV_TZ,
} OPERATOR_TYPE;		/* arithmetic operator types */

typedef enum
{
  /* aggregate functions */
  PT_MIN = 900, PT_MAX, PT_SUM, PT_AVG,
  PT_STDDEV, PT_VARIANCE,
  PT_STDDEV_POP, PT_VAR_POP,
  PT_STDDEV_SAMP, PT_VAR_SAMP,
  PT_COUNT, PT_COUNT_STAR,
  PT_GROUPBY_NUM,
  PT_AGG_BIT_AND, PT_AGG_BIT_OR, PT_AGG_BIT_XOR,
  PT_GROUP_CONCAT,
  PT_ROW_NUMBER,
  PT_RANK,
  PT_DENSE_RANK,
  PT_NTILE,
  PT_JSON_ARRAYAGG,
  PT_JSON_OBJECTAGG,
  PT_TOP_AGG_FUNC,
  /* only aggregate functions should be below PT_TOP_AGG_FUNC */

  /* analytic only functions */
  PT_LEAD, PT_LAG,

  /* foreign functions */
  PT_GENERIC,

  /* from here down are function code common to parser and xasl */
  /* "table" functions argument(s) are tables */
  F_TABLE_SET, F_TABLE_MULTISET, F_TABLE_SEQUENCE,
  F_TOP_TABLE_FUNC,
  F_MIDXKEY,

  /* "normal" functions, arguments are values */
  F_SET, F_MULTISET, F_SEQUENCE, F_VID, F_GENERIC, F_CLASS_OF,
  F_INSERT_SUBSTRING, F_ELT, F_JSON_OBJECT, F_JSON_ARRAY, F_JSON_MERGE, F_JSON_MERGE_PATCH, F_JSON_INSERT,
  F_JSON_REMOVE, F_JSON_ARRAY_APPEND, F_JSON_GET_ALL_PATHS, F_JSON_REPLACE, F_JSON_SET, F_JSON_KEYS,
  F_JSON_ARRAY_INSERT, F_JSON_SEARCH, F_JSON_CONTAINS_PATH, F_JSON_EXTRACT, F_JSON_CONTAINS, F_JSON_DEPTH,
  F_JSON_LENGTH, F_JSON_PRETTY, F_JSON_QUOTE, F_JSON_TYPE, F_JSON_UNQUOTE, F_JSON_VALID,

  F_REGEXP_COUNT, F_REGEXP_INSTR, F_REGEXP_LIKE, F_REGEXP_REPLACE, F_REGEXP_SUBSTR,

  F_BENCHMARK,

  /* only for FIRST_VALUE. LAST_VALUE, NTH_VALUE analytic functions */
  PT_FIRST_VALUE, PT_LAST_VALUE, PT_NTH_VALUE,
  /* aggregate and analytic functions */
  PT_MEDIAN,
  PT_CUME_DIST,
  PT_PERCENT_RANK,
  PT_PERCENTILE_CONT,
  PT_PERCENTILE_DISC
} FUNC_TYPE;

#ifdef __cplusplus
extern "C"
{
#endif				// c++
  const char *fcode_get_lowercase_name (FUNC_TYPE ftype);
  const char *fcode_get_uppercase_name (FUNC_TYPE ftype);
#ifdef __cplusplus
}
#endif				// c++

/************************************************************************/
/* QUERY                                                                */
/************************************************************************/

#define CACHE_TIME_AS_ARGS(ct)	(ct)->sec, (ct)->usec

#define CACHE_TIME_EQ(T1, T2) \
  (((T1)->sec != 0) && ((T1)->sec == (T2)->sec) && ((T1)->usec == (T2)->usec))

#define CACHE_TIME_RESET(T) \
  do \
    { \
      (T)->sec = 0; \
      (T)->usec = 0; \
    } \
  while (0)

#define CACHE_TIME_MAKE(CT, TV) \
  do \
    { \
      (CT)->sec = (TV)->tv_sec; \
      (CT)->usec = (TV)->tv_usec; \
    } \
  while (0)

#define OR_CACHE_TIME_SIZE (OR_INT_SIZE * 2)

#define OR_PACK_CACHE_TIME(PTR, T) \
  do \
    { \
      if ((CACHE_TIME *) (T) != NULL) \
        { \
          PTR = or_pack_int (PTR, (T)->sec); \
          PTR = or_pack_int (PTR, (T)->usec); \
        } \
    else \
      { \
        PTR = or_pack_int (PTR, 0); \
        PTR = or_pack_int (PTR, 0); \
      } \
    } \
  while (0)

#define OR_UNPACK_CACHE_TIME(PTR, T) \
  do \
    { \
      if ((CACHE_TIME *) (T) != NULL) \
        { \
          PTR = or_unpack_int (PTR, &((T)->sec)); \
          PTR = or_unpack_int (PTR, &((T)->usec)); \
        } \
    } \
  while (0)

/* XASL identifier */
typedef struct xasl_id XASL_ID;
struct xasl_id
{
  SHA1Hash sha1;		/* SHA-1 hash generated from query string. */
  INT32 cache_flag;
  /* Multiple-purpose field used to handle XASL cache. */
  CACHE_TIME time_stored;	/* when this XASL plan stored */
};				/* XASL plan file identifier */

typedef enum
{
  Q_DISTINCT,			/* no duplicate values */
  Q_ALL				/* all values */
} QUERY_OPTIONS;

typedef enum
{
  R_KEY = 1,			/* key value search */
  R_RANGE,			/* range search with the two key values and range spec */
  R_KEYLIST,			/* a list of key value searches */
  R_RANGELIST			/* a list of range searches */
} RANGE_TYPE;

typedef enum
{
  SHOWSTMT_START = 0,
  SHOWSTMT_NULL = SHOWSTMT_START,
  SHOWSTMT_ACCESS_STATUS,
  SHOWSTMT_VOLUME_HEADER,
  SHOWSTMT_ACTIVE_LOG_HEADER,
  SHOWSTMT_ARCHIVE_LOG_HEADER,
  SHOWSTMT_SLOTTED_PAGE_HEADER,
  SHOWSTMT_SLOTTED_PAGE_SLOTS,
  SHOWSTMT_HEAP_HEADER,
  SHOWSTMT_ALL_HEAP_HEADER,
  SHOWSTMT_HEAP_CAPACITY,
  SHOWSTMT_ALL_HEAP_CAPACITY,
  SHOWSTMT_INDEX_HEADER,
  SHOWSTMT_INDEX_CAPACITY,
  SHOWSTMT_ALL_INDEXES_HEADER,
  SHOWSTMT_ALL_INDEXES_CAPACITY,
  SHOWSTMT_GLOBAL_CRITICAL_SECTIONS,
  SHOWSTMT_JOB_QUEUES,
  SHOWSTMT_TIMEZONES,
  SHOWSTMT_FULL_TIMEZONES,
  SHOWSTMT_TRAN_TABLES,
  SHOWSTMT_THREADS,
  SHOWSTMT_PAGE_BUFFER_STATUS,

  /* append the new show statement types in here */

  SHOWSTMT_END
} SHOWSTMT_TYPE;

#define NUM_F_GENERIC_ARGS 32
#define NUM_F_INSERT_SUBSTRING_ARGS 4

extern const int SM_MAX_STRING_LENGTH;

/*
 * These are the names for the system defined properties on classes,
 * attributes and methods.  For the built in properties, try
 * to use short names.  User properties if they are ever allowed
 * should have more descriptive names.
 *
 * Lets adopt the convention that names beginning with a '*' are
 * reserved for system properties.
 */
#define SM_PROPERTY_UNIQUE "*U"
#define SM_PROPERTY_INDEX "*I"
#define SM_PROPERTY_NOT_NULL "*N"
#define SM_PROPERTY_REVERSE_UNIQUE "*RU"
#define SM_PROPERTY_REVERSE_INDEX "*RI"
#define SM_PROPERTY_VID_KEY "*V_KY"
#define SM_PROPERTY_PRIMARY_KEY "*P"
#define SM_PROPERTY_FOREIGN_KEY "*FK"

#define SM_PROPERTY_NUM_INDEX_FAMILY         6

#define SM_FILTER_INDEX_ID "*FP*"
#define SM_FUNCTION_INDEX_ID "*FI*"
#define SM_PREFIX_INDEX_ID "*PLID*"

/*
 *    Bit field identifiers for attribute flags.  These could be defined
 *    with individual unsigned bit fields but this makes it easier
 *    to save them as a single integer.
 *    The "new" flag is used only at run time and shouldn't be here.
 *    Need to re-design the template functions to operate from a different
 *    memory structure during flattening.
 */
typedef enum
{
  SM_ATTFLAG_NONE = 0,
  SM_ATTFLAG_INDEX = 1,		/* attribute has an index 0x01 */
  SM_ATTFLAG_UNIQUE = 2,	/* attribute has UNIQUE constraint 0x02 */
  SM_ATTFLAG_NON_NULL = 4,	/* attribute has NON_NULL constraint 0x04 */
  SM_ATTFLAG_VID = 8,		/* attribute is part of virtual object id 0x08 */
  SM_ATTFLAG_NEW = 16,		/* is a new attribute 0x10 */
  SM_ATTFLAG_REVERSE_INDEX = 32,	/* attribute has a reverse index 0x20 */
  SM_ATTFLAG_REVERSE_UNIQUE = 64,	/* attribute has a reverse unique 0x40 */
  SM_ATTFLAG_PRIMARY_KEY = 128,	/* attribute has a primary key 0x80 */
  SM_ATTFLAG_AUTO_INCREMENT = 256,	/* auto increment attribute 0x0100 */
  SM_ATTFLAG_FOREIGN_KEY = 512,	/* attribute has a primary key 0x200 */
  SM_ATTFLAG_PARTITION_KEY = 1024	/* attribute is the partitioning key for the class 0x400 */
} SM_ATTRIBUTE_FLAG;

/* delete or update action type for foreign key */
typedef enum
{
  SM_FOREIGN_KEY_CASCADE,
  SM_FOREIGN_KEY_RESTRICT,
  SM_FOREIGN_KEY_NO_ACTION,
  SM_FOREIGN_KEY_SET_NULL
} SM_FOREIGN_KEY_ACTION;

/*
 *    These identify "namespaces" for class components like attributes
 *    and methods.  A name_space identifier is frequently used
 *    in conjunction with a name so the correct component can be found
 *    in a class definition.  Since the namespaces for classes and
 *    instances can overlap, a name alone is not enough to uniquely
 *    identify a component.
 */
typedef enum
{
  ID_ATTRIBUTE,
  ID_SHARED_ATTRIBUTE,
  ID_CLASS_ATTRIBUTE,
  ID_METHOD,
  ID_CLASS_METHOD,
  ID_INSTANCE,			/* attributes/shared attributes/methods */
  ID_CLASS,			/* class methods/class attributes */
  ID_NULL
} SM_NAME_SPACE;

/*
 *    This constant defines the maximum size in bytes of a class name,
 *    attribute name, method name, or any other named entity in the schema.
 */
#define SM_MAX_IDENTIFIER_LENGTH 255

#define SERIAL_ATTR_NAME          "name"
#define SERIAL_ATTR_OWNER         "owner"
#define SERIAL_ATTR_CURRENT_VAL   "current_val"
#define SERIAL_ATTR_INCREMENT_VAL "increment_val"
#define SERIAL_ATTR_MAX_VAL       "max_val"
#define SERIAL_ATTR_MIN_VAL       "min_val"
#define SERIAL_ATTR_CYCLIC        "cyclic"
#define SERIAL_ATTR_STARTED       "started"
#define SERIAL_ATTR_CLASS_NAME    "class_name"
#define SERIAL_ATTR_ATT_NAME      "att_name"
#define SERIAL_ATTR_CACHED_NUM    "cached_num"
#define SERIAL_ATTR_COMMENT       "comment"

static const bool PEEK = true;	/* Peek for a slotted record */
static const bool COPY = false;	/* Don't peek, but copy a slotted record */

enum
{
/* Unknown record type */
  REC_UNKNOWN = 0,

/* Record without content, just the address */
  REC_ASSIGN_ADDRESS = 1,

/* Home of record */
  REC_HOME = 2,

/* No the original home of record.  part of relocation process */
  REC_NEWHOME = 3,

/* Record describe new home of record */
  REC_RELOCATION = 4,

/* Record describe location of big record */
  REC_BIGONE = 5,

/* Slot does not describe any record.
 * A record was stored in this slot.  Slot cannot be reused.
 */
  REC_MARKDELETED = 6,

/* Slot does not describe any record.
 * A record was stored in this slot.  Slot will be reused.
 */
  REC_DELETED_WILL_REUSE = 7,

/* unused reserved record type */
  REC_RESERVED_TYPE_8 = 8,
  REC_RESERVED_TYPE_9 = 9,
  REC_RESERVED_TYPE_10 = 10,
  REC_RESERVED_TYPE_11 = 11,
  REC_RESERVED_TYPE_12 = 12,
  REC_RESERVED_TYPE_13 = 13,
  REC_RESERVED_TYPE_14 = 14,
  REC_RESERVED_TYPE_15 = 15,
/* 4bit record type max */
  REC_4BIT_USED_TYPE_MAX = REC_DELETED_WILL_REUSE,
  REC_4BIT_TYPE_MAX = REC_RESERVED_TYPE_15
};

typedef struct dbdef_vol_ext_info DBDEF_VOL_EXT_INFO;
struct dbdef_vol_ext_info
{
  const char *path;		/* Directory where the volume extension is created.  If NULL, is given, it defaults to
				 * the system parameter. */
  const char *name;		/* Name of the volume extension If NULL, system generates one like "db".ext"volid"
				 * where "db" is the database name and "volid" is the volume identifier to be assigned
				 * to the volume extension. */
  const char *comments;		/* Comments which are included in the volume extension header. */
  int max_npages;		/* Maximum pages of this volume */
  int extend_npages;		/* Number of pages to extend - used for generic volume only */
  INT32 nsect_total;		/* DKNSECTS type, number of sectors for volume extension */
  INT32 nsect_max;		/* DKNSECTS type, maximum number of sectors for volume extension */
  int max_writesize_in_sec;	/* the amount of volume written per second */
  DB_VOLPURPOSE purpose;	/* The purpose of the volume extension. One of the following: -
				 * DB_PERMANENT_DATA_PURPOSE, DB_TEMPORARY_DATA_PURPOSE */
  DB_VOLTYPE voltype;		/* Permanent of temporary volume type */
  bool overwrite;
};

#define SERVER_SESSION_KEY_SIZE			8

typedef enum
{
  DB_PARTITION_HASH = 0,
  DB_PARTITION_RANGE,
  DB_PARTITION_LIST
} DB_PARTITION_TYPE;

typedef enum
{
  DB_NOT_PARTITIONED_CLASS = 0,
  DB_PARTITIONED_CLASS = 1,
  DB_PARTITION_CLASS = 2
} DB_CLASS_PARTITION_TYPE;

// TODO: move me in a proper place
typedef enum
{
  KILLSTMT_TRAN = 0,
  KILLSTMT_QUERY = 1,
} KILLSTMT_TYPE;

// query module
typedef enum
{
  HS_NONE = 0,			/* no hash aggregation */
  HS_ACCEPT_ALL,		/* accept tuples in hash table */
  HS_REJECT_ALL			/* reject tuples, use normal sort-based aggregation */
} AGGREGATE_HASH_STATE;

#endif /* _STORAGE_COMMON_H_ */
