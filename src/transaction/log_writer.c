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
#include "thread.h"
#endif
#include "dbi.h"
#if !defined(WINDOWS)
#include "heartbeat.h"
#endif
#include "log_applier.h"

#define LOGWR_THREAD_SUSPEND_TIMEOUT 	10

#define LOGWR_COPY_LOG_BUFFER_NPAGES      LOGPB_BUFFER_NPAGES_LOWER

static int prev_ha_server_state = HA_SERVER_STATE_NA;
static bool logwr_need_shutdown = false;

#if defined(CS_MODE)
LOGWR_GLOBAL logwr_Gl = {
  /* log header */
  LOGWR_HEADER_INITIALIZER,
  /* loghdr_pgptr */
  NULL,
  /* db_name */
  {'0'},
  /* hostname */
  NULL,
  /* log_path */
  {'0'},
  /* loginf_path */
  {'0'},
  /* active_name */
  {'0'},
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
  /* force_flush */
  false,
  /* last_flush_time */
  {0, 0},
  /* background archiving info */
  BACKGROUND_ARCHIVING_INFO_INITIALIZER,
  /* bg_archive_name */
  {'0'},
  /* ori_nxarv_pageid */
  NULL_PAGEID,
  /* start_pageid */
  -2
};


static int logwr_fetch_header_page (LOG_PAGE * log_pgptr, int vol_fd);
static int logwr_read_log_header (void);
static int logwr_read_bgarv_log_header (void);
static int logwr_initialize (const char *db_name, const char *log_path,
			     int mode, LOG_PAGEID start_pageid);
static void logwr_finalize (void);
static LOG_PAGE **logwr_writev_append_pages (LOG_PAGE ** to_flush,
					     DKNPAGES npages);
static int logwr_flush_all_append_pages (void);
static int logwr_archive_active_log (void);
static int logwr_background_archiving (void);
static int logwr_flush_bgarv_header_page (void);
static void logwr_send_end_msg (LOGWR_CONTEXT * ctx_ptr, int error);

/*
 * logwr_to_physical_pageid -
 *
 * return:
 *   logical_pageid(in):
 * Note:
 */
LOG_PHY_PAGEID
logwr_to_physical_pageid (LOG_PAGEID logical_pageid)
{
  LOG_PHY_PAGEID phy_pageid;

  if (logical_pageid == LOGPB_HEADER_PAGE_ID)
    {
      phy_pageid = 0;
    }
  else
    {
      LOG_PAGEID tmp_pageid;

      tmp_pageid = logical_pageid - logwr_Gl.hdr.fpageid;

      if (tmp_pageid >= logwr_Gl.hdr.npages)
	{
	  tmp_pageid %= logwr_Gl.hdr.npages;
	}
      else if (tmp_pageid < 0)
	{
	  tmp_pageid = (logwr_Gl.hdr.npages
			- ((-tmp_pageid) % logwr_Gl.hdr.npages));
	}
      tmp_pageid++;
      if (tmp_pageid > logwr_Gl.hdr.npages)
	{
	  tmp_pageid %= logwr_Gl.hdr.npages;
	}

      assert (tmp_pageid <= PAGEID_MAX);
      phy_pageid = (LOG_PHY_PAGEID) tmp_pageid;
    }

  return phy_pageid;
}

/*
 * logwr_fetch_header_page -
 *
 * return:
 *   log_pgptr(out):
 *   vol_fd(in):
 * Note:
 */
static int
logwr_fetch_header_page (LOG_PAGE * log_pgptr, int vol_fd)
{
  LOG_PAGEID pageid;
  LOG_PHY_PAGEID phy_pageid;

  assert (log_pgptr != NULL);

  /*
   * Page is contained in the active log.
   * Find the corresponding physical page and read the page form disk.
   */
  pageid = LOGPB_HEADER_PAGE_ID;
  phy_pageid = logwr_to_physical_pageid (pageid);

  if (fileio_read (NULL, vol_fd, log_pgptr, phy_pageid, LOG_PAGESIZE) == NULL)
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
	  error = logwr_fetch_header_page (log_pgptr, logwr_Gl.append_vdes);
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
 * logwr_read_bgarv_log_header -
 *
 * return:
 * Note:
 */
static int
logwr_read_bgarv_log_header (void)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr;
  LOG_BGARV_HEADER *bgarv_header;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info;
  int error = NO_ERROR;

  bg_arv_info = &logwr_Gl.bg_archive_info;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  assert (bg_arv_info->vdes != NULL_VOLDES);

  error = logwr_fetch_header_page (log_pgptr, bg_arv_info->vdes);
  if (error != NO_ERROR)
    {
      return error;
    }

  assert (log_pgptr->hdr.logical_pageid == LOGPB_HEADER_PAGE_ID);
  assert (log_pgptr->hdr.offset = NULL_OFFSET);

  bgarv_header = (LOG_BGARV_HEADER *) log_pgptr->area;

  if (strncmp
      (bgarv_header->magic, CUBRID_MAGIC_LOG_ARCHIVE,
       CUBRID_MAGIC_MAX_LENGTH) != 0)
    {
      /* magic is different */
      return ER_LOG_INCOMPATIBLE_DATABASE;
    }

  bg_arv_info->start_page_id = bgarv_header->start_page_id;
  bg_arv_info->current_page_id = bgarv_header->current_page_id;
  bg_arv_info->last_sync_pageid = bgarv_header->last_sync_pageid;

  return NO_ERROR;
}

/*
 * la_shutdown_by_signal() - When the process catches the SIGTERM signal,
 *                                it does the shutdown process.
 *   return: none
 *
 * Note:
 *        set the "logwr_need_shutdown" flag as true, then each threads would
 *        process "shutdown"
 */
static void
logwr_shutdown_by_signal ()
{
  logwr_need_shutdown = true;

  return;
}

bool
logwr_force_shutdown (void)
{
  return (logwr_need_shutdown) ? true : false;
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
logwr_initialize (const char *db_name, const char *log_path, int mode,
		  LOG_PAGEID start_pageid)
{
  int log_nbuffers;
  int error;
  char *at_char = NULL;

  /* signal processing */
#if defined(WINDOWS)
  (void) os_set_signal_handler (SIGABRT, logwr_shutdown_by_signal);
  (void) os_set_signal_handler (SIGINT, logwr_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, logwr_shutdown_by_signal);
#else /* ! WINDOWS */
  (void) os_set_signal_handler (SIGSTOP, logwr_shutdown_by_signal);
  (void) os_set_signal_handler (SIGTERM, logwr_shutdown_by_signal);
  (void) os_set_signal_handler (SIGPIPE, SIG_IGN);
#endif /* ! WINDOWS */

  /* set the db name and log path */
  strncpy (logwr_Gl.db_name, db_name, PATH_MAX - 1);
  if ((at_char = strchr (logwr_Gl.db_name, '@')) != NULL)
    {
      *at_char = '\0';
      logwr_Gl.hostname = at_char + 1;
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
  log_nbuffers = LOGWR_COPY_LOG_BUFFER_NPAGES + 1;

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

  logwr_Gl.start_pageid = start_pageid;
  if (logwr_Gl.start_pageid >= NULL_PAGEID
      && logwr_Gl.hdr.nxarv_pageid != NULL_PAGEID)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
	      1, "Replication transaction log already exist");
      return ER_HA_GENERIC_ERROR;
    }

  logwr_Gl.action = LOGWR_ACTION_NONE;

  logwr_Gl.last_arv_fpageid = logwr_Gl.hdr.nxarv_pageid;
  logwr_Gl.last_arv_num = logwr_Gl.hdr.nxarv_num;

  logwr_Gl.force_flush = false;
  logwr_Gl.last_flush_time.tv_sec = 0;
  logwr_Gl.last_flush_time.tv_usec = 0;

  logwr_Gl.ori_nxarv_pageid = NULL_PAGEID;

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      BACKGROUND_ARCHIVING_INFO *bg_arv_info;
      bg_arv_info = &logwr_Gl.bg_archive_info;

      if (fileio_is_volume_exist (logwr_Gl.bg_archive_name) == true)
	{
	  bg_arv_info->vdes =
	    fileio_mount (NULL, logwr_Gl.bg_archive_name,
			  logwr_Gl.bg_archive_name, LOG_DBLOG_ARCHIVE_VOLID,
			  true, false);
	  if (bg_arv_info->vdes == NULL_VOLDES)
	    {
	      return ER_IO_MOUNT_FAIL;
	    }

	  error = logwr_read_bgarv_log_header ();
	  if (error != NO_ERROR)
	    {
	      if (error == ER_LOG_INCOMPATIBLE_DATABASE)
		{
		  /* Considering the case of upgrading,
		   * if it read magic from bg archive file 
		   * which is not include magic, make new bg archive file.
		   */
		  fileio_dismount (NULL, bg_arv_info->vdes);
		  fileio_unformat (NULL, logwr_Gl.bg_archive_name);

		  bg_arv_info->vdes = NULL_VOLDES;
		}
	      else
		{
		  return error;
		}
	    }
	}

      /* if bg archive file does not exist or magic is diff, */
      /* create new bg archive file */
      if (bg_arv_info->vdes == NULL_VOLDES)
	{
	  bg_arv_info->vdes = fileio_format (NULL, logwr_Gl.db_name,
					     logwr_Gl.bg_archive_name,
					     LOG_DBLOG_BG_ARCHIVE_VOLID,
					     logwr_Gl.hdr.npages + 1,
					     false, false, false,
					     LOG_PAGESIZE, 0, false);
	  if (bg_arv_info->vdes == NULL_VOLDES)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, "Unable to create temporary archive log");
	      return ER_HA_GENERIC_ERROR;
	    }

	  bg_arv_info->start_page_id = logwr_Gl.hdr.nxarv_pageid;
	  bg_arv_info->current_page_id = NULL_PAGEID;
	  bg_arv_info->last_sync_pageid = logwr_Gl.hdr.nxarv_pageid;

	  error = logwr_flush_bgarv_header_page ();
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
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
      free_and_init (logwr_Gl.logpg_area);
      logwr_Gl.logpg_area_size = 0;
      logwr_Gl.logpg_fill_size = 0;
      logwr_Gl.loghdr_pgptr = NULL;
    }
  if (logwr_Gl.toflush != NULL)
    {
      free_and_init (logwr_Gl.toflush);
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

  logwr_Gl.ori_nxarv_pageid = NULL_PAGEID;
  logwr_Gl.start_pageid = -2;

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
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

  /* get original next archive pageid */
  logwr_Gl.ori_nxarv_pageid = logwr_Gl.hdr.nxarv_pageid;

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
	  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
	    {
	      logwr_Gl.bg_archive_info.start_page_id =
		logwr_Gl.last_arv_fpageid;
	    }
	}

      /* Check if it need archiving */
      if (((logwr_Gl.last_arv_num + 1 < logwr_Gl.hdr.nxarv_num)
	   && (logwr_Gl.hdr.ha_file_status == LOG_HA_FILESTAT_ARCHIVED))
	  && (logwr_Gl.last_arv_fpageid <= logwr_Gl.last_recv_pageid))
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
	      /* In case that it finishes delay write */
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
 * logwr_copy_necessary_log - 

 * return: 
 *   to_pageid(in): page id
 *
 * Note: copy active log to background archive file.
 *       (from bg_arv_info.current to to_pageid)
 */

static int
logwr_copy_necessary_log (LOG_PAGEID to_pageid)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE * LOGPB_IO_NPAGES + MAX_ALIGNMENT];
  char *aligned_log_pgbuf = NULL;
  LOG_PAGEID pageid = NULL_PAGEID;
  LOG_PHY_PAGEID phy_pageid = NULL_PAGEID;
  LOG_PHY_PAGEID ar_phy_pageid = NULL_PAGEID;
  LOG_PAGE *log_pgptr = NULL;
  int num_pages = 0;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info = NULL;

  bg_arv_info = &logwr_Gl.bg_archive_info;
  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

  pageid = bg_arv_info->current_page_id;
  if (pageid == NULL_PAGEID)
    {
      pageid = bg_arv_info->start_page_id;
    }

  ar_phy_pageid = (LOG_PHY_PAGEID) (pageid - bg_arv_info->start_page_id + 1);

  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  assert (logwr_Gl.last_arv_fpageid <= pageid
	  && pageid <= logwr_Gl.hdr.append_lsa.pageid);
  assert (logwr_Gl.last_arv_fpageid <= to_pageid
	  && to_pageid <= logwr_Gl.hdr.append_lsa.pageid);

  for (; pageid < to_pageid; pageid += num_pages, ar_phy_pageid += num_pages)
    {
      num_pages = MIN (LOGPB_IO_NPAGES, to_pageid - pageid);
      phy_pageid = logwr_to_physical_pageid (pageid);
      num_pages = MIN (num_pages, logwr_Gl.hdr.npages - phy_pageid + 1);

      if (fileio_read_pages (NULL, logwr_Gl.append_vdes,
			     (char *) log_pgptr, phy_pageid, num_pages,
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

      if (fileio_write_pages
	  (NULL, bg_arv_info->vdes, (char *) log_pgptr, ar_phy_pageid,
	   num_pages, LOG_PAGESIZE) == NULL)
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_WRITE, 3,
		  pageid, ar_phy_pageid, logwr_Gl.bg_archive_name);
	  return ER_LOG_WRITE;
	}
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
  LOG_PAGEID fpageid;
  LOG_PHY_PAGEID phy_pageid;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info = NULL;
  int error = NO_ERROR;

  if (npages > 0)
    {
      fpageid = to_flush[0]->hdr.logical_pageid;

      /* 1. archive temp write */
      if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
	{
	  bg_arv_info = &logwr_Gl.bg_archive_info;
	  /* check archive temp descriptor */
	  if (bg_arv_info->vdes == NULL_VOLDES)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		      1, "invalid temporary archive log file");
	      to_flush = NULL;
	      return to_flush;
	    }

	  /* If there exist empty page between current_page_id and (fpageid-1) 
	   * copy missing logs to background archive log file 
	   */
	  if (bg_arv_info->current_page_id < fpageid - 1
	      || bg_arv_info->current_page_id == NULL_PAGEID)
	    {
	      if (logwr_copy_necessary_log (fpageid) != NO_ERROR)
		{
		  to_flush = NULL;
		  return to_flush;
		}
	    }

	  phy_pageid =
	    (LOG_PHY_PAGEID) (fpageid - bg_arv_info->start_page_id + 1);
	  if (fileio_writev
	      (NULL, bg_arv_info->vdes, (void **) to_flush,
	       phy_pageid, npages, LOG_PAGESIZE) == NULL)
	    {
	      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
		{
		  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_LOG_WRITE_OUT_OF_SPACE, 4,
			  fpageid, phy_pageid, logwr_Gl.bg_archive_name,
			  logwr_Gl.hdr.db_logpagesize);
		}
	      else
		{
		  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
				       ER_LOG_WRITE, 3,
				       fpageid, phy_pageid,
				       logwr_Gl.bg_archive_name);
		}
	      to_flush = NULL;
	      return to_flush;
	    }
	  bg_arv_info->current_page_id = fpageid + (npages - 1);
	  er_log_debug (ARG_FILE_LINE,
			"background archiving  current_page_id[%lld], fpageid[%lld], npages[%d]",
			bg_arv_info->current_page_id, fpageid, npages);

	  error = logwr_flush_bgarv_header_page ();
	  if (error != NO_ERROR)
	    {
	      to_flush = NULL;
	      return to_flush;
	    }
	}

      /* 2. active write */
      phy_pageid = logwr_to_physical_pageid (fpageid);
      if (fileio_writev (NULL, logwr_Gl.append_vdes, (void **) to_flush,
			 phy_pageid, npages, LOG_PAGESIZE) == NULL)
	{
	  if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	    {
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_LOG_WRITE_OUT_OF_SPACE, 4,
		      fpageid, phy_pageid, logwr_Gl.active_name,
		      logwr_Gl.hdr.db_logpagesize);
	    }
	  else
	    {
	      er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_LOG_WRITE, 3,
				   fpageid, phy_pageid, logwr_Gl.active_name);
	    }
	  to_flush = NULL;
	  return to_flush;
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
  LOG_PAGEID pageid, prv_pageid;
  int idxflush;
  bool need_sync;
  int flush_page_count;
  int i;

  idxflush = -1;
  prv_pgptr = NULL;
  need_sync = false;
  flush_page_count = 0;

  for (i = 0; i < logwr_Gl.num_toflush; i++)
    {
      pgptr = logwr_Gl.toflush[i];

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
		  assert (er_errid () != NO_ERROR);
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
	  assert (er_errid () != NO_ERROR);
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
      && fileio_synchronize (NULL, logwr_Gl.append_vdes,
			     logwr_Gl.active_name) == NULL_VOLDES)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* It's for dual write. */
  if (need_sync == true
      && prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      if (fileio_synchronize (NULL, logwr_Gl.bg_archive_info.vdes,
			      logwr_Gl.bg_archive_name) == NULL_VOLDES)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
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
 * logwr_flush_bgarv_header_page -
 *
 * return:
 * Note:
 */
int
logwr_flush_bgarv_header_page (void)
{
  char log_pgbuf[IO_MAX_PAGE_SIZE + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr;
  BACKGROUND_ARCHIVING_INFO *bg_arv_info = NULL;
  LOG_BGARV_HEADER *bgarvhdr = NULL;
  LOG_PAGEID logical_pageid;
  LOG_PHY_PAGEID phy_pageid;
  int error_code = NO_ERROR;

  bg_arv_info = &logwr_Gl.bg_archive_info;

  assert (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING));
  assert (bg_arv_info->vdes != NULL_VOLDES);

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);
  log_pgptr = (LOG_PAGE *) aligned_log_pgbuf;

  log_pgptr->hdr.logical_pageid = LOGPB_HEADER_PAGE_ID;
  log_pgptr->hdr.offset = NULL_OFFSET;
  logical_pageid = LOGPB_HEADER_PAGE_ID;

  /* Construct the bg archive log header */
  bgarvhdr = (LOG_BGARV_HEADER *) log_pgptr->area;
  strncpy (bgarvhdr->magic, CUBRID_MAGIC_LOG_ARCHIVE,
	   CUBRID_MAGIC_MAX_LENGTH);
  bgarvhdr->db_creation = logwr_Gl.hdr.db_creation;
  bgarvhdr->start_page_id = bg_arv_info->start_page_id;
  bgarvhdr->current_page_id = bg_arv_info->current_page_id;
  bgarvhdr->last_sync_pageid = bg_arv_info->last_sync_pageid;

  phy_pageid = logwr_to_physical_pageid (log_pgptr->hdr.logical_pageid);

  if (fileio_write (NULL, bg_arv_info->vdes, log_pgptr,
		    phy_pageid, LOG_PAGESIZE) == NULL
      || fileio_synchronize (NULL, bg_arv_info->vdes,
			     logwr_Gl.bg_archive_name) == NULL_VOLDES)
    {
      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	{
	  error_code = ER_LOG_WRITE_OUT_OF_SPACE;
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_WRITE_OUT_OF_SPACE, 4, logical_pageid, phy_pageid,
		  logwr_Gl.bg_archive_name, LOG_PAGESIZE);
	}
      else
	{
	  error_code = ER_LOG_WRITE;
	  er_set_with_oserror (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_WRITE, 3, logical_pageid, phy_pageid,
			       logwr_Gl.bg_archive_name);
	}
      return error_code;
    }

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
  LOG_PAGEID logical_pageid;
  LOG_PHY_PAGEID phy_pageid;
  int nbytes;
  char buffer[1024];

  if (logwr_Gl.loghdr_pgptr == NULL)
    {
      return;
    }

  logwr_Gl.hdr.is_shutdown = true;

  /* flush current archiving status */
  logwr_Gl.hdr.nxarv_num = logwr_Gl.last_arv_num;
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
      || fileio_synchronize (NULL, logwr_Gl.append_vdes,
			     logwr_Gl.active_name) == NULL_VOLDES)
    {

      if (er_errid () == ER_IO_WRITE_OUT_OF_SPACE)
	{
	  nbytes = logwr_Gl.hdr.db_logpagesize;
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

  if (prev_ha_server_state != logwr_Gl.hdr.ha_server_state)
    {
      sprintf (buffer,
	       "change the state of HA server (%s@%s) from '%s' to '%s'",
	       logwr_Gl.db_name,
	       (logwr_Gl.hostname != NULL) ? logwr_Gl.hostname : "unknown",
	       css_ha_server_state_string (prev_ha_server_state),
	       css_ha_server_state_string (logwr_Gl.hdr.ha_server_state));
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	      buffer);
    }
  prev_ha_server_state = logwr_Gl.hdr.ha_server_state;

  er_log_debug (ARG_FILE_LINE,
		"logwr_flush_header_page, ha_server_state=%s, ha_file_status=%s\n",
		css_ha_server_state_string (logwr_Gl.hdr.ha_server_state),
		logwr_log_ha_filestat_to_string (logwr_Gl.
						 hdr.ha_file_status));
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
  char log_pgbuf[IO_MAX_PAGE_SIZE * LOGPB_IO_NPAGES + MAX_ALIGNMENT];
  char *aligned_log_pgbuf;
  LOG_PAGE *log_pgptr = NULL;
  LOG_PAGE *malloc_arv_hdr_pgptr = NULL;
  LOG_PAGEID pageid;
  LOG_LSA saved_append_lsa;
  LOG_PHY_PAGEID ar_phy_pageid = NULL_PAGEID, phy_pageid = NULL_PAGEID;
  int vdes = NULL_VOLDES;
  int error_code = NO_ERROR;
  int num_pages = 0;
  const char *catmsg;
  char buffer[LINE_MAX];
  BACKGROUND_ARCHIVING_INFO *bg_arv_info;

  aligned_log_pgbuf = PTR_ALIGN (log_pgbuf, MAX_ALIGNMENT);

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
  arvhdr->npages =
    (DKNPAGES) (logwr_Gl.last_arv_lpageid - arvhdr->fpageid + 1);

  /*
   * Now create the archive and start copying pages
   */

  snprintf (buffer, sizeof (buffer), "log archiving started for archive %03d",
	    arvhdr->arv_num);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
	  buffer);

  fileio_make_log_archive_name (archive_name, logwr_Gl.log_path,
				logwr_Gl.db_name, arvhdr->arv_num);
  bg_arv_info = &logwr_Gl.bg_archive_info;
  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING))
    {
      if (bg_arv_info->vdes == NULL_VOLDES)
	{
	  error_code = ER_HA_GENERIC_ERROR;
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_HA_GENERIC_ERROR, 1,
		  "invalid temporary archive log file");
	  goto error;
	}
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
				false, false, false, LOG_PAGESIZE, 0, false);
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

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING)
      && bg_arv_info->vdes != NULL_VOLDES
      && logwr_Gl.last_arv_fpageid == bg_arv_info->start_page_id)
    {
      pageid = bg_arv_info->current_page_id;
      ar_phy_pageid = (LOG_PHY_PAGEID) (bg_arv_info->current_page_id
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
		       (int) (logwr_Gl.last_arv_lpageid - pageid + 1));

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

  if (prm_get_bool_value (PRM_ID_LOG_BACKGROUND_ARCHIVING)
      && bg_arv_info->vdes != NULL_VOLDES)
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
					 false, LOG_PAGESIZE, 0, false);
      if (bg_arv_info->vdes != NULL_VOLDES)
	{
	  bg_arv_info->start_page_id = logwr_Gl.last_arv_lpageid + 1;
	  bg_arv_info->current_page_id = NULL_PAGEID;
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR, 1,
		  "Unable to create temporary archive log");
	  return ER_HA_GENERIC_ERROR;
	}
      error_code = logwr_flush_bgarv_header_page ();
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  /* Update archive info */
  logwr_Gl.last_arv_num++;
  logwr_Gl.last_arv_fpageid = logwr_Gl.last_arv_lpageid + 1;

  /* set append lsa as last archive logical pageid */
  /* in order to prevent log applier reading an immature active log page. */
  LSA_COPY (&saved_append_lsa, &logwr_Gl.hdr.append_lsa);
  logwr_Gl.hdr.append_lsa.pageid = logwr_Gl.last_arv_lpageid;
  logwr_Gl.hdr.append_lsa.offset = NULL_OFFSET;

  /* Flush the log header to reflect the archive */
  logwr_flush_header_page ();

  /* restore append lsa */
  LSA_COPY (&logwr_Gl.hdr.append_lsa, &saved_append_lsa);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_ARCHIVE_CREATED, 3,
	  archive_name, arvhdr->fpageid,
	  arvhdr->fpageid + arvhdr->npages - 1);

  catmsg = msgcat_message (MSGCAT_CATALOG_CUBRID,
			   MSGCAT_SET_LOG, MSGCAT_LOG_LOGINFO_ARCHIVE);
  if (catmsg == NULL)
    {
      catmsg = "ARCHIVE: %d %s %lld %lld\n";
    }
  error_code = log_dump_log_info (logwr_Gl.loginf_path, false, catmsg,
				  arvhdr->arv_num, archive_name,
				  arvhdr->fpageid,
				  arvhdr->fpageid + arvhdr->npages - 1);
  er_log_debug (ARG_FILE_LINE,
		"logwr_archive_active_log, arv_num(%d), fpageid(%lld) lpageid(%lld)\n",
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

      if (logwr_Gl.force_flush == false && !LOGWR_AT_SERVER_ARCHIVING ()
	  && (logwr_Gl.hdr.eof_lsa.pageid <=
	      logwr_Gl.toflush[0]->hdr.logical_pageid) && (diff_msec < 1000))
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
					    prm_get_bool_value
					    (PRM_ID_LOG_SWEEP_CLEAN), true,
					    false, LOG_PAGESIZE, 0, false);
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

  logwr_flush_header_page ();

  gettimeofday (&logwr_Gl.last_flush_time, NULL);

  return NO_ERROR;
}

#if !defined(WINDOWS)
/*
 * logwr_copy_log_header_check
 * return:
 *
 *      master_eof_lsa(OUT): record eof_lsa to calculate replication delay
 *
 * note:
 */
int
logwr_copy_log_header_check (const char *db_name, bool verbose,
			     LOG_LSA * master_eof_lsa)
{
  int error = NO_ERROR;
  LOGWR_CONTEXT ctx = { -1, 0, false };
  OR_ALIGNED_BUF (OR_INT_SIZE * 2 + OR_INT64_SIZE) a_request;
  OR_ALIGNED_BUF (OR_INT_SIZE * 2) a_reply;
  char *request, *reply;
  char *ptr;
  char *logpg_area = NULL;
  LOG_PAGE *loghdr_pgptr;
  struct log_header hdr;
  char *atchar;

  atchar = strchr (db_name, '@');
  if (atchar)
    {
      *atchar = '\0';
    }

  request = OR_ALIGNED_BUF_START (a_request);
  reply = OR_ALIGNED_BUF_START (a_reply);

  /* HEADER PAGE REQUEST */
  ptr = or_pack_int64 (request, LOGPB_HEADER_PAGE_ID);
  ptr = or_pack_int (ptr, LOGWR_MODE_ASYNC);
  ptr = or_pack_int (ptr, NO_ERROR);

  error = net_client_check_log_header (&ctx, request,
				       OR_ALIGNED_BUF_SIZE (a_request), reply,
				       OR_ALIGNED_BUF_SIZE (a_reply),
				       (char **) &logpg_area, verbose);
  if (error != NO_ERROR)
    {
      return error;
    }
  else
    {

      loghdr_pgptr = (LOG_PAGE *) logpg_area;
      hdr = *((struct log_header *) loghdr_pgptr->area);

      *master_eof_lsa = hdr.eof_lsa;

      printf ("\n ***  Active Info. *** \n");
      la_print_log_header (db_name, &hdr, verbose);
      free_and_init (logpg_area);
    }

  /* END REQUEST */
  ptr = or_pack_int64 (request, LOGPB_HEADER_PAGE_ID);
  ptr = or_pack_int (ptr, LOGWR_MODE_ASYNC);
  /* send ER_GENERIC_ERROR to make LWT not wait for more page requests */
  ptr = or_pack_int (ptr, ER_GENERIC_ERROR);

  error = net_client_check_log_header (&ctx, request,
				       OR_ALIGNED_BUF_SIZE (a_request), reply,
				       OR_ALIGNED_BUF_SIZE (a_reply),
				       (char **) &logpg_area, verbose);
  return error;
}
#endif /* !WINDOWS */

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
logwr_copy_log_file (const char *db_name, const char *log_path, int mode,
		     INT64 start_page_id)
{
  LOGWR_CONTEXT ctx = { -1, 0, false };
  int error = NO_ERROR;

  if ((error =
       logwr_initialize (db_name, log_path, mode, start_page_id)) != NO_ERROR)
    {
      logwr_finalize ();
      return error;
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_LW_STARTED, 1, mode);

  while (!ctx.shutdown && !logwr_need_shutdown)
    {
      if ((error = logwr_get_log_pages (&ctx)) != NO_ERROR)
	{
	  ctx.last_error = error;

	  if (error == ER_HA_LW_FAILED_GET_LOG_PAGE)
	    {
#if !defined(WINDOWS)
	      hb_deregister_from_master ();
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT,
		      2,
		      "Encountered an unrecoverable error "
		      "and will shut itself down", "");
#endif /* !WINDOWS */
	    }
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

#if !defined(WINDOWS)
  if (hb_Proc_shutdown == true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HB_PROCESS_EVENT, 2,
	      "Disconnected with the cub_master and will shut itself down",
	      "");
    }
#endif /* ! WINDOWS */

  /* SIGNAL caught and shutdown */
  if (logwr_need_shutdown)
    {
      if (logwr_Gl.mode == LOGWR_MODE_SEMISYNC)
	{
	  logwr_Gl.force_flush = true;
	  logwr_write_log_pages ();
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_LW_STOPPED_BY_SIGNAL,
	      0);
      error = ER_HA_LW_STOPPED_BY_SIGNAL;
    }

  if (ctx.rc > 0)
    {
      if (logwr_Gl.start_pageid >= NULL_PAGEID && error == NO_ERROR)
	{
	  /* to shutdown log writer thread of cub_server */
	  net_client_logwr_send_end_msg (ctx.rc, ER_FAILED);
	}
      else
	{
	  net_client_logwr_send_end_msg (ctx.rc, error);
	}
    }

  logwr_finalize ();
  return error;
}
#else /* CS_MODE */
int
logwr_copy_log_file (const char *db_name, const char *log_path, int mode,
		     INT64 start_page_id)
{
  return ER_FAILED;
}
#endif /* !CS_MODE */

/*
 * logwr_log_ha_filestat_to_string() - return the string alias of enum value
 *
 * return: constant string
 *
 *   val(in):
 */
const char *
logwr_log_ha_filestat_to_string (enum LOG_HA_FILESTAT val)
{
  switch (val)
    {
    case LOG_HA_FILESTAT_CLEAR:
      return "CLEAR";
    case LOG_HA_FILESTAT_ARCHIVED:
      return "ARCHIVED";
    case LOG_HA_FILESTAT_SYNCHRONIZED:
      return "SYNCHRONIZED";
    default:
      return "UNKNOWN";
    }
}

#if defined(SERVER_MODE)
static int logwr_register_writer_entry (LOGWR_ENTRY ** wr_entry_p,
					THREAD_ENTRY * thread_p,
					LOG_PAGEID fpageid, int mode,
					bool copy_from_first_phy_page);
static bool logwr_unregister_writer_entry (LOGWR_ENTRY * wr_entry,
					   int status);
static int logwr_pack_log_pages (THREAD_ENTRY * thread_p, char *logpg_area,
				 int *logpg_used_size, int *status,
				 LOGWR_ENTRY * entry, bool copy_from_file);
static void logwr_cs_exit (THREAD_ENTRY * thread_p, bool * check_cs_own);
static void logwr_write_end (THREAD_ENTRY * thread_p,
			     LOGWR_INFO * writer_info, LOGWR_ENTRY * entry,
			     int status);
static bool logwr_is_delayed (LOGWR_ENTRY * entry);
static void logwr_update_last_eof_lsa (LOGWR_ENTRY * entry);

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
			     LOG_PAGEID fpageid, int mode,
			     bool copy_from_first_phy_page)
{
  LOGWR_ENTRY *entry;
  int rv;
  LOGWR_INFO *writer_info = &log_Gl.writer_info;

  *wr_entry_p = NULL;
  rv = pthread_mutex_lock (&writer_info->wr_list_mutex);

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
	  pthread_mutex_unlock (&writer_info->wr_list_mutex);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (LOGWR_ENTRY));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      entry->thread_p = thread_p;
      entry->fpageid = fpageid;
      entry->mode = mode;
      entry->start_copy_time = 0;
      entry->copy_from_first_phy_page = copy_from_first_phy_page;

      entry->status = LOGWR_STATUS_DELAY;
      LSA_SET_NULL (&entry->tmp_last_eof_lsa);
      LSA_SET_NULL (&entry->last_eof_lsa);

      entry->next = writer_info->writer_list;
      writer_info->writer_list = entry;
    }
  else
    {
      entry->fpageid = fpageid;
      entry->mode = mode;
      entry->copy_from_first_phy_page = copy_from_first_phy_page;
      if (entry->status != LOGWR_STATUS_DELAY)
	{
	  entry->status = LOGWR_STATUS_WAIT;
	  entry->start_copy_time = 0;
	}
    }

  pthread_mutex_unlock (&writer_info->wr_list_mutex);
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

  rv = pthread_mutex_lock (&writer_info->wr_list_mutex);

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
	      free_and_init (entry);
	      break;
	    }
	  prev_entry = entry;
	  entry = entry->next;
	}
    }
  pthread_mutex_unlock (&writer_info->wr_list_mutex);

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
		      int *status, LOGWR_ENTRY * entry, bool copy_from_file)
{
  LOG_PAGEID fpageid, lpageid, pageid;
  char *p;
  LOG_PAGE *log_pgptr;
  INT64 num_logpgs;
  LOG_LSA nxio_lsa;
  bool is_hdr_page_only;
  int ha_file_status;
  int error_code;

  struct log_arv_header arvhdr;
  struct log_header *hdr_ptr;
  int nxarv_num;
  LOG_PAGEID nxarv_pageid, nxarv_phy_pageid;

  fpageid = NULL_PAGEID;
  lpageid = NULL_PAGEID;
  ha_file_status = LOG_HA_FILESTAT_CLEAR;

  is_hdr_page_only = (entry->fpageid == LOGPB_HEADER_PAGE_ID);

  if (is_hdr_page_only == true && entry->copy_from_first_phy_page == true)
    {
      assert_release (false);
      error_code = ER_FAILED;
      goto error;
    }

  if (entry->copy_from_first_phy_page == true)
    {
      fpageid = entry->fpageid;
      if (fpageid == NULL_PAGEID)
	{
	  fpageid = logpb_find_oldest_available_page_id (thread_p);
	  if (fpageid == NULL_PAGEID)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }
	}

      if (logpb_is_page_in_archive (fpageid) == true)
	{
	  if (logpb_fetch_from_archive
	      (thread_p, fpageid, NULL, NULL, &arvhdr, false) == NULL)
	    {
	      error_code = ER_FAILED;
	      goto error;
	    }

	  nxarv_phy_pageid = 1;	/* first physical page id */
	  nxarv_pageid = arvhdr.fpageid;
	  nxarv_num = arvhdr.arv_num;
	}
      else
	{
	  nxarv_phy_pageid = log_Gl.hdr.nxarv_phy_pageid;
	  nxarv_pageid = log_Gl.hdr.nxarv_pageid;
	  nxarv_num = log_Gl.hdr.nxarv_num;
	}

      lpageid = nxarv_pageid;
      fpageid = nxarv_pageid;
    }
  else if (!is_hdr_page_only)
    {
      /* Find the first pageid to be packed */
      fpageid = entry->fpageid;
      if (fpageid == NULL_PAGEID)
	{
	  /* In case of first request from the log writer,
	     pack all active pages to be flushed until now */
	  fpageid = log_Gl.hdr.nxarv_pageid;
	}
      else
	{
	  logpb_get_nxio_lsa (&nxio_lsa);
	  if (fpageid > nxio_lsa.pageid)
	    {
	      fpageid = nxio_lsa.pageid;
	    }
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
					&arvhdr, false) == NULL)
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
      if ((lpageid - fpageid + 1) > (LOGWR_COPY_LOG_BUFFER_NPAGES - 1))
	{
	  lpageid = fpageid + (LOGWR_COPY_LOG_BUFFER_NPAGES - 1) - 1;
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
  num_logpgs = (is_hdr_page_only) ? 1 : (int) ((lpageid - fpageid + 1) + 1);

  assert (lpageid >= fpageid);
  assert (num_logpgs <= LOGWR_COPY_LOG_BUFFER_NPAGES);

  p = logpg_area;

  /* Fill the header page */
  log_pgptr = (LOG_PAGE *) p;
  log_pgptr->hdr = log_Gl.loghdr_pgptr->hdr;
  memcpy (log_pgptr->area, &log_Gl.hdr, sizeof (log_Gl.hdr));

  if (entry->copy_from_first_phy_page == true)
    {
      hdr_ptr = (struct log_header *) (log_pgptr->area);
      hdr_ptr->nxarv_phy_pageid = nxarv_phy_pageid;
      hdr_ptr->nxarv_pageid = nxarv_pageid;
      hdr_ptr->nxarv_num = nxarv_num;
    }

  p += LOG_PAGESIZE;

  /* Fill the page array with the pages to send */
  if (!is_hdr_page_only)
    {
      for (pageid = fpageid; pageid >= 0 && pageid <= lpageid; pageid++)
	{
	  log_pgptr = (LOG_PAGE *) p;
	  if (copy_from_file == true)
	    {
	      if (logpb_copy_page_from_file (thread_p, pageid, log_pgptr) ==
		  NULL)
		{
		  error_code = ER_FAILED;
		  goto error;
		}
	    }
	  else
	    {
	      if (logpb_copy_page_from_log_buffer
		  (thread_p, pageid, log_pgptr) == NULL)
		{
		  error_code = ER_FAILED;
		  goto error;
		}
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
      LSA_COPY (&entry->tmp_last_eof_lsa, &log_Gl.hdr.eof_lsa);
    }
  else
    {
      *status = LOGWR_STATUS_DELAY;
      entry->tmp_last_eof_lsa.pageid = lpageid;
      entry->tmp_last_eof_lsa.offset = NULL_OFFSET;
    }

  er_log_debug (ARG_FILE_LINE,
		"logwr_pack_log_pages, fpageid(%lld), lpageid(%lld), num_pages(%lld),"
		"\n status(%d), delayed_free_log_pgptr(%p)\n",
		fpageid, lpageid, num_logpgs, entry->status,
		log_Gl.append.delayed_free_log_pgptr);

  return NO_ERROR;

error:

  *logpg_used_size = 0;
  *status = LOGWR_STATUS_ERROR;

  return error_code;
}

static void
logwr_cs_exit (THREAD_ENTRY * thread_p, bool * check_cs_own)
{
  if (*check_cs_own)
    {
      *check_cs_own = false;
      LOG_CS_EXIT (thread_p);
    }
  return;
}

static void
logwr_write_end (THREAD_ENTRY * thread_p, LOGWR_INFO * writer_info,
		 LOGWR_ENTRY * entry, int status)
{
  int rv;
  int tran_index;
  int prev_status;
  INT64 saved_start_time;

  rv = pthread_mutex_lock (&writer_info->flush_end_mutex);

  prev_status = entry->status;
  saved_start_time = entry->start_copy_time;

  if (entry != NULL && logwr_unregister_writer_entry (entry, status))
    {
      if (prev_status == LOGWR_STATUS_FETCH
	  && writer_info->trace_last_writer == true)
	{
	  assert (saved_start_time > 0);
	  writer_info->last_writer_elapsed_time =
	    thread_get_log_clock_msec () - saved_start_time;

	  tran_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);
	  logtb_get_client_ids (tran_index,
				&writer_info->last_writer_client_info);
	}
      pthread_cond_signal (&writer_info->flush_end_cond);
    }
  pthread_mutex_unlock (&writer_info->flush_end_mutex);
  return;
}

static bool
logwr_is_delayed (LOGWR_ENTRY * entry)
{
  if (entry == NULL || LSA_ISNULL (&entry->last_eof_lsa)
      || LSA_EQ (&entry->last_eof_lsa, &log_Gl.hdr.eof_lsa))
    {
      return false;
    }
  return true;
}


static void
logwr_update_last_eof_lsa (LOGWR_ENTRY * entry)
{
  if (entry)
    {
      LSA_COPY (&entry->last_eof_lsa, &entry->tmp_last_eof_lsa);
    }
  return;
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
xlogwr_get_log_pages (THREAD_ENTRY * thread_p, LOG_PAGEID first_pageid,
		      LOGWR_MODE mode)
{
  LOGWR_ENTRY *entry;
  char *logpg_area;
  int logpg_used_size;
  LOG_PAGEID next_fpageid;
  LOGWR_MODE next_mode;
  LOGWR_MODE orig_mode = LOGWR_MODE_ASYNC;
  int status;
  int timeout;
  int rv;
  int error_code;
  bool check_cs_own = false;
  bool is_interrupted = false;
  bool copy_from_file = false;
  bool need_cs_exit_after_send = true;
  struct timespec to;
  LOGWR_INFO *writer_info = &log_Gl.writer_info;
  bool copy_from_first_phy_page = false;

  logpg_used_size = 0;
  logpg_area =
    db_private_alloc (thread_p,
		      (LOGWR_COPY_LOG_BUFFER_NPAGES * LOG_PAGESIZE));
  if (logpg_area == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  if (thread_p->conn_entry)
    {
      thread_p->conn_entry->stop_phase = THREAD_STOP_LOGWR;
    }

  while (true)
    {
      if (mode & LOGWR_COPY_FROM_FIRST_PHY_PAGE_MASK)
	{
	  copy_from_first_phy_page = true;
	}
      else
	{
	  copy_from_first_phy_page = false;
	}
      mode &= ~LOGWR_COPY_FROM_FIRST_PHY_PAGE_MASK;

      /* In case that a non-ASYNC mode client internally uses ASYNC mode */
      orig_mode = MAX (mode, orig_mode);

      er_log_debug (ARG_FILE_LINE,
		    "[tid:%ld] xlogwr_get_log_pages, fpageid(%lld), mode(%s)\n",
		    thread_p->tid, first_pageid,
		    (mode == LOGWR_MODE_SYNC ? "sync" :
		     (mode == LOGWR_MODE_ASYNC ? "async" : "semisync")));

      /* Register the writer at the list and wait until LFT start to work */
      rv = pthread_mutex_lock (&writer_info->flush_start_mutex);
      error_code = logwr_register_writer_entry (&entry, thread_p,
						first_pageid, mode,
						copy_from_first_phy_page);
      if (error_code != NO_ERROR)
	{
	  pthread_mutex_unlock (&writer_info->flush_start_mutex);
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      if (entry->status == LOGWR_STATUS_WAIT)
	{
	  bool continue_checking = true;

	  if (mode == LOGWR_MODE_ASYNC)
	    {
	      timeout = LOGWR_THREAD_SUSPEND_TIMEOUT;
	      to.tv_sec = time (NULL) + timeout;
	      to.tv_nsec = 0;
	    }
	  else
	    {
	      timeout = INF_WAIT;
	      to.tv_sec = to.tv_nsec = 0;
	    }

	  rv = thread_suspend_with_other_mutex (thread_p,
						&writer_info->
						flush_start_mutex, timeout,
						&to, THREAD_LOGWR_SUSPENDED);
	  if (rv == ER_CSS_PTHREAD_COND_TIMEDOUT)
	    {
	      pthread_mutex_unlock (&writer_info->flush_start_mutex);

	      rv = pthread_mutex_lock (&writer_info->flush_end_mutex);
	      if (logwr_unregister_writer_entry (entry, LOGWR_STATUS_DELAY))
		{
		  pthread_cond_signal (&writer_info->flush_end_cond);
		}
	      pthread_mutex_unlock (&writer_info->flush_end_mutex);

	      continue;
	    }
	  else if (rv == ER_CSS_PTHREAD_MUTEX_LOCK
		   || rv == ER_CSS_PTHREAD_MUTEX_UNLOCK
		   || rv == ER_CSS_PTHREAD_COND_WAIT)
	    {
	      pthread_mutex_unlock (&writer_info->flush_start_mutex);

	      error_code = ER_FAILED;
	      status = LOGWR_STATUS_ERROR;
	      goto error;
	    }

	  pthread_mutex_unlock (&writer_info->flush_start_mutex);

	  if (logtb_is_interrupted (thread_p, false, &continue_checking))
	    {
	      /* interrupted, shutdown or connection has gone. */
	      error_code = ER_INTERRUPTED;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error_code, 0);
	      status = LOGWR_STATUS_ERROR;
	      goto error;
	    }
	  else if (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
	    {
	      if (logwr_is_delayed (entry))
		{
		  is_interrupted = true;
		  logwr_write_end (thread_p, writer_info, entry,
				   LOGWR_STATUS_DELAY);
		  continue;
		}

	      error_code = ER_INTERRUPTED;
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
	  pthread_mutex_unlock (&writer_info->flush_start_mutex);
	  LOG_CS_ENTER (thread_p);
	  check_cs_own = true;
	}

      if (thread_p->resume_status == THREAD_RESUME_DUE_TO_INTERRUPT)
	{
	  is_interrupted = true;
	}

      copy_from_file = (is_interrupted) ? true : false;
      /* Send the log pages to be flushed until now */
      error_code = logwr_pack_log_pages (thread_p, logpg_area,
					 &logpg_used_size, &status,
					 entry, copy_from_file);
      if (error_code != NO_ERROR)
	{
	  error_code = ER_HA_LW_FAILED_GET_LOG_PAGE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  error_code, 1, first_pageid);

	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      /* wait until LFT finishes flushing */
      rv = pthread_mutex_lock (&writer_info->flush_wait_mutex);

      if (entry->status == LOGWR_STATUS_FETCH
	  && writer_info->flush_completed == false)
	{
	  rv =
	    pthread_cond_wait (&writer_info->flush_wait_cond,
			       &writer_info->flush_wait_mutex);
	  assert_release (writer_info->flush_completed == true);
	}
      rv = pthread_mutex_unlock (&writer_info->flush_wait_mutex);

      if (entry->status == LOGWR_STATUS_FETCH)
	{
	  rv = pthread_mutex_lock (&writer_info->wr_list_mutex);
	  entry->start_copy_time = thread_get_log_clock_msec ();
	  pthread_mutex_unlock (&writer_info->wr_list_mutex);
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

      if (orig_mode == LOGWR_MODE_ASYNC
	  || (mode == LOGWR_MODE_ASYNC &&
	      (entry->status != LOGWR_STATUS_DELAY
	       || status != LOGWR_STATUS_DONE)))
	{
	  logwr_cs_exit (thread_p, &check_cs_own);
	  logwr_write_end (thread_p, writer_info, entry, status);
	  need_cs_exit_after_send = false;
	}

      error_code = xlog_send_log_pages_to_client (thread_p, logpg_area,
						  logpg_used_size, mode);
      if (error_code != NO_ERROR)
	{
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      /* Get the next request from the client and reset the arguments */
      error_code = xlog_get_page_request_with_reply (thread_p, &next_fpageid,
						     &next_mode);
      if (error_code != NO_ERROR)
	{
	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}

      logwr_update_last_eof_lsa (entry);

      /* In case of sync mode, unregister the writer and wakeup LFT to finish */
      if (need_cs_exit_after_send)
	{
	  logwr_cs_exit (thread_p, &check_cs_own);
	  logwr_write_end (thread_p, writer_info, entry, status);
	}

      /* Reset the arguments for the next request */
      first_pageid = next_fpageid;
      mode = next_mode;
      need_cs_exit_after_send = true;

      if (mode & LOGWR_COPY_FROM_FIRST_PHY_PAGE_MASK)
	{
	  assert_release (false);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HA_GENERIC_ERROR,
		  1, "Unexpected copy mode from copylogdb");
	  error_code = ER_HA_GENERIC_ERROR;

	  status = LOGWR_STATUS_ERROR;
	  goto error;
	}
    }

  db_private_free_and_init (thread_p, logpg_area);

  return NO_ERROR;

error:

  er_log_debug (ARG_FILE_LINE,
		"[tid:%ld] xlogwr_get_log_pages, error(%d)\n",
		thread_p->tid, error_code);

  logwr_cs_exit (thread_p, &check_cs_own);
  logwr_write_end (thread_p, writer_info, entry, status);

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
LOG_PAGEID
logwr_get_min_copied_fpageid (void)
{
  LOGWR_INFO *writer_info = &log_Gl.writer_info;
  LOGWR_ENTRY *entry;
  int num_entries = 0;
  LOG_PAGEID min_fpageid = LOGPAGEID_MAX;
  int rv;

  rv = pthread_mutex_lock (&writer_info->wr_list_mutex);

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

  pthread_mutex_unlock (&writer_info->wr_list_mutex);

  if (min_fpageid == LOGPAGEID_MAX || min_fpageid == LOGPB_HEADER_PAGE_ID)
    {
      min_fpageid = NULL_PAGEID;
    }

  return (min_fpageid);
}

#endif /* SERVER_MODE */
