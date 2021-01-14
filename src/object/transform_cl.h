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
 * transform_cl.h: Function declarations for the client side transformation
 *      routines.
 */

#ifndef _TRANSFORM_CL_H_
#define _TRANSFORM_CL_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "locator.h"

// forward definition
struct or_buf;

/*
 * this should be an enumeration but define it as ints first to
 * ease the integration
 */
typedef int TF_STATUS;

#define TF_SUCCESS 	(0)
#define TF_OUT_OF_SPACE (1)
#define TF_ERROR 	(2)


extern TF_STATUS tf_mem_to_disk (MOP classmop, MOBJ classobj, volatile MOBJ obj, RECDES * record, bool * index_flag);

extern MOBJ tf_disk_to_mem (MOBJ classobj, RECDES * record, int *convertp);

extern TF_STATUS tf_class_to_disk (MOBJ classobj, RECDES * record);

extern MOBJ tf_disk_to_class (OID * oid, RECDES * record);

extern int tf_object_size (MOBJ classobj, MOBJ obj);
#if defined(ENABLE_UNUSED_FUNCTION)
extern void tf_dump_class_size (MOBJ classobj);
#endif

/* new hacks for bulk temporary OID upgrading */
extern OID *tf_need_permanent_oid (or_buf * buf, DB_OBJECT * obj);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int tf_find_temporary_oids (LC_OIDSET * oidset, MOBJ classobj, MOBJ obj);

/* Set packing for M */
extern int tf_set_size (DB_SET * set);
extern int tf_pack_set (DB_SET * set, char *buffer, int buffer_size, int *actual_bytes);
#endif /* ENABLE_UNUSED_FUNCTION */

/* temporary integration kludge */
extern int tf_Allow_fixups;

#endif /* _TRANSFORM_CL_H_ */
