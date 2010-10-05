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
#include "message_catalog.h"
#include "misc_string.h"
#include "intl_support.h"
#include "system_parameter.h"
#include "file_io.h"
#include "log_writer.h"
#include "network_interface_cl.h"
#include "network_interface_sr.h"
#include "connection_support.h"
#if defined(SERVER_MODE)
#include "memory_alloc.h"
#include "server_support.h"
#include "thread_impl.h"
#endif
#include "dbi.h"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif

#if defined(CS_MODE)
LOGWR_GLOBAL logwr_Gl = {
  /* hdr */
  {{'0'}
   , 0, 0, {'0'}
   , 0.0, 0, 0, 0, 0, 0, 0, 0, 0,
   {NULL_PAGEID, NULL_OFFSET}
   ,
   {NULL_PAGEID, NULL_OFFSET}
   ,
   NULL_PAGEID, NULL_PAGEID, -1, -1, -1, false,
   {NULL_PAGEID, NULL_OFFSET}
   ,
   {NULL_PAGEID, NULL_OFFSET}
   ,
   {NULL_PAGEID, NULL_OFFSET}
   ,
   {'0'}
   , 0, 0, 0,
   {{0, 0, 0, 0, 0}
    }
   ,
   0, 0,
   {NULL_PAGEID, NULL_OFFSET}
   }
  ,
  /* loghdr_pgptr */
  NULL,
  /* db_name */
  {'0'}
  ,
  /* log_path */
  {'0'}
  ,
  /* loginf_path */
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
  /* last_chkpt_pageid */
  NULL_PAGEID,
  /* last_recv_pageid */
  NULL_PAGEID,
  /* last_arv_fpageid */
  NULL_PAGEID,
  /* last_arv_lpageid */
  NULL_PAGEID,
  /* last_arv_num */
  -1,
  /* last_deleted_arv_num */
  -1,
  /* force_flush */
  false,
  /* last_flush_time */
  {0, 0}
  ,
  /* background archiving info */
  {NULL_PAGEID, NULL_PAGEID, NULL_VOLDES
#if defined(SERVER_MODE)
   , MUTEX_INITIALIZER
#endif /* SERVER_MODE */
   }
  ,
  /* bg_archive_name */
  {'0'}
};


static int logwr_fetch_header_page (LOG_PAGE * log_pgptr);
static int logwr_read_log_header (void);
static int logwr_initialize (const char *db_name, const char *log_path,
			     int mode);
static void logwr_finalize (void);
static LOG_PAGE **logwr_writev_append_pages (LOG_PAGE ** to_flush,
					     DKNPAGES npages);
static int logwr_flush_all_append_pages (void);
static int logwr_archive_active_log (void);
static int logwr_background_archiving (void);

/*
 * logwr_to_physical_pageid -
 *
 * return:
 *   logical_pageid(in):
 * Note:
 */
PAGEID
logwr_to_physical_pageid (PAGEID logical_pageid)
{
  int phy_pageid;

  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = 0;
    }
  else
    {
      phy_pageid = logical_pageid - logwr_Gl.hdr.fpageid;

      if (phy_pageid >= logwr_Gl.hdr.npages)
	{
	  phy_pageid %= logwr_Gl.hdr.npages;
	}
      else if (phy_pageid < 0)
	{
	  phy_pageid = (logwr_Gl.hdr.npages
			- ((-phy_pageid) % logwr_Gl.hdr.npages));
	}
      phy_pageid++;
      if (phy_pageid > logwr_Gl.hdr.npages)
	{
	  phy_pageid %= logwr_Gl.hdr.npages;
	}
    }

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

  if (fileio_read (NULL, logwr_Gl.append_vdes, log_pgptr, phy_pageid,
		   LOG_PAGESIZE) == NULL)
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
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr;
  int error = NO_ERROR;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  if (!fileio_is_volume_exist (logwr_Gl.active_name))
    {
      /* Delay to create a new log file until it gets the log header page
         from server because we need to know the npages to create an log file */
      ;
    }
  else
    {
      /* Mount the active log and read the log header */
      logwr_Gl.append_vdes = fileio_mount (NULL, logwr_Gl.db_name,
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
	  error = logwr_fetch_header_page (log_pgptr);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }

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
  char prm_buf[LINE_MAX], *prm_val;
  int log_nbuffers;
  int error;
  char *at_char = NULL;

  /* set the db name and log path */
  strncpy (logwr_Gl.db_name, db_name, PATH_MAX - 1);
  if ((at_char = strchr (logwr_Gl.db_name, '@')) != NULL)
    {
      *at_char = '\0';
    }
  strncpy (logwr_Gl.log_path, log_path, PATH_MAX - 1);
  /* set the mode */
  logwr_Gl.mode = mode;

  /* set the active log file path */
  fileio_make_log_active_name (logwr_Gl.active_name, log_path,
			       logwr_Gl.db_name);
  /* set the log info file path */
  fileio_make_log_info_name (logwr_Gl.loginf_path, log_path,
			     logwr_Gl.db_name);
  /* background archive file path */
  fileio_make_log_archive_temp_name (logwr_Gl.bg_archive_name, log_path,
				     logwr_Gl.db_name);


  strncpy (prm_buf, PRM_NAME_LOG_NBUFFERS, LINE_MAX - 1);
  if ((error = db_get_system_parameters (prm_buf, LINE_MAX)) != NO_ERROR)
    {
      return error;
    }
  prm_val = strchr (prm_buf, '=');
  if (prm_val == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, prm_buf);
      return ER_PRM_BAD_VALUE;
    }
  log_nbuffers = atoi (prm_val + 1);
  if (log_nbuffers < 1 || INT_MAX <= log_nbuffers)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_PRM_BAD_VALUE, 1, prm_buf);
      return ER_PRM_BAD_VALUE;
    }

  if (logwr_Gl.logpg_area == NULL)
    {
      logwr_Gl.logpg_area_size = log_nbuffers * LOG_PAGESIZE;
      logwr_Gl.logpg_area = malloc (logwr_Gl.logpg_area_size);
      if (logwr_Gl.logpg_area == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, logwr_Gl.logpg_area_size);
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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, logwr_Gl.max_toflush * sizeof (logwr_Gl.toflush));
	  logwr_Gl.max_toflush = 0;
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      for (i = 0; i < logwr_Gl.max_toflush; i++)
	{
	  logwr_Gl.toflush[i] = NULL;
	}
    }

  error = logwr_read_log_header ();
  if (error != NO_ERROR)
    {
      return error;
    }

  logwr_Gl.action = LOGWR_ACTION_NONE;

  logwr_Gl.last_arv_fpageid = logwr_Gl.hdr.nxarv_pageid;
  logwr_Gl.last_arv_num = logwr_Gl.hdr.nxarv_num;
  logwr_Gl.last_deleted_arv_num = logwr_Gl.hdr.last_deleted_arv_num;

  logwr_Gl.force_flush = false;
  logwr_Gl.last_flush_time.tv_sec = 0;
  logwr_Gl.last_flush_time.tv_usec = 0;

  if (PRM_LOG_BACKGROUND_ARCHIVING)
    {
      BACKGROUND_ARCHIVING_INFO *bg_arv_info;
      bg_arv_info = &logwr_Gl.bg_archive_info;

      bg_arv_info->start_page_id = NULL_PAGEID;
      bg_arv_info->current_page_id = NULL_PAGEID;
      bg_arv_info->vdes = fileio_format (NULL, logwr_Gl.db_name,
					 logwr_Gl.bg_archive_name,
					 LOG_DBLOG_BG_ARCHIVE_VOLID,
					 logwr_Gl.hdr.npages + 1,
					 false, false, false, LOG_PAGESIZE);
      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  bg_arv_info->start_page_id = logwr_Gl.hdr.nxarv_pageid;
	  bg_arv_info->current_page_id = logwr_Gl.hdr.nxarv_pageid;
	}
      else
	{
	  er_log_debug (ARG_FILE_LINE,
			"Unable to create temporary archive log %s\n",
			logwr_Gl.bg_archive_name);
	}
    }

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
      fileio_dismount (NULL, logwr_Gl.append_vdes);
      logwr_Gl.append_vdes = NULL_VOLDES;
    }
  logwr_Gl.last_recv_pageid = NULL_PAGEID;
  logwr_Gl.mode = LOGWR_MODE_ASYNC;
  logwr_Gl.action = LOGWR_ACTION_NONE;

  logwr_Gl.force_flush = false;
  logwr_Gl.last_flush_time.tv_sec = 0;
  logwr_Gl.last_flush_time.tv_usec = 0;

  if (PRM_LOG_BACKGROUND_ARCHIVING)
    {
      if (logwr_Gl.bg_archive_info.vdes != NULL_VOLDES)
	{
	  fileio_dismount (NULL, logwr_Gl.bg_archive_info.vdes);
	  logwr_Gl.bg_archive_info.vdes = NULL_VOLDES;
	}
    }
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
  LOG_PAGE *log_pgptr = NULL, *last_pgptr;
  char *p;
  int num_toflush = 0;

  /* Set the flush information */
  p = logwr_Gl.logpg_area + LOG_PAGESIZE;
  while (p < (logwr_Gl.logpg_area + logwr_Gl.logpg_fill_size))
    {
      log_pgptr = (LOG_PAGE *) p;
      logwr_Gl.toflush[num_toflush++] = log_pgptr;
      p += LOG_PAGESIZE;
    }

  last_pgptr = log_pgptr;
  logwr_Gl.num_toflush = num_toflush;

  /* Set the header and action information */
  if (num_toflush > 0)
    {
      log_pgptr = (LOG_PAGE *) logwr_Gl.logpg_area;
      logwr_Gl.hdr = *((struct log_header *) log_pgptr->area);
      logwr_Gl.loghdr_pgptr = log_pgptr;

      /* Initialize archive info if it is not set */
      if (logwr_Gl.last_arv_fpageid == NULL_PAGEID
	  || logwr_Gl.last_arv_num < 0)
	{
	  logwr_Gl.last_arv_fpageid = logwr_Gl.hdr.nxarv_pageid;
	  logwr_Gl.last_arv_num = logwr_Gl.hdr.nxarv_num;
	}

      /* Check if it need archiving */
      if (((logwr_Gl.last_arv_num + 1 < logwr_Gl.hdr.nxarv_num)
	   && (logwr_Gl.hdr.ha_file_status == LOG_HA_FILESTAT_ARCHIVED)))
	{
	  /* Do delayed archiving */
	  logwr_Gl.action |= LOGWR_ACTION_ARCHIVING;
	  logwr_Gl.last_arv_lpageid = logwr_Gl.last_recv_pageid;
	}
      else if ((logwr_Gl.last_arv_num + 1 == logwr_Gl.hdr.nxarv_num)
	       && (last_pgptr->hdr.logical_pageid >=
		   logwr_Gl.hdr.nxarv_pageid))
	{
	  logwr_Gl.action |= LOGWR_ACTION_ARCHIVING;
	  logwr_Gl.last_arv_lpageid = logwr_Gl.hdr.nxarv_pageid - 1;
	}

      if (last_pgptr != NULL
	  && last_pgptr->hdr.logical_pageid < logwr_Gl.hdr.eof_lsa.pageid)
	{
	  /* There are left several pages to get from the server */
	  logwr_Gl.last_recv_pageid = last_pgptr->hdr.logical_pageid;
	  logwr_Gl.action |= LOGWR_ACTION_DELAYED_WRITE;
	}
      else
	{
	  logwr_Gl.last_recv_pageid = logwr_Gl.hdr.eof_lsa.pageid;

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

      if (difftime64 (hdr.db_creation, logwr_Gl.hdr.db_creation) != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_DOESNT_CORRESPOND_TO_DATABASE, 1,
		  logwr_Gl.active_name);
	  return ER_LOG_DOESNT_CORRESPOND_TO_DATABASE;
	}

      if (logwr_Gl.hdr.ha_file_status != LOG_HA_FILESTAT_SYNCHRONIZED)
	{
	  /* In case of delayed write,
	     get last_recv_pageid from the append lsa of the local log header */
	  logwr_Gl.last_recv_pageid = logwr_Gl.hdr.append_lsa.pageid - 1;
	}
      else
	{
	  /* To get the last page again, decrease last pageid */
	  logwr_Gl.last_recv_pageid = logwr_Gl.hdr.eof_lsa.pageid - 1;
	}
    }
  if (logwr_Gl.hdr.ha_file_status != LOG_HA_FILESTAT_SYNCHRONIZED)
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

      if (fileio_writev (NULL, logwr_Gl.append_vdes, (void **) to_flush,
			 phy_pageid, npages, LOG_PAGESIZE) == NULL)
	{
	  if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_WRITE_OUT_OF_SPACE, 4,
		      fpageid, phy_pageid, logwr_Gl.active_name,
		      ((logwr_Gl.hdr.npages + 1 - fpageid) *
		       logwr_Gl.hdr.db_logpagesize));
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
  if (logwr_Gl.mode == LOGWR_MODE_ASYNC)
    {
      need_sync = false;
    }
  if (need_sync == true
      && fileio_synchronize (NULL, logwr_Gl.append_vdes,
			     logwr_Gl.active_name) == NULL_VOLDES)
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

      if (logwr_writev_append_pages (&logwr_Gl.toflush[last_idxflush], 1)
	  == NULL
	  || fileio_synchronize (NULL, logwr_Gl.append_vdes,
				 logwr_Gl.active_name) == NULL_VOLDES)
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

  er_log_debug (ARG_FILE_LINE,
		"logwr_write_log_pages, flush_page_count(%d)\n",
		flush_page_count);

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
    {
      return;
    }

  /* flush current archiving status */
  logwr_Gl.hdr.nxarv_num = logwr_Gl.last_arv_num;
  logwr_Gl.hdr.last_deleted_arv_num = logwr_Gl.last_deleted_arv_num;
  logwr_Gl.hdr.nxarv_pageid = logwr_Gl.last_arv_fpageid;
  logwr_Gl.hdr.nxarv_phy_pageid
    = logwr_to_physical_pageid (logwr_Gl.last_arv_fpageid);

  memcpy (logwr_Gl.loghdr_pgptr->area, &logwr_Gl.hdr, sizeof (logwr_Gl.hdr));

  logical_pageid = LOGPB_HEADER_PAGE_ID;
  phy_pageid = logwr_to_physical_pageid (logical_pageid);

  /* logwr_Gl.append_vdes is only changed
   * while starting or finishing or recovering server.
   * So, log cs is not needed.
   */
  if (fileio_write (NULL, logwr_Gl.append_vdes, logwr_Gl.loghdr_pgptr,
		    phy_pageid, LOG_PAGESIZE) == NULL
      || (logwr_Gl.mode != LOGWR_MODE_ASYNC
	  && fileio_synchronize (NULL, logwr_Gl.append_vdes,
				 logwr_Gl.active_name)) == NULL_VOLDES)
    {

      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	{
	  nbytes = ((logwr_Gl.hdr.npages + 1 - logical_pageid) *
		    logwr_Gl.hdr.db_logpagesize);
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_WRITE_OUT_OF_SPACE, 4, logical_pageid, phy_pageid,
		  logwr_Gl.active_name, nbytes);
	}
      else
	{
	  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_WRITE, 3, logical_pageid, phy_pageid,
			       logwr_Gl.active_name);
	}
    }

  /* save last checkpoint pageid */
  logwr_Gl.last_chkpt_pageid = logwr_Gl.hdr.chkpt_lsa.pageid;

  er_log_debug (ARG_FILE_LINE,
		"logwr_flush_header_page, ha_server_state=%s, ha_file_status=%s\n",
		css_ha_server_state_string (logwr_Gl.hdr.ha_server_state),
		logwr_Gl.hdr.ha_file_status ==
		LOG_HA_FILESTAT_SYNCHRONIZED ? "sync" : "unsync");
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
  char archive_name[PATH_MAX] = { '\0' }, archive_name_first[PATH_MAX];
  LOG_PAGE *arvhdr_pgptr = NULL;
  struct log_arv_header *arvhdr;
  char log_pgbuf[IO_MAX_PAGE_SIZE * LOGPB_IO_NPAGES + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_PAGE *malloc_arv_hdr_pgptr = NULL;
  PAGEID pageid, ar_phy_pageid = NULL_PAGEID, phy_pageid = NULL_PAGEID;
  int vdes = NULL_VOLDES;
  int i, first_arv_num_to_delete, last_arv_num_to_delete;
  int error_code;
  int num_pages = 0;
  const char *info_reason, *catmsg;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  if ((logwr_Gl.last_arv_num - logwr_Gl.last_deleted_arv_num)
      > PRM_LOG_MAX_ARCHIVES)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED, 1, PRM_LOG_MAX_ARCHIVES);

      /* Remove the log archives at this point */
      first_arv_num_to_delete = logwr_Gl.last_deleted_arv_num + 1;
      last_arv_num_to_delete = logwr_Gl.last_arv_num - PRM_LOG_MAX_ARCHIVES;
      last_arv_num_to_delete--;
      for (i = first_arv_num_to_delete; i <= last_arv_num_to_delete; i++)
	{
	  fileio_make_log_archive_name (archive_name, logwr_Gl.log_path,
					logwr_Gl.db_name, i);
	  fileio_unformat (NULL, archive_name);
	  logwr_Gl.last_deleted_arv_num = last_arv_num_to_delete;
	}
      info_reason = msgcat_message (MSGCAT_CATALOG_CUBRID,
				    MSGCAT_SET_LOG,
				    MSGCAT_LOG_MAX_ARCHIVES_HAS_BEEN_EXCEEDED);
      if (info_reason == NULL)
	{
	  info_reason = "Number of active log archives has been exceeded"
	    " the max desired number.";
	}
      catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
			       MSGCAT_SET_LOG,
			       MSGCAT_LOG_LOGINFO_REMOVE_REASON);
      if (catmsg == NULL)
	{
	  catmsg = "REMOVE: %d %s to \n%d %s.\nREASON: %s\n";
	}
      if (first_arv_num_to_delete == last_arv_num_to_delete)
	{
	  log_dump_log_info (logwr_Gl.loginf_path, false, catmsg,
			     first_arv_num_to_delete, archive_name,
			     last_arv_num_to_delete, archive_name,
			     info_reason);
	}
      else
	{
	  fileio_make_log_archive_name (archive_name_first, logwr_Gl.log_path,
					logwr_Gl.db_name,
					first_arv_num_to_delete);
	  log_dump_log_info (logwr_Gl.loginf_path, false, catmsg,
			     first_arv_num_to_delete, archive_name_first,
			     last_arv_num_to_delete, archive_name,
			     info_reason);
	}
      /* ignore error from log_dump_log_info() */

      /* It will continue.... */
    }

  /* Create the archive header page */
  malloc_arv_hdr_pgptr = (LOG_PAGE *) malloc (LOG_PAGESIZE);
  if (malloc_arv_hdr_pgptr == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      LOG_PAGESIZE);
      goto error;
    }

  malloc_arv_hdr_pgptr->hdr.logical_pageid = LOGPB_HEADER_PAGE_ID;
  malloc_arv_hdr_pgptr->hdr.offset = NULL_OFFSET;

  /* Construct the archive log header */
  arvhdr = (struct log_arv_header *) malloc_arv_hdr_pgptr->area;
  strncpy (arvhdr->magic, CUBRID_MAGIC_LOG_ARCHIVE, CUBRID_MAGIC_MAX_LENGTH);
  arvhdr->db_creation = logwr_Gl.hdr.db_creation;
  arvhdr->next_trid = NULL_TRANID;
  arvhdr->fpageid = logwr_Gl.last_arv_fpageid;
  arvhdr->arv_num = logwr_Gl.last_arv_num;
  arvhdr->npages = logwr_Gl.last_arv_lpageid - arvhdr->fpageid + 1;

  /*
   * Now create the archive and start copying pages
   */

  fileio_make_log_archive_name (archive_name, logwr_Gl.log_path,
				logwr_Gl.db_name, arvhdr->arv_num);
  bg_arv_info = &logwr_Gl.bg_archive_info;
  if (PRM_LOG_BACKGROUND_ARCHIVING && bg_arv_info->vdes != NULL_VOLDES)
    {
      vdes = bg_arv_info->vdes;
    }
  else
    {
      if (fileio_is_volume_exist (archive_name) == true)
	{
	  vdes = fileio_mount (NULL, archive_name, archive_name,
			       LOG_DBLOG_ARCHIVE_VOLID, true, false);
	  if (vdes == NULL_VOLDES)
	    {
	      error_code = ER_IO_MOUNT_FAIL;
	      goto error;
	    }
	}
      else
	{
	  vdes = fileio_format (NULL, logwr_Gl.db_name, archive_name,
				LOG_DBLOG_ARCHIVE_VOLID, arvhdr->npages + 1,
				false, false, false, LOG_PAGESIZE);
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
	}
    }

  if (fileio_write (NULL, vdes, malloc_arv_hdr_pgptr, 0, LOG_PAGESIZE) ==
      NULL)
    {
      /* Error archiving header page into archive */
      error_code = ER_LOG_WRITE;
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3,
	      0, 0, archive_name);
      goto error;
    }

  if (PRM_LOG_BACKGROUND_ARCHIVING
      && bg_arv_info->vdes != NULL_VOLDES
      && logwr_Gl.last_arv_fpageid == bg_arv_info->start_page_id)
    {
      pageid = bg_arv_info->current_page_id;
      ar_phy_pageid = (bg_arv_info->current_page_id
		       - bg_arv_info->start_page_id + 1);
    }
  else
    {
      pageid = logwr_Gl.last_arv_fpageid;
      ar_phy_pageid = 1;
    }

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  /* Now start dumping the current active pages to archive */
  for (; pageid <= logwr_Gl.last_arv_lpageid;
       pageid += num_pages, ar_phy_pageid += num_pages)
    {
      /*
       * Page is contained in the active log.
       * Find the corresponding physical page and read the page form disk.
       */
      num_pages = MIN (LOGPB_IO_NPAGES,
		       logwr_Gl.last_arv_lpageid - pageid + 1);

      phy_pageid = logwr_to_physical_pageid (pageid);
      num_pages = MIN (num_pages, logwr_Gl.hdr.npages - phy_pageid + 1);

      if (fileio_read_pages (NULL, logwr_Gl.append_vdes, (char *) log_pgptr,
			     phy_pageid, num_pages, LOG_PAGESIZE) == NULL)
	{
	  error_code = ER_LOG_READ;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_READ, 3,
		  pageid, phy_pageid, logwr_Gl.active_name);
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

      if (fileio_write_pages (NULL, vdes, (char *) log_pgptr,
			      ar_phy_pageid, num_pages, LOG_PAGESIZE) == NULL)
	{
	  error_code = ER_LOG_WRITE;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOG_WRITE, 3,
		  pageid, ar_phy_pageid, archive_name);
	  goto error;
	}
    }

  fileio_dismount (NULL, vdes);
  vdes = NULL_VOLDES;

  if (PRM_LOG_BACKGROUND_ARCHIVING && bg_arv_info->vdes != NULL_VOLDES)
    {
      bg_arv_info->vdes = NULL_VOLDES;
      if (fileio_rename (NULL_VOLID, logwr_Gl.bg_archive_name, archive_name)
	  == NULL)
	{
	  goto error;
	}
      bg_arv_info->vdes = fileio_format (NULL, logwr_Gl.db_name,
					 logwr_Gl.bg_archive_name,
					 LOG_DBLOG_BG_ARCHIVE_VOLID,
					 logwr_Gl.hdr.npages, false, false,
					 false, LOG_PAGESIZE);
      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  bg_arv_info->start_page_id = logwr_Gl.hdr.nxarv_pageid;
	  bg_arv_info->current_page_id = logwr_Gl.hdr.nxarv_pageid;
	}
      else
	{
	  bg_arv_info->start_page_id = NULL_PAGEID;
	  bg_arv_info->current_page_id = NULL_PAGEID;
	  er_log_debug (ARG_FILE_LINE,
			"Unable to create temporary archive log %s\n",
			logwr_Gl.bg_archive_name);
	}
    }

  /* Update archive info */
  logwr_Gl.last_arv_num++;
  logwr_Gl.last_arv_fpageid = logwr_Gl.last_arv_lpageid + 1;

  /* Flush the log header to reflect the archive */
  logwr_flush_header_page ();

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_ARCHIVE_CREATED, 3,
	  archive_name, arvhdr->fpageid,
	  arvhdr->fpageid + arvhdr->npages - 1);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
			   MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ARCHIVE);
  if (catmsg == NULL)
    {
      catmsg = "ARCHIVE: %d %s %d %d\n";
    }
  error_code = log_dump_log_info (logwr_Gl.loginf_path, false, catmsg,
				  arvhdr->arv_num, archive_name,
				  arvhdr->fpageid,
				  arvhdr->fpageid + arvhdr->npages - 1);
  er_log_debug (ARG_FILE_LINE,
		"logwr_archive_active_log, arv_num(%d), fpageid(%d) lpageid(%d)\n",
		arvhdr->arv_num, arvhdr->fpageid,
		arvhdr->fpageid + arvhdr->npages - 1);

  free_and_init (malloc_arv_hdr_pgptr);

  return NO_ERROR;

error:

  if (malloc_arv_hdr_pgptr != NULL)
    {
      free_and_init (malloc_arv_hdr_pgptr);
    }

  if (vdes != NULL_VOLDES)
    {
      fileio_dismount (NULL, vdes);
      fileio_unformat (NULL, archive_name);
    }

  return error_code;
}

/*
 * logwr_background_archiving -
 *
 * return:
 *
 * NOTE:
 */
static int
logwr_background_archiving (void)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE * LOGPB_IO_NPAGES + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr;
  PAGEID page_id, phy_pageid, last_page_id, bg_phy_pageid;
  int num_pages = 0, vdes;
  int error_code = NO_ERROR;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info;

  assert (PRM_LOG_BACKGROUND_ARCHIVING);

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  bg_arv_info = &logwr_Gl.bg_archive_info;
  vdes = bg_arv_info->vdes;
  if (vdes == NULL_VOLDES)
    {
      return NO_ERROR;
    }

  last_page_id = logwr_Gl.hdr.chkpt_lsa.pageid - 1;
  page_id = bg_arv_info->current_page_id;
  bg_phy_pageid = page_id - bg_arv_info->start_page_id + 1;

  assert (last_page_id <= logwr_Gl.last_recv_pageid);

  /* Now start dumping the current active pages to archive */
  for (; page_id <= last_page_id;
       page_id += num_pages, bg_phy_pageid += num_pages)
    {
      phy_pageid = logwr_to_physical_pageid (page_id);

      num_pages = MIN (LOGPB_IO_NPAGES, last_page_id - page_id + 1);

      if (fileio_read_pages (NULL, logwr_Gl.append_vdes, (char *) log_pgptr,
			     phy_pageid, num_pages, LOG_PAGESIZE) == NULL)
	{
	  error_code = er_errid ();
	  goto error;
	}

      if (fileio_write_pages (NULL, vdes, (char *) log_pgptr,
			      bg_phy_pageid, num_pages, LOG_PAGESIZE) == NULL)
	{
	  error_code = ER_LOG_WRITE;
	  goto error;
	}

      bg_arv_info->current_page_id = page_id + num_pages;
    }

error:
  if (error_code == ER_LOG_WRITE || error_code == ER_LOG_READ)
    {
      fileio_dismount (NULL, bg_arv_info->vdes);
      bg_arv_info->vdes = NULL_VOLDES;
      bg_arv_info->start_page_id = NULL_PAGEID;
      bg_arv_info->current_page_id = NULL_PAGEID;

      er_log_debug (ARG_FILE_LINE,
		    "background archiving error, hdr->start_page_id = %d, "
		    "hdr->current_page_id = %d, error:%d\n",
		    bg_arv_info->start_page_id,
		    bg_arv_info->current_page_id, error_code);
    }

  er_log_debug (ARG_FILE_LINE,
		"logwr_background_archiving end, hdr->start_page_id = %d, "
		"hdr->current_page_id = %d\n",
		bg_arv_info->start_page_id, bg_arv_info->current_page_id);

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
  struct timeval curtime;
  int diff_msec;

  if (logwr_Gl.num_toflush <= 0)
    return NO_ERROR;

  if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
    {
      gettimeofday (&curtime, NULL);
      diff_msec =
	(((curtime.tv_sec - logwr_Gl.last_flush_time.tv_sec) * 1000) +
	 ((curtime.tv_usec - logwr_Gl.last_flush_time.tv_usec) / 1000));
      if ((!logwr_Gl.force_flush) && !LOGWR_AT_SERVER_ARCHIVING ()
	  && (logwr_Gl.hdr.eof_lsa.pageid <=
	      logwr_Gl.toflush[0]->hdr.logical_pageid)
	  && ((PRM_LOG_BG_FLUSH_INTERVAL_MSECS > 0)
	      && (diff_msec < PRM_LOG_BG_FLUSH_INTERVAL_MSECS)))
	{
	  return NO_ERROR;
	}
      logwr_Gl.force_flush = false;
    }

  if (logwr_Gl.append_vdes == NULL_VOLDES &&
      !fileio_is_volume_exist (logwr_Gl.active_name))
    {
      /* Create a new active log */
      logwr_Gl.append_vdes = fileio_format (NULL,
					    logwr_Gl.db_name,
					    logwr_Gl.active_name,
					    LOG_DBLOG_ACTIVE_VOLID,
					    (logwr_Gl.hdr.npages + 1),
					    PRM_LOG_SWEEP_CLEAN, true, false,
					    LOG_PAGESIZE);
      if (logwr_Gl.append_vdes == NULL_VOLDES)
	{
	  /* Unable to create an active log */
	  return ER_IO_FORMAT_FAIL;
	}
    }

  /*
   * LWT sets the archiving flag at the time when it sends new active page
   * after archiving finished, so that logwr_archive_active_log() should
   * be executed before logwr_flush_all_append_pages().
   */
  if (logwr_Gl.action & LOGWR_ACTION_ARCHIVING)
    {
      error = logwr_archive_active_log ();
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  error = logwr_flush_all_append_pages ();
  if (error != NO_ERROR)
    {
      return error;
    }

  if (PRM_LOG_BACKGROUND_ARCHIVING
      && !(logwr_Gl.action & LOGWR_ACTION_DELAYED_WRITE)
      && logwr_Gl.hdr.chkpt_lsa.pageid > logwr_Gl.last_chkpt_pageid)
    {
      error = logwr_background_archiving ();
      if (error == NO_ERROR)
	{
	  logwr_Gl.last_chkpt_pageid = logwr_Gl.hdr.chkpt_lsa.pageid;
	}
      /* ignore error */
    }

  /* TODO: periodic header flush */
  if (true			/*!(logwr_Gl.action & LOGWR_ACTION_DELAYED_WRITE) */
      && true /*logwr_Gl.action & LOGWR_ACTION_HDR_WRITE */ )
    {
      logwr_flush_header_page ();
    }

  gettimeofday (&logwr_Gl.last_flush_time, NULL);

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
	      error = logwr_write_log_pages ();
	      if (error != NO_ERROR)
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
static int logwr_register_writer_entry (LOGWR_ENTRY ** wr_entry_p,
					THREAD_ENTRY * thread_p,
					PAGEID fpageid, int mode);
static bool logwr_unregister_writer_entry (LOGWR_ENTRY * wr_entry,
					   int status);
static int logwr_pack_log_pages (THREAD_ENTRY * thread_p, char *logpg_area,
				 int *logpg_used_size, int *status,
				 LOGWR_ENTRY * entry);

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
	{
	  break;
	}
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
	{
	  entry->status = LOGWR_STATUS_WAIT;
	}
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
	{
	  break;
	}
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
  int ha_file_status;
  int error_code;

  fpageid = NULL_PAGEID;
  lpageid = NULL_PAGEID;
  ha_file_status = LOG_HA_FILESTAT_CLEAR;

  is_hdr_page_only = (entry->fpageid == LOGPB_HEADER_PAGE_ID);

  if (!is_hdr_page_only)
    {
      /* Find the first pageid to be packed */
      fpageid = entry->fpageid;
      if (fpageid == NULL_PAGEID)
	{
	  /* In case of first request from the log writer,
	     pack all active pages to be flushed until now */
	  fpageid = log_Gl.hdr.nxarv_pageid;
	}
      else if (fpageid > log_Gl.append.nxio_lsa.pageid)
	{
	  fpageid = log_Gl.append.nxio_lsa.pageid;
	}

      /* Find the last pageid which is bounded by several limitations */
      if (!logpb_is_page_in_archive (fpageid))
	{
	  lpageid = log_Gl.hdr.eof_lsa.pageid;
	}
      else
	{
	  struct log_arv_header arvhdr;

	  /* If the fpageid is in archive log,
	     fetch the page and the header page in the archive */
	  if (logpb_fetch_from_archive (thread_p, fpageid, NULL, NULL,
					&arvhdr) == NULL)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	  /* Reset the lpageid with the last pageid in the archive */
	  lpageid = arvhdr.fpageid + arvhdr.npages - 1;
	  if (fpageid == arvhdr.fpageid)
	    {
	      ha_file_status = LOG_HA_FILESTAT_ARCHIVED;
	    }
	}
      /* Pack the pages which can be in the page area of Log Writer */
      if ((lpageid - fpageid + 1) > (PRM_LOG_NBUFFERS - 1))
	{
	  lpageid = fpageid + (PRM_LOG_NBUFFERS - 1) - 1;
	}
      if (lpageid == log_Gl.hdr.eof_lsa.pageid)
	{
	  ha_file_status = LOG_HA_FILESTAT_SYNCHRONIZED;
	}
    }

  /* Set the server status on the header information */
  log_Gl.hdr.ha_server_state = css_ha_server_state ();
  log_Gl.hdr.ha_file_status = ha_file_status;

  /* Allocate the log page area */
  num_logpgs = (is_hdr_page_only) ? 1 : (lpageid - fpageid + 1) + 1;

  assert (lpageid >= fpageid);
  assert (num_logpgs <= PRM_LOG_NBUFFERS);

  p = logpg_area;

  /* Fill the header page */
  log_pgptr = (LOG_PAGE *) p;
  log_pgptr->hdr = log_Gl.loghdr_pgptr->hdr;
  memcpy (log_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr));
  p += LOG_PAGESIZE;

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
	  p += LOG_PAGESIZE;
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

  er_log_debug (ARG_FILE_LINE,
		"logwr_pack_log_pages, fpageid(%d), lpageid(%d), num_pages(%d),"
		"\n status(%d), delayed_free_log_pgptr(%p)\n",
		fpageid, lpageid, num_logpgs, entry->status,
		log_Gl.append.delayed_free_log_pgptr);

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
  LOGWR_MODE next_mode;
  int status;
  int rv;
  int error_code;
  bool check_cs_own = false;
  LOGWR_INFO *writer_info = &log_Gl.writer_info;

  logpg_used_size = 0;
  logpg_area = db_private_alloc (thread_p, PRM_LOG_NBUFFERS * LOG_PAGESIZE);
  if (logpg_area == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      PRM_LOG_NBUFFERS * LOG_PAGESIZE);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (thread_p->conn_entry)
    {
      thread_p->conn_entry->stop_phase = THREAD_WORKER_STOP_PHASE_1;
    }

  while (true)
    {
      er_log_debug (ARG_FILE_LINE,
		    "[tid:%ld] xlogwr_get_log_pages, fpageid(%d), mode(%s)\n",
		    thread_p->tid, first_pageid,
		    (mode == LOGWR_MODE_SYNC ? "sync" :
		     (mode == LOGWR_MODE_ASYNC ? "async" : "semisync")));

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
					   &writer_info->flush_start_mutex,
					   INF_WAIT, NULL,
					   THREAD_LOGWR_SUSPENDED);

	  LOG_MUTEX_UNLOCK (writer_info->flush_start_mutex);

	  if (logtb_is_interrupted (thread_p, false, &continue_checking)
	      || thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
	    {
	      /* interrupted, shutdown or connection has gone. */
	      error_code = ER_INTERRUPTED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	      status = LOGWR_STATUS_ERROR;
	      goto error;
	    }
	  else if (thread_p->resume_status != THREAD_LOGWR_RESUMED)
	    {
	      error_code = ER_FAILED;
	      status = LOGWR_STATUS_ERROR;
	      goto error;
	    }
	}
      else
	{
	  assert (entry->status == LOGWR_STATUS_DELAY);
	  LOG_MUTEX_UNLOCK (writer_info->flush_start_mutex);
	  LOG_CS_ENTER (thread_p);
	  check_cs_own = true;
	}

      /* Send the log pages to be flushed until now */
      error_code = logwr_pack_log_pages (thread_p, logpg_area,
					 &logpg_used_size, &status, entry);
      if (error_code != NO_ERROR)
	{
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      error_code = xlog_send_log_pages_to_client (thread_p, logpg_area,
						  logpg_used_size, mode);
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
      error_code = xlog_get_page_request_with_reply (thread_p, &next_fpageid,
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

  er_log_debug (ARG_FILE_LINE,
		"[tid:%ld] xlogwr_get_log_pages, error(%d)\n",
		thread_p->tid, error_code);
  if (check_cs_own)
    {
      LOG_CS_EXIT ();
    }
  LOG_MUTEX_LOCK (rv, writer_info->flush_end_mutex);
  if (entry != NULL && logwr_unregister_writer_entry (entry, status))
    {
      COND_SIGNAL (writer_info->flush_end_cond);
    }
  LOG_MUTEX_UNLOCK (writer_info->flush_end_mutex);

  db_private_free_and_init (thread_p, logpg_area);

  return error_code;
}

/*
 * logwr_get_min_copied_fpageid -
 *
 * return:
 *
 * Note:
 */
PAGEID
logwr_get_min_copied_fpageid (void)
{
  LOGWR_INFO *writer_info = &log_Gl.writer_info;
  LOGWR_ENTRY *entry;
  int num_entries = 0;
  PAGEID min_fpageid = PAGEID_MAX;
  int rv;

  LOG_MUTEX_LOCK (rv, writer_info->wr_list_mutex);

  entry = writer_info->writer_list;
  while (entry)
    {
      if (min_fpageid > entry->fpageid)
	{
	  min_fpageid = entry->fpageid;
	}
      entry = entry->next;
      num_entries++;
    }

  LOG_MUTEX_UNLOCK (writer_info->wr_list_mutex);

  if (min_fpageid == PAGEID_MAX || min_fpageid == LOGPB_HEADER_PAGE_ID)
    {
      min_fpageid = NULL_PAGEID;
    }
  if (min_fpageid < css_get_ha_num_of_hosts ())
    {
      min_fpageid = NULL_PAGEID;
    }

  return (min_fpageid);
}
#endif /* SERVER_MODE */
