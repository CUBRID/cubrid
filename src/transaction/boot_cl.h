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
extern char boot_Host_connected[MAXHOSTNAMELEN];
#endif /* CS_MODE */

extern int boot_initialize_client (BOOT_CLIENT_CREDENTIAL * client_credential,
				   BOOT_DB_PATH_INFO * db_path_info,
				   bool db_overwrite,
				   const char *file_addmore_vols,
				   DKNPAGES npages,
				   PGLENGTH db_desired_pagesize,
				   DKNPAGES log_npages,
				   PGLENGTH db_desired_log_page_size);
extern int boot_restart_client (BOOT_CLIENT_CREDENTIAL * client_credential);
extern int boot_shutdown_client (bool iserfinal);
extern void boot_donot_shutdown_client_at_exit (void);
extern void boot_server_die_or_changed (void);
extern void boot_client_all_finalize (bool iserfinal);
#if defined(CS_MODE)
extern char *boot_get_host_connected (void);
extern int boot_get_ha_server_state (void);
#endif /* CS_MODE */

#if defined(SA_MODE)
extern int boot_build_catalog_classes (const char *dbname);
extern int boot_destroy_catalog_classes (void);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int boot_rebuild_catalog_classes (const char *dbname);
#endif /* ENABLE_UNUSED_FUNCTION */
#endif /* SA_MODE */
#endif /* _BOOT_CL_H_ */
