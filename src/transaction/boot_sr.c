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
 * boot_sr.c - Boot management (at server)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(SOLARIS)
#include <netdb.h>
#endif /* SOLARIS */

/* for getcwd, possibly a candidate for the os_ library ? */
#if defined(WINDOWS)
#include <direct.h>
#else /* WINDOWS */
#include <unistd.h>
#endif /* WINDOWS */
#include <assert.h>

#include "porting.h"
#include "chartype.h"
#include "boot_sr.h"
#include "misc_string.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "system_parameter.h"
#include "file_io.h"
#include "page_buffer.h"
#include "log_impl.h"
#include "log_manager.h"
#include "disk_manager.h"
#include "lock_manager.h"
#include "locator_sr.h"
#include "heap_file.h"
#include "locator.h"
#include "slotted_page.h"
#include "extendible_hash.h"
#include "system_catalog.h"
#include "transaction_sr.h"
#include "transform.h"
#include "release_string.h"
#include "log_comm.h"
#include "critical_section.h"
#include "databases_file.h"
#include "query_manager.h"
#include "language_support.h"
#include "message_catalog.h"
#include "perf_monitor.h"
#include "set_object.h"
#include "object_domain.h"
#include "area_alloc.h"
#include "environment_variable.h"
#include "util_func.h"
#include "intl_support.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#include "connection_sr.h"
#include "server_support.h"
#include "tsc_timer.h"
#endif /* SERVER_MODE */
#include "serial.h"
#include "server_interface.h"
#include "jsp_sr.h"
#include "thread.h"
#include "xserver_interface.h"
#include "es.h"
#include "session.h"
#include "partition.h"
#include "event_log.h"

#if defined(WINDOWS)
#include "wintcp.h"
#else
#include "tcp.h"
#endif /* WINDOWS */

#if defined(ENABLE_SYSTEMTAP)
#include "probes.h"
#endif /* ENABLE_SYSTEMTAP */

#define BOOT_LEAVE_SAFE_OSDISK_PARTITION_FREE_SPACE  \
  (1250 * (IO_DEFAULT_PAGE_SIZE / IO_PAGESIZE))	/* 5 Mbytes */

static const int BOOT_VOLUME_MINPAGES = 50;
#define BOOT_FORMAT_MAX_LENGTH	500
#define BOOTSR_MAX_LINE	 500

typedef struct boot_dbparm BOOT_DB_PARM;
struct boot_dbparm
{
  VFID trk_vfid;		/* Tracker of files */
  HFID hfid;			/* Heap file where this information is stored.
				 * It is only used for validation purposes */
  HFID rootclass_hfid;		/* Heap file where classes are stored */
  EHID classname_table;		/* The hash file of class names */
  CTID ctid;			/* The catalog file */
  VFID query_vfid;		/* Query file */
  char rootclass_name[10];	/* Name of the root class */
  OID rootclass_oid;		/* OID of the root class */
  VOLID nvols;			/* Number of volumes that have been created */
  VOLID temp_nvols;		/* Number of temporary volumes that have been
				 * created */
  VOLID last_volid;		/* Next volume identifier */
  VOLID temp_last_volid;	/* Next temporary volume identifier. This goes
				 * from a higher number to a lower number */
  VFID vacuum_data_vfid;	/* Vacuum data file identifier */
  VFID dropped_files_vfid;	/* Vacuum dropped files file identifier */
};

#if defined(SERVER_MODE)
AUTO_ADDVOL_JOB boot_Auto_addvol_job = BOOT_AUTO_ADDVOL_JOB_INITIALIZER;
#endif

extern bool catcls_Enable;
extern int catcls_compile_catalog_classes (THREAD_ENTRY * thread_p);
extern int catcls_finalize_class_oid_to_oid_hash_table ();
extern int catcls_get_server_lang_charset (THREAD_ENTRY * thread_p,
					   int *charset_id_p, char *lang_buf,
					   const int lang_buf_size);
extern int catcls_get_db_collation (THREAD_ENTRY * thread_p,
				    LANG_COLL_COMPAT ** db_collations,
				    int *coll_cnt);
extern int catcls_find_and_set_serial_class_oid (THREAD_ENTRY * thread_p);
extern int catcls_find_and_set_partition_class_oid (THREAD_ENTRY * thread_p);

#if defined(SA_MODE)
int thread_Recursion_depth = 0;

extern void boot_client_all_finalize (bool is_er_final);
#endif /* SA_MODE */


BOOT_SERVER_STATUS boot_Server_status = BOOT_SERVER_DOWN;

#if defined(SERVER_MODE)
/* boot_cl.c:boot_Host_name[] if CS_MODE and SA_MODE */
char boot_Host_name[MAXHOSTNAMELEN] = "";
#endif /* SERVER_MODE */

/*
 * database parameter variables that do not change over time
 */
static char boot_Db_full_name[PATH_MAX];
static OID boot_Header_oid;	/* Location of parameters */
static BOOT_DB_PARM boot_Struct_db_parm;	/* The structure         */
static BOOT_DB_PARM *boot_Db_parm = &boot_Struct_db_parm;
static OID *boot_Db_parm_oid = &boot_Header_oid;
static int boot_Temp_volumes_tpgs = 0;
static int boot_Temp_volumes_max_pages = -2;
static int boot_Temp_volumes_sys_pages = 0;
static char boot_Lob_path[PATH_MAX + LOB_PATH_PREFIX_MAX] = "";
static bool skip_to_check_ct_classes_for_rebuild = false;
static char boot_Server_session_key[SERVER_SESSION_KEY_SIZE];

#if defined(SERVER_MODE)
static bool boot_Set_server_at_exit = false;
static int boot_Server_process_id = 1;
#endif /* SERVER_MODE */


/* Functions */
static int boot_get_db_parm (THREAD_ENTRY * thread_p,
			     const BOOT_DB_PARM * dbparm, OID * dbparm_oid);
static VOLID boot_add_volume (THREAD_ENTRY * thread_p,
			      DBDEF_VOL_EXT_INFO * ext_info);
static int boot_remove_volume (THREAD_ENTRY * thread_p, VOLID volid);
static int boot_remove_all_temp_volumes (THREAD_ENTRY * thread_p);
static int boot_xremove_temp_volume (THREAD_ENTRY * thread_p, VOLID volid,
				     const char *ignore_vlabel,
				     void *ignore_arg);
static void boot_remove_unknown_temp_volumes (THREAD_ENTRY * thread_p);
static int boot_parse_add_volume_extensions (THREAD_ENTRY * thread_p,
					     const char
					     *filename_addmore_vols);
static int boot_find_rest_volumes (THREAD_ENTRY * thread_p,
				   BO_RESTART_ARG * r_args, VOLID volid,
				   int (*fun) (THREAD_ENTRY * thread_p,
					       VOLID xvolid,
					       const char *vlabel,
					       void *args), void *args);
static int boot_find_rest_permanent_volumes (THREAD_ENTRY * thread_p,
					     bool newvolpath,
					     bool use_volinfo, VOLID volid,
					     int (*fun) (THREAD_ENTRY *
							 thread_p,
							 VOLID xvolid,
							 const char *vlabel,
							 void *args),
					     void *args);

static void boot_find_rest_temp_volumes (THREAD_ENTRY * thread_p, VOLID volid,
					 int (*fun) (THREAD_ENTRY * thread_p,
						     VOLID xvolid,
						     const char *vlabel,
						     void *args), void *args,
					 bool forward_dir,
					 bool check_before_access);
static int boot_check_permanent_volumes (THREAD_ENTRY * thread_p);
static int boot_mount (THREAD_ENTRY * thread_p, VOLID volid,
		       const char *vlabel, void *ignore_arg);
static VOLID boot_xadd_volume_extension (THREAD_ENTRY * thread_p,
					 DBDEF_VOL_EXT_INFO * ext_info);
static char *boot_find_new_db_path (char *db_pathbuf,
				    const char *fileof_vols_and_wherepaths);
static int
boot_create_all_volumes (THREAD_ENTRY * thread_p,
			 const BOOT_CLIENT_CREDENTIAL * client_credential,
			 const char *db_comments, DKNPAGES db_npages,
			 const char *file_addmore_vols, const char *log_path,
			 const char *log_prefix, DKNPAGES log_npages,
			 int client_lock_wait,
			 TRAN_ISOLATION client_isolation);
static int boot_remove_all_volumes (THREAD_ENTRY * thread_p,
				    const char *db_fullname,
				    const char *log_path,
				    const char *log_prefix, bool dirty_rem,
				    bool force_delete);
static char *boot_volume_info_log_path (char *log_path);
static void boot_remove_useless_path_separator (const char *path,
						char *new_path);
static void boot_ctrl_c_in_init_server (int ignore_signo);

#if defined(CUBRID_DEBUG)
static void boot_check_db_at_num_shutdowns (bool force_nshutdowns);
#endif /* CUBRID_DEBUG */

#if defined(SERVER_MODE)
static void boot_shutdown_server_at_exit (void);
#endif /* SERVER_MODE */


/*
 * bo_server) -set server's status, UP or DOWN
 *   return: void
 *   status(in) :
 */
void
boot_server_status (BOOT_SERVER_STATUS status)
{
  static const char *status_str[] =
    { "UNKNOWN", "UP", "DOWN", "MAINTENANCE" };
  if (status >= BOOT_SERVER_UP && status <= BOOT_SERVER_MAINTENANCE)
    {
      boot_Server_status = status;
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_BO_SERVER_STATUS,
	      1, status_str[status]);
#if defined(SERVER_MODE)
      if (boot_Set_server_at_exit == false)
	{
	  boot_Set_server_at_exit = true;
	  boot_Server_process_id = getpid ();
	  (void) atexit (boot_shutdown_server_at_exit);
	}
#endif /* SERVER_MODE */
    }
}

#if defined(SERVER_MODE)
/*
 * bo_shutdown_server_atexit () - make sure that the server is shutdown at exit
 *
 * return : nothing
 *
 * Note: This function is called when the invoked program terminates normally.
 *       This function make sure that the server is shutdown gracefully.
 */
static void
boot_shutdown_server_at_exit (void)
{
  if (BO_IS_SERVER_RESTARTED () && boot_Server_process_id == getpid ())
    {
      /* Avoid infinite looping if someone calls exit during shutdown */
      boot_Server_process_id++;
      (void) xboot_shutdown_server (NULL, true);
    }
}

/*
 * boot_donot_shutdown_server_at_exit () - do not shutdown server at exist.
 *
 * return : nothing
 *
 * Note: This function must be called when the system needs to exit without
 *       shutting down the system (e.g., in case of fatal failure).
 */
void
boot_donot_shutdown_server_at_exit (void)
{
  if (BO_IS_SERVER_RESTARTED () && boot_Server_process_id == getpid ())
    {
      boot_Server_process_id++;
    }
}
#endif /* SERVER_MODE */

/*
 * boot_get_db_parm () - retrieve database parameters from disk
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   dbparm(in): database parameter structure
 *   dbparm_oid(in): oid of parameter object
 *
 * Note: Retrieve the database boot/restart parameters from disk.
 */
static int
boot_get_db_parm (THREAD_ENTRY * thread_p, const BOOT_DB_PARM * dbparm,
		  OID * dbparm_oid)
{
  RECDES recdes;

  recdes.area_size = recdes.length = DB_SIZEOF (*dbparm);
  recdes.data = (char *) dbparm;
  if (heap_first (thread_p, &boot_Db_parm->hfid, NULL, dbparm_oid, &recdes,
		  NULL, COPY) != S_SUCCESS)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * boot_find_root_heap () - find the root heap
 *
 * return: root_hfid or NULL
 *
 * Note: Find the heap where the classes are stored.
 */
HFID *
boot_find_root_heap (void)
{
  return &boot_Db_parm->rootclass_hfid;
}

/*
 * xboot_find_number_permanent_volumes () - find the number of permanent volumes
 *
 * return : number of permanent volumes
 */
int
xboot_find_number_permanent_volumes (THREAD_ENTRY * thread_p)
{
  int nvols;

  if (csect_enter_as_reader (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) !=
      NO_ERROR)
    {
      return NULL_VOLID;
    }
  nvols = boot_Db_parm->nvols;
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return nvols;
}

/*
 * xboot_find_number_temp_volumes () - find the number of temporary volumes
 *
 * return : number of temporary volumes
 */
int
xboot_find_number_temp_volumes (THREAD_ENTRY * thread_p)
{
  int nvols;

  if (csect_enter_as_reader (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) !=
      NO_ERROR)
    {
      return NULL_VOLID;
    }
  nvols = boot_Db_parm->temp_nvols;
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return nvols;
}

/*
 * xboot_find_last_temp () - find the volid of next temporary volume
 *
 * return : volid of next temporary volume
 */
VOLID
xboot_find_last_temp (THREAD_ENTRY * thread_p)
{
  VOLID volid;

  if (csect_enter_as_reader (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) !=
      NO_ERROR)
    {
      return NULL_VOLID;
    }
  volid = boot_Db_parm->temp_last_volid;
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return volid;
}

/*
 * boot_find_next_permanent_volid () - find next volid for a permanent volume
 *
 * return : next volid for a permanent volume
 */
VOLID
boot_find_next_permanent_volid (THREAD_ENTRY * thread_p)
{
  VOLID volid;

  if (csect_enter (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) != NO_ERROR)
    {
      return NULL_VOLID;
    }
  volid = boot_Db_parm->last_volid + 1;
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return volid;
}

/*
 * boot_reset_db_parm () - reset database initialization parameters
 *                      (must be used only be log/recovery manager)
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 * Note: Reset database initialization parameters. It is used only during
 *       recovery of the database.
 */
int
boot_reset_db_parm (THREAD_ENTRY * thread_p)
{
  return boot_get_db_parm (thread_p, boot_Db_parm, boot_Db_parm_oid);
}

/*
 * boot_db_name () - find the name of the database
 *
 * return : name of the database
 *
 */
const char *
boot_db_name (void)
{
  return fileio_get_base_file_name (boot_Db_full_name);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * boot_db_full_name () - return current database full name.
 *
 * return : database full name
 */
const char *
boot_db_full_name ()
{
  return boot_Db_full_name;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * boot_get_lob_path - return the lob path which is read from databases.txt
 */
const char *
boot_get_lob_path (void)
{
  return boot_Lob_path;
}

/*
 * bo_maxpages_for_newvol () - find max pages that can be used to define a new
 *                             volume at given location
 *
 * return : max pages
 *
 * Note: Find the maximum number of pages that are accepted to safetly
 *       automatically create a volume extension or a temporary volume
 *       at the given location.
 */
DKNPAGES
boot_max_pages_new_volume (void)
{
  int nfree_pages;

  nfree_pages = (fileio_get_number_of_partition_free_pages (boot_Db_full_name,
							    IO_PAGESIZE)
		 - BOOT_LEAVE_SAFE_OSDISK_PARTITION_FREE_SPACE);
  if (nfree_pages < 0)
    {
      nfree_pages = 0;
    }

  return (DKNPAGES) nfree_pages;
}

/*
 * boot_add_volume () - add a volume to the database
 *
 * return : volid or NULL_VOLID (in case of failure)
 *
 *   ext_info(in): volume info
 *
 * Note: Add a new volume to the database. The volume may be a permanent or
 *       temporary volume. The addition of the volume is a system operation
 *       that will be either aborted in case of failure or committed in case of
 *       success, independently on the destiny of the current transaction.
 *       The volume becomes immediately available to other transactions.
 */
static VOLID
boot_add_volume (THREAD_ENTRY * thread_p, DBDEF_VOL_EXT_INFO * ext_info)
{
  VOLID volid;
  int vol_fd;
  RECDES recdes;		/* Record descriptor which describe the volume. */
  bool ignore_old;
  bool in_system_op = false;

  if (csect_enter (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) != NO_ERROR)
    {
      return NULL_VOLID;
    }

  /*
   * Assign a volume identifier according to its type
   */

  if (ext_info->purpose != DISK_TEMPVOL_TEMP_PURPOSE)
    {
      volid = boot_Db_parm->last_volid + 1;
      if (volid > LOG_MAX_DBVOLID
	  || (boot_Db_parm->temp_nvols > 0
	      && volid >= boot_Db_parm->temp_last_volid))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_MAXNUM_VOLS_HAS_BEEN_EXCEEDED, 1, LOG_MAX_DBVOLID);
	  goto error;
	}
    }
  else
    {
      if (boot_Db_parm->temp_nvols > 0)
	{
	  volid = boot_Db_parm->temp_last_volid - 1;
	}
      else
	{
	  volid = LOG_MAX_DBVOLID;
	}
      if (volid <= boot_Db_parm->last_volid)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_MAXNUM_VOLS_HAS_BEEN_EXCEEDED, 1, LOG_MAX_DBVOLID);
	  goto error;
	}
    }


  if (log_start_system_op (thread_p) == NULL)
    {
      goto error;
    }
  in_system_op = true;

  if (ext_info->overwrite == false && fileio_is_volume_exist (ext_info->name))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_VOLUME_EXISTS, 1, ext_info->name);
      goto error;
    }

  pgbuf_refresh_max_permanent_volume_id (volid);

  /* Format the volume */
  if (disk_format (thread_p, boot_Db_full_name,
		   volid, ext_info) == NULL_VOLID)
    {
      goto error;
    }

  if (logpb_add_volume (NULL, volid, ext_info->name, ext_info->purpose) !=
      volid)
    {
      goto error;
    }

  /*
   * Modify the system parameter table to reflect the addition of the volume
   */

  if (ext_info->purpose != DISK_TEMPVOL_TEMP_PURPOSE)
    {
      boot_Db_parm->nvols++;
      boot_Db_parm->last_volid = volid;
    }
  else
    {
      boot_Db_parm->temp_nvols++;
      boot_Db_parm->temp_last_volid = volid;
    }

  recdes.area_size = recdes.length = DB_SIZEOF (*boot_Db_parm);
  recdes.data = (char *) boot_Db_parm;

  if (heap_update (thread_p, &boot_Db_parm->hfid,
		   &boot_Db_parm->rootclass_oid, boot_Db_parm_oid,
		   &recdes, NULL, &ignore_old, NULL,
		   HEAP_UPDATE_IN_PLACE) == NULL)
    {
      /* Return back our global area of system parameter */
      if (ext_info->purpose != DISK_TEMPVOL_TEMP_PURPOSE)
	{
	  boot_Db_parm->nvols--;
	  boot_Db_parm->last_volid = volid - 1;
	}
      else
	{
	  boot_Db_parm->temp_nvols--;
	  if (boot_Db_parm->temp_nvols <= 0)
	    {
	      boot_Db_parm->temp_last_volid = NULL_VOLID;
	    }
	  else
	    {
	      boot_Db_parm->temp_last_volid = volid + 1;
	    }
	}
      goto error;
    }

  /*
   * Flush both the Dbparm object. This is not needed but it is good to do it,
   * so that during restart time we can mount every known volume. During media
   * crash that may not be possible. Thus, this is optional, we do not check
   * for error values.
   */

  heap_flush (thread_p, boot_Db_parm_oid);
  vol_fd = fileio_get_volume_descriptor (boot_Db_parm->hfid.vfid.volid);
  (void) fileio_synchronize (thread_p, vol_fd, ext_info->name);

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

  pgbuf_refresh_max_permanent_volume_id (boot_Db_parm->last_volid);
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

#if !defined(WINDOWS)
  if (prm_get_bool_value (PRM_ID_DBFILES_PROTECT))
    {
      fileio_set_permission (ext_info->name);
    }
#endif /* !WINDOWS */

  return volid;

error:
  if (in_system_op)
    {
      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
    }

  pgbuf_refresh_max_permanent_volume_id (boot_Db_parm->last_volid);
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return NULL_VOLID;
}

/*
 * boot_remove_volume () - remove a volume from the database
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   volid(in): Volume identifier to remove
 *
 * Note: Remove a volume from the database. The deletion of the volume is done
 *       independently of the destiny of the current transaction. That is,
 *       if this function finishes successfully the removal is made permanent,
 *       if the function fails, whatever was done is aborted.
 *       Currently, we do not allow to remove permananet volumes. In the future
 *       we may allow the removal of any volume but the primary volume
 *       (LOG_DBFIRST_VOLID).
 */
static int
boot_remove_volume (THREAD_ENTRY * thread_p, VOLID volid)
{
  RECDES recdes;		/* Record descriptor which describe the
				 * volume.
				 */
  char *vlabel = NULL;
  int vol_fd;
  bool ignore_old;
  int error_code = NO_ERROR;

  if (csect_enter (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /*
   * Make sure that this is a temporary volume
   */

  if (volid < boot_Db_parm->temp_last_volid)
    {
      if (volid >= LOG_DBFIRST_VOLID && volid <= boot_Db_parm->last_volid)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME, 1,
		  fileio_get_volume_label (volid, PEEK));
	  error_code = ER_BO_TRYING_TO_REMOVE_PERMANENT_VOLUME;
	}
      else
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_FILE_UNKNOWN_VOLID, 1, volid);
	  error_code = ER_FILE_UNKNOWN_VOLID;
	}
      goto end;
    }

  /*
   * Find the name of the volume to remove
   */

  vlabel = fileio_get_volume_label (volid, ALLOC_COPY);

  /*
   * Start a TOP SYSTEM OPERATION.
   * This top system operation will be either ABORTED (case of failure) or
   * COMMITTED independently of the current transaction, so that the volume
   * is removed immediately.
   */

  if (log_start_system_op (thread_p) == NULL)
    {
      error_code = ER_FAILED;
      goto end;
    }

  /*
   * Do the following for temporary volumes
   */

  boot_Db_parm->temp_nvols--;
  if (boot_Db_parm->temp_nvols <= 0)
    {
      boot_Db_parm->temp_last_volid = NULL_VOLID;
    }
  else if (boot_Db_parm->temp_last_volid == volid)
    {
      boot_Db_parm->temp_last_volid = volid + 1;
    }

  recdes.area_size = recdes.length = DB_SIZEOF (*boot_Db_parm);
  recdes.data = (char *) boot_Db_parm;

  if (heap_update (thread_p, &boot_Db_parm->hfid,
		   &boot_Db_parm->rootclass_oid, boot_Db_parm_oid, &recdes,
		   NULL, &ignore_old, NULL,
		   HEAP_UPDATE_IN_PLACE) != boot_Db_parm_oid)
    {
      boot_Db_parm->temp_nvols++;
      if (boot_Db_parm->temp_nvols == 1)
	{
	  boot_Db_parm->temp_last_volid = volid;
	}
      else if (volid < boot_Db_parm->temp_last_volid)
	{
	  boot_Db_parm->temp_last_volid = volid;
	}
      (void) log_end_system_op (thread_p, LOG_RESULT_TOPOP_ABORT);
      error_code = ER_FAILED;
      goto end;
    }

  /*
   * Flush the Dbparm object.. Not needed but it is good to do it, so that
   * during restart time we can mount every known volume. During media crash
   * that may not be possible... irrelevant..
   */

  heap_flush (thread_p, boot_Db_parm_oid);
  vol_fd = fileio_get_volume_descriptor (boot_Db_parm->hfid.vfid.volid);
  (void) fileio_synchronize (thread_p, vol_fd, vlabel);

  /*
   * The volume is not know by the system any longer. Remove it from disk
   */
  (void) pgbuf_invalidate_all (thread_p, volid);

  if (vlabel)
    {
      error_code = disk_unformat (thread_p, vlabel);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

  log_end_system_op (thread_p, LOG_RESULT_TOPOP_COMMIT);

  pgbuf_refresh_max_permanent_volume_id (boot_Db_parm->last_volid);
  (void) disk_goodvol_refresh (thread_p, boot_Db_parm->nvols);

end:
  if (vlabel)
    {
      free (vlabel);
    }
  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return error_code;
}

/*
 * xboot_add_volume_extension () - add a volume extension to the database
 *
 * return : volid or NULL_VOLID (in case of failure)
 *
 *   ext_info(in): volume info
 *
 * Note: Add a volume extension to the database. The addition of the volume
 *       is a system operation that will be either aborted in case of failure
 *       or committed in case of success, independently on the destiny of the
 *       current transaction. The volume becomes immediately available to other
 *       transactions.
 */
VOLID
xboot_add_volume_extension (THREAD_ENTRY * thread_p,
			    DBDEF_VOL_EXT_INFO * ext_info)
{
  DBDEF_VOL_EXT_INFO temp_ext_info = *ext_info;
  char real_pathbuf[PATH_MAX];

  if (temp_ext_info.path != NULL
      && realpath ((char *) temp_ext_info.path, real_pathbuf) != NULL)
    {
      temp_ext_info.path = real_pathbuf;
    }

  temp_ext_info.extend_npages = temp_ext_info.max_npages;

  return boot_xadd_volume_extension (thread_p, &temp_ext_info);
}

/*
 * boot_remove_useless_path_separator () - Remove useless PATH_SEPARATOR in path string
 *
 * return : true or false(in case of fail)
 *
 *   path(in): Original path.
 *   new_path(out): Transformed path.
 *
 * Note: This function removes uselses PATH_SEPARATOR in path string.
 *       For example,
 *       /home3/CUBRID/DB/               -->  /home3/CUBRID/DB
 *       C:\CUBRID\\\Databases\\         -->  C:\CUBRID\Databases
 *       \\pooh\user\                    -->  \\pooh\user
 *
 *       After transform..
 *       If new path string is "/" or "\", don't remove the last slash.
 *       It is survived.
 */
static void
boot_remove_useless_path_separator (const char *path, char *new_path)
{
  int slash_num = 0;		/* path separator counter */

  /* path must be not null */
  assert (path != NULL);
  assert (new_path != NULL);

  /*
   * Before transform.
   *   / h o m e 3 / / w o r k / c u b r i d / / / w o r k /
   *
   * After transform.
   *   / h o m e 3   / w o r k / c u b r i d     / w o r k
   */

  /* Consume the preceding continuous slash chars. */
  while (*path == PATH_SEPARATOR)
    {
      slash_num++;
      path++;
    }

  /* If there is preceding consumed slash, append PATH_SEPARATOR */
  if (slash_num)
    {
      *new_path++ = PATH_SEPARATOR;
#if defined(WINDOWS)
      /*
       * In Windows/NT,
       * If first duplicated PATH_SEPARATORs are appeared, they are survived.
       * For example,
       * \\pooh\user\ -> \\pooh\user(don't touch the first duplicated PATH_SEPARATORs)
       */
      if (slash_num > 1)
	{
	  *new_path++ = PATH_SEPARATOR;
	}
#endif /* WINDOWS */
    }

  /* Initialize separator counter again. */
  slash_num = 0;

  /*
   * If current character is PATH_SEPARATOR,
   *    skip after increasing separator counter.
   * If current character is normal character, copy to new_path.
   */
  while (*path)
    {
      if (*path == PATH_SEPARATOR)
	{
	  slash_num++;
	}
      else
	{
	  /*
	   * If there is consumed slash, append PATH_SEPARATOR.
	   * Initialize separator counter.
	   */
	  if (slash_num)
	    {
	      *new_path++ = PATH_SEPARATOR;
	      slash_num = 0;
	    }
	  *new_path++ = *path;
	}
      path++;
    }

  /* Assure null terminated string */
  *new_path = '\0';
}

/*
 * boot_xadd_volume_extension () -
 *
 * return :
 *
 * ext_info(in):
 */
static VOLID
boot_xadd_volume_extension (THREAD_ENTRY * thread_p,
			    DBDEF_VOL_EXT_INFO * ext_info)
{
  DBDEF_VOL_EXT_INFO temp_ext_info = *ext_info;
  VOLID volid;
  char vol_fullname[PATH_MAX];
  char ext_path_buf[PATH_MAX];
  DKNPAGES part_npages;
#if !defined(WINDOWS)
  char vol_realpath[PATH_MAX];
  char link_path[PATH_MAX];
  char link_fullname[PATH_MAX];
  struct stat stat_buf;
#endif

  /*
   * Get the name of the extension: ext_path|dbname|"ext"|volid
   */
  if (temp_ext_info.path == NULL)
    {
      temp_ext_info.path = prm_get_string_value (PRM_ID_IO_VOLUME_EXT_PATH);
      if (temp_ext_info.path == NULL)
	{
	  temp_ext_info.path = fileio_get_directory_path (ext_path_buf,
							  boot_Db_full_name);
	  if (temp_ext_info.path == NULL)
	    {
	      ext_path_buf[0] = '\0';
	      temp_ext_info.path = ext_path_buf;
	    }
	}
    }

  /*
   * Make sure that the name is going to be unique. We do not lock the object
   * DBparm to allow multiple transactions creating/deleting volumes
   */

  if (csect_enter (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) != NO_ERROR)
    {
      return NULL_VOLID;
    }

  if (temp_ext_info.name == NULL)
    {
      temp_ext_info.name = fileio_get_base_file_name (boot_Db_full_name);

      fileio_make_volume_ext_name (vol_fullname, temp_ext_info.path,
				   temp_ext_info.name,
				   boot_Db_parm->last_volid + 1);
    }
  else
    {
      fileio_make_volume_ext_given_name (vol_fullname, temp_ext_info.path,
					 temp_ext_info.name);
    }

#if !defined(WINDOWS)
  if (stat (vol_fullname, &stat_buf) == 0	/* file exist */
      && S_ISCHR (stat_buf.st_mode))
    {				/* is the raw device */

      temp_ext_info.path =
	fileio_get_directory_path (link_path, boot_Db_full_name);
      if (temp_ext_info.path == NULL)
	{
	  link_path[0] = '\0';
	  temp_ext_info.path = link_path;
	}

      temp_ext_info.name = fileio_get_base_file_name (boot_Db_full_name);
      fileio_make_volume_ext_name (link_fullname, temp_ext_info.path,
				   temp_ext_info.name,
				   boot_Db_parm->last_volid + 1);

      if (realpath (vol_fullname, vol_realpath) != NULL)
	{
	  strcpy (vol_fullname, vol_realpath);
	}

      (void) unlink (link_fullname);
      if (symlink (vol_fullname, link_fullname) != 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_LINK,
		  2, vol_fullname, link_fullname);

	  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

	  return NULL_VOLID;
	}

      strcpy (vol_fullname, link_fullname);

      /* we don't know character special files size */
      part_npages = VOL_MAX_NPAGES (IO_PAGESIZE);
    }
  else
    {
      part_npages = fileio_get_number_of_partition_free_pages (vol_fullname,
							       IO_PAGESIZE);
    }
#else /* !WINDOWS */
  part_npages = fileio_get_number_of_partition_free_pages (vol_fullname,
							   IO_PAGESIZE);
#endif /* !WINDOWS */

  /*
   * For automatic volume extensions do not let the disk partition to go too
   * full.
   */

  if (temp_ext_info.comments == NULL)
    {
      temp_ext_info.comments = " ";
    }

  if (temp_ext_info.max_npages > part_npages && part_npages >= 0)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_FORMAT_OUT_OF_SPACE, 5,
	      vol_fullname, temp_ext_info.max_npages,
	      ((IO_PAGESIZE / 1024) * ((off_t) temp_ext_info.max_npages)),
	      part_npages, ((IO_PAGESIZE / 1024) * ((off_t) part_npages)));
      volid = NULL_VOLID;
    }
  else
    {
      temp_ext_info.name = vol_fullname;
      temp_ext_info.comments = "Volume Extension";

      volid = boot_add_volume (thread_p, &temp_ext_info);
      if (volid != NULL_VOLID)
	{
	  (void) disk_goodvol_refresh_with_new (thread_p, volid);
	}
    }

  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return volid;
}


/*
 * boot_add_auto_volume_extension () - add an automatic volume extension to the database
 *
 * return : volid or NULL_VOLID (in case of failure)
 *
 * Note: Add an automatic volume extension to the database when the system
 *       allow it. The addition of the volume is a system operation that will
 *       be either aborted in case of failure or committed in case of success,
 *       independently on the destiny of the current transaction. The volume
 *       becomes immediately available to other transactions.
 */
VOLID
boot_add_auto_volume_extension (THREAD_ENTRY * thread_p, DKNPAGES min_npages,
				DISK_SETPAGE_TYPE setpage_type,
				DISK_VOLPURPOSE vol_purpose, bool wait)
{
#if defined (SERVER_MODE)
  bool old_check_interrupt;
  int new_vol_npages;
#endif
  VOLID volid;
  DBDEF_VOL_EXT_INFO ext_info;

  ext_info.max_npages =
    (DKNPAGES) (prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE) / IO_PAGESIZE);

  if (setpage_type != DISK_NONCONTIGUOUS_SPANVOLS_PAGES
      && ext_info.max_npages < min_npages)
    {
      ext_info.max_npages = min_npages;
    }

  if (ext_info.max_npages < BOOT_VOLUME_MINPAGES)
    {
      ext_info.max_npages = BOOT_VOLUME_MINPAGES;
    }

  ext_info.max_npages =
    MIN (ext_info.max_npages, VOL_MAX_NPAGES (IO_PAGESIZE));
  ext_info.path = NULL;
  ext_info.name = NULL;
  ext_info.comments = "Automatic Volume Extension";
  ext_info.purpose = DISK_PERMVOL_GENERIC_PURPOSE;
  ext_info.overwrite = false;
  ext_info.max_writesize_in_sec = 0;

#if defined (SERVER_MODE)
  ext_info.extend_npages =
    CEIL_PTVDIV (min_npages, AUTO_ADD_VOL_EXPAND_NPAGES) *
    AUTO_ADD_VOL_EXPAND_NPAGES;

retry:
  volid = NULL_VOLID;

  pthread_mutex_lock (&boot_Auto_addvol_job.lock);

  if (boot_Auto_addvol_job.ext_info.extend_npages > 0)
    {
      /* add_vol_job is already running */
      int allocating_npages;

      assert_release (boot_Auto_addvol_job.ext_info.purpose ==
		      DISK_PERMVOL_GENERIC_PURPOSE);

      if (wait)
	{
	  allocating_npages = boot_Auto_addvol_job.ext_info.extend_npages;

	  pthread_cond_wait (&boot_Auto_addvol_job.cond,
			     &boot_Auto_addvol_job.lock);

	  volid = boot_Auto_addvol_job.ret_volid;

	  if (volid != NULL_VOLID && allocating_npages < min_npages)
	    {
	      /* allocated size is not enough */
	      pthread_mutex_unlock (&boot_Auto_addvol_job.lock);
	      goto retry;
	    }
	}

      pthread_mutex_unlock (&boot_Auto_addvol_job.lock);

      /* if wait flag is false, NULL_VOLID will be returned */
      return volid;
    }

  if (thread_auto_volume_expansion_thread_is_running ())
    {
      /* volume_expansion_thread is active, but auto_addvol_job is empty.
       * Maybe, the last addvol_job was just finished.
       * Retry add job.
       */
      pthread_mutex_unlock (&boot_Auto_addvol_job.lock);
      thread_sleep (10);
      goto retry;
    }

  /* Register current job to boot_Auto_addvol_job */
  memcpy (&boot_Auto_addvol_job.ext_info, &ext_info,
	  sizeof (DBDEF_VOL_EXT_INFO));

  if (disk_cache_get_auto_extend_volid (thread_p) == NULL_VOLID)
    {
      /* New volume must be added */
      pthread_mutex_unlock (&boot_Auto_addvol_job.lock);

      /* add new volume with minimum pages */
      new_vol_npages =
	1 + disk_get_num_overhead_for_newvol (ext_info.max_npages);

      ext_info.extend_npages =
	CEIL_PTVDIV (new_vol_npages, AUTO_ADD_VOL_EXPAND_NPAGES) *
	AUTO_ADD_VOL_EXPAND_NPAGES;

      /* Do not check interrupt while volume extension */
      old_check_interrupt = thread_set_check_interrupt (thread_p, false);
      volid = boot_xadd_volume_extension (thread_p, &ext_info);
      thread_set_check_interrupt (thread_p, old_check_interrupt);

      if (volid != NULL_VOLID)
	{
	  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
		  ER_BO_NOTIFY_AUTO_VOLEXT, 2,
		  fileio_get_volume_label (volid, PEEK),
		  ext_info.extend_npages);
	}
      else
	{
	  /* just continue to expansion, although there was an error.
	   * In this case, auto expansion thread will detect this situation
	   * and set ret_volid to NULL_VOLID.
	   */
	}

      pthread_mutex_lock (&boot_Auto_addvol_job.lock);
    }

  /* expand volume */
  (void) thread_wakeup_auto_volume_expansion_thread ();

  if (wait)
    {
      pthread_cond_wait (&boot_Auto_addvol_job.cond,
			 &boot_Auto_addvol_job.lock);
      volid = boot_Auto_addvol_job.ret_volid;
    }

  pthread_mutex_unlock (&boot_Auto_addvol_job.lock);

#else /* !SERVER_MODE */
  ext_info.extend_npages = ext_info.max_npages;

  volid = boot_xadd_volume_extension (thread_p, &ext_info);

  if (volid != NULL_VOLID)
    {
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_BO_NOTIFY_AUTO_VOLEXT, 2,
	      fileio_get_volume_label (volid, PEEK), ext_info.extend_npages);
    }
#endif /* !SERVER_MODE */

  return volid;
}

/*
 * boot_parse_add_volume_extensions () - add a set of volume extensions
 *                                       to the database
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   filename_addmore_vols(in): File name where the volume specifications are
 *                              found
 *
 * Note: A set of volume extensions are added to the database. The given
 *       parameter is a file which indicates the specifications for all
 *       the volumes to be created. A volume is specified by ONE LINE which
 *       can contain the following parameters:
 *       [NAME volname] [PATH volpath] [COMMENTS volcomments]
 *                      [PURPOSE volpurpose] PAGES volnpages
 *
 *       The additions of the volumes is a system operations that will be either
 *       aborted in case of failure or committed in case of success,
 *       independently on the destiny of the current transaction. The volume
 *       becomes immediately available to other transactions.
 */
static int
boot_parse_add_volume_extensions (THREAD_ENTRY * thread_p,
				  const char *filename_addmore_vols)
{
  FILE *fp;
  DBDEF_VOL_EXT_INFO ext_info;
  char input_buffer[BOOTSR_MAX_LINE + 1];
  char *line;
  char *token;
  char *token_value;
  char *ext_name;
  char *ext_path;
  char *ext_comments;
  DKNPAGES ext_npages;
  DISK_VOLPURPOSE ext_purpose;
  int line_num = 0;
  int error_code = NO_ERROR;

  fp = fopen (filename_addmore_vols, "r");
  if (fp == NULL)
    {
      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			   ER_LOG_USER_FILE_UNKNOWN, 1,
			   filename_addmore_vols);
      return ER_LOG_USER_FILE_UNKNOWN;
    }

  /*
   * Get a line
   * Continue parsing even in case of error, so that we can indicate as
   * many errors as possible.
   */

  while ((line = fgets (input_buffer, BOOTSR_MAX_LINE, fp)) != NULL)
    {
      line_num++;

      /* Ignore lines with comments */

      if (line[0] == '\0' || line[0] == '#')
	{
	  continue;
	}

      ext_npages = (DKNPAGES) strlen (line);
      if (line[ext_npages - 1] != '\n')
	{
	  line[ext_npages] = '\n';
	  line[ext_npages + 1] = '\0';
	}

      /*
       * Parse the line
       */

      ext_name = NULL;
      ext_path = NULL;
      ext_comments = NULL;
      ext_npages = 0;
      ext_purpose = DISK_PERMVOL_GENERIC_PURPOSE;

      while (true)
	{
	  /*
	   * Read token.. skip leading whitespace and comments
	   */
	  while (char_isspace (line[0]))
	    {
	      line++;
	    }

	  if (line[0] == '\0')
	    {
	      break;
	    }

	  token = line;

	  do
	    {
	      line++;
	    }
	  while (!char_isspace (line[0]));
	  line[0] = '\0';
	  line++;

	  /*
	   * Skip any whitespace before the value.
	   */

	  while (char_isspace (line[0]))
	    {
	      line++;
	    }

	  token_value = line;

	  /*
	   * If string in " xxx " or ' xxxx ' find its delimiter
	   */

	  if (token_value[0] == '"' || token_value[0] == '\'')
	    {
	      int delim;

	      delim = token_value[0];
	      token_value++;
	      do
		{
		  line++;
		}
	      while (line[0] != delim);
	      line[0] = '\0';
	      line++;
	    }
	  else
	    {
	      do
		{
		  line++;
		}
	      while (!char_isspace (line[0]));
	      line[0] = '\0';
	      line++;
	    }

	  if (intl_mbs_casecmp (token, "NAME") == 0)
	    {
	      /* Name of volume */
	      ext_name = token_value;
	    }
	  else if (intl_mbs_casecmp (token, "PATH") == 0)
	    {
	      ext_path = token_value;
	    }
	  else if (intl_mbs_casecmp (token, "COMMENTS") == 0)
	    {
	      ext_comments = token_value;
	    }
	  else if (intl_mbs_casecmp (token, "PURPOSE") == 0)
	    {
	      if (intl_mbs_casecmp (token_value, "DATA") == 0)
		{
		  ext_purpose = DISK_PERMVOL_DATA_PURPOSE;
		}
	      else if (intl_mbs_casecmp (token_value, "INDEX") == 0)
		{
		  ext_purpose = DISK_PERMVOL_INDEX_PURPOSE;
		}
	      else if (intl_mbs_casecmp (token_value, "TEMP") == 0)
		{
		  ext_purpose = DISK_PERMVOL_TEMP_PURPOSE;
		}
	      else if (intl_mbs_casecmp (token_value, "GENERIC") == 0)
		{
		  ext_purpose = DISK_PERMVOL_GENERIC_PURPOSE;
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_BO_PARSE_ADDVOLS_UNKNOWN_PURPOSE, 2,
			  token_value, line_num);
		  error_code = ER_BO_PARSE_ADDVOLS_UNKNOWN_PURPOSE;
		  break;
		}
	    }
	  else if (intl_mbs_casecmp (token, "NPAGES") == 0)
	    {
	      if (sscanf (token_value, "%i", (int *) &ext_npages) != 1)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_BO_PARSE_ADDVOLS_NOGIVEN_NPAGES, 1, line_num);
		  error_code = ER_BO_PARSE_ADDVOLS_NOGIVEN_NPAGES;
		  break;
		}
	      else if (ext_npages <= 0)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_BO_PARSE_ADDVOLS_BAD_NPAGES, 2,
			  ext_npages, line_num);
		  break;
		}
	    }
	  else
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_BO_PARSE_ADDVOLS_UNKNOWN_TOKEN, 2, token, line_num);
	    }
	}

      /*
       * Add the volume
       */
      if (error_code != NO_ERROR
	  || (ext_name == NULL && ext_path == NULL && ext_comments == NULL
	      && ext_npages == 0))
	{
	  continue;
	}

      if (ext_npages <= 0)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_PARSE_ADDVOLS_NOGIVEN_NPAGES, 1, line_num);
	  error_code = ER_BO_PARSE_ADDVOLS_NOGIVEN_NPAGES;
	  continue;
	}

      ext_info.path = ext_path;
      ext_info.name = ext_name;
      ext_info.comments = ext_comments;
      ext_info.max_npages = ext_npages;
      ext_info.max_writesize_in_sec = 0;
      ext_info.purpose = ext_purpose;
      ext_info.overwrite = false;

      if (xboot_add_volume_extension (thread_p, &ext_info) == NULL_VOLID)
	{
	  error_code = ER_FAILED;
	}
    }
  fclose (fp);

  return error_code;
}

/*
 * boot_add_temp_volume () - add a temporarily volume to the database or
 *                     expand a current temporary volume
 *
 * return : volid or NULL_VOLID (in case of failure)
 *
 *   min_npages(in): Number of pages
 *
 * Note: Add a temporary volume to the database. The creation of the
 *       volume does not depend on the destiny of the current
 *       transaction. The addition of the volume is a system operation
 *       that will be either aborted in case of failure or committed in
 *       case of success, independently on the destiny of the current
 *       transaction. Note that the current transaction is not
 *       committed, only the current operation is committed.
 *       Any temporary volumes that remains at shutdown time or restart
 *       time are destroyed. However, temporary volumes should be
 *       in general destroyed by the caller.
 */
VOLID
boot_add_temp_volume (THREAD_ENTRY * thread_p, DKNPAGES min_npages)
{
  VOLID temp_volid;
  char temp_vol_fullname[PATH_MAX];
  char *temp_path;
  const char *temp_name;
  char temp_path_buf[PATH_MAX];
  DKNPAGES ext_npages, part_npages;
  DBDEF_VOL_EXT_INFO ext_info;
#if defined (SERVER_MODE)
  TSC_TICKS start_tick, end_tick;
  TSCTIMEVAL tv_diff;
#endif /* SERVER_MODE */

  if (boot_Temp_volumes_max_pages == -2)
    {
      /*
       * Get the maximum number of temporary pages that can be allocated for
       * all temporary volumes
       */
      boot_Temp_volumes_max_pages =
	prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES);
      if (boot_Temp_volumes_max_pages < 0)
	{
	  boot_Temp_volumes_max_pages = -1;	/* Infinite, until out of disk space */
	}
      else
	{
	  if (boot_Temp_volumes_max_pages < BOOT_VOLUME_MINPAGES)
	    {
	      boot_Temp_volumes_max_pages = 0;	/* Don't allocate any temp space */
	    }
	}
    }

  if (min_npages < BOOT_VOLUME_MINPAGES)
    {
      min_npages = BOOT_VOLUME_MINPAGES;
    }

  if (boot_Temp_volumes_sys_pages == 0)
    {
      DKNPAGES sect_alloctb_npages, page_alloctb_npages;
      PAGEID sect_alloctb_page1, page_alloctb_page1, sys_lastpage;
      DKNPAGES max_npages;

      max_npages = boot_get_temp_temp_vol_max_npages ();
      if (disk_set_alloctables (DISK_TEMPVOL_TEMP_PURPOSE, 0, max_npages,
				&sect_alloctb_npages,
				&page_alloctb_npages,
				&sect_alloctb_page1, &page_alloctb_page1,
				&sys_lastpage) != NO_ERROR)
	{
	  return NULL_VOLID;
	}
      boot_Temp_volumes_sys_pages = sys_lastpage + 1;
    }

  /*
   * Get the name of the extension: ext_path|dbname|"ext"|volid
   */

  /* Use the directory user specified
   * if NULL, use the directory where the primary volume is located
   */
  temp_path = (char *) prm_get_string_value (PRM_ID_IO_TEMP_VOLUME_PATH);
  if (temp_path == NULL || temp_path[0] == '\0')
    {
      temp_path =
	fileio_get_directory_path (temp_path_buf, boot_Db_full_name);
    }

  temp_name = fileio_get_base_file_name (boot_Db_full_name);

  /*
   * Make sure that the name is going to be unique. We do not lock the object
   * DBparm to allow multiple transactions creating/deleting volumes
   */

  if (csect_enter (thread_p, CSECT_BOOT_SR_DBPARM, INF_WAIT) != NO_ERROR)
    {
      return NULL_VOLID;
    }

  /* Get the name of the extension */
  if (boot_Db_parm->temp_nvols > 0)
    {
      temp_volid = boot_Db_parm->temp_last_volid - 1;
    }
  else
    {
      temp_volid = LOG_MAX_DBVOLID;
    }

  fileio_make_volume_temp_name (temp_vol_fullname, temp_path, temp_name,
				temp_volid);

  part_npages = (fileio_get_number_of_partition_free_pages (temp_vol_fullname,
							    IO_PAGESIZE)
		 - BOOT_LEAVE_SAFE_OSDISK_PARTITION_FREE_SPACE);

  if (min_npages > part_npages && part_npages >= 0)
    {
      ext_npages = part_npages;
    }
  else
    {
      ext_npages = min_npages;
    }

  /* Do not overpass any limit indicated by system parameters */

  if (boot_Temp_volumes_max_pages >= 0
      && (boot_Temp_volumes_tpgs + ext_npages) > boot_Temp_volumes_max_pages)
    {
      ext_npages = boot_Temp_volumes_max_pages - boot_Temp_volumes_tpgs;
    }

  /* Do not allocate fewer pages than the number needed by the caller */
  if (ext_npages < min_npages)
    {
      ext_npages = min_npages;
    }

  /*
   * Do not allocate more than the limit indicated by the User through
   * system parameters
   */

  if (boot_Temp_volumes_max_pages >= 0
      && (boot_Temp_volumes_tpgs + ext_npages) > boot_Temp_volumes_max_pages)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_MAXTEMP_SPACE_HAS_BEEN_EXCEEDED, 1,
	      boot_Temp_volumes_max_pages);
      temp_volid = NULL_VOLID;
    }
  else
    {
      if (boot_Db_parm->temp_nvols > 0
	  && (part_npages =
	      disk_expand_tmp (thread_p, boot_Db_parm->temp_last_volid,
			       min_npages, ext_npages)) >= min_npages)
	{
	  temp_volid = boot_Db_parm->temp_last_volid;
	  boot_Temp_volumes_tpgs += part_npages;	/* Expansion of pages */
	}
      else
	{
	  /*
	   * Definitely create a new temporary volume
	   */
	  /*
	   * must add system pages of temp volumes.
	   */
	  DKNPAGES possible_max_npages;

	  er_clear ();		/* clear error that was set by disk_expand_tmp() */

	  possible_max_npages = ext_npages + boot_Temp_volumes_sys_pages;
	  possible_max_npages = MIN (possible_max_npages,
				     boot_get_temp_temp_vol_max_npages ());

	  ext_info.path = NULL;
	  ext_info.name = temp_vol_fullname;
	  ext_info.comments = "Temporary Volume";
	  ext_info.max_npages = possible_max_npages;
	  ext_info.max_writesize_in_sec = 0;
	  ext_info.purpose = DISK_TEMPVOL_TEMP_PURPOSE;
	  ext_info.overwrite = true;
	  ext_info.extend_npages = ext_info.max_npages;

#if defined(SERVER_MODE)
	  tsc_getticks (&start_tick);
#endif /* SERVER_MODE */

	  temp_volid = boot_add_volume (thread_p, &ext_info);
	  if (temp_volid != NULL_VOLID)
	    {
	      boot_Temp_volumes_tpgs += ext_npages;
	      (void) disk_goodvol_refresh_with_new (thread_p, temp_volid);
	    }

#if defined(SERVER_MODE)
	  tsc_getticks (&end_tick);
	  tsc_elapsed_time_usec (&tv_diff, end_tick, start_tick);
	  TSC_ADD_TIMEVAL (thread_p->event_stats.temp_expand_time, tv_diff);

	  thread_p->event_stats.temp_expand_pages += possible_max_npages;
#endif /* SERVER_MODE */
	}
    }

  csect_exit (thread_p, CSECT_BOOT_SR_DBPARM);

  return temp_volid;
}

/*
 * boot_get_temp_temp_vol_max_npages
 *   a default temp temp volume grows up to 20G
 *   when prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES) is not specified.
 */
DKNPAGES
boot_get_temp_temp_vol_max_npages (void)
{
  if (prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES) < 0)
    {
      return (DKNPAGES) (((20LL * 1024LL * 1024LL * 1024LL) / IO_PAGESIZE));
    }
  else
    {
      return prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES);
    }
}

/*
 * boot_remove_all_temp_volumes () - remove all temporary volumes from the database
 *
 * return :NO_ERROR if all OK, ER_ status otherwise
 */
static int
boot_remove_all_temp_volumes (THREAD_ENTRY * thread_p)
{
  RECDES recdes;
  bool old_object;
  int error_code = NO_ERROR;

  /*
   * if volumes exist beyond bo_Dbparm.temp_last_volid,
   * we remove the volumes.
   * there is no logging to add or remove a temporary temp volume,
   * but logging to update bo_Dbparm.
   * so, unknown volumes can be possible to exist.
   */
  if (!BO_IS_SERVER_RESTARTED ())
    {
      boot_remove_unknown_temp_volumes (thread_p);
    }

  if (boot_Db_parm->temp_nvols == 0)
    {
      return error_code;
    }

  boot_find_rest_temp_volumes (thread_p, NULL_VOLID, boot_xremove_temp_volume,
			       NULL, true, false);

  if (boot_Db_parm->temp_nvols != 0
      || boot_Db_parm->temp_last_volid != NULL_VOLID)
    {
      boot_Db_parm->temp_nvols = 0;
      boot_Db_parm->temp_last_volid = NULL_VOLID;
      recdes.area_size = recdes.length = DB_SIZEOF (*boot_Db_parm);
      recdes.data = (char *) boot_Db_parm;
      if (heap_update (thread_p, &boot_Db_parm->hfid,
		       &boot_Db_parm->rootclass_oid, boot_Db_parm_oid,
		       &recdes, NULL, &old_object, NULL,
		       HEAP_UPDATE_IN_PLACE) != boot_Db_parm_oid
	  || xtran_server_commit (thread_p, false) != TRAN_UNACTIVE_COMMITTED)
	{
	  error_code = ER_FAILED;
	}
    }
  else
    {
      if (xtran_server_commit (thread_p, false) != TRAN_UNACTIVE_COMMITTED)
	{
	  error_code = ER_FAILED;
	}
    }

  return error_code;
}

/*
 * boot_xremove_temp_volume () - remove a temporary volume from the database
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   volid(in): Volume identifier to remove
 *   ignore_vlabel(in): Volume label (Unused)
 *   ignore_arg: Unused
 *
 * Note: Pass control to boot_remove_temp_volume to remove the temporary volume.
 */
static int
boot_xremove_temp_volume (THREAD_ENTRY * thread_p, VOLID volid,
			  const char *ignore_vlabel, void *ignore_arg)
{
  return boot_remove_volume (thread_p, volid);
}

/*
 * boot_remove_unknown_temp_volumes () -
 *
 * return: none
 */
static void
boot_remove_unknown_temp_volumes (THREAD_ENTRY * thread_p)
{
  VOLID temp_volid;
  char temp_vol_fullname[PATH_MAX];
  const char *temp_path;
  const char *temp_name;
  char *alloc_tempath = NULL;

  alloc_tempath = (char *) malloc (strlen (boot_Db_full_name) + 1);
  if (alloc_tempath == NULL)
    {
      return;
    }
  temp_path = fileio_get_directory_path (alloc_tempath, boot_Db_full_name);
  if (temp_path == NULL)
    {
      alloc_tempath[0] = '\0';
      temp_path = alloc_tempath;
    }
  temp_name = fileio_get_base_file_name (boot_Db_full_name);

  if (boot_Db_parm->temp_last_volid == NULL_VOLID)
    {
      temp_volid = LOG_MAX_DBVOLID;
    }
  else
    {
      temp_volid = boot_Db_parm->temp_last_volid - 1;
    }

  for (; temp_volid > boot_Db_parm->last_volid; temp_volid--)
    {
      fileio_make_volume_temp_name (temp_vol_fullname, temp_path, temp_name,
				    temp_volid);
      if (!fileio_is_volume_exist (temp_vol_fullname))
	{
	  break;
	}
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_BO_UNKNOWN_VOLUME, 1, temp_vol_fullname);
      fileio_unformat (thread_p, temp_vol_fullname);
    }

  if (alloc_tempath)
    {
      free_and_init (alloc_tempath);
    }
}

/*
 * boot_max_pages_for_new_auto_volume_extension () - find max pages that can
 *                                                   be allocated for an
 *                                                   automatic volume extension
 *
 * return : max pages
 *
 * Note: Find the maximum number of pages that are accepted to safetly
 *       automatically create a volume extension.
 */
DKNPAGES
boot_max_pages_for_new_auto_volume_extension (void)
{
  char vol_fullname[PATH_MAX];
  const char *ext_path;
  const char *ext_name;
  char *alloc_extpath = NULL;
  DKNPAGES npages;

  /*
   * Get the name of the extension: ext_path|dbname|"ext"|volid
   */

  /* Use the directory where the primary volume is located */
  alloc_extpath = (char *) malloc (strlen (boot_Db_full_name) + 1);
  if (alloc_extpath == NULL)
    {
      return 0;
    }
  ext_path = fileio_get_directory_path (alloc_extpath, boot_Db_full_name);
  if (ext_path == NULL)
    {
      alloc_extpath[0] = '\0';
      ext_path = alloc_extpath;
    }

  ext_name = fileio_get_base_file_name (boot_Db_full_name);
  fileio_make_volume_ext_name (vol_fullname, ext_path, ext_name, 1);

  npages = fileio_get_number_of_partition_free_pages (vol_fullname,
						      IO_PAGESIZE);

  if (alloc_extpath)
    {
      free_and_init (alloc_extpath);
    }

  return npages;
}

/*
 * boot_max_pages_for_new_temp_volume () - find max pages that can be allocated for a
 *                                  temporary volume
 *
 * return : max pages
 *
 * NOTGE: Find the maximum number of pages that are accepted to safetly
 *              automatically create a temporary volume.
 */
DKNPAGES
boot_max_pages_for_new_temp_volume (void)
{
  char temp_vol_fullname[PATH_MAX];
  const char *temp_path;
  const char *temp_name;
  char *alloc_tempath = NULL;
  DKNPAGES npages;

  if (boot_Temp_volumes_max_pages == -2)
    {
      /*
       * Get the maximum number of temporary pages that can be allocated for
       * all temporary volumes
       */
      boot_Temp_volumes_max_pages =
	prm_get_integer_value (PRM_ID_BOSR_MAXTMP_PAGES);
      if (boot_Temp_volumes_max_pages < 0)
	{
	  boot_Temp_volumes_max_pages = -1;	/* Infinite, until out of disk space */
	}
      else
	{
	  if (boot_Temp_volumes_max_pages < BOOT_VOLUME_MINPAGES)
	    {
	      boot_Temp_volumes_max_pages = 0;	/* Don't allocate any temp space */
	    }
	}
    }

  /*
   * Get the name of the extension: ext_path|dbname|"ext"|volid
   */

  /* Use the directory where the primary volume is located */
  alloc_tempath = (char *) malloc (strlen (boot_Db_full_name) + 1);
  if (alloc_tempath == NULL)
    {
      return NULL_VOLID;
    }
  temp_path = fileio_get_directory_path (alloc_tempath, boot_Db_full_name);
  if (temp_path == NULL)
    {
      alloc_tempath[0] = '\0';
      temp_path = alloc_tempath;
    }

  temp_name = fileio_get_base_file_name (boot_Db_full_name);
  fileio_make_volume_temp_name (temp_vol_fullname, temp_path, temp_name,
				LOG_MAX_DBVOLID);

  npages = fileio_get_number_of_partition_free_pages (temp_vol_fullname,
						      IO_PAGESIZE);

  if (boot_Temp_volumes_max_pages >= 0
      && npages > (boot_Temp_volumes_max_pages - boot_Temp_volumes_tpgs))
    {
      npages = boot_Temp_volumes_max_pages - boot_Temp_volumes_tpgs;
    }

  if (alloc_tempath)
    {
      free_and_init (alloc_tempath);
    }

  return npages;
}

/*
 * boot_find_rest_volumes () - call function on the rest of vols
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   r_args(in): restart argument structure contains various options
 *   volid(in): Volume identifier
 *   fun(in): Function to call on volid, vlabel, and arguments
 *   args(in): Extra arguments for function to call
 *
 * Note: The given function is called for every single volume which is
 *              different from the given one.
 */
static int
boot_find_rest_volumes (THREAD_ENTRY * thread_p, BO_RESTART_ARG * r_args,
			VOLID volid, int (*fun) (THREAD_ENTRY * thread_p,
						 VOLID xvolid,
						 const char *vlabel,
						 void *args), void *args)
{
  int error_code = NO_ERROR;
  bool check;

  if (r_args != NULL)
    {
      check = r_args->newvolpath;
    }
  else
    {
      check = false;
    }

  error_code = boot_find_rest_permanent_volumes (thread_p, check, true, volid,
						 fun, args);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (r_args != NULL)
    {
      check = true;
    }
  else
    {
      check = false;
    }

  boot_find_rest_temp_volumes (thread_p, volid, fun, args, false, check);

  return error_code;
}

/*
 * boot_find_rest_permanent_volumes () - call function on the rest of permanent vols
 *                                  of database
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   newvolpath(in): restore the database and log volumes to the path
 *                   specified in the database-loc-file.
 *   use_volinfo(in): use volinfo indicator
 *   volid(in): Volume identifier
 *   fun(in): Function to call on volid, vlabel, and arguments
 *   args(in): Extra arguments for function to call
 *
 * Note: The given function is called for every single permanent volume
 *       which is different from the given one.
 */
static int
boot_find_rest_permanent_volumes (THREAD_ENTRY * thread_p, bool newvolpath,
				  bool use_volinfo, VOLID volid,
				  int (*fun) (THREAD_ENTRY * thread_p,
					      VOLID xvolid,
					      const char *vlabel, void *args),
				  void *args)
{
  VOLID num_vols = 0;
  VOLID next_volid = LOG_DBFIRST_VOLID;	/* Next volume identifier */
  char next_vol_fullname[PATH_MAX];	/* Next volume name       */
  int error_code = NO_ERROR;

  if (newvolpath || !use_volinfo
      || (num_vols = logpb_scan_volume_info (thread_p, NULL, volid,
					     LOG_DBFIRST_VOLID, fun,
					     args)) == -1)
    {
      /*
       * Don't use volinfo .. or could not find volinfo
       */

      /* First the primary volume, then the rest of the volumes */
      num_vols = 0;
      next_volid = LOG_DBFIRST_VOLID;
      strcpy (next_vol_fullname, boot_Db_full_name);

      /*
       * Do not assume that all the volumes are mounted. This function may be
       * called to mount the volumes. Thus, request to current volume for the
       * next volume instead of going directly through the volume identifier.
       */
      do
	{
	  num_vols++;
	  if (next_volid != volid)
	    {
	      error_code = (*fun) (thread_p, next_volid, next_vol_fullname,
				   args);
	      if (error_code != NO_ERROR)
		{
		  return error_code;
		}
	    }
	  if (disk_get_link (thread_p, next_volid, next_vol_fullname) == NULL)
	    {
	      return ER_FAILED;
	    }
	  next_volid++;
	}
      while (next_vol_fullname[0] != '\0');

      if (use_volinfo == true)
	{
	  /*
	   * The volinfo was not found.. Recreate it with the current information
	   */
	  (void) logpb_recreate_volume_info (thread_p);
	}
    }
  else
    {
      /*
       * Add the volume that was ignored, as long as it is in the range of a
       * valid one
       */
      if (volid != NULL_VOLID && volid >= LOG_DBFIRST_VOLID
	  && volid <= (num_vols + LOG_DBFIRST_VOLID))
	{
	  num_vols++;
	}

      if (num_vols != boot_Db_parm->nvols)
	{
	  error_code = boot_find_rest_permanent_volumes (thread_p, newvolpath,
							 false, volid, fun,
							 args);
	  if (error_code != NO_ERROR)
	    {
	      /* Still could not mount or find all necessary volumes */
	      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_BO_INCONSISTENT_NPERM_VOLUMES, 2, num_vols,
		      boot_Db_parm->nvols);
	      return error_code;
	    }

	  (void) logpb_recreate_volume_info (thread_p);
	  return NO_ERROR;
	}
    }

  if (num_vols < boot_Db_parm->nvols)
    {
      return ER_FAILED;
    }

  return error_code;
}

/*
 * bo_find_rest_tempvols () - call function on the rest of temporary vols of the
 *                            database
 *
 *   volid(in): Volume identifier
 *   fun(in): Function to call on volid, vlabel, and arguments
 *   args(in): Extra arguments for function to call
 *   forward_dir(in): direction of accesing the tempvols (forward/backward)
 *   check_before_access(in): if true, check the existence of volume before
 *                            access
 *
 * Note: The given function is called for every single temporary volume
 *       which is different from the given one.
 */
static void
boot_find_rest_temp_volumes (THREAD_ENTRY * thread_p, VOLID volid,
			     int (*fun) (THREAD_ENTRY * thread_p,
					 VOLID xvolid, const char *vlabel,
					 void *args), void *args,
			     bool forward_dir, bool check_before_access)
{
  VOLID temp_volid;
  char temp_vol_fullname[PATH_MAX];
  const char *temp_path;
  const char *temp_name;
  char *alloc_tempath = NULL;
  int num_vols;
  bool go_to_access;

  /*
   * Get the name of the extension: ext_path|dbname|"ext"|volid
   */

  /* Use the directory where the primary volume is located */
  alloc_tempath = (char *) malloc (strlen (boot_Db_full_name) + 1);
  if (alloc_tempath == NULL)
    {
      return;
    }
  temp_path = fileio_get_directory_path (alloc_tempath, boot_Db_full_name);
  if (temp_path == NULL)
    {
      alloc_tempath[0] = '\0';
      temp_path = alloc_tempath;
    }
  temp_name = fileio_get_base_file_name (boot_Db_full_name);

  if (boot_Db_parm->temp_nvols > 0)
    {
      /* Cycle over all temporarily volumes, skip the given one */
      if (forward_dir)
	{
	  for (num_vols = boot_Db_parm->temp_last_volid;
	       num_vols <= LOG_MAX_DBVOLID; num_vols++)
	    {
	      temp_volid = (VOLID) num_vols;
	      if (temp_volid != volid)
		{
		  /* Find the name of the volume */
		  fileio_make_volume_temp_name (temp_vol_fullname, temp_path,
						temp_name, temp_volid);
		  go_to_access = false;
		  if (check_before_access)
		    {
		      if (fileio_is_volume_exist (temp_vol_fullname) == true)
			{
			  go_to_access = true;
			}
		    }
		  else
		    {
		      go_to_access = true;
		    }
		  if (go_to_access)
		    {		/* Call the function */
		      (void) (*fun) (thread_p, temp_volid, temp_vol_fullname,
				     args);
		    }
		}
	    }
	}
      else
	{
	  for (num_vols = LOG_MAX_DBVOLID;
	       num_vols >= boot_Db_parm->temp_last_volid; num_vols--)
	    {
	      temp_volid = (VOLID) num_vols;
	      if (temp_volid != volid)
		{
		  /* Find the name of the volume */
		  fileio_make_volume_temp_name (temp_vol_fullname, temp_path,
						temp_name, temp_volid);
		  go_to_access = false;
		  if (check_before_access)
		    {
		      if (fileio_is_volume_exist (temp_vol_fullname) == true)
			{
			  go_to_access = true;
			}
		    }
		  else
		    {
		      go_to_access = true;
		    }
		  if (go_to_access)
		    {		/* Call the function */
		      (void) (*fun) (thread_p, temp_volid, temp_vol_fullname,
				     args);
		    }
		}
	    }
	}
    }

  if (alloc_tempath)
    {
      free_and_init (alloc_tempath);
    }
}

/*
 * boot_check_permanent_volumes () - check consistency of permanent volume names and number
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 * Note: Make sure that we can reach the expected number of volumes
 *       using the internal link list of permananet volumes.
 */
static int
boot_check_permanent_volumes (THREAD_ENTRY * thread_p)
{
  VOLID num_vols = 0;
  VOLID next_volid = LOG_DBFIRST_VOLID;	/* Next volume identifier */
  char next_vol_fullname[PATH_MAX];	/* Next volume name       */
  const char *vlabel;

  /*
   * Don't use volinfo .. or could not find volinfo
   */

  /* First the primary volume, then the rest of the volumes */
  num_vols = 0;
  next_volid = LOG_DBFIRST_VOLID;
  strcpy (next_vol_fullname, boot_Db_full_name);

  /*
   * Do not assume that all the volumes are mounted. This function may be
   * called to mount the volumes. Thus, request to current volume for the
   * next volume instead of going directly through the volume identifier.
   */
  do
    {
      num_vols++;
      /* Have to make sure a label exists, before we try to use it below */
      vlabel = fileio_get_volume_label (next_volid, PEEK);
      if (vlabel == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  return ER_GENERIC_ERROR;
	}

      if (util_compare_filepath (next_vol_fullname, vlabel) != 0)
	{
	  /*
	   * Names are different. The database was renamed outside the domain of
	   * the database (e.g., in Unix), or this is not a database.
	   * If volume information is not present, assume that this is not a
	   * database
	   */
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_BO_NOT_A_VOLUME, 1,
		  vlabel);
	}
      if (disk_get_link (thread_p, next_volid, next_vol_fullname) == NULL)
	{
	  return ER_GENERIC_ERROR;
	}
      next_volid++;
    }
  while (next_vol_fullname[0] != '\0');

  if (num_vols != boot_Db_parm->nvols)
    {
      er_set (ER_SYNTAX_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_INCONSISTENT_NPERM_VOLUMES, 2, num_vols,
	      boot_Db_parm->nvols);
      return ER_BO_INCONSISTENT_NPERM_VOLUMES;
    }

  return NO_ERROR;
}

/*
 * boot_mount () - mount given volume
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   volid(in): Volume identifier
 *   vlabel(in): Volume label
 *   arg_ignore: Unused
 */
static int
boot_mount (THREAD_ENTRY * thread_p, VOLID volid, const char *vlabel,
	    void *ignore_arg)
{
  char check_vlabel[PATH_MAX];

  if (fileio_mount (thread_p, boot_Db_full_name, vlabel, volid,
		    false, false) == NULL_VOLDES)
    {
      return ER_FAILED;
    }

  /* Check the label and give a warning if labels are not the same */
  if (xdisk_get_fullname (thread_p, volid, check_vlabel) == NULL)
    {
      fileio_dismount (thread_p, volid);
      return ER_FAILED;
    }

  if (util_compare_filepath (check_vlabel, vlabel) != 0)
    {
      /*
       * Names are different. The database was renamed outside the domain of
       * the database (e.g., in Unix), or this is not a database.
       * If volume information is not present, assume that this is not a
       * database
       */
      if (!logpb_find_volume_info_exist ())
	{
	  er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_NOT_A_VOLUME,
		  1, vlabel);
	  return ER_BO_NOT_A_VOLUME;
	}
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_BO_NOT_A_VOLUME,
	      1, vlabel);
    }

  return NO_ERROR;
}

#if !defined(WINDOWS)
static jmp_buf boot_Init_server_jmpbuf;
#endif

static bool boot_Init_server_is_canceled = false;

/*
 * boot_ctrl_c_in_init_server () -
 *
 * return:
 */
static void
boot_ctrl_c_in_init_server (int ignore_signo)
{
#if !defined(WINDOWS)
  _longjmp (boot_Init_server_jmpbuf, 1);
#else
  boot_Init_server_is_canceled = true;
#endif
}

/*
 * xboot_initialize_server () - initialize server
 *
 * return : transaction index or NULL_TRAN_INDEX in the case of
 *          error.
 *
 *   print_version(in): Flag which indicates if the version of CUBRID server
 *          is printed at the end of the initialization process.
 *   db_name(in): Database Name
 *   db_path(in): Directory where the database is created. It allows you
 *          to specify the exact pathname of a directory in which
 *          to create the new database. A NULL value is invalid.
 *   db_comments(in): Database creation comments such as name of the user who
 *          created the database, the date of the creation,the name
 *          of the intended application, or nothing at all. NULL
 *          can be passed if no comments are desired.
 *   db_npages(in): Total number of pages to allocate for the database.
 *   db_overwrite(in): Wheater to overwrite the database if it already exists.
 *   file_addmore_vols(in): More volumes are created during the initialization
 *          process.
 *   log_path(in): Directory where the log and backups of the database are
 *          created. We recommend placing log and backup in a
 *          different directory and disk device from the directory
 *          and disk device of the data volumes. A NULL value is
 *          invalid.
 *   db_server_host(in): Server host where the database will reside. The host is
 *          needed in a client/server environment to identify the
 *          server which will maintain (e.g., restart) the database
 *          A NULL is invalid.
 *   rootclass_oid(in): OID of root class (Set as a side effect)
 *   rootclass_hfid(in): Heap for classes  (Set as a side effect)
 *   client_prog_name(in): Name of the client program or NULL
 *   client_user_name(in): Name of the client user or NULL
 *   client_host_name(in): Name of the client host or NULL
 *   client_process_id(in): Identifier of the process of the host where the client
 *          client transaction runs.
 *   client_lock_wait(in): Wait for at least this number of milliseconds to acquire a
 *          lock. Negative value is infinite
 *   client_isolation(in): Isolation level. One of the following:
 *                         TRAN_REPEATABLE_READ
 *                         TRAN_READ_COMMITTED
 *                         TRAN_SERIALIZABLE
 * db_desired_pagesize(in): Desired pagesize for the new database. The given size
 *          must be power of 2 and greater or equal than 512.
 *
 * Note: The first step of any CUBRID application is to initialize a
 *       database. A database is composed of data volumes (or Unix file
 *       system files), database backup files, and log files. A data
 *       volume contains information on attributes, classes, indexes,
 *       and objects created in the database. A database backup is a
 *       fuzzy snapshot of the entire database. The backup is fuzzy
 *       since it can be taken online when other transactions are
 *       updating the database. The logs contain records that reflects
 *       changes to the database. The log and backup files are used by
 *       the system to recover committed and uncommitted transactions
 *       in the event of system and media crashes. Logs are also used
 *       to support user-initiated rollbacks.
 *
 *       The rest of this function is identical to the restart. A
 *       transaction for the current client session is automatically
 *       started.
 */
int
xboot_initialize_server (THREAD_ENTRY * thread_p,
			 const BOOT_CLIENT_CREDENTIAL * client_credential,
			 BOOT_DB_PATH_INFO * db_path_info,
			 bool db_overwrite, const char *file_addmore_vols,
			 volatile DKNPAGES db_npages,
			 PGLENGTH db_desired_pagesize,
			 volatile DKNPAGES log_npages,
			 PGLENGTH db_desired_log_page_size,
			 OID * rootclass_oid, HFID * rootclass_hfid,
			 int client_lock_wait,
			 TRAN_ISOLATION client_isolation)
{
  int tran_index = NULL_TRAN_INDEX;
  const char *log_prefix = NULL;
  DB_INFO *db = NULL;
  DB_INFO *dir = NULL;
  int dbtxt_vdes = NULL_VOLDES;
  char db_pathbuf[PATH_MAX];
  char vol_real_path[PATH_MAX];
  char log_pathbuf[PATH_MAX];
  char lob_pathbuf[LOB_PATH_PREFIX_MAX + PATH_MAX];
  char dbtxt_label[PATH_MAX];
  char fixed_pathbuf[PATH_MAX];
  char original_namebuf[PATH_MAX];
#if defined (NDEBUG)
  char format[BOOT_FORMAT_MAX_LENGTH];
#endif
  int error_code;
  void (*old_ctrl_c_handler) (int sig_no) = SIG_ERR;
  struct stat stat_buf;
  bool is_exist_volume;
  char *db_path, *log_path, *lob_path, *p;

  assert (client_credential != NULL);
  assert (db_path_info != NULL);

#if defined(SERVER_MODE)
  if (lang_init () != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1,
		  "Failed to initialize language module");
	}
      return NULL_TRAN_INDEX;
    }

  /* open the system message catalog, before prm_ ?  */
  if (msgcat_init () != NO_ERROR)
    {
      /* need an appropriate error */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG, 0);
      return NULL_TRAN_INDEX;
    }

  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANT_LOAD_SYSPRM, 0);
      return NULL_TRAN_INDEX;
    }

  area_init (false);
  set_area_init ();
  pr_area_init ();
  tp_init ();

  /* Initialize tsc-timer */
  tsc_init ();

  /* Clear error structure */
  er_clear ();
#endif /* SERVER_MODE */

#if defined(CUBRID_DEBUG)
  if ((int) strlen (ROOTCLASS_NAME) >
      DB_SIZEOF (boot_Db_parm->rootclass_name))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_UNABLE_TO_RESTART_SERVER, 1, ROOTCLASS_NAME);
      er_log_debug (ARG_FILE_LINE,
		    "xboot_initialize_server: ** SYSTEM COMPILATION ERROR **"
		    " Length (i.e., %d) of ROOTCLASS_NAME(i.e, %s) is bigger than"
		    " length (i.e., %d) of bo_Dbparm->rootclass_name field",
		    strlen (ROOTCLASS_NAME) + 1, ROOTCLASS_NAME,
		    DB_SIZEOF (boot_Db_parm->rootclass_name));
      /* Destroy everything */
      return NULL_TRAN_INDEX;
    }
#endif /* CUBRID_DEBUG */

  if (client_credential->db_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      client_credential->db_name);
      return NULL_TRAN_INDEX;
    }

  /*
   * Make sure that the db_path and log_path and lob_path are the canonicalized
   * absolute pathnames
   */

  memset (db_pathbuf, 0, sizeof (db_pathbuf));
  memset (log_pathbuf, 0, sizeof (log_pathbuf));
  memset (lob_pathbuf, 0, sizeof (lob_pathbuf));

  /*
   * for db path,
   * convert to absolute path, remove useless PATH_SEPARATOR
   */
  db_path = db_path_info->db_path;
  if (realpath (db_path, fixed_pathbuf) != NULL)
    {
      db_path = fixed_pathbuf;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DIRECTORY_DOESNOT_EXIST,
	      1, db_path);
      return NULL_TRAN_INDEX;
    }
  boot_remove_useless_path_separator (db_path, db_pathbuf);
  db_path = db_pathbuf;

  /*
   * for log path,
   * convert to absolute path, remove useless PATH_SEPARATOR
   */
  log_path = db_path_info->log_path;
  if (realpath (log_path, fixed_pathbuf) != NULL)
    {
      log_path = fixed_pathbuf;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DIRECTORY_DOESNOT_EXIST,
	      1, log_path);
      return NULL_TRAN_INDEX;
    }
  boot_remove_useless_path_separator (log_path, log_pathbuf);
  log_path = log_pathbuf;

  /*
   * for lob path,
   * convert to absolute path, remove useless PATH_SEPARATOR
   */
  lob_path = db_path_info->lob_path;
  if (es_get_type (lob_path) == ES_NONE)
    {
      snprintf (lob_pathbuf, sizeof (lob_pathbuf), "%s%s",
		LOB_PATH_DEFAULT_PREFIX, lob_path);
      p = lob_path = strchr (lob_pathbuf, ':') + 1;
    }
  else
    {
      p = lob_path = strchr (strcpy (lob_pathbuf, lob_path), ':') + 1;
    }

  if (lob_path == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_ES_INVALID_PATH,
	      1, lob_pathbuf);
      return NULL_TRAN_INDEX;
    }

  if (es_get_type (lob_pathbuf) == ES_POSIX)
    {
#if defined (WINDOWS)
      if (realpath (lob_path, fixed_pathbuf) != NULL
	  && (stat (fixed_pathbuf, &stat_buf) == 0
	      && S_ISDIR (stat_buf.st_mode)))
#else
      if (realpath (lob_path, fixed_pathbuf) != NULL)
#endif
	{
	  lob_path = fixed_pathbuf;
	}
      else
	{
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		  ER_BO_DIRECTORY_DOESNOT_EXIST, 1, lob_path);
	  if (mkdir (lob_path, 0777) < 0)
	    {
	      cub_dirname_r (lob_path, fixed_pathbuf, PATH_MAX);
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_ES_GENERAL, 2, "POSIX", fixed_pathbuf);
	      return NULL_TRAN_INDEX;
	    }
	}
      boot_remove_useless_path_separator (lob_path, p);
    }
  lob_path = lob_pathbuf;

  /*
   * Compose the full name of the database
   */
  snprintf (boot_Db_full_name, sizeof (boot_Db_full_name), "%s%c%s", db_path,
	    PATH_SEPARATOR, client_credential->db_name);

  /*
   * Initialize error structure, critical section, slotted page, heap, and
   * recovery managers
   */

#if defined(SERVER_MODE)
  sysprm_load_and_init (boot_Db_full_name, NULL);
#endif /* SERVER_MODE */

  mvcc_Enabled = prm_get_bool_value (PRM_ID_MVCC_ENABLED);

  /* If the server is already restarted, shutdown the server */
  if (BO_IS_SERVER_RESTARTED ())
    {
      (void) xboot_shutdown_server (thread_p, true);
    }

  log_prefix = fileio_get_base_file_name (client_credential->db_name);

  /*
   * Find logging information to create the log volume. If the page size is
   * not the same as the one in production mode, adjust the number of pages
   * allocated.
   */

  if (db_npages <= 0)
    {
      db_npages =
	(DKNPAGES) (prm_get_bigint_value (PRM_ID_DB_VOLUME_SIZE) /
		    db_desired_pagesize);
    }

  if (log_npages <= 0)
    {
      log_npages =
	(DKNPAGES) (prm_get_bigint_value (PRM_ID_LOG_VOLUME_SIZE) /
		    db_desired_log_page_size);
    }

  if (log_npages < 10)
    {
      log_npages = 10;
    }

  /*
   * get the database directory information in write mode.
   */

  if (cfg_maycreate_get_directory_filename (dbtxt_label) == NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
/* Temporary fix for NT file locking problem */
      || (dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
				     LOG_DBTXT_VOLID, 2, true)) == NULL_VOLDES
#endif /* !WINDOWS || DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
    )
    {
      goto exit_on_error;
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      if (cfg_read_directory_ex (dbtxt_vdes, &dir, true) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }
  else
    {
      if (cfg_read_directory (&dir, true) != NO_ERROR)
	{
	  goto exit_on_error;
	}
    }

  if (dir != NULL
      && ((db = cfg_find_db_list (dir, client_credential->db_name)) != NULL))
    {
      if (db_overwrite == false)
	{
	  /* There is a database with the same name and we cannot overwrite it */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DATABASE_EXISTS, 1,
		  client_credential->db_name);
	  goto exit_on_error;
	}
      else
	{
	  /*
	   * Delete the database.. to make sure that all backups, log archives, and
	   * so on are removed... then continue...
	   *
	   * Note: we do not call xboot_delete since it shuttdown the system and
	   *       update database.txt that we have a read copy of its content.
	   */

	  /* Note: for database replacement, we need to remove the old database
	   *       with its original path!
	   */
	  memset (original_namebuf, 0, sizeof (original_namebuf));

	  /* Compose the original full name of the database */
	  snprintf (original_namebuf, sizeof (original_namebuf), "%s%c%s",
		    db->pathname, PATH_SEPARATOR, db->name);

	  error_code = boot_remove_all_volumes (thread_p, original_namebuf,
						db->logpath, log_prefix,
						false, true);
	  if (error_code != NO_ERROR)
	    {
	      goto exit_on_error;
	    }
	}
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);	/* unlock the directory file */
      dbtxt_vdes = NULL_VOLDES;
      cfg_free_directory (dir);
      dir = NULL;
    }

  error_code = logpb_check_exist_any_volumes (thread_p, boot_Db_full_name,
					      log_path, log_prefix,
					      vol_real_path,
					      &is_exist_volume);
  if (error_code != NO_ERROR || is_exist_volume)
    {
      goto exit_on_error;
    }

#if !defined(WINDOWS)
  if (db_path_info->vol_path != NULL)
    {
      if (realpath ((char *) db_path_info->vol_path, vol_real_path) == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_CREATE_LINK,
		  2, vol_real_path, boot_Db_full_name);
	  goto exit_on_error;
	}
      else
	{
	  if (stat (vol_real_path, &stat_buf) != 0	/* file not exist */
	      || S_ISDIR (stat_buf.st_mode))
	    {			/* is directory */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_BO_CANNOT_CREATE_LINK, 2, vol_real_path,
		      boot_Db_full_name);
	      goto exit_on_error;
	    }
	  (void) unlink (boot_Db_full_name);
	  if (symlink (vol_real_path, boot_Db_full_name) != 0)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_BO_CANNOT_CREATE_LINK, 2, vol_real_path,
		      boot_Db_full_name);
	      goto exit_on_error;
	    }
	}
    }
#endif /* !WINDOWS */

  (void) db_set_page_size (db_desired_pagesize, db_desired_log_page_size);

  if ((int) strlen (boot_Db_full_name) > DB_MAX_PATH_LENGTH - 1)
    {
      /* db_path + db_name is too long */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 4,
	      db_path, client_credential->db_name,
	      strlen (boot_Db_full_name), DB_MAX_PATH_LENGTH - 1);
      goto exit_on_error;
    }

  old_ctrl_c_handler = os_set_signal_handler (SIGINT,
					      boot_ctrl_c_in_init_server);

#if !defined(WINDOWS)
  boot_Init_server_is_canceled = (_setjmp (boot_Init_server_jmpbuf) != 0);
#endif

  if (!boot_Init_server_is_canceled)
    {
      tran_index = boot_create_all_volumes (thread_p, client_credential,
					    db_path_info->db_comments,
					    db_npages, file_addmore_vols,
					    log_path,
					    (const char *) log_prefix,
					    log_npages, client_lock_wait,
					    client_isolation);

      if (tran_index != NULL_TRAN_INDEX && !boot_Init_server_is_canceled)
	{
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
	  dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
				     LOG_DBTXT_VOLID, 2, true);
	  if (dbtxt_vdes == NULL_VOLDES)
	    {
	      goto exit_on_error;
	    }
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      if (cfg_read_directory_ex (dbtxt_vdes, &dir, true) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }
	  else
	    {
	      if (cfg_read_directory (&dir, true) != NO_ERROR)
		{
		  goto exit_on_error;
		}
	    }

	  db = cfg_find_db_list (dir, client_credential->db_name);

	  /* Now create the entry in the database table */
	  if (db == NULL)
	    {
	      db = cfg_add_db (&dir, client_credential->db_name,
			       db_path, log_path, lob_path,
			       db_path_info->db_host);
	    }
	  else
	    {
	      cfg_update_db (db, db_path, log_path, lob_path,
			     db_path_info->db_host);
	    }

	  if (db == NULL || db->name == NULL || db->pathname == NULL
	      || db->logpath == NULL || db->lobpath == NULL
	      || db->hosts == NULL)
	    {
	      goto exit_on_error;
	    }

#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
	  /* Under Windows/NT, it appears that locking a file prevents
	   * a subsequent open for write by the same process.  The
	   * cfg_write_directory will never succeed as long as the file
	   * is "mounted" by fileio_mount().  To allow the cubrid.db file to
	   * be updated, dismount before calling cfg_.  Note that this leaves
	   * an extremely small windows where another process could steal
	   * our lock.
	   */
	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      fileio_dismount (thread_p, dbtxt_vdes);
	      dbtxt_vdes = NULL_VOLDES;
	    }
#endif /* WINDOWS && !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      cfg_write_directory_ex (dbtxt_vdes, dir);
	    }
	  else
	    {
	      cfg_write_directory (dir);
	    }
	  cfg_free_directory (dir);
	  dir = NULL;

	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      fileio_dismount (thread_p, dbtxt_vdes);
	      dbtxt_vdes = NULL_VOLDES;
	    }
	}
    }

  if (tran_index == NULL_TRAN_INDEX || boot_Init_server_is_canceled)
    {
      (void) boot_remove_all_volumes (thread_p, boot_Db_full_name,
				      log_path, (const char *) log_prefix,
				      true, true);
      if (boot_Init_server_is_canceled)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INTERRUPTED, 0);
	}
      else
	{
	  if (db_path_info->vol_path != NULL)
	    {
	      (void) unlink (boot_Db_full_name);
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_CANNOT_CREATE_VOL, 3, "volumes",
		  client_credential->db_name, er_get_msglog_filename ());
	}

      goto exit_on_error;
    }

  rootclass_oid->volid = boot_Db_parm->rootclass_oid.volid;
  rootclass_oid->pageid = boot_Db_parm->rootclass_oid.pageid;
  rootclass_oid->slotid = boot_Db_parm->rootclass_oid.slotid;
  rootclass_hfid->vfid.volid = boot_Db_parm->rootclass_hfid.vfid.volid;
  rootclass_hfid->vfid.fileid = boot_Db_parm->rootclass_hfid.vfid.fileid;
  rootclass_hfid->hpgid = boot_Db_parm->rootclass_hfid.hpgid;

  /* print_version string */
#if defined (NDEBUG)
  strncpy (format, msgcat_message (MSGCAT_CATALOG_CUBRID,
				   MSGCAT_SET_GENERAL,
				   MSGCAT_GENERAL_DATABASE_INIT),
	   BOOT_FORMAT_MAX_LENGTH);
  fprintf (stdout, format, rel_name (), rel_build_number ());
#else /* NDEBUG */
  fprintf (stdout, "\n%s (%s) (%d debug build)\n\n", rel_name (),
	   rel_build_number (), rel_bit_platform ());
#endif /* !NDEBUG */

  if (old_ctrl_c_handler != SIG_ERR)
    {
      (void) os_set_signal_handler (SIGINT, old_ctrl_c_handler);
    }

  return tran_index;

exit_on_error:

  if (old_ctrl_c_handler != SIG_ERR)
    {
      (void) os_set_signal_handler (SIGINT, old_ctrl_c_handler);
    }

  /* Destroy everything */
  if (dir)
    {
      cfg_free_directory (dir);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
    }

  if (tran_index != NULL_TRAN_INDEX)
    {
      logtb_release_tran_index (thread_p, tran_index);
      log_final (thread_p);
    }

  er_stack_push ();
  boot_server_all_finalize (thread_p, false);
  er_stack_pop ();

  return NULL_TRAN_INDEX;
}

static void
boot_make_session_server_key (void)
{
  UINT32 t;
  unsigned char ip[4];

  t = time (NULL);
  memcpy (boot_Server_session_key, &t, sizeof (UINT32));
  css_hostname_to_ip (boot_Host_name, ip);
  boot_Server_session_key[4] = ip[0];
  boot_Server_session_key[5] = ip[1];
  boot_Server_session_key[6] = ip[2];
  boot_Server_session_key[7] = ip[3];
}

/*
 * boot_restart_server () - restart server
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   print_restart(in): Flag which indicates if the version of CUBRID server
 *                      is printed at the end of the restart process.
 *   db_name(in): Database Name
 *   from_backup(in): Execute the restart from a backup..
 *   check_db_coll(in): True if check DB collations with system collations
 *   r_args(in): restart argument structure contains various options
 *
 * Note: The CUBRID server is restarted. Recovery process, no
 *       related to media failures (i.e., disk crashes), takes place
 *       during the restart process.
 */
int
boot_restart_server (THREAD_ENTRY * thread_p, bool print_restart,
		     const char *db_name, bool from_backup,
		     bool check_db_coll, BO_RESTART_ARG * r_args)
{
  char log_path[PATH_MAX];
  const char *log_prefix;
  DB_INFO *db = NULL;
  DB_INFO *dir = NULL;
  bool old_object;
#if defined (NDEBUG)
  char format[BOOT_FORMAT_MAX_LENGTH];
#endif
  int tran_index = NULL_TRAN_INDEX;
  int dbtxt_vdes = NULL_VOLDES;
  char dbtxt_label[PATH_MAX];
  RECDES recdes;
#if defined(SERVER_MODE)
  int common_ha_mode;
#endif
  int error_code = NO_ERROR;
  char *prev_err_msg;
  int db_charset_db_header = INTL_CODESET_NONE;
  int db_charset_db_root = INTL_CODESET_NONE;
  char db_lang[LANG_MAX_LANGNAME + 1];

  /* language data is loaded in context of server */
  if (lang_init () != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1,
		  "Failed to initialize language module");
	}
      return ER_LOC_INIT;
    }

  if (msgcat_init () != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG, 0);
      return ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG;
    }

#if defined(SERVER_MODE)
  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANT_LOAD_SYSPRM, 0);
      return ER_BO_CANT_LOAD_SYSPRM;
    }

  common_ha_mode = prm_get_integer_value (PRM_ID_HA_MODE);
#endif /* SERVER_MODE */

  if (db_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      db_name);
      return ER_BO_UNKNOWN_DATABASE;
    }

  /*
   * Make sure that there is a database.txt and that the desired database
   * exists. We do not want to lock the database.txt at this point since we
   * are only reading it. However, if we do not find the desired database,
   * we must lock the file to make sure that we got a consistent view of
   * database.txt (database.txt is written from scratch when it is updated).
   * That is, don't complain that a database does not exist until we have
   * a lock of database.txt
   */

  if (cfg_read_directory (&dir, false) != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_FILE, 1,
		  DATABASES_FILENAME);
	}
      return ER_CFG_NO_FILE;
    }

  if (dir == NULL || ((db = cfg_find_db_list (dir, db_name)) == NULL))
    {
      /*
       * Make sure that nobody was in the process of writing the
       * database.txt when we got a snapshot of it.
       */
      if (dir != NULL)
	{
	  cfg_free_directory (dir);
	  dir = NULL;
	}

      if (cfg_maycreate_get_directory_filename (dbtxt_label) != NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
/* Temporary fix for NT file locking problem */
	  && (dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
					 LOG_DBTXT_VOLID, 2,
					 true)) != NULL_VOLDES
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
	)
	{
	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      if (cfg_read_directory_ex (dbtxt_vdes, &dir, false) == NO_ERROR)
		{
		  db = cfg_find_db_list (dir, db_name);
		}
	    }
	  else
	    {
	      if (cfg_read_directory (&dir, false) == NO_ERROR)
		{
		  db = cfg_find_db_list (dir, db_name);
		}
	    }

	  fileio_dismount (thread_p, dbtxt_vdes);
	  dbtxt_vdes = NULL_VOLDES;
	}
      if (db == NULL)
	{
	  if (dir != NULL)
	    {
	      cfg_free_directory (dir);
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
		  db_name);
	  return ER_BO_UNKNOWN_DATABASE;
	}
    }

  if (GETHOSTNAME (boot_Host_name, MAXHOSTNAMELEN) != 0)
    {
      strcpy (boot_Host_name, "(unknown)");
    }
  boot_Host_name[MAXHOSTNAMELEN - 1] = '\0';	/* bullet proof */

  COMPOSE_FULL_NAME (boot_Db_full_name, sizeof (boot_Db_full_name),
		     db->pathname, db_name);
  boot_make_session_server_key ();

  if (boot_volume_info_log_path (log_path) == NULL)
    {
      strcpy (log_path, db->logpath);
    }

  if (db->lobpath != NULL)
    {
      strlcpy (boot_Lob_path, db->lobpath, sizeof (boot_Lob_path));
    }
  else
    {
      boot_Lob_path[0] = '\0';
    }

  /*
   * Initialize error structure, critical section, slotted page, heap, and
   * recovery managers
   */
#if defined(SERVER_MODE)
  if (sysprm_load_and_init (boot_Db_full_name, NULL) != NO_ERROR)
    {
      cfg_free_directory (dir);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANT_LOAD_SYSPRM, 0);
      return ER_BO_CANT_LOAD_SYSPRM;
    }

  mvcc_Enabled = prm_get_bool_value (PRM_ID_MVCC_ENABLED);

  if (common_ha_mode != prm_get_integer_value (PRM_ID_HA_MODE)
      && prm_get_integer_value (PRM_ID_HA_MODE) != HA_MODE_OFF)
    {
      cfg_free_directory (dir);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_PRM_CONFLICT_EXISTS_ON_MULTIPLE_SECTIONS, 6, "cubrid.conf",
	      "common", prm_get_name (PRM_ID_HA_MODE),
	      css_ha_mode_string (common_ha_mode), db_name,
	      css_ha_mode_string (prm_get_integer_value (PRM_ID_HA_MODE)));
      return ER_PRM_CONFLICT_EXISTS_ON_MULTIPLE_SECTIONS;
    }

  /* reinit msg catalog to reflect PRM_MAX_THREADS */
  msgcat_final ();
  if (msgcat_init () != NO_ERROR)
    {
      cfg_free_directory (dir);
      return ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG;
    }

  error_code = css_init_conn_list ();
  if (error_code != NO_ERROR)
    {
      cfg_free_directory (dir);
      return error_code;
    }

  /* reinitialize thread mgr to reflect # of active requests */
  if (thread_initialize_manager () != NO_ERROR)
    {
      cfg_free_directory (dir);
      return ER_FAILED;
    }
  if (er_init_internal
      (prm_get_string_value (PRM_ID_ER_LOG_FILE),
       prm_get_integer_value (PRM_ID_ER_EXIT_ASK), true) != NO_ERROR)
    {
      cfg_free_directory (dir);
      return ER_FAILED;
    }
  er_clear ();

  /* initialize allocations areas for things we need, on the client, most
   * of this is done inside ws_init().
   */
  area_init (false);
  set_area_init ();
  pr_area_init ();

  locator_initialize_areas ();

  /* initialize the type/doain module (also sets up an area) */
  tp_init ();

  /* Initialize tsc-timer */
  tsc_init ();

#if defined(DIAG_DEVEL)
  init_diag_mgr (server_name, thread_num_worker_threads (), NULL);
#endif /* DIAG_DEVEL */
#else /* !SERVER_MODE */
  mvcc_Enabled = prm_get_bool_value (PRM_ID_MVCC_ENABLED);
#endif /* !SERVER_MODE */

  mnt_server_init (MAX_NTRANS);

  /*
   * Compose the full name of the database and find location of logs
   */

  log_prefix = fileio_get_base_file_name (db_name);

  /* The database pagesize is set by log_get_io_page_size */

  if (log_get_io_page_size (thread_p, boot_Db_full_name, log_path, log_prefix)
      == -1)
    {
      if (from_backup == false || er_errid () == ER_IO_MOUNT_LOCKED)
	{
	  cfg_free_directory (dir);
	  return ER_FAILED;
	}
    }

  db_charset_db_header =
    log_get_charset_from_header_page (thread_p, boot_Db_full_name, log_path,
				      log_prefix);

  if (db_charset_db_header == INTL_CODESET_ERROR)
    {
      if (from_backup == false || er_errid () == ER_IO_MOUNT_LOCKED)
	{
	  cfg_free_directory (dir);
	  return ER_FAILED;
	}
      db_charset_db_header = INTL_CODESET_NONE;
    }

  /* Initialize the transaction table */
  logtb_define_trantable (thread_p, -1, -1);

  error_code = spage_boot (thread_p);
  if (error_code != NO_ERROR)
    {
      cfg_free_directory (dir);
      return error_code;
    }
  error_code = heap_manager_initialize ();
  if (error_code != NO_ERROR)
    {
      cfg_free_directory (dir);
      return error_code;
    }

  /*
   * How to restart the system ?
   */
  if (from_backup != false)
    {
      /*
       * RESTART FROM BACKUP
       */
      error_code = logpb_restore (thread_p, boot_Db_full_name, log_path,
				  log_prefix, r_args);
      if (error_code != NO_ERROR)
	{
	  cfg_free_directory (dir);
	  return error_code;
	}
    }

  /*
   * Now continue the normal restart process. At this point the data volumes
   * are ok. However, some recovery may need to take place
   */

  /* Mount the data volume */
  error_code = boot_mount (thread_p, LOG_DBFIRST_VOLID, boot_Db_full_name,
			   NULL);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Find the location of the database parameters and read them */
  if (disk_get_boot_hfid (thread_p, LOG_DBFIRST_VOLID, &boot_Db_parm->hfid) ==
      NULL)
    {
      fileio_dismount_all (thread_p);
      error_code = ER_FAILED;
      goto error;
    }

  error_code = boot_get_db_parm (thread_p, boot_Db_parm, boot_Db_parm_oid);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      goto error;
    }

  /* Find the rest of the volumes and mount them */

  error_code = boot_find_rest_volumes (thread_p, from_backup ? r_args : NULL,
				       LOG_DBFIRST_VOLID, boot_mount, NULL);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      goto error;
    }

  /*
   * Initialize the catalog manager, the query evaluator, and install meta
   * classes
   */

  if (locator_initialize (thread_p, &boot_Db_parm->classname_table) == NULL)
    {
      fileio_dismount_all (thread_p);
      error_code = ER_FAILED;
      goto error;
    }

  oid_set_root (&boot_Db_parm->rootclass_oid);

  error_code = catcls_find_and_set_serial_class_oid (thread_p);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      goto error;
    }

  error_code = catcls_find_and_set_partition_class_oid (thread_p);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      goto error;
    }

  error_code = file_tracker_cache_vfid (&boot_Db_parm->trk_vfid);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      goto error;
    }
  catalog_initialize (&boot_Db_parm->ctid);

  (void) qexec_initialize_xasl_cache (thread_p);
  qfile_initialize_list_cache (thread_p);
  if (qmgr_initialize (thread_p) != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      error_code = ER_FAILED;
      goto error;
    }
  (void) qexec_initialize_filter_pred_cache (thread_p);

  /*
   * Since no logging is done on temporary volumes, we can not trust
   * the bitmap of temporary volumes.  So we reset the temporary volumes
   * before recovery.  At a later time, the temporary volumes will be
   * removed.
   */

  error_code = disk_reinit_all_tmp (thread_p);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      goto error;
    }

  /*
   * Initialize system locale using values ffrom db_root system table
   */
  if (db_charset_db_header == INTL_CODESET_NONE)
    {
      /* was unable to read charset from header, use INTL_CODESET_ISO88591;
       * db_root does not contain fixed CHAR values, it is safe to read it
       * using ISO charset */
      (void) lang_set_charset (INTL_CODESET_ISO88591);
    }
  else
    {
      error_code = lang_set_charset (db_charset_db_header);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  error_code = catcls_get_server_lang_charset (thread_p, &db_charset_db_root,
					       db_lang, sizeof (db_lang) - 1);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* set charset and language using values of "db_root" */
  error_code = lang_set_charset (db_charset_db_root);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  error_code = lang_set_language (db_lang);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (db_charset_db_header >= 0 && db_charset_db_header != db_charset_db_root)
    {
      char er_msg[ERR_MSG_SIZE];
      snprintf (er_msg, sizeof (er_msg) - 1, "Invalid charset in db_root "
		"system table: expecting %s, found %s",
		lang_charset_cubrid_name (db_charset_db_header),
		lang_charset_cubrid_name (db_charset_db_root));
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1, er_msg);
      goto error;
    }

  if (mvcc_Enabled)
    {
      /* We need to load vacuum data and initialize vacuum routine before
       * recovery.
       */
      error_code =
	vacuum_initialize (thread_p, &boot_Db_parm->vacuum_data_vfid,
			   &boot_Db_parm->dropped_files_vfid);
      if (error_code != NO_ERROR)
	{
	  fileio_dismount_all (thread_p);
	  goto error;
	}
      error_code = vacuum_load_data_from_disk (thread_p);
      if (error_code != NO_ERROR)
	{
	  fileio_dismount_all (thread_p);
	  goto error;
	}
    }

  /*
   * Now restart the recovery manager and execute any recovery actions
   */

  log_initialize (thread_p, boot_Db_full_name, log_path, log_prefix,
		  from_backup, (r_args) ? &r_args->stopat : NULL);

  if (mvcc_Enabled)
    {
      /* Make sure dropped files are loaded from disk after recovery */
      error_code = vacuum_load_dropped_files_from_disk (thread_p);
      if (error_code != NO_ERROR)
	{
	  fileio_dismount_all (thread_p);
	  goto error;
	}
    }

  /*
   * Allocate a temporary transaction index to finish further system related
   * changes such as removal of temporary volumes and modifications of
   * system parameter
   */

  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					NULL, NULL, TRAN_LOCK_INFINITE_WAIT,
					TRAN_DEFAULT_ISOLATION_LEVEL ());
  if (tran_index == NULL_TRAN_INDEX)
    {
      fileio_dismount_all (thread_p);
      error_code = ER_FAILED;
      goto error;
    }

  /*
   * Remove any database temporary volumes
   */

  (void) boot_remove_all_temp_volumes (thread_p);

  (void) disk_goodvol_refresh (thread_p, boot_Db_parm->nvols);

  /* Set any warnings about space by purpose
   * dk_warnspace_by_purpose(DISK_UNKNOWN_PURPOSE);
   */

  pgbuf_refresh_max_permanent_volume_id (boot_Db_parm->last_volid);
  /* Reinitialize all permanent volumes for temporary purposes */
  error_code = disk_reinit_all_tmp (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  (void) disk_goodvol_refresh (thread_p, boot_Db_parm->nvols);

  /* If there is an existing query area, delete it. */
  if (boot_Db_parm->query_vfid.volid != NULL_VOLID)
    {
      (void) file_destroy (thread_p, &boot_Db_parm->query_vfid);
      boot_Db_parm->query_vfid.fileid = NULL_FILEID;
      boot_Db_parm->query_vfid.volid = NULL_VOLID;

      recdes.area_size = recdes.length = DB_SIZEOF (*boot_Db_parm);
      recdes.data = (char *) boot_Db_parm;

      if (heap_update (thread_p, (const HFID *) &boot_Db_parm->hfid,
		       (const OID *) &boot_Db_parm->rootclass_oid,
		       (const OID *) boot_Db_parm_oid, &recdes, NULL,
		       &old_object, NULL,
		       HEAP_UPDATE_IN_PLACE) != boot_Db_parm_oid
	  || xtran_server_commit (thread_p, false) != TRAN_UNACTIVE_COMMITTED)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  if (boot_Lob_path[0] != '\0')
    {
      error_code = es_init (boot_Lob_path);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_ES_NO_LOB_PATH, 0);
    }

  if (tran_index != NULL_TRAN_INDEX)
    {
      logtb_release_tran_index (thread_p, tran_index);
    }

  logtb_set_to_system_tran_index (thread_p);

  if (!tf_Metaclass_class.n_variable)
    {
      tf_compile_meta_classes ();
    }

  if (skip_to_check_ct_classes_for_rebuild == false)
    {
      if (catcls_Enable != true)
	{
	  error_code = catcls_compile_catalog_classes (thread_p);
	  if (error_code != NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_BO_MISSING_OR_INVALID_CATALOG, 0);
	      goto error;
	    }
	}
    }

  /* check server collations with database collations */
  if (check_db_coll)
    {
      LANG_COLL_COMPAT *db_collations = NULL;
      int db_coll_cnt;

      error_code = catcls_get_db_collation (thread_p, &db_collations,
					    &db_coll_cnt);
      if (error_code != NO_ERROR)
	{
	  if (db_collations != NULL)
	    {
	      db_private_free (thread_p, db_collations);
	    }
	  goto error;
	}

      if (db_collations != NULL)
	{
	  error_code = lang_check_coll_compat (db_collations, db_coll_cnt,
					       "server", "database");
	  db_private_free (thread_p, db_collations);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  error_code = jsp_start_server (db_name, db->pathname);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* read only mode ? */
  if (prm_get_bool_value (PRM_ID_READ_ONLY_MODE))
    {
      logtb_disable_update (NULL);
    }

  error_code = serial_initialize_cache_pool (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  error_code = session_states_init (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

#if defined (SERVER_MODE)
  if (prm_get_bool_value (PRM_ID_ACCESS_IP_CONTROL) == true
      && from_backup == false)
    {
      error_code = css_set_accessible_ip_info ();
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
#endif

#if defined (SERVER_MODE)
  /* set number of hosts */
  css_set_ha_num_of_hosts (db->num_hosts);
  /* set server's starting mode for HA according to the 'ha_mode' parameter */
  css_change_ha_server_state (thread_p,
			      prm_get_integer_value (PRM_ID_HA_SERVER_STATE),
			      false, HA_CHANGE_MODE_IMMEDIATELY, true);
#endif

  /* initialize partitions cache */
  error_code = partition_cache_init (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  cfg_free_directory (dir);

  if (print_restart)
    {
#if defined (NDEBUG)
      strncpy (format, msgcat_message (MSGCAT_CATALOG_CUBRID,
				       MSGCAT_SET_GENERAL,
				       MSGCAT_GENERAL_DATABASE_INIT),
	       BOOT_FORMAT_MAX_LENGTH);
      fprintf (stdout, format, rel_name ());
#else /* NDEBUG */
      fprintf (stdout, "\n%s (%s) (%d debug build)\n\n", rel_name (),
	       rel_build_number (), rel_bit_platform ());
#endif /* !NDEBUG */
    }

  /* server status could be changed by css_change_ha_server_state */
  if (boot_Server_status == BOOT_SERVER_DOWN)
    {
      /* server is up! */
      boot_server_status (BOOT_SERVER_UP);
    }

  return NO_ERROR;

error:
  cfg_free_directory (dir);
  prev_err_msg = (char *) er_msg ();
  if (prev_err_msg != NULL)
    {
      prev_err_msg = strdup (prev_err_msg);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_UNABLE_TO_RESTART_SERVER, 1, prev_err_msg);
      free_and_init (prev_err_msg);
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_UNABLE_TO_RESTART_SERVER, 1, "");
    }
  return error_code;
}

/*
 * xboot_restart_from_backup () - restore the database system from backup.
 *                             used for media crash recovery
 *
 * return : Transaction index
 *
 *   print_restart(in): Flag which indicates if the version of CUBRID server
 *                      is printed at the end of the restart process.
 *   db_name(in): Database Name
 *   r_args(in): restart argument structure contains various options
 *
 * Note: The database is restored from its backup and from all archive
 *       and active logs with information recoded since the backup. If
 *       these files are not actually placed in the location of the log
 *       files, the system will request those files.
 */
int
xboot_restart_from_backup (THREAD_ENTRY * thread_p, int print_restart,
			   const char *db_name, BO_RESTART_ARG * r_args)
{
  if (lang_init () != NO_ERROR)
    {
      if (er_errid () == NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LOC_INIT, 1,
		  "Failed to initialize language module");
	}
      return NULL_TRAN_INDEX;
    }

  /* open the system message catalog, before prm_ ?  */
  if (msgcat_init () != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG, 0);
      return NULL_TRAN_INDEX;
    }

  /* initialize system parameters */
  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    {
      return NULL_TRAN_INDEX;
    }

  prm_set_bool_value (PRM_ID_DBFILES_PROTECT, false);

  /*
   *  We need to do some initialization that normally happens in
   *  boot_restart_server(), but only if the SERVER_MODE CPP variable is
   *  defined.  Unfortunately, if we're here, then SERVER_MODE is not defined
   *  so call the important initializers ourselves.
   *
   *  Initialize allocations areas for things we need
   *  Initialize the type/doain module (also sets up an area)
   */

  area_init (false);

  tp_init ();

  if (boot_restart_server (thread_p, print_restart, db_name, true, true,
			   r_args) != NO_ERROR)
    {
      return NULL_TRAN_INDEX;
    }
  else
    {
      return LOG_FIND_THREAD_TRAN_INDEX (thread_p);
    }
}

/*
 * xboot_shutdown_server () - shutdown CUBRID server
 *
 * return : true
 *
 *   is_er_final(in): Terminate the error module..
 *
 * Note: All active transactions of all clients are aborted and the
 *       CUBRID server is terminated. Any database temporarily volume
 *       is destroyed.
 */
bool
xboot_shutdown_server (THREAD_ENTRY * thread_p, bool is_er_final)
{
  if (BO_IS_SERVER_RESTARTED ())
    {
#if defined(CUBRID_DEBUG)
      boot_check_db_at_num_shutdowns (true);
#endif /* CUBRID_DEBUG */

      sysprm_set_force (prm_get_name (PRM_ID_SUPPRESS_FSYNC), "0");
      /* Shutdown the system with the system transaction */
      logtb_set_to_system_tran_index (thread_p);
      log_abort_all_active_transaction (thread_p);
#if defined(SERVER_MODE)
      thread_stop_active_daemons ();
#endif

      /* before removing temp vols */
      qfile_finalize_list_cache (thread_p);
      (void) qexec_finalize_xasl_cache (thread_p);
      (void) qexec_finalize_filter_pred_cache (thread_p);
      session_states_finalize (thread_p);

      if (mvcc_Enabled)
	{
	  vacuum_finalize (thread_p);
	}

      (void) boot_remove_all_temp_volumes (thread_p);
      log_final (thread_p);

      if (is_er_final)
	{
	  boot_server_all_finalize (thread_p, is_er_final);
	}
      else
	{
	  er_stack_push ();
	  boot_server_all_finalize (thread_p, is_er_final);
	  er_stack_pop ();
	}
    }
  return true;
}

/*
 * xboot_get_server_session_key ()
 */
const char *
xboot_get_server_session_key (void)
{
  return boot_Server_session_key;
}

/*
 * xboot_register_client () - register a client
 *
 * return : transaction index or NULL_TRAN_INDEX in the case of error.
 *
 *   client_credential(in): Client's credential (see boot.h)
 *   client_lock_wait(in): Wait for at least this number of milliseconds to acquire
 *                         a lock. Negative value is infinite
 *   client_isolation(in): Isolation level. One of the following:
 *                         TRAN_REPEATABLE_READ
 *                         TRAN_READ_COMMITTED
 *                         TRAN_SERIALIZABLE
 *   tran_state(out): State of transaction
 *   server_credential(out): Server's credential (see boot.h)
 *
 * Note: If the CUBRID server is not already restarted, it is
 *       restarted. The calling client is registered and a transaction
 *       index is assigned to the client. If the last transaction
 *       executed on behalf of the client has client loose ends
 *       recovery tasks, such transaction is assigned to the client and
 *       and the transaction state is returned as a side effect.
 */
int
xboot_register_client (THREAD_ENTRY * thread_p,
		       BOOT_CLIENT_CREDENTIAL * client_credential,
		       int client_lock_wait, TRAN_ISOLATION client_isolation,
		       TRAN_STATE * tran_state,
		       BOOT_SERVER_CREDENTIAL * server_credential)
{
  int tran_index;
  bool check_db_coll = true;
  char *db_user_save;
  char *adm_prg_file_name = NULL;
  char db_user_upper[DB_MAX_IDENTIFIER_LENGTH] = { '\0' };

#if defined(SA_MODE)
  if (client_credential != NULL
      && client_credential->program_name != NULL
      && client_credential->client_type == BOOT_CLIENT_ADMIN_UTILITY)
    {
      adm_prg_file_name = client_credential->program_name
	+ strlen (client_credential->program_name) - 1;
      while (adm_prg_file_name > client_credential->program_name
	     && *adm_prg_file_name != PATH_SEPARATOR)
	{
	  adm_prg_file_name--;
	}

      if (*adm_prg_file_name == PATH_SEPARATOR)
	{
	  adm_prg_file_name++;
	}
    }
  if (adm_prg_file_name != NULL
      && (strncasecmp (adm_prg_file_name, "synccolldb",
		       strlen ("synccolldb")) == 0
	  || strncasecmp (adm_prg_file_name, "migrate_",
			  strlen ("migrate_")) == 0))
    {
      check_db_coll = false;
    }
  /* If the server is not restarted, restart the server at this moment */
  if (!BO_IS_SERVER_RESTARTED ()
      && boot_restart_server (thread_p, false, client_credential->db_name,
			      false, check_db_coll, NULL) != NO_ERROR)
    {
      *tran_state = TRAN_UNACTIVE_UNKNOWN;
      return NULL_TRAN_INDEX;
    }
#else /* SA_MODE */
  /* If the server is not restarted, returns an error */
  if (!BO_IS_SERVER_RESTARTED ())
    {
      *tran_state = TRAN_UNACTIVE_UNKNOWN;
      return NULL_TRAN_INDEX;
    }
#endif /* SA_MODE */

  /* Initialize scan function pointers of show statements */
  showstmt_scan_init ();

  db_user_save = client_credential->db_user;
  if (client_credential->db_user != NULL)
    {
      intl_identifier_upper (client_credential->db_user, db_user_upper);
      client_credential->db_user = db_user_upper;
    }

  /* Assign a transaction index to the client */
  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					client_credential, tran_state,
					client_lock_wait, client_isolation);
#if defined (SERVER_MODE)
  if (thread_p->conn_entry->status != CONN_OPEN)
    {
      /* the connection is going down. stop it */
      logtb_release_tran_index (thread_p, tran_index);
      tran_index = NULL_TRAN_INDEX;
    }
#endif /* SERVER_MODE */

  if (tran_index != NULL_TRAN_INDEX)
    {
#if defined (SERVER_MODE)
      thread_p->conn_entry->transaction_id = tran_index;
#endif /* SERVER_MODE */
      server_credential->db_full_name = boot_Db_full_name;
      server_credential->host_name = boot_Host_name;
      server_credential->lob_path = boot_Lob_path;
      server_credential->process_id = getpid ();
      COPY_OID (&server_credential->root_class_oid,
		&boot_Db_parm->rootclass_oid);
      HFID_COPY (&server_credential->root_class_hfid,
		 &boot_Db_parm->rootclass_hfid);
      server_credential->page_size = IO_PAGESIZE;
      server_credential->log_page_size = LOG_PAGESIZE;
      server_credential->disk_compatibility = rel_disk_compatible ();
#if defined (SERVER_MODE)
      server_credential->ha_server_state = css_ha_server_state ();
#else
      server_credential->ha_server_state =
	prm_get_integer_value (PRM_ID_HA_SERVER_STATE);
#endif
      memcpy (server_credential->server_session_key, boot_Server_session_key,
	      SERVER_SESSION_KEY_SIZE);
      server_credential->db_charset = lang_charset ();
      server_credential->db_lang = (char *) lang_get_Lang_name ();

#if defined(SERVER_MODE)
      /* Check the server's state for HA action for this client */
      if (BOOT_NORMAL_CLIENT_TYPE (client_credential->client_type))
	{
	  if (css_check_ha_server_state_for_client (thread_p, 1) != NO_ERROR)
	    {
	      logtb_release_tran_index (thread_p, tran_index);
	      er_log_debug (ARG_FILE_LINE, "xboot_register_client: "
			    "css_check_ha_server_state_for_client() error\n");
	      *tran_state = TRAN_UNACTIVE_UNKNOWN;
	      client_credential->db_user = db_user_save;
	      return NULL_TRAN_INDEX;
	    }
	}
      if (client_credential->client_type == BOOT_CLIENT_LOG_APPLIER)
	{
	  css_notify_ha_log_applier_state (thread_p,
					   HA_LOG_APPLIER_STATE_UNREGISTERED);
	}
#endif /* SERVER_MODE */

      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE,
	      ER_BO_CLIENT_CONNECTED, 4, client_credential->program_name,
	      client_credential->process_id, client_credential->host_name,
	      tran_index);
    }

#if defined(ENABLE_SYSTEMTAP) && defined(SERVER_MODE)
  CUBRID_CONN_START (thread_p->conn_entry->client_id,
		     client_credential->db_user);
#endif /* ENABLE_SYSTEMTAP */

  client_credential->db_user = db_user_save;
  return tran_index;
}

/*
 * xboot_unregister_client () - unregister a client
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   tran_index(in): Client transaction index
 *
 * Note: A client is unregistered. Any active transactions on that
 *       client are aborted. This function is called when a client is
 *       disconnected or when a crash of the client is detected by the
 *       CUBRID server, to release resources acquired such as locks,
 *       and allocated memory, on behalf of the client.
 */
int
xboot_unregister_client (THREAD_ENTRY * thread_p, int tran_index)
{
  int save_index;
  LOG_TDES *tdes;
#if defined(SERVER_MODE)
  int client_id;
  CSS_CONN_ENTRY *conn;
#endif /* SERVER_MODE */

  if (BO_IS_SERVER_RESTARTED () && tran_index != NULL_TRAN_INDEX)
    {
      save_index = LOG_FIND_THREAD_TRAN_INDEX (thread_p);

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, tran_index);
      tdes = LOG_FIND_TDES (tran_index);
#if defined(SERVER_MODE)
      conn = thread_p->conn_entry;
      client_id = (conn != NULL) ? conn->client_id : -1;
      if (tdes == NULL || tdes->client_id != client_id)
	{
	  thread_p->tran_index = save_index;

#if defined(ENABLE_SYSTEMTAP)
	  CUBRID_CONN_END (-1, NULL);
#endif /* ENABLE_SYSTEMTAP */

	  return NO_ERROR;
	}

      /* check if the client was a log applier */
      if (tdes->client.client_type == BOOT_CLIENT_LOG_APPLIER)
	{
	  css_notify_ha_log_applier_state (thread_p,
					   HA_LOG_APPLIER_STATE_UNREGISTERED);
	}
      /* Check the server's state for HA action for this client */
      if (BOOT_NORMAL_CLIENT_TYPE (tdes->client.client_type))
	{
	  if (css_check_ha_server_state_for_client (thread_p, 2) != NO_ERROR)
	    {
	      er_log_debug (ARG_FILE_LINE, "xboot_unregister_client: "
			    "css_check_ha_server_state_for_client() error\n");
	    }
	}
#else
      if (tdes == NULL)
	{

#if defined(ENABLE_SYSTEMTAP)
	  CUBRID_CONN_END (-1, NULL);
#endif /* ENABLE_SYSTEMTAP */

	  return NO_ERROR;
	}
#endif /* SERVER_MODE */

      /* If the transaction is active abort it */
      if (LOG_ISTRAN_ACTIVE (tdes))	/*logtb_is_current_active (thread_p) */
	{
	  (void) xtran_server_abort (thread_p);
	}

      xmnt_server_stop_stats (thread_p);

      /* Release the transaction index */
      logtb_release_tran_index (thread_p, tran_index);

      LOG_SET_CURRENT_TRAN_INDEX (thread_p, save_index);
    }

#if defined(CUBRID_DEBUG)
  boot_check_db_at_num_shutdowns (false);
#endif /* CUBRID_DEBUG */

#if defined(SA_MODE)
  (void) xboot_shutdown_server (NULL, true);
#endif /* SA_MODE */

#if defined(ENABLE_SYSTEMTAP) && defined(SERVER_MODE)
  CUBRID_CONN_END (client_id, tdes->client.db_user);
#endif /* ENABLE_SYSTEMTAP */

  return NO_ERROR;
}

#if defined(SERVER_MODE)
/*
 * xboot_notify_unregister_client () -
 *
 * return :
 *
 *   tran_index(in) :
 *
 * Note:
 */
void
xboot_notify_unregister_client (THREAD_ENTRY * thread_p, int tran_index)
{
  CSS_CONN_ENTRY *conn;
  LOG_TDES *tdes;
  int client_id;

  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
      if (thread_p == NULL)
	{
	  return;
	}
    }

  conn = thread_p->conn_entry;

#if defined(SERVER_MODE)
  assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
  assert (conn->csect.name == css_Csect_name_conn);
#endif

  csect_enter_critical_section (thread_p, &conn->csect, INF_WAIT);

  client_id = conn->client_id;
  tdes = LOG_FIND_TDES (tran_index);
  if (tdes != NULL && tdes->client_id == client_id)
    {
      if (conn->status == CONN_OPEN)
	{
	  conn->status = CONN_CLOSING;
	}
    }

#if defined(SERVER_MODE)
  assert (conn->csect.cs_index == CRITICAL_SECTION_COUNT + conn->idx);
  assert (conn->csect.name == css_Csect_name_conn);
#endif

  csect_exit_critical_section (thread_p, &conn->csect);
}
#endif /* SERVER_MODE */

#if defined(CUBRID_DEBUG)
/*
 * boot_check_db_at_num_shutdowns () - run checkdb when a number of client
 *                                       shutdowns has been executed.
 *                                       This is a debugging function.
 *
 *   force_nshutdowns(in): true if only forcing is desired.
 *
 * Note: Run checkdb when a number of client shutdowns has been
 *       executed (at the same or across several server sessions) and
 *       the env variable CUBRID_CHECKDB_ATNUM_SHUTDOWNS has a value
 *       greater than zero at the server process. The environment value
 *       indicates when to execute the checkdb.
 *
 * Side-effect: A file with the name ".checkdb_num_shutdowns" is created to
 *              to save the number of client shutdowns. This file is removed
 *              once the above env variable is not set.
 *              Note this file is not associated with a particular database.
 *              Therefore, the file could be modified (and removed) by several
 *              servers (databases). This will work OK, for most practical
 *              purposes. No a good reason, why it was implemented this way.
 *              The author was lazy and it was for debugging only.
 */
static void
boot_check_db_at_num_shutdowns (bool force_nshutdowns)
{
  const char *env_value;
  const char *checkdb_file_num_shutdowns = ".checkdb_num_shutdowns";
  FILE *fp;
  static int checkdb_every_nshutdowns = -1;
  static int num_current_shutdowns = -1;

  if (force_nshutdowns == true)
    {
      if (checkdb_every_nshutdowns > 0
	  && (fp = fopen (checkdb_file_num_shutdowns, "w")) != NULL)
	{
	  fprintf (fp, "%d", num_current_shutdowns);
	  fclose (fp);
	}
      return;
    }

  /*
   * Check the consistency of the database when the client is unregister
   */

  if (checkdb_every_nshutdowns == -1)
    {
      env_value = envvar_get ("CHECKDB_EVERY_NSHUTDOWNS");
      if (env_value != NULL)
	{
	  checkdb_every_nshutdowns = atoi (env_value);
	}
      else
	{
	  checkdb_every_nshutdowns = 0;
	}

      if (checkdb_every_nshutdowns <= 0)
	{
	  checkdb_every_nshutdowns = 0;	/* Don't check at all */
	  (void) remove (checkdb_file_num_shutdowns);
	  return;
	}

      fp = fopen (checkdb_file_num_shutdowns, "r");
      if (fp != NULL)
	{
	  if (fscanf (fp, "%d", &num_current_shutdowns) != 1)
	    {
	      num_current_shutdowns = 0;
	    }
	}
      else
	{
	  num_current_shutdowns = 0;
	}
    }

  if (checkdb_every_nshutdowns > 0)
    {
      num_current_shutdowns++;

      if (num_current_shutdowns == checkdb_every_nshutdowns)
	{
	  num_current_shutdowns = 0;
	  if (xboot_check_db_consistency () != NO_ERROR)
	    {
	      const char *tmpname;
	      fflush (stderr);
	      fflush (stdout);
	      tmpname = er_get_msglog_filename ();
	      if (tmpname == NULL)
		{
		  tmpname = "/dev/null";
		}
	      fprintf (stdout, "Some inconsistencies were detected in your"
		       " database.\n Please consult error_log file = %s"
		       " for additional information\n", tmpname);
	      fflush (stdout);
	      /*
	       * The following is added so we can attach to the debugger on
	       * a fatal error. It is of great help to stop execution when
	       * running a set of sql scripts. (That is, find the script that
	       * leave the DB inconsistent).
	       */
	      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_GENERIC_ERROR, 0);
	    }
	}
    }
}
#endif /* CUBRID_DEBUG */

enum
{
  CHECK_ONLY = 0,
  REPAIR_ALL = 1
};

/*
 * xboot_checkdb_table () - check consistency of table
 *                              as much as possible
 *
 * return :
 *
 */
DISK_ISVALID
xboot_checkdb_table (THREAD_ENTRY * thread_p, int check_flag, OID * oid,
		     BTID * index_btid)
{
  HFID hfid;
  bool repair = check_flag & CHECKDB_REPAIR;
  DISK_ISVALID allvalid, valid;

  allvalid = DISK_VALID;

  if (check_flag & CHECKDB_CHECK_PREV_LINK)
    {
      valid = btree_repair_prev_link (thread_p, oid, index_btid, CHECK_ONLY);
      if (valid == DISK_ERROR)
	{
	  return DISK_ERROR;
	}
      if (valid != DISK_VALID)
	{
	  allvalid = valid;
	}
    }

  if (check_flag & CHECKDB_REPAIR_PREV_LINK)
    {
      valid = btree_repair_prev_link (thread_p, oid, index_btid, REPAIR_ALL);
      if (valid == DISK_ERROR)
	{
	  return DISK_ERROR;
	}
      if (valid != DISK_VALID)
	{
	  allvalid = valid;
	}
    }

  if (heap_get_hfid_from_class_oid (thread_p, oid, &hfid) != NO_ERROR
      || HFID_IS_NULL (&hfid))
    {
      return DISK_ERROR;
    }

  if (index_btid == NULL)
    {
      /* if index name was specified, skip checking heap file */
      valid = heap_check_heap_file (thread_p, &hfid);
      if (valid == DISK_ERROR)
	{
	  return DISK_ERROR;
	}
      if (valid != DISK_VALID)
	{
	  allvalid = valid;
	}
    }

  valid = btree_check_by_class_oid (thread_p, oid, index_btid);
  if (valid == DISK_ERROR)
    {
      return DISK_ERROR;
    }
  if (valid != DISK_VALID)
    {
      allvalid = valid;
    }

  valid = locator_check_by_class_oid (thread_p, oid, &hfid,
				      index_btid, repair);
  if (valid == DISK_ERROR)
    {
      return DISK_ERROR;
    }
  if (valid != DISK_VALID)
    {
      allvalid = valid;
    }

  return allvalid;
}

/*
 * xcallback_console_print -
 *
 * return:
 *
 *   print_str(in):
 */
#if defined (SA_MODE)
int
xcallback_console_print (THREAD_ENTRY * thread_p, char *print_str)
{
  fprintf (stdout, print_str);

  return NO_ERROR;
}
#endif

/*
 * xboot_check_db_consistency () - check consistency of database
 *                              as much as possible
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 */
int
xboot_check_db_consistency (THREAD_ENTRY * thread_p, int check_flag,
			    OID * oids, int num_oids, BTID * index_btid)
{
  DISK_ISVALID isvalid = DISK_VALID;
  VOLID volid;
  int nperm_vols, i;
  bool repair = check_flag & CHECKDB_REPAIR;
  int error_code = NO_ERROR;

  error_code = boot_check_permanent_volumes (thread_p);
  nperm_vols = xboot_find_number_permanent_volumes (thread_p);

  if (index_btid != NULL && BTID_IS_NULL (index_btid))
    {
      index_btid = NULL;
    }

  for (volid = 0; volid < nperm_vols && isvalid != DISK_ERROR; volid++)
    {
      isvalid = disk_check (thread_p, volid, repair);
      if (isvalid != DISK_VALID)
	{
	  error_code = ER_FAILED;
	}
    }

  if (num_oids > 0)
    {
      for (i = 0; i < num_oids; i++)
	{
	  if (OID_ISNULL (&oids[i]))
	    {
	      continue;
	    }
	  isvalid =
	    xboot_checkdb_table (thread_p, check_flag, &oids[i], index_btid);

	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
      return error_code;
    }

  if (check_flag & CHECKDB_CHECK_PREV_LINK)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid = btree_repair_prev_link (thread_p, NULL, NULL, CHECK_ONLY);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_REPAIR_PREV_LINK)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid = btree_repair_prev_link (thread_p, NULL, NULL, REPAIR_ALL);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_FILE_TRACKER_CHECK)
    {
      if (isvalid != DISK_ERROR)
	{
#if defined (SERVER_MODE)
	  isvalid = file_tracker_check (thread_p);
#else
	  isvalid = file_tracker_cross_check_with_disk_idsmap (thread_p);
#endif
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_HEAP_CHECK_ALLHEAPS)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid = heap_check_all_heaps (thread_p);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_CT_CHECK_CAT_CONSISTENCY)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid = catalog_check_consistency (thread_p);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_BTREE_CHECK_ALL_BTREES)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid = btree_check_all (thread_p);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_LC_CHECK_CLASSNAMES)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid = locator_check_class_names (thread_p);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  if (check_flag & CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES)
    {
      if (isvalid != DISK_ERROR)
	{
	  isvalid =
	    locator_check_all_entries_of_all_btrees (thread_p, repair);
	  if (isvalid != DISK_VALID)
	    {
	      error_code = ER_FAILED;
	    }
	}
    }

  return error_code;
}

/*
 * boot_server_all_finalize () - terminate every single module
 * 				 except the log/recovery manager
 *   is_er_final(in): Terminate the error module..
 *
 * Note: Every single module except the log/recovery manager are
 *       uninitialized. All data volumes are unmounted.
 */
void
boot_server_all_finalize (THREAD_ENTRY * thread_p, bool is_er_final)
{
  locator_finalize (thread_p);
  spage_finalize (thread_p);
  catalog_finalize ();
  qmgr_finalize (thread_p);
  (void) heap_manager_finalize ();
  mnt_server_final ();
  fileio_dismount_all (thread_p);
  disk_goodvol_decache (thread_p);
  boot_server_status (BOOT_SERVER_DOWN);

  catcls_finalize_class_oid_to_oid_hash_table ();
  serial_finalize_cache_pool ();
  partition_cache_finalize (thread_p);
#if defined(SERVER_MODE)
#if defined(DIAG_DEVEL)
  close_diag_mgr ();
#endif /* DIAG_DEVEL */
  es_final ();
  tp_final ();
  locator_free_areas ();
  set_final ();
  sysprm_final ();
  area_final ();
  msgcat_final ();
  if (is_er_final == true)
    {
      er_final (1);
    }
  lang_final ();
  css_free_accessible_ip_info ();
  event_log_final ();
#endif /* SERVER_MODE */
}

/*
 * xboot_backup () - a fuzzy backup of the database
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   backup_path(in): Location where information volumes are
 *                    backed up. If NULL is given, the following
 *                    defaults are assumed to back up each
 *                    information volume:
 *                    - If file "fileof_vols_and_backup_paths" is
 *                      given, the path to backup each volume is
 *                      found in this file.
 *                    - All information volumes are backed up on
 *                      the same location where the log files are
 *                      located.
 *   backup_level(in): backup levels allowed: 0 - Full (default),
 *                     1 - Incremental1, 2 - Incremental
 *   deleted_unneeded_logarchives(in): Whetear to remove log archives that are
 *                                 not needed any longer to recovery from
 *                                 crashes when the backup just created is
 *                                 used.
 *   backup_verbose_file(in): verbose mode file path
 *                    num_threads: number of threads
 *                    zip_method: compression method
 *                    zip_level: compression level
 *   sleep_msecs(in):
 *
 * Note: A fuzzy backup of the database is taken. The backup is written
 *       into the given backup_path location. If the backup_path
 *       location is omitted (i.e, NULL is given), the log path
 *       location which was specified at database creation is used to
 *       store the backup.
 */
int
xboot_backup (THREAD_ENTRY * thread_p, const char *backup_path,
	      FILEIO_BACKUP_LEVEL backup_level,
	      bool delete_unneeded_logarchives,
	      const char *backup_verbose_file, int num_threads,
	      FILEIO_ZIP_METHOD zip_method, FILEIO_ZIP_LEVEL zip_level,
	      int skip_activelog, int sleep_msecs)
{
  int error_code;

  error_code = logpb_backup (thread_p, boot_Db_parm->nvols, backup_path,
			     backup_level, delete_unneeded_logarchives,
			     backup_verbose_file, num_threads, zip_method,
			     zip_level, skip_activelog, sleep_msecs);
  return error_code;
}

/*
 * xboot_copy () - copy the database to a new destination
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   fromdb_name(in): The database from where the copy is made.
 *   newdb_name(in): Name of new database
 *   newdb_path(in): Directory where the new database will reside
 *   newlog_path(in): Directory where the log volumes of the new database
 *                    will reside
 *   newdb_server_host(in): Server host where the new database reside
 *   new_volext_path(in): A path is included if all volumes are placed in one
 *                        place/directory. If NULL is given,
 *                        - If file "fileof_vols_and_wherepaths" is given, the
 *                          path is found in this file.
 *                        - Each volume is copied to same place where the
 *                          volume resides.
 *                      Note: This parameter should be NULL, if the above file
 *                            is given.
 *   fileof_vols_and_wherepaths(in): A file is given when the user decides to
 *                               control the copy/rename of the volume by
 *                               individual bases. That is, user decides to
 *                               spread the volumes over several locations and
 *                               or to label the volumes with specific names.
 *                               Each volume entry consists of:
 *                                 volid from_fullvolname to_fullvolname
 *   newdb_overwrite(in): Wheater to overwrite the new database if it already
 *                        exist.
 */
int
xboot_copy (THREAD_ENTRY * thread_p, const char *from_dbname,
	    const char *new_db_name, const char *new_db_path,
	    const char *new_log_path, const char *new_lob_path,
	    const char *new_db_server_host, const char *new_volext_path,
	    const char *fileof_vols_and_copypaths, bool new_db_overwrite)
{
  DB_INFO *dir = NULL;
  DB_INFO *db = NULL;
  const char *new_log_prefix;
  char new_db_fullname[PATH_MAX];
  char new_db_pathbuf[PATH_MAX];
  char new_db_pathbuf2[PATH_MAX];
  char new_log_pathbuf[PATH_MAX];
  char new_lob_pathbuf2[PATH_MAX];
  char new_lob_pathbuf[PATH_MAX];
  char new_volext_pathbuf[PATH_MAX];
  char fixed_pathbuf[PATH_MAX];
  char new_db_server_host_buf[MAXHOSTNAMELEN + 1];
  char dbtxt_label[PATH_MAX];
  int dbtxt_vdes = NULL_VOLDES;
  int error_code = NO_ERROR;
#if defined (WINDOWS)
  struct stat stat_buf;
#endif

  /* If db_path and/or log_path are NULL find the defaults */

  if (new_db_path == NULL || fileof_vols_and_copypaths != NULL)
    {
      /*
       * If a newdb path was given, it is ignored since only one option must
       * be specified
       */
      new_db_path = boot_find_new_db_path (new_db_pathbuf,
					   fileof_vols_and_copypaths);
      if (new_db_path == NULL)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
    }

  /*
   * Make sure that the db_path and log_path are the canonicalized absolute
   * pathnames
   */

  if (new_db_path == NULL)
    {
      new_db_path = "";
    }
  else if (realpath ((char *) new_db_path, new_db_pathbuf2) != NULL)
    {
      new_db_path = new_db_pathbuf2;
    }

  if (new_log_path != NULL
      && realpath ((char *) new_log_path, new_log_pathbuf) != NULL)
    {
      new_log_path = new_log_pathbuf;
    }

  if (new_log_path == NULL)
    {
      /* Assign the data volume directory */
      strcpy (new_log_pathbuf, new_db_path);
      new_log_path = new_log_pathbuf;
    }

  if (new_lob_path == NULL)
    {
      assert_release (new_db_path != NULL);
      snprintf (new_lob_pathbuf2, sizeof (new_lob_pathbuf2), "%s%s/lob",
		LOB_PATH_DEFAULT_PREFIX, new_db_path);
      new_lob_path = new_lob_pathbuf2;
    }

  if (new_lob_path != NULL)
    {
      ES_TYPE es_type = es_get_type (new_lob_path);
      char *p = NULL;

      switch (es_type)
	{
	case ES_NONE:
	  /* prepend default prefix */
	  snprintf (new_lob_pathbuf, sizeof (new_lob_pathbuf), "%s%s",
		    LOB_PATH_DEFAULT_PREFIX, new_lob_path);
	  new_lob_path = new_lob_pathbuf;
	  es_type = ES_POSIX;
	  p = strchr (new_lob_path, ':') + 1;
	  break;
	case ES_POSIX:
	  p = strchr (strcpy (new_lob_pathbuf, new_lob_path), ':') + 1;
	  break;
	case ES_OWFS:
#if !defined (CUBRID_OWFS)
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_ES_INVALID_PATH, 1, new_lob_path);
	  error_code = ER_ES_INVALID_PATH;
	  goto error;
#endif /* !CUBRID_OWFS */
	case ES_LOCAL:
	default:
	  break;
	}

      if (es_type == ES_POSIX && p != NULL)
	{
#if defined (WINDOWS)
	  if (realpath (p, fixed_pathbuf) != NULL
	      && (stat (fixed_pathbuf, &stat_buf) == 0
		  && S_ISDIR (stat_buf.st_mode)))
#else
	  if (realpath (p, fixed_pathbuf) != NULL)
#endif
	    {
	      strcpy (p, fixed_pathbuf);
	    }
	  else
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_BO_DIRECTORY_DOESNOT_EXIST, 1, p);
	      if (mkdir (p, 0777) < 0)
		{
		  cub_dirname_r (p, fixed_pathbuf, PATH_MAX);
		  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				       ER_ES_GENERAL, 2, "POSIX",
				       fixed_pathbuf);
		  error_code = ER_BO_DIRECTORY_DOESNOT_EXIST;
		  goto error;
		}
	    }
	}
    }

  if (new_volext_path != NULL
      && realpath ((char *) new_volext_path, new_volext_pathbuf) != NULL)
    {
      new_volext_path = new_volext_pathbuf;
    }

  /* If a host was not given, assume the current host */

  if (new_db_server_host == NULL)
    {
#if 0				/* use Unix-domain socket for localhost */
      if (GETHOSTNAME (new_db_server_host_buf, MAXHOSTNAMELEN) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_BO_UNABLE_TO_FIND_HOSTNAME, 0);
	  error_code = ER_BO_UNABLE_TO_FIND_HOSTNAME;
	  goto error;
	}
#else
      strcpy (new_db_server_host_buf, "localhost");
#endif
      new_db_server_host = new_db_server_host_buf;
    }

  /* Make sure that the full path for the new database is not too long */
  if ((int) (strlen (new_db_name) + strlen (new_db_path) + 2)
      > DB_MAX_PATH_LENGTH)
    {
      /*
       * db_path + db_name is too long
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, new_db_path,
	      new_db_name, strlen (new_db_name) + strlen (new_db_path) + 2,
	      DB_MAX_PATH_LENGTH);
      error_code = ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG;
      goto error;
    }

  /* Get the log prefix */
  new_log_prefix = fileio_get_base_file_name (new_db_name);

  /*
   * get the database directory information in write mode
   */
  if (cfg_maycreate_get_directory_filename (dbtxt_label) == NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
/* Temporary fix for NT file locking problem */
      || (dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
				     LOG_DBTXT_VOLID, 2, true)) == NULL_VOLDES
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
    )
    {
      error_code = ER_FAILED;
      goto error;
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      error_code = cfg_read_directory_ex (dbtxt_vdes, &dir, true);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      error_code = cfg_read_directory (&dir, true);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  if (dir != NULL && ((db = cfg_find_db_list (dir, new_db_name)) != NULL))
    {
      if (new_db_overwrite == false)
	{
	  /* There is a database with the same name and we cannot overwrite it */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DATABASE_EXISTS, 1,
		  new_db_name);
	  {
	    error_code = ER_BO_DATABASE_EXISTS;
	    goto error;
	  }
	}
      else
	{
	  /*
	   * Delete the database.. to make sure that all backups, log archives, and
	   * so on are removed... then continue...
	   * Note: we do not call xboot_delete since it reverts a bunch of stuff.
	   */
	  cfg_free_directory (dir);
	  dir = NULL;

	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      fileio_dismount (thread_p, dbtxt_vdes);
	      dbtxt_vdes = NULL_VOLDES;
	    }
	  (void) xboot_shutdown_server (thread_p, false);

	  error_code = xboot_delete (thread_p, new_db_name, true);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }

	  error_code = boot_restart_server (thread_p, false, from_dbname,
					    false, false, NULL);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }

	  error_code = xboot_copy (thread_p, from_dbname, new_db_name,
				   new_db_path, new_log_path, new_lob_path,
				   new_db_server_host, new_volext_path,
				   fileof_vols_and_copypaths, false);

	  return error_code;
	}
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);	/* unlock the directory file */
      dbtxt_vdes = NULL_VOLDES;
      cfg_free_directory (dir);
      dir = NULL;
    }

  /*
   * Compose the full name of the new database
   */

  COMPOSE_FULL_NAME (new_db_fullname, sizeof (new_db_fullname),
		     new_db_path, new_db_name);

  /*
   * Copy the database
   */

  error_code =
    logpb_copy_database (thread_p, boot_Db_parm->nvols, new_db_fullname,
			 new_log_path, new_log_prefix, new_volext_path,
			 fileof_vols_and_copypaths);
  if (error_code != NO_ERROR)
    {
      (void) xtran_server_abort (NULL);
    }
  else
    {
      /* Now create the entry in the database table */

      if (xtran_server_commit (thread_p, false) != TRAN_UNACTIVE_COMMITTED)
	{
	  error_code = ER_FAILED;
	  goto error;
	}

#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
      dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
				 LOG_DBTXT_VOLID, 2, true);
      if (dbtxt_vdes == NULL_VOLDES)
	{
	  error_code = ER_FAILED;
	  goto error;
	}
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

      if (dbtxt_vdes != NULL_VOLDES)
	{
	  error_code = cfg_read_directory_ex (dbtxt_vdes, &dir, true);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  error_code = cfg_read_directory (&dir, true);
	  if (error_code != NO_ERROR)
	    {
	      goto error;
	    }
	}
      db = cfg_find_db_list (dir, new_db_name);

      if (db == NULL)
	{
	  db = cfg_add_db (&dir, new_db_name, new_db_path, new_log_path,
			   new_lob_path, new_db_server_host);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DATABASE_EXISTS, 1,
		  new_db_name);
	  error_code = ER_BO_DATABASE_EXISTS;
	  goto error;
	}

      if (dbtxt_vdes != NULL_VOLDES)
	{
	  cfg_write_directory_ex (dbtxt_vdes, dir);
	}
      else
	{
	  cfg_write_directory (dir);
	}

#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
      if (dbtxt_vdes != NULL_VOLDES)
	{
	  fileio_dismount (thread_p, dbtxt_vdes);
	  dbtxt_vdes = NULL_VOLDES;
	}
#endif /* WINDOWS && !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
    }

  cfg_free_directory (dir);
  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
    }

  (void) xboot_shutdown_server (thread_p, false);
  return error_code;

error:
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
    }

  (void) xboot_shutdown_server (thread_p, false);

  return error_code;
}

/*
 * xboot_soft_rename () - a soft rename of a database on the same disk partitions
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   olddb_name(in): Name of new database
 *   newdb_name(in): Directory where the new database will reside
 *   newdb_path(in): Directory where the log volumes of the new database
 *                        will reside
 *   newlog_path(in): Server host where the new database reside
 *   newdb_server_host(in): Wheater to overwrite the new database if it already
 *                        exist.
 *   new_volext_path(in): A path is included if all volumes are placed in one
 *                        place/directory. If NULL is given,
 *                        - If file "fileof_vols_and_wherepaths" is given, the
 *                          path is found in this file.
 *                        - Each volume is copied to same place where the
 *                          volume resides.
 *                      Note: This parameter should be NULL, if the above file
 *                            is given.
 *   fileof_vols_and_renamepaths(in):A file is given when the user decides to
 *                               control the rename of the volume by
 *                               individual bases. That is, user decides to
 *                               spread the volumes over several locations and
 *                               or to label the volumes with specific names.
 *                               Each volume entry consists of:
 *                                 volid from_fullvolname to_fullvolname
 *   newdb_overwrite(in):Rename the volumes/files at OS too. If it is true,
 *                        the enw database cannot exist in database.txt
 *   extern_rename(in): Rename the volumes/files at OS too. If it is true,
 *                        the enw database cannot exist in database.txt
 *   force_delete(in): Force delete backup volumes and information file
 *
 */
int
xboot_soft_rename (THREAD_ENTRY * thread_p, const char *old_db_name,
		   const char *new_db_name, const char *new_db_path,
		   const char *new_log_path,
		   const char *new_db_server_host,
		   const char *new_volext_path,
		   const char *fileof_vols_and_renamepaths,
		   bool new_db_overwrite, bool extern_rename,
		   bool force_delete)
{
  DB_INFO *dir = NULL;
  DB_INFO *db = NULL;
  const char *newlog_prefix;
  char new_db_server_host_buf[MAXHOSTNAMELEN + 1];
  char new_db_fullname[PATH_MAX];
  char new_db_pathbuf[PATH_MAX];
  char new_log_pathbuf[PATH_MAX];
  int dbtxt_vdes = NULL_VOLDES;
  char dbtxt_label[PATH_MAX];
  char allocdb_path[PATH_MAX];
  char alloclog_path[PATH_MAX];
  int error_code = NO_ERROR;

  if (fileof_vols_and_renamepaths != NULL)
    {
      /*
       * If a newdb path was given, it is ignored since only one option must
       * be specified
       */
      new_db_path = boot_find_new_db_path (allocdb_path,
					   fileof_vols_and_renamepaths);
      if (new_db_path == NULL)
	{
	  error_code = ER_FAILED;
	  goto end;
	}
    }

  if (new_db_path == NULL)
    {
      /*
       * Use the same location as the source database
       */
      new_db_path = fileio_get_directory_path (allocdb_path,
					       boot_Db_full_name);
      if (new_db_path == NULL)
	{
	  if (getcwd (allocdb_path, PATH_MAX) == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_BO_CWD_FAIL, 0);
	      error_code = ER_BO_CWD_FAIL;
	      goto end;
	    }
	  new_db_path = allocdb_path;
	}
    }

  if (new_log_path == NULL)
    {
      /*
       * Use the same log location as the source database
       */
      new_log_path = fileio_get_directory_path (alloclog_path,
						log_Name_active);
      if (new_log_path == NULL)
	{
	  if (getcwd (alloclog_path, PATH_MAX) == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_BO_CWD_FAIL, 0);
	      error_code = ER_BO_CWD_FAIL;
	      goto end;
	    }
	  new_log_path = alloclog_path;
	}
    }

  /*
   * Make sure that the db_path and log_path are the canonicalized absolute
   * pathnames
   */

  if (realpath ((char *) new_db_path, new_db_pathbuf) != NULL)
    {
      new_db_path = new_db_pathbuf;
    }

  if (new_log_path != NULL
      && realpath ((char *) new_log_path, new_log_pathbuf) != NULL)
    {
      new_log_path = new_log_pathbuf;
    }

  /* If db_path and/or log_path are NULL find the defaults */

  if (new_log_path == NULL)
    {
      /* Assign the data volume directory */
      strncpy (new_log_pathbuf, new_db_path, PATH_MAX);
      new_log_path = new_log_pathbuf;
    }

  /* If a host was not given, assume the current host */

  if (new_db_server_host == NULL)
    {
#if 0				/* use Unix-domain socekt for localhost */
      if (GETHOSTNAME (new_db_server_host_buf, MAXHOSTNAMELEN) != 0)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_BO_UNABLE_TO_FIND_HOSTNAME, 0);
	  error_code = ER_BO_UNABLE_TO_FIND_HOSTNAME;
	  goto end;
	}
#else
      strcpy (new_db_server_host_buf, "localhost");
#endif
      new_db_server_host = new_db_server_host_buf;
    }

  /* Make sure that the full path for the new database is not too long */
  if ((int) (strlen (new_db_name) + strlen (new_db_path) + 2)
      > DB_MAX_PATH_LENGTH)
    {
      /*
       * db_path + db_name is too long
       */
      er_set (ER_FATAL_ERROR_SEVERITY, ARG_FILE_LINE,
	      ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG, 3, new_db_path,
	      new_db_name, strlen (new_db_name) + strlen (new_db_path) + 2,
	      DB_MAX_PATH_LENGTH);
      error_code = ER_BO_FULL_DATABASE_NAME_IS_TOO_LONG;
      goto end;
    }

  /* Get the log prefix */
  newlog_prefix = fileio_get_base_file_name (new_db_name);

  /*
   * get the database directory information in write mode
   */
  if (cfg_maycreate_get_directory_filename (dbtxt_label) == NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
/* Temporary fix for NT file locking problem */
      || (dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
				     LOG_DBTXT_VOLID, 2, true)) == NULL_VOLDES
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
    )
    {
      error_code = ER_FAILED;
      goto end;
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      error_code = cfg_read_directory_ex (dbtxt_vdes, &dir, true);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }
  else
    {
      error_code = cfg_read_directory (&dir, true);
      if (error_code != NO_ERROR)
	{
	  goto end;
	}
    }

  if (dir != NULL && (db = cfg_find_db_list (dir, new_db_name)) == NULL
      && extern_rename != true)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      new_db_name);
      error_code = ER_BO_UNKNOWN_DATABASE;
      goto end;
    }

  if (dir != NULL && db != NULL && extern_rename == true
      && new_db_overwrite == false)
    {
      /* There is a database with the same name and we cannot overwrite it */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_DATABASE_EXISTS, 1,
	      new_db_name);
      error_code = ER_BO_DATABASE_EXISTS;
      goto end;
    }

  /*
   * Compose the full name of the new database
   */

  COMPOSE_FULL_NAME (new_db_fullname, sizeof (new_db_fullname),
		     new_db_path, new_db_name);

  /*
   * Rename the database
   */

  error_code = logpb_rename_all_volumes_files (thread_p, boot_Db_parm->nvols,
					       new_db_fullname, new_log_path,
					       newlog_prefix, new_volext_path,
					       fileof_vols_and_renamepaths,
					       extern_rename, force_delete);
  if (error_code != NO_ERROR)
    {
      goto end;
    }

  /* Now create the entry in the database table */
  if (extern_rename == true)
    {
      if (db == NULL)
	{
	  const char *old_lob_path = NULL;
	  char new_lob_pathbuf[PATH_MAX] = { '\0' };
	  char *new_lob_path = NULL;

	  old_lob_path = boot_get_lob_path ();
	  if (*old_lob_path != '\0')
	    {
	      new_lob_path =
		strncpy (new_lob_pathbuf, old_lob_path, PATH_MAX);
	    }

	  cfg_delete_db (&dir, old_db_name);
	  db = cfg_add_db (&dir, new_db_name, new_db_path, new_log_path,
			   new_lob_path, new_db_server_host);
	}
      else
	{
	  cfg_update_db (db, new_db_path, new_log_path, NULL,
			 new_db_server_host);
	}
      if (db == NULL || db->name == NULL || db->pathname == NULL ||
	  db->logpath == NULL || db->hosts == NULL)
	{
	  error_code = ER_FAILED;
	  goto end;
	}
    }
#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  /* must unlock this before we can open it again for writing */
  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
      dbtxt_vdes = NULL_VOLDES;
    }
#endif /* WINDOWS && !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
  if (dbtxt_vdes != NULL_VOLDES)
    {
      cfg_write_directory_ex (dbtxt_vdes, dir);
    }
  else
    {
      cfg_write_directory (dir);
    }

end:
  if (dir != NULL)
    {
      cfg_free_directory (dir);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
    }

  return error_code;
}

/*
 * xboot_delete () - delete all log files and database backups
 *
 * return: NO_ERROR if all OK, ER_ status otherwise
 *
 *   db_name(in):
 *   force_delete(in):
 *
 * Note: All data, log, and backup files associated with the current
 *              database are removed from the system.
 *              This is a very dangerous operation since the database cannot
 *              be recovered after this operation is executed. We strongly
 *              recommend that you backup the database and put the backup on
 *              tape or outside the log and backup directories before this
 *              operation is done. After this operation is executed the system
 *              is unavailable, that is, the system is shutdown by this
 *              operation.
 *
 * Note: This function must be run offline, that is, it should not be
 *              run when there are multiusers in the system.
 */
int
xboot_delete (THREAD_ENTRY * thread_p, const char *db_name, bool force_delete)
{
  char log_path[PATH_MAX];
  const char *log_prefix = NULL;
  DB_INFO *db;
  DB_INFO *dir = NULL;
  int dbtxt_vdes = NULL_VOLDES;
  char dbtxt_label[PATH_MAX];
  int error_code = NO_ERROR;

  if (!BO_IS_SERVER_RESTARTED ())
    {
      /*
       * Compose the full name of the database and find location of logs
       */
      if (msgcat_init () != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG, 0);
	  return ER_FAILED;
	}

      if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_CANT_LOAD_SYSPRM, 0);
	  return ER_FAILED;
	}

      er_clear ();
    }

  /* Find the prefix for the database */
  log_prefix = fileio_get_base_file_name (db_name);

  /*
   * get the database directory information in write mode.
   */
  if (cfg_maycreate_get_directory_filename (dbtxt_label) == NULL)
    {
      goto error_dirty_delete;
    }

#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
  /* Temporary solution to NT file locking problem */
  dbtxt_vdes =
    fileio_mount (thread_p, dbtxt_label, dbtxt_label, LOG_DBTXT_VOLID, 2,
		  true);
  if (dbtxt_vdes == NULL_VOLDES)
    {
      return ER_FAILED;
    }
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

  if (dbtxt_vdes != NULL_VOLDES)
    {
      error_code = cfg_read_directory_ex (dbtxt_vdes, &dir, true);
    }
  else
    {
      error_code = cfg_read_directory (&dir, true);
    }

  if (error_code != NO_ERROR)
    {
      /*
       * If I cannot obtain a Lock on database.txt, it is better to quite at
       * this moment. We will not even perform a dirty delete.
       */
      if (dbtxt_vdes != NULL_VOLDES)
	{
	  fileio_dismount (thread_p, dbtxt_vdes);
	}
      return error_code;
    }

  if (dir == NULL || (db = cfg_find_db_list (dir, db_name)) == NULL)
    {
      if (dbtxt_vdes != NULL_VOLDES)
	{
	  fileio_dismount (thread_p, dbtxt_vdes);
	}
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      db_name);
      if (dir)
	{
	  cfg_free_directory (dir);
	}
      goto error_dirty_delete;
    }

  /*
   * How can we perform the delete operation..without restarting the system
   * or restarted the system.
   */

  if (!BO_IS_SERVER_RESTARTED ())
    {
      /*
       * Compose the full name of the database and find location of logs
       */
      COMPOSE_FULL_NAME (boot_Db_full_name, sizeof (boot_Db_full_name),
			 db->pathname, db_name);
    }

  if (boot_volume_info_log_path (log_path) == NULL)
    {
      strcpy (log_path, db->logpath);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
      dbtxt_vdes = NULL_VOLDES;
      cfg_free_directory (dir);
      dir = NULL;
    }

  /* Now delete the database */
  error_code = boot_remove_all_volumes (thread_p, boot_Db_full_name, log_path,
					log_prefix, false, force_delete);
  if (error_code == NO_ERROR)
    {
#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
      dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
				 LOG_DBTXT_VOLID, 2, true);
      if (dbtxt_vdes == NULL_VOLDES)
	{
	  goto error_dirty_delete;
	}
#endif /* WINDOWS && !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */

      if (dbtxt_vdes != NULL_VOLDES)
	{
	  if (cfg_read_directory_ex (dbtxt_vdes, &dir, true) != NO_ERROR)
	    {
	      goto error_dirty_delete;
	    }
	}
      else
	{
	  if (cfg_read_directory (&dir, true) != NO_ERROR)
	    {
	      goto error_dirty_delete;
	    }
	}

      db = cfg_find_db_list (dir, db_name);

      if (db && cfg_delete_db (&dir, db_name))
	{
#if defined(WINDOWS) && !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
	  /* must unlock it before opening it for write again */
	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      fileio_dismount (thread_p, dbtxt_vdes);
	      dbtxt_vdes = NULL_VOLDES;
	    }
#endif /* WINDOWS && !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      cfg_write_directory_ex (dbtxt_vdes, dir);
	    }
	  else
	    {
	      cfg_write_directory (dir);
	    }
	}
      cfg_free_directory (dir);
    }

  if (dbtxt_vdes != NULL_VOLDES)
    {
      fileio_dismount (thread_p, dbtxt_vdes);
    }

  /* Shutdown the server */
  if (error_code == NO_ERROR)
    {
      boot_server_all_finalize (thread_p, true);
    }
  else
    {
      er_stack_push ();
      boot_server_all_finalize (thread_p, false);
      er_stack_pop ();
    }
  return error_code;

error_dirty_delete:

  error_code = ER_FAILED;

  /* Shutdown the server */
  er_stack_push ();
  boot_server_all_finalize (thread_p, false);
  er_stack_pop ();

  return error_code;
}

/*
 * boot_create_all_volumes () -
 *
 * return:
 *
 *   db_comments(in):
 *   db_npages(in):
 *   file_addmore_vols(in):
 *   log_path(in):
 *   log_prefix(in):
 *   log_npages(in):
 *   client_prog_name(in):
 *   client_user_name(in):
 *   client_host_name(in):
 *   client_process_id(in):
 *   client_lock_wait(in):
 *   client_isolation(in):
 */
static int
boot_create_all_volumes (THREAD_ENTRY * thread_p,
			 const BOOT_CLIENT_CREDENTIAL * client_credential,
			 const char *db_comments, DKNPAGES db_npages,
			 const char *file_addmore_vols, const char *log_path,
			 const char *log_prefix, DKNPAGES log_npages,
			 int client_lock_wait,
			 TRAN_ISOLATION client_isolation)
{
  int tran_index = NULL_TRAN_INDEX;
  VOLID db_volid = NULL_VOLID;
  RECDES recdes;
  int error_code;
  int vacuum_data_npages;
  DBDEF_VOL_EXT_INFO ext_info;
  VFID vacuum_data_vfid, dropped_files_vfid;
  VPID vacuum_data_vpid, dropped_files_vpid;
  bool ignore_old;

  assert (client_credential != NULL);

  error_code = spage_boot (thread_p);
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = heap_manager_initialize ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Create the active log and initialize the log and recovery manager */
  error_code = log_create (thread_p, boot_Db_full_name, log_path, log_prefix,
			   log_npages);
  if (error_code != NO_ERROR || boot_Init_server_is_canceled)
    {
      goto error;
    }
  log_initialize (thread_p, boot_Db_full_name, log_path, log_prefix, false,
		  NULL);

  /* Assign an index to current thread of execution (i.e., a client id) */

  tran_index = logtb_assign_tran_index (thread_p, NULL_TRANID, TRAN_ACTIVE,
					client_credential, NULL,
					client_lock_wait, client_isolation);
  if (tran_index == NULL_TRAN_INDEX)
    {
      goto error;
    }

  ext_info.name = boot_Db_full_name;
  ext_info.comments = db_comments;
  ext_info.max_npages = db_npages;
  ext_info.max_writesize_in_sec = 0;
  ext_info.purpose = DISK_PERMVOL_GENERIC_PURPOSE;
  ext_info.extend_npages = db_npages;

  /* Format the first database volume */
  db_volid = disk_format (thread_p, boot_Db_full_name, LOG_DBFIRST_VOLID,
			  &ext_info);
  if (db_volid != LOG_DBFIRST_VOLID || boot_Init_server_is_canceled)
    {
      goto error;
    }

  if (logpb_add_volume (NULL, LOG_DBFIRST_VOLID, boot_Db_full_name,
			DISK_PERMVOL_GENERIC_PURPOSE) != LOG_DBFIRST_VOLID)
    {
      goto error;
    }

  /*
   * Initialize the database parameter table
   */

  boot_Db_parm->trk_vfid.volid = LOG_DBFIRST_VOLID;
  boot_Db_parm->hfid.vfid.volid = LOG_DBFIRST_VOLID;
  boot_Db_parm->rootclass_hfid.vfid.volid = LOG_DBFIRST_VOLID;
  boot_Db_parm->classname_table.vfid.volid = LOG_DBFIRST_VOLID;
  boot_Db_parm->ctid.vfid.volid = LOG_DBFIRST_VOLID;
  boot_Db_parm->ctid.xhid.vfid.volid = LOG_DBFIRST_VOLID;

  (void) strncpy (boot_Db_parm->rootclass_name, ROOTCLASS_NAME,
		  DB_SIZEOF (boot_Db_parm->rootclass_name));
  boot_Db_parm->nvols = 1;
  boot_Db_parm->last_volid = LOG_DBFIRST_VOLID;
  boot_Db_parm->temp_nvols = 0;
  boot_Db_parm->temp_last_volid = NULL_VOLID;

  /* The query area has been removed */
  boot_Db_parm->query_vfid.volid = NULL_VOLID;
  boot_Db_parm->query_vfid.fileid = NULL_FILEID;

  OID_SET_NULL (&boot_Db_parm->rootclass_oid);
  oid_set_root (&boot_Db_parm->rootclass_oid);

  VFID_SET_NULL (&boot_Db_parm->vacuum_data_vfid);
  VFID_SET_NULL (&boot_Db_parm->dropped_files_vfid);

  /* Create the needed files */
  if (file_tracker_create (thread_p, &boot_Db_parm->trk_vfid) == NULL
      || xheap_create (thread_p, &boot_Db_parm->hfid, NULL, false) < 0
      || xheap_create (thread_p, &boot_Db_parm->rootclass_hfid, NULL,
		       false) < 0
      || heap_assign_address (thread_p, &boot_Db_parm->rootclass_hfid,
			      &boot_Db_parm->rootclass_oid, 0) != NO_ERROR)
    {
      goto error;
    }

  oid_set_root (&boot_Db_parm->rootclass_oid);

  if (xehash_create (thread_p, &boot_Db_parm->classname_table, DB_TYPE_STRING,
		     -1, &boot_Db_parm->rootclass_oid, -1, false) == NULL)
    {
      goto error;
    }

  if (catalog_create (thread_p, &boot_Db_parm->ctid, -1, -1) == NULL)
    {
      goto error;
    }

  error_code = disk_set_boot_hfid (thread_p, LOG_DBFIRST_VOLID,
				   &boot_Db_parm->hfid);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* Store the parameter table */
  recdes.area_size = recdes.length = DB_SIZEOF (*boot_Db_parm);
  recdes.type = REC_HOME;
  recdes.data = (char *) boot_Db_parm;

  if (heap_insert (thread_p, &boot_Db_parm->hfid,
		   &boot_Db_parm->rootclass_oid, boot_Db_parm_oid,
		   &recdes, NULL) != boot_Db_parm_oid)
    {
      goto error;
    }

  if (mvcc_Enabled)
    {
      /* TODO: Compute vacuum data npages */
      vacuum_data_npages = prm_get_integer_value (PRM_ID_VACUUM_DATA_PAGES);

      /* Create files required for vacuum */
      if (file_create (thread_p, &vacuum_data_vfid, vacuum_data_npages,
		       FILE_DROPPED_FILES, NULL, &vacuum_data_vpid,
		       -vacuum_data_npages) == NULL
	  || file_create (thread_p, &dropped_files_vfid, 1, FILE_VACUUM_DATA,
			  NULL, &dropped_files_vpid, 1) == NULL)
	{
	  goto error;
	}

      /* Save VFID's in boot_Db_parm */
      VFID_COPY (&boot_Db_parm->vacuum_data_vfid, &vacuum_data_vfid);
      VFID_COPY (&boot_Db_parm->dropped_files_vfid, &dropped_files_vfid);

      if (heap_update (thread_p, &boot_Db_parm->hfid,
		       &boot_Db_parm->rootclass_oid, boot_Db_parm_oid,
		       &recdes, NULL, &ignore_old, NULL,
		       HEAP_UPDATE_IN_PLACE) != boot_Db_parm_oid)
	{
	  goto error;
	}

      if (vacuum_init_vacuum_files (thread_p, &vacuum_data_vfid,
				    &dropped_files_vfid) != NO_ERROR)
	{
	  goto error;
	}
    }

  /*
   * Create the rest of the other volumes if any
   */

  if (file_addmore_vols != NULL)
    {
      error_code = boot_parse_add_volume_extensions (thread_p,
						     file_addmore_vols);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}
    }

  if (locator_initialize (thread_p, &boot_Db_parm->classname_table) == NULL)
    {
      goto error;
    }

  error_code = pgbuf_flush_all (thread_p, NULL_VOLID);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /*
   * Initialize the catalog manager, the query evaluator, and install meta
   * classes
   */

  oid_set_root (&boot_Db_parm->rootclass_oid);
  catalog_initialize (&boot_Db_parm->ctid);

  if (qmgr_initialize (thread_p) != NO_ERROR)
    {
      goto error;
    }

  error_code = tf_install_meta_classes ();
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  logpb_flush_pages_direct (thread_p);
  (void) pgbuf_flush_all (thread_p, NULL_VOLID);
  (void) fileio_synchronize_all (thread_p, false);

  (void) logpb_checkpoint (thread_p);
  pgbuf_refresh_max_permanent_volume_id (boot_Db_parm->last_volid);
  boot_server_status (BOOT_SERVER_UP);

  return tran_index;

  /* An error was found */
error:

  if (db_volid != NULL_VOLID)
    {
      (void) logpb_delete (thread_p, boot_Db_parm->nvols, boot_Db_full_name,
			   log_path, log_prefix, true);
    }
  else
    {
      if (tran_index != NULL_TRAN_INDEX)
	{
	  logtb_release_tran_index (thread_p, tran_index);
	  log_final (thread_p);
	}
    }

  er_stack_push ();
  boot_server_all_finalize (thread_p, false);
  er_stack_pop ();

  return NULL_TRAN_INDEX;
}

/*
 * boot_remove_all_volumes () - remove all log files, information volumes, and backups
 *                     of given full database name
 *
 * return:  NO_ERROR if all OK, ER_ status otherwise
 *
 *   db_fullname(in):Full name of the database (A path)
 *   log_path(in): Path of log (cannot be NULL)
 *   log_prefix(in): Prefix of log (cannot be NULL)
 *   dirty_rem(in):
 *   force_delete(in):
 *
 * Note: All data, log, and backup files associated with the given
 *              database are removed from the system. However, the database is
 *              not unregistered from the database.txt. That is, this function
 *              does not know anything about database.txt.
 *              See xboot_delete for deletion of database instead of volumes.
 */
static int
boot_remove_all_volumes (THREAD_ENTRY * thread_p, const char *db_fullname,
			 const char *log_path, const char *log_prefix,
			 bool dirty_rem, bool force_delete)
{
  int error_code = NO_ERROR;

  if (dirty_rem)
    {
      goto error_rem_allvols;
    }

  /*
   * How can we perform the delete operation..without restarting the system
   * or restarted the system.
   */

  if (!BO_IS_SERVER_RESTARTED ())
    {
      /* System is not restarted. Read the system parameters */
      if (msgcat_init () != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_CANNOT_ACCESS_MESSAGE_CATALOG, 0);
	  return ER_FAILED;
	}

      if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_BO_CANT_LOAD_SYSPRM, 0);
	  return ER_FAILED;
	}

      /*
       * Initialize error structure, critical section, slotted page, heap, and
       * recovery managers
       */

      er_clear ();

      /* Initialize the transaction table */
      logtb_define_trantable (thread_p, -1, -1);

      error_code = spage_boot (thread_p);
      if (error_code != NO_ERROR)
	{
	  goto error_rem_allvols;
	}
      error_code = heap_manager_initialize ();
      if (error_code != NO_ERROR)
	{
	  goto error_rem_allvols;
	}

      /* The database pagesize is set by log_get_io_page_size */

      if (log_get_io_page_size (thread_p, db_fullname, log_path, log_prefix)
	  == -1)
	{
	  /*
	   * There is something wrong with this database... We will only remove
	   * as much as we can
	   */
	  goto error_rem_allvols;
	}
      if (!fileio_is_volume_exist (db_fullname))
	{
	  goto error_rem_allvols;
	}
      if (!logpb_exist_log (thread_p, db_fullname, log_path, log_prefix))
	{
	  goto error_rem_allvols;
	}
      error_code = boot_mount (thread_p, LOG_DBFIRST_VOLID, db_fullname,
			       NULL);
      if (error_code != NO_ERROR)
	{
	  goto error_rem_allvols;
	}
      if (disk_get_boot_hfid (thread_p, LOG_DBFIRST_VOLID,
			      &boot_Db_parm->hfid) == NULL)
	{
	  goto error_rem_allvols;
	}
      error_code = boot_get_db_parm (thread_p, boot_Db_parm,
				     boot_Db_parm_oid);
      if (error_code != NO_ERROR)
	{
	  goto error_rem_allvols;
	}

      /* Find the rest of the volumes and mount them */
      error_code = boot_find_rest_volumes (thread_p, NULL, LOG_DBFIRST_VOLID,
					   boot_mount, NULL);
      if (error_code != NO_ERROR)
	{
	  goto error_rem_allvols;
	}
      if (locator_initialize (thread_p, &boot_Db_parm->classname_table) ==
	  NULL)
	{
	  goto error_rem_allvols;
	}

      oid_set_root (&boot_Db_parm->rootclass_oid);
      error_code = file_tracker_cache_vfid (&boot_Db_parm->trk_vfid);
      if (error_code != NO_ERROR)
	{
	  goto error_rem_allvols;
	}
      catalog_initialize (&boot_Db_parm->ctid);

      if (qmgr_initialize (thread_p) != NO_ERROR)
	{
	  goto error_rem_allvols;
	}

      log_restart_emergency (thread_p, db_fullname, log_path, log_prefix);
      (void) boot_remove_all_temp_volumes (thread_p);
      boot_server_status (BOOT_SERVER_UP);
      log_final (thread_p);

    }

  /* Now delete the database */
  error_code = logpb_delete (thread_p, boot_Db_parm->nvols, db_fullname,
			     log_path, log_prefix, force_delete);
  return error_code;

error_rem_allvols:

  error_code = logpb_delete (thread_p, -1, db_fullname, log_path, log_prefix,
			     force_delete);

  return error_code;
}

/*
 * xboot_emergency_patch () - patch the database for emergency restart
 *
 * return:  NO_ERROR if all OK, ER_ status otherwise
 *
 *   db_name(in): Database Name
 *   recreate_log(in): true if the log is missing
 *   log_npages(in):
 *   out_fp(in):
 *
 * Note: The database is patched for future restarts. The patch will
 *              remove any indication of recovery to be performed. If a log
 *              is not available, the recreate_flag must be given
 */
int
xboot_emergency_patch (THREAD_ENTRY * thread_p, const char *db_name,
		       bool recreate_log, DKNPAGES log_npages, FILE * out_fp)
{
  char log_path[PATH_MAX];
  const char *log_prefix;
  DB_INFO *db = NULL;
  DB_INFO *dir = NULL;
  int dbtxt_vdes = NULL_VOLDES;
  char dbtxt_label[PATH_MAX];
  int error_code = NO_ERROR;
  int db_charset_db_header = INTL_CODESET_ERROR;

  (void) msgcat_init ();
  if (sysprm_load_and_init (NULL, NULL) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANT_LOAD_SYSPRM, 0);
      return ER_FAILED;
    }

  if (db_name == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
	      db_name);
      return ER_BO_UNKNOWN_DATABASE;
    }

  /*
   * Compose the full name of the database and find location of logs
   */
  if (cfg_read_directory (&dir, false) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CFG_NO_FILE, 1,
	      DATABASES_FILENAME);
      return ER_CFG_NO_FILE;
    }

  if (dir == NULL || ((db = cfg_find_db_list (dir, db_name)) == NULL))
    {
      /*
       * Make sure that nobody was in the process of writing the
       * database.txt when we got a snapshot of it.
       */
      if (dir != NULL)
	{
	  cfg_free_directory (dir);
	  dir = NULL;
	}

      if (cfg_maycreate_get_directory_filename (dbtxt_label) != NULL
#if !defined(WINDOWS) || !defined(DONT_USE_MANDATORY_LOCK_IN_WINDOWS)
	  /* Temporary solution to NT file locking problem. */
	  && (dbtxt_vdes = fileio_mount (thread_p, dbtxt_label, dbtxt_label,
					 LOG_DBTXT_VOLID, 2,
					 true)) != NULL_VOLDES
#endif /* !WINDOWS || !DONT_USE_MANDATORY_LOCK_IN_WINDOWS */
	)
	{
	  if (dbtxt_vdes != NULL_VOLDES)
	    {
	      if (cfg_read_directory_ex (dbtxt_vdes, &dir, false) == NO_ERROR)
		{
		  db = cfg_find_db_list (dir, db_name);
		}
	    }
	  else
	    {
	      if (cfg_read_directory (&dir, false) == NO_ERROR)
		{
		  db = cfg_find_db_list (dir, db_name);
		}
	    }

	  fileio_dismount (thread_p, dbtxt_vdes);
	  dbtxt_vdes = NULL_VOLDES;
	}
      if (db == NULL)
	{
	  if (dir != NULL)
	    {
	      cfg_free_directory (dir);
	    }
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_UNKNOWN_DATABASE, 1,
		  db_name);
	  return ER_BO_UNKNOWN_DATABASE;
	}
    }

  COMPOSE_FULL_NAME (boot_Db_full_name, sizeof (boot_Db_full_name),
		     db->pathname, db_name);

  if (boot_volume_info_log_path (log_path) == NULL)
    {
      strcpy (log_path, db->logpath);
    }

  cfg_free_directory (dir);

  log_prefix = fileio_get_base_file_name (db_name);

  if (sysprm_load_and_init (boot_Db_full_name, NULL) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANT_LOAD_SYSPRM, 0);
      return ER_FAILED;
    }

  /*
   * Initialize error structure, critical section, slotted page, heap, and
   * recovery managers
   */

  /* The database pagesize is set by log_get_io_page_size */

  if (log_get_io_page_size (thread_p, boot_Db_full_name, log_path, log_prefix)
      == -1)
    {
      if (recreate_log != 0)
	{
	  /*
	   * User must indicate the database pagesize through its own environment
	   */
	  (void) db_set_page_size (IO_DEFAULT_PAGE_SIZE,
				   IO_DEFAULT_PAGE_SIZE);
	}
      else
	{
	  return ER_FAILED;
	}
    }

  /* Initialize the transaction table */
  logtb_define_trantable (thread_p, -1, -1);

  error_code = spage_boot (thread_p);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }
  error_code = heap_manager_initialize ();
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* Mount the data volume */
  error_code = boot_mount (thread_p, LOG_DBFIRST_VOLID, boot_Db_full_name,
			   NULL);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* Find the location of the database parameters and read them */
  if (disk_get_boot_hfid (thread_p, LOG_DBFIRST_VOLID, &boot_Db_parm->hfid) ==
      NULL)
    {
      fileio_dismount_all (thread_p);
      return ER_FAILED;
    }
  error_code = boot_get_db_parm (thread_p, boot_Db_parm, boot_Db_parm_oid);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      return error_code;
    }

  /* Find the rest of the volumes and mount them */

  error_code = boot_find_rest_volumes (thread_p, NULL, LOG_DBFIRST_VOLID,
				       boot_mount, NULL);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      return error_code;
    }

  /*
   * Initialize the catalog manager, the query evaluator, and install meta
   * classes
   */

  if (locator_initialize (thread_p, &boot_Db_parm->classname_table) == NULL)
    {
      fileio_dismount_all (thread_p);
      return ER_FAILED;
    }

  oid_set_root (&boot_Db_parm->rootclass_oid);
  error_code = file_tracker_cache_vfid (&boot_Db_parm->trk_vfid);
  if (error_code != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      return error_code;
    }
  catalog_initialize (&boot_Db_parm->ctid);

  if (qmgr_initialize (thread_p) != NO_ERROR)
    {
      fileio_dismount_all (thread_p);
      return ER_FAILED;
    }

  db_charset_db_header =
    log_get_charset_from_header_page (thread_p, boot_Db_full_name, log_path,
				      log_prefix);

  if (db_charset_db_header == INTL_CODESET_ERROR)
    {
      int db_charset_db_root;
      char db_lang[LANG_MAX_LANGNAME];

      if (recreate_log == false)
	{
	  return ER_FAILED;
	}

      (void) lang_set_charset (INTL_CODESET_ISO88591);

      tp_init ();

      /* Initialize tsc-timer */
      tsc_init ();

      error_code =
	catcls_get_server_lang_charset (thread_p, &db_charset_db_root,
					db_lang, sizeof (db_lang) - 1);
      if (error_code != NO_ERROR)
	{
	  return error_code;
	}

      db_charset_db_header = db_charset_db_root;
      tp_final ();
      area_final ();
    }

  error_code = lang_set_charset (db_charset_db_header);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  if (recreate_log == true)
    {
      if (log_npages <= 0)
	{
	  /* Use the default that is the size of the database */
	  log_npages = xdisk_get_total_numpages (thread_p, LOG_DBFIRST_VOLID);

	  if (log_npages < 10)
	    {
	      log_npages = 10;
	    }
	}

      log_recreate (thread_p, boot_Db_parm->nvols, boot_Db_full_name,
		    log_path, log_prefix, log_npages, out_fp);
    }
  else
    {
      log_restart_emergency (thread_p, boot_Db_full_name, log_path,
			     log_prefix);
    }

  boot_server_status (BOOT_SERVER_UP);

  (void) xtran_server_commit (thread_p, false);
  (void) xboot_shutdown_server (thread_p, true);

  return error_code;
}

/*
 * boot_find_new_db_path () - find the new path of database
 *
 * return: db_pathbuf or NULL
 *
 *   db_pathbuf(in): The database path buffer (Set as a side effect)
 *                The size of this buffer muts be at least PATH_MAX
 *   fileof_vols_and_wherepaths(in):A file of volumes and path or NULL.
 *                Each volume entry consists of:
 *                 volid from_fullvolname to_fullvolname
 *
 * Note: Find the new database path from either the given wherepath
 *              file or the current working directory.
 */
static char *
boot_find_new_db_path (char *db_pathbuf,
		       const char *fileof_vols_and_wherepaths)
{
  FILE *where_paths_fp;
  char from_volname[PATH_MAX];	/* Name of new volume      */
  int from_volid;
  char *name;
  char format_string[32];
#if !defined(WINDOWS)
  struct stat stat_buf;
#endif

  if (fileof_vols_and_wherepaths != NULL)
    {
      /*
       * Obtain the new database path from where paths file
       */
      where_paths_fp = fopen (fileof_vols_and_wherepaths, "r");
      if (where_paths_fp == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_LOG_USER_FILE_UNKNOWN, 1,
			       fileof_vols_and_wherepaths);
	  return NULL;
	}

      *db_pathbuf = '\0';
      *from_volname = '\0';

      sprintf (format_string, "%%d %%%ds %%%ds", PATH_MAX - 1, PATH_MAX - 1);
      if (fscanf (where_paths_fp, format_string, &from_volid, from_volname,
		  db_pathbuf) != 3 || from_volid != LOG_DBFIRST_VOLID)
	{
	  fclose (where_paths_fp);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_LOG_USER_FILE_UNORDERED_ENTRIES, 7,
		  fileof_vols_and_wherepaths, 0, from_volid, from_volname,
		  db_pathbuf, LOG_DBFIRST_VOLID, boot_Db_full_name);
	  return NULL;
	}
      fclose (where_paths_fp);

#if !defined(WINDOWS)
      if (stat (db_pathbuf, &stat_buf) != -1 && S_ISCHR (stat_buf.st_mode))
	{
	  if (getcwd (db_pathbuf, PATH_MAX) == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_BO_CWD_FAIL, 0);
	      *db_pathbuf = '0';
	      return NULL;
	    }

	  return db_pathbuf;
	}
#endif /* !WINDOWS */

      name = strrchr (db_pathbuf, PATH_SEPARATOR);
#if defined(WINDOWS)
      {
	char *name_tmp = strrchr (db_pathbuf, '/');

	if (name < name_tmp)
	  {
	    name = name_tmp;
	  }
      }
#endif /* WINDOWS */
      if (name == NULL)
	{
	  /* It does not look like a path name. Use working directory */
	  if (getcwd (db_pathbuf, PATH_MAX) == NULL)
	    {
	      er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				   ER_BO_CWD_FAIL, 0);
	      *db_pathbuf = '\0';
	      return NULL;
	    }
	}
      else
	{
	  *name = '\0';
	}
    }
  else
    {
      /* Use current working directory */
      if (getcwd (db_pathbuf, PATH_MAX) == NULL)
	{
	  er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			       ER_BO_CWD_FAIL, 0);
	  *db_pathbuf = '\0';
	  return NULL;
	}
    }

  return db_pathbuf;
}

/*
 * boot_volume_info_log_path () - find path for log in volinfo
 *
 * return: log_path or NULL
 *
 *   log_path(in): Storage for log path
 *
 * Note: Find path for the log in the volume information.
 */
static char *
boot_volume_info_log_path (char *log_path)
{
  int read_int_volid = NULL_VOLID;
  char *slash;
  FILE *volinfo_fp = NULL;	/* Pointer to new volinfo */
  char format_string[32];

  fileio_make_volume_info_name (log_path, boot_Db_full_name);
  volinfo_fp = fopen (log_path, "r");
  if (volinfo_fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_BO_CANNOT_FINE_VOLINFO, 1,
	      log_path);
      return NULL;
    }

  sprintf (format_string, "%%d %%%ds", PATH_MAX - 1);
  while (true)
    {
      if (fscanf (volinfo_fp, format_string, &read_int_volid, log_path) != 2)
	{
	  read_int_volid = NULL_VOLID;
	  break;
	}
      if (LOG_DBLOG_ACTIVE_VOLID == (VOLID) read_int_volid)
	{
	  break;
	}
    }
  fclose (volinfo_fp);

  if (LOG_DBLOG_ACTIVE_VOLID == (VOLID) read_int_volid)
    {
      slash = strrchr (log_path, PATH_SEPARATOR);
#if defined(WINDOWS)
      {
	char *r_slash = strrchr (log_path, '/');

	if (slash < r_slash)
	  {
	    slash = r_slash;
	  }
      }
#endif /* WINDOWS */
      if (slash != NULL)
	{
	  *slash = '\0';
	}
      return log_path;
    }

  return NULL;
}

/*
 * xboot_compact_db () - compact the database
 *
 * return : NO_ERROR if all OK, ER_ status otherwise
 *
 *   class_oids(in): the class oids list to process
 *   n_classes(in): the length of class_oids
 *   space_to_process(in): the maximum space to process
 *   instance_lock_timeout(in): the lock timeout for instances
 *   class_lock_timeout(in): the lock timeout for classes
 *   delete_old_repr(in): drop old class representations
 *   last_processed_class_oid(in,out): last processed class oid
 *   last_processed_oid(in,out): last processed oid
 *   total_objects(in,out): count processed objects for each class
 *   failed_objects(in,out): count failed objects for each class
 *   modified_objects(in,out): count modified objects for each class
 *   big_objects(in,out): count big objects for each class
 *   initial_last_repr_id(in,out): the list of last class representation
 *
 * Note:
 */

int
xboot_compact_db (THREAD_ENTRY * thread_p, OID * class_oids, int n_classes,
		  int space_to_process,
		  int instance_lock_timeout,
		  int class_lock_timeout,
		  bool delete_old_repr,
		  OID * last_processed_class_oid,
		  OID * last_processed_oid,
		  int *total_objects, int *failed_objects,
		  int *modified_objects, int *big_objects,
		  int *initial_last_repr_id)
{
  return boot_compact_db (thread_p, class_oids, n_classes,
			  space_to_process, instance_lock_timeout,
			  class_lock_timeout, delete_old_repr,
			  last_processed_class_oid, last_processed_oid,
			  total_objects, failed_objects, modified_objects,
			  big_objects, initial_last_repr_id);
}

/*
 * xboot_heap_compact () - compact all pages from hfid of specified class OID
 *   return: error_code
 *   class_oid(in):  the class oid
 */
int
xboot_heap_compact (THREAD_ENTRY * thread_p, OID * class_oid)
{
  return boot_heap_compact_pages (thread_p, class_oid);
}

/*
 * xboot_compact_start () - start database compaction
 *   return: error_code
 */
int
xboot_compact_start (THREAD_ENTRY * thread_p)
{
  return boot_compact_start (thread_p);
}

/*
 * xboot_compact_stop () - stop database compaction
 *   return: error_code
 */
int
xboot_compact_stop (THREAD_ENTRY * thread_p)
{
  return boot_compact_stop (thread_p);
}

bool
boot_set_skip_check_ct_classes (bool val)
{
  bool old_val = skip_to_check_ct_classes_for_rebuild;
  skip_to_check_ct_classes_for_rebuild = val;
  return old_val;
}
