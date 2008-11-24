/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 * elo_recovery.h - external definitions for FBO recovery functions
 *
 * Note: 
 */

#ifndef _ELO_RECOVERY_H_
#define _ELO_RECOVERY_H_

#ident "$Id$"

extern void esm_expand_pathname (const char *source, char *destination,
				 int max_length);
extern int esm_redo (const int buffer_size, char *buffer);
extern int esm_undo (const int buffer_size, char *buffer);
extern void esm_dump (const int buffer_size, void *data);
extern int esm_shadow_file_exists (const DB_OBJECT * holder_p);
extern void esm_delete_shadow_entry (const DB_OBJECT * holder_p);
extern char *esm_make_shadow_file (DB_OBJECT * holder_p);
extern int esm_make_dropped_shadow_file (DB_OBJECT * holder_p);
extern int esm_get_shadow_file_name (DB_OBJECT * glo_p, char **path);
extern void esm_process_savepoint (void);
extern void esm_process_system_savepoint (void);

#endif /* _ELO_RECOVERY_H_ */
