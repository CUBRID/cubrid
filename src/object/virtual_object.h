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
 * virtual_object.h - External definitions for VID's
 */

#ifndef _VIRTUAL_OBJECT_H_
#define _VIRTUAL_OBJECT_H_

#ident "$Id$"

#include "locator_cl.h"
#include "class_object.h"

typedef enum vid_statement_type
{
  VID_FETCH,
  VID_STORE,
  VID_REMOVE
} VID_STATEMENT_TYPE;

/* From virtual_object_2.h */
extern const LOCK lk_Conv[][9];	/* TODO : From lock.c */
extern int vid_flush_instance (MOP mop, void *arg);


extern bool vid_inhibit_null_check;

extern int vid_is_new_pobj (MOP mop);
extern int vid_make_vobj (const OID * view_oid, const OID * class_oid,
			  const DB_VALUE * keys, DB_VALUE * vobj);
extern MOBJ vid_fetch_instance (MOP mop, DB_FETCH_MODE purpose);
extern MOBJ vid_upd_instance (MOP mop);
extern int vid_flush_all_instances (MOP class_mop, bool decache);
extern int vid_flush_and_rehash (MOP mop);
extern DB_VALUE *vid_flush_and_rehash_lbl (DB_VALUE * value);
extern int vid_allflush (void);
extern void vid_gc_vmop (MOP mop, void (*gcmarker) (MOP));
extern MOP vid_add_base_instance (MOBJ instance, MOP class_mop);
extern MOP vid_add_virtual_instance (MOBJ instance, MOP vclass_mop,
				     MOP bclass_mop, SM_CLASS * bclass);
extern MOP vid_build_virtual_mop (MOP bmop, MOP vclass_mop);
extern MOP vid_get_referenced_mop (MOP mop);
extern bool vid_is_updatable (MOP mop);
extern bool vid_is_base_instance (MOP mop);
extern MOP vid_base_instance (MOP mop);
extern bool vid_att_in_obj_id (SM_ATTRIBUTE * attribute_p);
extern int vid_set_att_obj_id (const char *class_name,
			       SM_ATTRIBUTE * attribute_p, int id_no);
extern int vid_record_update (MOP mop, SM_CLASS * class_p,
			      SM_ATTRIBUTE * attribute_p);
extern bool vid_compare_non_updatable_objects (MOP mop1, MOP mop2);

extern bool vid_class_has_intrinsic_oid (SM_CLASS * class_p);
extern void vid_rem_instance (MOP mop);
extern void vid_decache_instance (MOP mop);
extern void vid_get_keys (MOP mop, DB_VALUE * value);
extern DB_OBJLIST *vid_getall_mops (MOP class_mop, SM_CLASS * class_p,
				    DB_FETCH_MODE purpose);
extern int vid_vobj_to_object (const DB_VALUE * vobj, DB_OBJECT ** mop);
extern int vid_oid_to_object (const DB_VALUE * value, DB_OBJECT ** mop);
extern int vid_object_to_vobj (const DB_OBJECT * obj, DB_VALUE * vobj);
extern int vid_encode_object (DB_OBJECT * object, char *string,
			      int allocated_length, int *actual_length);
extern int vid_decode_object (const char *string, DB_OBJECT ** object);

#endif /* _VIRTUAL_OBJECT_H_ */
