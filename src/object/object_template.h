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
 * object_template.h - Definitions for the object manager
 */

#ifndef _OBJECT_TEMPLATE_H_
#define _OBJECT_TEMPLATE_H_

#ident "$Id$"

#include <stdarg.h>

#include "area_alloc.h"
#include "object_representation.h"
#include "class_object.h"

#define OBT_BASE_OBJECT(template_ptr) \
  (((template_ptr)->base_object != NULL) ? \
   (template_ptr)->base_object : (template_ptr)->object)

#define OBT_BASE_CLASS(template_ptr) \
  (((template_ptr)->base_class != NULL) ? \
   (template_ptr)->base_class : (template_ptr)->class_)

#define OBT_BASE_CLASSOBJ(template_ptr) \
  (((template_ptr)->base_classobj != NULL) ? \
   (template_ptr)->base_classobj : (template_ptr)->classobj)

/*
 * ATT_IS_UNIQUE
 *
 * Note :
 *    Checks to see if an attribute has the UNIQUE integrity constraint.
 */
#define ATT_IS_UNIQUE(att) \
  (classobj_get_cached_constraint((att)->constraints, SM_CONSTRAINT_PRIMARY_KEY, NULL) || \
   classobj_get_cached_constraint((att)->constraints, SM_CONSTRAINT_UNIQUE, NULL) || \
   classobj_get_cached_constraint((att)->constraints, SM_CONSTRAINT_REVERSE_UNIQUE, NULL))

/*
 * OBJ_TEMPASSIGN
 *
 * Note :
 *    Substructure of OBJ_TEMPLATE.  Used to store information about
 *    a pending attribute assignment.
 */
typedef struct obj_tempassign
{

  struct obj_template *obj;	/* if new object assignment */
  DB_VALUE *variable;		/* if non-object assignment */

  /*
   * cache of attribute definition, must be verified as part
   * of the outer template validation
   */
  SM_ATTRIBUTE *att;

  /* used for UPDATE templates with triggers */
  DB_VALUE *old_value;

  unsigned is_default:1;
  unsigned is_auto_increment:1;

} OBJ_TEMPASSIGN;

/*
 * OBJ_TEMPLATE
 *
 * Note :
 *    Used to define a set of attribute assignments that are to be
 *    considered as a single operation.  Either all of the assignments
 *    will be applied or none.
 */
typedef struct obj_template
{

  /* edited object, NULL if insert template */
  /*
   * garbage collector tickets are not required for the object & base_object
   * fields as the entire object template area is registered for scanning by
   * area_create().
   */
  MOP object;

  /* class cache, always set, matches the "object" field  */
  MOP classobj;
  SM_CLASS *class_;

  /* base class cache, set only if class cache has a virtual class */
  MOP base_classobj;
  SM_CLASS *base_class;
  MOP base_object;

  /* class cache validation info */
  int tran_id;			/* transaction id at the time the template was created */
  unsigned int schema_id;	/* schema counter at the time the template was created */

  /* template assignment vector */
  OBJ_TEMPASSIGN **assignments;

  /* optional address to store new object pointer when created */
  DB_VALUE *label;

  /* Number of assignments allocated for the vector */
  int nassigns;

  /* Used to detect cycles in the template hierarchy */
  unsigned int traversal;

  /* write lock flag */
  unsigned write_lock:1;

  /* for detection of cycles in template hierarchy */
  unsigned traversed:1;

  /*
   * set if this is being used for the "old" temporary object in
   * trigger processing
   */
  unsigned is_old_template:1;

  /*
   * Set if we're updating class attributes rather than instance attributes.
   * This happens when the object and the class are the same.
   */
  unsigned is_class_update:1;

  /*
   * Set if we're doing bulk updates to disable unique checking from
   * templates.
   */
  unsigned check_uniques:1;

  /*
   * Set if we ever make an assignment for an attribute that has the
   * UNIQUE constraint.  Speeds up a common test.
   */
  unsigned uniques_were_modified:1;

  /* Set if we ever make an assignment for a shared attribute. */
  unsigned shared_was_modified:1;

  /* Set if we should free the template after it is applied */
  unsigned discard_on_finish:1;

  /*
   * true if we ever make an assignment for an attribute that has the
   * FOREIGN KEY constraint.  Speeds up a common test.
   */
  unsigned is_fkeys_were_modified:1;
} OBJ_TEMPLATE, *OBT;

/*
 * State used when creating templates, to indicate whether unique constraint
 * checking is enabled.
 * This state can be modifed using obt_enable_unique_checking()
 */
extern bool obt_Check_uniques;

/*
 * State variable used when creating object template, to indicate whether enable
 * auto increment feature
 */
extern bool obt_Enable_autoincrement;


/* OBJECT TEMPLATE FUNCTIONS */

extern OBJ_TEMPLATE *obt_def_object (MOP class_);
extern OBJ_TEMPLATE *obt_edit_object (MOP object);
extern int obt_quit (OBJ_TEMPLATE * template_ptr);

extern int obt_set (OBJ_TEMPLATE * template_ptr, const char *attname,
		    DB_VALUE * value);
extern int obt_set_obt (OBJ_TEMPLATE * template_ptr, const char *attname,
			OBJ_TEMPLATE * value);

extern void obt_set_label (OBJ_TEMPLATE * template_ptr, DB_VALUE * label);
extern void obt_disable_unique_checking (OBJ_TEMPLATE * template_ptr);
extern bool obt_enable_unique_checking (bool new_state);
extern int obt_update (OBJ_TEMPLATE * template_ptr, MOP * newobj);
extern int obt_assign (OBJ_TEMPLATE * template_ptr, SM_ATTRIBUTE * att,
		       int base_assignment, DB_VALUE * value,
		       SM_VALIDATION * valid);

/* UTILITY FUNCTIONS */

extern DB_VALUE *obt_check_assignment (SM_ATTRIBUTE * att,
				       DB_VALUE * proposed_value,
				       SM_VALIDATION * valid);
extern int obt_update_internal (OBJ_TEMPLATE * template_ptr, MOP * newobj,
				int check_non_null);
extern void obt_area_init (void);
extern int obt_find_attribute (OBJ_TEMPLATE * template_ptr,
			       int use_base_class, const char *name,
			       SM_ATTRIBUTE ** attp);
extern int obt_desc_set (OBJ_TEMPLATE * template_ptr, SM_DESCRIPTOR * desc,
			 DB_VALUE * value);
extern int obt_check_missing_assignments (OBJ_TEMPLATE * template_ptr);
extern void obt_retain_after_finish (OBJ_TEMPLATE * template_ptr);

#endif /* _OBJECT_TEMPLATE_H_ */
