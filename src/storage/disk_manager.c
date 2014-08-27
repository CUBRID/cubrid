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

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

#define DISK_MIN_NPAGES_TO_TRUNCATE 100

#define DISK_HINT_START_SECT     4

/* do not use assert_release () for performance risk */
#define DISK_VERIFY_VAR_HEADER(h) 					\
  do { 									\
    if (BO_IS_SERVER_RESTARTED ())					\
      {									\
	assert ((h)->total_pages > 0);					\
	assert ((h)->free_pages >= 0);					\
	assert ((h)->free_pages <= (h)->total_pages);			\
									\
	assert ((h)->total_sects > 0);					\
	assert ((h)->free_sects >= 0);					\
	assert ((h)->free_sects <= (h)->total_sects);			\
									\
	assert ((h)->sect_npgs == DISK_SECTOR_NPAGES);			\
	assert ((h)->total_sects == 					\
		CEIL_PTVDIV ((h)->total_pages, (h)->sect_npgs));	\
	assert ((h)->sect_alloctb_page1 == DISK_VOLHEADER_PAGE + 1);	\
									\
	assert ((h)->page_alloctb_page1 == 				\
		((h)->sect_alloctb_page1 + (h)->sect_alloctb_npages));	\
	assert ((h)->sys_lastpage == 					\
		((h)->page_alloctb_page1 + (h)->page_alloctb_npages - 1));\
   									\
        if ((h)->purpose != DISK_TEMPVOL_TEMP_PURPOSE)                  \
          {                                                             \
            assert ((h)->sect_alloctb_npages >= 			\
                    CEIL_PTVDIV ((h)->total_sects, DISK_PAGE_BIT));	\
            assert ((h)->page_alloctb_npages >= 			\
                    CEIL_PTVDIV ((h)->total_pages, DISK_PAGE_BIT));	\
          }								\
        if ((h)->purpose != DISK_PERMVOL_GENERIC_PURPOSE                \
            && (h)->purpose != DISK_TEMPVOL_TEMP_PURPOSE)               \
          {                                                             \
            assert ((h)->total_pages == (h)->max_npages);               \
          }                                                             \
      }									\
  } while (0)

#define DISK_PAGE   1
#define DISK_SECTOR 0

#define DISK_EXPAND_TMPVOL_INCREMENTS 1000

typedef enum
{
  DISK_ALLOCTABLE_SET,
  DISK_ALLOCTABLE_CLEAR
} DISK_ALLOCTABLE_MODE;

typedef struct disk_recv_mtab_bits DISK_RECV_MTAB_BITS;
struct disk_recv_mtab_bits
{				/* Recovery for allocation table */
  unsigned int start_bit;	/* Start bit */
  INT32 num;			/* Number of bits */
};

typedef struct disk_recv_mtab_bits_with DISK_RECV_MTAB_BITS_WITH;
struct disk_recv_mtab_bits_with
{				/* Recovery for allocation table */
  unsigned int start_bit;	/* Start bit */
  INT32 num;			/* Number of bits */
  int deallid_type;		/* Deallocation identifier - sector or page */
  DISK_PAGE_TYPE page_type;	/* page type */
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
  bool exists;			/* weather volid does exist */
};

/* Cache of volumes with their purposes and hint of num of free pages */

typedef struct disk_volfreepgs DISK_VOLFREEPGS;
struct disk_volfreepgs
{
  INT16 volid;			/* Volume identifier */
  int hint_freepages;		/* Hint of free pages on volume */
};

typedef struct disk_cache_volinfo DISK_CACHE_VOLINFO;
struct disk_cache_volinfo
{
  INT16 max_nvols;		/* Max size supported in the array */
  INT16 nvols;			/* Total number of permanent volumes */
  struct
  {
#if !defined(HAVE_ATOMIC_BUILTINS) && defined(SERVER_MODE)
    pthread_mutex_t update_lock;	/* Protect below variables from concurrent updates */
#endif
    INT16 nvols;		/* Number of volumes for this purpose */
    int total_pages;		/* Number of total pages for this purpose */
    int free_pages;		/* Number of free pages for this purpose */
  } purpose[DISK_UNKNOWN_PURPOSE];
  DISK_VOLFREEPGS *vols;	/* Really a pointer to more than one */
  VOLID auto_extend_volid;
};


static DISK_CACHE_VOLINFO disk_Cache_struct = {
  0, 0,
  {
#if !defined(HAVE_ATOMIC_BUILTINS) && defined(SERVER_MODE)
   {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0},
   {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0},
   {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0},
   {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0},
   {PTHREAD_MUTEX_INITIALIZER, 0, 0, 0}
#else
   {0, 0, 0},
   {0, 0, 0},
   {0, 0, 0},
   {0, 0, 0},
   {0, 0, 0}
#endif
   },
  NULL,
  NULL_VOLID
};

static DISK_CACHE_VOLINFO *disk_Cache = &disk_Cache_struct;

static char *disk_vhdr_get_vol_fullname (const DISK_VAR_HEADER * vhdr);
static char *disk_vhdr_get_next_vol_fullname (const DISK_VAR_HEADER * vhdr);
static char *disk_vhdr_get_vol_remarks (const DISK_VAR_HEADER * vhdr);
static int disk_vhdr_length_of_varfields (const DISK_VAR_HEADER * vhdr);
static int disk_vhdr_set_vol_fullname (DISK_VAR_HEADER * vhdr,
				       const char *vol_fullname);
static int disk_vhdr_set_next_vol_fullname (DISK_VAR_HEADER * vhdr,
					    const char *next_vol_fullname);
static int disk_vhdr_set_vol_remarks (DISK_VAR_HEADER * vhdr,
				      const char *vol_remarks);

static void disk_bit_set (unsigned char *c, unsigned int n);
static void disk_bit_clear (unsigned char *c, unsigned int n);
static bool disk_bit_is_set (unsigned char *c, unsigned int n);
#if defined (ENABLE_UNUSED_FUNCTION)
static bool disk_bit_is_cleared (unsigned char *c, unsigned int n);
#endif

static bool disk_cache_goodvol_refresh_onevol (THREAD_ENTRY * thread_p,
					       INT16 volid, void *ignore);
static int disk_cache_goodvol_update (THREAD_ENTRY * thread_p, INT16 volid,
				      DISK_VOLPURPOSE vol_purpose,
				      INT32 num_pages, bool do_update_total,
				      bool * need_to_add_generic_volume);
#if defined (ENABLE_UNUSED_FUNCTION)
static INT32 disk_vhdr_get_last_alloc_pageid (THREAD_ENTRY * thread_p,
					      DISK_VAR_HEADER * vhdr,
					      INT32 old_lpageid);
#endif /* ENABLE_UNUSED_FUNCTION */
static INT16 disk_probe_disk_cache_to_find_desirable_vol (THREAD_ENTRY *
							  thread_p,
							  INT16 * best_volid,
							  INT32 *
							  best_numpages,
							  INT16
							  undesirable_volid,
							  INT32 exp_numpages,
							  int start_at,
							  int num_vols);
static VOLID disk_find_goodvol_from_disk_cache (THREAD_ENTRY * thread_p,
						INT16 hint_volid,
						INT16 undesirable_volid,
						INT32 exp_numpages,
						DISK_SETPAGE_TYPE
						setpage_type,
						DISK_VOLPURPOSE vol_purpose);

static INT16 disk_cache_get_purpose_info (THREAD_ENTRY * thread_p,
					  DISK_VOLPURPOSE vol_purpose,
					  INT16 * nvols, int *total_pages,
					  int *free_pages);
static const char *disk_purpose_to_string (DISK_VOLPURPOSE purpose);

static bool disk_reinit (THREAD_ENTRY * thread_p, INT16 volid, void *ignore);

static int disk_map_init (THREAD_ENTRY * thread_p, INT16 volid,
			  INT32 at_fpageid, INT32 at_lpageid,
			  INT32 nalloc_bits, DISK_VOLPURPOSE vol_purpose);
static int disk_map_dump (THREAD_ENTRY * thread_p, FILE * fp, VPID * vpid,
			  const char *at_name, INT32 at_fpageid,
			  INT32 at_lpageid, INT32 all_fid, INT32 all_lid);
static const char *disk_page_type_to_string (DISK_PAGE_TYPE page_type);

#if defined(CUBRID_DEBUG)
static void disk_scramble_newpages (INT16 volid, INT32 first_pageid,
				    INT32 npages,
				    DISK_VOLPURPOSE vol_purpose);
#endif /* CUBRID_DEBUG */
static bool disk_get_hint_contiguous_free_numpages (THREAD_ENTRY * thread_p,
						    INT16 volid,
						    INT32
						    arecontiguous_npages,
						    INT32 * num_freepgs);

static bool disk_check_sector_has_npages (THREAD_ENTRY * thread_p,
					  INT16 volid, INT32 at_pg1,
					  INT32 low_allid, INT32 high_allid,
					  int exp_npages);
static INT32 disk_id_alloc (THREAD_ENTRY * thread_p, INT16 volid,
			    DISK_VAR_HEADER * vhdr, INT32 nalloc,
			    INT32 low_allid, INT32 high_allid,
			    int allid_type, int exp_npages, int skip_pageid);
static int disk_id_dealloc (THREAD_ENTRY * thread_p, INT16 volid,
			    INT32 at_pg1, INT32 deallid, INT32 ndealloc,
			    int deallid_type, DISK_PAGE_TYPE page_type);
static INT32 disk_id_get_max_contiguous (THREAD_ENTRY * thread_p, INT16 volid,
					 INT32 at_pg1, INT32 low_allid,
					 INT32 high_allid, INT32 nunits_quit);
static INT32 disk_id_get_max_frees (THREAD_ENTRY * thread_p, INT16 volid,
				    INT32 at_pg1, INT32 low_allid,
				    INT32 high_allid);
static DISK_ISVALID disk_id_isvalid (THREAD_ENTRY * thread_p, INT16 volid,
				     INT32 at_pg1, INT32 allid);

static int disk_repair (THREAD_ENTRY * thread_p, INT16 volid, int dk_type);

static int disk_dump_goodvol_system (THREAD_ENTRY * thread_p, FILE * fp,
				     INT16 volid, INT32 fs_sectid,
				     INT32 ls_sectid, INT32 fs_pageid,
				     INT32 ls_pageid);
static bool disk_dump_goodvol_all (THREAD_ENTRY * thread_p, INT16 volid,
				   void *ignore);
static int disk_vhdr_dump (FILE * fp, const DISK_VAR_HEADER * vhdr);

static int disk_rv_alloctable_helper (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				      DISK_ALLOCTABLE_MODE mode);
static void disk_set_page_to_zeros (THREAD_ENTRY * thread_p, PAGE_PTR pgptr);

static void disk_verify_volume_header (THREAD_ENTRY * thread_p,
				       PAGE_PTR pgptr);
static bool disk_check_volume_exist (THREAD_ENTRY * thread_p, VOLID volid,
				     void *arg);

static char *
disk_vhdr_get_vol_fullname (const DISK_VAR_HEADER * vhdr)
{
  return ((char *) (vhdr->var_fields + vhdr->offset_to_vol_fullname));
}

static char *
disk_vhdr_get_next_vol_fullname (const DISK_VAR_HEADER * vhdr)
{
  return ((char *) (vhdr->var_fields + vhdr->offset_to_next_vol_fullname));
}

static char *
disk_vhdr_get_vol_remarks (const DISK_VAR_HEADER * vhdr)
{
  return ((char *) (vhdr->var_fields + vhdr->offset_to_vol_remarks));
}

static int
disk_vhdr_length_of_varfields (const DISK_VAR_HEADER * vhdr)
{
  return (vhdr->offset_to_vol_remarks +
	  (int) strlen (disk_vhdr_get_vol_remarks (vhdr)));
}

/*
 * disk_bit_set () - Set N-th bit of given byte
 *   return: none
 *   c(in): byte
 *   n(in): N-th
 *
 * Note: Bits are numbered from 0 through 7 from right to left
 */
static void
disk_bit_set (unsigned char *c, unsigned int n)
{
  assert_release (!disk_bit_is_set (c, n));

  *c |= (1 << n);
}

/*
 * disk_bit_clear () - Clear N-th bit of given byte
 *   return: none
 *   c(in): byte
 *   n(in): N-th
 *
 * Note: Bits are numbered from 0 through 7 from right to left
 */
static void
disk_bit_clear (unsigned char *c, unsigned int n)
{
  /* TODO: Uncomment and investigate the double clearing issue. As far as I am
   *       concerned, this shouldn't happen due to last changes of vacuum.
   *       Maybe a bugged case is uncovered.
   *       Investigate after fixing current crashes.
   */
  /* assert_release (disk_bit_is_set (c, n)); */

  *c &= ~(1 << n);
}

/*
 * disk_bit_is_set () - Check N-th bit of given byte. Is the bit set ?
 *   return: true/false
 *   c(in): byte
 *   n(in): N-th
 *
 * Note: Bits are numbered from 0 through 7 from right to left
 */
static bool
disk_bit_is_set (unsigned char *c, unsigned int n)
{
  return (*c & (1 << n)) ? true : false;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * disk_bit_is_cleared () - Check N-th bit of given byte. Is the bit cleared ?
 *   return: true/false
 *   c(in): byte
 *   n(in): N-th
 *
 * Note: Bits are numbered from 0 through 7 from right to left
 */
static bool
disk_bit_is_cleared (unsigned char *c, unsigned int n)
{
  return !disk_bit_is_set (c, n);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/* TODO: check not use */
//#if 0
//static int dk_get_first_free (unsigned char *ch);
///*
// * dk_get_first_free () - Returns the first free bit of ch byte
// *   return: the first free bit
// *   ch(in): byte
// */
//static int
//dk_get_first_free (unsigned char *ch)
//{
//  register unsigned int bit, mask;
//
//  for (bit = 0, mask = 1; bit < CHAR_BIT; mask <<= 1, bit++)
//    if (!(*ch & mask))
//      return (int) bit;
//  return -1;
//}
//#endif

/* Caching of multivolume information */

/* TODO: STL::list for disk_Cache->vols */
/*
 * disk_goodvol_decache () - Decache information about volumes
 *   return: NO_ERROR
 */
int
disk_goodvol_decache (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;

  /*
   * We do not check access to the cache, we will remove it.
   * This function is called only during shutdown time
   */
  if (csect_enter (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT) ==
      NO_ERROR)
    {
      if (disk_Cache->max_nvols > 0)
	{
	  free_and_init (disk_Cache->vols);
	  disk_Cache->max_nvols = 0;
	}
      csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);
    }

  return ret;
}

/* TODO: STL::vector for disk_Cache->purpose */
/* TODO: STL::list for disk_Cache->vols */
/*
 * disk_cache_goodvol_refresh_onevol () - Cache specific volume information
 *   return:
 *   volid(in):
 *   ignore(in):
 */
static bool
disk_cache_goodvol_refresh_onevol (THREAD_ENTRY * thread_p, INT16 volid,
				   void *ignore)
{
  void *ptr;
  DISK_VOLPURPOSE vol_purpose;
  VOL_SPACE_INFO space_info;
  INT16 vol_index;
  int i;

  if ((disk_Cache->nvols + 1) > disk_Cache->max_nvols)
    {
      /* Increase volumes by 10 */
      vol_index = disk_Cache->max_nvols + 10;
      ptr = realloc (disk_Cache->vols, sizeof (DISK_VOLFREEPGS) * vol_index);
      if (ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (DISK_VOLFREEPGS) * vol_index);
	  return false;
	}
      disk_Cache->vols = (DISK_VOLFREEPGS *) ptr;
      disk_Cache->max_nvols = vol_index;
    }

  if (xdisk_get_purpose_and_space_info (thread_p, volid,
					&vol_purpose, &space_info) == volid)
    {
      switch (vol_purpose)
	{
	case DISK_PERMVOL_DATA_PURPOSE:
	  vol_index = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
	  break;

	case DISK_PERMVOL_INDEX_PURPOSE:
	  vol_index =
	    (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
	     disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols);
	  break;

	case DISK_PERMVOL_GENERIC_PURPOSE:
	  vol_index = (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
		       disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols +
		       disk_Cache->
		       purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols);
	  break;

	case DISK_PERMVOL_TEMP_PURPOSE:
	  vol_index = (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
		       disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols +
		       disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].
		       nvols +
		       disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].nvols);
	  break;

	case DISK_TEMPVOL_TEMP_PURPOSE:
	  vol_index = disk_Cache->nvols;
	  break;

	case DISK_UNKNOWN_PURPOSE:
	case DISK_EITHER_TEMP_PURPOSE:
	default:
	  /* We do not cache this kind of volume */
	  return true;
	}

      /* Make the space for the volume when needed. That is, shift the
         volumes */
      if (disk_Cache->nvols != disk_Cache->purpose[vol_purpose].nvols)
	{
	  /* shift the information */
	  for (i = disk_Cache->nvols; i > vol_index; i--)
	    {
	      disk_Cache->vols[i] = disk_Cache->vols[i - 1];
	    }
	}

      disk_Cache->vols[vol_index].volid = volid;
      disk_Cache->vols[vol_index].hint_freepages = space_info.free_pages;

      disk_Cache->nvols++;
      disk_Cache->purpose[vol_purpose].nvols++;
      disk_Cache->purpose[vol_purpose].total_pages += space_info.total_pages;
      disk_Cache->purpose[vol_purpose].free_pages += space_info.free_pages;
      if (disk_Cache->purpose[vol_purpose].free_pages < 0)
	{
	  assert (false);
	  disk_Cache->purpose[vol_purpose].free_pages = 0;
	}

      if (vol_purpose == DISK_PERMVOL_GENERIC_PURPOSE
	  && space_info.total_pages < space_info.max_pages)
	{
	  assert (csect_check_own (thread_p,
				   CSECT_DISK_REFRESH_GOODVOL) == 1);
	  disk_Cache->auto_extend_volid = volid;
	}
    }

  return true;
}

/* TODO: STL::list for disk_Cache->vols */
/*
 * disk_goodvol_refresh_with_new () -
 *   return:
 *   volid(in):
 */
bool
disk_goodvol_refresh_with_new (THREAD_ENTRY * thread_p, INT16 volid)
{
  bool answer;

  /*
   * using csect_enter() for the safety instead of an assert()
   * assert (csect_check_own (thread_p, CSECT_BOOT_SR_DBPARM) == 1)
   */
  if (csect_enter (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) != NO_ERROR)
    {
      return false;
    }

  if (csect_enter (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT) !=
      NO_ERROR)
    {
      csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);
      return false;
    }

  if (disk_Cache->vols == NULL || disk_Cache->max_nvols <= 0)
    {
      answer = disk_goodvol_refresh (thread_p, (int) volid);
    }
  else
    {
      answer = disk_cache_goodvol_refresh_onevol (thread_p, volid, NULL);
    }

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return answer;
}

/* TODO: STL::vector for disk_Cache->purpose */
/* TODO: STL::list for disk_Cache->vols */
/*
 * disk_goodvol_refresh () - Refresh cached information about volumes
 *   return:
 *   hint_max_nvols(in):
 */
bool
disk_goodvol_refresh (THREAD_ENTRY * thread_p, int hint_max_nvols)
{
  void *ptr;
  DISK_VOLPURPOSE vol_purpose;
  bool answer = false;

  if (hint_max_nvols < 10)
    {
      hint_max_nvols = 10;
    }

  if (csect_enter (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT) !=
      NO_ERROR)
    {
      return false;
    }

  /* Make sure that nobody is accessing the cache when refreshing. */
  if (disk_Cache->vols == NULL || hint_max_nvols < disk_Cache->max_nvols)
    {
      ptr = realloc (disk_Cache->vols,
		     sizeof (DISK_VOLFREEPGS) * hint_max_nvols);
      if (ptr == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (DISK_VOLFREEPGS) * hint_max_nvols);
	  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);
	  return false;
	}
      disk_Cache->vols = (DISK_VOLFREEPGS *) ptr;
      disk_Cache->max_nvols = hint_max_nvols;
    }

  disk_Cache->nvols = 0;
  disk_Cache->auto_extend_volid = NULL_VOLID;

  /* Initialize the volume information purpose */
  for (vol_purpose = DISK_PERMVOL_DATA_PURPOSE;
       vol_purpose < DISK_UNKNOWN_PURPOSE;
       vol_purpose = (DB_VOLPURPOSE) (vol_purpose + 1))
    {
      disk_Cache->purpose[vol_purpose].nvols = 0;
      disk_Cache->purpose[vol_purpose].total_pages = 0;
      disk_Cache->purpose[vol_purpose].free_pages = 0;
    }

  /* Cache every single volume */
  answer =
    fileio_map_mounted (thread_p, disk_cache_goodvol_refresh_onevol, NULL);

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);

  return answer;
}

/*
 * disk_cache_get_auto_extend_volid () -
 *   return:
 */
VOLID
disk_cache_get_auto_extend_volid (THREAD_ENTRY * thread_p)
{
  VOLID ret_volid = NULL_VOLID;

  if (csect_enter_as_reader (thread_p,
			     CSECT_DISK_REFRESH_GOODVOL,
			     INF_WAIT) == NO_ERROR)
    {
      ret_volid = disk_Cache->auto_extend_volid;
      csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);
    }

  return ret_volid;
}

/*
 * disk_cache_set_auto_extend_volid () -
 *   return:
 */
int
disk_cache_set_auto_extend_volid (THREAD_ENTRY * thread_p, VOLID volid)
{
  if (csect_enter (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  disk_Cache->auto_extend_volid = volid;

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);

  return NO_ERROR;
}

/* TODO: STL::vector for disk_Cache->purpose */
/* TODO: STL::list for disk_Cache->vols */
/*
 * disk_cache_goodvol_update () - Update the free pages cache of volume purpose
 *                              and give a space warning when appropiate
 *   return: NO_ERROR
 *   volid(in): Volume identifier
 *   vol_purpose(in): The main purpose of the volume
 *   nfree_pages_toadd(in): Number of allocated or deallocated pages Negative
 *                          for deallocated and positive for allocated
 *   do_update_total(in): Flag is true if total_pages should be updated
 *                        on purpose information
 *   need_to_add_generic_volume(out):
 */
static int
disk_cache_goodvol_update (THREAD_ENTRY * thread_p, INT16 volid,
			   DISK_VOLPURPOSE vol_purpose,
			   INT32 nfree_pages_toadd, bool do_update_total,
			   bool * need_to_add_generic_volume)
{
  int start_at = -1;
  int end_at = -1;
  int i;
#if !defined(HAVE_ATOMIC_BUILTINS)
  int rv;
#endif

#if defined (SERVER_MODE)
  int total_free_pages = 0;
  bool need_to_check_auto_volume_ext = false;
#endif

  if (log_is_in_crash_recovery ())
    {
      /*
       * Do not update disk cache while restart recovery is doing.
       * Disk cache will be built after it. So, updating is unnecessary now.
       */
      return NO_ERROR;
    }

#if defined (SERVER_MODE)
  if (nfree_pages_toadd < 0 && need_to_add_generic_volume
      && prm_get_bigint_value (PRM_ID_GENERIC_VOL_PREALLOC_SIZE) > 0
      && (vol_purpose == DISK_PERMVOL_DATA_PURPOSE
	  || vol_purpose == DISK_PERMVOL_INDEX_PURPOSE
	  || vol_purpose == DISK_PERMVOL_GENERIC_PURPOSE))
    {
      /* we need to check whether generic volume auto extension is needed */
      need_to_check_auto_volume_ext = true;
    }
#endif

  if (csect_enter_as_reader (thread_p, CSECT_DISK_REFRESH_GOODVOL,
			     INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  switch (vol_purpose)
    {
    case DISK_PERMVOL_DATA_PURPOSE:
      start_at = 0;
      end_at = start_at + disk_Cache->purpose[vol_purpose].nvols;
      break;

    case DISK_PERMVOL_INDEX_PURPOSE:
      start_at = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      end_at = start_at + disk_Cache->purpose[vol_purpose].nvols;
      break;

    case DISK_PERMVOL_GENERIC_PURPOSE:
      start_at = (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols);
      end_at = start_at + disk_Cache->purpose[vol_purpose].nvols;
      break;

    case DISK_PERMVOL_TEMP_PURPOSE:
      start_at = (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols);
      end_at = start_at + disk_Cache->purpose[vol_purpose].nvols;
      break;

    case DISK_TEMPVOL_TEMP_PURPOSE:
      start_at = (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].nvols);
      end_at = start_at + disk_Cache->purpose[vol_purpose].nvols;
      break;

    default:
      csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);
      return ER_FAILED;
    }

  for (i = start_at; i < end_at; i++)
    {
      if (disk_Cache->vols[i].volid == volid)
	{
	  /*
	   * At this point, the caller is holding a volume header.
	   * So, we do not use atomic built-in or mutex variable.
	   */
	  disk_Cache->vols[i].hint_freepages += nfree_pages_toadd;
	}
    }

  /*
   * Decrement the number of free pages for the particular purpose. If the
   * total amount of free space for all volumes of that purpose has dropped
   * below the warning level, send a warning
   */
#if defined(HAVE_ATOMIC_BUILTINS)
  if (do_update_total)
    {
      ATOMIC_INC_32 (&(disk_Cache->purpose[vol_purpose].total_pages),
		     nfree_pages_toadd);
    }
  ATOMIC_INC_32 (&(disk_Cache->purpose[vol_purpose].free_pages),
		 nfree_pages_toadd);
#else /* HAVE_ATOMIC_BUILTINS */
  rv = pthread_mutex_lock (&(disk_Cache->purpose[vol_purpose].update_lock));
  if (do_update_total)
    {
      disk_Cache->purpose[vol_purpose].total_pages += nfree_pages_toadd;
    }
  disk_Cache->purpose[vol_purpose].free_pages += nfree_pages_toadd;
#endif /* HAVE_ATOMIC_BUILTINS */

  if (disk_Cache->purpose[vol_purpose].free_pages < 0)
    {
      assert (false);
      disk_Cache->purpose[vol_purpose].free_pages = 0;
    }

#if !defined(HAVE_ATOMIC_BUILTINS)
  pthread_mutex_unlock (&(disk_Cache->purpose[vol_purpose].update_lock));
#endif /* ! HAVE_ATOMIC_BUILTINS */

#if defined (SERVER_MODE)
  if (need_to_check_auto_volume_ext == true)
    {
      /* calculate remained free pages of generic */
      start_at = (disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols +
		  disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols);
      end_at =
	start_at + disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;

      for (i = start_at; i < end_at; i++)
	{
	  total_free_pages += disk_Cache->vols[i].hint_freepages;
	}
    }
#endif

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);

#if defined (SERVER_MODE)
  if (need_to_check_auto_volume_ext == true
      && total_free_pages <
      (int) (prm_get_bigint_value (PRM_ID_GENERIC_VOL_PREALLOC_SIZE)
	     / IO_PAGESIZE))
    {
      *need_to_add_generic_volume = true;
    }
#endif

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * disk_vhdr_get_last_alloc_pageid () -
 *   return:
 *   vhdr(in):
 *   old_lpageid(in):
 */
static INT32
disk_vhdr_get_last_alloc_pageid (THREAD_ENTRY * thread_p,
				 DISK_VAR_HEADER * vhdr, INT32 old_lpageid)
{
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 lpageid;
  unsigned char *at_chptr, *out_chptr;
  int i;
  bool found = false;

  vpid.volid = vhdr->volid;
  vpid.pageid = vhdr->page_alloctb_page1 + (old_lpageid / DISK_PAGE_BIT);
  lpageid = (((vpid.pageid - vhdr->page_alloctb_page1 + 1) * DISK_PAGE_BIT)
	     - 1);

  for (; (found == false
	  && vpid.pageid >= vhdr->page_alloctb_page1); vpid.pageid--)
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  return NULL_PAGEID;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_VOLBITMAP);

      /* one byte at a time */
      out_chptr = (unsigned char *) pgptr;
      for (at_chptr = (unsigned char *) pgptr + DB_PAGESIZE - 1;
	   found == false && at_chptr >= out_chptr; at_chptr--)
	{

	  /* one bit at a time */
	  for (i = CHAR_BIT - 1; i >= 0; lpageid--, i--)
	    {
	      if (disk_bit_is_set (at_chptr, i))
		{
		  found = true;
		  break;
		}
	    }
	}
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

end:

  return lpageid;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * disk_probe_disk_cache_to_find_desirable_vol () - Find the best volume of the given
 *                     set with at least the expected number of free pages
 *   return: contiguous_best_volid or NULL_VOLID when there is not a volume
 *           with that number of contiguous pages
 *   best_volid(out):
 *   best_numpages(out):
 *   undesirable_volid(in): Undesirable volid
 *   exp_numpages(in): Number of expected pages
 *   start_at(in): Where to start looking for cached volume information
 *   num_vols(in): Number of volumes to look.
 *
 * Note: The function sets as a side effect best_volid and best_numpages for
 *       the most promisent volume base on the  criteria:
 *
 *       1) The volume with the most free pages and with at least exp_numpages
 *          contiguous pages.
 *       2) The volume with the most free pages.
 *
 *       The function returns a valid volume id only when a volume with
 *       exp_numpages contiguous pages was found.
 */
static INT16
disk_probe_disk_cache_to_find_desirable_vol (THREAD_ENTRY * thread_p,
					     INT16 * best_volid,
					     INT32 * best_numpages,
					     INT16 undesirable_volid,
					     INT32 exp_numpages, int start_at,
					     int num_vols)
{
  int i;
  INT16 contiguous_best_volid;

  /* Assume we have enter critical section */
  contiguous_best_volid = NULL_VOLID;

  for (i = start_at; i < num_vols + start_at; i++)
    {
      if (disk_Cache->vols[i].hint_freepages >= exp_numpages
	  && disk_Cache->vols[i].hint_freepages > *best_numpages
	  && undesirable_volid != disk_Cache->vols[i].volid)
	{

	  /* This seems to be a good volume for the desired number of
	     pages. Make sure that this is the case. */
	  if (exp_numpages == 1
	      || disk_get_hint_contiguous_free_numpages (thread_p,
							 disk_Cache->
							 vols[i].volid,
							 exp_numpages,
							 &disk_Cache->
							 vols
							 [i].hint_freepages)
	      == true)
	    {
	      /*
	       * Contiguous pages. This is a very good volume
	       * Reset when either we do not have a contiguous volume or the
	       * current one has more free pages than the previous contiguous
	       * volume
	       */
	      if (contiguous_best_volid == NULL_VOLID
		  || disk_Cache->vols[i].hint_freepages > *best_numpages)
		{
		  *best_numpages = disk_Cache->vols[i].hint_freepages;
		  *best_volid = disk_Cache->vols[i].volid;
		  contiguous_best_volid = *best_volid;
		}
	    }
	  else
	    {
	      /*
	       * There is not that number of contiguous pages. However, we may
	       * have a lot of non contiguous pages.
	       * Reset only when we do not have a contiguous volume
	       */
	      if (contiguous_best_volid == NULL_VOLID
		  && disk_Cache->vols[i].hint_freepages > *best_numpages)
		{
		  *best_numpages = disk_Cache->vols[i].hint_freepages;
		  *best_volid = disk_Cache->vols[i].volid;
		}
	    }
	}
      else
	{
	  /* Reset only if we do not have a contiguous volume and we guess that
	     the number of free pages is larger than the current cached value */
	  if (contiguous_best_volid == NULL_VOLID
	      && undesirable_volid != disk_Cache->vols[i].volid
	      && disk_Cache->vols[i].hint_freepages > *best_numpages)
	    {
	      *best_numpages = disk_Cache->vols[i].hint_freepages;
	      *best_volid = disk_Cache->vols[i].volid;
	    }
	  else
	    {
	      /*
	       * If a volume is undesirable and we are requesting one page.
	       * It is likely that such volume does not have any pages.
	       * Reset the cache for that volume. This is done to try to avoid
	       * a possible loop which could happen if the bitmap and the disk
	       * header are inconsistent
	       */
	      if (exp_numpages == 1
		  && undesirable_volid == disk_Cache->vols[i].volid)
		{
		  disk_Cache->vols[i].hint_freepages = 0;
		}
	    }
	}
    }

  return contiguous_best_volid;
}

/*
 * disk_add_auto_volume_extension:
 *    return:
 *
 *  min_npages(in):
 *  setpage_type(in):
 *  vol_purpose(in):
 */
VOLID
disk_add_auto_volume_extension (THREAD_ENTRY * thread_p, DKNPAGES min_npages,
				DISK_SETPAGE_TYPE setpage_type,
				DISK_VOLPURPOSE vol_purpose)
{
  VOLID volid;
  int max_npages;
  int alloc_npages;

  if (min_npages <= 0)
    {
      min_npages = 1;
    }

  if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE)
    {
      max_npages = boot_get_temp_temp_vol_max_npages ();
    }
  else
    {
      max_npages = (DKNPAGES) (prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE)
			       / IO_PAGESIZE);
    }

  if (max_npages < min_npages)
    {
      max_npages = min_npages;
    }

  alloc_npages = min_npages + disk_get_num_overhead_for_newvol (max_npages);
  alloc_npages = MIN (alloc_npages, VOL_MAX_NPAGES (IO_PAGESIZE));

  if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE
      || vol_purpose == DISK_EITHER_TEMP_PURPOSE)
    {
      volid = boot_add_temp_volume (thread_p, alloc_npages);
    }
  else
    {
      volid = boot_add_auto_volume_extension (thread_p, alloc_npages,
					      setpage_type, vol_purpose,
					      true);
    }

  return volid;
}

/*
 * disk_find_goodvol () - Find a good volume to allocate
 *          the number of expected pages and
 *          create new volume if not find a volume with enough pages.
 *
 *   return: volid or NULL_VOLID (if none is available)
 *
 *   hint_volid(in): Use this volume identifier as a hint
 *   undesirable_volid(in): This volume should not be used
 *   exp_numpages(in): Expected number of pages
 *   setpage_type(in): Type of the set of needed pages
 *   vol_purpose(in): Purpose of storage page
 *
 */
VOLID
disk_find_goodvol (THREAD_ENTRY * thread_p, INT16 hint_volid,
		   INT16 undesirable_volid, INT32 exp_numpages,
		   DISK_SETPAGE_TYPE setpage_type,
		   DISK_VOLPURPOSE vol_purpose)
{
  VOLID volid;
  bool continue_check;


retry:
  /* If volume is exteded automatically,
   * we should retry find goodvol after volume extension.
   */
  volid = disk_find_goodvol_from_disk_cache (thread_p, hint_volid,
					     undesirable_volid,
					     exp_numpages, setpage_type,
					     vol_purpose);

  if (volid == NULL_VOLID)
    {
      if (thread_get_check_interrupt (thread_p) == true)
	{
	  if (logtb_is_interrupted (thread_p, false, &continue_check) == true)
	    {
	      return NULL_VOLID;
	    }
	}

      volid = disk_add_auto_volume_extension (thread_p, exp_numpages,
					      setpage_type, vol_purpose);

      if (volid != NULL_VOLID && vol_purpose == DISK_PERMVOL_GENERIC_PURPOSE)
	{
	  /* volume is added or expanded, retry find */
	  if (volid == undesirable_volid)
	    {
	      /* this volume was expanded */
	      undesirable_volid = NULL_VOLID;
	    }
	  goto retry;
	}
    }

  if (volid == NULL_VOLID)
    {
      if (thread_get_check_interrupt (thread_p) == true)
	{
	  if (logtb_is_interrupted (thread_p, false, &continue_check) == true)
	    {
	      return NULL_VOLID;
	    }
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_FILE_NOT_ENOUGH_PAGES_IN_DATABASE, 1, exp_numpages);
    }

  return volid;
}

/*
 * disk_find_goodvol_from_disk_cache () - Find a good volume to allocate
 *              the number of expected pages
 *   return: volid or NULL_VOLID (if none is available)
 *   hint_volid(in): Use this volume identifier as a hint
 *   undesirable_volid(in): This volume should not be used
 *   exp_numpages(in): Expected number of pages
 *   setpage_type(in): Type of the set of needed pages
 *   vol_purpose(in): Purpose of storage page
 */
static VOLID
disk_find_goodvol_from_disk_cache (THREAD_ENTRY * thread_p, INT16 hint_volid,
				   INT16 undesirable_volid,
				   INT32 exp_numpages,
				   DISK_SETPAGE_TYPE setpage_type,
				   DISK_VOLPURPOSE vol_purpose)
{
  VOLID best_volid = NULL_VOLID;
  INT32 best_numpages = -1;
  INT16 num_data_vols, num_index_vols, num_generic_vols;
  INT16 num_ptemp_vols, num_ttemp_vols;
  bool found_contiguous = true;

  /* If disk cache is not initialized, we do it here. */
  if (disk_Cache->max_nvols <= 0
      && disk_goodvol_refresh (thread_p, -1) == false)
    {
      return NULL_VOLID;
    }

  if (csect_enter_as_reader
      (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT) != NO_ERROR)
    {
      return NULL_VOLID;
    }

  switch (vol_purpose)
    {
    case DISK_PERMVOL_DATA_PURPOSE:
      /*
       * The best volume is one in the following range
       * 1) A volume with main purpose = DATA
       * 2) A volume with main purpose = GENERIC
       */
      num_data_vols = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      if (num_data_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages, 0,
							  num_data_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      num_generic_vols =
	disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;
      num_index_vols = disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols;
      if (num_generic_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  (num_data_vols +
							   num_index_vols),
							  num_generic_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      /* We did not find contiguous pages. */
      found_contiguous = false;
      break;

    case DISK_PERMVOL_INDEX_PURPOSE:
      /*
       * The best volume is one in the following range
       * 1) A volume with main purpose = INDEX
       * 2) A volume with main purpose = GENERIC
       */
      num_index_vols = disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols;
      num_data_vols = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      if (num_index_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  num_data_vols,
							  num_index_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      num_generic_vols =
	disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;
      if (num_generic_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  num_data_vols +
							  num_index_vols,
							  num_generic_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      /* We did not find contiguous pages. */
      found_contiguous = false;
      break;

    case DISK_PERMVOL_GENERIC_PURPOSE:
    case DISK_UNKNOWN_PURPOSE:
    default:
      /*
       * The best volume is one in the following range
       * 1) A volume with main purpose = GENERIC
       */
      num_generic_vols =
	disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;
      num_data_vols = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      num_index_vols = disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols;
      if (num_generic_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  (num_data_vols +
							   num_index_vols),
							  num_generic_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      /* We did not find contiguous pages. */
      found_contiguous = false;
      break;

    case DISK_TEMPVOL_TEMP_PURPOSE:
      if (hint_volid >= disk_Cache->nvols
	  && disk_get_hint_contiguous_free_numpages (thread_p, hint_volid,
						     exp_numpages,
						     &best_numpages) == true)
	{
	  /* This is a temporary volume */
	  best_volid = hint_volid;
	  break;
	}

      /*
       * The best volume is one in the following range
       * 1) The given hinted volume
       * 2) A volume with main purpose = TEMP TEMP
       * 3) A volume with main purpose = PERM TEMP
       */
      num_ttemp_vols = disk_Cache->purpose[DISK_TEMPVOL_TEMP_PURPOSE].nvols;
      num_data_vols = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      num_index_vols = disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols;
      num_generic_vols =
	disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;
      num_ptemp_vols = disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].nvols;

      if (num_ttemp_vols
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  (num_data_vols +
							   num_index_vols +
							   num_generic_vols +
							   num_ptemp_vols),
							  num_ttemp_vols) !=
	  NULL_VOLID)
	{
	  break;
	}
      /* Fall throu DISK_PERMVOL_TEMP_PURPOSE case */

    case DISK_PERMVOL_TEMP_PURPOSE:
      if (hint_volid >= disk_Cache->nvols
	  && disk_get_hint_contiguous_free_numpages (thread_p, hint_volid,
						     exp_numpages,
						     &best_numpages) == true)
	{
	  /* This is a temporary volume */
	  best_volid = hint_volid;
	  break;
	}

      /*
       * The best volume is one in the following range
       * 1) The given hinted volume
       * 2) A volume with main purpose = PERM TEMP
       */
      num_ptemp_vols = disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].nvols;
      num_data_vols = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      num_index_vols = disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols;
      num_generic_vols =
	disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;

      if (num_ptemp_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  (num_data_vols +
							   num_index_vols +
							   num_generic_vols),
							  num_ptemp_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      /* We did not find contiguous pages. */
      found_contiguous = false;
      break;

    case DISK_EITHER_TEMP_PURPOSE:
      if (hint_volid >= disk_Cache->nvols
	  && disk_get_hint_contiguous_free_numpages (thread_p, hint_volid,
						     exp_numpages,
						     &best_numpages) == true)
	{
	  /* This is a temporary volume */
	  best_volid = hint_volid;
	  break;
	}

      /*
       * The best volume is one in the following range
       * 1) A volume with main purpose = PERM TEMP
       * 2) A volume with main purpose = TEMP TEMP
       */
      num_ptemp_vols = disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].nvols;
      num_data_vols = disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].nvols;
      num_index_vols = disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].nvols;
      num_generic_vols =
	disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].nvols;

      if (num_ptemp_vols
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  (num_data_vols +
							   num_index_vols +
							   num_generic_vols),
							  num_ptemp_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      num_ttemp_vols = disk_Cache->purpose[DISK_TEMPVOL_TEMP_PURPOSE].nvols;
      if (num_ttemp_vols > 0
	  && disk_probe_disk_cache_to_find_desirable_vol (thread_p,
							  &best_volid,
							  &best_numpages,
							  undesirable_volid,
							  exp_numpages,
							  (num_data_vols +
							   num_index_vols +
							   num_generic_vols +
							   num_ptemp_vols),
							  num_ttemp_vols) !=
	  NULL_VOLID)
	{
	  break;
	}

      /* We did not find contiguous pages. */
      found_contiguous = false;
      break;
    }

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);

  if (found_contiguous == false)
    {
      /* We did not find contiguous pages. Can we provide a volume with
         the non contiguos pages ? */
      switch (setpage_type)
	{
	case DISK_CONTIGUOUS_PAGES:
	  /* Don't have contiguous pages */
	  best_volid = NULL_VOLID;
	  break;

	case DISK_NONCONTIGUOUS_PAGES:
	  /* Don't need to be contiguous but they need to be in the same
	     volume */
	  if (best_numpages < exp_numpages)
	    {
	      /* Don't have enough pages */
	      best_volid = NULL_VOLID;
	    }
	  break;

	case DISK_NONCONTIGUOUS_SPANVOLS_PAGES:
	  /*
	   * Don't need to be contiguous, they can be on several volumes.
	   *
	   * We will use this volume only if it has at least 100 free pages.
	   * This function may be called when creating a file or making a
	   * new allocset. In this case, we think that it's better to create
	   * a new volume if a small number of pages can be used on this volume.
	   * It is expected that free pages will be used by another file located
	   * on this volume.
	   */
	  if (best_volid != NULL_VOLID
	      && best_numpages < DISK_SECTOR_NPAGES * 10)
	    {
	      /* Don't have enough pages */
	      best_volid = NULL_VOLID;
	    }
	  break;

	default:
	  best_volid = NULL_VOLID;
	  break;
	}
    }

  return best_volid;
}

/* TODO: STL::vector for disk_Cache->purpose */
/*
 * disk_cache_get_purpose_info () - Find total and free pages of volumes with the
 *                            given purpose
 *   return: num of vols
 *   vol_purpose(in): For this purpose
 *   nvols(out): Number of volumes with the given purpose
 *   total_pages(out): Total number of pages with the above storage purpose
 *   free_pages(out): Total number of free pages with the above storage purpose
 *
 * NOte : If the purpose is unknown, it find the total specifications for all
 *        volumes.
 */
static INT16
disk_cache_get_purpose_info (THREAD_ENTRY * thread_p,
			     DISK_VOLPURPOSE vol_purpose, INT16 * nvols,
			     int *total_pages, int *free_pages)
{
  if (csect_enter_as_reader (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT)
      != NO_ERROR)
    {
      *nvols = -1;
      *total_pages = -1;
      *free_pages = -1;

      return NULL_VOLID;
    }

  if (vol_purpose < DISK_PERMVOL_DATA_PURPOSE
      || vol_purpose > DISK_TEMPVOL_TEMP_PURPOSE)
    {
      vol_purpose = DISK_UNKNOWN_PURPOSE;
    }

  if (vol_purpose != DISK_UNKNOWN_PURPOSE)
    {
      *nvols = disk_Cache->purpose[vol_purpose].nvols;
      *total_pages = disk_Cache->purpose[vol_purpose].total_pages;
      *free_pages = disk_Cache->purpose[vol_purpose].free_pages;
    }
  else
    {
      *nvols = 0;
      *total_pages = 0;
      *free_pages = 0;

      for (vol_purpose = DISK_PERMVOL_DATA_PURPOSE;
	   vol_purpose < DISK_UNKNOWN_PURPOSE;
	   vol_purpose = (DISK_VOLPURPOSE) (vol_purpose + 1))
	{
	  *nvols += disk_Cache->purpose[vol_purpose].nvols;
	  *total_pages += disk_Cache->purpose[vol_purpose].total_pages;
	  *free_pages += disk_Cache->purpose[vol_purpose].free_pages;
	}
    }

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);

  return *nvols;
}

/* TODO: STL::vector for disk_Cache->purpose */
/*
 * disk_get_max_numpages () - Find number of free pages for volumes,
 *                                including newly automatically created,
 *                                of the given purpose
 *   return: num of free pages
 *   vol_purpose(in): For this purpose
 */
INT32
disk_get_max_numpages (THREAD_ENTRY * thread_p, DISK_VOLPURPOSE vol_purpose)
{
  INT32 maxpgs = 0;
  INT32 (*fun) (void);

  if (csect_enter_as_reader (thread_p, CSECT_DISK_REFRESH_GOODVOL, INF_WAIT)
      != NO_ERROR)
    {
      return ER_FAILED;
    }

  switch (vol_purpose)
    {
    case DISK_PERMVOL_DATA_PURPOSE:
      /*
       * Space on one of the following volumes
       * 1) A volume with main purpose = DATA
       * 2) A volume with main purpose = GENERIC
       */

      maxpgs += disk_Cache->purpose[DISK_PERMVOL_DATA_PURPOSE].free_pages;
      maxpgs += disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].free_pages;
      fun = boot_max_pages_for_new_auto_volume_extension;
      break;

    case DISK_PERMVOL_INDEX_PURPOSE:
      /*
       * Space on one of the following volumes
       * 1) A volume with main purpose = INDEX
       * 2) A volume with main purpose = GENERIC
       */

      maxpgs += disk_Cache->purpose[DISK_PERMVOL_INDEX_PURPOSE].free_pages;
      maxpgs += disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].free_pages;
      fun = boot_max_pages_for_new_auto_volume_extension;
      break;

    case DISK_PERMVOL_GENERIC_PURPOSE:
    case DISK_UNKNOWN_PURPOSE:
    default:
      /*
       * Space on one of the following volumes
       * 1) A volume with main purpose = GENERIC
       */

      maxpgs += disk_Cache->purpose[DISK_PERMVOL_GENERIC_PURPOSE].free_pages;
      fun = boot_max_pages_for_new_auto_volume_extension;
      break;

    case DISK_TEMPVOL_TEMP_PURPOSE:
      /*
       * Space on one of the following volumes
       * 1) A volume with main purpose = TEMP TEMP
       * 2) A volume with main purpose = PERM TEMP
       */

      maxpgs += disk_Cache->purpose[DISK_TEMPVOL_TEMP_PURPOSE].free_pages;
      maxpgs += disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].free_pages;
      fun = boot_max_pages_for_new_temp_volume;
      break;

    case DISK_PERMVOL_TEMP_PURPOSE:
      /*
       * Space on one of the following volumes
       * 1) A volume with main purpose = PERM TEMP
       */

      maxpgs += disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].free_pages;
      fun = NULL;
      break;

    case DISK_EITHER_TEMP_PURPOSE:
      /*
       * Space on one of the following volumes
       * The best volume is one in the following range
       * 1) A volume with main purpose = TEMP TEMP
       * 2) A volume with main purpose = PERM TEMP
       */

      maxpgs += disk_Cache->purpose[DISK_TEMPVOL_TEMP_PURPOSE].free_pages;
      maxpgs += disk_Cache->purpose[DISK_PERMVOL_TEMP_PURPOSE].free_pages;
      fun = boot_max_pages_for_new_temp_volume;
      break;
    }

  csect_exit (thread_p, CSECT_DISK_REFRESH_GOODVOL);

  if (fun != NULL)
    {
      maxpgs += (*fun) ();
    }

  return maxpgs;
}

/*
 * disk_get_all_total_free_numpages () - Find total and free pages of volumes with the
 *                              given purpose
 *   return: num of vols
 *   vol_purpose(in): For this purpose
 *   nvols(out): Number of volumes with the given purpose
 *   total_pages(out): Total number of pages with the above storage purpose
 *   free_pages(out): Total number of free pages with the above storage purpose
 *
 * Note: If the purpose is unknown, it find the total specifications for all
 *       volumes
 */
INT16
disk_get_all_total_free_numpages (THREAD_ENTRY * thread_p,
				  DISK_VOLPURPOSE vol_purpose, INT16 * nvols,
				  int *total_pages, int *free_pages)
{
  return disk_cache_get_purpose_info (thread_p, vol_purpose, nvols,
				      total_pages, free_pages);
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
    case DISK_PERMVOL_DATA_PURPOSE:
      return "Permanent DATA Volume";
    case DISK_PERMVOL_INDEX_PURPOSE:
      return "Permanent INDEX Volume";
    case DISK_PERMVOL_GENERIC_PURPOSE:
      return "Permanent GENERIC Volume";
    case DISK_PERMVOL_TEMP_PURPOSE:
      return "Permanent TEMP Volume";
    case DISK_TEMPVOL_TEMP_PURPOSE:
      return "Temporary TEMP Volume";
    case DISK_EITHER_TEMP_PURPOSE:
    case DISK_UNKNOWN_PURPOSE:
      break;
    }
  return "Unknown purpose";
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

  length_to_move = (length_diff +
		    (int) strlen (vhdr->var_fields + length_diff)
		    + 1 - vhdr->offset_to_next_vol_fullname);

  /* Difference in length between new name and old name */
  length_diff = (((int) strlen (vol_fullname) + 1) -
		 (vhdr->offset_to_next_vol_fullname -
		  vhdr->offset_to_vol_fullname));

  if (length_diff != 0)
    {
      /* We need to either move to right(expand) or left(shrink) the rest
         of the variable length fields */
      memmove (disk_vhdr_get_next_vol_fullname (vhdr) + length_diff,
	       disk_vhdr_get_next_vol_fullname (vhdr), length_to_move);
      vhdr->offset_to_next_vol_fullname += length_diff;
      vhdr->offset_to_vol_remarks += length_diff;
    }

  (void) memcpy (disk_vhdr_get_vol_fullname (vhdr),
		 vol_fullname, MIN ((ssize_t) strlen (vol_fullname) + 1,
				    DB_MAX_PATH_LENGTH));

  return ret;
}

/*
 * disk_vhdr_set_next_vol_fullname () -
 *   return: NO_ERROR
 *   vhdr(in):
 *   next_vol_fullname(in):
 */
static int
disk_vhdr_set_next_vol_fullname (DISK_VAR_HEADER * vhdr,
				 const char *next_vol_fullname)
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
  length_diff = (next_vol_fullname_size -
		 (vhdr->offset_to_vol_remarks -
		  vhdr->offset_to_next_vol_fullname));

  if (length_diff != 0)
    {
      /* We need to either move to right(expand) or left(shrink) the rest
         of the variable length fields */
      memmove (disk_vhdr_get_vol_remarks (vhdr) + length_diff,
	       disk_vhdr_get_vol_remarks (vhdr), length_to_move);
      vhdr->offset_to_vol_remarks += length_diff;
    }

  (void) memcpy (disk_vhdr_get_next_vol_fullname (vhdr),
		 next_vol_fullname, next_vol_fullname_size);

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
      maxsize =
	(DB_PAGESIZE - offsetof (DISK_VAR_HEADER, var_fields) -
	 vhdr->offset_to_vol_remarks);

      if ((int) strlen (vol_remarks) > maxsize)
	{
	  /* Does not fit.. Truncate the comment */
	  (void) strncpy (disk_vhdr_get_vol_remarks (vhdr), vol_remarks,
			  maxsize - 1);
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
 * disk_format () - Format a volume with the given name, identifier, and number
 *                of pages
 *   return: volid on success, NULL_VOLID on failure
 *   dbname(in): Name of database where the volume belongs
 *   volid(in): Permanent volume identifier
 *   vol_fullname(in): Name of volume to format
 *   vol_remarks(in): Volume remarks such as version of system, name of the
 *                    creator of the database, or nothing at all(NULL)
 *   npages(in): Size of the volume in pages
 *   Kbytes_to_be_written_per_sec(in) : Size to add volume per sec
 *   vol_purpose(in): The main purpose of the volume
 *
 * Note: The volume header, the sector and page allocator tables are
 *       initialized. All the pages are formatted for database access. For
 *       example, LSA number recovery information is recorded in every page
 *       (See log and recovery manager for its use).
 */
INT16
disk_format (THREAD_ENTRY * thread_p, const char *dbname, INT16 volid,
	     DBDEF_VOL_EXT_INFO * ext_info)
{
  int vdes;			/* Volume descriptor           */
  DISK_VAR_HEADER *vhdr;	/* Pointer to volume header    */
  VPID vpid;			/* Volume and page identifiers */
  LOG_DATA_ADDR addr;		/* Address of logging data     */
  const char *vol_fullname = ext_info->name;
  INT32 max_npages = ext_info->max_npages;
  int kbytes_to_be_written_per_sec = ext_info->max_writesize_in_sec;
  DISK_VOLPURPOSE vol_purpose = ext_info->purpose;
  INT32 extend_npages = ext_info->extend_npages;

  if (vol_purpose != DISK_PERMVOL_GENERIC_PURPOSE)
    {
      assert_release (max_npages == extend_npages);
    }

#if defined(CUBRID_DEBUG)
  if (DB_PAGESIZE < sizeof (DISK_VAR_HEADER))
    {
      er_log_debug (ARG_FILE_LINE,
		    "dk_format: ** SYSTEM_ERROR AT COMPILE TIME **"
		    " DB_PAGESIZE must be > %d and multiple of %d. Current value"
		    " is set to %d", sizeof (DISK_VAR_HEADER),
		    sizeof (INT32), DB_PAGESIZE);
#if defined(NDEBUG)
      exit (EXIT_FAILURE);
#else /* NDEBUG */
      /* debugging purpose */
      abort ();
#endif /* NDEBUG */
    }
#endif /* CUBRID_DEBUG */

  addr.vfid = NULL;

  if ((strlen (vol_fullname) + 1 > DB_MAX_PATH_LENGTH)
      || (DB_PAGESIZE <
	  (SSIZEOF (DISK_VAR_HEADER) + strlen (vol_fullname) + 1)))
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, NULL, vol_fullname,
	      strlen (vol_fullname) + 1, DB_MAX_PATH_LENGTH);
      return NULL_VOLID;
    }

  /* Make sure that this is a valid purpose */
  if (vol_purpose < DISK_PERMVOL_DATA_PURPOSE
      || vol_purpose > DISK_TEMPVOL_TEMP_PURPOSE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DISK_UNKNOWN_PURPOSE, 3,
	      vol_purpose, DISK_PERMVOL_DATA_PURPOSE,
	      DISK_TEMPVOL_TEMP_PURPOSE);
      return NULL_VOLID;
    }

  /*
   * Undo must be logical since we are going to remove the volume in the
   * case of rollback (really a crash since we are in a top operation)
   */
  addr.offset = 0;
  addr.pgptr = NULL;
  log_append_undo_data (thread_p, RVDK_FORMAT, &addr,
			(int) strlen (vol_fullname) + 1, vol_fullname);
  /* This log must be flushed. */
  LOG_CS_ENTER (thread_p);
  logpb_flush_pages_direct (thread_p);
  LOG_CS_EXIT (thread_p);

  /* Create and initialize the volume. Recovery information is initialized in
     every page. */

  if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE)
    {
      vdes = fileio_format (thread_p, dbname, vol_fullname, volid,
			    max_npages, false, false, false, IO_PAGESIZE,
			    kbytes_to_be_written_per_sec, false);
    }
  else
    {
      vdes = fileio_format (thread_p, dbname, vol_fullname, volid,
			    extend_npages, true, false, false, IO_PAGESIZE,
			    kbytes_to_be_written_per_sec, false);
    }

  if (vdes == NULL_VOLDES)
    {
      return NULL_VOLID;
    }

  /* initialize the volume header and the sector and page allocation tables */

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /* Lock the volume header in exclusive mode and then fetch the page. */

  addr.pgptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return NULL_VOLID;
    }

  (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_VOLHEADER);

  /* Initialize the header */

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  strncpy (vhdr->magic, CUBRID_MAGIC_DATABASE_VOLUME,
	   CUBRID_MAGIC_MAX_LENGTH);
  vhdr->iopagesize = IO_PAGESIZE;
  vhdr->volid = volid;
  vhdr->purpose = vol_purpose;
  vhdr->sect_npgs = DISK_SECTOR_NPAGES;
  vhdr->total_pages = extend_npages;
  vhdr->free_pages = 0;
  vhdr->total_sects = CEIL_PTVDIV (extend_npages, DISK_SECTOR_NPAGES);
  vhdr->max_npages = max_npages;
  vhdr->free_sects = 0;
  vhdr->used_data_npages = 0;
  vhdr->used_index_npages = 0;

  /* page/sect alloctable must be created with max_npages.
   * not initial size(extend_npages) */
  if (disk_set_alloctables (vol_purpose,
			    CEIL_PTVDIV (max_npages, DISK_SECTOR_NPAGES),
			    max_npages, &vhdr->sect_alloctb_npages,
			    &vhdr->page_alloctb_npages,
			    &vhdr->sect_alloctb_page1,
			    &vhdr->page_alloctb_page1,
			    &vhdr->sys_lastpage) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return NULL_VOLID;
    }

  if (vhdr->sys_lastpage >= extend_npages)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      (void) pgbuf_invalidate_all (thread_p, volid);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_BAD_NPAGES, 2,
	      vol_fullname, max_npages);
      return NULL_VOLID;
    }

  vhdr->free_pages = vhdr->total_pages - vhdr->sys_lastpage - 1;
  vhdr->free_sects = (vhdr->total_sects -
		      CEIL_PTVDIV ((vhdr->sys_lastpage + 1),
				   DISK_SECTOR_NPAGES));

  /*
   * Start allocating sectors a little bit away from the top of the volume.
   * This is done to allow system functions which allocate pages from the
   * special sector to find pages as close as possible. One example, is the
   * allocation of pages for system file table information. For now, five
   * sectors are skipped. Note that these sectors are allocated once the
   * hint of allocated sectors is looped.
   */

  if (vol_purpose != DISK_TEMPVOL_TEMP_PURPOSE
      && vhdr->total_sects > DISK_HINT_START_SECT
      && (vhdr->total_sects - vhdr->free_sects) < DISK_HINT_START_SECT)
    {
      vhdr->hint_allocsect = DISK_HINT_START_SECT;
    }
  else
    {
      vhdr->hint_allocsect = vhdr->total_sects - 1;
    }

  /* Find the time of the creation of the database and the current LSA
     checkpoint. */

  if (log_get_db_start_parameters (&vhdr->db_creation, &vhdr->chkpt_lsa) !=
      NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return NULL_VOLID;
    }

  /* Initialize the system heap file for booting purposes. This field is
     reseted after the heap file is created by the boot manager */

  vhdr->boot_hfid.vfid.volid = NULL_VOLID;
  vhdr->boot_hfid.vfid.fileid = NULL_PAGEID;
  vhdr->boot_hfid.hpgid = NULL_PAGEID;

  /* Initialize variable length fields */

  vhdr->offset_to_vol_fullname = vhdr->offset_to_next_vol_fullname =
    vhdr->offset_to_vol_remarks = 0;
  vhdr->var_fields[vhdr->offset_to_vol_fullname] = '\0';
  if (disk_vhdr_set_vol_fullname (vhdr, vol_fullname) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return NULL_VOLID;
    }
  if (disk_vhdr_set_next_vol_fullname (vhdr, NULL) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return NULL_VOLID;
    }
  if (disk_vhdr_set_vol_remarks (vhdr, ext_info->comments) != NO_ERROR)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      return NULL_VOLID;
    }

  /* Make sure that in the case of a crash, the volume is created. Otherwise,
     the recovery will not work */

  if (vol_purpose != DISK_TEMPVOL_TEMP_PURPOSE)
    {
      log_append_dboutside_redo (thread_p, RVDK_NEWVOL,
				 sizeof (*vhdr) +
				 disk_vhdr_length_of_varfields (vhdr), vhdr);
    }

  /* Even though the volume header page is not completed at this moment,
   * to write REDO log for the header page is crucial for redo recovery
   * since disk_map_init and disk_set_link will write their redo logs.
   * These functions will access the header page during restart recovery.
   *
   * Another REDO log for RVDK_FORMAT will be written to completely log
   * the header page including the volume link.
   */
  if (vol_purpose != DISK_TEMPVOL_TEMP_PURPOSE)
    {
      addr.offset = 0;		/* Header is located at position zero */
      log_append_redo_data (thread_p, RVDK_FORMAT, &addr,
			    sizeof (*vhdr) +
			    disk_vhdr_length_of_varfields (vhdr), vhdr);
    }

  /* Now initialize the sector and page allocator tables and link the volume
     to previous allocated volume */

  if (disk_map_init (thread_p, volid, vhdr->sect_alloctb_page1,
		     vhdr->sect_alloctb_page1 + vhdr->sect_alloctb_npages - 1,
		     vhdr->total_sects - vhdr->free_sects,
		     vol_purpose) != NO_ERROR
      || disk_map_init (thread_p, volid, vhdr->page_alloctb_page1,
			vhdr->page_alloctb_page1 + vhdr->page_alloctb_npages -
			1, vhdr->sys_lastpage + 1, vol_purpose) != NO_ERROR
      || (vol_purpose != DISK_TEMPVOL_TEMP_PURPOSE && volid > 0
	  && disk_set_link (thread_p, volid - 1, vol_fullname, true,
			    DISK_FLUSH) != NO_ERROR))
    {
      /* Problems setting the map allocation tables, release the header page,
         dismount and destroy the volume, and return */
      pgbuf_unfix_and_init (thread_p, addr.pgptr);

      (void) pgbuf_invalidate_all (thread_p, volid);
      return NULL_VOLID;
    }
  else
    {
      if (vol_purpose != DISK_TEMPVOL_TEMP_PURPOSE)
	{
	  addr.offset = 0;	/* Header is located at position zero */
	  log_append_redo_data (thread_p, RVDK_FORMAT, &addr,
				sizeof (*vhdr) +
				disk_vhdr_length_of_varfields (vhdr), vhdr);
	}

      /*
       * If this is a volume with temporary purposes, we do not log any disk
       * driver related changes any longer. Indicate that by setting the disk
       * pages to temporary lsa
       */

      if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE
	  || vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
	{

	  PAGE_PTR pgptr = NULL;	/* Page pointer                 */
	  LOG_LSA init_with_temp_lsa;	/* A lsa for temporary purposes */

	  /* Flush the pages so that the log is forced */
	  (void) pgbuf_flush_all (thread_p, volid);

	  for (vpid.volid = volid, vpid.pageid = DISK_VOLHEADER_PAGE;
	       vpid.pageid <= vhdr->sys_lastpage; vpid.pageid++)
	    {
	      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
				 PGBUF_UNCONDITIONAL_LATCH);
	      if (pgptr != NULL)
		{
		  pgbuf_set_lsa_as_temporary (thread_p, pgptr);
		  pgbuf_unfix_and_init (thread_p, pgptr);
		}
	    }

	  if (vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
	    {
	      LSA_SET_INIT_TEMP (&init_with_temp_lsa);
	      /*
	       * Flush all dirty pages and then invalidate them from page buffer
	       * pool. So that we can reset the recovery information directly
	       * using the io module
	       */

	      (void) pgbuf_invalidate_all (thread_p, volid);	/* Flush and invalidate */
	      if (fileio_reset_volume (thread_p, vdes, vol_fullname,
				       max_npages,
				       &init_with_temp_lsa) != NO_ERROR)
		{
		  /*
		   * Problems reseting the pages of the permanent volume for
		   * temporary storage purposes... That is, with a tempvol LSA.
		   * dismount and destroy the volume, and return
		   */
		  pgbuf_unfix_and_init (thread_p, addr.pgptr);
		  return NULL_VOLID;
		}
	    }
	}

      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  /*
   * Flush all pages that were formatted. This is not needed, but it is done
   * for security reasons to identify the volume in case of a system crash.
   * Note that the identification may not be possible during media crashes
   */
  (void) pgbuf_flush_all (thread_p, volid);
  (void) fileio_synchronize (thread_p, vdes, vol_fullname);

  /*
   * If this is a permanent volume for temporary storage purposes, indicate
   * so to page buffer manager, so that fetches of new pages can be
   * initialized with temporary lsa..which will avoid logging.
   */

  if (vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
    {
      pgbuf_cache_permanent_volume_for_temporary (volid);
    }

  return volid;
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
 * disk_expand_tmp () - Expand a temporary volume with a minumum min_pages
 *                        and a maximum min_pages
 *   return: number of pages that were added
 *   volid(in): Volume identifier
 *   min_pages(in): Minimum number of pages to expand
 *   max_pages(in): Maximum number of pages to expand
 *
 * Note: That is, a set of pages between min_pages and max_pages are added to
 *       the volume. Volume must be a temporary volume for temporary purposes.
 */
INT32
disk_expand_tmp (THREAD_ENTRY * thread_p, INT16 volid, INT32 min_pages,
		 INT32 max_pages)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;
  INT32 npages_toadd;
  INT32 nsects_toadd;
#if defined(SERVER_MODE)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#endif /* SERVER_MODE */

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  assert (csect_check_own (thread_p, CSECT_BOOT_SR_DBPARM) == 1);

  if (min_pages < DISK_EXPAND_TMPVOL_INCREMENTS
      && max_pages > DISK_EXPAND_TMPVOL_INCREMENTS)
    {
      max_pages = DISK_EXPAND_TMPVOL_INCREMENTS;
    }

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return -1;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  if (vhdr->purpose != DISK_TEMPVOL_TEMP_PURPOSE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_DISK_CANNOT_EXPAND_PERMVOLS, 2,
	      fileio_get_volume_label (volid, PEEK), vhdr->purpose);
      goto error;
    }

  /* Compute the maximum pages that I can add */
  npages_toadd = ((vhdr->sys_lastpage - vhdr->page_alloctb_page1 + 1) *
		  DISK_PAGE_BIT) - vhdr->total_pages;

  /* Now adjust according to the requested numbers */
  if (npages_toadd < min_pages)
    {
      /* This volume cannot be expanded with the given number of pages. */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DISK_UNABLE_TO_EXPAND, 2,
	      fileio_get_volume_label (volid, PEEK), min_pages);
      goto error;
    }
  else if (npages_toadd > max_pages)
    {
      npages_toadd = max_pages;
    }

#if defined(SERVER_MODE)
  tsc_getticks (&start_tick);
#endif /* SERVER_MODE */

  if (fileio_expand (thread_p, volid, npages_toadd, DISK_TEMPVOL_TEMP_PURPOSE)
      != npages_toadd)
    {
      goto error;
    }

#if defined(SERVER_MODE)
  tsc_getticks (&end_tick);
  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
  TSC_ADD_TIMEVAL (thread_p->event_stats.temp_expand_time, tv_diff);

  thread_p->event_stats.temp_expand_pages += npages_toadd;
#endif /* SERVER_MODE */

  /*
   * Now apply the changes to the volume header.
   * NOTE that the bitmap has already been initialized during the format of the
   *      volume.
   */
  vhdr->total_pages += npages_toadd;
  vhdr->free_pages += npages_toadd;

  /*
   * Add any sectors to covert the new set of pages or until the end of
   * the sector allocation table. That is, the sector allocation table is
   * not expand beyond its number of pages.
   */

  nsects_toadd = CEIL_PTVDIV (vhdr->total_pages, DISK_SECTOR_NPAGES);
  if (nsects_toadd <= (vhdr->sect_alloctb_npages * DISK_PAGE_BIT))
    {
      nsects_toadd = nsects_toadd - vhdr->total_sects;
    }
  else
    {
      nsects_toadd = ((vhdr->sect_alloctb_npages * DISK_PAGE_BIT)
		      - vhdr->total_sects);
    }

  vhdr->total_sects += nsects_toadd;
  vhdr->free_sects += nsects_toadd;

  /* Update total_pages and free_pages on disk_Cache. */
  disk_cache_goodvol_update (thread_p, volid, DISK_TEMPVOL_TEMP_PURPOSE,
			     npages_toadd, true, NULL);

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  /* Set dirty header page, free it, and unlock it */
  pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);

  return npages_toadd;

error:

  assert (hdr_pgptr != NULL);

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return -1;
}

/*
 * disk_expand_perm () -
 *   return:
 *   volid(in):
 *   npages(in):
 *
 * Note:
 */
int
disk_expand_perm (THREAD_ENTRY * thread_p, INT16 volid, INT32 npages)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;
  INT32 nsects_toadd;
  LOG_DATA_ADDR addr;
  DISK_VOLPURPOSE purpose;
  DISK_RECV_INIT_PAGES_INFO log_data;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  assert (csect_check_own (thread_p, CSECT_BOOT_SR_DBPARM) == 1);

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return -1;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  if (vhdr->purpose != DISK_PERMVOL_GENERIC_PURPOSE)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_DISK_CANNOT_EXPAND_PERMVOLS, 2,
	      fileio_get_volume_label (volid, PEEK), vhdr->purpose);
      assert_release (vhdr->purpose == DISK_PERMVOL_GENERIC_PURPOSE);
      pgbuf_unfix_and_init (thread_p, hdr_pgptr);

      return -1;
    }

  if (vhdr->max_npages - vhdr->total_pages < npages)
    {
      /* Add maximum pages that i can add */
      npages = vhdr->max_npages - vhdr->total_pages;
    }

  purpose = vhdr->purpose;

  log_data.volid = volid;
  log_data.start_pageid = vhdr->total_pages;
  log_data.npages = npages;

  /* release disk header page latch while fileio_expand. */
  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  if (fileio_expand (thread_p, volid, npages, purpose) != npages)
    {
      return -1;
    }

  log_append_dboutside_redo (thread_p, RVDK_INIT_PAGES,
			     sizeof (DISK_RECV_INIT_PAGES_INFO), &log_data);

  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return -1;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  /*
   * Now apply the changes to the volume header.
   * NOTE that the bitmap has already been initialized during the format of the
   *      volume.
   */
  vhdr->total_pages += npages;
  vhdr->free_pages += npages;

  /*
   * Add any sectors to covert the new set of pages or until the end of
   * the sector allocation table. That is, the sector allocation table is
   * not expand beyond its number of pages.
   */

  nsects_toadd = CEIL_PTVDIV (vhdr->total_pages, DISK_SECTOR_NPAGES);
  if (nsects_toadd <= (vhdr->sect_alloctb_npages * DISK_PAGE_BIT))
    {
      nsects_toadd = nsects_toadd - vhdr->total_sects;
    }
  else
    {
      nsects_toadd = ((vhdr->sect_alloctb_npages * DISK_PAGE_BIT)
		      - vhdr->total_sects);
    }

  vhdr->total_sects += nsects_toadd;
  vhdr->free_sects += nsects_toadd;


  addr.offset = 0;		/* Header is located at position zero */
  addr.vfid = NULL;
  addr.pgptr = hdr_pgptr;

  log_append_redo_data (thread_p, RVDK_FORMAT, &addr,
			sizeof (*vhdr) + disk_vhdr_length_of_varfields (vhdr),
			vhdr);

  /* Update total_pages and free_pages on disk_Cache. */
  disk_cache_goodvol_update (thread_p, volid, vhdr->purpose, npages, true,
			     NULL);

  if (vhdr->total_pages >= vhdr->max_npages)
    {
      /* This generic volume was extended to the max */
      disk_cache_set_auto_extend_volid (thread_p, NULL_VOLID);
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  /* Set dirty header page, free it, and unlock it */
  pgbuf_set_dirty (thread_p, hdr_pgptr, FREE);

  return npages;
}

/*
 * disk_reinit_all_tmp () - Reinitialize all volumes with temporary storage
 *                        purposes
 *   return: NO_ERROR
 *
 * Note: All pages and sectors of temporary storage purposes are declared
 *       as deallocated.
 */
int
disk_reinit_all_tmp (THREAD_ENTRY * thread_p)
{
  int ret = NO_ERROR;

  ret =
    fileio_map_mounted (thread_p, disk_reinit,
			NULL) == true ? NO_ERROR : ER_FAILED;

  return ret;
}

/*
 * disk_reinit () - Reinitialize a volume with temporary storage purposes
 *   return: true
 *   volid(in): Permanent volume identifier
 *   ignore(in): Nothing
 *
 * Note: All sectors and pages of the given volume are declared as not
 *       allocated.
 *
 *       WARNING:
 *       This function should be used only for volumes with temporary purposes.
 *       This function will NOT log anything. The function can be used at
 *       restart time to reinitialize permanent volumes with temporary storage
 *       purposes.
 */
static bool
disk_reinit (THREAD_ENTRY * thread_p, INT16 volid, void *ignore)
{
  DISK_VAR_HEADER *vhdr;	/* Pointer to volume header    */
  VPID vpid;			/* Volume and page identifiers */
  PAGE_PTR pgptr = NULL;	/* Page pointer               */

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return true;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  if (vhdr->purpose != DISK_PERMVOL_TEMP_PURPOSE)
    {
      /* Cannot reinitialize bitmaps of storage purposes other than temporary */
      pgbuf_unfix_and_init (thread_p, pgptr);
      return true;
    }

  /*
   * indicate
   * so to page buffer manager, so that fetches of new pages can be
   * initialized with temporary lsa..which will avoid logging.
   */
  pgbuf_cache_permanent_volume_for_temporary (volid);

  vhdr->free_pages = vhdr->total_pages - vhdr->sys_lastpage - 1;
  vhdr->free_sects = (vhdr->total_sects -
		      CEIL_PTVDIV ((vhdr->sys_lastpage + 1),
				   DISK_SECTOR_NPAGES));

  /*
   * Start allocating sectors a little bit away from the top of the volume.
   * This is done to allow system functions which allocate pages from the
   * special sector to find pages as close as possible. One example, is the
   * allocation of pages for system file table information. For now, five
   * sectors are skipped. Note that these sectors are allocated once the
   * hint of allocated sectors is looped.
   */

  if (vhdr->total_sects > DISK_HINT_START_SECT
      && (vhdr->total_sects - vhdr->free_sects) < DISK_HINT_START_SECT)
    {
      vhdr->hint_allocsect = DISK_HINT_START_SECT;
    }
  else
    {
      vhdr->hint_allocsect = vhdr->total_sects - 1;
    }

  /* Now initialize the sector and page allocator tables and link the volume
     to previous allocated volume */

  if (disk_map_init (thread_p, volid, vhdr->sect_alloctb_page1,
		     vhdr->sect_alloctb_page1 + vhdr->sect_alloctb_npages - 1,
		     vhdr->total_sects - vhdr->free_sects,
		     vhdr->purpose) != NO_ERROR
      || disk_map_init (thread_p, volid, vhdr->page_alloctb_page1,
			(vhdr->page_alloctb_page1 +
			 vhdr->page_alloctb_npages - 1),
			vhdr->sys_lastpage + 1, vhdr->purpose) != NO_ERROR)
    {
      (void) disk_verify_volume_header (thread_p, pgptr);

      /* Problems setting the map allocation tables, release the header page,
         dismount and destroy the volume, and return */
      pgbuf_unfix_and_init (thread_p, pgptr);
      return true;
    }
  else
    {
      (void) disk_verify_volume_header (thread_p, pgptr);

      pgbuf_set_lsa_as_temporary (thread_p, pgptr);
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return true;
}

/* TODO: check not use */
//#if 0
//extern int dk_change_magic (INT16 volid, const char *magic, bool logchange);
//
///*
// * dk_change_magic () - Change the magic string of the given volume to the
// *                      given magic string
// *   return:
// *   volid(in): Volume identifier
// *   magic(in): Magic string
// *   logchange(in): Whether or not to log the change
// *
// * Note: This function should be used only by the log and recovery manager.
// *       It is used when the volume is backed up.
// */
//int
//dk_change_magic (INT16 volid, const char *magic, bool logchange)
//{
//  DISK_VAR_HEADER *vhdr;
//  VPID vpid;
//  LOG_DATA_ADDR addr;
//
//  vpid.volid = volid;
//  vpid.pageid = DISK_VOLHEADER_PAGE;
//
//  addr.vfid = NULL;
//  addr.offset = 0;
//
//  addr.pgptr = pb_lock_and_fetch (&vpid, OLD_PAGE, X_LOCK);
//  if (addr.pgptr == NULL)
//    {
//      return ER_FAILED;
//    }
//  vhdr = (DISK_VAR_HEADER *) addr.pgptr;
//
//  if (logchange != false)
//    {
//      /* log the change */
//      log_append_undoredo_data (RVDK_MAGIC, &addr, CUBRID_MAGIC_MAX_LENGTH,
//                       CUBRID_MAGIC_MAX_LENGTH, vhdr->magic,
//                       magic);
//    }
//  else
//    {
//      log_skip_logging (&addr);
//    }
//
//  strncpy (vhdr->magic, magic, CUBRID_MAGIC_MAX_LENGTH);
//  pb_setdirty_free_and_unlock (addr.pgptr);
//  addr.pgptr = NULL;
//  return NO_ERROR;
//}
//#endif

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
disk_set_creation (THREAD_ENTRY * thread_p, INT16 volid,
		   const char *new_vol_fullname,
		   const INT64 * new_dbcreation,
		   const LOG_LSA * new_chkptlsa, bool logchange,
		   DISK_FLUSH_TYPE flush)
{
  DISK_VAR_HEADER *vhdr;
  LOG_DATA_ADDR addr;
  VPID vpid;
  DISK_RECV_CHANGE_CREATION *undo_recv;
  DISK_RECV_CHANGE_CREATION *redo_recv;

  if ((int) strlen (new_vol_fullname) + 1 > DB_MAX_PATH_LENGTH)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, NULL, new_vol_fullname,
	      (int) strlen (new_vol_fullname) + 1, DB_MAX_PATH_LENGTH);
      return ER_FAILED;
    }

  addr.vfid = NULL;
  addr.offset = 0;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  /* Do I need to log anything ? */
  if (logchange != false)
    {
      int undo_size, redo_size;

      undo_size = (sizeof (*undo_recv)
		   + (int) strlen (disk_vhdr_get_vol_fullname (vhdr)));
      undo_recv = (DISK_RECV_CHANGE_CREATION *) malloc (undo_size);
      if (undo_recv == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, undo_size);
	  goto error;
	}

      redo_size = sizeof (*redo_recv) + (int) strlen (new_vol_fullname);
      redo_recv = (DISK_RECV_CHANGE_CREATION *) malloc (redo_size);
      if (redo_recv == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, redo_size);
	  free_and_init (undo_recv);
	  goto error;
	}

      /* Undo stuff */
      memcpy (&undo_recv->db_creation, &vhdr->db_creation,
	      sizeof (vhdr->db_creation));
      memcpy (&undo_recv->chkpt_lsa, &vhdr->chkpt_lsa,
	      sizeof (vhdr->chkpt_lsa));
      (void) strcpy (undo_recv->vol_fullname,
		     disk_vhdr_get_vol_fullname (vhdr));

      /* Redo stuff */
      memcpy (&redo_recv->db_creation, new_dbcreation,
	      sizeof (*new_dbcreation));
      memcpy (&redo_recv->chkpt_lsa, new_chkptlsa, sizeof (*new_chkptlsa));
      (void) strcpy (redo_recv->vol_fullname, new_vol_fullname);

      log_append_undoredo_data (thread_p, RVDK_CHANGE_CREATION, &addr,
				undo_size, redo_size, undo_recv, redo_recv);
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
 *   next_volext_fullname(in): New volume label/name
 *   logchange(in): Whether or not to log the change
 *   flush(in):
 *
 * Note: No logging is intended for exclusive use by the log and recovery
 *       manager. It is used when a database is copied or renamed.
 */
int
disk_set_link (THREAD_ENTRY * thread_p, INT16 volid,
	       const char *next_volext_fullname, bool logchange,
	       DISK_FLUSH_TYPE flush)
{
  DISK_VAR_HEADER *vhdr;
  LOG_DATA_ADDR addr;
  VPID vpid;


  addr.vfid = NULL;
  addr.offset = 0;

  /* Get the header of the previous permanent volume */

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  /* Do I need to log anything ? */
  if (logchange == true)
    {
      log_append_undoredo_data (thread_p, RVDK_LINK_PERM_VOLEXT, &addr,
				(int)
				strlen (disk_vhdr_get_next_vol_fullname
					(vhdr)) + 1,
				(int) strlen (next_volext_fullname) + 1,
				disk_vhdr_get_next_vol_fullname (vhdr),
				next_volext_fullname);
    }
  else
    {
      log_skip_logging (thread_p, &addr);
    }

  /* Modify the header */
  if (disk_vhdr_set_next_vol_fullname (vhdr, next_volext_fullname) !=
      NO_ERROR)
    {
      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      return ER_FAILED;
    }

  /* Forcing the log here to be safer, especially in the case of
     permanent temp volumes. */
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

  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  log_append_undoredo_data (thread_p, RVDK_RESET_BOOT_HFID, &addr,
			    sizeof (vhdr->boot_hfid), sizeof (*hfid),
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
  VPID vpid;
  PAGE_PTR pgptr = NULL;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return NULL;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  HFID_COPY (hfid, &(vhdr->boot_hfid));

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return hfid;
}

/*
 * disk_get_link () - Find the name of the next permananet volume
 *                          extension
 *   return: next_volext_fullname or NULL in case of error
 *   volid(in): Volume identifier
 *   next_volext_fullname(out): Next volume extension
 *
 * Note: If there is none, next_volext_fullname is set to null string
 */
char *
disk_get_link (THREAD_ENTRY * thread_p, INT16 volid,
	       char *next_volext_fullname)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR pgptr = NULL;
  VPID vpid;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return NULL;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  strncpy (next_volext_fullname, disk_vhdr_get_next_vol_fullname (vhdr),
	   DB_MAX_PATH_LENGTH);

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return next_volext_fullname;
}

/*
 * disk_map_init () - Initialize the allocation map table
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   at_fpageid(in): First pageid of allocator map table
 *   at_lpageid(in): Last pageid of allocator map table
 *   nalloc_bits(in): Number of bits to set
 *   vol_purpose(in): The main purpose of the volume
 *
 * Note: Initialize the allocation map table which starts at at_fpageid and
 *       end at at_llpageid. The first nalloc_bits (units) of the allocation
 *       table are marked as allocated.
 */
static int
disk_map_init (THREAD_ENTRY * thread_p, INT16 volid, INT32 at_fpageid,
	       INT32 at_lpageid, INT32 nalloc_bits,
	       DISK_VOLPURPOSE vol_purpose)
{
  unsigned char *at_chptr;	/* Char Pointer to Sector/page allocator table */
  unsigned char *out_chptr;	/* Outside of page */
  VPID vpid;
  LOG_DATA_ADDR addr;
  int i;


  addr.vfid = NULL;
  addr.offset = 0;

  vpid.volid = volid;

  /* One page at a time */
  for (vpid.pageid = at_fpageid; vpid.pageid <= at_lpageid; vpid.pageid++)
    {
      addr.pgptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  return ER_FAILED;
	}

      (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_VOLBITMAP);

      /* If this is a volume with temporary purposes, we do not log any bitmap
         changes. Indicate that by setting the disk pages to temporary lsa */

      if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE
	  || vol_purpose == DISK_PERMVOL_TEMP_PURPOSE)
	{
	  pgbuf_set_lsa_as_temporary (thread_p, addr.pgptr);
	}

      /*
       * Initialize the page to zeros, and allocate the needed bits for the
       * pages or sectors. The nalloc_bits are usually gatherd from the first
       * page of the allocation table
       */

      disk_set_page_to_zeros (thread_p, addr.pgptr);

      /* One byte at a time */
      out_chptr = (unsigned char *) addr.pgptr + DB_PAGESIZE;
      for (at_chptr = (unsigned char *) addr.pgptr;
	   nalloc_bits > 0 && at_chptr < out_chptr; at_chptr++)
	{
	  /* One bit at a time */
	  for (i = 0; nalloc_bits > 0 && i < CHAR_BIT; i++, nalloc_bits--)
	    {
	      disk_bit_set (at_chptr, i);
	    }
	}

      /*
       *  Log the data and set the page as dirty
       *
       * UNDO data is NOT NEEDED since it is the initialization process, during
       * the creation of the volume. If we rollback the whole volume goes.
       *
       */

      log_append_redo_data (thread_p, RVDK_INITMAP, &addr,
			    sizeof (nalloc_bits), &nalloc_bits);
      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  return NO_ERROR;
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
disk_set_checkpoint (THREAD_ENTRY * thread_p, INT16 volid,
		     const LOG_LSA * log_chkpt_lsa)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  LOG_DATA_ADDR addr;

  addr.pgptr = NULL;
  addr.vfid = NULL;
  addr.offset = 0;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in exclusive mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  vhdr->chkpt_lsa.pageid = log_chkpt_lsa->pageid;
  vhdr->chkpt_lsa.offset = log_chkpt_lsa->offset;

  /* Set dirty and unlock the page */
#if 0
  (void) pgbuf_flush_all_unfixed (volid);
#endif

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
  VPID vpid;
  PAGE_PTR hdr_pgptr = NULL;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in exclusive mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

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
disk_get_creation_time (THREAD_ENTRY * thread_p, INT16 volid,
			INT64 * db_creation)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  PAGE_PTR hdr_pgptr = NULL;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /* The creation of a volume does not change. Therefore, we do not lock
     the page. */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

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
  DISK_VOLPURPOSE purpose;

  xdisk_get_purpose_and_space_info (thread_p, volid, &purpose, NULL);
  return purpose;
}

/*
 *
 *
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
VOLID
xdisk_get_purpose_and_space_info (THREAD_ENTRY * thread_p,
				  VOLID volid, DISK_VOLPURPOSE * vol_purpose,
				  VOL_SPACE_INFO * space_info)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;
  INT32 total_pages, free_pages;

  if (space_info != NULL)
    {
      space_info->total_pages = -1;
      space_info->free_pages = -1;
      space_info->max_pages = -1;
      space_info->used_data_npages = -1;
      space_info->used_index_npages = -1;
      space_info->used_temp_npages = -1;
    }

  if (volid != NULL_VOLID)
    {
      vpid.volid = volid;
      vpid.pageid = DISK_VOLHEADER_PAGE;

      hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     PGBUF_UNCONDITIONAL_LATCH);
      if (hdr_pgptr == NULL)
	{
	  *vol_purpose = DISK_UNKNOWN_PURPOSE;
	  return NULL_VOLID;
	}

      (void) disk_verify_volume_header (thread_p, hdr_pgptr);

      vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

      *vol_purpose = vhdr->purpose;
      if (space_info != NULL)
	{
	  space_info->max_pages = vhdr->max_npages;
	  space_info->total_pages = vhdr->total_pages;
	  space_info->free_pages = vhdr->free_pages;
	  space_info->used_data_npages = vhdr->used_data_npages;
	  space_info->used_index_npages = vhdr->used_index_npages;

	  if (vhdr->purpose == DISK_PERMVOL_TEMP_PURPOSE
	      || vhdr->purpose == DISK_TEMPVOL_TEMP_PURPOSE)
	    {
	      space_info->used_temp_npages =
		vhdr->total_pages - vhdr->free_pages - (vhdr->sys_lastpage +
							1);
	    }
	  else
	    {
	      space_info->used_temp_npages = 0;
	    }
	}

      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }
  else
    {
      *vol_purpose = DISK_UNKNOWN_PURPOSE;
      (void) disk_get_all_total_free_numpages (thread_p, *vol_purpose, &volid,
					       &total_pages, &free_pages);
      *vol_purpose = DISK_PERMVOL_GENERIC_PURPOSE;
      if (space_info != NULL)
	{
	  space_info->total_pages = total_pages;
	  space_info->free_pages = free_pages;
	}
    }

  return volid;
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
xdisk_get_purpose_and_sys_lastpage (THREAD_ENTRY * thread_p, INT16 volid,
				    DISK_VOLPURPOSE * vol_purpose,
				    INT32 * sys_lastpage)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /* The purpose of a volume does not change, so we do not lock the page */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return NULL_VOLID;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

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
  DISK_VOLPURPOSE ignore_purpose;
  VOL_SPACE_INFO space_info;

  xdisk_get_purpose_and_space_info (thread_p, volid, &ignore_purpose,
				    &space_info);

  return space_info.total_pages;
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
  DISK_VOLPURPOSE ignore_purpose;
  VOL_SPACE_INFO space_info;

  xdisk_get_purpose_and_space_info (thread_p, volid, &ignore_purpose,
				    &space_info);

  return space_info.free_pages;
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
  VPID vpid;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * The total number of sectors for a volume does not change. Therefore,
   * we do need to lock the header page since the field does not change.
   *
   * The above is not quite true for temporary volumes, but it is OK to
   * not lock the page, since we were going to unlock the page at the end
   * anyhow.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return -1;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  total_sects = vhdr->total_sects;

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return total_sects;
}

/* TODO: check not use */
//#if 0
//extern INT32 dk_free_sects (INT16 volid);
///*
// * dk_free_sects () - Return the number of free sectors for the given volume
// *   return: Number of free sectors
// *   volid(in): Permanent volume identifier
// *
// * Note: The free number of pages should be taken as an approximation by the
// *       caller since we do not leave the page locked after the inquire.
// *       That is, someone else can allocate pages.
// */
//INT32
//dk_free_sects (INT16 volid)
//{
//  DISK_VAR_HEADER *vhdr;
//  PAGE_PTR hdr_pgptr = NULL;
//  INT32 free_sects;
//  VPID vpid;
//
//  vpid.volid = volid;
//  vpid.pageid = DISK_VOLHEADER_PAGE;
//
//  hdr_pgptr = pb_lock_and_fetch (&vpid, OLD_PAGE, S_LOCK);
//  if (hdr_pgptr == NULL)
//    {
//      return -1;
//    }
//
//  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;
//  free_sects = vhdr->free_sects;
//
//  pgbuf_unfix_and_init (thread_p, hdr_pgptr);
//
//  return free_sects;
//}
//
//extern void dk_free_pgs_sects (INT16 volid, INT32 * free_pages,
//                             INT32 * free_sects);
//
///*
// * dk_free_pgs_sects () - Find the number of free pages and sectors of the
// *                        given volume
// *   return: void
// *   volid(in): Permanent volume identifier
// *   free_pages(out): Number of free pages
// *   free_sects(out): Number of free sectors
// *
// * Note: The free number of pages should be taken as an approximation by the
// *       caller since we do not leave the page locked after the inquire.
// *       That is, someone else can allocate pages.
// */
//void
//dk_free_pgs_sects (INT16 volid, INT32 * free_pages, INT32 * free_sects)
//{
//  DISK_VAR_HEADER *vhdr;
//  PAGE_PTR hdr_pgptr = NULL;
//  VPID vpid;
//
//  vpid.volid = volid;
//  vpid.pageid = DISK_VOLHEADER_PAGE;
//
//  hdr_pgptr = pb_lock_and_fetch (&vpid, OLD_PAGE, S_LOCK);
//  if (hdr_pgptr == NULL)
//    {
//      *free_pages = -1;
//      *free_sects = -1;
//    }
//  else
//    {
//      vhdr = (DISK_VAR_HEADER *) hdr_pgptr;
//      *free_pages = vhdr->free_pages;
//      *free_sects = vhdr->free_sects;
//
//      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
//    }
//}
//#endif

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
  VPID vpid;

  if (vol_fullname == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return NULL;
    }

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * The name of a volume does not change unless we are running the copydb
   * utility, but this is a standalone utility. Therefore, we do need to
   * lock the header page since the field does not change.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      *vol_fullname = '\0';
      return NULL;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  strncpy (vol_fullname, disk_vhdr_get_vol_fullname (vhdr),
	   DB_MAX_PATH_LENGTH);

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
  VPID vpid;
  char *remarks;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in shared mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return NULL;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  remarks = (char *) malloc ((int) strlen (disk_vhdr_get_vol_remarks (vhdr))
			     + 1);
  if (remarks != NULL)
    {
      strcpy (remarks, disk_vhdr_get_vol_remarks (vhdr));
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return remarks;
}

/* TODO: check not use */
//#if 0
//extern void dk_warnspace (INT16 volid);
//
///*
// * dk_warnspace () - Display a warning if volume is close to running out of
// *                   space
// *   return: void
// *   volid(in): Permanent volume identifier. If NULL_VOLID is given, it means
// *              combine all the space of all volumes...and try it as one big
// *              volume.
// *
// * Note: If NULL_VOLID is given, it display warning only if the combined total
// *       space for all volumes is running out of space. A lock is not acquired
// *       on the volume header.
// */
//void
//dk_warnspace (INT16 volid)
//{
//  DISK_VAR_HEADER *vhdr;
//  PAGE_PTR hdr_pgptr = NULL;
//  VPID vpid;
//
//
//  if (volid != NULL_VOLID)
//    {
//      vpid.volid = volid;
//      vpid.pageid = DISK_VOLHEADER_PAGE;
//
//      hdr_pgptr = pb_lock_and_fetch (&vpid, OLD_PAGE, S_LOCK);
//      if (hdr_pgptr != NULL)
//      {
//        vhdr = (DISK_VAR_HEADER *) hdr_pgptr;
//        if (vhdr->free_pages < vhdr->total_pages * vhdr->warn_ratio)
//          {
//            er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
//                    ER_DISK_ALMOST_OUT_OF_SPACE, 3,
//                    fileio_get_volume_label (volid), vhdr->total_pages, vhdr->free_pages);
//          }
//        pgbuf_unfix_and_init (thread_p, hdr_pgptr);
//      }
//    }
//  else
//    {
//      dk_warnspace_by_purpose (DISK_UNKNOWN_PURPOSE);
//    }
//}
//#endif

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
disk_alloc_sector (THREAD_ENTRY * thread_p, INT16 volid, INT32 nsects,
		   int exp_npages)
{
  DISK_VAR_HEADER *vhdr;
  INT32 alloc_sect;
  VPID vpid;
  LOG_DATA_ADDR addr;
  DKNPAGES undo_data, redo_data;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  addr.vfid = NULL;

  /*
   * Lock the volume header in exclusive mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return DISK_SECTOR_WITH_ALL_PAGES;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  /*
   * If there are not any more sectors availables, share the whole volume.
   * If the number of free pages is less than the number of pages in a sector,
   * share the whole volume. That is, allocate the special sector.
   */

  if (vhdr->free_sects < nsects || vhdr->free_pages < vhdr->sect_npgs)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      return DISK_SECTOR_WITH_ALL_PAGES;
    }

  /* Use the hint to start looking for the next sector */
  alloc_sect = disk_id_alloc (thread_p, volid, vhdr, nsects,
			      vhdr->hint_allocsect, vhdr->total_sects - 1,
			      DISK_SECTOR, exp_npages, NULL_PAGEID);
  if (alloc_sect == NULL_SECTID)
    {
      alloc_sect = disk_id_alloc (thread_p, volid, vhdr, nsects,
				  1, vhdr->hint_allocsect - 1, DISK_SECTOR,
				  exp_npages, NULL_PAGEID);
    }

  if (alloc_sect == NULL_SECTID)
    {
      alloc_sect = DISK_SECTOR_WITH_ALL_PAGES;

      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }
  else
    {
      /* Set hint for next sector to allocate and subtract the number of free
         sectors */
      vhdr->hint_allocsect = ((alloc_sect + nsects >= vhdr->total_sects)
			      ? 1 : alloc_sect + nsects);
      vhdr->free_sects -= nsects;

      /*
       * Log the number of allocated sectors. Note the hint for next allocated
       * sector is not logged (i.e., it is not fixed during undo/redo). It is
       * only a hint. Note that we cannot log the value of free_sects since it
       * can be modified concurrently by other transactions, thus the undo/redo
       * must be executed through an operation
       */
      addr.offset = 0;		/* Header is located at offset zero */
      undo_data = nsects;
      redo_data = -nsects;
      log_append_undoredo_data (thread_p, RVDK_VHDR_SCALLOC, &addr,
				sizeof (undo_data), sizeof (redo_data),
				&undo_data, &redo_data);

      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;
    }

  return alloc_sect;
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
 *   search_wrap_around(in): if true,
 *                           search for page(s) from the beginning of the volume
 *			     when the new page(s) could not be allocated
 *
 * Note: This function allocates the closest "npages" contiguous free pages to
 *       the "near_pageid" page in the "Sector-id" sector of the given volume.
 *       If there are not enough "npages" contiguous free pages, a NULL_PAGEID
 *       is returned and an error condition code is flaged.
 */
INT32
disk_alloc_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 sectid,
		 INT32 npages, INT32 near_pageid, bool search_wrap_around,
		 DISK_PAGE_TYPE alloc_page_type)
{
  DISK_VAR_HEADER *vhdr;
  INT32 fpageid;
  INT32 lpageid;
  INT32 new_pageid;
  INT32 skip_pageid;
  VPID vpid;
  LOG_DATA_ADDR addr;
  DISK_VOLPURPOSE vol_purpose;
  DISK_RECV_MTAB_BITS_WITH undo_data, redo_data;
  bool need_to_add_generic_volume;

#if defined(CUBRID_DEBUG)
  if (npages <= 0)
    {
      er_log_debug (ARG_FILE_LINE,
		    "dk_pgalloc: ** SYSTEM_ERROR.. Bad interface"
		    " trying to allocate %d pages", npages);
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL_PAGEID;
    }
#endif /* CUBRID_DEBUG */

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  addr.vfid = NULL;

  /*
   * Lock the volume header in exclusive mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			  PGBUF_UNCONDITIONAL_LATCH);
  if (addr.pgptr == NULL)
    {
      return NULL_PAGEID;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  vol_purpose = vhdr->purpose;

  if (sectid < 0 || sectid > vhdr->total_sects
#if defined(CUBRID_DEBUG)
      || (disk_id_isvalid (volid, vhdr->sect_alloctb_page1, sectid)
	  == DISK_INVALID)
#endif /* CUBRID_DEBUG */
    )
    {
      /* Unknown sector identifier. Assume DISK_SECTOR_WITH_ALL_PAGES */
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DISK_UNKNOWN_SECTOR, 2,
	      sectid, fileio_get_volume_label (volid, PEEK));
      sectid = DISK_SECTOR_WITH_ALL_PAGES;
    }

  /* If this volume does not have enough pages to allocate, we just give up
   * here. The caller will add a new volume or pick another volume, and then
   * retry to allocate pages.
   */
  if (vhdr->free_pages < npages)
    {
      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      return NULL_PAGEID;
    }

  /* Find the first page and last page of the given sector */
  if (sectid == DISK_SECTOR_WITH_ALL_PAGES)
    {
      /* Allocate pages from any place, even across sectors */
      fpageid = vhdr->sys_lastpage + 1;
      lpageid = vhdr->total_pages - 1;
    }
  else
    {
      fpageid = sectid * vhdr->sect_npgs;
      if (sectid + 1 == vhdr->total_sects)
	{
	  lpageid = vhdr->total_pages - 1;
	}
      else
	{
	  lpageid = fpageid + vhdr->sect_npgs - 1;
	}
    }

  skip_pageid = near_pageid;
  if (sectid == DISK_SECTOR_WITH_ALL_PAGES && near_pageid == NULL_PAGEID)
    {
      near_pageid = DISK_HINT_START_SECT * DISK_SECTOR_NPAGES;

      /* For not better estimate assume that the allocated pages are in
         the front of the disk. */
      if (near_pageid < (vhdr->total_pages - vhdr->free_pages))
	{
	  near_pageid = vhdr->total_pages - vhdr->free_pages - 1;
	}
    }

  /* If the near_page is out of bounds, assume the first page of the sector as
     the near page */
  if (near_pageid == NULL_PAGEID || near_pageid < fpageid
      || (search_wrap_around == true && (near_pageid + npages > lpageid)))
    {
      near_pageid = fpageid;
    }


  /*
   * First look at the pages after near_pageid
   *
   * skip near_pageid itself because it's used already.
   * Normally it's marked in disk bitmap so it's not chosen.
   * But in abnormal case it could be not marked
   * (disk & file allocset mismatch)
   */
  new_pageid = disk_id_alloc (thread_p, volid, vhdr, npages, near_pageid,
			      lpageid, DISK_PAGE, -1, skip_pageid);

  if (new_pageid == NULL_PAGEID && near_pageid != fpageid
      && search_wrap_around == true)
    {
      /* Try again from the beginning of the sector. Include the near_pageid
         for multiple pages */

      lpageid = near_pageid + npages - 2;
      new_pageid = disk_id_alloc (thread_p, volid, vhdr, npages, fpageid,
				  lpageid, DISK_PAGE, -1, NULL_PAGEID);
    }

  if (new_pageid == NULL_PAGEID)
    {
      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      if (sectid != DISK_SECTOR_WITH_ALL_PAGES)
	{
	  new_pageid = DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES;
	}
    }
  else
    {
      vhdr->free_pages -= npages;
      if (vhdr->purpose == DISK_PERMVOL_GENERIC_PURPOSE)
	{
	  if (alloc_page_type == DISK_PAGE_DATA_TYPE)
	    {
	      vhdr->used_data_npages += npages;
	    }
	  else if (alloc_page_type == DISK_PAGE_INDEX_TYPE)
	    {
	      vhdr->used_index_npages += npages;
	    }
	  else
	    {
	      assert_release (alloc_page_type == DISK_PAGE_DATA_TYPE
			      || alloc_page_type == DISK_PAGE_INDEX_TYPE);
	    }
	}
      else if (vhdr->purpose == DISK_PERMVOL_DATA_PURPOSE)
	{
	  assert_release (alloc_page_type == DISK_PAGE_DATA_TYPE);
	  vhdr->used_data_npages += npages;
	}
      else if (vhdr->purpose == DISK_PERMVOL_INDEX_PURPOSE)
	{
	  assert_release (alloc_page_type == DISK_PAGE_INDEX_TYPE);
	  vhdr->used_index_npages += npages;
	}

      if (sectid == DISK_SECTOR_WITH_ALL_PAGES
	  && (vhdr->hint_allocsect >= (new_pageid / vhdr->sect_npgs))
	  && (vhdr->hint_allocsect <=
	      ((new_pageid + npages) / vhdr->sect_npgs)))
	{
	  /*
	   * Special sector stole some pages from next hinted sector to
	   * allocate.
	   * Avoid, allocating this sector unless there are no more sectors.
	   * Note it can be allocated... we just try to avoid it
	   */
	  vhdr->hint_allocsect = (((new_pageid + npages) / vhdr->sect_npgs)
				  + 1);
	  if (vhdr->hint_allocsect > vhdr->total_sects)
	    {
	      vhdr->hint_allocsect = 1;
	    }
	}

      /*
       * Log the number of allocated pages. Note the hint for next allocated
       * sector and the warning at fields are not logged (i.e., they are not
       * fixed during undo/redo). They are used only for hints. They are fixed
       * automatically during normal execution. Note that we cannot log the
       * value of free_pages since it can be modified concurrently by other
       * transactions, thus the undo/redo must be executed through a logical
       * operation
       */
      addr.offset = 0;		/* Header is located at offset zero */
      undo_data.start_bit = 0;	/* not used */
      undo_data.num = npages;
      undo_data.deallid_type = DISK_PAGE;
      undo_data.page_type = alloc_page_type;

      redo_data.start_bit = 0;
      redo_data.num = -npages;
      redo_data.deallid_type = DISK_PAGE;
      redo_data.page_type = alloc_page_type;

      log_append_undoredo_data (thread_p, RVDK_VHDR_PGALLOC, &addr,
				sizeof (undo_data), sizeof (redo_data),
				&undo_data, &redo_data);

      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      /* Update free_pages on disk_Cache. */
      need_to_add_generic_volume = false;
      disk_cache_goodvol_update (thread_p, volid, vol_purpose, -npages,
				 false, &need_to_add_generic_volume);

      pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
      addr.pgptr = NULL;

      if (need_to_add_generic_volume)
	{
	  (void) boot_add_auto_volume_extension (thread_p, 1,
						 DISK_CONTIGUOUS_PAGES,
						 DISK_PERMVOL_GENERIC_PURPOSE,
						 false);
	}
    }

#if defined(CUBRID_DEBUG)
  if (new_pageid != NULL_PAGEID
      && new_pageid != DISK_NULL_PAGEID_WITH_ENOUGH_DISK_PAGES)
    {
      disk_scramble_newpages (volid, new_pageid, npages, vol_purpose);
    }
#endif /* CUBRID_DEBUG */

  return new_pageid;
}

#if defined(CUBRID_DEBUG)
/*
 * disk_scramble_newpages () - Scramble the content of new pages
 *   return: void
 *   volid(in): Permanent volume identifier
 *   first_pageid(in): First page to scramble
 *   npages(in): Number of pages
 *   vol_purpose(in): Storage purpose of pages
 *
 * Note: This is done for debugging reasons to make sure that caller of the
 *       new pages does not assume pages initialized to zero.
 */
static void
disk_scramble_newpages (INT16 volid, INT32 first_pageid, INT32 npages,
			DISK_VOLPURPOSE vol_purpose)
{
  const char *env_value;
  static int scramble = -1;
  VPID vpid;
  LOG_DATA_ADDR addr;
  int i;

  if (scramble == -1)
    {
      /* Make sure that the user of the system allow us to scramble the
         newly allocated pages. */
      if ((env_value = envvar_get ("DK_DEBUG_SCRAMBLE_NEWPAGES")) != NULL)
	{
	  scramble = atoi (env_value) != 0 ? 1 : 0;
	}
      else
	{
	  scramble = 1;
	}
    }

  if (scramble == 0)
    {
      return;
    }

  addr.vfid = NULL;
  addr.offset = 0;

  vpid.volid = volid;
  vpid.pageid = first_pageid;

  for (i = 0; i < npages; i++)
    {
      addr.pgptr = pgbuf_fix (thread_p, &vpid, NEW_PAGE, PGBUF_LATCH_WRITE,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr != NULL)
	{
	  (void) pgbuf_set_page_ptype (thread_p, addr.pgptr, PAGE_UNKNOWN);

	  memset (addr.pgptr, MEM_REGION_SCRAMBLE_MARK, DB_PAGESIZE);
	  /* The following is needed since the file manager may have set
	     a page as a temporary one */
	  if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE)
	    {
	      pgbuf_set_lsa_as_temporary (addr.pgptr);
	    }
	  else
	    {
	      pgbuf_set_lsa_as_permanent (addr.pgptr);
	    }

	  log_skip_logging (&addr);
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	  vpid.pageid++;
	}
    }
}
#endif /* CUBRID_DEBUG */

/*
 * disk_id_alloc () - Allocate N units from the given allocation bitmap table
 *   return: Unit (i.e., Page/sector) identifier
 *   volid(in): Permanent volume identifier
 *   vhdr(in): Volume header
 *   nalloc(in): Number of pages/sectors to allocate
 *   low_allid(in): First possible allocation page/sector
 *   high_allid(in): Last possible allocation page/sector
 *   allid_type(in): Unit type (SECTOR or PAGE)
 *   exp_npages(in): Expected pages that sector will have
 *   skip_pageid(in): skip pageid (this function will not allocate this pageid)
 *
 * Note: The "nalloc" units should be allocated from "low_allid" to
 *       "high_allid".
 */
static INT32
disk_id_alloc (THREAD_ENTRY * thread_p, INT16 volid, DISK_VAR_HEADER * vhdr,
	       INT32 nalloc, INT32 low_allid, INT32 high_allid,
	       int allid_type, int exp_npages, int skip_pageid)
{
  int i;
  INT32 nfound = 0;		/* Number of contiguous allocation
				   pages/sectors */
  INT32 allid = NULL_PAGEID;	/* The founded page/sector */
  INT32 at_pg1;			/* First page of PAT/SAT page */
  unsigned char *at_chptr;	/* Pointer to character of Sector/page
				   allocator table */
  unsigned char *out_chptr;	/* Outside of page */
  char *logdata;		/* Pointer to data to log */
  VPID vpid;
  LOG_DATA_ADDR addr;
  DISK_RECV_MTAB_BITS recv;

  assert (allid_type == DISK_SECTOR || allid_type == DISK_PAGE);

  addr.vfid = NULL;
  vpid.volid = volid;

  if (allid_type == DISK_SECTOR)
    {
      at_pg1 = vhdr->sect_alloctb_page1;
    }
  else
    {
      at_pg1 = vhdr->page_alloctb_page1;
    }

  /* One allocation table page at a time */
  for (vpid.pageid = (low_allid / DISK_PAGE_BIT) + at_pg1;
       nfound < nalloc && low_allid <= high_allid; vpid.pageid++)
    {
      addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  nfound = 0;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_VOLBITMAP);

      /* One byte at a time */
      addr.offset = ((low_allid - (vpid.pageid - at_pg1) * DISK_PAGE_BIT)
		     / CHAR_BIT);
      out_chptr = (unsigned char *) addr.pgptr + DB_PAGESIZE;
      for (at_chptr = (unsigned char *) addr.pgptr + addr.offset;
	   nfound < nalloc && low_allid <= high_allid && at_chptr < out_chptr;
	   at_chptr++)
	{
	  /* One bit at a time */
	  for (i = low_allid % CHAR_BIT;
	       i < CHAR_BIT && nfound < nalloc && low_allid <= high_allid;
	       i++, low_allid++)
	    {
	      if (!disk_bit_is_set (at_chptr, i) && low_allid != skip_pageid)
		{
		  if (allid == NULL_PAGEID)
		    {
		      allid = low_allid;
		    }

		  nfound++;
		}
	      else
		{
		  /*
		   * There is not contiguous pages
		   * try next pageid
		   */
		  nfound = 0;
		  allid = NULL_PAGEID;

		  continue;
		}

	      /* Checking that sector has enough pages for following page allocation. */
	      if (allid_type == DISK_SECTOR && nalloc == 1 && nfound == 1
		  && exp_npages > 0 && allid > DISK_SECTOR_WITH_ALL_PAGES)
		{
		  int fpageid, lpageid;

		  fpageid = allid * vhdr->sect_npgs;
		  if (allid + 1 == vhdr->total_sects)
		    {
		      lpageid = vhdr->total_pages - 1;
		    }
		  else
		    {
		      lpageid = fpageid + vhdr->sect_npgs - 1;
		    }

		  if (disk_check_sector_has_npages (thread_p, volid,
						    vhdr->page_alloctb_page1,
						    fpageid, lpageid,
						    exp_npages) == false)
		    {
		      nfound = 0;
		      allid = NULL_PAGEID;
		    }
		}
	    }
	}

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  /* Now set the bits for the allocated pages */
  if (nfound == nalloc)
    {
      /* Set the bits for the identifier of the pages/sectors being allocated. */
      /* One allocation table page at a time */

      low_allid = allid;
      /* One map table page at a time */
      for (vpid.pageid = (low_allid / DISK_PAGE_BIT) + at_pg1;
	   low_allid < allid + nalloc; vpid.pageid++)
	{
	  addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH);
	  if (addr.pgptr == NULL)
	    {
	      allid = NULL_PAGEID;
	      break;
	    }

	  (void) pgbuf_check_page_ptype (thread_p, addr.pgptr,
					 PAGE_VOLBITMAP);

	  /* One byte at a time */
	  addr.offset = ((low_allid - (vpid.pageid - at_pg1) * DISK_PAGE_BIT)
			 / CHAR_BIT);
	  recv.start_bit = low_allid % CHAR_BIT;
	  recv.num = 0;

	  out_chptr = (unsigned char *) addr.pgptr + DB_PAGESIZE;
	  logdata = (char *) addr.pgptr + addr.offset;
	  for (at_chptr = (unsigned char *) logdata;
	       low_allid < allid + nalloc && at_chptr < out_chptr; at_chptr++)
	    {
	      /* One bit at a time */
	      for (i = low_allid % CHAR_BIT;
		   i < CHAR_BIT && low_allid < allid + nalloc;
		   i++, low_allid++)
		{
		  recv.num++;
		  disk_bit_set (at_chptr, i);
		}
	    }
	  /*
	   * Log by bits instead of bytes since bytes in the allocation table
	   * are updated by several concurrent transactions. Thus, undo/redo
	   * must be executed by a logical operation.
	   */
	  log_append_undoredo_data (thread_p, RVDK_IDALLOC, &addr,
				    sizeof (recv), sizeof (recv), &recv,
				    &recv);
	  pgbuf_set_dirty (thread_p, addr.pgptr, FREE);
	  addr.pgptr = NULL;
	}
    }
  else
    {
      allid = NULL_PAGEID;
    }

  return allid;
}

/*
 * disk_check_sector_has_npages () - Check sector has N pages
 *   return: TRUE if sector has more than N pages, else FALSE
 *   volid(in): Permanent volume identifier
 *   at_pg1(in): First page of PAT/SAT page
 *   low_allid(in): First possible allocation page/sector
 *   high_allid(in): Last possible allocation page/sector
 *   exp_npages(in): expected pages that sector will have
 */
static bool
disk_check_sector_has_npages (THREAD_ENTRY * thread_p, INT16 volid,
			      INT32 at_pg1, INT32 low_allid, INT32 high_allid,
			      int exp_npages)
{
  int i;
  int nfound = 0;		/* Number of contiguous allocation pages */
  unsigned char *at_chptr;	/* Pointer to page allocator table */
  unsigned char *out_chptr;	/* Outside of page */
  VPID vpid;
  LOG_DATA_ADDR addr;

  addr.vfid = NULL;
  vpid.volid = volid;

  /* One allocation table page at a time */
  for (vpid.pageid = (low_allid / DISK_PAGE_BIT) + at_pg1;
       nfound < exp_npages && low_allid <= high_allid; vpid.pageid++)
    {
      addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			      PGBUF_UNCONDITIONAL_LATCH);
      if (addr.pgptr == NULL)
	{
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_VOLBITMAP);

      /* One byte at a time */
      addr.offset = ((low_allid - (vpid.pageid - at_pg1) * DISK_PAGE_BIT)
		     / CHAR_BIT);
      out_chptr = (unsigned char *) addr.pgptr + DB_PAGESIZE;

      for (at_chptr = (unsigned char *) addr.pgptr + addr.offset;
	   nfound < exp_npages && low_allid <= high_allid
	   && at_chptr < out_chptr; at_chptr++)
	{
	  /* One bit at a time */
	  for (i = low_allid % CHAR_BIT;
	       i < CHAR_BIT && nfound < exp_npages && low_allid <= high_allid;
	       i++, low_allid++)
	    {
	      if (!disk_bit_is_set (at_chptr, i))
		{
		  nfound++;
		}
	      else
		{
		  /* There is not contiguous pages */
		  nfound = 0;
		}
	    }
	}

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  return nfound >= exp_npages;
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
disk_dealloc_sector (THREAD_ENTRY * thread_p, INT16 volid, INT32 sectid,
		     INT32 nsects)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  LOG_DATA_ADDR addr;
  int retry = 0;
  int ret = NO_ERROR;

  addr.vfid = NULL;
  addr.pgptr = NULL;


  /* Sector zero is never deallocated. It is always assigned to the system */
  if (sectid == DISK_SECTOR_WITH_ALL_PAGES)
    {
      if (nsects > 1)
	{
	  sectid++;
	  nsects--;
	}
      else
	{
	  return NO_ERROR;
	}
    }

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in exclusive mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  while ((addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH)) == NULL)
    {
      switch (er_errid ())
	{
	case NO_ERROR:
	case ER_INTERRUPTED:
	  continue;
	case ER_LK_UNILATERALLY_ABORTED:
	case ER_LK_PAGE_TIMEOUT:
	case ER_PAGE_LATCH_TIMEDOUT:
	  retry++;
	  break;
	default:
	  goto exit_on_error;
	}
      if (retry > 10)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_PAGE_LATCH_ABORTED, 2, vpid.volid, vpid.pageid);
	  goto exit_on_error;
	}
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  if (sectid < 0)
    {
      for (; sectid < 0 && nsects > 0; sectid++, nsects--)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DISK_UNKNOWN_SECTOR,
		  2, sectid, fileio_get_volume_label (volid, PEEK));
	}

      if (nsects <= 0)
	{
	  goto exit_on_error;
	}
    }

  if (sectid + nsects > vhdr->total_sects)
    {
#if defined(CUBRID_DEBUG)
      INT32 bad_sectid;

      for (bad_sectid = vhdr->total_sects,
	   nsects -= vhdr->total_sects - sectid;
	   nsects > 0; nsects--, bad_sectid++)
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_DISK_UNKNOWN_SECTOR,
		  2, bad_sectid, fileio_get_volume_label (volid, PEEK));
	}
#endif /* CUBRID_DEBUG */

      /* Deallocate the rest of the good sectors */
      nsects = vhdr->total_sects - sectid;
      if (nsects <= 0)
	{
	  goto exit_on_error;
	}
    }

  /* If there is not any error update the header page too.
   * DISK_PAGE_UNKNOWN_TYPE is passed, because
   * the page info of volume header is not changed,
   */
  nsects =
    disk_id_dealloc (thread_p, volid, vhdr->sect_alloctb_page1, sectid,
		     nsects, DISK_SECTOR, DISK_PAGE_UNKNOWN_TYPE);
  if (nsects <= 0)
    {
      goto exit_on_error;
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  /* To sync volume header and page bitmap, use RVDK_IDDEALLOC_WITH_VOLHEADER.
     See disk_id_dealloc() function */
  pgbuf_unfix_and_init (thread_p, addr.pgptr);

  return ret;

exit_on_error:

  if (addr.pgptr)
    {
      (void) disk_verify_volume_header (thread_p, addr.pgptr);

      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  if (ret == NO_ERROR)
    {
      ret = ER_FAILED;
    }

  return ret;
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
disk_dealloc_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 pageid,
		   INT32 npages, DISK_PAGE_TYPE page_type)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  LOG_DATA_ADDR addr;
  int retry = 0;

  addr.vfid = NULL;
  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in exclusive mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  while ((addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				  PGBUF_LATCH_WRITE,
				  PGBUF_UNCONDITIONAL_LATCH)) == NULL)
    {
      switch (er_errid ())
	{
	case NO_ERROR:
	case ER_INTERRUPTED:
	  continue;
	case ER_LK_UNILATERALLY_ABORTED:
	case ER_LK_PAGE_TIMEOUT:
	case ER_PAGE_LATCH_TIMEDOUT:
	  retry++;
	  break;
	default:
	  return ER_FAILED;
	}
      if (retry > 10)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_PAGE_LATCH_ABORTED, 2, vpid.volid, vpid.pageid);
	  return ER_FAILED;
	}
    }

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  vhdr = (DISK_VAR_HEADER *) addr.pgptr;

  if (pageid <= vhdr->sys_lastpage && pageid >= DISK_VOLHEADER_PAGE)
    {
      /* System error.. trying to deallocate a system page */
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_DISK_TRY_DEALLOC_DISK_SYSPAGE, 2, pageid,
	      fileio_get_volume_label (volid, PEEK));
      return ER_FAILED;
    }

  if (pageid < DISK_VOLHEADER_PAGE || pageid >= vhdr->total_pages)
    {
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DISK_UNKNOWN_PAGE, 2,
	      pageid, fileio_get_volume_label (volid, PEEK));
      return ER_FAILED;
    }

  /* If there is not any error update the header page too */
  npages = disk_id_dealloc (thread_p, volid, vhdr->page_alloctb_page1,
			    pageid, npages, DISK_PAGE, page_type);

  (void) disk_verify_volume_header (thread_p, addr.pgptr);

  pgbuf_unfix_and_init (thread_p, addr.pgptr);

  if (npages > 0)
    {
      /*
       * PAGES ARE EITHER DEALLOCATED UNTIL THE END OF THE TRANSACTION. Thus,
       * an UNDO OPERATION IS NOT NEEDED.
       *
       * The number of deallocated pages is logged. Note that we cannot log the
       * value of free_pages since it can be modified concurrently by other
       * transactions, thus the redo must be executed through an operation.
       */

      /* To sync volume header and page bitmap, use RVDK_IDDEALLOC_WITH_VOLHEADER.
         See disk_id_dealloc() function */

      return NO_ERROR;
    }
  else
    {
      return ER_FAILED;
    }
}

/*
 * disk_id_dealloc () - Deallocate the given allocation unit from the given
 *                   allocation map table
 *   return: number of units were deallocated or -1 when error
 *   volid(in): Permanent volume identifier
 *   at_pg1(in): First page of PAT/SAT page
 *   deallid(in): Deallocation identifier
 *   ndealloc(in):
 *   deallid_type(in):
 */
static int
disk_id_dealloc (THREAD_ENTRY * thread_p, INT16 volid, INT32 at_pg1,
		 INT32 deallid, INT32 ndealloc, int deallid_type,
		 DISK_PAGE_TYPE page_type)
{
  int i;
  unsigned char *at_chptr;	/* Pointer to character of Sector/page allocator
				   table */
  unsigned char *out_chptr;	/* Outside of page */
  VPID vpid;
  LOG_DATA_ADDR addr;
  INT32 nfound = -1;		/* Number of units actually deallocated */
  DISK_RECV_MTAB_BITS_WITH recv;
  int retry;

  addr.vfid = NULL;

  /* One allocation table page at a time */
  vpid.volid = volid;
  for (vpid.pageid = (deallid / DISK_PAGE_BIT) + at_pg1;
       ndealloc > 0; vpid.pageid++)
    {

      retry = 0;
      while ((addr.pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE,
				      PGBUF_LATCH_WRITE,
				      PGBUF_UNCONDITIONAL_LATCH)) == NULL)
	{
	  switch (er_errid ())
	    {
	    case NO_ERROR:
	    case ER_INTERRUPTED:
	      continue;
	    case ER_LK_UNILATERALLY_ABORTED:
	    case ER_LK_PAGE_TIMEOUT:
	    case ER_PAGE_LATCH_TIMEDOUT:
	      retry++;
	      break;
	    }

	  if (!retry)
	    {
	      break;
	    }

	  if (retry > 10)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_PAGE_LATCH_ABORTED, 2, vpid.volid, vpid.pageid);
	      break;
	    }
	}

      if (addr.pgptr == NULL)
	{
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, addr.pgptr, PAGE_VOLBITMAP);

      /* Locate the first "at-byte" from where we start deallocating the
         current deallid, continue with the rest as needed */
      addr.offset = ((deallid - (vpid.pageid - at_pg1) * DISK_PAGE_BIT)
		     / CHAR_BIT);
      recv.start_bit = deallid % CHAR_BIT;
      recv.num = 0;
      recv.deallid_type = deallid_type;
      recv.page_type = page_type;

      /* One byte at a time */
      for (at_chptr = (unsigned char *) addr.pgptr + addr.offset,
	   out_chptr = (unsigned char *) addr.pgptr + DB_PAGESIZE;
	   ndealloc > 0 && at_chptr < out_chptr; at_chptr++)
	{

	  /* One bit at a time */
	  for (i = deallid % CHAR_BIT;
	       i < CHAR_BIT && ndealloc > 0; i++, deallid++, ndealloc--)
	    {
	      if (disk_bit_is_set (at_chptr, i))
		{
		  /* ids (pages/sectors) are deallocated until the end of the
		     transaction. */
		  recv.num++;
		  if (nfound == -1)
		    {
		      nfound = 1;
		    }
		  else
		    {
		      nfound++;
		    }
		}
	      else
		{
		  /*
		   * It looks like an error, this id is not allocated.
		   * We are going to continue anyhow deallocating the rest.
		   * Most of the time, we do
		   * not want to stop in case of some ids been deallocated
		   */
		  if (deallid_type == DISK_SECTOR)
		    {
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			      ER_DISK_UNKNOWN_SECTOR, 2, deallid,
			      fileio_get_volume_label (volid, PEEK));
		    }
		  else
		    {
		      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
			      ER_DISK_UNKNOWN_PAGE, 2, deallid,
			      fileio_get_volume_label (volid, PEEK));
		    }

		  if (recv.num > 0)
		    {
		      /* Log a postpone operation to deallocate the found ids
		         at the END OF THE TRANSACTION (or top action). */
		      log_append_postpone (thread_p,
					   RVDK_IDDEALLOC_WITH_VOLHEADER,
					   &addr, sizeof (recv), &recv);

		      /* Continue at next deallocation identifier */
		      recv.start_bit = (deallid + 1) % CHAR_BIT;
		      recv.num = 0;
		      recv.deallid_type = deallid_type;
		      recv.page_type = page_type;
		      addr.offset =
			((deallid + 1 -
			  (vpid.pageid - at_pg1) * DISK_PAGE_BIT) / CHAR_BIT);
		    }
		}
	    }
	}
      if (recv.num > 0)
	{
	  /* Log a postpone operation to deallocate the ids at the end of the
	     transaction. */
	  log_append_postpone (thread_p, RVDK_IDDEALLOC_WITH_VOLHEADER, &addr,
			       sizeof (recv), &recv);
	}
      pgbuf_unfix_and_init (thread_p, addr.pgptr);
    }

  return nfound;
}

/*
 * disk_get_maxcontiguous_numpages () - Find the maximum number of contiguous pages
 *   return: max number of contiguous pages
 *   volid(in): Permanent volume identifier
 *   max_npages(in): maximum number of pages to find
 *
 * Note: The maximum number of free contiguous pages should be taken as an
 *       approximation by the caller since we do not leave the page locked
 *       after the inquire. That is, someone else can allocate pages from that
 *       pool of contiguous pages.
 */
INT32
disk_get_maxcontiguous_numpages (THREAD_ENTRY * thread_p, INT16 volid,
				 INT32 max_npages)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 npages;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in shared mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      return NULL_PAGEID;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  npages = vhdr->free_pages;
  if (npages <= 1)
    {
      goto end;
    }

  npages = disk_id_get_max_contiguous (thread_p, volid,
				       vhdr->page_alloctb_page1,
				       vhdr->sys_lastpage + 1,
				       vhdr->total_pages - 1, max_npages);
end:

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return npages;
}

/*
 * disk_get_hint_contiguous_free_numpages () - Find number of free pages if there are
 *                                   the given contiguous pages
 *   return: true if there are the given contiguous pages, or false
 *   volid(in): Permanent volume identifier
 *   arecontiguous_npages(in): Number of desired contiguous pages
 *   num_freepgs(out): Number of free pages
 *
 * Note: The maximum number of free contiguous pages should be taken as a hint
 *       since the bit map is not locked during its computation. That is,
 *       someone else can allocate pages from that pool of contiguous pages.
 */
static bool
disk_get_hint_contiguous_free_numpages (THREAD_ENTRY * thread_p, INT16 volid,
					INT32 arecontiguous_npages,
					INT32 * num_freepgs)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 npages;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
		     PGBUF_UNCONDITIONAL_LATCH);
  if (pgptr == NULL)
    {
      *num_freepgs = -1;
      return false;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  *num_freepgs = vhdr->free_pages;
  if (*num_freepgs <= arecontiguous_npages)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
      return false;
    }

  if (arecontiguous_npages > 1)
    {
      npages = disk_id_get_max_contiguous (thread_p, volid,
					   vhdr->page_alloctb_page1,
					   vhdr->sys_lastpage + 1,
					   vhdr->total_pages - 1,
					   arecontiguous_npages);
    }
  else
    {
      npages = *num_freepgs;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return (npages >= arecontiguous_npages) ? true : false;
}

/*
 * disk_id_get_max_contiguous () - Find the maximum number of contiguous units from
 *                          the given allocation bitmap table
 *   return: Number of contiguous units (i.e., Page/sector)
 *   volid(in): Permanent volume identifier
 *   at_pg1(in): First page of PAT/SAT page
 *   low_allid(in): First possible allocation page/sector
 *   high_allid(in): Last possible allocation page/sector
 *   nunits_quit(in): Quit immediately if nunits are found
 */
static INT32
disk_id_get_max_contiguous (THREAD_ENTRY * thread_p, INT16 volid,
			    INT32 at_pg1, INT32 low_allid, INT32 high_allid,
			    INT32 nunits_quit)
{
  int i;
  INT32 last_nfound = 0;	/* Last number of contiguous allocation
				   pages/sectors */
  INT32 nfound = 0;		/* Number of contiguous allocation pages/sectors */
  unsigned char *at_chptr;	/* Pointer to character of Sector/page allocator
				   table */
  unsigned char *out_chptr;	/* Outside of page */
  VPID vpid;
  PAGE_PTR pgptr = NULL;	/* Pointer to allocation map */
  INT16 offset;			/* Offset in allocation map page */

  vpid.volid = volid;

  /* One allocation table page at a time */
  for (vpid.pageid = (low_allid / DISK_PAGE_BIT) + at_pg1;
       nfound < nunits_quit && low_allid <= high_allid; vpid.pageid++)
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  nfound = 0;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_VOLBITMAP);

      /* One byte at a time */
      offset =
	((low_allid - (vpid.pageid - at_pg1) * DISK_PAGE_BIT) / CHAR_BIT);
      out_chptr = (unsigned char *) pgptr + DB_PAGESIZE;

      for (at_chptr = (unsigned char *) pgptr + offset;
	   (nfound < nunits_quit && low_allid <= high_allid
	    && at_chptr < out_chptr); at_chptr++)
	{
	  /* One bit at a time */
	  for (i = low_allid % CHAR_BIT;
	       (i < CHAR_BIT && nfound < nunits_quit
		&& low_allid <= high_allid); i++, low_allid++)
	    {
	      if (!disk_bit_is_set (at_chptr, i))
		{
		  nfound++;
		}
	      else
		{
		  if (last_nfound < nfound)
		    {
		      last_nfound = nfound;
		    }
		  /* There is not contiguous pages */
		  nfound = 0;
		}
	    }
	}
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (last_nfound > nfound)
    {
      nfound = last_nfound;
    }

  return nfound;
}

/*
 * disk_id_get_max_frees () - Find the maximum number of free units
 *   return: Number of free units (i.e., Page/sector)
 *   volid(in): Permanent volume identifier
 *   at_pg1(in): First page of PAT/SAT page
 *   low_allid(in): First possible allocation page/sector
 *   high_allid(in): Last possible allocation page/sector
 *
 * Note: The function is used for checking consistency purposes.
 */
static INT32
disk_id_get_max_frees (THREAD_ENTRY * thread_p, INT16 volid, INT32 at_pg1,
		       INT32 low_allid, INT32 high_allid)
{
  int i;
  INT32 count = 0;		/* Number of free that has been found */
  unsigned char *at_chptr;	/* Pointer to character of Sector/page allocator
				   table */
  unsigned char *out_chptr;	/* Outside of page */
  VPID vpid;
  PAGE_PTR pgptr = NULL;	/* Pointer to allocation map */
  INT16 offset;			/* Offset in allocation map page */

  vpid.volid = volid;

  /* One allocation table page at a time */
  for (vpid.pageid = (low_allid / DISK_PAGE_BIT) + at_pg1;
       low_allid <= high_allid; vpid.pageid++)
    {
      pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
      if (pgptr == NULL)
	{
	  count = -1;
	  break;
	}

      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_VOLBITMAP);

      /* One byte at a time */
      offset =
	((low_allid - (vpid.pageid - at_pg1) * DISK_PAGE_BIT) / CHAR_BIT);
      out_chptr = (unsigned char *) pgptr + DB_PAGESIZE;

      for (at_chptr = (unsigned char *) pgptr + offset;
	   low_allid <= high_allid && at_chptr < out_chptr; at_chptr++)
	{
	  /* One bit at a time */
	  for (i = low_allid % CHAR_BIT;
	       i < CHAR_BIT && low_allid <= high_allid; i++, low_allid++)
	    {
	      if (!disk_bit_is_set (at_chptr, i))
		{
		  count++;
		}
	    }
	}
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return count;
}

/*
 * disk_isvalid_page () - Check if page is valid
 *   return: DISK_INVALID, DISK_VALID, DISK_ERROR
 *   volid(in): Permanent volume identifier
 *   pageid(in): pageid for verification
 *
 * Note: This function can be used for debugging purposes. The page buffer
 *       manager in debugging mode calls this function to detect invalid
 *       references to pages.
 */
DISK_ISVALID
disk_isvalid_page (THREAD_ENTRY * thread_p, INT16 volid, INT32 pageid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;
  DISK_ISVALID valid;

  if (fileio_get_volume_descriptor (volid) == NULL_VOLDES || pageid < 0)
    {
      return DISK_INVALID;
    }

  if (pageid == DISK_VOLHEADER_PAGE)
    {
      return DISK_VALID;
    }

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in shared mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
  if (hdr_pgptr == NULL)
    {
      return DISK_ERROR;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  valid = ((pageid <= vhdr->sys_lastpage)
	   ? DISK_VALID
	   : ((pageid > vhdr->total_pages)
	      ? DISK_INVALID
	      : disk_id_isvalid (thread_p, volid, vhdr->page_alloctb_page1,
				 pageid)));

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return valid;
}

/*
 * disk_id_isvalid () - Check if unit is valid/allocated
 *   return:  DISK_INVALID, DISK_VALID
 *   volid(in): Permanent volume identifier
 *   at_pg1(in): First page of PAT/SAT page
 *   allid(in): Deallocation identifier
 */
static DISK_ISVALID
disk_id_isvalid (THREAD_ENTRY * thread_p, INT16 volid, INT32 at_pg1,
		 INT32 allid)
{
  VPID vpid;
  PAGE_PTR at_pgptr = NULL;	/* Pointer to Sector/page allocator table */
  unsigned char *at_chptr;	/* Pointer to character of Sector/page
				   allocator table */
  DISK_ISVALID valid = DISK_ERROR;

  vpid.volid = volid;
  vpid.pageid = (allid / DISK_PAGE_BIT) + at_pg1;

  at_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, false);
  if (at_pgptr != NULL)
    {
      (void) pgbuf_check_page_ptype (thread_p, at_pgptr, PAGE_VOLBITMAP);

      /* Locate the "at-byte" of the unit */
      at_chptr = ((unsigned char *) at_pgptr +
		  (allid -
		   (vpid.pageid - at_pg1) * DISK_PAGE_BIT) / CHAR_BIT);
      /* Now locate the bit, and verify it */
      valid = (disk_bit_is_set (at_chptr, allid % CHAR_BIT))
	? DISK_VALID : DISK_INVALID;
      pgbuf_unfix_and_init (thread_p, at_pgptr);
    }

  return valid;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * disk_get_overhead_numpages () - Return the number of overhead pages
 *   return: Number of overhead pages
 *   volid(in): Permanent volume identifier
 */
INT32
disk_get_overhead_numpages (THREAD_ENTRY * thread_p, INT16 volid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;
  INT32 noverhead_pages;

  if (volid == NULL_VOLID)
    {
      return -1;
    }

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * The overhead for a volume does not change. Therefore, we do need to
   * lock the header page since the field does not change.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, false);
  if (hdr_pgptr == NULL)
    {
      return -1;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  noverhead_pages = vhdr->sys_lastpage + 1;

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return noverhead_pages;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * disk_get_num_overhead_for_newvol () - Return the number of overhead pages when a
 *                             volume of the given number of pages is formatted
 *   return: Number of overhead pages
 *   npages(in): Size of the volume in pages
 */
INT32
disk_get_num_overhead_for_newvol (INT32 npages)
{
  int nsects;
  INT32 num_overhead_pages;

  /* Overhead: header + sectaor table + page table */
  num_overhead_pages = 1;
  nsects = CEIL_PTVDIV (npages, DISK_SECTOR_NPAGES);
  num_overhead_pages += CEIL_PTVDIV (nsects, DISK_PAGE_BIT);
  num_overhead_pages += CEIL_PTVDIV (npages, DISK_PAGE_BIT);

  return num_overhead_pages;
}

/*
 * disk_repair () - Repair the allocate map table
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   dk_type(in): Page type
 */
static int
disk_repair (THREAD_ENTRY * thread_p, INT16 volid, int dk_type)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 nfree;
  int error = NO_ERROR;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, false);
  if (pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  if (dk_type == DISK_PAGE)
    {
      nfree =
	disk_id_get_max_frees (thread_p, volid, vhdr->page_alloctb_page1,
			       vhdr->sys_lastpage + 1, vhdr->total_pages - 1);
      if (vhdr->free_pages >= nfree)
	{
	  vhdr->free_pages = nfree;
	}
      else
	{
	  error = ER_FAILED;
	}
    }
  else if (dk_type == DISK_SECTOR)
    {
      nfree = disk_id_get_max_frees (thread_p, volid,
				     vhdr->sect_alloctb_page1, 1,
				     vhdr->total_sects - 1);
      if (vhdr->free_sects >= nfree)
	{
	  vhdr->free_sects = nfree;
	}
      else
	{
	  error = ER_FAILED;
	}
    }

  if (error == NO_ERROR)
    {
      pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return error;
}

/*
 * disk_check () - Check for any inconsistencies at the volume header
 *   return: DISK_INVALID, DISK_VALID
 *   volid(in): Permanent volume identifier
 *   repair(in): repair when inconsistencies occur
 */
DISK_ISVALID
disk_check (THREAD_ENTRY * thread_p, INT16 volid, bool repair)
{
  DISK_ISVALID valid = DISK_VALID;
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  PAGE_PTR pgptr = NULL;
  INT32 nfree;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in shared mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, false);
  if (pgptr == NULL)
    {
      goto error;
    }

  (void) disk_verify_volume_header (thread_p, pgptr);

  vhdr = (DISK_VAR_HEADER *) pgptr;

  if (vhdr->purpose == DISK_TEMPVOL_TEMP_PURPOSE)
    {
      assert_release (vhdr->purpose != DISK_TEMPVOL_TEMP_PURPOSE);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);

      goto error;
    }

  nfree = disk_id_get_max_frees (thread_p, volid, vhdr->page_alloctb_page1,
				 vhdr->sys_lastpage + 1,
				 vhdr->total_pages - 1);
  if (nfree != vhdr->free_pages)
    {
      if (nfree == -1)
	{
	  /* There was an error (e.g., interrupt) while calculating the number
	     of free pages */
	  goto error;
	}
      else if (repair)
	{
	  (void) disk_verify_volume_header (thread_p, pgptr);

	  pgbuf_unfix_and_init (thread_p, pgptr);

	  if (disk_repair (thread_p, volid, DISK_PAGE) != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_DISK_CANNOT_REPAIR_INCONSISTENT_NFREE_PAGES, 3,
		      fileio_get_volume_label (volid, PEEK),
		      vhdr->free_pages, nfree);
	      valid = DISK_INVALID;
	    }

	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     false);
	  if (pgptr == NULL)
	    {
	      goto error;
	    }

	  (void) disk_verify_volume_header (thread_p, pgptr);

	  vhdr = (DISK_VAR_HEADER *) pgptr;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_DISK_INCONSISTENT_NFREE_PAGES, 3,
		  fileio_get_volume_label (volid, PEEK), vhdr->free_pages,
		  nfree);
	  valid = DISK_INVALID;
	}
    }

  nfree = disk_id_get_max_frees (thread_p, volid, vhdr->sect_alloctb_page1, 1,
				 vhdr->total_sects - 1);

  if (nfree != vhdr->free_sects)
    {
      if (nfree == -1)
	{
	  /* There was an error (e.g., interrupt) while calculating the number
	     of free sectors */
	  goto error;
	}
      else if (repair)
	{
	  (void) disk_verify_volume_header (thread_p, pgptr);

	  pgbuf_unfix_and_init (thread_p, pgptr);

	  if (disk_repair (thread_p, volid, DISK_SECTOR) != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_DISK_CANNOT_REPAIR_INCONSISTENT_NFREE_SECTS, 3,
		      fileio_get_volume_label (volid, PEEK),
		      vhdr->free_sects, nfree);
	      valid = DISK_INVALID;
	    }

	  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     false);
	  if (pgptr == NULL)
	    {
	      goto error;
	    }

	  (void) disk_verify_volume_header (thread_p, pgptr);

	  vhdr = (DISK_VAR_HEADER *) pgptr;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_DISK_INCONSISTENT_NFREE_SECTS, 3,
		  fileio_get_volume_label (volid, PEEK), vhdr->free_sects,
		  nfree);
	  valid = DISK_INVALID;
	}
    }

  /* the following check also added to the disk_verify_volume_header() macro */
  if (vhdr->sect_npgs != DISK_SECTOR_NPAGES
      || vhdr->total_sects != CEIL_PTVDIV (vhdr->total_pages, vhdr->sect_npgs)
      || vhdr->sect_alloctb_page1 != DISK_VOLHEADER_PAGE + 1
      || vhdr->page_alloctb_page1 != (vhdr->sect_alloctb_page1
				      + vhdr->sect_alloctb_npages)
      || vhdr->sys_lastpage != (vhdr->page_alloctb_page1
				+ vhdr->page_alloctb_npages - 1))
    {
      (void) disk_verify_volume_header (thread_p, pgptr);

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_DISK_INCONSISTENT_VOL_HEADER, 1,
	      fileio_get_volume_label (volid, PEEK));
      valid = DISK_INVALID;
    }
  else
    {
      if (vhdr->sect_alloctb_npages < CEIL_PTVDIV (vhdr->total_sects,
						   DISK_PAGE_BIT)
	  || vhdr->page_alloctb_npages < CEIL_PTVDIV (vhdr->total_pages,
						      DISK_PAGE_BIT)
	  || vhdr->page_alloctb_npages != CEIL_PTVDIV (vhdr->max_npages,
						       DISK_PAGE_BIT))
	{
	  (void) disk_verify_volume_header (thread_p, pgptr);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_DISK_INCONSISTENT_VOL_HEADER, 1,
		  fileio_get_volume_label (volid, PEEK));
	  valid = DISK_INVALID;
	}
    }


  (void) disk_verify_volume_header (thread_p, pgptr);

  pgbuf_unfix_and_init (thread_p, pgptr);

  return valid;

error:

  if (pgptr != NULL)
    {
      (void) disk_verify_volume_header (thread_p, pgptr);

      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  return DISK_ERROR;
}

/*
 * disk_dump_goodvol_system () - Dump the system area information of the given volume
 *   return: NO_ERROR
 *   volid(in): Permanent volume identifier
 *   fs_sectid(in): First sector to print in SAT
 *   ls_sectid(in): Last sector to print in SAT
 *   fs_pageid(in): First page to print in PAT
 *   ls_pageid(in): Last page to print in PAT
 *
 * Note: The header information and the sector and page allocator maps tables
 *       are printed. This function is used for debugging purposes.
 */
static int
disk_dump_goodvol_system (THREAD_ENTRY * thread_p, FILE * fp, INT16 volid,
			  INT32 fs_sectid, INT32 ls_sectid, INT32 fs_pageid,
			  INT32 ls_pageid)
{
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;

  vpid.volid = volid;
  vpid.pageid = DISK_VOLHEADER_PAGE;

  /*
   * Lock the volume header in shared mode and then fetch the page. The
   * volume header page is locked to maintain a persistent view of volume
   * header and the map allocation tables until the operation is done. Note
   * that this is the only page among the volume system pages that is locked.
   */
  hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, false);
  if (hdr_pgptr == NULL)
    {
      return ER_FAILED;
    }

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

  disk_vhdr_dump (fp, vhdr);

  /* Make sure the input parameters are OK */
  if (fs_sectid < 0)
    {
      fs_sectid = 0;
    }
  else if (fs_sectid > vhdr->total_sects)
    {
      fs_sectid = vhdr->total_sects;
    }

  if (ls_sectid < 0 || ls_sectid > vhdr->total_sects)
    {
      ls_sectid = vhdr->total_sects;
    }

  if (ls_sectid < fs_sectid)
    {
      ls_sectid = fs_sectid;
    }

  if (fs_pageid < 0)
    {
      fs_pageid = 0;
    }
  else if (fs_pageid > vhdr->total_pages)
    {
      fs_pageid = vhdr->total_pages;
    }

  if (ls_pageid < 0 || ls_pageid > vhdr->total_pages)
    {
      ls_pageid = vhdr->total_pages;
    }

  if (ls_pageid < fs_pageid)
    {
      ls_pageid = fs_pageid;
    }

  /* Display Sector allocator Map table */
  (void) fprintf (fp, "\nSECTOR ALLOCATOR MAP TABLE\n");
  if (disk_map_dump (thread_p, fp, &vpid, "SECTOR ID",
		     (fs_sectid / DISK_PAGE_BIT) + vhdr->sect_alloctb_page1,
		     (ls_sectid / DISK_PAGE_BIT) + vhdr->sect_alloctb_page1,
		     fs_sectid, ls_sectid) != NO_ERROR)
    {
      (void) fprintf (fp,
		      "Problems dumping sector table of volume = %s\n",
		      disk_vhdr_get_vol_fullname (vhdr));
    }
  else
    {
      /* Display Page allocator Map table */
      (void) fprintf (fp, "\nPAGE ALLOCATOR MAP TABLE\n");
      if (disk_map_dump (thread_p, fp, &vpid, "PAGE ID",
			 (fs_pageid / DISK_PAGE_BIT) +
			 vhdr->page_alloctb_page1,
			 (ls_pageid / DISK_PAGE_BIT) +
			 vhdr->page_alloctb_page1, fs_pageid,
			 ls_pageid) != NO_ERROR)
	{
	  (void) fprintf (fp,
			  "Problems dumping page table of volume = %s\n",
			  disk_vhdr_get_vol_fullname (vhdr));
	}
    }

  (void) fprintf (fp, "\n\n");

  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

  return NO_ERROR;
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
disk_volume_header_start_scan (THREAD_ENTRY * thread_p, int type,
			       DB_VALUE ** arg_values, int arg_cnt,
			       void **ptr)
{
  int error = NO_ERROR;
  DISK_CHECK_VOL_INFO vol_info;
  DISK_VOL_HEADER_CONTEXT *ctx = NULL;

  ctx = db_private_alloc (thread_p, sizeof (DISK_VOL_HEADER_CONTEXT));
  if (ctx == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }
  memset (ctx, 0, sizeof (DISK_VOL_HEADER_CONTEXT));

  assert (arg_values != NULL && arg_cnt && arg_values[0] != NULL);
  assert (DB_VALUE_TYPE (arg_values[0]) == DB_TYPE_INTEGER);
  ctx->volume_id = db_get_int (arg_values[0]);

  /* if volume id is out of range */
  if (ctx->volume_id < 0 || ctx->volume_id > DB_INT16_MAX)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_VOLID_NOT_EXIST, 1,
	      ctx->volume_id);
      error = ER_DIAG_VOLID_NOT_EXIST;
      goto exit_on_error;
    }

  vol_info.volid = (INT16) ctx->volume_id;
  vol_info.exists = false;
  /* check volume id exist or not */
  (void) fileio_map_mounted (thread_p, disk_check_volume_exist, &vol_info);
  if (!vol_info.exists)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_DIAG_VOLID_NOT_EXIST, 1,
	      ctx->volume_id);
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
disk_volume_header_next_scan (THREAD_ENTRY * thread_p, int cursor,
			      DB_VALUE ** out_values, int out_cnt, void *ptr)
{
  DISK_VAR_HEADER *vhdr;
  VPID vpid;
  int error = NO_ERROR, idx = 0;
  PAGE_PTR pgptr = NULL;
  DB_DATETIME create_time;
  char buf[256];
  DISK_VOL_HEADER_CONTEXT *ctx = (DISK_VOL_HEADER_CONTEXT *) ptr;

  if (cursor >= 1)
    {
      return S_END;
    }

  vpid.volid = (INT16) ctx->volume_id;
  vpid.pageid = DISK_VOLHEADER_PAGE;
  pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ, false);
  if (pgptr == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto exit_on_error;
    }

  vhdr = (DISK_VAR_HEADER *) pgptr;

  /* fill each column */
  db_make_int (out_values[idx], ctx->volume_id);
  idx++;

  snprintf (buf, sizeof (buf), "MAGIC SYMBOL = %s at disk location = %lld",
	    vhdr->magic, offsetof (FILEIO_PAGE, page) +
	    (long long) offsetof (DISK_VAR_HEADER, magic));
  error = db_make_string_copy (out_values[idx], buf);
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  db_make_int (out_values[idx], vhdr->iopagesize);
  idx++;

  db_make_string (out_values[idx], disk_purpose_to_string (vhdr->purpose));
  idx++;

  db_make_int (out_values[idx], vhdr->sect_npgs);
  idx++;

  db_make_int (out_values[idx], vhdr->total_sects);
  idx++;

  db_make_int (out_values[idx], vhdr->free_sects);
  idx++;

  db_make_int (out_values[idx], vhdr->hint_allocsect);
  idx++;

  db_make_int (out_values[idx], vhdr->total_pages);
  idx++;

  db_make_int (out_values[idx], vhdr->free_pages);
  idx++;

  db_make_int (out_values[idx], vhdr->sect_alloctb_npages);
  idx++;

  db_make_int (out_values[idx], vhdr->sect_alloctb_page1);
  idx++;

  db_make_int (out_values[idx], vhdr->page_alloctb_npages);
  idx++;

  db_make_int (out_values[idx], vhdr->page_alloctb_page1);
  idx++;

  db_make_int (out_values[idx], vhdr->sys_lastpage);
  idx++;

  db_localdatetime ((time_t *) & vhdr->db_creation, &create_time);
  db_make_datetime (out_values[idx], &create_time);
  idx++;

  db_make_int (out_values[idx], vhdr->max_npages);
  idx++;

  db_make_int (out_values[idx], vhdr->used_data_npages);
  idx++;

  db_make_int (out_values[idx], vhdr->used_index_npages);
  idx++;

  error = db_make_string_copy (out_values[idx],
			       lsa_to_string (buf, sizeof (buf),
					      &vhdr->chkpt_lsa));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = db_make_string_copy (out_values[idx],
			       hfid_to_string (buf, sizeof (buf),
					       &vhdr->boot_hfid));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = db_make_string_copy (out_values[idx],
			       (char *) (vhdr->var_fields +
					 vhdr->offset_to_vol_fullname));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = db_make_string_copy (out_values[idx],
			       (char *) (vhdr->var_fields +
					 vhdr->offset_to_next_vol_fullname));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  error = db_make_string_copy (out_values[idx],
			       (char *) (vhdr->var_fields +
					 vhdr->offset_to_vol_remarks));
  idx++;
  if (error != NO_ERROR)
    {
      goto exit_on_error;
    }

  assert (idx == out_cnt);

exit_on_error:
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
 *   return: NO_ERROR
 *   vhdr(in): Pointer to volume header
 *
 * Note: This function is used for debugging purposes.
 */
static int
disk_vhdr_dump (FILE * fp, const DISK_VAR_HEADER * vhdr)
{
  char time_val[CTIME_MAX];
  int ret = NO_ERROR;
  time_t tmp_time;

  (void) fprintf (fp, " MAGIC SYMBOL = %s at disk location = %lld\n",
		  vhdr->magic, offsetof (FILEIO_PAGE, page) +
		  (long long) offsetof (DISK_VAR_HEADER, magic));
  (void) fprintf (fp, " io_pagesize = %d,\n", vhdr->iopagesize);
  (void) fprintf (fp, " VID = %d, VOL_FULLNAME = %s\n"
		  " VOL PURPOSE = %s\n VOL_REMARKS = %s\n",
		  vhdr->volid, disk_vhdr_get_vol_fullname (vhdr),
		  disk_purpose_to_string (vhdr->purpose),
		  disk_vhdr_get_vol_remarks (vhdr));
  (void) fprintf (fp, " NEXT_VOL_FULLNAME = %s\n",
		  disk_vhdr_get_next_vol_fullname (vhdr));
  (void) fprintf (fp, " LAST SYSTEM PAGE = %d\n", vhdr->sys_lastpage);
  (void) fprintf (fp, " SECTOR: SIZE IN PAGES = %10d, TOTAL = %10d,",
		  vhdr->sect_npgs, vhdr->total_sects);
  (void) fprintf (fp, " FREE = %10d,\n %10s HINT_ALLOC = %10d\n",
		  vhdr->free_sects, " ", vhdr->hint_allocsect);
  (void) fprintf (fp, " PAGE:   TOTAL = %10d, FREE = %10d, MAX = %10d\n",
		  vhdr->total_pages, vhdr->free_pages, vhdr->max_npages);
  (void) fprintf (fp,
		  " SAT:    SIZE IN PAGES = %10d, FIRST_PAGE = %5d\n",
		  vhdr->sect_alloctb_npages, vhdr->sect_alloctb_page1);
  (void) fprintf (fp,
		  " PAT:    SIZE IN PAGES = %10d, FIRST_PAGE = %5d\n",
		  vhdr->page_alloctb_npages, vhdr->page_alloctb_page1);

  tmp_time = (time_t) vhdr->db_creation;
  (void) ctime_r (&tmp_time, time_val);
  (void) fprintf (fp,
		  " Database creation time = %s\n"
		  " Lowest Checkpoint for recovery = %lld|%d\n",
		  time_val, (long long int) vhdr->chkpt_lsa.pageid,
		  vhdr->chkpt_lsa.offset);
  (void) fprintf (fp,
		  "Boot_hfid: volid %d, fileid %d header_pageid %d\n",
		  vhdr->boot_hfid.vfid.volid, vhdr->boot_hfid.vfid.fileid,
		  vhdr->boot_hfid.hpgid);

  return ret;
}

/*
 * disk_map_dump () - Dump the content of the allocation map table
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
disk_map_dump (THREAD_ENTRY * thread_p, FILE * fp, VPID * vpid,
	       const char *at_name, INT32 at_fpageid, INT32 at_lpageid,
	       INT32 all_fid, INT32 all_lid)
{
  int i;
  PAGE_PTR at_pgptr = NULL;	/* Pointer to Sector/page allocator table */
  unsigned char *at_chptr;	/* Char Pointer to Sector/page allocator table */
  unsigned char *out_chptr;	/* Outside of page */

  fprintf (fp, "%10s 0123456789 0123456789 0123456789 0123456789", at_name);

  /* Skip over the desired number */
  if (all_fid % 10)
    {
      fprintf (fp, "\n%10d ", all_fid);
      for (i = 0; i < all_fid % 10; i++)
	{
	  fprintf (fp, " ");
	}
    }

  /* Read every page of the allocation table */
  for (vpid->pageid = at_fpageid; vpid->pageid <= at_lpageid; vpid->pageid++)
    {
      at_pgptr = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_WRITE,
			    false);
      if (at_pgptr == NULL)
	{
	  return ER_FAILED;
	}

      /* One byte at a time */
      out_chptr = (unsigned char *) at_pgptr + DB_PAGESIZE;
      for (at_chptr = (unsigned char *) at_pgptr;
	   at_chptr < out_chptr && all_fid < all_lid; at_chptr++)
	{
	  /* One bit at a time */
	  for (i = 0; i < CHAR_BIT && all_fid < all_lid; i++, all_fid++)
	    {
	      if (all_fid % 40 == 0)
		{
		  fprintf (fp, "\n%10d ", all_fid);
		}
	      else if (all_fid % 10 == 0)
		{
		  fprintf (fp, " ");
		}
	      fprintf (fp, "%d", disk_bit_is_set (at_chptr, i) ? 1 : 0);
	    }
	}
      pgbuf_unfix_and_init (thread_p, at_pgptr);
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

  ret = (fileio_map_mounted (thread_p, disk_dump_goodvol_all,
			     NULL) == true ? NO_ERROR : ER_FAILED);

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
  int ret = NO_ERROR;

  ret =
    disk_dump_goodvol_system (thread_p, stdout, volid, NULL_PAGEID,
			      NULL_PAGEID, NULL_PAGEID, NULL_PAGEID);

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
      if (vhdr->purpose == DISK_TEMPVOL_TEMP_PURPOSE)
	{
	  (void) fileio_format (thread_p, NULL, vol_label,
				vhdr->volid, vhdr->total_pages, false, false,
				false, IO_PAGESIZE, 0, false);
	}
      else
	{
	  (void) fileio_format (thread_p, NULL, vol_label,
				vhdr->volid, vhdr->total_pages, true, false,
				false, IO_PAGESIZE, 0, false);
	}
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
  log_append_dboutside_redo (thread_p, RVLOG_OUTSIDE_LOGICAL_REDO_NOOP, 0,
			     NULL);
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
  int ret = NO_ERROR;

  vhdr = (DISK_VAR_HEADER *) data;
  ret = disk_vhdr_dump (fp, vhdr);
}

/*
 * disk_rv_redo_init_map () - REDO the initialization of map table page.
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_redo_init_map (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  int i;
  INT32 nalloc_bits;
  unsigned char *at_chptr;	/* Char Pointer to Sector/page allocator table */
  unsigned char *out_chptr;	/* Outside of page */

  (void) pgbuf_set_page_ptype (thread_p, rcv->pgptr, PAGE_VOLBITMAP);

  nalloc_bits = *(INT32 *) rcv->data;

  /* Initialize the page to zeros, and allocate the needed bits for the
     pages or sectors */
  disk_set_page_to_zeros (thread_p, rcv->pgptr);

  /* One byte at a time */
  out_chptr = (unsigned char *) rcv->pgptr + DB_PAGESIZE;
  for (at_chptr = (unsigned char *) rcv->pgptr;
       nalloc_bits > 0 && at_chptr < out_chptr; at_chptr++)
    {
      /* One bit at a time */
      for (i = 0; nalloc_bits > 0 && i < CHAR_BIT; i++, nalloc_bits--)
	{
	  disk_bit_set (at_chptr, i);
	}
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
 * disk_vhdr_rv_undoredo_free_sectors () - Redo (Undo) the update of the volume header
 *                                for a sector deallocation(allocation)
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_vhdr_rv_undoredo_free_sectors (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;
  INT32 delta_alloc_sects;

  delta_alloc_sects = *(INT32 *) rcv->data;
  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  vhdr->free_sects += delta_alloc_sects;
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_vhdr_rv_dump_free_sectors () - Dump either redo/undo volume header for
 *                                sector allocation or deallocation.
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_vhdr_rv_dump_free_sectors (FILE * fp, int length_ignore, void *data)
{
  fprintf (fp, "Nalloc_sects = %d\n", abs (*(INT32 *) data));
}

/*
 * disk_vhdr_rv_undoredo_free_pages () - Redo (Undo) the update of the volume header
 *                                for page deallocation(allocation)
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_vhdr_rv_undoredo_free_pages (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;
  DISK_RECV_MTAB_BITS_WITH *mtb;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  mtb = (DISK_RECV_MTAB_BITS_WITH *) rcv->data;

  vhdr->free_pages += mtb->num;

  if (vhdr->purpose == DISK_PERMVOL_DATA_PURPOSE)
    {
      if (mtb->page_type == DISK_PAGE_DATA_TYPE)
	{
	  vhdr->used_data_npages -= mtb->num;
	  if (vhdr->used_data_npages < 0)
	    {
	      assert_release (vhdr->used_data_npages >= 0);
	      vhdr->used_data_npages = 0;
	    }
	}
      else
	{
	  assert_release (mtb->page_type == DISK_PAGE_DATA_TYPE);
	}
    }
  else if (vhdr->purpose == DISK_PERMVOL_INDEX_PURPOSE)
    {
      if (mtb->page_type == DISK_PAGE_INDEX_TYPE)
	{
	  vhdr->used_index_npages -= mtb->num;
	  if (vhdr->used_index_npages < 0)
	    {
	      /* TODO: Uncomment the check after fixing the multiple
	       *       deallocation issue.
	       */
	      /*assert_release (vhdr->used_index_npages >= 0); */
	      vhdr->used_index_npages = 0;
	    }
	}
      else
	{
	  assert_release (mtb->page_type == DISK_PAGE_INDEX_TYPE);
	}
    }
  else if (vhdr->purpose == DISK_PERMVOL_GENERIC_PURPOSE)
    {
      if (mtb->page_type == DISK_PAGE_DATA_TYPE)
	{
	  vhdr->used_data_npages -= mtb->num;
	  if (vhdr->used_data_npages < 0)
	    {
	      assert_release (vhdr->used_data_npages >= 0);
	      vhdr->used_data_npages = 0;
	    }
	}
      else if (mtb->page_type == DISK_PAGE_INDEX_TYPE)
	{
	  vhdr->used_index_npages -= mtb->num;
	  if (vhdr->used_index_npages < 0)
	    {
	      /* TODO: Uncomment the check after fixing the multiple
	       *       deallocation issue.
	       */
	      /*assert_release (vhdr->used_index_npages >= 0); */
	      vhdr->used_index_npages = 0;
	    }
	}
      else
	{
	  assert_release (mtb->page_type == DISK_PAGE_DATA_TYPE
			  || mtb->page_type == DISK_PAGE_INDEX_TYPE);
	}
    }
  else
    {
      assert_release (mtb->page_type != DISK_PAGE_DATA_TYPE
		      && mtb->page_type != DISK_PAGE_INDEX_TYPE);
    }

  disk_cache_goodvol_update (thread_p, vhdr->volid, vhdr->purpose,
			     mtb->num, false, NULL);

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_page_type_to_string () - Get a string of the given disk page type
 *   return: string of the disk page type
 *   page_type(in): The type of the structure
 */
static const char *
disk_page_type_to_string (DISK_PAGE_TYPE page_type)
{
  switch (page_type)
    {
    case DISK_PAGE_DATA_TYPE:
      return "DISK_PAGE_TEMP_TYPE";
    case DISK_PAGE_INDEX_TYPE:
      return "DISK_PAGE_TEMP_TYPE";
    case DISK_PAGE_TEMP_TYPE:
      return "DISK_PAGE_TEMP_TYPE";
    case DISK_PAGE_UNKNOWN_TYPE:
      return "DISK_PAGE_UNKNOWN_TYPE";
    }

  return "UNKNOWN";
}

/*
 * disk_vhdr_rv_dump_free_pages () - Dump either redo/undo volume header for page
 *                                allocation or deallocation
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_vhdr_rv_dump_free_pages (FILE * fp, int length_ignore, void *data)
{
  DISK_RECV_MTAB_BITS_WITH *mtb;

  mtb = (DISK_RECV_MTAB_BITS_WITH *) data;

  fprintf (fp, "num_pages = %d, page_type = %s\n", mtb->num,
	   disk_page_type_to_string (mtb->page_type));
}

/*
 * disk_rv_alloctable_helper () - Redo (undo) update of allocation table for
 *                           allocation (deallocation) of IDS (pageid, sectid)
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
static int
disk_rv_alloctable_helper (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
			   DISK_ALLOCTABLE_MODE mode)
{
  DISK_RECV_MTAB_BITS *mtb;	/* Recovery structure of bits */
  unsigned char *at_chptr;	/* Pointer to character of Sector or page
				   allocation table */
  INT32 num = 0;		/* Number of allocated bits */
  unsigned int bit, i;

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_VOLBITMAP);

  mtb = (DISK_RECV_MTAB_BITS *) rcv->data;

  /* Set mtb->num of bits starting at mtb->start_bit of byte rcv->offset */
  bit = mtb->start_bit;
  num = 0;

  for (at_chptr = (unsigned char *) rcv->pgptr + rcv->offset; num < mtb->num;
       at_chptr++)
    {
      for (i = bit; i < CHAR_BIT && num < mtb->num; i++, num++)
	{
	  if (mode == DISK_ALLOCTABLE_SET)
	    {
	      disk_bit_set (at_chptr, i);
	    }
	  else
	    {
	      disk_bit_clear (at_chptr, i);
	    }
	}
      bit = 0;
    }
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_rv_set_alloctable () - Redo (undo) update of allocation table for
 *                           allocation (deallocation) of IDS (pageid, sectid)
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_set_alloctable (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return disk_rv_alloctable_helper (thread_p, rcv, DISK_ALLOCTABLE_SET);
}

/*
 * disk_rv_clear_alloctable () -  Redo (Undo) update of allocation table for
 *                              deallocation (allocation) of IDS (pageid,
 *                              sectid)
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_clear_alloctable (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return disk_rv_alloctable_helper (thread_p, rcv, DISK_ALLOCTABLE_CLEAR);
}

/*
 * disk_rv_dump_alloctable () - Dump either redo or undo information for either
 *                            allocation or deallocation of ids(pageids,
 *                            sectids)
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_alloctable (FILE * fp, int length_ignore, void *data)
{
  DISK_RECV_MTAB_BITS *mtb;	/* Recovery structure of bits */

  mtb = (DISK_RECV_MTAB_BITS *) data;
  fprintf (fp, "Start_bit = %u, Num_bits = %d\n", mtb->start_bit, mtb->num);
}


/*
 * disk_rv_alloctable_bitmap_only () - Redo (undo) update of allocation
 *                                     table for allocation (deallocation)
 *                                     of IDS (pageid, sectid) only for bitmap page
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
static int
disk_rv_alloctable_bitmap_only (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				DISK_ALLOCTABLE_MODE mode)
{
  DISK_RECV_MTAB_BITS_WITH *mtb;	/* Recovery structure of bits */
  unsigned char *at_chptr;	/* Pointer to character of Sector or page
				   allocation table */
  INT32 num = 0;		/* Number of allocated bits */
  unsigned int bit, i;

  /* TODO: Remove this code when double deallocations issue is
   *       fixed.
   */
  int already_cleared = 0;	/* <-- */

  (void) pgbuf_check_page_ptype (thread_p, rcv->pgptr, PAGE_VOLBITMAP);

  mtb = (DISK_RECV_MTAB_BITS_WITH *) rcv->data;

  assert (mtb != NULL);
  assert (mtb->num > 0);

  /* Set mtb->num of bits starting at mtb->start_bit of byte rcv->offset */
  bit = mtb->start_bit;
  num = 0;

  at_chptr = (unsigned char *) rcv->pgptr + rcv->offset;
  for (; num < mtb->num; at_chptr++)
    {
      for (i = bit; i < CHAR_BIT && num < mtb->num; i++, num++)
	{
	  if (mode == DISK_ALLOCTABLE_SET)
	    {
	      disk_bit_set (at_chptr, i);
	    }
	  else
	    {
	      /* TODO: Remove this code when double deallocations issue is
	       *       fixed. This should only call:
	       *       disk_bit_clear (at_chptr, i);
	       */
	      if (!disk_bit_is_set (at_chptr, i))	/* <-- */
		{		/* <-- */
		  already_cleared++;	/* <-- */
		}		/* <-- */
	      else		/* <-- */
		{		/* <-- */
		  disk_bit_clear (at_chptr, i);
		}		/* <-- */
	    }
	}
      bit = 0;
    }
  /* TODO: Remove this code when double deallocations issue is
   *       fixed. The number is passed to volume header recovery so update
   *       it to avoid header corruption.
   * PLEASE ALSO FIX disk_rv_alloctable_vhdr_only ().
   */
  mtb->num -= already_cleared;	/* <-- */
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);


  return NO_ERROR;
}

/*
 * disk_rv_alloctable_vhdr_only () - Redo (undo) update of allocation
 *                                   table for allocation (deallocation)
 *                                   of IDS (pageid, sectid) only for volume header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
static int
disk_rv_alloctable_vhdr_only (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
			      DISK_ALLOCTABLE_MODE mode)
{
  DISK_RECV_MTAB_BITS_WITH *mtb;	/* Recovery structure of bits */
  DISK_VAR_HEADER *vhdr;
  INT32 delta = 0;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  mtb = (DISK_RECV_MTAB_BITS_WITH *) rcv->data;

  assert (mtb != NULL);

  /* TODO: Remove this code when double deallocations issue is fixed. 
   *       The number is passed to volume header recovery so update
   *       it to avoid header corruption.
   */
  if (mtb->num == 0)	      /* <---- */
    {			      /* <---- */
      return NO_ERROR;	      /* <---- */
    }			      /* <---- */

  assert (mtb->num > 0);

  if (mode == DISK_ALLOCTABLE_SET)
    {
      delta = -(mtb->num);
    }
  else
    {
      delta = mtb->num;
    }

  if (mtb->deallid_type == DISK_SECTOR)
    {
      vhdr->free_sects += delta;
    }
  else
    {
      vhdr->free_pages += delta;

      if (vhdr->purpose == DISK_PERMVOL_DATA_PURPOSE)
	{
	  if (mtb->page_type == DISK_PAGE_DATA_TYPE)
	    {
	      vhdr->used_data_npages -= delta;
	      if (vhdr->used_data_npages < 0)
		{
		  assert_release (vhdr->used_data_npages >= 0);
		  vhdr->used_data_npages = 0;
		}
	    }
	  else
	    {
	      assert_release (mtb->page_type == DISK_PAGE_DATA_TYPE);
	    }
	}
      else if (vhdr->purpose == DISK_PERMVOL_INDEX_PURPOSE)
	{
	  if (mtb->page_type == DISK_PAGE_INDEX_TYPE)
	    {
	      vhdr->used_index_npages -= delta;
	      if (vhdr->used_index_npages < 0)
		{
		  /* TODO: Uncomment the check after fixing the multiple
		   *       deallocation issue.
		   */
		  /* assert_release (vhdr->used_index_npages >= 0); */
		  vhdr->used_index_npages = 0;
		}
	    }
	  else
	    {
	      assert_release (mtb->page_type == DISK_PAGE_INDEX_TYPE);
	    }
	}
      else if (vhdr->purpose == DISK_PERMVOL_GENERIC_PURPOSE)
	{
	  if (mtb->page_type == DISK_PAGE_DATA_TYPE)
	    {
	      vhdr->used_data_npages -= delta;
	      if (vhdr->used_data_npages < 0)
		{
		  assert_release (vhdr->used_data_npages >= 0);
		  vhdr->used_data_npages = 0;
		}
	    }
	  else if (mtb->page_type == DISK_PAGE_INDEX_TYPE)
	    {
	      vhdr->used_index_npages -= delta;
	      if (vhdr->used_index_npages < 0)
		{
		  /* TODO: Uncomment the check after fixing the multiple
		   *       deallocation issue.
		   */
		  /* assert_release (vhdr->used_index_npages >= 0); */
		  vhdr->used_index_npages = 0;
		}
	    }
	  else
	    {
	      assert_release (mtb->page_type == DISK_PAGE_DATA_TYPE
			      || mtb->page_type == DISK_PAGE_INDEX_TYPE);
	    }
	}
      else
	{
	  assert (mtb->page_type != DISK_PAGE_DATA_TYPE
		  && mtb->page_type != DISK_PAGE_INDEX_TYPE);
	}

      disk_cache_goodvol_update (thread_p, vhdr->volid, vhdr->purpose,
				 delta, false, NULL);
    }

  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}


/*
 * disk_rv_alloctable_with_volheader () - Redo (undo) update of allocation
 *                                          table for allocation (deallocation)
 *                                          of IDS (pageid, sectid) with
 *                                          volume_header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
*/
int
disk_rv_alloctable_with_volheader (THREAD_ENTRY * thread_p, LOG_RCV * rcv,
				   LOG_LSA * ref_lsa)
{
  LOG_RCV vhdr_rcv = *rcv;
  VPID vhdr_vpid, page_vpid;

  LOG_DATA_ADDR page_addr, vhdr_addr;
  /* TODO: Remove this code when double deallocations issue is fixed. */
  /* disk_rv_alloctable_bitmap_only updates the number of deallocated because
   * some pages are already deallocated (this is a known issue due to vacuum
   * worker merge b-tree and destroy index file). The real number of
   * deallocated must be passed to header in order to not mess up with its
   * statistics on volume pages.
   * However, when the run postpones are appended, bitmap and header each
   * should receive its own number.
   * The fix for this issue is high priority after merging MVCC into trunk and
   * requires two changes:
   * 1. Mark root page header that the file is being dropped and prevent any
   *	further merges.
   * 2. Deallocate file pages in the b-tree normal traverse order (to be
   *	blocked on merges that started before deleting file).
   */
  int bitmap_num =				    /* <----- */
    ((DISK_RECV_MTAB_BITS_WITH *) rcv->data)->num;  /* <----- */
  int vhdr_num = 0;

  assert (rcv->pgptr != NULL);
  assert (rcv->length > 0);
  assert (rcv->data != NULL);

  /* Find the volume header */
  vhdr_vpid.volid = pgbuf_get_volume_id (rcv->pgptr);
  vhdr_vpid.pageid = DISK_VOLHEADER_PAGE;
  page_vpid.volid = vhdr_vpid.volid;
  page_vpid.pageid = pgbuf_get_page_id (rcv->pgptr);

  /* To avoid latch dead-lock, free page latch first and fetch again. */
  pgbuf_unfix_and_init (thread_p, rcv->pgptr);

  vhdr_rcv.pgptr = pgbuf_fix_with_retry (thread_p, &vhdr_vpid, OLD_PAGE,
					 PGBUF_LATCH_WRITE, 10);
  if (vhdr_rcv.pgptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (vhdr_vpid.volid, PEEK));

      return ER_FAILED;
    }

  rcv->pgptr = pgbuf_fix_with_retry (thread_p, &page_vpid, OLD_PAGE,
				     PGBUF_LATCH_WRITE, 10);
  if (rcv->pgptr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (page_vpid.volid, PEEK));

      pgbuf_unfix (thread_p, vhdr_rcv.pgptr);
      return ER_FAILED;
    }

  disk_rv_alloctable_bitmap_only (thread_p, rcv, DISK_ALLOCTABLE_CLEAR);
  disk_rv_alloctable_vhdr_only (thread_p, &vhdr_rcv, DISK_ALLOCTABLE_CLEAR);

  /* TODO: Remove this code when double deallocations issue is fixed. */
  vhdr_num =					    /* <----- */
    ((DISK_RECV_MTAB_BITS_WITH *) rcv->data)->num;  /* <----- */

  if (ref_lsa != NULL)
    {
      /*
       * append below two log for synchronization between
       * volume header and bitmap page
       */
      page_addr.offset = rcv->offset;
      page_addr.pgptr = rcv->pgptr;

      /* TODO: Remove this code when double deallocations issue is fixed. */
      ((DISK_RECV_MTAB_BITS_WITH *) rcv->data)->num = bitmap_num; /* <--- */

      log_append_run_postpone (thread_p,
			       RVDK_IDDEALLOC_BITMAP_ONLY,
			       &page_addr,
			       &page_vpid, rcv->length, rcv->data, ref_lsa);

      vhdr_addr.offset = 0;
      vhdr_addr.pgptr = vhdr_rcv.pgptr;

      /* TODO: Remove this code when double deallocations issue is fixed. */
      ((DISK_RECV_MTAB_BITS_WITH *) rcv->data)->num = vhdr_num; /* <--- */

      log_append_run_postpone (thread_p,
			       RVDK_IDDEALLOC_VHDR_ONLY,
			       &vhdr_addr,
			       &vhdr_vpid, rcv->length, rcv->data, ref_lsa);
    }

  pgbuf_unfix (thread_p, vhdr_rcv.pgptr);

  return NO_ERROR;
}


/*
 * disk_rv_set_alloctable_vhdr_only () - Redo (undo) update of allocation
 *                                          table for allocation (deallocation)
 *                                          of IDS (pageid, sectid) in volume header
 *   return:
 *   rcv(in): Recovery structure
 */
int
disk_rv_set_alloctable_vhdr_only (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return disk_rv_alloctable_vhdr_only (thread_p, rcv, DISK_ALLOCTABLE_SET);
}

/*
 * disk_rv_clear_alloctable_vhdr_only () - Redo (Undo) update of allocation
 *                                            table for deallocation
 *                                            (allocation) of IDS (pageid,
 *                                            sectid) in volume_header
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_clear_alloctable_vhdr_only (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return disk_rv_alloctable_vhdr_only (thread_p, rcv, DISK_ALLOCTABLE_CLEAR);
}

/*
 * disk_rv_set_alloctable_bitmap_only () - Redo (undo) update of allocation
 *                                          table for allocation (deallocation)
 *                                          of IDS (pageid, sectid) in
 *                                          bitmap
 *   return:
 *   rcv(in): Recovery structure
 */
int
disk_rv_set_alloctable_bitmap_only (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return disk_rv_alloctable_bitmap_only (thread_p, rcv, DISK_ALLOCTABLE_SET);
}

/*
 * disk_rv_clear_alloctable_bitmap_only () - Redo (Undo) update of allocation
 *                                            table for deallocation
 *                                            (allocation) of IDS (pageid,
 *                                            sectid) in bitmap
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_clear_alloctable_bitmap_only (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  return disk_rv_alloctable_bitmap_only (thread_p, rcv,
					 DISK_ALLOCTABLE_CLEAR);
}

/*
 * disk_rv_dump_alloctable_with_vhdr () - Dump either redo or undo
 *                                           information for either allocation
 *                                           or deallocation of ids(pageids,
 *                                           sectids) with volume header
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_alloctable_with_vhdr (FILE * fp, int length_ignore, void *data)
{
  DISK_RECV_MTAB_BITS_WITH *mtb;	/* Recovery structure of bits */

  mtb = (DISK_RECV_MTAB_BITS_WITH *) data;
  fprintf (fp,
	   "Start_bit = %u, Num_bits = %d, Deallocation_type = %d\n",
	   mtb->start_bit, mtb->num, mtb->deallid_type);
}

/*
 * disk_rv_redo_magic () - Recover the change of magic value
 *   return: NO_ERROR
 *   rcv(in): Recovery structure
 */
int
disk_rv_redo_magic (THREAD_ENTRY * thread_p, LOG_RCV * rcv)
{
  DISK_VAR_HEADER *vhdr;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  strncpy (vhdr->magic, (char *) rcv->data, CUBRID_MAGIC_MAX_LENGTH);
  pgbuf_set_dirty (thread_p, rcv->pgptr, DONT_FREE);

  return NO_ERROR;
}

/*
 * disk_rv_dump_magic () - Dump either redo or undo information about magic
 *                       change
 *   return: void
 *   length_ignore(in): Length of Recovery Data
 *   data(in): The data being logged
 */
void
disk_rv_dump_magic (FILE * fp, int length_ignore, void *data)
{
  fprintf (fp, "Magic = %s\n", (char *) data);
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

  memcpy (&vhdr->db_creation, &change->db_creation,
	  sizeof (change->db_creation));
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

  fprintf (fp, "Label = %s, Db_creation = %lld, chkpt = %lld|%d\n",
	   change->vol_fullname, (long long) change->db_creation,
	   (long long int) change->chkpt_lsa.pageid,
	   change->chkpt_lsa.offset);
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
  int ret = NO_ERROR;

  vhdr = (DISK_VAR_HEADER *) rcv->pgptr;
  ret = disk_vhdr_set_next_vol_fullname (vhdr, rcv->data);
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
  fprintf (fp, "Next_Volextension = %s\n", (char *) data);
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
  fprintf (fp, "Heap: Volid = %d, Fileid = %d, Header_pageid = %d\n",
	   hfid->vfid.volid, hfid->vfid.fileid, hfid->hpgid);
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (volid, PEEK));
      return ER_FAILED;
    }

  malloc_io_page_p = (FILEIO_PAGE *) malloc (IO_PAGESIZE);
  if (malloc_io_page_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, IO_PAGESIZE);
      return ER_FAILED;
    }

  memset (malloc_io_page_p, 0, IO_PAGESIZE);
  (void) fileio_initialize_res (thread_p, &(malloc_io_page_p->prv));

  if (fileio_initialize_pages (thread_p, vol_fd, malloc_io_page_p,
			       info->start_pageid, info->npages,
			       IO_PAGESIZE, -1) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_MAYNEED_MEDIA_RECOVERY,
	      1, fileio_get_volume_label (volid, PEEK));
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

  fprintf (fp, "Volid = %d, start pageid = %d, npages = %d\n",
	   info->volid, info->start_pageid, info->npages);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * disk_get_first_total_free_numpages () -
 *   return:
 *   purpose(in):
 *   ntotal_pages(in):
 *   nfree_pages(in):
 */
INT16
disk_get_first_total_free_numpages (THREAD_ENTRY * thread_p,
				    DISK_VOLPURPOSE purpose,
				    INT32 * ntotal_pages, INT32 * nfree_pages)
{
  INT16 volid;
  int nperm_vols;
  DISK_VAR_HEADER *vhdr;
  PAGE_PTR hdr_pgptr = NULL;
  VPID vpid;

  *ntotal_pages = 0;
  *nfree_pages = 0;
  nperm_vols = xboot_find_number_permanent_volumes (thread_p);

  for (volid = LOG_DBFIRST_VOLID; volid < nperm_vols; volid++)
    {
      vpid.volid = volid;
      vpid.pageid = DISK_VOLHEADER_PAGE;

      hdr_pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_READ,
			     false);
      if (hdr_pgptr == NULL)
	{
	  return -1;
	}

      (void) disk_verify_volume_header (thread_p, hdr_pgptr);

      vhdr = (DISK_VAR_HEADER *) hdr_pgptr;

      if (vhdr->purpose == purpose)
	{
	  *ntotal_pages = vhdr->total_pages;
	  *nfree_pages = vhdr->free_pages;

	  (void) disk_verify_volume_header (thread_p, hdr_pgptr);

	  pgbuf_unfix_and_init (thread_p, hdr_pgptr);

	  return volid;
	}

      (void) disk_verify_volume_header (thread_p, hdr_pgptr);

      pgbuf_unfix_and_init (thread_p, hdr_pgptr);
    }

  return -1;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
int
disk_set_alloctables (DISK_VOLPURPOSE vol_purpose,
		      INT32 total_sects, INT32 total_pages,
		      INT32 * sect_alloctb_npages,
		      INT32 * page_alloctb_npages,
		      INT32 * sect_alloctb_page1,
		      INT32 * page_alloctb_page1, INT32 * sys_lastpage)
{
  INT32 possible_max_npages, possible_max_total_sects;
  int ret = NO_ERROR;

  /*
   * In case of temporary purpose temp volume, allocate maximum possible
   * page allocation table, and sector allocation table.
   * It will reserve possible maximum system space. In the time of
   * expansion of that volume, can expand it to the maximum volume size.
   *
   * Currently only temporary purpose temp volume is possible to expand
   * its size, but I guess other permanent volumes are also possible.
   */
  if (vol_purpose == DISK_TEMPVOL_TEMP_PURPOSE)
    {
      possible_max_npages = boot_get_temp_temp_vol_max_npages ();
      possible_max_total_sects = CEIL_PTVDIV (possible_max_npages,
					      DISK_SECTOR_NPAGES);
    }
  else
    {
      possible_max_npages = total_pages;
      possible_max_total_sects = total_sects;
    }

  *sect_alloctb_npages = CEIL_PTVDIV (possible_max_total_sects,
				      DISK_PAGE_BIT);
  *page_alloctb_npages = CEIL_PTVDIV (possible_max_npages, DISK_PAGE_BIT);

  *sect_alloctb_page1 = DISK_VOLHEADER_PAGE + 1;
  *page_alloctb_page1 = *sect_alloctb_page1 + *sect_alloctb_npages;

  *sys_lastpage = *page_alloctb_page1 + *page_alloctb_npages - 1;

  return ret;
}

/*
 * disk_set_page_to_zeros () - Initialize the given page to zeros
 *   return: void
 *   pgptr(in): Pointer to page
 */
static void
disk_set_page_to_zeros (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  /* NOTE: Does not need to hold BCB_mutex since the page is fixed */
  (void) memset (pgptr, '\0', DB_PAGESIZE);
  pgbuf_set_dirty (thread_p, pgptr, DONT_FREE);
}

/*
 * disk_verify_volume_header () -
 *   return: void
 *   pgphtr(in): Pointer to volume header page
 */
static void
disk_verify_volume_header (THREAD_ENTRY * thread_p, PAGE_PTR pgptr)
{
  DISK_VAR_HEADER *vhdr;

#if !defined (NDEBUG)
  if (pgptr != NULL)
    {
      (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_VOLHEADER);
    }
#endif /* NDEBUG */

  vhdr = (DISK_VAR_HEADER *) pgptr;

  DISK_VERIFY_VAR_HEADER (vhdr);
}
