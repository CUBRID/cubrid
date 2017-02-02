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

#include "porting.h"

#include "dbdef.h"
#include "dbtype.h"

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

/* Compose the full name of a database */

#define COMPOSE_FULL_NAME(buf, buf_size, path, name) \
  do { \
    int len = strlen(path); \
    if (len > 0 && path[len - 1] != PATH_SEPARATOR) { \
      snprintf(buf, buf_size - 1, "%s%c%s", path, PATH_SEPARATOR, name); \
    } else { \
      snprintf(buf, buf_size - 1, "%s%s", path, name); \
    } \
  } while (0)

/* Type definitions related to disk information	*/

typedef INT16 VOLID;		/* Volume identifier */

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

/* Log address structure */

typedef struct log_lsa LOG_LSA;	/* Log address identifier */
struct log_lsa
{
  INT64 pageid:48;		/* Log page identifier : 6 bytes length */
  INT64 offset:16;		/* Offset in page : 2 bytes length */
  /* The offset field is defined as 16bit-INT64 type (not short), because of alignment in windows */
};

#define LSA_COPY(lsa_ptr1, lsa_ptr2) *(lsa_ptr1) = *(lsa_ptr2)
#define LSA_SET_NULL(lsa_ptr)\
  do {									      \
    (lsa_ptr)->pageid = NULL_PAGEID;                                          \
    (lsa_ptr)->offset = NULL_OFFSET;                                          \
  } while(0)

#define LSA_INITIALIZER	{NULL_PAGEID, NULL_OFFSET}

#define LSA_AS_ARGS(lsa_ptr) (long long int) (lsa_ptr)->pageid, (int) (lsa_ptr)->offset

#define LSA_SET_INIT_NONTEMP(lsa_ptr) LSA_SET_NULL(lsa_ptr)
#define LSA_SET_INIT_TEMP(lsa_ptr)\
  do {									      \
    (lsa_ptr)->pageid = NULL_PAGEID - 1;                                      \
    (lsa_ptr)->offset = NULL_OFFSET - 1;                                      \
  } while(0)

#define LSA_ISNULL(lsa_ptr) ((lsa_ptr)->pageid == NULL_PAGEID)
#define LSA_IS_INIT_NONTEMP(lsa_ptr) LSA_ISNULL(lsa_ptr)
#define LSA_IS_INIT_TEMP(lsa_ptr) (((lsa_ptr)->pageid == NULL_PAGEID - 1) &&  \
				  ((lsa_ptr)->offset == NULL_OFFSET - 1))

#define LSA_LT(lsa_ptr1, lsa_ptr2)                                            \
  ((lsa_ptr1) != (lsa_ptr2) &&                                                \
   ((lsa_ptr1)->pageid < (lsa_ptr2)->pageid ||                                \
    ((lsa_ptr1)->pageid == (lsa_ptr2)->pageid &&                              \
     (lsa_ptr1)->offset < (lsa_ptr2)->offset)))                               \

#define LSA_EQ(lsa_ptr1, lsa_ptr2)                                            \
  ((lsa_ptr1) == (lsa_ptr2) ||                                                \
    ((lsa_ptr1)->pageid == (lsa_ptr2)->pageid &&                              \
     (lsa_ptr1)->offset == (lsa_ptr2)->offset))

#define LSA_LE(lsa_ptr1, lsa_ptr2) (!LSA_LT(lsa_ptr2, lsa_ptr1))
#define LSA_GT(lsa_ptr1, lsa_ptr2) LSA_LT(lsa_ptr2, lsa_ptr1)
#define LSA_GE(lsa_ptr1, lsa_ptr2) LSA_LE(lsa_ptr2, lsa_ptr1)

/* BOTH IO_PAGESIZE AND DB_PAGESIZE MUST BE MULTIPLE OF sizeof(int) */

#define IO_DEFAULT_PAGE_SIZE    (16 * ONE_K)
#define IO_MIN_PAGE_SIZE        (4 * ONE_K)
#define IO_MAX_PAGE_SIZE        (16 * ONE_K)

#define LOG_PAGESIZE            (db_log_page_size())
#define IO_PAGESIZE             (db_io_page_size())
#define DB_PAGESIZE             (db_page_size())

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

/* TYPE DEFINITIONS RELATED TO KEY AND VALUES */

typedef enum			/* range search option */
{
  NA_NA,			/* v1 and v2 are N/A, so that no range is defined */
  GE_LE,			/* v1 <= key <= v2 */
  GE_LT,			/* v1 <= key < v2 */
  GT_LE,			/* v1 < key <= v2 */
  GT_LT,			/* v1 < key < v2 */
  GE_INF,			/* v1 <= key (<= the end) */
  GT_INF,			/* v1 < key (<= the end) */
  INF_LE,			/* (the beginning <=) key <= v2 */
  INF_LT,			/* (the beginning <=) key < v2 */
  INF_INF,			/* the beginning <= key <= the end */
  EQ_NA,			/* key = v1, v2 is N/A */

  /* following options are reserved for the future use */
  LE_GE,			/* key <= v1 || key >= v2 or NOT (v1 < key < v2) */
  LE_GT,			/* key <= v1 || key > v2 or NOT (v1 < key <= v2) */
  LT_GE,			/* key < v1 || key >= v2 or NOT (v1 <= key < v2) */
  LT_GT,			/* key < v1 || key > v2 or NOT (v1 <= key <= v2) */
  NEQ_NA			/* key != v1 */
} RANGE;

#define RANGE_REVERSE(range) \
  do \
    { \
      switch (range) \
	{ \
	case GT_LE: \
	  (range) = GE_LT; \
	  break; \
	case GE_LT: \
	  (range) = GT_LE; \
	  break; \
	case GE_INF: \
	  (range) = INF_LE; \
	  break; \
	case GT_INF: \
	  (range) = INF_LT; \
	  break; \
	case INF_LE: \
	  (range) = GE_INF; \
	  break; \
	case INF_LT: \
	  (range) = GT_INF; \
	  break; \
	case NA_NA: \
	case GE_LE: \
	case GT_LT: \
	case INF_INF: \
	case EQ_NA: \
	default: \
	  /* No change. */ \
	  break; \
	} \
    } while (0)

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

/* MVCC RECORD HEADER */
typedef struct mvcc_rec_header MVCC_REC_HEADER;
struct mvcc_rec_header
{
  INT32 mvcc_flag:8;		/* MVCC flags */
  INT32 repid:24;		/* representation id */
  int chn;			/* cache coherency number */
  MVCCID mvcc_ins_id;		/* MVCC insert id */
  MVCCID mvcc_del_id;		/* MVCC delete id */
  LOG_LSA prev_version_lsa;	/* log address of previous version */
};
#define MVCC_REC_HEADER_INITIALIZER \
{ 0, 0, NULL_CHN, MVCCID_NULL, MVCCID_NULL, LSA_INITIALIZER }

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
  /* Don't change the initialization since they reflect the elements of lock_Conv and lock_Comp */
  NA_LOCK = 0,			/* N/A lock */
  INCON_NON_TWO_PHASE_LOCK = 1,	/* Incompatible 2 phase lock. */
  NULL_LOCK = 2,		/* NULL LOCK */
  SCH_S_LOCK = 3,		/* Schema Stability Lock */
  IS_LOCK = 4,			/* Intention Shared lock */
  S_LOCK = 5,			/* Shared lock */
  IX_LOCK = 6,			/* Intention exclusive lock */
  SIX_LOCK = 7,			/* Shared and intention exclusive lock */
  U_LOCK = 8,			/* Update lock */
  X_LOCK = 9,			/* Exclusive lock */
  SCH_M_LOCK = 10		/* Schema Modification Lock */
} LOCK;

extern LOCK lock_Conv[11][11];

#define LOCK_TO_LOCKMODE_STRING(lock) 			\
  (((lock) ==NULL_LOCK) ? "NULL_LOCK" :			\
   ((lock) ==  IS_LOCK) ? "  IS_LOCK" :			\
   ((lock) ==   S_LOCK) ? "   S_LOCK" :			\
   ((lock) ==  IX_LOCK) ? "  IX_LOCK" :			\
   ((lock) == SIX_LOCK) ? " SIX_LOCK" :			\
   ((lock) ==   U_LOCK) ? "   U_LOCK" :			\
   ((lock) ==  SCH_S_LOCK) ? "  SCH_S_LOCK" :		\
   ((lock) ==  SCH_M_LOCK) ? "  SCH_M_LOCK" :		\
   ((lock) ==   X_LOCK) ? "   X_LOCK" : "UNKNOWN")

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

/* TYPEDEFS FOR BACKUP/RESTORE */

/* structure for passing arguments into boot_restart_server et. al. */
typedef struct bo_restart_arg BO_RESTART_ARG;
struct bo_restart_arg
{
  bool printtoc;		/* True to show backup's table of contents */
  time_t stopat;		/* the recovery stop time if restarting from backup */
  const char *backuppath;	/* Pathname override for location of backup volumes */
  int level;			/* The backup level to use */
  const char *verbose_file;	/* restoredb verbose msg file */
  bool newvolpath;		/* true: restore the database and log volumes to the path specified in the
				 * database-loc-file */
  bool restore_upto_bktime;

  bool restore_slave;		/* restore slave */
  INT64 db_creation;		/* database creation time */
  LOG_LSA restart_repl_lsa;	/* restart replication lsa after restoreslave */
};

/* Magic default values */
#define CUBRID_MAGIC_MAX_LENGTH                 25
#define CUBRID_MAGIC_PREFIX			"CUBRID/"
#define CUBRID_MAGIC_DATABASE_VOLUME            "CUBRID/Volume"
#define CUBRID_MAGIC_LOG_ACTIVE                 "CUBRID/LogActive"
#define CUBRID_MAGIC_LOG_ARCHIVE                "CUBRID/LogArchive"
#define CUBRID_MAGIC_LOG_INFO                   "CUBRID/LogInfo"
#define CUBRID_MAGIC_DATABASE_BACKUP            "CUBRID/Backup_v2"
#define CUBRID_MAGIC_DATABASE_BACKUP_OLD        "CUBRID/Backup"

/* B+tree local statististical information for Uniqueness enforcement */
typedef struct btree_unique_stats BTREE_UNIQUE_STATS;
struct btree_unique_stats
{
  BTID btid;
  int num_nulls;
  int num_keys;
  int num_oids;
};

#define UNIQUE_STAT_INFO_INCREMENT   10

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

extern INT16 db_page_size (void);
extern INT16 db_io_page_size (void);
extern INT16 db_log_page_size (void);
extern int db_set_page_size (INT16 io_page_size, INT16 log_page_size);
extern INT16 db_network_page_size (void);
extern void db_print_data (DB_TYPE type, DB_DATA * data, FILE * fd);

extern int recdes_allocate_data_area (RECDES * rec, int size);
extern void recdes_free_data_area (RECDES * rec);
extern void recdes_set_data_area (RECDES * rec, char *data, int size);

extern char *lsa_to_string (char *buf, int buf_size, LOG_LSA * lsa);
extern char *oid_to_string (char *buf, int buf_size, OID * oid);
extern char *vpid_to_string (char *buf, int buf_size, VPID * vpid);
extern char *vfid_to_string (char *buf, int buf_size, VFID * vfid);
extern char *hfid_to_string (char *buf, int buf_size, HFID * hfid);
extern char *btid_to_string (char *buf, int buf_size, BTID * btid);

#endif /* _STORAGE_COMMON_H_ */
