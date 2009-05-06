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
 * log_writer.c -
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "page_buffer.h"
#include "log_impl.h"
#include "storage_common.h"
#include "log_manager.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "porting.h"
#include "environment_variable.h"
#include "misc_string.h"
#include "intl_support.h"
#include "system_parameter.h"
#include "file_io.h"
#include "log_writer.h"
#include "network_interface_cl.h"
#include "network_interface_sr.h"
#if defined(SERVER_MODE)
#include "memory_alloc.h"
#include "server_support.h"
#endif
#include "dbi.h"

#if defined(CS_MODE)
LOGWR_GLOBAL logwr_Gl = {
  /* hdr */
  {{'0'}, 0, 0, {'0'}, 0.0, 0, 0, 0, 0, 0, 0, 0,
   {NULL_PAGEID, NULL_OFFSET},
   {NULL_PAGEID, NULL_OFFSET},
   0, 0, 0, 0, 0, false,
   {NULL_PAGEID, NULL_OFFSET},
   {NULL_PAGEID, NULL_OFFSET},
   {NULL_PAGEID, NULL_OFFSET},
   {'0'}, 0, 0, 0,
   {{0, 0, 0, 0, 0}},
   0, 0,
   {NULL_PAGEID, NULL_OFFSET}},
  /* loghdr_pgptr */
  NULL,
  /* db_name */
  {'0'}
  ,
  /* log_path */
  {'0'}
  ,
  /* active_name */
  {'0'}
  ,
  /* append_vdes */
  NULL_VOLDES,
  /* logpg_area */
  NULL,
  /* logpg_area_size */
  0,
  /* logpg_fill_size */
  0,
  /* toflush */
  NULL,
  /* max_toflush */
  0,
  /* num_toflush */
  0,
  /* mode */
  LOGWR_MODE_ASYNC,
  /* action */
  LOGWR_ACTION_NONE,
  /* last_recv_pageid */
  NULL_PAGEID,
  /* last_flush_pageid */
  NULL_PAGEID
};

/*
 * logwr_to_physical_pageid -
 *
 * return:
 *   logical_pageid(in):
 * Note:
 */
static PAGEID
logwr_to_physical_pageid (PAGEID logical_pageid)
{
  int phy_pageid;

  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = /*LOGPB_PHYSICAL_HEADER_PAGE_ID */ 0;
    }
  else
    {
      phy_pageid = logical_pageid -
	/*LOGPB_FIRST_ACTIVE_PAGE_ID */ logwr_Gl.hdr.fpageid;

      if (phy_pageid >= /*LOGPB_ACTIVE_NPAGES */ logwr_Gl.hdr.npages)
	{
	  phy_pageid %= /*LOGPB_ACTIVE_NPAGES */ logwr_Gl.hdr.npages;
	}
      else if (phy_pageid < 0)
	{
	  phy_pageid = ( /*LOGPB_ACTIVE_NPAGES */ logwr_Gl.hdr.npages
			- ((-phy_pageid) %	/*LOGPB_ACTIVE_NPAGES */
			   logwr_Gl.hdr.npages)) % logwr_Gl.hdr.npages;
	}
      phy_pageid++;
    }

  assert (phy_pageid >= 0 && phy_pageid <= logwr_Gl.hdr.npages);
  return phy_pageid;
}

/*
 * logwr_fetch_header_page -
 *
 * return:
 *   log_pgptr(out):
 * Note:
 */
static int
logwr_fetch_header_page (LOG_PAGE * log_pgptr)
{
  PAGEID pageid;
  PAGEID phy_pageid;

  assert (log_pgptr != NULL);

  /*
   * Page is contained in the active log.
   * Find the corresponding physical page and read the page form disk.
   */
  pageid = LOGPB_HEADER_PAGE_ID;
  phy_pageid = logwr_to_physical_pageid (pageid);

  if (fileio_read (logwr_Gl.append_vdes, log_pgptr, phy_pageid) == NULL)
    {
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_READ, 3, pageid, phy_pageid, logwr_Gl.active_name);
      return ER_LOG_READ;
    }
  else
    {
      if (log_pgptr->hdr.logical_pageid != pageid)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_PAGE_CORRUPTED, 1, pageid);
	  return ER_LOG_PAGE_CORRUPTED;
	}
    }

  return NO_ERROR;
}

/*
 * logwr_read_log_header -
 *
 * return:
 * Note:
 */
static int
logwr_read_log_header (void)
{
  if (!fileio_is_volume_exist (logwr_Gl.active_name))
    {
      /* Delay to create a new log file until it gets the log header page
         from server because we need to know the npages to create an log file */
      ;
    }
  else
    {
      /* Mount the active log and read the log header */
      logwr_Gl.append_vdes = fileio_mount (logwr_Gl.db_name,
					   logwr_Gl.active_name,
					   LOG_DBLOG_ACTIVE_VOLID,
					   true, false);
      if (logwr_Gl.append_vdes == NULL_VOLDES)
	{
	  /* Unable to mount the active log */
	  return ER_IO_MOUNT_FAIL;
	}
      else
	{
	  int log_pgbuf[IO_MAX_PAGE_SIZE / sizeof (int)];
	  LOG_PAGE *log_pgptr = (LOG_PAGE *) log_pgbuf;

	  int error = logwr_fetch_header_page (log_pgptr);
	  if (error != NO_ERROR)
	    return error;

	  logwr_Gl.hdr = *((struct log_header *) log_pgptr->area);

	  assert (log_pgptr->hdr.logical_pageid == LOGPB_HEADER_PAGE_ID);
	  assert (log_pgptr->hdr.offset == NULL_OFFSET);
	}
    }

  return NO_ERROR;
}

/*
 * logwr_initialize - Initialize logwr_Gl structure
 *
 * return:
 *
 *   db_name(in):
 *   log_path(in):
 *   mode(in):
 *
 * Note:
 */
static int
logwr_initialize (const char *db_name, const char *log_path, int mode)
{
  char param_name[128];
  int log_nbuffers;
  int error;
  char *at_char = NULL;

  strcpy (logwr_Gl.db_name, db_name);
  if ((at_char = strchr (logwr_Gl.db_name, '@')) != NULL)
    {
      *at_char = '\0';
    }
  strcpy (logwr_Gl.log_path, log_path);
  logwr_Gl.mode = mode;

  strncpy (param_name, "log_buffer_pages", 127);
  if ((error = db_get_system_parameters (param_name, 127)) != NO_ERROR)
    {
      return error;
    }
  sscanf (param_name, "log_buffer_pages=%d", &log_nbuffers);

  fileio_make_log_active_name (logwr_Gl.active_name, log_path,
			       logwr_Gl.db_name);

  if (logwr_Gl.logpg_area == NULL)
    {
      logwr_Gl.logpg_area_size = log_nbuffers * SIZEOF_LOG_PAGE_PAGESIZE;
      logwr_Gl.logpg_area = malloc (logwr_Gl.logpg_area_size);
      if (logwr_Gl.logpg_area == NULL)
	{
	  logwr_Gl.logpg_area_size = 0;
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  if (logwr_Gl.toflush == NULL)
    {
      int i;

      logwr_Gl.max_toflush = log_nbuffers - 1;
      logwr_Gl.toflush = (LOG_PAGE **) calloc (logwr_Gl.max_toflush,
					       sizeof (logwr_Gl.toflush));
      if (logwr_Gl.toflush == NULL)
	{
	  logwr_Gl.max_toflush = 0;
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (i = 0; i < logwr_Gl.max_toflush; i++)
	{
	  logwr_Gl.toflush[i] = NULL;
	}
    }

  if ((error = logwr_read_log_header ()) != NO_ERROR)
    {
      return error;
    }

  logwr_Gl.last_flush_pageid = logwr_Gl.hdr.append_lsa.pageid;

  return NO_ERROR;
}

/*
 * logwr_finalize -
 *
 * return:
 * Note:
 */
static void
logwr_finalize (void)
{
  if (logwr_Gl.logpg_area != NULL)
    {
      free (logwr_Gl.logpg_area);
      logwr_Gl.logpg_area = NULL;
      logwr_Gl.logpg_area_size = 0;
      logwr_Gl.logpg_fill_size = 0;
      logwr_Gl.loghdr_pgptr = NULL;
    }
  if (logwr_Gl.toflush != NULL)
    {
      free (logwr_Gl.toflush);
      logwr_Gl.toflush = NULL;
      logwr_Gl.max_toflush = 0;
      logwr_Gl.num_toflush = 0;
    }
  if (logwr_Gl.append_vdes != NULL_VOLDES)
    {
      fileio_dismount (logwr_Gl.append_vdes);
      logwr_Gl.append_vdes = NULL_VOLDES;
    }
  logwr_Gl.last_recv_pageid = NULL_PAGEID;
  logwr_Gl.mode = LOGWR_MODE_ASYNC;
  logwr_Gl.action = LOGWR_ACTION_NONE;
}

/*
 * logwr_set_hdr_and_flush_info -
 *
 * return:
 * Note:
 */
int
logwr_set_hdr_and_flush_info (void)
{
  LOG_PAGE *log_pgptr, *last_pgptr;
  char *p;
  int num_toflush = 0;

  /* Set the flush information */
  p = logwr_Gl.logpg_area + SIZEOF_LOG_PAGE_PAGESIZE;
  while (p < (logwr_Gl.logpg_area + logwr_Gl.logpg_fill_size))
    {
      log_pgptr = (LOG_PAGE *) p;
      if (num_toflush == 0)
	{
	  /* Check if it need archiving */
	  if ((logwr_Gl.last_recv_pageid != NULL_PAGEID) &&
	      (logwr_to_physical_pageid (logwr_Gl.last_recv_pageid) >
	       logwr_to_physical_pageid (log_pgptr->hdr.logical_pageid)))
	    {
	      logwr_Gl.action |= LOGWR_ACTION_ARCHIVING;
	    }
	}
      logwr_Gl.toflush[num_toflush++] = log_pgptr;
      p += SIZEOF_LOG_PAGE_PAGESIZE;
    }
  last_pgptr = log_pgptr;
  logwr_Gl.num_toflush = num_toflush;

  /* Set the header and action information */
  if (num_toflush > 0)
    {
      log_pgptr = (LOG_PAGE *) logwr_Gl.logpg_area;
      logwr_Gl.hdr = *((struct log_header *) log_pgptr->area);
      logwr_Gl.loghdr_pgptr = log_pgptr;

      /* During the server is archiving, current active log is flushed
         and sent to Log Writer. And, the append_lsa in the log header
         indicates the first record of the active log to be newly created.
         So, the last pageid to be flushed is the previous one of the append
         pageid. */
      if (LOGWR_IS_SERVER_ARCHIVING ())
	logwr_Gl.last_flush_pageid = logwr_Gl.hdr.append_lsa.pageid - 1;
      else
	logwr_Gl.last_flush_pageid = logwr_Gl.hdr.append_lsa.pageid;

      if (last_pgptr->hdr.logical_pageid != logwr_Gl.last_flush_pageid)
	{
	  /* There are left several pages to get from the server */
	  logwr_Gl.last_recv_pageid = last_pgptr->hdr.logical_pageid;
	  logwr_Gl.action |= LOGWR_ACTION_DELAYED_WRITE;
	}
      else
	{
	  logwr_Gl.last_recv_pageid = logwr_Gl.last_flush_pageid;

	  if (logwr_Gl.hdr.perm_status == LOG_PSTAT_HDRFLUSH_INPPROCESS
	      || logwr_Gl.action & LOGWR_ACTION_DELAYED_WRITE)
	    {
	      /* In case that it finishes delay write or
	         it is the time of periodic header flushing */
	      logwr_Gl.action |= LOGWR_ACTION_HDR_WRITE;
	      logwr_Gl.action &= ~LOGWR_ACTION_DELAYED_WRITE;
	    }
	}
    }
  else
    {
      /* If it gets only the header page, compares both of the headers.
         There is no update for the header information */
      struct log_header hdr;
      log_pgptr = (LOG_PAGE *) logwr_Gl.logpg_area;
      hdr = *((struct log_header *) log_pgptr->area);
      if (difftime (hdr.db_creation, logwr_Gl.hdr.db_creation) != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1,
		  logwr_Gl.active_name);
	  return ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
	}
      /* To get the last page again, decrease last pageid */
      logwr_Gl.last_recv_pageid = logwr_Gl.hdr.append_lsa.pageid - 1;
      logwr_Gl.last_flush_pageid = logwr_Gl.hdr.append_lsa.pageid;
    }
  if (logwr_Gl.hdr.ha_file_status == LOG_HA_FILESTAT_CLEAR)
    {
      /* In case of delayed write,
         save the append lsa of the log to be written locally */
      logwr_Gl.hdr.append_lsa.pageid = logwr_Gl.last_recv_pageid;
      logwr_Gl.hdr.append_lsa.offset = NULL_OFFSET;
    }
  return NO_ERROR;
}

/*
 * logwr_fetch_append_pages -
 *
 * return:
 *   to_flush(in):
 *   npages(in):
 * Note:
 */
static LOG_PAGE **
logwr_writev_append_pages (LOG_PAGE ** to_flush, DKNPAGES npages)
{
  PAGEID fpageid;
  PAGEID phy_pageid;

  if (npages > 0)
    {
      fpageid = to_flush[0]->hdr.logical_pageid;
      phy_pageid = logwr_to_physical_pageid (fpageid);

      if (fileio_writev
	  (logwr_Gl.append_vdes, (void **) to_flush, phy_pageid,
	   npages) == NULL)
	{
	  if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_WRITE_OUT_OF_SPACE, 4,
		      fpageid, phy_pageid, logwr_Gl.active_name,
		      ((logwr_Gl.hdr.npages + 1 - fpageid) *
		       logwr_Gl.hdr.db_iopagesize));
	    }
	  else
	    {
	      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_LOG_WRITE, 3,
				   fpageid, phy_pageid, logwr_Gl.active_name);
	    }
	  to_flush = NULL;
	}
    }
  return to_flush;
}

/*
 * logwr_flush_all_append_pages -
 *
 * return:
 * Note:
 */
static int
logwr_flush_all_append_pages (void)
{
  LOG_PAGE *pgptr, *prv_pgptr;
  PAGEID pageid, prv_pageid;
  int last_idxflush;
  int idxflush;
  bool need_sync;
  int flush_page_count;
  int i;

  idxflush = -1;
  last_idxflush = -1;
  prv_pgptr = NULL;
  need_sync = false;
  flush_page_count = 0;

  for (i = 0; i < logwr_Gl.num_toflush; i++)
    {
      pgptr = logwr_Gl.toflush[i];

      if (last_idxflush == -1)
	{
	  /* We have found the smallest dirty page */
	  last_idxflush = i;
	  prv_pgptr = pgptr;
	  continue;
	}

      if (idxflush != -1 && prv_pgptr != NULL)
	{
	  /*
	   * This append log page should be dirty and contiguous to previous
	   * append page. If it is not, we need to flush the accumulated pages
	   * up to this point, and then start accumulating pages again.
	   */
	  pageid = pgptr->hdr.logical_pageid;
	  prv_pageid = prv_pgptr->hdr.logical_pageid;

	  if ((pageid != prv_pageid + 1)
	      || (logwr_to_physical_pageid (pageid)
		  != logwr_to_physical_pageid (prv_pageid) + 1))
	    {
	      /*
	       * This page is not contiguous.
	       *
	       * Flush the accumulated contiguous pages
	       */
	      if (logwr_writev_append_pages (&logwr_Gl.toflush[idxflush],
					     i - idxflush) == NULL)
		{
		  return er_errid ();
		}
	      else
		{
		  need_sync = true;

		  /*
		   * Start over the accumulation of pages
		   */

		  flush_page_count += i - idxflush;
		  idxflush = -1;
		}
	    }
	}

      if (idxflush == -1)
	{
	  /*
	   * This page should be included in the flush
	   */
	  idxflush = i;
	}

      /* prv_pgptr was not pgptr's previous buffer.
       * prv_pgptr was the first buffer to flush,
       * so only 2 continous pages always were flushed together.
       */
      prv_pgptr = pgptr;
    }

  /*
   * If there are any accumulated pages, flush them at this point
   */

  if (idxflush != -1)
    {
      int page_toflush = logwr_Gl.num_toflush - idxflush;

      /* last countious pages */
      if (logwr_writev_append_pages (&logwr_Gl.toflush[idxflush],
				     page_toflush) == NULL)
	{
	  return er_errid ();
	}
      else
	{
	  need_sync = true;
	  flush_page_count += page_toflush;
	  pgptr = logwr_Gl.toflush[idxflush + page_toflush - 1];
	}
    }

  /*
   * Make sure that all of the above log writes are synchronized with any
   * future log writes. That is, the pages should be stored on physical disk.
   */

  if (need_sync == true
      && fileio_synchronize (logwr_Gl.append_vdes,
			     !PRM_SUPPRESS_FSYNC) == NULL_VOLDES)
    {
      return er_errid ();
    }

  assert (last_idxflush != -1);
  if (last_idxflush != -1)
    {
      /*
       * Now flush and sync the first log append dirty page
       */
      ++flush_page_count;

      if (logwr_writev_append_pages
	  (&logwr_Gl.toflush[last_idxflush], 1) == NULL
	  || fileio_synchronize (logwr_Gl.append_vdes,
				 !PRM_SUPPRESS_FSYNC) == NULL_VOLDES)
	{
	  PAGEID pageid = logwr_Gl.toflush[last_idxflush]->hdr.logical_pageid;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_WRITE, 3, pageid, logwr_to_physical_pageid (pageid),
		  logwr_Gl.active_name);
	  return ER_LOG_WRITE;
	}
    }

  /* Initialize flush info */
  for (i = 0; i < logwr_Gl.num_toflush; i++)
    {
      logwr_Gl.toflush[i] = NULL;
    }
  logwr_Gl.num_toflush = 0;

#if defined(LOGWR_DEBUG_MSG)
  er_log_debug (ARG_FILE_LINE,
		"logwr_write_log_pages, flush_page_count(%d)\n",
		flush_page_count);
#endif /* LOGWR_DEBUG_MSG */

  return NO_ERROR;
}

/*
 * logwr_flush_header_page -
 *
 * return:
 * Note:
 */
void
logwr_flush_header_page (void)
{
  PAGEID phy_pageid;
  PAGEID logical_pageid;
  int nbytes;

  if (logwr_Gl.loghdr_pgptr == NULL)
    return;

  memcpy (logwr_Gl.loghdr_pgptr->area, &logwr_Gl.hdr, sizeof (logwr_Gl.hdr));

  logical_pageid = LOGPB_HEADER_PAGE_ID;
  phy_pageid = logwr_to_physical_pageid (logical_pageid);

  /* logwr_Gl.append_vdes is only changed
   * while starting or finishing or recovering server.
   * So, log cs is not needed.
   */
  if (fileio_write (logwr_Gl.append_vdes, logwr_Gl.loghdr_pgptr,
		    phy_pageid) == NULL
      || fileio_synchronize (logwr_Gl.append_vdes,
			     !PRM_SUPPRESS_FSYNC) == NULL_VOLDES)
    {

      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	{
	  nbytes = ((logwr_Gl.hdr.npages + 1 - logical_pageid) *
		    logwr_Gl.hdr.db_iopagesize);
	  er_set (ER_FATAL_ERROR_SEVERITY,
		  ARG_FILE_LINE,
		  ER_LOG_WRITE_OUT_OF_SPACE,
		  4, logical_pageid, phy_pageid, logwr_Gl.active_name,
		  nbytes);
	}
      else
	{
	  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY,
			       ARG_FILE_LINE,
			       ER_LOG_WRITE,
			       3,
			       logical_pageid, phy_pageid,
			       logwr_Gl.active_name);
	}
    }
#if defined(LOGWR_DEBUG_MSG)
  {
    int ha_server_status = logwr_Gl.hdr.ha_server_status;
    int ha_file_status = logwr_Gl.hdr.ha_file_status;
    er_log_debug (ARG_FILE_LINE,
		  "logwr_flush_header_page, ha_server_status=%s, ha_file_status=%s\n",
		  ha_server_status ==
		  LOG_HA_SRVSTAT_ACTIVE ? "active" : (ha_server_status ==
						      LOG_HA_SRVSTAT_TO_BE_ACTIVE
						      ? "t-b-a"
						      : (ha_server_status ==
							 LOG_HA_SRVSTAT_STANDBY
							 ? "standby"
							 : (ha_server_status
							    ==
							    LOG_HA_SRVSTAT_TO_BE_STANDBY
							    ? "t-b-s" :
							    "dead"))),
		  ha_file_status ==
		  LOG_HA_FILESTAT_SYNCHRONIZED ? "sync" : "unsync");
  }
#endif /* LOGWR_DEBUG_MSG */
}

/*
 * logwr_archive_active_log -
 *
 * return:
 * Note:
 */
static int
logwr_archive_active_log (void)
{
  char archive_name[PATH_MAX] = { '\0' };
  LOG_PAGE *arvhdr_pgptr = NULL;
  struct log_arv_header *arvhdr;
  int log_pgbuf[IO_MAX_PAGE_SIZE / sizeof (int)];
  LOG_PAGE *log_pgptr = NULL;
  LOG_PAGE *malloc_arv_hdr_pgptr = NULL;
  PAGEID pageid, phy_pageid = NULL_PAGEID;
  int vdes = NULL_VOLDES;
  int error_code;

  /* Create the archive header page */
  malloc_arv_hdr_pgptr = (LOG_PAGE *) malloc (IO_PAGESIZE);
  if (malloc_arv_hdr_pgptr == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      IO_PAGESIZE);
      goto error;
    }

  malloc_arv_hdr_pgptr->hdr.logical_pageid = LOGPB_HEADER_PAGE_ID;
  malloc_arv_hdr_pgptr->hdr.offset = NULL_OFFSET;

  /* Construct the archive log header */
  arvhdr = (struct log_arv_header *) malloc_arv_hdr_pgptr->area;
  strncpy (arvhdr->magic, CUBRID_MAGIC_LOG_ARCHIVE, CUBRID_MAGIC_MAX_LENGTH);
  arvhdr->db_creation = logwr_Gl.hdr.db_creation;
  arvhdr->next_trid = NULL_TRANID;
  arvhdr->npages = logwr_Gl.hdr.npages;

  /* Here, the last pageid to be received is the last page to be written
     at active log after the archiving is done */
  arvhdr->arv_num = logwr_Gl.last_recv_pageid / logwr_Gl.hdr.npages - 1;
  arvhdr->fpageid = logwr_Gl.hdr.npages * arvhdr->arv_num;

  /*
   * Now create the archive and start copying pages
   */

  fileio_make_log_archive_name (archive_name, logwr_Gl.log_path,
				logwr_Gl.db_name, arvhdr->arv_num);
  vdes =
    fileio_format (NULL, logwr_Gl.db_name, archive_name,
		   LOG_DBLOG_ARCHIVE_VOLID, arvhdr->npages + 1, false, false,
		   false);
  if (vdes == NULL_VOLDES)
    {
      /* Unable to create archive log to archive */
      error_code = ER_LOG_CREATE_LOGARCHIVE_FAIL;
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_CREATE_LOGARCHIVE_FAIL, 3,
	      archive_name, arvhdr->fpageid,
	      arvhdr->fpageid + arvhdr->npages - 1);
      goto error;
    }

  if (fileio_write (vdes, malloc_arv_hdr_pgptr, 0) == NULL)
    {
      /* Error archiving header page into archive */
      error_code = ER_LOG_WRITE;
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3,
	      0, 0, archive_name);
      goto error;
    }

  /* TODO: rename the active log as the archive log instead of copy for it */

  log_pgptr = (LOG_PAGE *) log_pgbuf;

  /* Now start dumping the current active pages to archive */
  for (pageid = arvhdr->fpageid; phy_pageid < arvhdr->npages; pageid++)
    {
      /*
       * Page is contained in the active log.
       * Find the corresponding physical page and read the page form disk.
       */
      phy_pageid = logwr_to_physical_pageid (pageid);

      if (fileio_read (logwr_Gl.append_vdes, log_pgptr, phy_pageid) == NULL)
	{
	  error_code = ER_LOG_READ;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_READ, 3, pageid, phy_pageid, logwr_Gl.active_name);
	  goto error;
	}
      else
	{
	  if (log_pgptr->hdr.logical_pageid != pageid)
	    {
	      /* Clean the buffer... since it may be corrupted */
	      error_code = ER_LOG_PAGE_CORRUPTED;
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_PAGE_CORRUPTED, 1, pageid);
	      goto error;
	    }
	}

      if (fileio_write (vdes, log_pgptr, phy_pageid) == NULL)
	{
	  error_code = ER_LOG_WRITE;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3,
		  pageid, phy_pageid, archive_name);
	  goto error;
	}
    }

  /*
   * Make sure that the whole log archive is in physical storage at this
   * moment
   */
  if (fileio_synchronize (vdes, true) == NULL_VOLDES)
    {
      error_code = er_errid ();
      goto error;
    }

  /* Flush the log header to reflect the archive */
  logwr_flush_header_page ();

#if defined(LOGWR_DEBUG_MSG)
  er_log_debug (ARG_FILE_LINE,
		"logwr_archive_active_log, arv_num(%d)\n", arvhdr->arv_num);

#endif

  free_and_init (malloc_arv_hdr_pgptr);

  return NO_ERROR;

error:

  if (malloc_arv_hdr_pgptr != NULL)
    {
      free_and_init (malloc_arv_hdr_pgptr);
    }

  if (vdes != NULL_VOLDES)
    {
      fileio_dismount (vdes);
      fileio_unformat (archive_name);
    }

  return error_code;
}

/*
 * logwr_write_log_pages -
 *
 * return:
 * Note:
 */
int
logwr_write_log_pages (void)
{
  int error;

  if (logwr_Gl.num_toflush <= 0)
    return NO_ERROR;

  if (logwr_Gl.append_vdes == NULL_VOLDES &&
      !fileio_is_volume_exist (logwr_Gl.active_name))
    {
      /* Create a new active log */
      logwr_Gl.append_vdes = fileio_format (NULL,
					    logwr_Gl.db_name,
					    logwr_Gl.active_name,
					    LOG_DBLOG_ACTIVE_VOLID,
					    (logwr_Gl.hdr.npages + 1),
					    PRM_LOG_SWEEP_CLEAN, true, false);
      if (logwr_Gl.append_vdes == NULL_VOLDES)
	{
	  /* Unable to create an active log */
	  return ER_IO_FORMAT_FAIL;
	}
    }

  if (logwr_Gl.action & LOGWR_ACTION_ARCHIVING)
    {
      if ((error = logwr_archive_active_log ()) != NO_ERROR)
	return error;
    }

  if ((error = logwr_flush_all_append_pages ()) != NO_ERROR)
    return error;

  /* TODO: periodic header flush */
  if (true			/*!(logwr_Gl.action & LOGWR_ACTION_DELAYED_WRITE) */
      && true /*logwr_Gl.action & LOGWR_ACTION_HDR_WRITE */ )
    {
      logwr_flush_header_page ();
    }

  return NO_ERROR;
}

/*
 * logwr_copy_log_file -
 *
 * return: NO_ERROR if successful, error_code otherwise
 *
 *   db_name(in): database name to copy the log file
 *   log_path(in): file pathname to copy the log file
 *   mode(in): LOGWR_MODE_SYNC, LOGWR_MODE_ASYNC or LOGWR_MODE_SEMISYNC
 *
 * Note:
 */
int
logwr_copy_log_file (const char *db_name, const char *log_path, int mode)
{
  LOGWR_CONTEXT ctx = { -1, 0, false };
  int error = NO_ERROR;

  if ((error = logwr_initialize (db_name, log_path, mode)) != NO_ERROR)
    {
      logwr_finalize ();
      return error;
    }
  while (!ctx.shutdown)
    {
      if ((error = logwr_get_log_pages (&ctx)) != NO_ERROR)
	{
	  ctx.last_error = error;
	}
      else
	{
	  if (logwr_Gl.action & LOGWR_ACTION_ASYNC_WRITE)
	    {
	      if ((error = logwr_write_log_pages ()) != NO_ERROR)
		{
		  ctx.last_error = error;
		}
	    }
	}
      logwr_Gl.action &= LOGWR_ACTION_DELAYED_WRITE;
    }
  logwr_finalize ();
  return error;
}
#else /* CS_MODE */
int
logwr_copy_log_file (const char *db_name, const char *log_path, int mode)
{
  return ER_FAILED;
}
#endif /* !CS_MODE */

#if defined(SERVER_MODE)
/*
 * logwr_register_writer_entry -
 *
 * return:
 *
 *   wr_entry_p(out):
 *   id(in):
 *   fpageid(in):
 *   mode(in):
 *
 * Note:
 */
static int
logwr_register_writer_entry (LOGWR_ENTRY ** wr_entry_p,
			     THREAD_ENTRY * thread_p,
			     PAGEID fpageid, int mode)
{
  LOGWR_ENTRY *entry;
  int rv;
  LOGWR_INFO *writer_info = &log_Gl.writer_info;

  *wr_entry_p = NULL;
  LOG_MUTEX_LOCK (rv, writer_info->wr_list_mutex);

  entry = writer_info->writer_list;
  while (entry)
    {
      if (entry->thread_p == thread_p)
	break;
      entry = entry->next;
    }

  if (entry == NULL)
    {
      entry = malloc (sizeof (LOGWR_ENTRY));
      if (entry == NULL)
	{
	  LOG_MUTEX_UNLOCK (writer_info->wr_list_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (LOGWR_ENTRY));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      entry->thread_p = thread_p;
      entry->fpageid = fpageid;
      entry->mode = mode;
      entry->status = LOGWR_STATUS_DELAY;

      entry->next = writer_info->writer_list;
      writer_info->writer_list = entry;
    }
  else
    {
      entry->fpageid = fpageid;
      entry->mode = mode;
      if (entry->status != LOGWR_STATUS_DELAY)
	entry->status = LOGWR_STATUS_WAIT;
    }

  LOG_MUTEX_UNLOCK (writer_info->wr_list_mutex);
  *wr_entry_p = entry;

  return NO_ERROR;
}

/*
 * logwr_unregister_writer_entry -
 *
 * return:
 *
 *   wr_entry(in):
 *   status(in):
 *
 * Note:
 */
static bool
logwr_unregister_writer_entry (LOGWR_ENTRY * wr_entry, int status)
{
  LOGWR_ENTRY *entry;
  bool is_all_done;
  int rv;
  LOGWR_INFO *writer_info = &log_Gl.writer_info;

  LOG_MUTEX_LOCK (rv, writer_info->wr_list_mutex);

  wr_entry->status = status;

  entry = writer_info->writer_list;
  while (entry)
    {
      if (entry->status == LOGWR_STATUS_FETCH)
	break;
      entry = entry->next;
    }

  is_all_done = (entry == NULL) ? true : false;

  if (status == LOGWR_STATUS_ERROR)
    {
      LOGWR_ENTRY *prev_entry = NULL;
      entry = writer_info->writer_list;
      while (entry)
	{
	  if (entry == wr_entry)
	    {
	      if (entry == writer_info->writer_list)
		{
		  writer_info->writer_list = entry->next;
		}
	      else
		{
		  prev_entry->next = entry->next;
		}
	      free (entry);
	      break;
	    }
	  prev_entry = entry;
	  entry = entry->next;
	}
    }
  LOG_MUTEX_UNLOCK (writer_info->wr_list_mutex);

  return is_all_done;
}

/*
 * logwr_get_server_status -
 *
 * return:
 * Note:
 */
static int
logwr_get_server_status (void)
{
  switch (css_ha_server_state ())
    {
    case HA_SERVER_STATE_ACTIVE:
      return LOG_HA_SRVSTAT_ACTIVE;
    case HA_SERVER_STATE_TO_BE_ACTIVE:
      return LOG_HA_SRVSTAT_TO_BE_ACTIVE;
    case HA_SERVER_STATE_STANDBY:
      return LOG_HA_SRVSTAT_STANDBY;
    case HA_SERVER_STATE_TO_BE_STANDBY:
      return LOG_HA_SRVSTAT_TO_BE_STANDBY;
    case HA_SERVER_STATE_DEAD:
      return LOG_HA_SRVSTAT_DEAD;
    default:
      return LOG_HA_SRVSTAT_IDLE;
    }
}

/*
 * logwr_pack_log_pages -
 *
 * return:
 *
 *   thread_p(in):
 *   logpg_area(in):
 *   logpg_used_size(out):
 *   status(out): LOGWR_STATUS_DONE, LOGWR_STATUS_DELAY or LOGWR_STATUS_ERROR
 *   entry(in):
 *
 * Note:
 */
static int
logwr_pack_log_pages (THREAD_ENTRY * thread_p,
		      char *logpg_area, int *logpg_used_size,
		      int *status, LOGWR_ENTRY * entry)
{
  PAGEID fpageid, lpageid, pageid;
  char *p;
  LOG_PAGE *log_pgptr;
  int num_logpgs;
  bool is_hdr_page_only;
  int error_code;

  fpageid = NULL_PAGEID;
  lpageid = NULL_PAGEID;

  is_hdr_page_only = (entry->fpageid == LOGPB_HEADER_PAGE_ID);

  if (!is_hdr_page_only)
    {
      /* Find the first pageid to be packed */
      fpageid = entry->fpageid;
      if (fpageid == NULL_PAGEID)
	{
	  /* In case of first request from the log writer,
	     pack all active pages to be flushed until now */
	  fpageid = log_Gl.hdr.fpageid;
	}
      else if (fpageid > log_Gl.hdr.eof_lsa.pageid)
	{
	  /* Will it be happened ? */
	  error_code = ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
	  goto error;
	}

      if (fpageid > log_Gl.append.nxio_lsa.pageid)
	{
	  fpageid = log_Gl.append.nxio_lsa.pageid;
	}

      /* Find the last pageid to be packed */
      lpageid = log_Gl.hdr.eof_lsa.pageid;

      /* Check if it is out of bound by several limitations */

      /* 1. Pack the pages which are in the same log file */
      if (logpb_is_page_in_archive (fpageid)
	  && (fpageid < log_Gl.hdr.fpageid))
	{
	  struct log_arv_header arvhdr;

	  /* If fpageid is in archive log, fetch the archive page
	     and the header page in the archive */
	  if (logpb_fetch_header_from_archive (thread_p, fpageid, &arvhdr)
	      != NO_ERROR)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	  /* Reset the lpageid with the last pageid in the archive */
	  lpageid = arvhdr.fpageid + arvhdr.npages - 1;
	}

      /* 2. Pack the pages which can be in the page area of Log Writer */
      if ((lpageid - fpageid + 1) > (PRM_LOG_NBUFFERS - 1))
	{
	  lpageid = fpageid + (PRM_LOG_NBUFFERS - 1) - 1;
	}
    }

  /* Set the server status on the header information */
  log_Gl.hdr.ha_server_status = logwr_get_server_status ();
  log_Gl.hdr.ha_file_status = (lpageid < log_Gl.hdr.eof_lsa.pageid)
    ? LOG_HA_FILESTAT_CLEAR : LOG_HA_FILESTAT_SYNCHRONIZED;

  /* Allocate the log page area */
  num_logpgs = (is_hdr_page_only) ? 1 : (lpageid - fpageid + 1) + 1;

  assert (lpageid >= fpageid);
  assert (num_logpgs <= PRM_LOG_NBUFFERS);

  p = logpg_area;

  /* Fill the header page */
  log_pgptr = (LOG_PAGE *) p;
  log_pgptr->hdr = log_Gl.loghdr_pgptr->hdr;
  memcpy (log_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr));
  p += SIZEOF_LOG_PAGE_PAGESIZE;

  /* Fill the page array with the pages to send */
  if (!is_hdr_page_only)
    {
      for (pageid = fpageid; pageid >= 0 && pageid <= lpageid; pageid++)
	{
	  log_pgptr = (LOG_PAGE *) p;
	  if (logpb_fetch_page (thread_p, pageid, log_pgptr) == NULL)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	  assert (pageid == (log_pgptr->hdr.logical_pageid));
	  p += SIZEOF_LOG_PAGE_PAGESIZE;
	}
    }

  *logpg_used_size = (int) (p - logpg_area);

  /* In case that EOL exists at lpageid */
  if (!is_hdr_page_only && (lpageid >= log_Gl.hdr.eof_lsa.pageid))
    {
      *status = LOGWR_STATUS_DONE;
    }
  else
    {
      *status = LOGWR_STATUS_DELAY;
    }

#if defined(LOGWR_DEBUG_MSG)
  er_log_debug (ARG_FILE_LINE,
		"logwr_pack_log_pages, fpageid(%d), lpageid(%d), num_pages(%d),"
		"\n status(%d), delayed_free_log_pgptr(%p)\n",
		fpageid, lpageid, num_logpgs, entry->status,
		log_Gl.append.delayed_free_log_pgptr);
#endif /* LOGWR_DEBUG_MSG */

  return NO_ERROR;

error:

  *logpg_used_size = 0;
  *status = LOGWR_STATUS_ERROR;

  return error_code;
}

/*
 * xlogwr_get_log_pages -
 *
 * return:
 *
 *   thread_p(in):
 *   first_pageid(in):
 *   mode(in):
 *
 * Note:
 */
int
xlogwr_get_log_pages (THREAD_ENTRY * thread_p, PAGEID first_pageid, int mode)
{
  LOGWR_ENTRY *entry;
  char *logpg_area;
  int logpg_used_size;
  PAGEID next_fpageid;
  int next_mode;
  int status;
  int rv;
  int error_code;
  bool check_cs_own = false;
  LOGWR_INFO *writer_info = &log_Gl.writer_info;

  logpg_used_size = 0;
  logpg_area = db_private_alloc (thread_p,
				 PRM_LOG_NBUFFERS * SIZEOF_LOG_PAGE_PAGESIZE);
  if (logpg_area == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      PRM_LOG_NBUFFERS * SIZEOF_LOG_PAGE_PAGESIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  while (true)
    {
#if defined(LOGWR_DEBUG_MSG)
      er_log_debug (ARG_FILE_LINE,
		    "[tid:%ld] xlogwr_get_log_pages, fpageid(%d), mode(%s)\n",
		    thread_p->tid, first_pageid,
		    (mode == LOGWR_MODE_SYNC ? "sync" :
		     (mode == LOGWR_MODE_ASYNC ? "async" : "semisync")));
#endif /* LOGWR_DEBUG_MSG */

      /* Register the writer at the list and wait until LFT start to work */
      LOG_MUTEX_LOCK (rv, writer_info->flush_start_mutex);
      error_code = logwr_register_writer_entry (&entry, thread_p,
						first_pageid, mode);
      if (error_code != NO_ERROR)
	{
	  LOG_MUTEX_UNLOCK (writer_info->flush_start_mutex);
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}
      if (entry->status == LOGWR_STATUS_WAIT)
	{
	  bool continue_checking = true;

	  thread_suspend_with_other_mutex (thread_p,
					   &(writer_info->flush_start_mutex));
	  LOG_MUTEX_UNLOCK (writer_info->flush_start_mutex);

	  if (logtb_is_interrupt (thread_p, false, &continue_checking))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPT, 0);
	      error_code = ER_INTERRUPT;
	      goto error;
	    }
	}
      else if (entry->status == LOGWR_STATUS_DELAY)
	{
	  LOG_MUTEX_UNLOCK (writer_info->flush_start_mutex);
	  LOG_CS_ENTER (thread_p);
	  check_cs_own = true;
	}

      /* Send the log pages to be flushed until now */
      error_code =
	logwr_pack_log_pages (thread_p, logpg_area, &logpg_used_size,
			      &status, entry);
      if (error_code != NO_ERROR)
	{
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}
      error_code =
	xlog_send_log_pages_to_client (thread_p,
				       logpg_area, logpg_used_size, mode);
      if (error_code != NO_ERROR)
	{
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      /* In case of async mode, unregister the writer and wakeup LFT to finish */
      /*
         The result mode is the following.

         transition \ req mode |  req_sync   req_async
         -----------------------------------------
         delay -> delay    |  n/a        ASYNC
         delay -> done     |  n/a        SYNC
         wait -> delay     |  SYNC       ASYNC
         wait -> done      |  SYNC       ASYNC
       */

      if (mode == LOGWR_MODE_ASYNC
	  && (entry->status != LOGWR_STATUS_DELAY
	      || (entry->status == LOGWR_STATUS_DELAY
		  && status != LOGWR_STATUS_DONE)))
	{
	  if (check_cs_own)
	    {
	      check_cs_own = false;
	      LOG_CS_EXIT ();
	    }
	  LOG_MUTEX_LOCK (rv, writer_info->flush_end_mutex);
	  if (logwr_unregister_writer_entry (entry, status))
	    {
	      COND_SIGNAL (writer_info->flush_end_cond);
	    }
	  LOG_MUTEX_UNLOCK (writer_info->flush_end_mutex);
	}

      /* Get the next request from the client and reset the arguments */
      error_code =
	xlog_get_page_request_with_reply (thread_p, &next_fpageid,
					  &next_mode);
      if (error_code != NO_ERROR)
	{
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      /* In case of sync mode, unregister the writer and wakeup LFT to finish */
      if (mode != LOGWR_MODE_ASYNC
	  || (entry->status == LOGWR_STATUS_DELAY
	      && status == LOGWR_STATUS_DONE))
	{
	  if (check_cs_own)
	    {
	      check_cs_own = false;
	      LOG_CS_EXIT ();
	    }
	  LOG_MUTEX_LOCK (rv, writer_info->flush_end_mutex);
	  if (logwr_unregister_writer_entry (entry, status))
	    {
	      COND_SIGNAL (writer_info->flush_end_cond);
	    }
	  LOG_MUTEX_UNLOCK (writer_info->flush_end_mutex);
	}

      /* Reset the arguments for the next request */
      first_pageid = next_fpageid;
      mode = next_mode;
    }

  db_private_free_and_init (thread_p, logpg_area);

  return NO_ERROR;

error:

#if defined(LOGWR_DEBUG_MSG)
  er_log_debug (ARG_FILE_LINE,
		"[tid:%ld] xlogwr_get_log_pages, error(%d)\n",
		thread_p->tid, error_code);
#endif /* LOGWR_DEBUG_MSG */
  if (check_cs_own)
    {
      LOG_CS_EXIT ();
    }
  LOG_MUTEX_LOCK (rv, writer_info->flush_end_mutex);
  if (logwr_unregister_writer_entry (entry, status))
    {
      COND_SIGNAL (writer_info->flush_end_cond);
    }
  LOG_MUTEX_UNLOCK (writer_info->flush_end_mutex);

  db_private_free_and_init (thread_p, logpg_area);

  return error_code;
}
#endif /* SERVER_MODE */
