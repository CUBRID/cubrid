/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * elo_recover.h - external definitions for FBO recovery functions
 *
 * Note: 
 */

#ifndef _ELO_RECOVER_H_
#define _ELO_RECOVER_H_

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

#endif /* _ELO_RECOVER_H_ */
