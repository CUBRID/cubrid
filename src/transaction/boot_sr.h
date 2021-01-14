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
#include "log_lsa.hpp"
#include "file_io.h"
#include "tde.h"

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

enum boot_server_status
{ BOOT_SERVER_UP = 1, BOOT_SERVER_DOWN, BOOT_SERVER_MAINTENANCE };
typedef enum boot_server_status BOOT_SERVER_STATUS;
extern BOOT_SERVER_STATUS boot_Server_status;

enum boot_server_shutdown_mode
{ BOOT_SHUTDOWN_EXCEPT_COMMON_MODULES, BOOT_SHUTDOWN_ALL_MODULES };
typedef enum boot_server_shutdown_mode BOOT_SERVER_SHUTDOWN_MODE;

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
  bool is_restore_from_backup;
  INT64 db_creation;		/* database creation time */
  LOG_LSA restart_repl_lsa;	/* restart replication lsa after restoreslave */
  char keys_file_path[PATH_MAX];	/* Master Key File (_keys) path for TDE. If it is not NULL, it is used, not the keys spcified system parameter or from default path */
};

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

extern int boot_restart_server (THREAD_ENTRY * thread_p, bool print_restart, const char *db_name, bool from_backup,
				CHECK_ARGS * check_coll_and_timezone, BO_RESTART_ARG * r_args, bool skip_vacuum);
extern int xboot_restart_from_backup (THREAD_ENTRY * thread_p, int print_restart, const char *db_name,
				      BO_RESTART_ARG * r_args);
extern int boot_reset_mk_after_restart_from_backup (THREAD_ENTRY * thread_p, BO_RESTART_ARG * r_args);
extern bool xboot_shutdown_server (REFPTR (THREAD_ENTRY, thread_p), ER_FINAL_CODE is_er_final);
extern int xboot_copy (REFPTR (THREAD_ENTRY, thread_p), const char *from_dbname, const char *new_db_name,
		       const char *new_db_path, const char *new_log_path, const char *new_lob_path,
		       const char *new_db_server_host, const char *new_volext_path,
		       const char *fileof_vols_and_copypaths, bool new_db_overwrite);
extern int xboot_soft_rename (THREAD_ENTRY * thread_p, const char *old_db_name, const char *new_db_name,
			      const char *new_db_path, const char *new_log_path, const char *new_db_server_host,
			      const char *new_volext_path, const char *fileof_vols_and_renamepaths,
			      bool new_db_overwrite, bool extern_rename, bool force_delete);
extern int xboot_delete (const char *db_name, bool force_delete, BOOT_SERVER_SHUTDOWN_MODE shutdown_common_modules);
extern int xboot_emergency_patch (const char *db_name, bool recreate_log, DKNPAGES log_npages, const char *db_locale,
				  FILE * out_fp);
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
