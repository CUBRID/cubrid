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
#define PGLENGTH_MAX    SHRT_MAX

/* NULL_CHN is a special value for an unspecified cache coherency number.
 * It should only be used in error conditions.  This should never be
 * found as a stored chn in a disk or memory object.
 */
enum
{ NULL_CHN = -1, CHN_UNKNOWN_ATCLIENT = -2 };

/* Compose the full name of a database */

#if defined(WINDOWS)
#define PATHSLASH       '\\'

#define COMPOSE_FULL_NAME(buf, path, name)  \
  do { \
    strcpy(buf, path); \
    if (path[strlen(path) - 1] != PATHSLASH) { \
      strcat(buf, "\\"); \
    } \
    strcat(buf, name); \
  } while (0);
#else /* WINDOWS */
#define PATHSLASH       '/'

#define COMPOSE_FULL_NAME(buf, path, name) \
  do { \
    strcpy(buf, path); \
    if (path[strlen(path) - 1] != PATHSLASH) { \
      strcat(buf, "/"); \
    } \
    strcat(buf, name); \
  } while (0);
#endif /* WINDOWS */

/* Type definitions related to disk information	*/

typedef INT32 PAGEID;		/* Page identifier */

typedef INT16 VOLID;		/* Volume identifier */
typedef PAGEID DKNPAGES;	/* Number of disk pages */

typedef INT16 PGSLOTID;		/* Page slot identifier */
typedef PGSLOTID PGNSLOTS;	/* Number of slots on a page */
typedef INT16 PGLENGTH;		/* Page length */

typedef PAGEID FILEID;		/* File identifier */
typedef INT32 LOLENGTH;		/* Length for a large object */

/* Log address structure */

typedef struct log_lsa LOG_LSA;	/* Log address identifer */
struct log_lsa
{
  INT32 pageid;			/* Log page identifier */
  INT16 offset;			/* Offset in page */
};

#define LSA_COPY(lsa_ptr1, lsa_ptr2) *(lsa_ptr1) = *(lsa_ptr2)
#define LSA_SET_NULL(lsa_ptr)\
  do {									      \
    (lsa_ptr)->pageid = NULL_PAGEID;                                          \
    (lsa_ptr)->offset = NULL_OFFSET;                                          \
  } while(0)

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

#define ONE_K                   1024
#define IO_DEFAULT_PAGE_SIZE    (4 * ONE_K)
#define IO_MIN_PAGE_SIZE        (1 * ONE_K)
#define IO_MAX_PAGE_SIZE        (16 * ONE_K)

#define IO_PAGESIZE             (db_io_page_size())
#define DB_PAGESIZE             (db_page_size())
#define DB_MAX_PATH_LENGTH      PATH_MAX

typedef char *PAGE_PTR;		/* Pointer to a page */

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

/* File structure identifiers */

typedef struct vpid VPID;	/* REAL PAGE IDENTIFIER */
struct vpid
{
  INT32 pageid;			/* Page identifier */
  INT16 volid;			/* Volume identifier where the page reside */
};

typedef struct vfid VFID;	/* REAL FILE IDENTIFIER */
struct vfid
{
  INT32 fileid;			/* File identifier */
  INT16 volid;			/* Volume identifier where the file reside */
};

typedef struct hfid HFID;	/* FILE HEAP IDENTIFIER */
struct hfid
{
  VFID vfid;			/* Volume and file identifier */
  INT32 hpgid;			/* First page identifier (the header page) */
};

typedef struct loid LOID;	/* LARGE OBJECT IDENTIFIER */
struct loid
{
  VPID vpid;			/* Real page identifier */
  VFID vfid;			/* Real file identifier */
};

typedef struct btid BTID;	/* B+tree identifier */
struct btid
{
  VFID vfid;			/* B+tree index volume identifier */
  INT32 root_pageid;		/* Root page identifier */
};

typedef struct ehid EHID;	/* EXTENDIBLE HASHING IDENTIFIER */
struct ehid
{
  VFID vfid;			/* Volume and Directory file identifier */
  INT32 pageid;			/* The first (root) page of the directory */
};

typedef struct recdes RECDES;	/* RECORD DESCRIPTOR */
struct recdes
{
  int area_size;		/* Length of the allocated area. It includes
				   only the data field. The value is negative
				   if data is inside buffer. For example,
				   peeking in a slotted page. */
  int length;			/* Length of the data. Does not include the
				   length and type fields */
  INT16 type;			/* Type of record */
  char *data;			/* The data */
};

typedef struct lorecdes LORECDES;	/* Work area descriptor */
struct lorecdes
{
  LOLENGTH length;		/* The length of data in the area */
  LOLENGTH area_size;		/* The size of the area */
  char *data;			/* Pointer to the beginning of the area */
};

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

#define LOID_SET_NULL(loid) \
  do { \
    (loid)->vpid.pageid = NULL_PAGEID; \
    (loid)->vfid.fileid = NULL_FILEID; \
  } while (0)

#define LOID_IS_NULL(loid)  (((loid)->vpid.pageid == NULL_PAGEID) ? 1 : 0)

#define DISK_VOLPURPOSE DB_VOLPURPOSE

/* Types ans defines of transaction managment */

typedef int TRANID;		/* Transaction identifier      */

#define NULL_TRANID     (-1)
#define NULL_TRAN_INDEX (-1)

typedef enum
{
  /* Don't change the initialization since they reflect the elements of
     lock_Conv and lock_Comp */
  NULL_LOCK = 0,		/* NULL LOCK */
  IS_LOCK = 1,			/* Intention Shared lock */
  NS_LOCK = 2,			/* Next Key Shared lock */
  S_LOCK = 3,			/* Shared lock */
  IX_LOCK = 4,			/* Intention exclusive lock */
  SIX_LOCK = 5,			/* Shared and intention exclusive lock */
  U_LOCK = 6,			/* Update lock */
  NX_LOCK = 7,			/* Next Key Exclusive lock */
  X_LOCK = 8,			/* Exclusive lock */
  INCON_NON_TWO_PHASE_LOCK = -2,	/* Incompatible 2 phase lock. */
  NA_LOCK = -3			/* N/A lock */
} LOCK;

extern LOCK lock_Conv[][9];

#define LOCK_TO_LOCKMODE_STRING(lock) 			\
  (((lock) ==NULL_LOCK) ? "NULL_LOCK" :			\
   ((lock) ==  IS_LOCK) ? "  IS_LOCK" :			\
   ((lock) ==  NS_LOCK) ? "  NS_LOCK" :			\
   ((lock) ==   S_LOCK) ? "   S_LOCK" :			\
   ((lock) ==  IX_LOCK) ? "  IX_LOCK" :			\
   ((lock) == SIX_LOCK) ? " SIX_LOCK" :			\
   ((lock) ==   U_LOCK) ? "   U_LOCK" :			\
   ((lock) ==  NX_LOCK) ? "  NX_LOCK" :			\
   ((lock) ==   X_LOCK) ? "   X_LOCK" : "UNKNOWN")

/* CLASSNAME TO OID RETURN VALUES */

typedef enum
{
  LC_CLASSNAME_RESERVED,
  LC_CLASSNAME_DELETED,
  LC_CLASSNAME_EXIST,
  LC_CLASSNAME_EXIST_OTHER = LC_CLASSNAME_EXIST,
  LC_CLASSNAME_ERROR,
  LC_CLASSNAME_ERROR_AMBIG = LC_CLASSNAME_ERROR,
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

typedef enum
{
  BTREE_KEY_FOUND,
  BTREE_KEY_NOTFOUND,
  BTREE_ERROR_OCCURRED
} BTREE_SEARCH;

/* TYPEDEFS FOR BACKUP/RESTORE */

/* structure for passing arguments into boot_restart_server et. al. */
typedef struct bo_restart_arg BO_RESTART_ARG;
struct bo_restart_arg
{
  bool printtoc;		/* True to show backup's table of contents */
  time_t stopat;		/* the recovery stop time if restarting from
				   backup */
  const char *backuppath;	/* Pathname override for location of backup
				   volumes */
  int level;			/* The backup level to use */
  const char *verbose_file;	/* restoredb verbose msg file */
  bool newvolpath;		/* true: restore the database and log
				   volumes to the path specified in the
				   database-loc-file */
  bool restore_upto_bktime;
};

/* Magic default values */
#define CUBRID_MAGIC_MAX_LENGTH        25
#define CUBRID_MAGIC_DATABASE_VOLUME   "CUBRID/Volume"
#define CUBRID_MAGIC_LOG_ACTIVE        "CUBRID/LogActive"
#define CUBRID_MAGIC_LOG_ARCHIVE       "CUBRID/LogArchive"
#define CUBRID_MAGIC_LOG_INFO          "CUBRID/LogInfo"
#define CUBRID_MAGIC_DATABASE_BACKUP   "CUBRID/Backup"
/* Release string length in the log header */
#define CUBRID_REL_STRING_MAX_LENGTH    15

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
  S_DOESNT_EXIST		/* only for slotted page */
} SCAN_CODE;

extern INT16 db_page_size (void);
extern INT16 db_io_page_size (void);
extern INT16 db_set_page_size (INT16 iopagesize);
extern INT16 db_network_page_size (void);
extern void db_print_data (DB_TYPE type, DB_DATA * data, FILE * fd);

extern int recdes_allocate_data_area (RECDES * rec, int size);
extern void recdes_free_data_area (RECDES * rec);
extern void recdes_set_data_area (RECDES * rec, char *data, int size);

#endif /* _STORAGE_COMMON_H_ */
