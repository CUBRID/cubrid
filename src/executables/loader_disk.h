/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 *      ldr_disk.c: loader transformer disk access module
 */

#ifndef _LOADER_DISK_H_
#define _LOADER_DISK_H_

#ident "$Id$"

#include "load_object.h"

/* Module control */
extern int disk_init (void);
extern void disk_final (void);

/* Instance operations */
extern int disk_reserve_instance (MOP classop, OID * oid);
extern int disk_insert_instance (MOP classop, DESC_OBJ * obj, OID * oid);
extern int disk_update_instance (MOP classop, DESC_OBJ * obj, OID * oid);
extern int disk_insert_instance_using_mobj (MOP classop, MOBJ classobj,
					    MOBJ obj, OID * oid);
extern int disk_update_instance_using_mobj (MOP classop, MOBJ classobj,
					    MOBJ obj, OID * oid);

#endif /* _LOADER_DISK_H_ */
