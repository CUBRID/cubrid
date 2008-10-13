/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 *      tfcl.h: Function declarations for the client side transformation 
 *      routines.
 */

#ifndef _TFCL_H_
#define _TFCL_H_

#ident "$Id$"

#include "locator_bt.h"

/*
 * this should be an enumeration but define it as ints first to
 * ease the integration
 */
typedef int TF_STATUS;

#define TF_SUCCESS 	(0)
#define TF_OUT_OF_SPACE (1)
#define TF_ERROR 	(2)


extern TF_STATUS tf_mem_to_disk (MOP classmop, MOBJ classobj,
				 volatile MOBJ obj,
				 RECDES * record, int *index_flag);

extern MOBJ tf_disk_to_mem (MOBJ classobj, RECDES * record, int *convertp);

extern TF_STATUS tf_class_to_disk (MOBJ classobj, RECDES * record);

extern MOBJ tf_disk_to_class (RECDES * record);

extern int tf_object_size (MOBJ classobj, MOBJ obj);
extern int tf_class_size (MOBJ classobj);
extern void tf_dump_class_size (MOBJ classobj);


/* new hacks for bulk temporary OID upgrading */
extern OID *tf_need_permanent_oid (OR_BUF * buf, DB_OBJECT * obj);
extern int tf_find_temporary_oids (LC_OIDSET * oidset, MOBJ classobj,
				   MOBJ obj);

/* Set packing for M */
extern int tf_set_size (DB_SET * set);
extern int tf_pack_set (DB_SET * set, char *buffer, int buffer_size,
			int *actual_bytes);

/* temporary integration kludge */
extern int tf_Allow_fixups;

#endif /* _TFCL_H_ */
