/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * obj.h - Definitions for the object manager
 *
 */

#ifndef _OBJ_H_
#define _OBJ_H_

#ident "$Id$"

#include <stdarg.h>
#include "memory_manager_1.h"
#include "object_representation.h"
#include "class_object.h"
#include "object_template.h"
#include "authenticate.h"

/*
 * OBJ_FORCE_NULL_TO_UNBOUND
 * 
 * Note:
 *    Macro to convert a DB_VALUE structure that contains a logical NULL
 *    value into one with DB_TYPE_NULL.
 * 
 */

#define OBJ_FORCE_NULL_TO_UNBOUND(dbvalue) \
  if (((DB_VALUE_TYPE(dbvalue) == DB_TYPE_STRING) && \
       (DB_GET_STRING(dbvalue) == NULL)) || \
      (((DB_VALUE_TYPE(dbvalue) == DB_TYPE_SET) || \
	(DB_VALUE_TYPE(dbvalue) == DB_TYPE_MULTI_SET) || \
	(DB_VALUE_TYPE(dbvalue) == DB_TYPE_SEQUENCE)) && \
       DB_GET_SET(dbvalue) == NULL) || \
      ((DB_VALUE_TYPE(dbvalue) == DB_TYPE_OBJECT) && \
       (DB_GET_OBJECT(dbvalue) == NULL)) || \
      ((DB_VALUE_TYPE(dbvalue) == DB_TYPE_ELO) && \
       (DB_GET_ELO(dbvalue) == NULL)) ) \
  DB_MAKE_NULL(dbvalue);

/*
 * OBJ_FORCE_SIMPLE_NULL_TO_UNBOUND
 * 
 * Note:
 *    Like above but only handles the "simple" types.
 *    This is used in cases where prcoessing of the other types has been
 *    broken out and there is no need to perform the
 *    extra type comparisons.
 * 
 */

#define OBJ_FORCE_SIMPLE_NULL_TO_UNBOUND(dbvalue) \
  if ((DB_VALUE_TYPE(dbvalue) == DB_TYPE_STRING) && \
      (DB_GET_STRING(dbvalue) == NULL)) \
  DB_MAKE_NULL(dbvalue);


/*
 * 
 *       		      OBJECT HEADER FIELDS
 * 
 * 
 */

/*
 * These are the header fields that are present in every object.
 * Note that the first field in the header must be able to be overlayed
 * by the WS_OBJECT_HEADER structure.  This is so the workspace manager
 * can get to the CHN without knowing anything about the details of the
 * object structure.
 */

#define OBJ_HEADER_CHN_OFFSET 0
#define OBJ_HEADER_BOUND_BITS_OFFSET 4

#define OBJ_HEADER_FIXED_SIZE 4

#define OBJ_HEADER_SIZE(nvars) \
  OBJ_HEADER_FIXED_SIZE + OR_BOUND_BIT_BYTES(nvars)

/*
 * Try to define these in terms of the OR bound bit macros if possible.
 * When the time comes where we have to support heterogeneous networks,
 * this may no longer be an option.  We're lucky now in that the
 * disk representation for these is exactly the same as the memory
 * representation.
 * 
 */

#define OBJ_BOUND_BIT_WORDS OR_BOUND_BIT_WORDS
#define OBJ_BOUND_BIT_BYTES OR_BOUND_BIT_BYTES

#define OBJ_GET_BOUND_BITS(obj) \
  ((char *)(obj) + OBJ_HEADER_BOUND_BITS_OFFSET)

#define OBJ_GET_BOUND_BIT(obj, element) \
  OR_GET_BOUND_BIT(OBJ_GET_BOUND_BITS(obj), element)

#define OBJ_SET_BOUND_BIT(obj, element) \
  OR_ENABLE_BOUND_BIT(OBJ_GET_BOUND_BITS(obj), element)

#define OBJ_CLEAR_BOUND_BIT(obj, element) \
  OR_CLEAR_BOUND_BIT(OBJ_GET_BOUND_BITS(obj), element)

extern char *obj_Method_error_msg;

/*
 * 
 *       		    OBJECT ACCESS FUNCTIONS
 * 
 * 
 */

/* Creation and deletion */
extern MOP obj_create (MOP classop);
extern MOP obj_create_by_name (const char *name);
extern MOP obj_copy (MOP op);
extern int obj_delete (MOP op);

/* Attribute access functions */

extern int obj_get (MOP op, const char *name, DB_VALUE * value);

extern int obj_get_att (MOP op, SM_CLASS * class_, SM_ATTRIBUTE * att,
			DB_VALUE * value);

extern int obj_get_shared (MOP op, const char *name, DB_VALUE * value);
extern int obj_get_path (DB_OBJECT * object, const char *attpath,
			 DB_VALUE * value);
extern int obj_set (MOP op, const char *name, DB_VALUE * value);

extern int obj_set_shared (MOP op, const char *name, DB_VALUE * value);
extern int obj_assign_value (MOP op, SM_ATTRIBUTE * att, char *mem,
			     DB_VALUE * value);

/* Attribute descriptor interface */
extern int obj_desc_set (MOP op, SM_DESCRIPTOR * desc, DB_VALUE * value);
extern int obj_desc_get (MOP op, SM_DESCRIPTOR * desc, DB_VALUE * value);

/* Method invocation */
extern int obj_send_va (MOP obj, const char *name, DB_VALUE * returnval,
			va_list args);

extern int obj_send_stack (MOP obj, const char *name,
			   DB_VALUE * returnval, ...);
extern int obj_send_list (MOP obj, const char *name, DB_VALUE * returnval,
			  DB_VALUE_LIST * arglist);
extern int obj_send_array (MOP obj, const char *name,
			   DB_VALUE * returnval, DB_VALUE ** argarray);
/* Method descriptor interface */

extern int obj_desc_send_va (MOP obj, SM_DESCRIPTOR * desc,
			     DB_VALUE * returnval, va_list args);
extern int obj_desc_send_stack (MOP obj, SM_DESCRIPTOR * desc,
				DB_VALUE * returnval, ...);
extern int obj_desc_send_list (MOP obj, SM_DESCRIPTOR * desc,
			       DB_VALUE * returnval, DB_VALUE_LIST * arglist);
extern int obj_desc_send_array (MOP obj, SM_DESCRIPTOR * desc,
				DB_VALUE * returnval, DB_VALUE ** argarray);
extern int obj_desc_send_array_quick (MOP obj, SM_DESCRIPTOR * desc,
				      DB_VALUE * returnval, int nargs,
				      DB_VALUE ** argarray);

/* backward compatibility, should use obj_send_list() */
extern int obj_send (MOP obj, const char *name, DB_VALUE * returnval,
		     DB_VALUE_LIST * arglist);

/* Tests */

extern int obj_isclass (MOP obj);
extern int obj_isinstance (MOP obj);
extern int obj_is_instance_of (MOP obj, MOP class_mop);

/* Misc operations */
extern int obj_lock (MOP op, int for_write);
extern MOP obj_find_unique (MOP op, const char *attname, DB_VALUE * value,
			    AU_FETCHMODE fetchmode);
extern MOP obj_find_object_by_pkey (MOP classop, DB_VALUE * key,
				    AU_FETCHMODE fetchmode);

extern MOP obj_desc_find_unique (MOP op, SM_DESCRIPTOR * desc,
				 DB_VALUE * value, AU_FETCHMODE fetchmode);

/* Internal support for specific modules */

/* called by Workspace */
extern void obj_free_memory (SM_CLASS * class_, MOBJ obj);

/* attribute locator for set handler */
extern int obj_locate_attribute (MOP op, int attid, int for_write,
				 char **memp, SM_ATTRIBUTE ** attp);
extern char *obj_alloc (SM_CLASS * class_, int bound_bit_status);


extern MOP obj_find_primary_key (MOP op, const DB_VALUE ** values,
				 int size, AU_FETCHMODE fetchmode);
/*
 * extern MOP obj_find_object_by_pkey(MOP op, SM_CLASS_CONSTRAINT *pk, const DB_VALUE *key);
 * 
 */

extern MOP obj_find_multi_attr (MOP op, int size,
				const char *attr_names[],
				const DB_VALUE * values[],
				AU_FETCHMODE fetchmode);
extern MOP obj_find_multi_desc (MOP op, int size,
				const SM_DESCRIPTOR * desc[],
				const DB_VALUE * values[],
				AU_FETCHMODE fetchmode);

extern int obj_get_value (MOP op, SM_ATTRIBUTE * att, void *mem,
			  DB_VALUE * source, DB_VALUE * dest);
extern int obj_find_unique_id (MOP op, const char *att_name,
			       BTID * id_array, int id_array_size,
			       int *total_ids);


#endif /* _OBJ_H_ */
