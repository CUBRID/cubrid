/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 *      qf.h: Definitions for the Qick Fit allocation system.
 */

#ifndef _QF_H_
#define _QF_H_

#ident "$Id$"

extern unsigned int db_create_workspace_heap (void);
extern void db_destroy_workspace_heap (void);

extern void db_ws_free (void *obj);
extern void *db_ws_alloc (size_t bytes);
extern void *db_ws_realloc (void *obj, size_t newsize);

#endif /* _QF_H_ */
