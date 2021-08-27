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
 * boot_cl.h - Boot management in the client (interface)
 *
 * Note: See .c file for overview and description of the interface functions.
 *
 */

#ifndef _BOOT_CL_H_
#define _BOOT_CL_H_

#ident "$Id$"

#if !defined(WINDOWS)
#include <sys/time.h>
#endif /* !WINDOWS */

#include "porting.h"
#include "boot.h"
#include "error_manager.h"
#include "storage_common.h"
#include "transaction_cl.h"

#define BOOT_IS_CLIENT_RESTARTED() (tm_Tran_index != NULL_TRAN_INDEX)

/* Volume assigned for new files/objects  (e.g., heap files) */
extern VOLID boot_User_volid;
#if defined(CS_MODE)
/* Server host connected */
extern char boot_Host_connected[CUB_MAXHOSTNAMELEN];
#endif /* CS_MODE */

extern int boot_initialize_client (BOOT_CLIENT_CREDENTIAL * client_credential, BOOT_DB_PATH_INFO * db_path_info,
				   bool db_overwrite, const char *file_addmore_vols, DKNPAGES npages,
				   PGLENGTH db_desired_pagesize, DKNPAGES log_npages, PGLENGTH db_desired_log_page_size,
				   const char *lang_charset);
extern int boot_restart_client (BOOT_CLIENT_CREDENTIAL * client_credential);
extern int boot_shutdown_client (bool iserfinal);
extern void boot_donot_shutdown_client_at_exit (void);
extern void boot_server_die_or_changed (void);
extern void boot_client_all_finalize (bool iserfinal);
#if defined(CS_MODE)
extern char *boot_get_host_connected (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern HA_SERVER_STATE boot_get_ha_server_state (void);
#endif /* ENABLE_UNUSED_FUNCTION */
extern const char *boot_get_lob_path (void);
#endif /* CS_MODE */

extern char *boot_get_host_name (void);

#if defined(SA_MODE)
extern int boot_build_catalog_classes (const char *dbname);
extern int boot_destroy_catalog_classes (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int boot_rebuild_catalog_classes (const char *dbname);
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* SA_MODE */
extern void boot_clear_host_connected (void);
extern char *boot_get_server_session_key (void);
extern void boot_set_server_session_key (const char *key);
extern int boot_add_collations (MOP class_mop);
#endif /* _BOOT_CL_H_ */
