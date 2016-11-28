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
 * boot_sr.h - Boot managment in the server (interface)
 *
 * Note: See .c file for overview and description of the interface functions.
 *
 */

#ifndef _BOOT_SR_H_
#define _BOOT_SR_H_

#ident "$Id$"

#include "config.h"

#include <time.h>

#include "boot.h"
#include "error_manager.h"
#include "storage_common.h"
#include "oid.h"
#include "disk_manager.h"
#include "log_comm.h"
#include "file_io.h"

#if defined (SERVER_MODE)
#define AUTO_ADD_VOL_EXPAND_NPAGES        (20)

typedef struct auto_addvol_job AUTO_ADDVOL_JOB;
struct auto_addvol_job
{
  pthread_mutex_t lock;
  pthread_cond_t cond;
  DBDEF_VOL_EXT_INFO ext_info;
  VOLID ret_volid;
};

#define BOOT_AUTO_ADDVOL_JOB_INITIALIZER                          \
  { PTHREAD_MUTEX_INITIALIZER,                                    \
    PTHREAD_COND_INITIALIZER,                                     \
    {NULL, NULL, NULL, 0, 0, 0, DISK_PERMVOL_DATA_PURPOSE, false},\
    NULL_VOLID                                                    \
  };

#endif

typedef enum boot_server_status BOOT_SERVER_STATUS;
enum boot_server_status
{ BOOT_SERVER_UP = 1, BOOT_SERVER_DOWN, BOOT_SERVER_MAINTENANCE };
extern BOOT_SERVER_STATUS boot_Server_status;

typedef enum boot_server_shutdown_mode BOOT_SERVER_SHUTDOWN_MODE;
enum boot_server_shutdown_mode
{ BOOT_SHUTDOWN_EXCEPT_COMMON_MODULES, BOOT_SHUTDOWN_ALL_MODULES };

typedef struct check_args CHECK_ARGS;

struct check_args
{
  bool check_db_coll;
  bool check_timezone;
};

#define BO_IS_SERVER_RESTARTED() \
        (boot_Server_status == BOOT_SERVER_UP \
         || boot_Server_status == BOOT_SERVER_MAINTENANCE)

extern void boot_server_status (BOOT_SERVER_STATUS status);

#if defined(SERVER_MODE)
/* in xserver_interface.h */
extern void boot_donot_shutdown_server_at_exit (void);
extern void xboot_notify_unregister_client (THREAD_ENTRY * thread_p, int tran_index);
#endif /* SERVER_MODE */

extern const char *boot_db_name (void);
extern const char *boot_db_full_name (void);
#if !defined(CS_MODE)
extern const char *boot_get_lob_path (void);
#endif /* !CS_MODE */
int boot_find_root_heap (HFID * root_hfid_p);

extern VOLID boot_find_next_permanent_volid (THREAD_ENTRY * thread_p);
extern int boot_reset_db_parm (THREAD_ENTRY * thread_p);
extern DKNPAGES boot_max_pages_new_volume (void);
extern DKNPAGES boot_max_pages_for_new_auto_volume_extension (void);
extern DKNPAGES boot_max_pages_for_new_temp_volume (void);
extern DKNPAGES boot_get_temp_temp_vol_max_npages (void);	/* todo: remove me */

extern int boot_restart_server (THREAD_ENTRY * thread_p, bool print_restart, const char *db_name, bool from_backup,
				CHECK_ARGS * check_coll_and_timezone, BO_RESTART_ARG * r_args);
extern int xboot_restart_from_backup (THREAD_ENTRY * thread_p, int print_restart, const char *db_name,
				      BO_RESTART_ARG * r_args);
extern bool xboot_shutdown_server (THREAD_ENTRY * thread_p, ER_FINAL_CODE is_er_final);
extern int xboot_copy (THREAD_ENTRY * thread_p, const char *from_dbname, const char *new_db_name,
		       const char *new_db_path, const char *new_log_path, const char *new_lob_path,
		       const char *new_db_server_host, const char *new_volext_path,
		       const char *fileof_vols_and_copypaths, bool new_db_overwrite);
extern int xboot_soft_rename (THREAD_ENTRY * thread_p, const char *old_db_name, const char *new_db_name,
			      const char *new_db_path, const char *new_log_path, const char *new_db_server_host,
			      const char *new_volext_path, const char *fileof_vols_and_renamepaths,
			      bool new_db_overwrite, bool extern_rename, bool force_delete);
extern int xboot_delete (THREAD_ENTRY * thread_p, const char *db_name, bool force_delete,
			 BOOT_SERVER_SHUTDOWN_MODE shutdown_common_modules);
extern int xboot_emergency_patch (THREAD_ENTRY * thread_p, const char *db_name, bool recreate_log, DKNPAGES log_npages,
				  const char *db_locale, FILE * out_fp);
extern void boot_server_all_finalize (THREAD_ENTRY * thread_p, ER_FINAL_CODE is_er_final,
				      BOOT_SERVER_SHUTDOWN_MODE shutdown_common_modules);
extern int boot_compact_db (THREAD_ENTRY * thread_p, OID * class_oids, int n_classes, int space_to_process,
			    int instance_lock_timeout, int class_lock_timeout, bool delete_old_repr,
			    OID * last_processed_class_oid, OID * last_processed_oid, int *total_objects,
			    int *failed_objects, int *modified_objects, int *big_objects, int *initial_last_repr_id);
extern int boot_heap_compact_pages (THREAD_ENTRY * thread_p, OID * class_oid);
extern int boot_compact_start (THREAD_ENTRY * thread_p);
extern int boot_compact_stop (THREAD_ENTRY * thread_p);
extern bool boot_can_compact (THREAD_ENTRY * thread_p);
extern bool boot_set_skip_check_ct_classes (bool val);
extern const char *boot_client_type_to_string (BOOT_CLIENT_TYPE type);

extern int boot_get_new_volume_name_and_id (THREAD_ENTRY * thread_p, DB_VOLTYPE voltype, const char *given_path,
					    const char *given_name, char *fullname_newvol_out,
					    VOLID * volid_newvol_out);
extern int boot_dbparm_save_volume (THREAD_ENTRY * thread_p, DB_VOLTYPE voltype, VOLID volid);
#endif /* _BOOT_SR_H_ */
