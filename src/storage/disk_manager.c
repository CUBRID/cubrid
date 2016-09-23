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
 * disk_manager.c - Disk managment module (at server)
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "porting.h"
#include "disk_manager.h"
#include "memory_alloc.h"
#include "system_parameter.h"
#include "xserver_interface.h"
#include "storage_common.h"
#include "error_manager.h"
#include "file_io.h"
#include "page_buffer.h"
#include "log_manager.h"
#include "log_impl.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
#include "critical_section.h"
#include "boot_sr.h"
#include "environment_variable.h"
#include "event_log.h"
#include "tsc_timer.h"

#if !defined (SERVER_MODE)
#include "transaction_cl.h"
#endif

#include "bit.h"

/************************************************************************/
/* Define structures, globals, and macro's                              */
/************************************************************************/

typedef struct disk_recv_link_perm_volume DISK_RECV_LINK_PERM_VOLUME;
struct disk_recv_link_perm_volume
{				/* Recovery for links */
  INT16 next_volid;
  char next_vol_fullname[1];	/* Actually more than one */
};

typedef struct disk_recv_change_creation DISK_RECV_CHANGE_CREATION;
struct disk_recv_change_creation
{				/* Recovery for changes */
  INT64 db_creation;
  LOG_LSA chkpt_lsa;
  char vol_fullname[1];		/* Actually more than one */
};

typedef struct disk_recv_init_pages_info DISK_RECV_INIT_PAGES_INFO;
struct disk_recv_init_pages_info
{				/* Recovery for volume page init */
  INT32 start_pageid;
  DKNPAGES npages;
  INT16 volid;
};

/* show volume header context structure */
typedef struct disk_vol_header_context DISK_VOL_HEADER_CONTEXT;
struct disk_vol_header_context
{
  int volume_id;		/* volume id */
};

typedef struct disk_check_vol_info DISK_CHECK_VOL_INFO;
struct disk_check_vol_info
{
  VOLID volid;			/* volume id be found */
  bool exists;			/* whether volid does exist */
};

/************************************************************************/
/* Disk cache section                                                   */
/************************************************************************/

typedef struct disk_cache_volinfo DISK_CACHE_VOLINFO;
struct disk_cache_volinfo
{
  DB_VOLPURPOSE purpose;
  DKNSECTS nsect_free;		/* Hint of free sectors on volume */
};

typedef struct disk_extend_info DISK_EXTEND_INFO;
struct disk_extend_info
{
  volatile DKNSECTS nsect_free;
  volatile DKNSECTS nsect_total;
  volatile DKNSECTS nsect_max;
  volatile DKNSECTS nsect_intention;

  pthread_mutex_t mutex_reserve;
#if !defined (NDEBUG)
  volatile int owner_reserve;
  volatile int owner_extend;
#endif				/* !NDEBUG */

  VOLID volid_extend;
  DB_VOLTYPE voltype;
};

typedef struct disk_perm_info DISK_PERM_PURPOSE_INFO;
struct disk_perm_info
{
  DISK_EXTEND_INFO extend_info;
  DKNSECTS nsect_vol_max;
};

typedef struct disk_temp_info DISK_TEMP_PURPOSE_INFO;
struct disk_temp_info
{
  DISK_EXTEND_INFO extend_info;
  DKNSECTS nsect_vol_max;
  DKNSECTS nsect_perm_free;
  DKNSECTS nsect_perm_total;
};

typedef struct disk_cache DISK_CACHE;
struct disk_cache
{
  int nvols_perm;		/* number of permanent type volumes */
  int nvols_temp;		/* number of temporary type volumes */
  DISK_CACHE_VOLINFO vols[LOG_MAX_DBVOLID + 1];	/* volume info array */

  DISK_PERM_PURPOSE_INFO perm_purpose_info;	/* info for permanent purpose */
  DISK_TEMP_PURPOSE_INFO temp_purpose_info;	/* info for temporary purpose */

  pthread_mutex_t mutex_extend;	/* note: never get expand mutex while keeping reserve mutexes */
#if !defined (NDEBUG)
  volatile int owner_extend;
#endif				/* !NDEBUG */
};

static DISK_CACHE *disk_Cache = NULL;

static DKNSECTS disk_Temp_max_sects = -2;

/************************************************************************/
/* Disk allocation table section                                        */
/************************************************************************/

/* Disk allocation table is a bitmap that keeps track of reserved sectors. When files are created or extended, they
 * reserve a number of sectors from disk (each sector containing a predefined number of pages). */

/* The default unit used to divide a page of allocation table. Currently it is a char (8 bit).
 * If we ever want to change the type of unit, this can be modified and should be handled automatically. However, some
 * other structures may need updating (e.g. DISK_ALLOCTBL_CURSOR).
 */
typedef UINT64 DISK_STAB_UNIT;
#define DISK_STAB_UNIT_SIZE_OF sizeof (DISK_STAB_UNIT)

/* Disk allocation table cursor. Used to iterate through table bits. */
typedef struct disk_stab_cursor DISK_STAB_CURSOR;
struct disk_stab_cursor
{
  const DISK_VAR_HEADER *volheader;	/* Volume header */

  PAGEID pageid;		/* Current page ID */
  int offset_to_unit;		/* Offset to current unit in page. */
  int offset_to_bit;		/* Offset to current bit in unit. */

  SECTID sectid;		/* Sector ID */

  PAGE_PTR page;		/* Fixed table page. */
  DISK_STAB_UNIT *unit;		/* Unit pointer in current page. */
};
#define DISK_STAB_CURSOR_INITIALIZER { NULL, 0, 0, 0, 0, NULL, NULL }

/* Allocation table macro's */
/* Bit count in a unit */
#define DISK_STAB_UNIT_BIT_COUNT	    ((int) (DISK_STAB_UNIT_SIZE_OF * CHAR_BIT))
/* Unit count in a table page */
#define DISK_STAB_PAGE_UNITS_COUNT	    ((int) (DB_PAGESIZE / DISK_STAB_UNIT_BIT_COUNT))
/* Bit count in a table page */
#define DISK_STAB_PAGE_BIT_COUNT	    ((int) (DISK_STAB_UNIT_BIT_COUNT * DISK_STAB_PAGE_UNITS_COUNT))

/* Get page offset for sector ID. Note this is not the real page ID (since table does not start from page 0). */
#define DISK_ALLOCTBL_SECTOR_PAGE_OFFSET(sect) ((sect) / DISK_STAB_PAGE_BIT_COUNT)
/* Get unit offset in page for sector ID. */
#define DISK_ALLOCTBL_SECTOR_UNIT_OFFSET(sect) (((sect) % DISK_STAB_PAGE_BIT_COUNT) / DISK_STAB_UNIT_BIT_COUNT)
/* Get bit offset in unit for sector ID */
#define DISK_ALLOCTBL_SECTOR_BIT_OFFSET(sect) (((sect) % DISK_STAB_PAGE_BIT_COUNT) % DISK_STAB_UNIT_BIT_COUNT)

#define DISK_SECTS_ROUND_UP(nsects)  (CEIL_PTVDIV (nsects, DISK_STAB_UNIT_BIT_COUNT) * DISK_STAB_UNIT_BIT_COUNT)
#define DISK_SECTS_ROUND_DOWN(nsects)  ((nsects / DISK_STAB_UNIT_BIT_COUNT) * DISK_STAB_UNIT_BIT_COUNT)
#define DISK_SECTS_ASSERT_ROUNDED(nsects)  assert (nsects == DISK_SECTS_ROUND_DOWN (nsects));

/* function used by disk_stab_iterate_units */
typedef int (*DISK_STAB_UNIT_FUNC) (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args);

/************************************************************************/
/* Sector reserve section                                               */
/************************************************************************/

typedef struct disk_cache_vol_reserve DISK_CACHE_VOL_RESERVE;
struct disk_cache_vol_reserve
{
  VOLID volid;
  DKNSECTS nsect;
};
#define DISK_PRERESERVE_BUF_DEFAULT 16

typedef struct disk_reserve_context DISK_RESERVE_CONTEXT;
struct disk_reserve_context
{
  int nsect_total;
  int nsect_reserve_remaining;
  VSID *vsidp;

  DISK_CACHE_VOL_RESERVE cache_vol_reserve[VOLID_MAX];
  int n_cache_vol_reserve;
  int n_cache_reserve_remaining;

  DKNSECTS nsects_lastvol_remaining;

  DB_VOLPURPOSE purpose;
};

/************************************************************************/
/* Disk create, extend, destroy section                                 */
/************************************************************************/

/* minimum volume sectors for create/expand... at least one allocation table unit */
#define DISK_MIN_VOLUME_SECTS DISK_STAB_UNIT_BIT_COUNT

/* when allocating remaining disk space, leave out 64MB for safety */
#define DISK_SAFE_OSDISK_FREE_SPACE (64 * 1024 * 1024)
#define DISK_SAFE_OSDISK_FREE_SECTS (DISK_SAFE_OSDISK_FREE_SPACE / IO_SECTORSIZE)

/************************************************************************/
/* Declare static functions.                                            */
/************************************************************************/

STATIC_INLINE char *disk_vhdr_get_vol_fullname (const DISK_VAR_HEADER * vhdr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE char *disk_vhdr_get_next_vol_fullname (const DISK_VAR_HEADER * vhdr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE char *disk_vhdr_get_vol_remarks (const DISK_VAR_HEADER * vhdr) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int disk_vhdr_length_of_varfields (const DISK_VAR_HEADER * vhdr) __attribute__ ((ALWAYS_INLINE));
static int disk_vhdr_set_vol_fullname (DISK_VAR_HEADER * vhdr, const char *vol_fullname);
static int disk_vhdr_set_next_vol_fullname (DISK_VAR_HEADER * vhdr, const char *next_vol_fullname);
static int disk_vhdr_set_vol_remarks (DISK_VAR_HEADER * vhdr, const char *vol_remarks);

static bool disk_cache_load_all_volumes (THREAD_ENTRY * thread_p);
static bool disk_cache_load_volume (THREAD_ENTRY * thread_p, INT16 volid, void *ignore);

static const char *disk_purpose_to_string (DISK_VOLPURPOSE purpose);
static const char *disk_type_to_string (DB_VOLTYPE voltype);

static int disk_stab_dump (THREAD_ENTRY * thread_p, FILE * fp, const DISK_VAR_HEADER * volheader);

static int disk_dump_volume_system_info (THREAD_ENTRY * thread_p, FILE * fp, INT16 volid);
static bool disk_dump_goodvol_all (THREAD_ENTRY * thread_p, INT16 volid, void *ignore);
static void disk_vhdr_dump (FILE * fp, const DISK_VAR_HEADER * vhdr);

STATIC_INLINE void disk_verify_volume_header (THREAD_ENTRY * thread_p, PAGE_PTR pgptr) __attribute__ ((ALWAYS_INLINE));
static bool disk_check_volume_exist (THREAD_ENTRY * thread_p, VOLID volid, void *arg);

/************************************************************************/
/* Disk sector table section                                            */
/************************************************************************/

STATIC_INLINE void disk_stab_cursor_set_at_sectid (const DISK_VAR_HEADER * volheader, SECTID sectid,
						   DISK_STAB_CURSOR * cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_stab_cursor_set_at_end (const DISK_VAR_HEADER * volheader, DISK_STAB_CURSOR * cursor)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_stab_cursor_set_at_start (const DISK_VAR_HEADER * volheader, DISK_STAB_CURSOR * cursor)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int disk_stab_cursor_compare (const DISK_STAB_CURSOR * first_cursor,
					    const DISK_STAB_CURSOR * second_cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_stab_cursor_check_valid (const DISK_STAB_CURSOR * cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool disk_stab_cursor_is_bit_set (const DISK_STAB_CURSOR * cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_stab_cursor_set_bit (DISK_STAB_CURSOR * cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_stab_cursor_clear_bit (DISK_STAB_CURSOR * cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int disk_stab_cursor_get_bit_index_in_page (const DISK_STAB_CURSOR * cursor)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE SECTID disk_stab_cursor_get_sectid (const DISK_STAB_CURSOR * cursor) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE int disk_stab_cursor_fix (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor,
					PGBUF_LATCH_MODE latch_mode) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_stab_cursor_unfix (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor)
  __attribute__ ((ALWAYS_INLINE));
static int disk_stab_unit_reserve (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args);

static int disk_stab_iterate_units (THREAD_ENTRY * thread_p, const DISK_VAR_HEADER * volheader, PGBUF_LATCH_MODE mode,
				    DISK_STAB_CURSOR * start, DISK_STAB_CURSOR * end, DISK_STAB_UNIT_FUNC f_unit,
				    void *f_unit_args);
static int disk_stab_iterate_units_all (THREAD_ENTRY * thread_p, const DISK_VAR_HEADER * volheader,
					PGBUF_LATCH_MODE mode, DISK_STAB_UNIT_FUNC f_unit, void *f_unit_args);
static int disk_stab_dump_unit (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args);
static int disk_stab_count_free (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args);
static int disk_stab_set_bits_contiguous (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args);

/************************************************************************/
/* Disk cache section                                                   */
/************************************************************************/

static int disk_volume_boot (THREAD_ENTRY * thread_p, VOLID volid, DB_VOLPURPOSE * purpose_out,
			     DB_VOLTYPE * voltype_out, VOL_SPACE_INFO * space_out);
STATIC_INLINE void disk_cache_lock_reserve (DISK_EXTEND_INFO * expand_info) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_cache_unlock_reserve (DISK_EXTEND_INFO * expand_info) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_cache_lock_reserve_for_purpose (DB_VOLPURPOSE purpose) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_cache_unlock_reserve_for_purpose (DB_VOLPURPOSE purpose) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_cache_update_vol_free (VOLID volid, DKNSECTS delta_free) __attribute__ ((ALWAYS_INLINE));

/************************************************************************/
/* Sector reserve section                                               */
/************************************************************************/

static int disk_reserve_sectors_in_volume (THREAD_ENTRY * thread_p, int vol_index, DISK_RESERVE_CONTEXT * context);
static DISK_ISVALID disk_is_sector_reserved (THREAD_ENTRY * thread_p, const DISK_VAR_HEADER * volheader, SECTID sectid);
static int disk_reserve_from_cache (THREAD_ENTRY * thread_p, DISK_RESERVE_CONTEXT * context);
STATIC_INLINE void disk_reserve_from_cache_vols (DB_VOLTYPE type, DISK_RESERVE_CONTEXT * context)
  __attribute__ ((ALWAYS_INLINE));
static int disk_extend (THREAD_ENTRY * thread_p, DISK_EXTEND_INFO * expand_info,
			DISK_RESERVE_CONTEXT * reserve_context);
static int disk_volume_expand (THREAD_ENTRY * thread_p, VOLID volid, DB_VOLTYPE voltype, DKNSECTS nsect_extend,
			       DKNSECTS * nsect_extended_out);
static int disk_add_volume (THREAD_ENTRY * thread_p, DBDEF_VOL_EXT_INFO * extinfo, VOLID * volid_out,
			    DKNSECTS * nsects_free_out);
STATIC_INLINE void disk_reserve_from_cache_volume (VOLID volid, DISK_RESERVE_CONTEXT * context)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_cache_free_reserved (DISK_RESERVE_CONTEXT * context) __attribute__ ((ALWAYS_INLINE));
static int disk_unreserve_sectors_from_volume (THREAD_ENTRY * thread_p, VOLID volid, DISK_RESERVE_CONTEXT * context);
static int disk_stab_unit_unreserve (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args);

/************************************************************************/
/* Other                                                                */
/************************************************************************/

static int disk_stab_init (THREAD_ENTRY * thread_p, DISK_VAR_HEADER * volheader);
STATIC_INLINE void disk_set_alloctables (DB_VOLPURPOSE vol_purpose, DISK_VAR_HEADER * volheader)
  __attribute__ ((ALWAYS_INLINE));

static int disk_format (THREAD_ENTRY * thread_p, const char *dbname, INT16 volid, DBDEF_VOL_EXT_INFO * ext_info,
			DKNSECTS * nsect_free_out);

STATIC_INLINE int disk_get_volheader (THREAD_ENTRY * thread_p, VOLID volid, PGBUF_LATCH_MODE latch_mode,
				      PAGE_PTR * page_volheader_out, DISK_VAR_HEADER ** volheader_out)
  __attribute__ ((ALWAYS_INLINE));
static int disk_cache_init (void);
STATIC_INLINE bool disk_is_valid_volid (VOLID volid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_VOLPURPOSE disk_get_volpurpose (VOLID volid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE DB_VOLTYPE disk_get_voltype (VOLID volid) __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE bool disk_compatible_type_and_purpose (DB_VOLTYPE type, DB_VOLPURPOSE purpose)
  __attribute__ ((ALWAYS_INLINE));
STATIC_INLINE void disk_check_own_reserve_for_purpose (DB_VOLPURPOSE purpose) __attribute__ ((ALWAYS_INLINE));
static DISK_ISVALID disk_check_volume (THREAD_ENTRY * thread_p, INT16 volid, bool repair);

/************************************************************************/
/* End of static functions                                              */
/************************************************************************/

/************************************************************************/
/* Define functions.                                                    */
/************************************************************************/

STATIC_INLINE char *
disk_vhdr_get_vol_fullname (const DISK_VAR_HEADER * vhdr)
{
  return ((char *) (vhdr->var_fields + vhdr->offset_to_vol_fullname));
}

STATIC_INLINE char *
disk_vhdr_get_next_vol_fullname (const DISK_VAR_HEADER * vhdr)
{
  return ((char *) (vhdr->var_fields + vhdr->offset_to_next_vol_fullname));
}

STATIC_INLINE char *
disk_vhdr_get_vol_remarks (const DISK_VAR_HEADER * vhdr)
{
  return ((char *) (vhdr->var_fields + vhdr->offset_to_vol_remarks));
}

STATIC_INLINE int
disk_vhdr_length_of_varfields (const DISK_VAR_HEADER * vhdr)
{
  return (vhdr->offset_to_vol_remarks + (int) strlen (disk_vhdr_get_vol_remarks (vhdr)));
}

/*
 * disk_cache_load_volume () - load and cache volume information
 *
 * return        : true if successful, false otherwise
 * thread_p (in) : thread entry
 * volid (in)    : volume identifier
 * ignore (in)   : not used
 */
static bool
disk_cache_load_volume (THREAD_ENTRY * thread_p, INT16 volid, void *ignore)
{
  DB_VOLPURPOSE vol_purpose;
  DB_VOLTYPE vol_type;
  VOL_SPACE_INFO space_info;

  if (disk_volume_boot (thread_p, volid, &vol_purpose, &vol_type, &space_info) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return false;
    }

  if (vol_type != DB_PERMANENT_VOLTYPE)
    {
      /* don't save temporary volumes... they will be dropped anyway */
      return true;
    }

  /* called during boot, no sync required */
  if (vol_purpose == DB_PERMANENT_DATA_PURPOSE)
    {
      disk_Cache->perm_purpose_info.extend_info.nsect_free += space_info.n_free_sects;
      disk_Cache->perm_purpose_info.extend_info.nsect_total = space_info.n_total_sects;
      disk_Cache->perm_purpose_info.extend_info.nsect_max = space_info.n_max_sects;

      if (space_info.n_total_sects < space_info.n_max_sects)
	{
	  assert (disk_Cache->perm_purpose_info.extend_info.volid_extend == NULL_VOLID);
	  disk_Cache->perm_purpose_info.extend_info.volid_extend = volid;
	}
    }
  else
    {
      assert (space_info.n_total_sects == space_info.n_max_sects);

      disk_Cache->temp_purpose_info.nsect_perm_free += space_info.n_free_sects;
      disk_Cache->temp_purpose_info.nsect_perm_total += space_info.n_total_sects;
    }

  disk_Cache->vols[volid].nsect_free = space_info.n_free_sects;
  disk_Cache->vols[volid].purpose = vol_purpose;

  disk_Cache->nvols_perm++;
  return true;
}

/*
 * disk_cache_init () - initialize disk cache
 *
 * return    : error code
 * void (in) : void
 */
static int
disk_cache_init (void)
{
  int i;

  assert (disk_Cache == NULL);

  disk_Cache = (DISK_CACHE *) malloc (sizeof (DISK_CACHE));
  if (disk_Cache == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DISK_CACHE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  disk_Cache->nvols_perm = 0;
  disk_Cache->nvols_temp = 0;

  disk_Cache->perm_purpose_info.nsect_vol_max =
    (DKNSECTS) (prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE) / IO_SECTORSIZE);
  disk_Cache->perm_purpose_info.extend_info.nsect_free = 0;
  disk_Cache->perm_purpose_info.extend_info.nsect_total = 0;
  disk_Cache->perm_purpose_info.extend_info.nsect_max = 0;
  disk_Cache->perm_purpose_info.extend_info.nsect_intention = 0;
  disk_Cache->perm_purpose_info.extend_info.voltype = DB_PERMANENT_VOLTYPE;
  disk_Cache->perm_purpose_info.extend_info.volid_extend = NULL_VOLID;
  pthread_mutex_init (&disk_Cache->perm_purpose_info.extend_info.mutex_reserve, NULL);
#if !defined (NDEBUG)
  disk_Cache->perm_purpose_info.extend_info.owner_reserve = -1;
#endif /* !NDEBUG */

  disk_Cache->temp_purpose_info.nsect_vol_max =
    (DKNSECTS) (prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE) / IO_SECTORSIZE);
  disk_Cache->temp_purpose_info.nsect_perm_free = 0;
  disk_Cache->temp_purpose_info.nsect_perm_total = 0;
  disk_Cache->temp_purpose_info.extend_info.nsect_free = 0;
  disk_Cache->temp_purpose_info.extend_info.nsect_total = 0;
  disk_Cache->temp_purpose_info.extend_info.nsect_max = 0;
  disk_Cache->temp_purpose_info.extend_info.nsect_intention = 0;
  disk_Cache->temp_purpose_info.extend_info.voltype = DB_TEMPORARY_VOLTYPE;
  disk_Cache->temp_purpose_info.extend_info.volid_extend = NULL_VOLID;
  pthread_mutex_init (&disk_Cache->temp_purpose_info.extend_info.mutex_reserve, NULL);
#if !defined (NDEBUG)
  disk_Cache->temp_purpose_info.extend_info.owner_reserve = -1;
#endif /* !NDEBUG */

  pthread_mutex_init (&disk_Cache->mutex_extend, NULL);
#if !defined (NDEBUG)
  disk_Cache->owner_extend = -1;
#endif /* !NDEBUG */

  for (i = 0; i <= LOG_MAX_DBVOLID; i++)
    {
      disk_Cache->vols[i].purpose = DISK_UNKNOWN_PURPOSE;
      disk_Cache->vols[i].nsect_free = 0;
    }
  return NO_ERROR;
}

/*
 * disk_cache_load_all_volumes () - load all disk volumes and save info to disk cache
 *
 * return        : true if successful, false otherwise
 * thread_p (in) : thread entry
 */
static bool
disk_cache_load_all_volumes (THREAD_ENTRY * thread_p)
{
  /* Cache every single volume */
  assert (disk_Cache != NULL);
  return fileio_map_mounted (thread_p, disk_cache_load_volume, NULL);
}

/*
 * disk_purpose_to_string () - Return the volume purpose in string format
 *   return:
 *   vol_purpose(in): Purpose of volume
 */
static const char *
disk_purpose_to_string (DISK_VOLPURPOSE vol_purpose)
{
  switch (vol_purpose)
    {
    case DB_PERMANENT_DATA_PURPOSE:
      return "Permanent data purpose";
    case DB_TEMPORARY_DATA_PURPOSE:
      return "Temporary data purpose";
    default:
      assert (false);
      break;
    }
  return "Unknown purpose";
}

/*
 * disk_type_to_string () - volume type to string
 *
 * return       : volume type to string
 * voltype (in) : volume type
 */
static const char *
disk_type_to_string (DB_VOLTYPE voltype)
{
  return voltype == DB_PERMANENT_VOLTYPE ? "Permanent Volume" : "Temporary Volume";
}

/*
 * disk_vhdr_set_vol_fullname () -
 *   return: NO_ERROR
 *   vhdr(in):
 *   vol_fullname(in):
 */
static int
disk_vhdr_set_vol_fullname (DISK_VAR_HEADER * vhdr, const char *vol_fullname)
{
  int length_diff;
  int length_to_move;
  int ret = NO_ERROR;

  length_diff = vhdr->offset_to_vol_remarks;

  length_to_move = (length_diff + (int) strlen (vhdr->var_fields + length_diff) + 1
		    - vhdr->offset_to_next_vol_fullname);

  /* Difference in length between new name and old name */
  length_diff = (((int) strlen (vol_fullname) + 1)
		 - (vhdr->offset_to_next_vol_fullname - vhdr->offset_to_vol_fullname));

  if (length_diff != 0)
    {
      /* We need to either move to right(expand) or left(shrink) the rest of the variable length fields */
      memmove (disk_vhdr_get_next_vol_fullname (vhdr) + length_diff, disk_vhdr_get_next_vol_fullname (vhdr),
	       length_to_move);
      vhdr->offset_to_next_vol_fullname += length_diff;
      vhdr->offset_to_vol_remarks += length_diff;
    }

  (void) memcpy (disk_vhdr_get_vol_fullname (vhdr), vol_fullname,
		 MIN ((ssize_t) strlen (vol_fullname) + 1, DB_MAX_PATH_LENGTH));
  return ret;
}

/*
 * disk_vhdr_set_next_vol_fullname () -
 *   return: NO_ERROR
 *   vhdr(in):
 *   next_vol_fullname(in):
 */
static int
disk_vhdr_set_next_vol_fullname (DISK_VAR_HEADER * vhdr, const char *next_vol_fullname)
{
  int length_diff;
  int length_to_move;
  int ret = NO_ERROR;
  int next_vol_fullname_size;

  if (next_vol_fullname == NULL)
    {
      next_vol_fullname = "";
      next_vol_fullname_size = 1;
    }
  else
    {
      next_vol_fullname_size = strlen (next_vol_fullname) + 1;
      if (next_vol_fullname_size > PATH_MAX)
	{
	  next_vol_fullname_size = PATH_MAX;
	}
    }

  length_diff = vhdr->offset_to_vol_remarks;

  length_to_move = strlen (vhdr->var_fields + length_diff) + 1;

  /* Difference in length between new name and old name */
  length_diff = (next_vol_fullname_size - (vhdr->offset_to_vol_remarks - vhdr->offset_to_next_vol_fullname));

  if (length_diff != 0)
    {
      /* We need to either move to right(expand) or left(shrink) the rest of the variable length fields */
      memmove (disk_vhdr_get_vol_remarks (vhdr) + length_diff, disk_vhdr_get_vol_remarks (vhdr), length_to_move);
      vhdr->offset_to_vol_remarks += length_diff;
    }

  (void) memcpy (disk_vhdr_get_next_vol_fullname (vhdr), next_vol_fullname, next_vol_fullname_size);

  return ret;
}

/*
 * disk_vhdr_set_vol_remarks () -
 *   return: NO_ERROR
 *   vhdr(in):
 *   vol_remarks(in):
 */
static int
disk_vhdr_set_vol_remarks (DISK_VAR_HEADER * vhdr, const char *vol_remarks)
{
  int maxsize;
  int ret = NO_ERROR;

  if (vol_remarks != NULL)
    {
      maxsize = (DB_PAGESIZE - offsetof (DISK_VAR_HEADER, var_fields) - vhdr->offset_to_vol_remarks);

      if ((int) strlen (vol_remarks) > maxsize)
	{
	  /* Does not fit.. Truncate the comment */
	  (void) strncpy (disk_vhdr_get_vol_remarks (vhdr), vol_remarks, maxsize - 1);
	  vhdr->var_fields[maxsize] = '\0';
	}
      else
	{
	  (void) strcpy (disk_vhdr_get_vol_remarks (vhdr), vol_remarks);
	}
    }
  else
    {
      vhdr->var_fields[vhdr->offset_to_vol_remarks] = '\0';
    }

  return ret;
}

/*
 * disk_format () - format new volume
 *
 * return               : error code
 * thread_p (in)        : thread entry
 * dbname (in)          : database name
 * volid (in)           : new volume identifier
 * ext_info (in)        : all extension info
 * nsect_free_out (out) : output number of free sectors in new volume
 */
int
disk_format (THREAD_ENTRY * thread_p, const char *dbname, VOLID volid, DBDEF_VOL_EXT_INFO * ext_info,
	     DKNSECTS * nsect_free_out)
{
  int vdes;			/* Volume descriptor */
  DISK_VAR_HEADER *vhdr;	/* Pointer to volume header */
  VPID vpid;			/* Volume and page identifiers */
  LOG_DATA_ADDR addr;		/* Address of logging data */
  const char *vol_fullname = ext_info->name;
  DKNSECTS max_npages = ext_info->max_npages * DISK_SECTOR_NPAGES;
  int kbytes_to_be_written_per_sec = ext_info->max_writesize_in_sec;
  DISK_VOLPURPOSE vol_purpose = ext_info->purpose;
  DKNPAGES extend_npages = ext_info->nsect_total * DISK_SECTOR_NPAGES;
  INT16 prev_volid;

  int error_code = NO_ERROR;

  assert ((int) sizeof (DISK_VAR_HEADER) <= DB_PAGESIZE);

  addr.vfid = NULL;

  if ((strlen (vol_fullname) + 1 > DB_MAX_PATH_LENGTH)
      || (DB_PAGESIZE < (SSIZEOF (DISK_VAR_HEADER) + strlen (vol_fullname) + 1)))
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, NULL, vol_fullname,
	      strlen (vol_fullname) + 1, DB_MAX_PATH_LENGTH);
      return ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG;
    }

  /* make sure that this is a valid purpose */
  if (vol_purpose != DB_PERMANENT_DATA_PURPOSE && vol_purpose != DB_TEMPORARY_DATA_PURPOSE)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DISK_UNKNOWN_PURPOSE, 3, vol_purpose, DB_PERMANENT_DATA_PURPOSE,
	      DB_TEMPORARY_DATA_PURPOSE);
      return ER_DISK_UNKNOWN_PURPOSE;
    }

  /* safe guard: permanent volumes with temporary data purpose must be maxed */
  assert (vol_purpose != DB_TEMPORARY_DATA_PURPOSE || ext_info->voltype != DB_PERMANENT_VOLTYPE
	  || ext_info->nsect_total == ext_info->nsect_max);

  /* undo must be logical since we are going to remove the volume in the case of rollback (really a crash since we are
   * in a top operation) */
  addr.offset = 0;
  addr.pgptr = NULL;
  log_append_undo_data (thread_p, RVDK_FORMAT, &addr, (int) strlen (vol_fullname) + 1, vol_fullname);
  /* this log must be flushed. */
  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);

  /* create and initialize the volume. recovery information is initialized in every page. */
  vdes =
    fileio_format (thread_p, dbname, vol_fullname, volid, extend_npages, vol_purpose == DB_PERMANENT_DATA_PURPOSE,
		   false, false, IO_PAGESIZE, kbytes_to_be_written_per_sec, false);
  if (vdes == NULL_VOLDES)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  /* initialize the volume header and the sector and page allocation tables */
  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /* lock the volume header in exclusive mode and then fetch the page. */
  addr.pgptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_VOLHEADER);

  /* initialize the header */
  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  strncpy (vhdr->magic, CUBRID_MAGIC_DATABASE_VOLUME, CUBRID_MAGIC_MAX_LENGTH);
  vhdr->iopagesize = IO_PAGESIZE;
  vhdr->volid = volid;
  vhdr->purpose = vol_purpose;
  vhdr->type = ext_info->voltype;
  vhdr->sect_npgs = DISK_SECTOR_NPAGES;
  vhdr->nsect_total = ext_info->nsect_total;
  vhdr->nsect_max = ext_info->nsect_max;
  vhdr->db_charset = lang_charset ();
  vhdr->hint_allocsect = NULL_SECTID;
  vhdr->dummy1 = vhdr->dummy2 = vhdr->dummy3 = 0;	/* useless */

  /* set sector table info in volume header */
  disk_set_alloctables (vol_purpose, vhdr);
  if (vhdr->sys_lastpage >= extend_npages)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      (void) pgbuf_invalidate_all (thread_p, volid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_BAD_NPAGES, 2, vol_fullname, max_npages);
      return ER_IO_FORMAT_BAD_NPAGES;
    }

  /* Find the time of the creation of the database and the current LSA checkpoint. */

  if (log_get_db_start_parameters (&vhdr->db_creation, &vhdr->chkpt_lsa) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return NULL_VOLID;
    }

  /* Initialize the system heap file for booting purposes. This field is reseted after the heap file is created by the
   * boot manager */

  vhdr->boot_hfid.vfid.volid = NULL_VOLID;
  vhdr->boot_hfid.vfid.fileid = NULL_PAGEID;
  vhdr->boot_hfid.hpgid = NULL_PAGEID;

  /* Initialize variable length fields */

  vhdr->next_volid = NULL_VOLID;
  vhdr->offset_to_vol_fullname = vhdr->offset_to_next_vol_fullname = vhdr->offset_to_vol_remarks = 0;
  vhdr->var_fields[vhdr->offset_to_vol_fullname] = '\0';
  error_code = disk_vhdr_set_vol_fullname (vhdr, vol_fullname);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return error_code;
    }

  error_code = disk_vhdr_set_next_vol_fullname (vhdr, NULL);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return error_code;
    }

  error_code = disk_vhdr_set_vol_remarks (vhdr, ext_info->comments);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return error_code;
    }

  /* Make sure that in the case of a crash, the volume is created. Otherwise, the recovery will not work */

  if (ext_info->voltype == DB_PERMANENT_VOLTYPE)
    {
      log_append_dboutside_redo (thread_p, RVDK_NEWVOL, sizeof (*vhdr) + disk_vhdr_length_of_varfields (vhdr), vhdr);

      /* Even though the volume header page is not completed at this moment, to write REDO log for the header page is
       * crucial for redo recovery since disk_map_init and disk_set_link will write their redo logs. These functions
       * will access the header page during restart recovery. Another REDO log for RVDK_FORMAT will be written to
       * completely log the header page including the volume link. */
      addr.offset = 0;		/* Header is located at position zero */
      log_append_redo_data (thread_p, RVDK_FORMAT, &addr, sizeof (*vhdr) + disk_vhdr_length_of_varfields (vhdr), vhdr);
    }

  /* Now initialize the sector and page allocator tables and link the volume to previous allocated volume */
  prev_volid = fileio_find_previous_perm_volume (thread_p, volid);
  error_code = disk_stab_init (thread_p, vhdr);
  if (error_code != NO_ERROR)
    {
      /* Problems setting the map allocation tables, release the header page, dismount and destroy the volume, and
       * return */
      ASSERT_ERROR ();
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      (void) pgbuf_invalidate_all (thread_p, volid);
      return error_code;
    }
  if (ext_info->voltype == DB_PERMANENT_VOLTYPE && volid != LOG_DBFIRST_VOLID)
    {
      error_code = disk_set_link (thread_p, prev_volid, volid, vol_fullname, true, DISK_FLUSH);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();

	  pgbuf_unfix_and_init (thread_p, addr.pgptr);

	  (void) pgbuf_invalidate_all (thread_p, volid);
	  return error_code;
	}
    }

  if (ext_info->voltype == DB_PERMANENT_VOLTYPE)
    {
      addr.offset = 0;		/* Header is located at position zero */
      log_append_redo_data (thread_p, RVDK_FORMAT, &addr, sizeof (*vhdr) + disk_vhdr_length_of_varfields (vhdr), vhdr);
    }

  /* if this is a volume with temporary purposes, we do not log any disk driver related changes any longer.
   * indicate that by setting the disk pages to temporary lsa */
  if (vol_purpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      /* todo: understand what this code is supposed to do */
      PAGE_PTR pgptr = NULL;	/* Page pointer */
      LOG_LSA init_with_temp_lsa;	/* A lsa for temporary purposes */

      /* Flush the pages so that the log is forced */
      (void) pgbuf_flush_all (thread_p, volid);

      for (vpid.volid = volid, vpid.pageid = DISK_VOLHEADER_PAGE; vpid.pageid <= vhdr->sys_lastpage; vpid.pageid++)
	{
	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	  if (pgptr != NULL)
	    {
	      pgbuf_set_lsa_as_temporary (thread_p, pgptr);
	      pgbuf_unfix_and_init (thread_p, pgptr);
	    }
	}
      if (ext_info->voltype == DB_PERMANENT_VOLTYPE)
	{
	  LSA_SET_INIT_TEMP (&init_with_temp_lsa);
	  /* Flush all dirty pages and then invalidate them from page buffer pool. So that we can reset the recovery
	   * information directly using the io module */

	  (void) pgbuf_invalidate_all (thread_p, volid);	/* Flush and invalidate */
	  error_code = fileio_reset_volume (thread_p, vdes, vol_fullname, max_npages, &init_with_temp_lsa);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      /* Problems reseting the pages of the permanent volume for temporary storage purposes... That is, with a
	       * tempvol LSA. dismount and destroy the volume, and return */
	      pgbuf_unfix_and_init (thread_p, addr.pgptr);
	      return error_code;
	    }
	}
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  *nsect_free_out = vhdr->nsect_total - SECTOR_FROM_PAGEID (vhdr->sys_lastpage);
  pgbuf_set_dirty_and_free (thread_p, addr.pgptr);

  /* Flush all pages that were formatted. This is not needed, but it is done for security reasons to identify the volume
   * in case of a system crash. Note that the identification may not be possible during media crashes */
  (void) pgbuf_flush_all (thread_p, volid);
  (void) fileio_synchronize (thread_p, vdes, vol_fullname);

  /* If this is a permanent volume for temporary storage purposes, indicate so to page buffer manager, so that fetches
   * of new pages can be initialized with temporary lsa..which will avoid logging. */
  if (ext_info->voltype == DB_PERMANENT_VOLTYPE && vol_purpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      pgbuf_cache_permanent_volume_for_temporary (volid);
    }
  /* todo: temporary is not logged because code should avoid it. this complicated system that uses page buffer should
   * not be necessary. with the exception of file manager and disk manager, who already manage to skip logging on
   * temporary files, all other changes on temporary pages are really not logged. so why bother? */

  return NO_ERROR;
}

/*
 * disk_unformat () - Destroy/unformat a volume with the given name
 *   return: NO_ERROR
 *   vol_fullname(in): Full name of volume to unformat
 */
int
disk_unformat (THREAD_ENTRY * thread_p, const char *vol_fullname)
{
  INT16 volid;
  int ret = NO_ERROR;

  volid = fileio_find_volume_id_with_label (thread_p, vol_fullname);
  if (volid != NULL_VOLID)
    {
      (void) pgbuf_flush_all (thread_p, volid);
      (void) pgbuf_invalidate_all (thread_p, volid);
    }

  fileio_unformat (thread_p, vol_fullname);

  return ret;
}

/*
 * disk_set_creation () - Change database creation information of the
 *                            given volume
 *   return: NO_ERROR
 *   volid(in): Volume identifier
 *   new_vol_fullname(in): New volume label/name
 *   new_dbcreation(in): New database creation time
 *   new_chkptlsa(in): New checkpoint
 *   logchange(in): Whether or not to log the change
 *   flush_page(in): true for flush dirty page. otherwise, false
 *
 * Note: This function is targeted for the log and recovery manager. It is
 *       used when a database is copied or renamed.
 */
int
disk_set_creation (THREAD_ENTRY * thread_p, INT16 volid, const char *new_vol_fullname, const INT64 * new_dbcreation,
		   const LOG_LSA * new_chkptlsa, bool logchange, DISK_FLUSH_TYPE flush)
{
  DISK_VAR_HEADER *vhdr = NULL;
  LOG_DATA_ADDR addr;
  DISK_RECV_CHANGE_CREATION *undo_recv;
  DISK_RECV_CHANGE_CREATION *redo_recv;

  int error_code = NO_ERROR;

  if ((int) strlen (new_vol_fullname) + 1 > DB_MAX_PATH_LENGTH)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, NULL, new_vol_fullname,
	      (int) strlen (new_vol_fullname) + 1, DB_MAX_PATH_LENGTH);
      return ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG;
    }

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &addr.pgptr, &vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* Do I need to log anything ? */
  if (logchange != false)
    {
      int undo_size, redo_size;

      undo_size = (sizeof (*undo_recv) + (int) strlen (disk_vhdr_get_vol_fullname (vhdr)));
      undo_recv = (DISK_RECV_CHANGE_CREATION *) malloc (undo_size);
      if (undo_recv == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, undo_size);
	  goto error;
	}

      redo_size = sizeof (*redo_recv) + (int) strlen (new_vol_fullname);
      redo_recv = (DISK_RECV_CHANGE_CREATION *) malloc (redo_size);
      if (redo_recv == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_size);
	  free_and_init (undo_recv);
	  goto error;
	}

      /* Undo stuff */
      memcpy (&undo_recv->db_creation, &vhdr->db_creation, sizeof (vhdr->db_creation));
      memcpy (&undo_recv->chkpt_lsa, &vhdr->chkpt_lsa, sizeof (vhdr->chkpt_lsa));
      (void) strcpy (undo_recv->vol_fullname, disk_vhdr_get_vol_fullname (vhdr));

      /* Redo stuff */
      memcpy (&redo_recv->db_creation, new_dbcreation, sizeof (*new_dbcreation));
      memcpy (&redo_recv->chkpt_lsa, new_chkptlsa, sizeof (*new_chkptlsa));
      (void) strcpy (redo_recv->vol_fullname, new_vol_fullname);

      log_append_undoredo_data (thread_p, RVDK_CHANGE_CREATION, &addr, undo_size, redo_size, undo_recv, redo_recv);
      free_and_init (undo_recv);
      free_and_init (redo_recv);
    }
  else
    {
      log_skip_logging (thread_p, &addr);
    }

  /* Modify volume creation information */
  memcpy (&vhdr->db_creation, new_dbcreation, sizeof (*new_dbcreation));
  memcpy (&vhdr->chkpt_lsa, new_chkptlsa, sizeof (*new_chkptlsa));
  if (disk_vhdr_set_vol_fullname (vhdr, new_vol_fullname) != NO_ERROR)
    {
      goto error;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  if (flush == DISK_FLUSH)
    {
      assert (false);
      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
      (void) pgbuf_flush (thread_p, addr.pgptr, FREE);
    }
  else if (flush == DISK_FLUSH_AND_INVALIDATE)
    {
      pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
      if (pgbuf_invalidate (thread_p, addr.pgptr) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {				/* DISK_DONT_FLUSH */
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
    }
  addr.pgptr = NULL;

  return NO_ERROR;

error:

  assert (addr.pgptr != NULL);

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  pgbuf_unfix_and_init (thread_p, addr.pgptr);

  return ER_FAILED;
}

/*
 * disk_set_link () - Link the given permanent volume with the previous
 *                            permanent volume
 *   return: NO_ERROR
 *   volid(in): Volume identifier
 *   next_volid (in): next volume identifier
 *   next_volext_fullname(in): next volume label/name
 *   logchange(in): Whether or not to log the change
 *   flush(in):
 *
 * Note: No logging is intended for exclusive use by the log and recovery
 *       manager. It is used when a database is copied or renamed.
 */
int
disk_set_link (THREAD_ENTRY * thread_p, INT16 volid, INT16 next_volid, const char *next_volext_fullname, bool logchange,
	       DISK_FLUSH_TYPE flush)
{
  DISK_VAR_HEADER *vhdr;
  LOG_DATA_ADDR addr;
  VPID vpid;
  DISK_RECV_LINK_PERM_VOLUME *undo_recv;
  DISK_RECV_LINK_PERM_VOLUME *redo_recv;

  int error_code = NO_ERROR;

  addr.vfid = NULL;
  addr.offset = 0;

  /* Get the header of the previous permanent volume */

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &addr.pgptr, &vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /* Do I need to log anything ? */
  if (logchange == true)
    {
      int undo_size, redo_size;

      if (next_volid != NULL_VOLID)
	{
	  /* Before updating and logging disk link to a new volume, we need to log a page flush on undo.
	   * Recovery must flush disk link changes before removing volume from disk.
	   */
	  log_append_undo_data2 (thread_p, RVPGBUF_FLUSH_PAGE, NULL, NULL, 0, sizeof (vpid), &vpid);
	}

      undo_size = (sizeof (*undo_recv) + (int) strlen (disk_vhdr_get_next_vol_fullname (vhdr)));
      undo_recv = (DISK_RECV_LINK_PERM_VOLUME *) malloc (undo_size);
      if (undo_recv == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, undo_size);
	  goto error;
	}

      redo_size = sizeof (*redo_recv) + (int) strlen (next_volext_fullname);
      redo_recv = (DISK_RECV_LINK_PERM_VOLUME *) malloc (redo_size);
      if (redo_recv == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, redo_size);
	  free_and_init (undo_recv);
	  goto error;
	}

      undo_recv->next_volid = vhdr->next_volid;
      (void) strcpy (undo_recv->next_vol_fullname, disk_vhdr_get_next_vol_fullname (vhdr));

      redo_recv->next_volid = next_volid;
      (void) strcpy (redo_recv->next_vol_fullname, next_volext_fullname);

      log_append_undoredo_data (thread_p, RVDK_LINK_PERM_VOLEXT, &addr, undo_size, redo_size, undo_recv, redo_recv);

      free_and_init (undo_recv);
      free_and_init (redo_recv);
    }
  else
    {
      log_skip_logging (thread_p, &addr);
    }

  /* Modify the header */
  vhdr->next_volid = next_volid;
  if (disk_vhdr_set_next_vol_fullname (vhdr, next_volext_fullname) != NO_ERROR)
    {
      goto error;
    }

  /* Forcing the log here to be safer, especially in the case of permanent temp volumes. */
  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  pgbuf_set_dirty (thread_p, addr.pgptr, DONT_FREE);
  if (flush == DISK_FLUSH_AND_INVALIDATE)
    {
      /* this will invoke pgbuf_flush */
      if (pgbuf_invalidate (thread_p, addr.pgptr) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      (void) pgbuf_flush (thread_p, addr.pgptr, FREE);
    }
  addr.pgptr = NULL;

  return NO_ERROR;

error:

  assert (addr.pgptr != NULL);

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  pgbuf_unfix_and_init (thread_p, addr.pgptr);

  return ER_FAILED;
}

/*
 * disk_set_boot_hfid () - Reset system boot heap
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   hfid(in): System boot heap file
 *
 * Note: The system boot file filed of in the volume header is redefined to
 *       point to the given value. This function is called only during the
 *       initialization process
 */
int
disk_set_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid, const HFID * hfid)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  LOG_DATA_ADDR addr;

  addr.vfid = NULL;
  addr.offset = 0;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  log_append_undoredo_data (thread_p, RVDK_RESET_BOOT_HFID, &addr, sizeof (vhdr->boot_hfid), sizeof (*hfid),
			    &vhdr->boot_hfid, &hfid);
  HFID_COPY (&(vhdr->boot_hfid), hfid);

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  return NO_ERROR;
}

/*
 * disk_get_boot_hfid () - Find the system boot heap file
 *   return: hfid on success or NULL on failure
 *   volid(in): Permanent volume identifier
 *   hfid(out): System boot heap file
 */
HFID *
disk_get_boot_hfid (THREAD_ENTRY * thread_p, INT16 volid, HFID * hfid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR pgptr = NULL;

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  HFID_COPY (hfid, &(vhdr->boot_hfid));

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  if (HFID_IS_NULL (hfid))
    {
      return NULL;
    }

  return hfid;
}

/*
 * disk_get_link () - Find the name of the next permananet volume
 *                          extension
 *   return: next_volext_fullname or NULL in case of error
 *   volid(in): Volume identifier
 *   next_volid(out): Next volume identifier
 *   next_volext_fullname(out): Next volume extension
 *
 * Note: If there is none, next_volext_fullname is set to null string
 */
char *
disk_get_link (THREAD_ENTRY * thread_p, INT16 volid, INT16 * next_volid, char *next_volext_fullname)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR pgptr = NULL;

  assert (next_volid != NULL);
  assert (next_volext_fullname != NULL);

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  *next_volid = vhdr->next_volid;
  strncpy (next_volext_fullname, disk_vhdr_get_next_vol_fullname (vhdr), DB_MAX_PATH_LENGTH);

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return next_volext_fullname;
}

/*
 * disk_set_checkpoint () - Reset the recovery checkpoint address for this volume
 *   return: NO_ERROR;
 *   volid(in): Permanent volume identifier
 *   log_chkpt_lsa(in): Recovery checkpoint for volume
 *
 * Note: The dirty pages of this volume are not written out, not even the
 *       header page which maintains the checkpoint value. The function
 *       assumes that all volume pages with lsa smaller that the given one has
 *       already been forced to disk (e.g., by the log and recovery manager).
 *
 *       When a backup of the database is taken, it is important that the
 *       volume header page is forced out. The checkpoint on the volume is
 *       used as an indicator to start a media recovery process, so it may be
 *       good idea to force all dirty unfixed pages.
 */
int
disk_set_checkpoint (THREAD_ENTRY * thread_p, INT16 volid, const LOG_LSA * log_chkpt_lsa)
{
  DISK_VAR_HEADER *vhdr;
  LOG_DATA_ADDR addr;

  int error_code = NO_ERROR;

  addr.pgptr = NULL;
  addr.vfid = NULL;
  addr.offset = 0;

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &addr.pgptr, &vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  vhdr->chkpt_lsa.pageid = log_chkpt_lsa->pageid;
  vhdr->chkpt_lsa.offset = log_chkpt_lsa->offset;

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  log_skip_logging (thread_p, &addr);
  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
  addr.pgptr = NULL;

  return NO_ERROR;
}

/*
 * disk_get_checkpoint () - Get the recovery checkpoint address of this volume
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   vol_lsa(out): Volume recovery checkpoint
 */
int
disk_get_checkpoint (THREAD_ENTRY * thread_p, INT16 volid, LOG_LSA * vol_lsa)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;

  int error_code = NO_ERROR;

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  vol_lsa->pageid = vhdr->chkpt_lsa.pageid;
  vol_lsa->offset = vhdr->chkpt_lsa.offset;

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return NO_ERROR;
}

/*
 * disk_get_creation_time () - Get the database creation time according to the
 *                        volume header
 *   return: void
 *   volid(in): Permanent volume identifier
 *   db_creation(out): Database creation time according to the volume
 */
int
disk_get_creation_time (THREAD_ENTRY * thread_p, INT16 volid, INT64 * db_creation)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;

  int error_code = NO_ERROR;

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  memcpy (db_creation, &vhdr->db_creation, sizeof (*db_creation));

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return NO_ERROR;
}

/*
 * xdisk_get_purpose () - Find the main purpose of the given volume
 *   return: volume_purpose or DISK_UNKNOWN_PURPOSE
 *   volid(in): Permanent volume identifier
 */
DISK_VOLPURPOSE
xdisk_get_purpose (THREAD_ENTRY * thread_p, INT16 volid)
{
  assert (disk_Cache != NULL);

  if (!disk_is_valid_volid (volid))
    {
      assert (false);
      return DISK_UNKNOWN_PURPOSE;
    }
  return disk_get_volpurpose (volid);
}

/*
 * xdisk_get_purpose_and_space_info ()
 *          Find the main purpose and space info of the volume
 *
 *   return: volid or NULL_VOLID in case of error
 *   volid(in): Permanent volume identifier. If NULL_VOLID is given, the total
 *              information of all volumes is requested.
 *   vol_purpose(out): Purpose for the given volume
 *   space_info (out): space info of the volume.
 *
 * Note: The free number of pages should be taken as an approximation by the
 *       caller since we do not leave the page locked after the inquire. That
 *       is, someone else can allocate pages
 */
int
xdisk_get_purpose_and_space_info (THREAD_ENTRY * thread_p, VOLID volid, DISK_VOLPURPOSE * vol_purpose,
				  VOL_SPACE_INFO * space_info)
{
  int error_code = NO_ERROR;

  assert (volid != NULL_VOLID);
  assert (disk_Cache != NULL);
  assert (volid < disk_Cache->nvols_perm || volid > LOG_MAX_DBVOLID - disk_Cache->nvols_temp);

  if (space_info != NULL)
    {
      /* we don't cache total/max sectors */
      PAGE_PTR page_volheader;
      DISK_VAR_HEADER *volheader;

      error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &page_volheader, &volheader);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      space_info->n_max_sects = volheader->nsect_max;
      space_info->n_total_sects = volheader->nsect_total;
      pgbuf_unfix_and_init (thread_p, page_volheader);

      space_info->n_free_sects = disk_Cache->vols[volid].nsect_free;
    }
  if (vol_purpose != NULL)
    {
      *vol_purpose = disk_Cache->vols[volid].purpose;
    }

  return NO_ERROR;
}

/*
 * xdisk_get_purpose_and_sys_lastpage () - Find the main purpose of the given volume
 *                                   and the pagied of the last system page
 *                                   used by the volume
 *   return: volid or NULL_VOLID in case of error
 *   volid(in): Permanent volume identifier
 *   vol_purpose(out): Purpose for the given volume
 *   sys_lastpage(out): Pageid of last system page
 */
INT16
xdisk_get_purpose_and_sys_lastpage (THREAD_ENTRY * thread_p, INT16 volid, DISK_VOLPURPOSE * vol_purpose,
				    INT32 * sys_lastpage)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;

  /* The purpose of a volume does not change, so we do not lock the page */
  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL_VOLID;
    }

  *vol_purpose = vhdr->purpose;
  *sys_lastpage = vhdr->sys_lastpage;

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return volid;
}

/*
 * xdisk_get_total_numpages () - Return the number of total pages for the given volume
 *   return: Total Number of pages
 *   volid(in): Permanent volume identifier
 */
INT32
xdisk_get_total_numpages (THREAD_ENTRY * thread_p, INT16 volid)
{
  /* we don't have this info in cache */
  /* todo: investigate further its usage */
  PAGE_PTR page_volheader;
  DISK_VAR_HEADER *volheader;

  DKNPAGES npages;

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &page_volheader, &volheader) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return -1;
    }
  npages = DISK_SECTS_NPAGES (volheader->nsect_total);

  pgbuf_unfix (thread_p, page_volheader);

  return npages;
}

/*
 * xdisk_get_free_numpages () - Return the number of free pages for the given volume
 *   return: Number of free pages
 *   volid(in): Permanent volume identifier
 *
 * Note: The free number of pages should be taken as an approximation by the
 *       caller since we do not leave the page locked after the inquire.
 *       That is, someone else can allocate pages.
 */
INT32
xdisk_get_free_numpages (THREAD_ENTRY * thread_p, INT16 volid)
{
  /* get from cache. */
  /* todo: investigate usage */
  assert (disk_Cache != NULL);
  assert (volid <= disk_Cache->nvols_perm || volid > LOG_MAX_DBVOLID - disk_Cache->nvols_temp);

  return DISK_SECTS_NPAGES (disk_Cache->vols[volid].nsect_free);
}

/*
 * xdisk_is_volume_exist () - 
 *   return: 
 *   volid(in): volume identifier
 */
bool
xdisk_is_volume_exist (THREAD_ENTRY * thread_p, VOLID volid)
{
  DISK_CHECK_VOL_INFO vol_info;

  vol_info.volid = volid;
  vol_info.exists = false;
  (void) fileio_map_mounted (thread_p, disk_check_volume_exist, &vol_info);

  return vol_info.exists;
}

/*
 * disk_get_total_numsectors () - Return the number of total sectors for the given volume
 *   return: Total Number of sectors
 *   volid(in): Permanent volume identifier
 */
INT32
disk_get_total_numsectors (THREAD_ENTRY * thread_p, INT16 volid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  INT32 total_sects;

  /* todo: will we need this? */

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return -1;
    }

  total_sects = vhdr->nsect_total;

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return total_sects;
}

/*
 * xdisk_get_fullname () - Find the name of the volume and copy it into vol_fullname
 *   return: vol_fullname on success or NULL on failure
 *   volid(in): Permanent volume identifier
 *   vol_fullname(out): Address where the name of the volume is placed.
 *                     The size must be at least DB_MAX_PATH_LENGTH
 *
 * Note: Alternative function fileio_get_volume_label which is much faster and does not copy
 *       the name
 */
char *
xdisk_get_fullname (THREAD_ENTRY * thread_p, INT16 volid, char *vol_fullname)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;

  if (vol_fullname == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      *vol_fullname = '\0';
      return NULL;
    }

  strncpy (vol_fullname, disk_vhdr_get_vol_fullname (vhdr), DB_MAX_PATH_LENGTH);

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return vol_fullname;
}

/*
 * xdisk_get_remarks () - Find the remarks attached to the volume creation
 *   return: remarks string
 *   volid(in): Permanent volume identifier
 *
 * Note: The string returned, must be freed by using free_and_init.
 */
char *
xdisk_get_remarks (THREAD_ENTRY * thread_p, INT16 volid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  char *remarks;

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  remarks = (char *) malloc ((int) strlen (disk_vhdr_get_vol_remarks (vhdr)) + 1);
  if (remarks != NULL)
    {
      strcpy (remarks, disk_vhdr_get_vol_remarks (vhdr));
    }

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return remarks;
}

/*
 * disk_get_boot_db_charset () - Find the system boot charset
 *   return: hfid on success or NULL on failure
 *   volid(in): Permanent volume identifier
 *   db_charset(out): System boot charset
 */
int *
disk_get_boot_db_charset (THREAD_ENTRY * thread_p, INT16 volid, int *db_charset)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR pgptr = NULL;

  assert (db_charset != NULL);

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &pgptr, &vhdr) != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  *db_charset = vhdr->db_charset;

  pgbuf_unfix_and_init (thread_p, pgptr);

  return db_charset;
}

/*
 * disk_alloc_sector () - Allocates a new sector
 *   return: sector identifier
 *   volid(in): Permanent volume identifier
 *   nsects(in): Number of contiguous sectors to allocate
 *   exp_npages(in): Expected pages that sector will have
 *
 * Note: This function allocates a new sector. If the volume has run out of
 *       sectors, the special sector is returned. The special sector has
 *       special meaning since it can steal pages from other sectors,
 *       especially from those sectors with numerous free pages. The special
 *       sector prevents volume fragmentation when numerous sectors are
 *       assigned to relatively small files resulting in many unused pages.
 *       The special sector also allows the allocation of a large set of
 *       contiguous pages in one request. While normal sectors hold a fixed
 *       maximum number of contiguous pages, the special sector can hold any
 *       number of pages. The special sector can be assigned to several file
 *       structures.
 */
INT32
disk_alloc_sector (THREAD_ENTRY * thread_p, INT16 volid, INT32 nsects, int exp_npages)
{
  return 0;
}

/*
 * disk_alloc_special_sector () - Allocate the special sector
 *   return: special sector identifier
 *
 * Note: The special sector is returned. The special sector has special meaning
 *       since it can steal pages from other sectors, especially from those
 *       sectors with numerous free pages. The special sector prevents volume
 *       fragmentation when numerous sectors are assigned to relatively small
 *       files resulting in many unused pages. The special sector also allows
 *       the allocation of a large set of contiguous pages in one request.
 *       While normal sectors hold a fixed maximum number of contiguous pages,
 *       special sector can be assigned to several file structures.
 *
 *       This function should be called in very unusual cases. Mainly, it
 *       should be called when the sectors are very small, that several
 *       contiguous pages are impossible to find in a single sector.
 */
INT32
disk_alloc_special_sector (void)
{
  return DISK_SECTOR_WITH_ALL_PAGES;
}

/*
 * disk_alloc_page () -Allocate pages
 *   return: A valid page identifier (>= 0) on success, or
 *           NULL_PAGEID on any failure except out of space in sector.
 *           DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES, out of space in sector,
 *           however, there are such an amount of requested pages in the disk.
 *   volid(in): Permanent volume identifier
 *   sectid(in): Sector-id from where pages are allocated
 *   npages(in): Number of pages to allocate
 *   near_pageid(in): Near_pageid. Hint only, it may be ignored
 *
 * Note: This function allocates the closest "npages" contiguous free pages to
 *       the "near_pageid" page in the "Sector-id" sector of the given volume.
 *       If there are not enough "npages" contiguous free pages, a NULL_PAGEID
 *       is returned and an error condition code is flaged.
 */
INT32
disk_alloc_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 sectid, INT32 npages, INT32 near_pageid,
		 DISK_PAGE_TYPE alloc_page_type)
{
  return NULL_PAGEID;
}

/*
 * disk_dealloc_sector () - Deallocate a sector
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   sectid(in): sectorid of the first contiguous sector to deallocate
 *   nsects(in): Number of contiguous sectors to deallocate
 *
 * Note: deallocate the given set of contiguous sectors starting at "sectid".
 *       The pages of these sector are not deallocated automatically. The
 *       pages should be deallocated by the caller since the pages of this
 *       sector may have been stolen by the special sector. The special sector
 *       is never deallocated since it is permanently allocated by the system.
 */
int
disk_dealloc_sector (THREAD_ENTRY * thread_p, INT16 volid, INT32 sectid, INT32 nsects)
{
  return NO_ERROR;
}

/*
 * disk_dealloc_page () - Deallocate a page
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   pageid(in): pageid to deallocate
 *   npages(in): Number of contiguous pages to deallocate
 *   page_type(in):
 *
 * Note: Deallocate the given set of contiguous pages starting at "pageid".
 */
int
disk_dealloc_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 pageid, INT32 npages, DISK_PAGE_TYPE page_type)
{
  return NO_ERROR;
}

/*
 * disk_check_volume () - compare cache and volume and check for inconsistencies
 * 
 * return        : DISK_VALID if no inconsistency or if all inconsistencies have been fixed
 *                 DISK_ERROR if expected errors occurred
 *                 DISK_INVALID if cache and/or volume header are inconsistent
 * thread_p (in) : thread entry
 * volid (in)    : volume identifier
 * repair (in)   : true to try fix the inconsistencies
 */
static DISK_ISVALID
disk_check_volume (THREAD_ENTRY * thread_p, INT16 volid, bool repair)
{
  DISK_ISVALID valid = DISK_VALID;
  DISK_VAR_HEADER *volheader;
  PAGE_PTR page_volheader = NULL;
  DKNSECTS nfree = 0;

  int error_code = NO_ERROR;

  /* get check critical section. it will prevent reservations/extensions to interfere with the process */
  error_code = csect_enter (thread_p, CSECT_DISK_CHECK, INF_WAIT);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &page_volheader, &volheader) != NO_ERROR)
    {
      ASSERT_ERROR ();
      valid = DISK_ERROR;
      goto exit;
    }

  /* check that purpose matches */
  if (volheader->purpose != disk_Cache->vols[volid].purpose)
    {
      assert (false);
      if (repair)
	{
	  disk_Cache->vols[volid].purpose = volheader->purpose;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  valid = DISK_INVALID;
	  goto exit;
	}
    }

  /* count free sectors */
  error_code = disk_stab_iterate_units_all (thread_p, volheader, PGBUF_LATCH_READ, disk_stab_count_free, &nfree);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      valid = DISK_ERROR;
      goto exit;
    }

  /* check (and maybe fix) cache inconsistencies */
  disk_cache_lock_reserve_for_purpose (volheader->purpose);
  if (nfree != disk_Cache->vols[volid].nsect_free)
    {
      /* inconsistent! */
      assert (false);

      if (repair)
	{
	  DKNSECTS diff = nfree - disk_Cache->vols[volid].nsect_free;
	  disk_cache_update_vol_free (volid, diff);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DISK_INCONSISTENT_NFREE_SECTS, 3,
		  fileio_get_volume_label (volid, PEEK), disk_Cache->vols[volid].nsect_free, nfree);
	  valid = DISK_INVALID;
	}
    }

  /* the following check also added to the disk_verify_volume_header() macro */
  if (volheader->sect_npgs != DISK_SECTOR_NPAGES
      || volheader->stab_first_page != DISK_VOLHEADER_PAGE + 1
      || volheader->sys_lastpage != (volheader->stab_first_page + volheader->stab_npages - 1)
      || volheader->nsect_total > volheader->nsect_max
      || volheader->stab_npages < CEIL_PTVDIV (volheader->nsect_max, DISK_STAB_PAGE_BIT_COUNT)
      || volheader->nsect_total < disk_Cache->vols[volid].nsect_free)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DISK_INCONSISTENT_VOL_HEADER, 1,
	      fileio_get_volume_label (volid, PEEK));
      valid = DISK_INVALID;
    }

exit:

  if (page_volheader != NULL)
    {
      pgbuf_unfix_and_init (thread_p, page_volheader);
    }

  csect_exit (thread_p, CSECT_DISK_CHECK);

  return valid;
}

/*
 * disk_check () - check disk cache is not out of sync
 *
 * return        : DISK_VALID if all was ok or fixed
                   DISK_ERROR if expected error occurred
                   DISK_INVALID if disk cache is in an invalid state
 * thread_p (in) : thread entry
 * repair (in)   : true to repair invalid states (if possible)
 */
DISK_ISVALID
disk_check (THREAD_ENTRY * thread_p, bool repair)
{
  DISK_ISVALID valid = DISK_VALID;
  int nvols_perm;
  int nvols_temp;
  VOLID volid_perm_last;
  VOLID volid_temp_last;
  VOLID volid_iter;
  DKNSECTS perm_free;
  DKNSECTS temp_free;

  int error_code = NO_ERROR;

  error_code = csect_enter (thread_p, CSECT_DISK_CHECK, INF_WAIT);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }

  /* first step: check cache matches boot_db_parm */
  nvols_perm = xboot_find_number_permanent_volumes (thread_p);
  nvols_temp = xboot_find_number_temp_volumes (thread_p);
  volid_perm_last = xboot_find_last_permanent (thread_p);
  volid_temp_last = xboot_find_last_temp (thread_p);

  if (nvols_perm != volid_perm_last + 1)
    {
      /* cannot repair */
      assert_release (false);
      csect_exit (thread_p, CSECT_DISK_CHECK);
      return DISK_INVALID;
    }
  if (nvols_temp > 0 && (nvols_temp != (LOG_MAX_DBVOLID - volid_temp_last - 1)))
    {
      /* cannot repair */
      assert_release (false);
      csect_exit (thread_p, CSECT_DISK_CHECK);
      return DISK_INVALID;
    }
  if (nvols_perm != disk_Cache->nvols_perm)
    {
      assert (false);
      if (repair)
	{
	  disk_Cache->nvols_perm = nvols_perm;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_DISK_CHECK);
	  return DISK_INVALID;
	}
    }
  if (nvols_temp > 0 && nvols_temp != disk_Cache->nvols_temp)
    {
      assert (false);
      if (repair)
	{
	  disk_Cache->nvols_temp = nvols_temp;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_DISK_CHECK);
	  return DISK_INVALID;
	}
    }

  /* release critical section. we will get it for each volume we check, to avoid blocking all reservations and
   * extensions for a long time. */
  csect_exit (thread_p, CSECT_DISK_CHECK);

  /* second step: check volume cached info is consistent */
  for (volid_iter = 0; volid_iter < disk_Cache->nvols_perm; volid_iter++)
    {
      valid = disk_check_volume (thread_p, volid_iter, repair);
      if (valid != DISK_VALID)
	{
	  assert (valid == DISK_ERROR);
	  ASSERT_ERROR ();
	  return valid;
	}
    }
  for (volid_iter = LOG_MAX_DBVOLID; volid_iter > LOG_MAX_DBVOLID - disk_Cache->nvols_temp; volid_iter--)
    {
      valid = disk_check_volume (thread_p, volid_iter, repair);
      if (valid != DISK_VALID)
	{
	  assert (valid == DISK_ERROR);
	  ASSERT_ERROR ();
	  return valid;
	}
    }

  /* third step: check information aggregated for each purpose matches the sum of all volumes */
  error_code = csect_enter (thread_p, CSECT_DISK_CHECK, INF_WAIT);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return DISK_ERROR;
    }

  /* check permanently stored volumes */
  for (perm_free = 0, temp_free = 0, volid_iter = 0; volid_iter < disk_Cache->nvols_perm; volid_iter++)
    {
      if (disk_Cache->vols[volid_iter].purpose == DB_PERMANENT_DATA_PURPOSE)
	{
	  perm_free += disk_Cache->vols[volid_iter].nsect_free;
	}
      else
	{
	  temp_free += disk_Cache->vols[volid_iter].nsect_free;
	}
    }
  if (perm_free != disk_Cache->perm_purpose_info.extend_info.nsect_free)
    {
      assert (false);
      if (repair)
	{
	  disk_Cache->perm_purpose_info.extend_info.nsect_free = perm_free;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_DISK_CHECK);
	  return DISK_INVALID;
	}
    }
  if (temp_free != disk_Cache->temp_purpose_info.nsect_perm_free)
    {
      assert (false);
      if (repair)
	{
	  disk_Cache->temp_purpose_info.nsect_perm_free = temp_free;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_DISK_CHECK);
	  return DISK_INVALID;
	}
    }

  /* check temporarily stored volumes */
  for (temp_free = 0, volid_iter = LOG_MAX_DBVOLID; volid_iter > LOG_MAX_DBVOLID - disk_Cache->nvols_temp; volid_iter--)
    {
      temp_free += disk_Cache->vols[volid_iter].nsect_free;
    }
  if (temp_free != disk_Cache->temp_purpose_info.extend_info.nsect_free)
    {
      assert (false);
      if (repair)
	{
	  disk_Cache->temp_purpose_info.extend_info.nsect_free = temp_free;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  csect_exit (thread_p, CSECT_DISK_CHECK);
	  return DISK_INVALID;
	}
    }

  /* all valid or all repaired */
  csect_exit (thread_p, CSECT_DISK_CHECK);
  return DISK_VALID;
}

/*
 * disk_dump_goodvol_system () - dump volume system information
 *
 * return        : error code
 * thread_p (in) : thread entry
 * fp (in)       : output file
 * volid (in)    : volume identifier
 */
static int
disk_dump_volume_system_info (THREAD_ENTRY * thread_p, FILE * fp, INT16 volid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;

  int error_code = NO_ERROR;

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &hdr_pgptr, &vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  disk_vhdr_dump (fp, vhdr);
  error_code = disk_stab_dump (thread_p, fp, vhdr);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
    }
  else
    {
      (void) fprintf (fp, "\n\n");
    }
  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return error_code;
}

/*
 * disk_check_volume_exist () - check whether volume existed
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   volid(in):
 *   arg(in/out):CHECK_VOL_INFO structure
 */
static bool
disk_check_volume_exist (THREAD_ENTRY * thread_p, VOLID volid, void *arg)
{
  DISK_CHECK_VOL_INFO *vol_infop = (DISK_CHECK_VOL_INFO *) arg;

  if (volid == vol_infop->volid)
    {
      vol_infop->exists = true;
    }
  return true;
}

/*
 * disk_volume_header_start_scan () -  start scan function for show volume header
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   type (in):
 *   arg_values(in):
 *   arg_cnt(in):
 *   ptr(in/out): volume header context
 */
extern int
disk_volume_header_start_scan (THREAD_ENTRY * thread_p, int type, DB_VALUE ** arg_values, int arg_cnt, void **ptr)
{
  int error = NO_ERROR;
  DISK_VOL_HEADER_CONTEXT *ctx = NULL;

  ctx = (DISK_VOL_HEADER_CONTEXT *) db_private_alloc (thread_p, sizeof (DISK_VOL_HEADER_CONTEXT));
  if (ctx == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto exit_on_error;
    }
  memset (ctx, 0, sizeof (DISK_VOL_HEADER_CONTEXT));

  assert (arg_values != NULL && arg_cnt && arg_values[0] != NULL);
  assert (DB_VALUE_TYPE (arg_values[0]) == DB_TYPE_INTEGER);
  ctx->volume_id = db_get_int (arg_values[0]);

  /* if volume id is out of range */
  if (ctx->volume_id < 0 || ctx->volume_id > DB_INT16_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_VOLID_NOT_EXIST, 1, ctx->volume_id);
      error = ER_DIAG_VOLID_NOT_EXIST;
      goto exit_on_error;
    }

  /* check volume id exist or not */
  if (xdisk_is_volume_exist (thread_p, (INT16) ctx->volume_id) == false)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_VOLID_NOT_EXIST, 1, ctx->volume_id);
      error = ER_DIAG_VOLID_NOT_EXIST;
      goto exit_on_error;
    }

  *ptr = ctx;
  return NO_ERROR;

exit_on_error:
  db_private_free (thread_p, ctx);
  return error;
}

/*
 * disk_volume_header_next_scan () -  next scan function for show volume header
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   ptr(in/out): volume header context
 */
SCAN_CODE
disk_volume_header_next_scan (THREAD_ENTRY * thread_p, int cursor, DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  DISK_VAR_HEADER *vhdr;
  int error = NO_ERROR, idx = 0;
  PAGE_PTR pgptr = NULL;
  DB_DATETIME create_time;
  char buf[256];
  DISK_VOL_HEADER_CONTEXT *ctx = (DISK_VOL_HEADER_CONTEXT *) ptr;

  if (cursor >= 1)
    {
      return S_END;
    }

  error = disk_get_volheader (thread_p, ctx->volume_id, PGBUF_LATCH_READ, &pgptr, &vhdr);
  if (error != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* fill each column */
  db_make_int (out_values[idx], ctx->volume_id);
  idx++;

  snprintf (buf, sizeof (buf), "MAGIC SYMBOL = %s at disk location = %lld", vhdr->magic,
	    offsetof (FILEIO_PAGE, page) + (long long) offsetof (DISK_VAR_HEADER, magic));
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  db_make_int (out_values[idx], vhdr->iopagesize);
  idx++;

  db_make_string (out_values[idx], disk_purpose_to_string (vhdr->purpose));
  idx++;

  db_make_string (out_values[idx], disk_type_to_string (vhdr->type));
  idx++;

  db_make_int (out_values[idx], vhdr->sect_npgs);
  idx++;

  db_make_int (out_values[idx], vhdr->nsect_total);
  idx++;

  /* free sects are not kept in header */
  db_make_int (out_values[idx], disk_Cache->vols[ctx->volume_id].nsect_free);
  idx++;

  db_make_int (out_values[idx], vhdr->nsect_max);
  idx++;

  db_make_int (out_values[idx], vhdr->hint_allocsect);
  idx++;

  db_make_int (out_values[idx], vhdr->stab_npages);
  idx++;

  db_make_int (out_values[idx], vhdr->stab_first_page);
  idx++;

  db_make_int (out_values[idx], vhdr->sys_lastpage);
  idx++;

  db_localdatetime ((time_t *) (&vhdr->db_creation), &create_time);
  db_make_datetime (out_values[idx], &create_time);
  idx++;

  db_make_int (out_values[idx], vhdr->db_charset);
  idx++;

  error = db_make_string_copy (out_values[idx], lsa_to_string (buf, sizeof (buf), &vhdr->chkpt_lsa));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = db_make_string_copy (out_values[idx], hfid_to_string (buf, sizeof (buf), &vhdr->boot_hfid));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = db_make_string_copy (out_values[idx], (char *) (vhdr->var_fields + vhdr->offset_to_vol_fullname));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  db_make_int (out_values[idx], vhdr->next_volid);
  idx++;

  error = db_make_string_copy (out_values[idx], (char *) (vhdr->var_fields + vhdr->offset_to_next_vol_fullname));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  error = db_make_string_copy (out_values[idx], (char *) (vhdr->var_fields + vhdr->offset_to_vol_remarks));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit;
    }

  assert (idx == out_cnt);

exit:
  if (pgptr)
    {
      pgbuf_unfix (thread_p, pgptr);
    }
  return error == NO_ERROR ? S_SUCCESS : S_ERROR;
}

/*
 * disk_volume_header_end_scan() -- end scan function of show volume header
 *   return: NO_ERROR, or ER_code
 *
 *   thread_p(in):
 *   ptr(in):  volume header context
 */
int
disk_volume_header_end_scan (THREAD_ENTRY * thread_p, void **ptr)
{
  db_private_free_and_init (thread_p, *ptr);
  return NO_ERROR;
}

/*
 * disk_vhdr_dump () - Dump the volume header structure.
 *   return: void
 *   vhdr(in): Pointer to volume header
 *
 * Note: This function is used for debugging purposes.
 */
static void
disk_vhdr_dump (FILE * fp, const DISK_VAR_HEADER * vhdr)
{
  char time_val[CTIME_MAX];
  time_t tmp_time;

  (void) fprintf (fp, " MAGIC SYMBOL = %s at disk location = %lld\n", vhdr->magic,
		  offsetof (FILEIO_PAGE, page) + (long long) offsetof (DISK_VAR_HEADER, magic));
  (void) fprintf (fp, " io_pagesize = %d,\n", vhdr->iopagesize);
  (void) fprintf (fp, " VID = %d, VOL_FULLNAME = %s\n VOL PURPOSE = %s\n VOL TYPE = %s VOL_REMARKS = %s\n",
		  vhdr->volid, disk_vhdr_get_vol_fullname (vhdr), disk_purpose_to_string (vhdr->purpose),
		  disk_type_to_string (vhdr->type), disk_vhdr_get_vol_remarks (vhdr));
  (void) fprintf (fp, " NEXT_VID = %d, NEXT_VOL_FULLNAME = %s\n", vhdr->next_volid,
		  disk_vhdr_get_next_vol_fullname (vhdr));
  (void) fprintf (fp, " LAST SYSTEM PAGE = %d\n", vhdr->sys_lastpage);
  (void) fprintf (fp, " SECTOR: SIZE IN PAGES = %10d, TOTAL = %10d,", vhdr->sect_npgs, vhdr->nsect_total);
  (void) fprintf (fp, " FREE = %10d, MAX=%d10\n", disk_Cache->vols[vhdr->volid].nsect_free, vhdr->nsect_max);
  (void) fprintf (fp, " %10s HINT_ALLOC = %10d\n", " ", vhdr->hint_allocsect);
  (void) fprintf (fp, " SECTOR TABLE:    SIZE IN PAGES = %10d, FIRST_PAGE = %5d\n", vhdr->stab_npages,
		  vhdr->stab_first_page);

  tmp_time = (time_t) vhdr->db_creation;
  (void) ctime_r (&tmp_time, time_val);
  (void) fprintf (fp, " Database creation time = %s\n Lowest Checkpoint for recovery = %lld|%d\n", time_val,
		  (long long int) vhdr->chkpt_lsa.pageid, vhdr->chkpt_lsa.offset);
  (void) fprintf (fp, "Boot_hfid: volid %d, fileid %d header_pageid %d\n", vhdr->boot_hfid.vfid.volid,
		  vhdr->boot_hfid.vfid.fileid, vhdr->boot_hfid.hpgid);
  (void) fprintf (fp, " db_charset = %d\n", vhdr->db_charset);
}

/*
 * disk_stab_dump_unit () - dump a unit in sector table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * cursor (in)   : sector table cursor
 * stop (in)     : not used
 * args (in/out) : output file
 */
static int
disk_stab_dump_unit (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args)
{
  FILE *fp = (FILE *) args;
  int bit;

  fprintf (fp, "\n%10d", cursor->sectid);

  for (bit = 0; bit < DISK_STAB_UNIT_BIT_COUNT; bit++)
    {
      if (bit % CHAR_BIT == 0)
	{
	  fprintf (fp, " ");
	}
      fprintf (fp, "%d", disk_stab_cursor_is_bit_set (cursor) ? 1 : 0);
    }

  return NO_ERROR;
}

/*
 * disk_stab_dump () - Dump the content of the allocation map table
 *   return: NO_ERROR
 *   vpid(in): Complete Page identifier
 *   at_name(in): Name of allocator table
 *   at_fpageid(in): First page of map allocation table
 *   at_lpageid(in): Last page of map allocation table
 *   all_fid(in): First allocation(page/sector) id
 *   all_lid(in): Last  allocation(page/sector) id
 *
 * Note: This function is used for debugging purposes.
 */
static int
disk_stab_dump (THREAD_ENTRY * thread_p, FILE * fp, const DISK_VAR_HEADER * volheader)
{
  int error_code = NO_ERROR;

  fprintf (fp, "SECTOR TABLE BITMAP:\n\n");
  fprintf (fp, "           01234567 01234567 01234567 01234567 01234567 01234567 01234567 01234567");

  error_code = disk_stab_iterate_units_all (thread_p, volheader, PGBUF_LATCH_READ, disk_stab_dump_unit, fp);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  fprintf (fp, "\n");
  return NO_ERROR;
}

/*
 * disk_dump_all () - Dump the system area information of every single volume,
 *                 but log and backup volumes.
 *   return: NO_ERROR;
 */
int
disk_dump_all (THREAD_ENTRY * thread_p, FILE * fp)
{
  int ret = NO_ERROR;

  ret = (fileio_map_mounted (thread_p, disk_dump_goodvol_all, NULL) == true ? NO_ERROR : ER_FAILED);

  return ret;
}

/*
 * disk_dump_goodvol_all () -  Dump all information of given volume
 *   return: true
 *   volid(in): Permanent volume identifier
 *   ignore(in):
 */
static bool
disk_dump_goodvol_all (THREAD_ENTRY * thread_p, INT16 volid, void *ignore)
{
  (void) disk_dump_volume_system_info (thread_p, stdout, volid);

  return true;
}

/* Recovery functions */

/*
 * disk_rv_redo_dboutside_newvol () - Redo the initialization of a disk from the
 *                                point of view of operating system
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_redo_dboutside_newvol (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;
  char *vol_label;

  vhdr = (DISK_VAR_HEADER *) rcv->data;
  vol_label = disk_vhdr_get_vol_fullname (vhdr);

  if (fileio_find_volume_descriptor_with_label (vol_label) == NULL_VOLDES)
    {
      (void) fileio_format (thread_p, NULL, vol_label, vhdr->volid, DISK_SECTS_NPAGES (vhdr->nsect_total),
			    vhdr->purpose != DB_TEMPORARY_DATA_PURPOSE, false, false, IO_PAGESIZE, 0, false);
      (void) pgbuf_invalidate_all (thread_p, vhdr->volid);
    }

  return NO_ERROR;
}

/*
 * disk_rv_undo_format () - Undo the initialization of a disk. The disk is
 *                        uninitialized or removed
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_undo_format (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int ret = NO_ERROR;

  ret = disk_unformat (thread_p, (char *) rcv->data);
  log_append_dboutside_redo (thread_p, RVLOG_OUTSIDE_LOGICAL_REDO_NOOP, 0, NULL);
  return NO_ERROR;
}

/*
 * disk_rv_redo_format () - Redo the initialization of a disk.
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_redo_format (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_VOLHEADER);

  return log_rv_copy_char (thread_p, rcv);
}

/*
 * disk_rv_dump_hdr () - Dump recovery header information.
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_hdr (FILE * fp, int length_ignore, void *data)
{
  DISK_VAR_HEADER *vhdr;

  vhdr = (DISK_VAR_HEADER *) data;
  disk_vhdr_dump (fp, vhdr);
}

/*
 * disk_rv_redo_init_map () - REDO the initialization of map table page.
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_redo_init_map (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DKNSECTS nsects;
  DISK_STAB_UNIT *stab_unit;

  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_VOLBITMAP);

  nsects = *(DKNSECTS *) rcv->data;

  /* Initialize the page to zeros, and allocate the needed bits for the pages or sectors */
  memset (rcv->pgptr, 0, DB_PAGESIZE);

  /* One byte at a time */
  for (stab_unit = (DISK_STAB_UNIT *) rcv->pgptr; nsects >= DISK_STAB_UNIT_BIT_COUNT;
       nsects -= DISK_STAB_UNIT_BIT_COUNT, stab_unit++)
    {
      *stab_unit = BIT64_FULL;
    }
  if (nsects > 0)
    {
      *stab_unit = bit64_set_trailing_bits (0, nsects);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * disk_rv_dump_init_map () - Dump redo information to initialize a map table page
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_init_map (FILE * fp, int length_ignore, void *data)
{
  fprintf (fp, "Nalloc_bits = %d\n", *(INT32 *) data);
}

/*
 * disk_rv_undoredo_set_creation_time () - Recover the modification of change creation stuff
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_undoredo_set_creation_time (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;
  DISK_RECV_CHANGE_CREATION *change;
  int ret = NO_ERROR;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  change = (DISK_RECV_CHANGE_CREATION *) rcv->data;

  memcpy (&vhdr->db_creation, &change->db_creation, sizeof (change->db_creation));
  memcpy (&vhdr->chkpt_lsa, &change->chkpt_lsa, sizeof (change->chkpt_lsa));
  ret = disk_vhdr_set_vol_fullname (vhdr, change->vol_fullname);

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_rv_dump_set_creation_time () - Dump either redo or undo change creation
 *                                 information
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_set_creation_time (FILE * fp, int length_ignore, void *data)
{
  DISK_RECV_CHANGE_CREATION *change;

  change = (DISK_RECV_CHANGE_CREATION *) data;

  fprintf (fp, "Label = %s, Db_creation = %lld, chkpt = %lld|%d\n", change->vol_fullname,
	   (long long) change->db_creation, (long long int) change->chkpt_lsa.pageid, change->chkpt_lsa.offset);
}

/*
 * disk_rv_undoredo_link () - Recover the link of a volume extension
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_undoredo_link (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;
  DISK_RECV_LINK_PERM_VOLUME *link;
  int ret = NO_ERROR;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  link = (DISK_RECV_LINK_PERM_VOLUME *) rcv->data;

  vhdr->next_volid = link->next_volid;
  ret = disk_vhdr_set_next_vol_fullname (vhdr, link->next_vol_fullname);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_rv_dump_link () - Dump either redo or undo link of a volume
 *                             extension
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_link (FILE * fp, int length_ignore, void *data)
{
  DISK_RECV_LINK_PERM_VOLUME *link;

  link = (DISK_RECV_LINK_PERM_VOLUME *) data;

  fprintf (fp, "Next_Volid = %d, Next_Volextension = %s\n", link->next_volid, link->next_vol_fullname);
}

/*
 * disk_rv_undoredo_set_boot_hfid () - Recover the reset of boot system heap
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_undoredo_set_boot_hfid (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;
  HFID *hfid;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  hfid = (HFID *) rcv->data;

  vhdr->boot_hfid.vfid.volid = hfid->vfid.volid;
  vhdr->boot_hfid.vfid.fileid = hfid->vfid.fileid;
  vhdr->boot_hfid.hpgid = hfid->hpgid;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_rv_dump_set_boot_hfid () - Dump either redo/undo reset of boot system
 *                                 heap
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_set_boot_hfid (FILE * fp, int length_ignore, void *data)
{
  HFID *hfid;

  hfid = (HFID *) data;
  fprintf (fp, "Heap: Volid = %d, Fileid = %d, Header_pageid = %d\n", hfid->vfid.volid, hfid->vfid.fileid, hfid->hpgid);
}

/*
 *  disk_rv_redo_dboutside_init_pages ()
 *
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_redo_dboutside_init_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_RECV_INIT_PAGES_INFO *info;
  VOLID volid;
  int vol_fd;
  FILEIO_PAGE *malloc_io_page_p;

  info = (DISK_RECV_INIT_PAGES_INFO *) rcv->data;

  volid = info->volid;
  vol_fd = fileio_get_volume_descriptor (volid);

  if (vol_fd == NULL_VOLDES)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
	      fileio_get_volume_label (volid, PEEK));
      return ER_FAILED;
    }

  malloc_io_page_p = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
  if (malloc_io_page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) IO_PAGESIZE);
      return ER_FAILED;
    }

  memset (malloc_io_page_p, 0, IO_PAGESIZE);
  (void) fileio_initialize_res (thread_p, &(malloc_io_page_p->prv));

  if (fileio_initialize_pages (thread_p, vol_fd, malloc_io_page_p, info->start_pageid, info->npages, IO_PAGESIZE, -1) ==
      NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY, 1,
	      fileio_get_volume_label (volid, PEEK));
      free_and_init (malloc_io_page_p);

      return ER_FAILED;
    }

  free_and_init (malloc_io_page_p);

  return NO_ERROR;
}

/*
 * disk_rv_dump_init_pages () -
 *
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in):
 */
void
disk_rv_dump_init_pages (FILE * fp, int length_ignore, void *data)
{
  DISK_RECV_INIT_PAGES_INFO *info;

  info = (DISK_RECV_INIT_PAGES_INFO *) data;

  fprintf (fp, "Volid = %d, start pageid = %d, npages = %d\n", info->volid, info->start_pageid, info->npages);
}

/*
 * disk_set_alloctables () -
 *   return: NO_ERROR
 *   vol_purpose(in):
 *   total_sects(in):
 *   total_pages(in):
 *   sect_alloctb_npages(in):
 *   page_alloctb_npages(in):
 *   sect_alloctb_page1(in):
 *   page_alloctb_page1(in):
 *   sys_lastpage(in):
 */
STATIC_INLINE void
disk_set_alloctables (DB_VOLPURPOSE vol_purpose, DISK_VAR_HEADER * volheader)
{
  volheader->stab_first_page = DISK_VOLHEADER_PAGE + 1;
  volheader->stab_npages = CEIL_PTVDIV (volheader->nsect_total, DISK_STAB_PAGE_BIT_COUNT);
  volheader->sys_lastpage = volheader->stab_first_page + volheader->stab_npages - 1;
}

/*
 * disk_verify_volume_header () -
 *   return: void
 *   pgptr(in): Pointer to volume header page
 */
STATIC_INLINE void
disk_verify_volume_header (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
#if !defined (NDEBUG)
  DISK_VAR_HEADER *vhdr;

  assert (pgptr != NULL);
  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_VOLHEADER);
  vhdr = (DISK_VAR_HEADER *) pgptr;

  assert (vhdr->sect_npgs == DISK_SECTOR_NPAGES);
  assert (vhdr->nsect_total > 0);
  assert (vhdr->nsect_total <= vhdr->nsect_max);
  DISK_SECTS_ASSERT_ROUNDED (vhdr->nsect_total);
  DISK_SECTS_ASSERT_ROUNDED (vhdr->nsect_max);
  assert (disk_Cache == NULL || disk_Cache->vols[vhdr->volid].nsect_free <= vhdr->nsect_total);

  assert (vhdr->stab_first_page == DISK_VOLHEADER_PAGE + 1);
  assert (vhdr->stab_npages >= CEIL_PTVDIV (vhdr->nsect_total, DISK_STAB_PAGE_BIT_COUNT));
  assert (vhdr->stab_npages == CEIL_PTVDIV (vhdr->nsect_max, DISK_STAB_PAGE_BIT_COUNT));
  assert (vhdr->sys_lastpage == (vhdr->stab_first_page + vhdr->stab_npages - 1));

  assert (vhdr->purpose == DB_PERMANENT_DATA_PURPOSE || vhdr->purpose == DB_TEMPORARY_DATA_PURPOSE);
  assert (vhdr->type == DB_PERMANENT_VOLTYPE || vhdr->type == DB_TEMPORARY_VOLTYPE);
  assert (vhdr->type != DB_TEMPORARY_VOLTYPE || vhdr->purpose != DB_PERMANENT_DATA_PURPOSE);
#endif /* !NDEBUG */
}

/************************************************************************/
/*                                                                      */
/* FILE MANAGER REDESIGN                                                */
/*                                                                      */
/************************************************************************/

/************************************************************************/
/* Disk allocation table section.                                       */
/************************************************************************/

/*
 * disk_stab_cursor_set_at_sectid () - Position cursor for allocation table at sector ID.
 *
 * return	  : Void.
 * volheader (in) : Volume header.
 * sectid (in)	  : Sector ID.
 * cursor (out)	  : Allocation table cursor.
 */
STATIC_INLINE void
disk_stab_cursor_set_at_sectid (const DISK_VAR_HEADER * volheader, SECTID sectid, DISK_STAB_CURSOR * cursor)
{
  assert (volheader != NULL);
  assert (cursor != NULL);
  assert (sectid >= 0 && sectid <= volheader->nsect_total);

  cursor->volheader = volheader;
  cursor->sectid = sectid;

  cursor->pageid = volheader->stab_first_page + DISK_ALLOCTBL_SECTOR_PAGE_OFFSET (sectid);
  assert (cursor->pageid < volheader->stab_first_page + volheader->stab_npages);
  cursor->offset_to_unit = DISK_ALLOCTBL_SECTOR_UNIT_OFFSET (sectid);
  cursor->offset_to_bit = DISK_ALLOCTBL_SECTOR_BIT_OFFSET (sectid);

  cursor->page = NULL;
  cursor->unit = NULL;
}

/*
 * disk_stab_cursor_set_at_end () - Position cursor at the end of allocation table.
 *
 * return	  : Void.
 * volheader (in) : Volume header.
 * cursor (out)   : Allocation table cursor.
 */
STATIC_INLINE void
disk_stab_cursor_set_at_end (const DISK_VAR_HEADER * volheader, DISK_STAB_CURSOR * cursor)
{
  assert (volheader != NULL);
  assert (cursor != NULL);

  cursor->volheader = volheader;

  DISK_SECTS_ASSERT_ROUNDED (volheader->nsect_total);
  disk_stab_cursor_set_at_sectid (volheader, volheader->nsect_total, cursor);
}

/*
 * disk_stab_cursor_set_at_start () - Position cursor at the start of allocation table.
 *
 * return	  : Void.
 * volheader (in) : Volume header.
 * cursor (out)   : Allocation table cursor.
 */
STATIC_INLINE void
disk_stab_cursor_set_at_start (const DISK_VAR_HEADER * volheader, DISK_STAB_CURSOR * cursor)
{
  assert (volheader != NULL);
  assert (cursor != NULL);

  cursor->volheader = volheader;
  cursor->sectid = 0;

  cursor->pageid = volheader->stab_first_page;
  cursor->offset_to_unit = 0;
  cursor->offset_to_bit = 0;
  cursor->page = NULL;
  cursor->unit = NULL;
}

/*
 * disk_stab_cursor_compare () - Compare two allocation table cursors.
 *
 * return	      : -1 if first cursor is positioned before second cursor.
 *			0 if both cursors have the same position.
 *			1 if first cursor is positioned after second cursor.
 * first_cursor (in)  : First cursor.
 * second_cursor (in) : Second cursor.
 */
STATIC_INLINE int
disk_stab_cursor_compare (const DISK_STAB_CURSOR * first_cursor, const DISK_STAB_CURSOR * second_cursor)
{
  assert (first_cursor != NULL);
  assert (second_cursor != NULL);

  if (first_cursor->pageid < second_cursor->pageid)
    {
      return -1;
    }
  else if (first_cursor->pageid > second_cursor->pageid)
    {
      return 1;
    }

  if (first_cursor->offset_to_unit < second_cursor->offset_to_unit)
    {
      return -1;
    }
  else if (first_cursor->offset_to_unit > second_cursor->offset_to_unit)
    {
      return 1;
    }

  if (first_cursor->offset_to_bit < second_cursor->offset_to_bit)
    {
      return -1;
    }
  else if (first_cursor->offset_to_bit > second_cursor->offset_to_bit)
    {
      return 1;
    }

  return 0;
}

/*
 * disk_stab_cursor_check_valid () - Check allocation table cursor validity.
 *
 * return      : void.
 * cursor (in) : Allocation table cursor.
 *
 * NOTE: This is a debug function.
 */
STATIC_INLINE void
disk_stab_cursor_check_valid (const DISK_STAB_CURSOR * cursor)
{
  assert (cursor != NULL);

  if (cursor->pageid == NULL_PAGEID)
    {
      /* Must be recovery. */
      assert (!LOG_ISRESTARTED ());
    }
  else
    {
      /* Cursor must have valid volheader pointer. */
      assert (cursor->volheader != NULL);

      /* Cursor must have a valid position. */
      assert (cursor->pageid >= cursor->volheader->stab_first_page);
      assert (cursor->pageid < cursor->volheader->stab_first_page + cursor->volheader->stab_npages);
      assert ((cursor->pageid - cursor->volheader->stab_first_page) * DISK_STAB_PAGE_BIT_COUNT
	      + cursor->offset_to_unit * DISK_STAB_UNIT_BIT_COUNT + cursor->offset_to_bit == cursor->sectid);
    }

  assert (cursor->offset_to_unit >= 0);
  assert (cursor->offset_to_unit < DISK_STAB_PAGE_UNITS_COUNT);
  assert (cursor->offset_to_bit >= 0);
  assert (cursor->offset_to_bit < DISK_STAB_UNIT_BIT_COUNT);

  if (cursor->unit != NULL)
    {
      /* Must have a page fixed */
      assert (cursor->page != NULL);
      /* Unit pointer must match offset_to_unit */
      assert ((cursor->unit - ((DISK_STAB_UNIT *) cursor->page)) == cursor->offset_to_unit);
    }
}

/*
 * disk_stab_cursor_is_bit_set () - Is bit set on current allocation table cursor.
 *
 * return      : True if bit is set, false otherwise.
 * cursor (in) : Allocation table cursor.
 */
STATIC_INLINE bool
disk_stab_cursor_is_bit_set (const DISK_STAB_CURSOR * cursor)
{
  disk_stab_cursor_check_valid (cursor);
  /* update if unit size is changed */
  return bit64_is_set (*cursor->unit, cursor->offset_to_bit);
}

/*
 * disk_stab_cursor_set_bit () - Set bit on current allocation table cursor.
 *
 * return	   : Void.
 * cursor (in/out) : Allocation table cursor.
 */
STATIC_INLINE void
disk_stab_cursor_set_bit (DISK_STAB_CURSOR * cursor)
{
  disk_stab_cursor_check_valid (cursor);
  assert (!disk_stab_cursor_is_bit_set (cursor));
  /* update if unit size is changed */
  *cursor->unit = bit64_set (*cursor->unit, cursor->offset_to_bit);
}

/*
 * disk_stab_cursor_clear_bit () - Clear bit on current allocation table cursor.
 *
 * return	   : Void.
 * cursor (in/out) : Allocation table cursor.
 */
STATIC_INLINE void
disk_stab_cursor_clear_bit (DISK_STAB_CURSOR * cursor)
{
  disk_stab_cursor_check_valid (cursor);
  assert (!disk_stab_cursor_is_bit_set (cursor));
  /* update if unit size is changed */
  *cursor->unit = bit64_clear (*cursor->unit, cursor->offset_to_bit);
}

/*
 * disk_stab_cursor_get_bit_index_in_page () - Get the index of cursor bit in allocation table page.
 *
 * return      : Bit index in page.
 * cursor (in) : Allocation table cursor.
 */
STATIC_INLINE int
disk_stab_cursor_get_bit_index_in_page (const DISK_STAB_CURSOR * cursor)
{
  disk_stab_cursor_check_valid (cursor);
  return cursor->offset_to_unit * DISK_STAB_UNIT_BIT_COUNT + cursor->offset_to_bit;
}

/*
 * disk_alloctbl_cursor_get_sectid () - Get the sector ID of cursor.
 *
 * return      : Sector ID.
 * cursor (in) : Allocation table cursor.
 */
STATIC_INLINE SECTID
disk_stab_cursor_get_sectid (const DISK_STAB_CURSOR * cursor)
{
  disk_stab_cursor_check_valid (cursor);

  return cursor->sectid;
}

/*
 * disk_stab_cursor_fix () - Fix current table page.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * cursor (in/out) : Allocation table cursor.
 * latch_mode (in) : Page latch mode.
 */
STATIC_INLINE int
disk_stab_cursor_fix (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, PGBUF_LATCH_MODE latch_mode)
{
  VPID vpid = VPID_INITIALIZER;
  int error_code = NO_ERROR;

  assert (cursor->page == NULL);

  cursor->unit = NULL;

  /* Fix page. */
  vpid.volid = cursor->volheader->volid;
  vpid.pageid = cursor->pageid;
  cursor->page = pgbuf_fix (thread_p, &vpid, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
  if (cursor->page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  cursor->unit = (DISK_STAB_UNIT *) (cursor->page + cursor->offset_to_unit);

  return NO_ERROR;
}

/*
 * disk_stab_cursor_unfix () - Unfix page from allocation table cursor.
 *
 * return	   : Void.
 * thread_p (in)   : Thread entry.
 * cursor (in/out) : Allocation table cursor.
 */
STATIC_INLINE void
disk_stab_cursor_unfix (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor)
{
  if (cursor->page != NULL)
    {
      pgbuf_unfix_and_init (thread_p, cursor->page);
    }
  cursor->unit = NULL;
}

/*
 * disk_stab_unit_reserve () - DISK_STAB_UNIT_FUNC function used to lookup and reserve free sectors
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * cursor (in)   : disk sector table cursor
 * stop (out)    : output true when all required sectors are reserved
 * args (in/out) : reserve context
 */
static int
disk_stab_unit_reserve (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args)
{
  DISK_RESERVE_CONTEXT *context;
  DISK_STAB_UNIT log_unit;
  SECTID sectid;

  /* how it works
   * look for unset bits and reserve the required number of sectors.
   * we have two special cases, which can be very usual:
   * 1. full unit - nothing can be reserved, so we early out
   * 2. empty unit - we can consume it all (if we need it all) or just trailing bits.
   * otherwise, we iterate bit by bit and reserve free sectors.
   */

  if (*cursor->unit == BIT64_FULL)
    {
      /* nothing free */
      return NO_ERROR;
    }

  context = (DISK_RESERVE_CONTEXT *) args;
  assert (context->nsects_lastvol_remaining > 0);

  if (*cursor->unit == 0)
    {
      /* empty unit. set all required bits */
      int bits_to_set = MIN (context->nsects_lastvol_remaining, DISK_STAB_UNIT_BIT_COUNT);
      int i;

      if (bits_to_set == DISK_STAB_UNIT_BIT_COUNT)
	{
	  /* Consume all unit */
	  *cursor->unit = BIT64_FULL;
	}
      else
	{
	  /* consume only part of unit */
	  *cursor->unit = bit64_set_trailing_bits (*cursor->unit, bits_to_set);
	}
      /* what we log */
      log_unit = *cursor->unit;

      /* update reserve context */
      context->nsects_lastvol_remaining -= bits_to_set;

      /* save sector ids */
      for (i = 0, sectid = disk_stab_cursor_get_sectid (cursor); i < bits_to_set; i++, sectid++)
	{
	  context->vsidp->volid = cursor->volheader->volid;
	  context->vsidp->sectid = sectid;
	  context->vsidp++;
	}
    }
  else
    {
      /* iterate through unit bits */
      log_unit = 0;
      for (cursor->offset_to_bit = bit64_count_trailing_ones (*cursor->unit), cursor->sectid += cursor->offset_to_bit;
	   cursor->offset_to_bit < DISK_STAB_UNIT_BIT_COUNT && context->nsects_lastvol_remaining > 0;
	   cursor->offset_to_bit++, cursor->sectid++)
	{
	  disk_stab_cursor_check_valid (cursor);

	  if (!disk_stab_cursor_is_bit_set (cursor))
	    {
	      /* reserve this sector */
	      disk_stab_cursor_set_bit (cursor);

	      /* update what we log */
	      log_unit = bit64_set (log_unit, cursor->offset_to_bit);

	      /* update context */
	      context->nsects_lastvol_remaining--;

	      /* save vsid */
	      context->vsidp->volid = cursor->volheader->volid;
	      context->vsidp->sectid = cursor->sectid;
	      context->vsidp++;
	    }
	}
    }

  /* safe guard: we must have done something, so log_unit cannot be 0 */
  assert (log_unit != 0);
  /* safe guard: all bits in log_unit must be set */
  assert ((log_unit & *cursor->unit) == log_unit);
  if (context->purpose == DB_PERMANENT_DATA_PURPOSE)
    {
      /* log changes */
      log_append_undoredo_data2 (thread_p, RVDK_RESERVE_SECTORS, NULL, cursor->page, cursor->offset_to_unit,
				 sizeof (log_unit), sizeof (log_unit), &log_unit, &log_unit);
    }
  /* page was modified */
  pgbuf_set_dirty (thread_p, cursor->page, DONT_FREE);

  if (context->nsects_lastvol_remaining <= 0)
    {
      /* all required sectors are reserved, we can stop now */
      assert (context->nsects_lastvol_remaining == 0);
      *stop = true;
    }
  return NO_ERROR;
}

/*
 * disk_stab_iterate_units () - iterate through units between start and end and call argument function. start and end
 *                              cursor should be aligned.
 *
 * return           : error code
 * thread_p (in)    : thread entry
 * volheader (in)   : volume header
 * mode (in)        : page latch mode
 * start (in)       : start cursor
 * end (in)         : end cursor
 * f_unit (in)      : function called on each unit
 * f_unit_args (in) : argument for unit function
 */
static int
disk_stab_iterate_units (THREAD_ENTRY * thread_p, const DISK_VAR_HEADER * volheader, PGBUF_LATCH_MODE mode,
			 DISK_STAB_CURSOR * start, DISK_STAB_CURSOR * end, DISK_STAB_UNIT_FUNC f_unit,
			 void *f_unit_args)
{
  DISK_STAB_CURSOR cursor;
  DISK_STAB_UNIT *end_unit;
  bool stop = false;

  int error_code = NO_ERROR;

  assert (volheader != NULL);
  assert (start->offset_to_bit == 0);
  assert (end->offset_to_bit == 0);
  assert (disk_stab_cursor_compare (start, end));

  /* iterate through pages */
  for (cursor = *start; cursor.pageid <= end->pageid; cursor.pageid++, cursor.offset_to_unit = 0)
    {
      assert (cursor.page == NULL);
      disk_stab_cursor_check_valid (&cursor);

      error_code = disk_stab_cursor_fix (thread_p, &cursor, mode);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}

      /* iterate through units */

      /* set end_unit */
      end_unit =
	((DISK_STAB_UNIT *) cursor.page)
	+ (cursor.pageid == end->pageid ? end->offset_to_unit : DISK_STAB_PAGE_UNITS_COUNT);
      /* iterate */
      for (; cursor.unit < end_unit;
	   cursor.unit++, cursor.offset_to_unit++, cursor.sectid += (DISK_STAB_UNIT_BIT_COUNT - cursor.offset_to_bit),
	   cursor.offset_to_bit = 0)
	{
	  disk_stab_cursor_check_valid (&cursor);

	  /* call unit function */
	  error_code = f_unit (thread_p, &cursor, &stop, f_unit_args);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      disk_stab_cursor_unfix (thread_p, &cursor);
	      return error_code;
	    }
	  if (stop)
	    {
	      /* stop */
	      disk_stab_cursor_unfix (thread_p, &cursor);
	      return NO_ERROR;
	    }
	}
      disk_stab_cursor_unfix (thread_p, &cursor);
    }
  return NO_ERROR;
}

/*
 * disk_stab_iterate_units_all () - iterate trough all sector table units and apply function
 *
 * return           : error code
 * thread_p (in)    : thread entry
 * volheader (in)   : volume header
 * mode (in)        : page latch mode
 * f_unit (in)      : function to apply for each unit
 * f_unit_args (in) : argument for unit function
 */
static int
disk_stab_iterate_units_all (THREAD_ENTRY * thread_p, const DISK_VAR_HEADER * volheader, PGBUF_LATCH_MODE mode,
			     DISK_STAB_UNIT_FUNC f_unit, void *f_unit_args)
{
  DISK_STAB_CURSOR start, end;

  disk_stab_cursor_set_at_start (volheader, &start);
  disk_stab_cursor_set_at_end (volheader, &end);

  return disk_stab_iterate_units (thread_p, volheader, mode, &start, &end, f_unit, f_unit_args);
}

/*
 * disk_stab_count_free () - DISK_STAB_UNIT_FUNC to count free sectors
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * cursor (in)   : disk sector table cursor
 * stop (in)     : not used
 * args (in/out) : free sectors total count
 */
static int
disk_stab_count_free (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args)
{
  DKNSECTS *nfreep = (DKNSECTS *) args;

  /* add zero bit count to free sectors total count */
  *nfreep += bit64_count_zeros (*cursor->unit);
  return NO_ERROR;
}

/*
 * disk_stab_set_bits_contiguous () - set first bits
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * cursor (in)   : sector table cursor
 * stop (out)    : output true when all required bits are set
 * args (in/out) : remaining number of bits to set
 */
static int
disk_stab_set_bits_contiguous (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args)
{
  DKNSECTS *nsectsp = (DKNSECTS *) args;

  if (*nsectsp > DISK_STAB_UNIT_BIT_COUNT)
    {
      *cursor->unit = BIT64_FULL;
      (*nsectsp) -= DISK_STAB_UNIT_BIT_COUNT;
    }
  else
    {
      *cursor->unit = bit64_set_trailing_bits (0, *nsectsp);
      *nsectsp = 0;
      *stop = true;
    }
  return NO_ERROR;
}

/************************************************************************/
/* Sector reserve section                                               */
/************************************************************************/

/*
 * disk_rv_reserve_sectors () - Apply recovery for reserving sectors.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * rcv (in)	   : Recovery data.
 */
int
disk_rv_reserve_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_STAB_UNIT rv_unit = *(DISK_STAB_UNIT *) rcv->data;
  DISK_STAB_UNIT *stab_unit;
  VOLID volid;
  DB_VOLPURPOSE purpose;
  DKNSECTS nsect;

  int error_code = NO_ERROR;

  assert (rcv->pgptr != NULL);
  assert (rcv->length == sizeof (rv_unit));
  assert (rcv->offset >= 0 && rcv->offset < DISK_STAB_PAGE_UNITS_COUNT);

  /* we need to enter CSECT_DISK_CHECK as reader, to make sure we do not conflict with disk check */
  error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, 0);
  if (error_code == NO_ERROR)
    {
      /* we locked. */
    }
  else if (error_code == ETIMEDOUT)
    {
      /* disk check is in progress. unfix page, get critical section again and then fix page and make the changes */
      VPID vpid;
      pgbuf_get_vpid (rcv->pgptr, &vpid);
      pgbuf_unfix_and_init (thread_p, rcv->pgptr);
      error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, INF_WAIT);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* fix page again */
      rcv->pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (rcv->pgptr == NULL)
	{
	  /* should not fail */
	  assert_release (false);
	  return ER_FAILED;
	}
    }
  else
    {
      assert_release (false);
      return ER_FAILED;
    }

  pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_VOLBITMAP);

  stab_unit = ((DISK_STAB_UNIT *) rcv->pgptr) + rcv->offset;
  assert (((*stab_unit) & rv_unit) == 0);
  *stab_unit |= rv_unit;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  /* update cache */
  volid = pgbuf_get_volume_id (rcv->pgptr);
  purpose = disk_get_volpurpose (volid);

  nsect = bit64_count_ones (rv_unit);

  disk_cache_lock_reserve_for_purpose (purpose);
  disk_cache_update_vol_free (volid, -nsect);
  disk_cache_unlock_reserve_for_purpose (purpose);

  csect_exit (thread_p, CSECT_DISK_CHECK);
  return NO_ERROR;
}

/*
 * disk_rv_unreserve_sectors () - Apply recovery for unreserving sectors.
 *
 * return	   : Error code.
 * thread_p (in)   : Thread entry.
 * rcv (in)	   : Recovery data.
 */
int
disk_rv_unreserve_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_STAB_UNIT rv_unit = *(DISK_STAB_UNIT *) rcv->data;
  DISK_STAB_UNIT *stab_unit;
  VOLID volid;
  DB_VOLPURPOSE purpose;
  DKNSECTS nsect;

  int error_code = NO_ERROR;

  assert (rcv->pgptr != NULL);
  assert (rcv->length == sizeof (rv_unit));
  assert (rcv->offset >= 0 && rcv->offset < DISK_STAB_PAGE_UNITS_COUNT);

  /* we need to enter CSECT_DISK_CHECK as reader, to make sure we do not conflict with disk check */
  error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, 0);
  if (error_code == NO_ERROR)
    {
      /* we locked. */
    }
  else if (error_code == ETIMEDOUT)
    {
      /* disk check is in progress. unfix page, get critical section again and then fix page and make the changes */
      VPID vpid;
      pgbuf_get_vpid (rcv->pgptr, &vpid);
      pgbuf_unfix_and_init (thread_p, rcv->pgptr);
      error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, INF_WAIT);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  return ER_FAILED;
	}
      /* fix page again */
      rcv->pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (rcv->pgptr == NULL)
	{
	  /* should not fail */
	  assert_release (false);
	  return ER_FAILED;
	}
    }
  else
    {
      assert_release (false);
      return ER_FAILED;
    }

  pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_VOLBITMAP);

  stab_unit = ((DISK_STAB_UNIT *) rcv->pgptr) + rcv->offset;
  assert (((*stab_unit) & rv_unit) == rv_unit);
  *stab_unit &= ~rv_unit;

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  /* update cache */
  volid = pgbuf_get_volume_id (rcv->pgptr);
  purpose = disk_get_volpurpose (volid);

  nsect = bit64_count_ones (rv_unit);

  disk_cache_lock_reserve_for_purpose (purpose);
  disk_cache_update_vol_free (volid, nsect);
  disk_cache_unlock_reserve_for_purpose (purpose);

  csect_exit (thread_p, CSECT_DISK_CHECK);
  return NO_ERROR;
}

/*
 * disk_reserve_sectors_in_volume () - Reserve a number of sectors in the given volume.
 *
 * return	    : Error code.
 * thread_p (in)    : Thread entry.
 * vol_index (in)   : The index of volume in reserve context
 * context (in/out) : Reserve context
 */
static int
disk_reserve_sectors_in_volume (THREAD_ENTRY * thread_p, int vol_index, DISK_RESERVE_CONTEXT * context)
{
  VOLID volid;
  PAGE_PTR page_volheader = NULL;
  DISK_VAR_HEADER *volheader = NULL;
  DISK_STAB_CURSOR start_cursor = DISK_STAB_CURSOR_INITIALIZER;
  DISK_STAB_CURSOR end_cursor = DISK_STAB_CURSOR_INITIALIZER;
  int vol_free_sects = 0;

  int error_code = NO_ERROR;

  volid = context->cache_vol_reserve[vol_index].volid;
  if (volid == NULL_VOLID)
    {
      assert_release (false);
      return ER_FAILED;
    }
  context->nsects_lastvol_remaining = context->cache_vol_reserve[vol_index].nsect;
  assert (context->nsects_lastvol_remaining > 0);

  /* fix volume header */
  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &page_volheader, &volheader);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* reserve all possible sectors. */
  if (volheader->hint_allocsect > 0 && volheader->hint_allocsect < volheader->nsect_total)
    {
      /* start with hinted sector */
      DISK_SECTS_ASSERT_ROUNDED (volheader->hint_allocsect);
      disk_stab_cursor_set_at_sectid (volheader, volheader->hint_allocsect, &start_cursor);
      disk_stab_cursor_set_at_end (volheader, &end_cursor);

      /* reserve sectors after hint */
      error_code =
	disk_stab_iterate_units (thread_p, volheader, PGBUF_LATCH_WRITE, &start_cursor, &end_cursor,
				 disk_stab_unit_reserve, context);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      if (context->nsects_lastvol_remaining > 0)
	{
	  /* we need to reserve more. reserve sectors before hint */
	  end_cursor = start_cursor;
	  disk_stab_cursor_set_at_start (volheader, &start_cursor);
	  error_code =
	    disk_stab_iterate_units (thread_p, volheader, PGBUF_LATCH_WRITE, &start_cursor, &end_cursor,
				     disk_stab_unit_reserve, context);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      goto exit;
	    }
	}
    }
  else
    {
      /* search the entire sector table */
      disk_stab_cursor_set_at_start (volheader, &start_cursor);
      disk_stab_cursor_set_at_end (volheader, &end_cursor);
      error_code =
	disk_stab_iterate_units (thread_p, volheader, PGBUF_LATCH_WRITE, &start_cursor, &end_cursor,
				 disk_stab_unit_reserve, context);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }
  if (context->nsects_lastvol_remaining != 0)
    {
      /* our logic must be flawed... the sectors are reserved ahead so we should have found enough free sectors */
      assert_release (false);
      error_code = ER_FAILED;
      goto exit;
    }
  /* update hint */
  volheader->hint_allocsect = (context->vsidp - 1)->sectid + 1;
  volheader->hint_allocsect = DISK_SECTS_ROUND_DOWN (volheader->hint_allocsect);
  /* we don't really need to set the page dirty or log the hint change. */

exit:
  if (page_volheader != NULL)
    {
      pgbuf_unfix (thread_p, page_volheader);
    }
  return error_code;
}

/*
 * disk_is_page_sector_reserved () - Is sector of page reserved?
 *
 * return        : Valid if sector of page is reserved, invalid (or error) otherwise.
 * thread_p (in) : Thread entry
 * volid (in)    : Page volid
 * pageid (in)   : Page pageid
 */
DISK_ISVALID
disk_is_page_sector_reserved (THREAD_ENTRY * thread_p, VOLID volid, PAGEID pageid)
{
  PAGE_PTR page_volheader = NULL;
  DISK_VAR_HEADER *volheader;
  DISK_ISVALID isvalid = DISK_VALID;
  SECTID sectid;
  bool old_check_interrupt;

  old_check_interrupt = thread_set_check_interrupt (thread_p, false);

  if (fileio_get_volume_descriptor (volid) == NULL_VOLDES || pageid < 0)
    {
      /* invalid */
      assert (false);
      isvalid = DISK_INVALID;
      goto exit;
    }

  if (pageid == DISK_VOLHEADER_PAGE)
    {
      /* valid */
      isvalid = DISK_VALID;
      goto exit;
    }

  if (disk_get_volheader (thread_p, volid, PGBUF_LATCH_READ, &page_volheader, &volheader) != NO_ERROR)
    {
      ASSERT_ERROR ();
      isvalid = DISK_ERROR;
      goto exit;
    }

  if (pageid <= volheader->sys_lastpage)
    {
      isvalid = DISK_VALID;
      goto exit;
    }
  if (pageid > DISK_SECTS_NPAGES (volheader->nsect_total))
    {
      isvalid = DISK_INVALID;
      goto exit;
    }

  sectid = SECTOR_FROM_PAGEID (pageid);
  isvalid = disk_is_sector_reserved (thread_p, volheader, sectid);

exit:
  (void) thread_set_check_interrupt (thread_p, old_check_interrupt);

  if (page_volheader)
    {
      pgbuf_unfix (thread_p, page_volheader);
    }

  return isvalid;
}

/*
 * disk_is_sector_reserved () - Return valid if sector is reserved, invalid otherwise.
 *
 * return         : Valid if sector is reserved, invalid if it is not reserved or error if table page cannot be fixed.
 * thread_p (in)  : Thread entry
 * volheader (in) : Volume header
 * sectid (in)    : Sector ID
 */
static DISK_ISVALID
disk_is_sector_reserved (THREAD_ENTRY * thread_p, const DISK_VAR_HEADER * volheader, SECTID sectid)
{
  DISK_STAB_CURSOR cursor_sectid;

  disk_stab_cursor_set_at_sectid (volheader, sectid, &cursor_sectid);
  if (disk_stab_cursor_fix (thread_p, &cursor_sectid, PGBUF_LATCH_READ) != NO_ERROR)
    {
      return DISK_ERROR;
    }
  if (!disk_stab_cursor_is_bit_set (&cursor_sectid))
    {
      disk_stab_cursor_unfix (thread_p, &cursor_sectid);
      return DISK_INVALID;
    }
  else
    {
      disk_stab_cursor_unfix (thread_p, &cursor_sectid);
      return DISK_VALID;
    }
}

/*
 * disk_reserve_sectors () - Reserve the required number of sectors in all database volumes.
 *
 * return		  : Error code.
 * thread_p (in)	  : Thread entry.
 * purpose (in)		  : Reservations purpose (data, index, generic or temp).
 * reserve_type (in)	  : Contiguous/non-contiguous (not used).
 * volid_hint (in)	  : Hint a volume to be checked first.
 * n_sectors (in)	  : Number of sectors to reserve.
 * reserved_sectors (out) : Array of reserved sectors.
 */
int
disk_reserve_sectors (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, DISK_SETPAGE_TYPE reserve_type, VOLID volid_hint,
		      int n_sectors, VSID * reserved_sectors)
{
  int n_sectors_found = 0;
  int n_sectors_found_in_last_volume = 0;
  int n_sectors_to_find = n_sectors;
  VOLID volid = NULL_VOLID;
  VOLID banned_volid = NULL_VOLID;
  int iter;

  DISK_RESERVE_CONTEXT context;

  int error_code = NO_ERROR;

  assert (purpose == DB_PERMANENT_DATA_PURPOSE || purpose == DB_TEMPORARY_DATA_PURPOSE);

  if (n_sectors <= 0 || reserved_sectors == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }

  if (purpose != DB_TEMPORARY_DATA_PURPOSE && !log_check_system_op_is_started (thread_p))
    {
      /* We really need a system operation. */
      assert (false);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  log_sysop_start (thread_p);

  /* we don't want to conflict with disk check */
  error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, INF_WAIT);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      return error_code;
    }

  /* init context */
  context.nsect_total = n_sectors;
  context.n_cache_reserve_remaining = n_sectors;
  context.vsidp = reserved_sectors;
  context.n_cache_vol_reserve = 0;
  context.purpose = purpose;

  error_code = disk_reserve_from_cache (thread_p, &context);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto error;
    }

  for (iter = 0; iter < context.n_cache_vol_reserve; iter++)
    {
      error_code = disk_reserve_sectors_in_volume (thread_p, iter, &context);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto error;
	}
    }

  /* Should have enough sectors. */
  assert ((context.vsidp - reserved_sectors) == n_sectors);

  /* attach sys op to outer */
  log_sysop_attach_to_outer (thread_p);

  csect_exit (thread_p, CSECT_DISK_CHECK);

  return NO_ERROR;

error:

  /* abort any changes */
  log_sysop_abort (thread_p);

  /* undo cache reserve */
  disk_cache_free_reserved (&context);

  csect_exit (thread_p, CSECT_DISK_CHECK);

  return error_code;
}

/*
 * disk_reserve_ahead () - First step of sector reservation on disk. This searches the cache for free space in existing
 *                         volumes. If not enough available sectors were found, volumes will be expanded/added until
 *                         all sectors could be reserved.
 *                         NOTE: this will modify the disk cache. It will "move" free sectors from disk cache to reserve
 *                         context. If any error occurs, the sectors must be returned to disk cache.
 *
 * return           : Error code
 * thread_p (in)    : Thread entry
 * context (in/out) : Reserve context
 */
static int
disk_reserve_from_cache (THREAD_ENTRY * thread_p, DISK_RESERVE_CONTEXT * context)
{
  DISK_EXTEND_INFO *extend_info;
  DKNSECTS save_remaining;

  int error_code = NO_ERROR;

  if (disk_Cache == NULL)
    {
      /* not initialized? */
      assert_release (false);
      return ER_FAILED;
    }

  disk_cache_lock_reserve_for_purpose (context->purpose);
  if (context->purpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      /* if we want to allocate temporary files, we have two options: preallocated permanent volumes (but with the
       * purpose of temporary files) or temporary volumes. try first the permanent volumes */

      extend_info = &disk_Cache->temp_purpose_info.extend_info;

      if (disk_Cache->temp_purpose_info.nsect_perm_free > 0)
	{
	  disk_reserve_from_cache_vols (DB_PERMANENT_VOLTYPE, context);
	}
      if (context->n_cache_reserve_remaining <= 0)
	{
	  /* found enough sectors */
	  assert (context->n_cache_reserve_remaining == 0);
	  disk_cache_unlock_reserve_for_purpose (context->purpose);
	  return NO_ERROR;
	}

      /* reserve sectors from temporary volumes */
      extend_info = &disk_Cache->temp_purpose_info.extend_info;
      if (extend_info->nsect_total - extend_info->nsect_free + context->n_cache_reserve_remaining
	  >= disk_Temp_max_sects)
	{
	  /* too much temporary space */
	  assert (false);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_MAXTEMP_SPACE_HAS_BEEN_EXCEEDED, 1, disk_Temp_max_sects);
	  disk_cache_unlock_reserve_for_purpose (context->purpose);
	  return ER_BO_MAXTEMP_SPACE_HAS_BEEN_EXCEEDED;
	}

      /* fall through */
    }
  else
    {
      extend_info = &disk_Cache->perm_purpose_info.extend_info;
      /* fall through */
    }

  assert (context->n_cache_reserve_remaining > 0);
  assert (extend_info->owner_reserve == thread_get_current_entry_index ());

  if (extend_info->nsect_free > context->n_cache_reserve_remaining)
    {
      disk_reserve_from_cache_vols (extend_info->voltype, context);
      if (context->n_cache_reserve_remaining <= 0)
	{
	  /* found enough sectors */
	  assert (context->n_cache_reserve_remaining == 0);
	  disk_cache_unlock_reserve (extend_info);
	  return NO_ERROR;
	}
    }

  /* we might have to expand */
  /* first, save our intention in case somebody else will do the expand */
  extend_info->nsect_intention += context->n_cache_reserve_remaining;
  disk_cache_unlock_reserve (extend_info);

  /* now lock expand */
  disk_lock_extend ();

  /* check again free sectors */
  disk_cache_lock_reserve (extend_info);
  if (extend_info->nsect_free > context->n_cache_reserve_remaining)
    {
      /* somebody else expanded? try again to reserve */
      /* also update intention */
      extend_info->nsect_intention -= context->n_cache_reserve_remaining;

      disk_reserve_from_cache_vols (extend_info->voltype, context);
      if (context->n_cache_reserve_remaining <= 0)
	{
	  assert (context->n_cache_reserve_remaining == 0);
	  disk_cache_unlock_reserve (extend_info);
	  disk_unlock_extend ();
	  return NO_ERROR;
	}

      extend_info->nsect_intention += context->n_cache_reserve_remaining;
    }

  /* ok, we really have to extend the disk space ourselves */
  save_remaining = context->n_cache_reserve_remaining;

  disk_cache_unlock_reserve (extend_info);
  error_code = disk_extend (thread_p, extend_info, context);

  /* remove intention */
  disk_cache_lock_reserve (extend_info);
  extend_info->nsect_intention -= save_remaining;
  disk_cache_unlock_reserve (extend_info);

  disk_unlock_extend ();
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (context->n_cache_reserve_remaining > 0)
    {
      assert_release (false);
      return ER_FAILED;
    }
  /* all cache reservations were made */
  return NO_ERROR;
}

/*
 * disk_reserve_ahead_from_perm_vols () - reserve sectors in disk cache permanent volumes
 *
 * return       : Void
 * purpose (in) : Permanent/temporary purpose
 * context (in) : Reserve context
 */
STATIC_INLINE void
disk_reserve_from_cache_vols (DB_VOLTYPE type, DISK_RESERVE_CONTEXT * context)
{
  VOLID volid_iter;
  VOLID start_iter, end_iter, incr;
  DKNSECTS min_free;

  assert (disk_compatible_type_and_purpose (type, context->purpose));

  if (type == DB_PERMANENT_VOLTYPE)
    {
      start_iter = 0;
      end_iter = disk_Cache->nvols_perm;
      incr = 1;

      min_free = MIN (context->nsect_total, disk_Cache->perm_purpose_info.nsect_vol_max) / 2;
    }
  else
    {
      start_iter = LOG_MAX_DBVOLID;
      end_iter = LOG_MAX_DBVOLID - disk_Cache->nvols_temp;
      incr = -1;

      min_free = MIN (context->nsect_total, disk_Cache->temp_purpose_info.nsect_vol_max) / 2;
    }

  for (volid_iter = start_iter; volid_iter != end_iter && context->n_cache_reserve_remaining > 0; volid_iter += incr)
    {
      if (disk_Cache->vols[volid_iter].purpose != context->purpose)
	{
	  /* not the right purpose. */
	  continue;
	}
      if (disk_Cache->vols[volid_iter].nsect_free < min_free)
	{
	  /* avoid unnecessary fragmentation */
	  continue;
	}
      /* reserve from this volume */
      disk_reserve_from_cache_volume (volid_iter, context);
    }
}

/*
 * disk_extend () - expand disk storage by extending existing volumes or by adding new volumes.
 *
 * return               : error code
 * thread_p (in)        : thread entry
 * extend_info (in)     : disk extend info
 * reserve_context (in) : reserve context (can be NULL)
 */
static int
disk_extend (THREAD_ENTRY * thread_p, DISK_EXTEND_INFO * extend_info, DISK_RESERVE_CONTEXT * reserve_context)
{
  DKNSECTS free = extend_info->nsect_free;
  DKNSECTS intention = extend_info->nsect_intention;
  DKNSECTS total = extend_info->nsect_total;
  DKNSECTS max = extend_info->nsect_max;
  DB_VOLTYPE voltype = extend_info->voltype;

  DKNSECTS nsect_extend;
  DKNSECTS target_free;

  DBDEF_VOL_EXT_INFO volext;
  VOLID volid_new = NULL_VOLID;

  DKNSECTS nsect_free_new = 0;

  int error_code = NO_ERROR;

  /* how this works:
   * there can be only one expand running for permanent volumes and one for temporary volumes. any expander must first
   * lock expand mutex.
   *
   * we want to avoid concurrent expansions as much as possible, therefore on each expansion we try to allocate more
   * disk space than currently necessary. moreover, there is an auto-expansion thread that tries to keep a stable level
   * of free space. as long as the worker requirements do not spike, they would never have to do the disk expansion.
   *
   * however, we cannot rule out spikes in disk space requirements. we cannot even rule out concurrent spikes. intention
   * field saves all sector requests that could not be satisfied with existing free space. therefore, one expand
   * iteration can serve all expansion needs.
   *
   * this being said, now let's get to how expand works.
   *
   * first we decide how much to expand. we set the target_free to MAX (1% current size, min threshold). The we subtract
   * the current free space. if the difference is negative, we set it to 0.
   * then we add the reserve intentions that could not be satisfied by existing disk space.
   *
   * once we decided how much we want to expand, we first extend last volume are already extended to their maximum
   * capacities). if last volume is also extended to its maximum capacity, we start adding new volumes.
   * 
   * note: the same algorithm is applied to both permanent and temporary files. more exactly, the expansion is allowed
   *       for permanent volumes used for permanent data purpose or temporary files used for temporary data purpose.
   *       permanent volumes for temporary data purpose can only be added by user and are never extended.
   */

  assert (extend_info->owner_extend == thread_get_current_entry_index ());

  /* expand */
  /* what is the desired remaining free after expand? */
  target_free = MAX ((DKNSECTS) (total * 0.01), DISK_MIN_VOLUME_SECTS);
  /* what is the desired expansion? do not expand less than intention. */
  nsect_extend = MAX (target_free - free, 0) + intention;
  if (nsect_extend <= 0)
    {
      /* no expand needed */
      return NO_ERROR;
    }

  if (total < max)
    {
      /* first expand last volume to its capacity */
      DKNSECTS to_expand;

      assert (extend_info->volid_extend != NULL_VOLID);

      to_expand = MIN (nsect_extend, max - total);
      error_code = disk_volume_expand (thread_p, extend_info->volid_extend, voltype, to_expand, &nsect_free_new);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      assert (nsect_free_new >= to_expand);

      /* subtract from what we need to expand */
      nsect_extend -= nsect_free_new;

      /* no one else modifies total, it is protected by expand mutex */
      extend_info->nsect_total += nsect_free_new;

      disk_cache_lock_reserve (extend_info);
      disk_cache_update_vol_free (extend_info->volid_extend, nsect_free_new);

      if (reserve_context != NULL && reserve_context->n_cache_reserve_remaining > 0)
	{
	  disk_reserve_from_cache_volume (extend_info->volid_extend, reserve_context);
	}
      disk_cache_unlock_reserve (extend_info);

      if (nsect_extend <= 0)
	{
	  /* it is enough */
	  return NO_ERROR;
	}
    }
  assert (nsect_extend > 0);

  /* add new volume(s) */
  volext.nsect_max = (DKNSECTS) (prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE) / IO_SECTORSIZE);
  volext.nsect_max = DISK_SECTS_ROUND_UP (volext.nsect_max);
  volext.comments = reserve_context != NULL ? "Forced Volume Extension" : "Automatic Volume Extension";
  volext.voltype = voltype;
  volext.purpose = voltype == DB_PERMANENT_VOLTYPE ? DB_PERMANENT_DATA_PURPOSE : DB_TEMPORARY_DATA_PURPOSE;
  volext.overwrite = false;
  volext.max_writesize_in_sec = 0;
  while (nsect_extend > 0)
    {
      volext.path = NULL;
      volext.name = NULL;

      /* set size to remaining sectors */
      volext.nsect_total = nsect_extend;
      /* but size cannot exceed max */
      volext.nsect_total = MIN (volext.nsect_max, volext.nsect_total);
      /* and it cannot be lower than a minimum size */
      volext.nsect_total = MAX (volext.nsect_total, DISK_MIN_VOLUME_SECTS);
      /* we always keep rounded number of sectors */
      volext.nsect_total = DISK_SECTS_ROUND_UP (volext.nsect_total);

      /* add new volume */
      error_code = disk_add_volume (thread_p, &volext, &volid_new, &nsect_free_new);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error_code;
	}
      nsect_extend -= nsect_free_new;

      /* update volume count */
      if (voltype == DB_PERMANENT_VOLTYPE)
	{
	  disk_Cache->nvols_perm++;
	}
      else
	{
	  disk_Cache->nvols_temp++;
	}
      assert (disk_Cache->nvols_perm + disk_Cache->nvols_temp <= LOG_MAX_DBVOLID);

      /* update total and max */
      extend_info->nsect_total += volext.nsect_total;
      extend_info->nsect_max += volext.nsect_max;

      disk_cache_lock_reserve (extend_info);
      /* add new volume */
      disk_Cache->vols[volid_new].nsect_free = nsect_free_new;
      disk_Cache->vols[volid_new].purpose =
	voltype == DB_PERMANENT_VOLTYPE ? DB_PERMANENT_DATA_PURPOSE : DB_TEMPORARY_DATA_PURPOSE;
      extend_info->nsect_free += nsect_free_new;

      if (reserve_context && reserve_context->n_cache_reserve_remaining > 0)
	{
	  /* reserve ahead */
	  disk_reserve_from_cache_volume (volid_new, reserve_context);
	}

      disk_cache_unlock_reserve (extend_info);

      if (extend_info->nsect_total < extend_info->nsect_max)
	{
	  /* update volume used for auto extend */
	  extend_info->volid_extend = volid_new;
	}

      assert (disk_is_valid_volid (volid_new));
    }

  /* finished expand */

  /* safe guard: if this was called during sector reservation, the expansion should cover all required sectors. */
  assert (reserve_context == NULL || reserve_context->n_cache_reserve_remaining == 0);
  return NO_ERROR;
}

/*
 * disk_volume_expand () - expand disk space for volume
 *
 * return                   : error code
 * thread_p (in)            : thread entry
 * volid (in)               : volume identifier
 * voltype (in)             : volume type (hint, caller should know)
 * nsect_extend (in)        : desired extension
 * nsect_extended_out (out) : extended size (rounded up desired extension)
 */
static int
disk_volume_expand (THREAD_ENTRY * thread_p, VOLID volid, DB_VOLTYPE voltype, DKNSECTS nsect_extend,
		    DKNSECTS * nsect_extended_out)
{
  PAGE_PTR page_volheader = NULL;
  DISK_VAR_HEADER *volheader = NULL;
  DISK_RECV_INIT_PAGES_INFO log_data;

  int npages;

  bool do_logging;

  int error_code = NO_ERROR;

  assert (nsect_extend > 0);

  /* how it works:
   * we extend the volume disk space by desired size, rounded up. if volume is successfully expanded, we update its
   * header. */

  /* round up */
  nsect_extend = DISK_SECTS_ROUND_UP (nsect_extend);

  /* extend disk space */
  npages = nsect_extend * DISK_SECTOR_NPAGES;
  if (fileio_expand (thread_p, volid, npages, voltype) != npages)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  /* fix volume header */
  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &page_volheader, &volheader);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  assert (volheader->type == voltype);
  do_logging = (volheader->type == DB_PERMANENT_VOLTYPE);

  if (do_logging)
    {
      log_data.volid = volid;
      log_data.start_pageid = volheader->nsect_total * DISK_SECTOR_NPAGES;
      log_data.npages = npages;
      log_append_dboutside_redo (thread_p, RVDK_INIT_PAGES, sizeof (DISK_RECV_INIT_PAGES_INFO), &log_data);
    }

  /* update sector total number */
  volheader->nsect_total += nsect_extend;
  disk_verify_volume_header (thread_p, page_volheader);
  if (do_logging)
    {
      log_append_redo_data2 (thread_p, RVDK_VOLHEAD_EXPAND, NULL, page_volheader, NULL_OFFSET, sizeof (nsect_extend),
			     &nsect_extend);
    }
  pgbuf_set_dirty (thread_p, page_volheader, DONT_FREE);

  *nsect_extended_out = nsect_extend;

  /* success */
  /* caller will update cache */

  pgbuf_unfix (thread_p, page_volheader);
  return NO_ERROR;
}

/*
 * disk_rv_volhead_extend_redo () - volume header redo recovery after volume extension.
 *
 * return        : Error code
 * thread_p (in) : Thread entry
 * rcv (in)      : Recovery data
 */
int
disk_rv_volhead_extend_redo (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *volheader = (DISK_VAR_HEADER *) rcv->pgptr;
  DKNSECTS nsect_extend = *(DKNSECTS *) rcv->data;

  assert (rcv->length == sizeof (nsect_extend));

  disk_verify_volume_header (thread_p, rcv->pgptr);
  volheader->nsect_total += nsect_extend;
  disk_verify_volume_header (thread_p, rcv->pgptr);

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);
  return NO_ERROR;
}

/*
 * disk_add_volume () - add new volume (permanent or temporary)
 *
 * return                : error code
 * thread_p (in)         : thread entry
 * extinfo (in)          : extend info
 * volid_out (out)       : new volume identifier
 * nsects_free_out (out) :
 */
static int
disk_add_volume (THREAD_ENTRY * thread_p, DBDEF_VOL_EXT_INFO * extinfo, VOLID * volid_out, DKNSECTS * nsects_free_out)
{
  char fullname[PATH_MAX];
  VOLID volid;
  DKNSECTS nsect_part_max;

  bool is_sysop_started = true;

  int error_code = NO_ERROR;

  /* how it works:
   *
   * we need to do several steps to add a new volume:
   * 1. get from boot the full name (path + file name) and volume id.
   * 2. make sure there is enough space on disk to handle a new volume.
   * 3. notify page buffer (it needs to track permanent/temporary volumes).
   * 4. format new volume.
   * 5. update volume info file (if permanent volume).
   * 6. update boot_Db_parm.
   */

  if (disk_Cache->nvols_perm + disk_Cache->nvols_temp >= LOG_MAX_DBVOLID)
    {
      /* oops, too many volumes */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_MAXNUM_VOLS_HAS_BEEN_EXCEEDED, 1, LOG_MAX_DBVOLID);
      return ER_BO_MAXNUM_VOLS_HAS_BEEN_EXCEEDED;
    }

  /* get from boot the volume name and identifier */
  error_code =
    boot_get_new_volume_name_and_id (thread_p, extinfo->voltype, extinfo->path, extinfo->name, fullname, &volid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* make sure the total and max size are rounded */
  extinfo->nsect_max = DISK_SECTS_ROUND_UP (extinfo->nsect_max);
  extinfo->nsect_total = DISK_SECTS_ROUND_UP (extinfo->nsect_total);

#if !defined (WINDOWS)
  {
    DBDEF_VOL_EXT_INFO temp_extinfo = *extinfo;
    char vol_realpath[PATH_MAX];
    char link_path[PATH_MAX];
    char link_fullname[PATH_MAX];
    struct stat stat_buf;
    if (stat (fullname, &stat_buf) == 0	/* file exists */
	|| S_ISCHR (stat_buf.st_mode))	/* is the raw device? */
      {
	temp_extinfo.path = fileio_get_directory_path (link_path, boot_db_full_name ());
	if (temp_extinfo.path == NULL)
	  {
	    link_path[0] = '\0';
	    temp_extinfo.path = link_path;
	  }
	temp_extinfo.name = fileio_get_base_file_name (boot_db_full_name ());
	fileio_make_volume_ext_name (link_fullname, temp_extinfo.path, temp_extinfo.name, volid);

	if (realpath (fullname, vol_realpath) != NULL)
	  {
	    strcpy (fullname, vol_realpath);
	  }
	(void) unlink (link_fullname);
	if (symlink (fullname, link_fullname) != 0)
	  {
	    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_LINK, 2, fullname, link_fullname);
	    return ER_BO_CANNOT_CREATE_LINK;
	  }
	strcpy (fullname, link_fullname);

	/* we don't know character special files size */
	nsect_part_max = VOL_MAX_NSECTS (IO_PAGESIZE);
      }
    else
      {
	nsect_part_max = fileio_get_number_of_partition_free_sectors (fullname);
      }
  }
#else /* WINDOWS */
  nsect_part_max = fileio_get_number_of_partition_free_sectors (fullname);
#endif /* WINDOWS */

  if (nsect_part_max >= 0 && nsect_part_max < extinfo->nsect_max)
    {
      /* not enough space on disk */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_OUT_OF_SPACE, 5, fullname,
	      DISK_SECTS_NPAGES (extinfo->nsect_max), DISK_SECTS_SIZE (extinfo->nsect_max) / 1204 /* KB */ ,
	      DISK_SECTS_NPAGES (nsect_part_max), DISK_SECTS_SIZE (nsect_part_max) / 1204 /* KB */ );
      return ER_IO_FORMAT_OUT_OF_SPACE;
    }

  if (extinfo->comments == NULL)
    {
      extinfo->comments = "Volume Extension";
    }
  extinfo->name = fullname;

  if (!extinfo->overwrite && fileio_is_volume_exist (extinfo->name))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_VOLUME_EXISTS, 1, extinfo->name);
      return ER_BO_VOLUME_EXISTS;
    }

  log_sysop_start (thread_p);
  pgbuf_refresh_max_permanent_volume_id (volid);

  error_code = disk_format (thread_p, boot_db_full_name (), volid, extinfo, nsects_free_out);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  if (extinfo->voltype == DB_PERMANENT_VOLTYPE)
    {
      if (logpb_add_volume (NULL, volid, extinfo->name, DB_PERMANENT_DATA_PURPOSE) == NULL_VOLID)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  goto exit;
	}
    }

  /* this must be last step (sys op will get committed/aborted) */
  error_code = boot_dbparm_save_volume (thread_p, extinfo->voltype, volid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  assert (error_code == NO_ERROR);
  *volid_out = volid;

exit:
  if (error_code == NO_ERROR)
    {
      log_sysop_attach_to_outer (thread_p);
    }
  else
    {
      log_sysop_abort (thread_p);
    }

  return NO_ERROR;
}

/*
 * disk_add_volume_extension () - add new volume extension. this can be called by addvol utility or on create database.
 *
 * return                     : error code
 * thread_p (in)              : thread entry
 * purpose (in)               : permanent or temporary purpose
 * npages (in)                : desired number of pages
 * path (in)                  : path to volume file
 * name (in)                  : name of volume file
 * comments (in)              : comments in volume header
 * max_write_size_in_sec (in) : write speed (to limit IO when database is on line)
 * overwrite (in)             : true to overwrite existing file, false otherwise
 * volid_out (out)            : Output new volume identifier
 */
int
disk_add_volume_extension (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, DKNPAGES npages, const char *path,
			   const char *name, const char *comments, int max_write_size_in_sec, bool overwrite,
			   VOLID * volid_out)
{
  DBDEF_VOL_EXT_INFO ext_info;
  VOLID volid_new;
  DKNSECTS nsect_free;
  char buf_realpath[PATH_MAX];

  int error_code = NO_ERROR;

  *volid_out = NULL_VOLID;

  error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, INF_WAIT);

  /* first we need to block other expansions */
  disk_lock_extend ();

  /* build volume extension info */
  ext_info.purpose = purpose;
  if (path != NULL && realpath (path, buf_realpath) != NULL)
    {
      ext_info.path = buf_realpath;
    }
  else
    {
      ext_info.path = path;
    }
  ext_info.path = path;
  ext_info.name = name;
  ext_info.comments = comments;
  ext_info.max_writesize_in_sec = max_write_size_in_sec;
  ext_info.overwrite = overwrite;

  /* compute total/max sectors. we always keep a rounded number of sectors. */
  ext_info.nsect_total = CEIL_PTVDIV (npages, DISK_SECTOR_NPAGES);
  ext_info.nsect_total = DISK_SECTS_ROUND_UP (ext_info.nsect_total);
  ext_info.nsect_max = ext_info.nsect_total;

  /* extensions are permanent */
  ext_info.voltype = DB_PERMANENT_VOLTYPE;

  /* add volume */
  error_code = disk_add_volume (thread_p, &ext_info, &volid_new, &nsect_free);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      disk_unlock_extend ();
      return error_code;
    }
  assert (volid_new == disk_Cache->nvols_perm);

  /* update cache */
  disk_Cache->nvols_perm++;

  disk_cache_lock_reserve_for_purpose (ext_info.purpose);
  disk_Cache->vols[volid_new].purpose = ext_info.purpose;
  assert (disk_Cache->vols[volid_new].nsect_free == 0);

  disk_cache_update_vol_free (volid_new, nsect_free);
  disk_cache_unlock_reserve_for_purpose (ext_info.purpose);

  /* unblock expand */
  disk_unlock_extend ();

  assert (disk_is_valid_volid (volid_new));

  /* output volume identifier */
  *volid_out = volid_new;

  /* success */
  return NO_ERROR;
}

/*
 * disk_reserve_from_cache_volume () - reserve sectors from cache
 *
 * return           : void
 * volid (in)       : volume identifier
 * nsects (in)      : number of reserved sectors
 * context (in/out) : reserve context
 */
STATIC_INLINE void
disk_reserve_from_cache_volume (VOLID volid, DISK_RESERVE_CONTEXT * context)
{
  DKNSECTS nsects;

  if (context->n_cache_vol_reserve >= LOG_MAX_DBVOLID)
    {
      assert_release (false);
      return;
    }
  disk_check_own_reserve_for_purpose (context->purpose);
  assert (context->n_cache_reserve_remaining > 0);

  nsects = MIN (disk_Cache->vols[volid].nsect_free, context->n_cache_reserve_remaining);
  disk_cache_update_vol_free (volid, -nsects);

  context->cache_vol_reserve[context->n_cache_vol_reserve].volid = volid;
  context->cache_vol_reserve[context->n_cache_vol_reserve].nsect = nsects;
  context->n_cache_vol_reserve++;
  context->n_cache_reserve_remaining -= nsects;
  assert (context->n_cache_reserve_remaining >= 0);
}

/*
 * disk_unreserve_ordered_sectors () - un-reserve given list of sectors from disk volumes. the list must be ordered.
 *
 * return        : error code
 * thread_p (in) : thread entry
 * purpose (in)  : the purpose of reserved sectors
 * nsects (in)   : number of sectors
 * vsids (in)    : array of sectors
 */
int
disk_unreserve_ordered_sectors (THREAD_ENTRY * thread_p, DB_VOLPURPOSE purpose, int nsects, VSID * vsids)
{
  int start_index = 0;
  int end_index = 0;
  int index;
  VOLID volid = NULL_VOLID;
  DISK_RESERVE_CONTEXT context;

  int error_code = NO_ERROR;

  error_code = csect_enter_as_reader (thread_p, CSECT_DISK_CHECK, INF_WAIT);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  context.nsect_total = nsects;

  context.n_cache_vol_reserve = 0;
  context.vsidp = vsids;
  context.purpose = purpose;

  /* note: vsids are ordered */
  for (start_index = 0; start_index < nsects; start_index = end_index)
    {
      assert (volid < vsids[start_index].volid);
      volid = vsids[start_index].volid;
      for (end_index = start_index + 1; end_index < nsects && vsids[end_index].volid == volid; end_index++)
	{
	  assert (vsids[end_index].sectid > vsids[end_index - 1].sectid);
	}
      assert (end_index == nsects);
      context.cache_vol_reserve[context.n_cache_vol_reserve].nsect = end_index - start_index;
      context.cache_vol_reserve[context.n_cache_vol_reserve].volid = volid;
      context.n_cache_vol_reserve++;
    }

  for (index = 0; index < context.n_cache_vol_reserve; index++)
    {
      /* unreserve volume sectors */
      context.nsects_lastvol_remaining = context.cache_vol_reserve[index].nsect;

      error_code = disk_unreserve_sectors_from_volume (thread_p, context.cache_vol_reserve[index].volid, &context);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  csect_exit (thread_p, CSECT_DISK_CHECK);
	  return error_code;
	}
    }

  csect_exit (thread_p, CSECT_DISK_CHECK);

  return NO_ERROR;
}

/*
 * disk_unreserve_sectors_from_volume () - un-reserve sectors indicated in reserve context from volume's sector table
 *
 * return        : error code
 * thread_p (in) : thread entry
 * volid (in)    : volume identifier
 * context (in)  : reserve context
 */
static int
disk_unreserve_sectors_from_volume (THREAD_ENTRY * thread_p, VOLID volid, DISK_RESERVE_CONTEXT * context)
{
  PAGE_PTR page_volheader;
  DISK_VAR_HEADER *volheader = NULL;
  SECTID sectid_start_cursor;
  DISK_STAB_CURSOR start_cursor, end_cursor;

  int error_code = NO_ERROR;

  assert (context != NULL && context->nsects_lastvol_remaining > 0);

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &page_volheader, &volheader);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* un-reserve all given sectors. */

  /* use disk_stab_iterate_units. set starting cursor to first sector rounded down */
  sectid_start_cursor = DISK_SECTS_ROUND_DOWN (context->vsidp->sectid);
  disk_stab_cursor_set_at_sectid (volheader, sectid_start_cursor, &start_cursor);
  disk_stab_cursor_set_at_end (volheader, &end_cursor);
  error_code =
    disk_stab_iterate_units (thread_p, volheader, PGBUF_LATCH_WRITE, &start_cursor, &end_cursor,
			     disk_stab_unit_unreserve, context);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      goto exit;
    }

  /* that's it */
  assert (error_code == NO_ERROR);

exit:
  pgbuf_unfix (thread_p, page_volheader);

  return error_code;
}

/*
 * disk_stab_unit_unreserve () - DISK_STAB_UNIT_FUNC used to un-reserve sectors from sector table
 *
 * return        : NO_ERROR
 * thread_p (in) : thread entry
 * cursor (in)   : sector table cursor
 * stop (in)     : output true when all sectors have been un-reserved
 * args (in)     : reserve context
 */
static int
disk_stab_unit_unreserve (THREAD_ENTRY * thread_p, DISK_STAB_CURSOR * cursor, bool * stop, void *args)
{
  DISK_RESERVE_CONTEXT *context = (DISK_RESERVE_CONTEXT *) args;
  DISK_STAB_UNIT unreserve_bits = 0;
  LOG_DATA_ADDR addr = LOG_DATA_ADDR_INITIALIZER;
  int nsect = 0;

  while (context->nsects_lastvol_remaining > 0 && context->vsidp->sectid < cursor->sectid + DISK_STAB_UNIT_BIT_COUNT)
    {
      unreserve_bits |= (context->vsidp->sectid - cursor->sectid);
      context->nsects_lastvol_remaining--;
      context->vsidp++;
      nsect++;
    }

  /* all bits muse be set */
  assert ((unreserve_bits & (*cursor->unit)) == unreserve_bits);
  if (unreserve_bits != 0)
    {
      if (context->purpose == DB_PERMANENT_DATA_PURPOSE)
	{
	  /* postpone */
	  addr.pgptr = cursor->page;
	  addr.offset = cursor->offset_to_unit;
	  log_append_postpone (thread_p, RVDK_UNRESERVE_SECTORS, &addr, sizeof (unreserve_bits), &unreserve_bits);
	}
      else
	{
	  /* remove immediately */
	  (*cursor->unit) &= ~unreserve_bits;
	  pgbuf_set_dirty (thread_p, cursor->page, DONT_FREE);

	  assert (context->purpose == DB_TEMPORARY_DATA_PURPOSE);
	  assert (nsect > 0);
	  disk_cache_lock_reserve_for_purpose (DB_TEMPORARY_DATA_PURPOSE);
	  disk_cache_update_vol_free (cursor->volheader->volid, nsect);
	  disk_cache_unlock_reserve_for_purpose (DB_TEMPORARY_DATA_PURPOSE);
	}
    }

  if (context->nsects_lastvol_remaining <= 0)
    {
      assert (context->nsects_lastvol_remaining == 0);
      *stop = true;
    }
  return NO_ERROR;
}

/************************************************************************/
/* Disk cache section                                                   */
/************************************************************************/

/*
 * disk_volume_boot () - Boot disk volume.
 *
 * return            : error code
 * thread_p (in)     : thread entry
 * volid (in)        : volume identifier
 * purpose_out (out) : output volume purpose
 * voltype_out (out) : output volume type
 * space_out (out)   : output space information
 */
static int
disk_volume_boot (THREAD_ENTRY * thread_p, VOLID volid, DB_VOLPURPOSE * purpose_out, DB_VOLTYPE * voltype_out,
		  VOL_SPACE_INFO * space_out)
{
  PAGE_PTR page_volheader = NULL;
  DISK_VAR_HEADER *volheader;

  int error_code = NO_ERROR;

  assert (volid != NULL_VOLID);

  error_code = disk_get_volheader (thread_p, volid, PGBUF_LATCH_WRITE, &page_volheader, &volheader);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  *purpose_out = volheader->purpose;
  *voltype_out = volheader->type;

  if (*voltype_out == DB_TEMPORARY_VOLTYPE)
    {
      /* don't load temporary volumes */
      return NO_ERROR;
    }

  /* get space info */
  space_out->n_max_sects = volheader->nsect_max;
  space_out->n_total_sects = volheader->nsect_total;

  if (volheader->purpose == DB_TEMPORARY_DATA_PURPOSE)
    {
      /* reset volume */
      assert (volheader->nsect_max == volheader->nsect_total);
      /* set back sectors used by system */
      error_code = disk_stab_init (thread_p, volheader);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
      space_out->n_free_sects = space_out->n_total_sects - SECTOR_FROM_PAGEID (volheader->sys_lastpage);
    }
  else
    {
      space_out->n_free_sects = 0;
      error_code =
	disk_stab_iterate_units_all (thread_p, volheader, PGBUF_LATCH_READ, disk_stab_count_free,
				     &space_out->n_free_sects);
      if (error_code != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit;
	}
    }

  assert (error_code == NO_ERROR);

exit:

  if (page_volheader != NULL)
    {
      pgbuf_unfix (thread_p, page_volheader);
    }

  return error_code;
}

/*
 * disk_stab_init () - initialize disk sector table
 *
 * return         : error code
 * thread_p (in)  : thread entry
 * volheader (in) : volume header
 */
static int
disk_stab_init (THREAD_ENTRY * thread_p, DISK_VAR_HEADER * volheader)
{
  DKNSECTS nsects_sys = SECTOR_FROM_PAGEID (volheader->sys_lastpage) + 1;
  DKNSECTS nsect_copy = 0;
  VPID vpid_stab;
  PAGE_PTR page_stab = NULL;
  DISK_STAB_CURSOR start_cursor;
  DISK_STAB_CURSOR end_cursor;

  int error_code = NO_ERROR;

  assert (nsects_sys < DISK_STAB_PAGE_BIT_COUNT);

  vpid_stab.volid = volheader->volid;
  for (vpid_stab.pageid = volheader->stab_first_page;
       vpid_stab.pageid < volheader->stab_first_page + volheader->stab_npages; vpid_stab.pageid++)
    {
      page_stab = pgbuf_fix (thread_p, &vpid_stab, NEW_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (page_stab == NULL)
	{
	  ASSERT_ERROR_AND_SET (error_code);
	  return error_code;
	}

      pgbuf_set_page_ptype (thread_p, page_stab, PAGE_VOLBITMAP);

      if (volheader->purpose == DB_TEMPORARY_DATA_PURPOSE)
	{
	  /* why is this necessary? */
	  pgbuf_set_lsa_as_temporary (thread_p, page_stab);
	}

      memset (page_stab, 0, DB_PAGESIZE);

      if (nsects_sys > 0)
	{
	  nsect_copy = nsects_sys;

	  disk_stab_cursor_set_at_sectid (volheader,
					  (vpid_stab.pageid - volheader->stab_first_page) * DISK_STAB_PAGE_BIT_COUNT,
					  &start_cursor);
	  if (vpid_stab.pageid == volheader->stab_first_page + volheader->stab_npages - 1)
	    {
	      disk_stab_cursor_set_at_end (volheader, &end_cursor);
	    }
	  else
	    {
	      disk_stab_cursor_set_at_sectid (volheader,
					      (vpid_stab.pageid + 1 - volheader->stab_first_page)
					      * DISK_STAB_PAGE_BIT_COUNT, &end_cursor);
	    }
	  error_code =
	    disk_stab_iterate_units (thread_p, volheader, PGBUF_LATCH_WRITE, &start_cursor, &end_cursor,
				     disk_stab_set_bits_contiguous, &nsect_copy);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      pgbuf_unfix (thread_p, page_stab);
	      return NO_ERROR;
	    }
	}

      if (volheader->purpose != DB_TEMPORARY_DATA_PURPOSE)
	{
	  log_append_redo_data2 (thread_p, RVDK_INITMAP, NULL, page_stab, NULL_OFFSET, sizeof (nsects_sys),
				 &nsects_sys);
	  nsects_sys = 0;
	}
      pgbuf_set_dirty_and_free (thread_p, page_stab);

      nsects_sys -= nsect_copy;
      nsect_copy = 0;
    }
  return NO_ERROR;
}

int
disk_manager_init (THREAD_ENTRY * thread_p, bool load_from_disk)
{
  int error_code = NO_ERROR;

  disk_Temp_max_sects = (DKNSECTS) prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES);
  if (disk_Temp_max_sects < 0)
    {
      disk_Temp_max_sects = SECTID_MAX;	/* infinite */
    }
  else
    {
      disk_Temp_max_sects = disk_Temp_max_sects / DISK_SECTOR_NPAGES;
    }

  error_code = disk_cache_init ();
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  assert (disk_Cache != NULL);

  if (load_from_disk && !disk_cache_load_all_volumes (thread_p))
    {
      ASSERT_ERROR_AND_SET (error_code);
      disk_manager_final ();
      return error_code;
    }
  return NO_ERROR;
}

void
disk_manager_final (void)
{
  if (disk_Cache == NULL)
    {
      /* not initialized */
      return;
    }

  assert (disk_Cache->perm_purpose_info.extend_info.owner_reserve == -1);
  assert (disk_Cache->temp_purpose_info.extend_info.owner_reserve == -1);
  assert (disk_Cache->owner_extend == -1);

  pthread_mutex_destroy (&disk_Cache->perm_purpose_info.extend_info.mutex_reserve);
  pthread_mutex_destroy (&disk_Cache->temp_purpose_info.extend_info.mutex_reserve);
  pthread_mutex_destroy (&disk_Cache->mutex_extend);

  free_and_init (disk_Cache);
}

/*
 * disk_format_first_volume () - format first database volume
 *
 * return           : error code
 * thread_p (in)    : thread entry
 * full_dbname (in) : database full name
 * dbcomments (in)  : database comments
 * npages (in)      : desired number of pages
 *                    todo: replace with number of sectors or disk size
 *
 * NOTE: disk manager is also initialized
 */
int
disk_format_first_volume (THREAD_ENTRY * thread_p, const char *full_dbname, const char *dbcomments, DKNPAGES npages)
{
  int error_code = NO_ERROR;
  DBDEF_VOL_EXT_INFO ext_info;
  DKNSECTS nsect_free = 0;

  error_code = disk_manager_init (thread_p, false);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  ext_info.name = full_dbname;
  ext_info.comments = dbcomments;
  ext_info.nsect_total = CEIL_PTVDIV (npages, DISK_SECTOR_NPAGES);
  ext_info.nsect_total = DISK_SECTS_ROUND_UP (ext_info.nsect_total);
  ext_info.nsect_max = ext_info.nsect_total;
  ext_info.max_writesize_in_sec = 0;
  ext_info.overwrite = false;
  ext_info.purpose = DB_PERMANENT_DATA_PURPOSE;
  ext_info.voltype = DB_PERMANENT_VOLTYPE;

  error_code = disk_format (thread_p, full_dbname, LOG_DBFIRST_VOLID, &ext_info, &nsect_free);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  disk_Cache->vols[LOG_DBFIRST_VOLID].purpose = DB_PERMANENT_DATA_PURPOSE;
  disk_Cache->vols[LOG_DBFIRST_VOLID].nsect_free = nsect_free;
  disk_Cache->perm_purpose_info.extend_info.nsect_free = nsect_free;
  disk_Cache->perm_purpose_info.extend_info.nsect_total = ext_info.nsect_total;
  disk_Cache->perm_purpose_info.extend_info.nsect_max = ext_info.nsect_max;

  disk_Cache->nvols_perm = 1;
  return NO_ERROR;
}

void
disk_lock_extend ()
{
#if defined (NDEBUG)
  pthread_mutex_lock (&disk_Cache->mutex_extend);
#else /* !NDEBUG */
  int me = thread_get_current_entry_index ();

  assert (me != disk_Cache->perm_purpose_info.extend_info.owner_reserve);
  assert (me != disk_Cache->temp_purpose_info.extend_info.owner_reserve);
  if (me == disk_Cache->owner_extend)
    {
      /* already owner */
      assert (false);
      return;
    }

  pthread_mutex_lock (&disk_Cache->mutex_extend);
  assert (disk_Cache->owner_extend == -1);
  disk_Cache->owner_extend = me;
#endif /* !NDEBUG */
}

void
disk_unlock_extend ()
{
#if defined (NDEBUG)
#else /* !NDEBUG */
  int me = thread_get_current_entry_index ();

  assert (disk_Cache->owner_extend == me);
  disk_Cache->owner_extend = -1;
  pthread_mutex_unlock (&disk_Cache->mutex_extend);
#endif /* !NDEBUG */
}

STATIC_INLINE void
disk_cache_lock_reserve_for_purpose (DB_VOLPURPOSE purpose)
{
  if (purpose == DB_PERMANENT_DATA_PURPOSE)
    {
      disk_cache_lock_reserve (&disk_Cache->perm_purpose_info.extend_info);
    }
  else
    {
      disk_cache_lock_reserve (&disk_Cache->temp_purpose_info.extend_info);
    }
}

STATIC_INLINE void
disk_cache_unlock_reserve_for_purpose (DB_VOLPURPOSE purpose)
{
  if (purpose == DB_PERMANENT_DATA_PURPOSE)
    {
      disk_cache_unlock_reserve (&disk_Cache->perm_purpose_info.extend_info);
    }
  else
    {
      disk_cache_unlock_reserve (&disk_Cache->temp_purpose_info.extend_info);
    }
}

STATIC_INLINE void
disk_cache_lock_reserve (DISK_EXTEND_INFO * extend_info)
{
#if defined (NDEBUG)
#else /* !NDEBUG */
  int me = thread_get_current_entry_index ();

  if (me == extend_info->owner_reserve)
    {
      /* already owner */
      assert (false);
      return;
    }
  pthread_mutex_lock (&extend_info->mutex_reserve);
  assert (extend_info->owner_reserve == -1);
  extend_info->owner_reserve = me;
#endif /* !NDEBUG */
}

STATIC_INLINE void
disk_cache_unlock_reserve (DISK_EXTEND_INFO * extend_info)
{
#if defined (NDEBUG)
#else /* !NDEBUG */
  int me = thread_get_current_entry_index ();

  assert (me == extend_info->owner_reserve);
  extend_info->owner_reserve = -1;
  pthread_mutex_unlock (&extend_info->mutex_reserve);
#endif /* !NDEBUG */
}

#if defined (SERVER_MODE)
int
disk_auto_expand (THREAD_ENTRY * thread_p)
{
  int error_code = NO_ERROR;

  /* todo: we cannot expand the volumes unless we have a transaction descriptor. we might allocate a special tdes for
   *       auto-volume expansion thread, similar to how vacuum works. otherwise, it can be limited to extend last
   *       volume only.
   * for now, do nothing. we'll think about it later.
   */

  return error_code;
}
#endif /* SERVER_MODE */

STATIC_INLINE int
disk_get_volheader (THREAD_ENTRY * thread_p, VOLID volid, PGBUF_LATCH_MODE latch_mode, PAGE_PTR * page_volheader_out,
		    DISK_VAR_HEADER ** volheader_out)
{
  VPID vpid_volheader;
  int error_code = NO_ERROR;

  vpid_volheader.volid = volid;
  vpid_volheader.pageid = DISK_VOLHEADER_PAGE;

  *page_volheader_out = pgbuf_fix (thread_p, &vpid_volheader, OLD_PAGE, latch_mode, PGBUF_UNCONDITIONAL_LATCH);
  if (*page_volheader_out == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }
  disk_verify_volume_header (thread_p, *page_volheader_out);
  *volheader_out = (DISK_VAR_HEADER *) (*page_volheader_out);
  return NO_ERROR;
}

/*
 * disk_cache_free_reserved () - add reserved sectors to cache free sectors
 *
 * return       : void
 * context (in) : reserve context
 */
STATIC_INLINE void
disk_cache_free_reserved (DISK_RESERVE_CONTEXT * context)
{
  int iter;
  disk_cache_lock_reserve_for_purpose (context->purpose);
  for (iter = 0; iter < context->n_cache_vol_reserve; iter++)
    {
      disk_cache_update_vol_free (context->cache_vol_reserve[iter].volid, context->cache_vol_reserve[iter].nsect);
    }
  disk_cache_unlock_reserve_for_purpose (context->purpose);
}

/*
 * disk_cache_update_vol_free () - update number of volume free sectors in cache
 *
 * return          : void
 * volid (in)      : volume identifier
 * delta_free (in) : delta free sectors
 */
STATIC_INLINE void
disk_cache_update_vol_free (VOLID volid, DKNSECTS delta_free)
{
  /* must be locked */
  disk_Cache->vols[volid].nsect_free += delta_free;
  assert (disk_Cache->vols[volid].nsect_free >= 0);
  disk_check_own_reserve_for_purpose (disk_Cache->vols[volid].purpose);
  if (disk_Cache->vols[volid].purpose == DB_PERMANENT_DATA_PURPOSE)
    {
      assert (disk_get_voltype (volid) == DB_PERMANENT_VOLTYPE);
      disk_Cache->perm_purpose_info.extend_info.nsect_free += delta_free;
      assert (disk_Cache->perm_purpose_info.extend_info.nsect_free >= 0);
    }
  else
    {
      if (disk_get_voltype (volid) == DB_PERMANENT_VOLTYPE)
	{
	  disk_Cache->temp_purpose_info.nsect_perm_free += delta_free;
	  assert (disk_Cache->temp_purpose_info.nsect_perm_free >= 0);
	}
      else
	{
	  disk_Cache->temp_purpose_info.extend_info.nsect_free += delta_free;
	  assert (disk_Cache->temp_purpose_info.extend_info.nsect_free >= 0);
	}
    }
}

/*
 * disk_is_valid_volid () - is volume identifier valid? (permanent or temporary)
 *
 * return     : true if volume id is valid, false otherwise
 * volid (in) : volume identifier
 */
STATIC_INLINE bool
disk_is_valid_volid (VOLID volid)
{
  return volid < disk_Cache->nvols_perm || volid > LOG_MAX_DBVOLID - disk_Cache->nvols_temp;
}

/*
 * disk_get_volpurpose () - get volume purpose
 *
 * return     : volume purpose
 * volid (in) : volume identifier
 */
STATIC_INLINE DB_VOLPURPOSE
disk_get_volpurpose (VOLID volid)
{
  assert (disk_Cache != NULL);
  assert (disk_is_valid_volid (volid));
  return disk_Cache->vols[volid].purpose;
}

/*
 * disk_get_voltype () - get volume type
 *
 * return     : permanent/temporary volume type
 * volid (in) : volume identifier
 */
STATIC_INLINE DB_VOLTYPE
disk_get_voltype (VOLID volid)
{
  assert (disk_Cache != NULL);
  assert (disk_is_valid_volid (volid));
  return volid < disk_Cache->nvols_perm ? DB_PERMANENT_VOLTYPE : DB_TEMPORARY_VOLTYPE;
}

/*
 * disk_compatible_type_and_purpose () - is volume purpose compatible to volume type?
 *
 * return       : true if compatible, false otherwise
 * type (in)    : volume type
 * purpose (in) : volume purpose
 */
STATIC_INLINE bool
disk_compatible_type_and_purpose (DB_VOLTYPE type, DB_VOLPURPOSE purpose)
{
  /* temporary type with permanent purpose is not compatible */
  return type == DB_PERMANENT_VOLTYPE || purpose == DB_TEMPORARY_VOLTYPE;
}

/*
 * disk_check_own_reserve_for_purpose () - check current thread owns reserve mutex (based on purpose)
 *
 * return       : true if thread owns mutex, false otherwise
 * purpose (in) : volume purpose
 */
STATIC_INLINE void
disk_check_own_reserve_for_purpose (DB_VOLPURPOSE purpose)
{
  assert (thread_get_current_entry_index ()
	  == ((purpose == DB_PERMANENT_DATA_PURPOSE) ?
	      disk_Cache->perm_purpose_info.extend_info.owner_reserve :
	      disk_Cache->temp_purpose_info.extend_info.owner_reserve));
}
