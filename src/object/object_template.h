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
 * object_template.h - Definitions for the object manager
 */

#ifndef _OBJECT_TEMPLATE_H_
#define _OBJECT_TEMPLATE_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include <stdarg.h>

#include "memory_alloc_area.h"
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

  /* class cache, always set, matches the "object" field */
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

  /* how to perform pruning on this template */
  int pruning_type;

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
   * Set if need to check SERIALIZABLE conflicts
   */
  unsigned check_serializable_conflict:1;

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
  unsigned fkeys_were_modified:1;

  /* Set if we want to flush the object to the server regardless of updating the PRIMARY KEY/UNIQUE constraint. */
  unsigned force_flush:1;

  /*
   * true if we want to regard NULL values in NOT NULL AUTO_INCREMENT
   * attributes as errors (i.e. when executing UPDATE or ON DUPLICATE KEY
   * UPDATE statements).
   */
  unsigned force_check_not_null:1;

  /*
   * Set if we ever make an assignment for an attribute that has the
   * function key constraint.
   */
  unsigned function_key_modified:1;

  /*
   * Set if at least one autoincrement column has been populated
   */
  unsigned is_autoincrement_set:1;
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

/*
 * State variable used when generating AUTO_INCREMENT value,
 * to set the first generated AUTO_INCREMENT value as LAST_INSERT_ID.
 * It is only for client-side insertion.
 */
extern bool obt_Last_insert_id_generated;


/* OBJECT TEMPLATE FUNCTIONS */

extern OBJ_TEMPLATE *obt_def_object (MOP class_);
extern OBJ_TEMPLATE *obt_edit_object (MOP object);
extern int obt_quit (OBJ_TEMPLATE * template_ptr);

extern int obt_set (OBJ_TEMPLATE * template_ptr, const char *attname, DB_VALUE * value);
extern int obt_set_obt (OBJ_TEMPLATE * template_ptr, const char *attname, OBJ_TEMPLATE * value);

extern void obt_set_label (OBJ_TEMPLATE * template_ptr, DB_VALUE * label);
extern void obt_disable_unique_checking (OBJ_TEMPLATE * template_ptr);
extern void obt_disable_serializable_conflict_checking (OBJ_TEMPLATE * template_ptr);
extern bool obt_enable_unique_checking (bool new_state);
extern void obt_set_force_flush (OBJ_TEMPLATE * template_ptr);
extern void obt_reset_force_flush (OBJ_TEMPLATE * template_ptr);
extern int obt_update (OBJ_TEMPLATE * template_ptr, MOP * newobj);
extern int obt_assign (OBJ_TEMPLATE * template_ptr, SM_ATTRIBUTE * att, int base_assignment, DB_VALUE * value,
		       SM_VALIDATION * valid);

/* UTILITY FUNCTIONS */

extern DB_VALUE *obt_check_assignment (SM_ATTRIBUTE * att, DB_VALUE * proposed_value, SM_VALIDATION * valid,
				       unsigned force_check_not_null);
extern int obt_update_internal (OBJ_TEMPLATE * template_ptr, MOP * newobj, int check_non_null);
extern int obt_area_init (void);
extern void obt_area_final (void);
extern int obt_find_attribute (OBJ_TEMPLATE * template_ptr, int use_base_class, const char *name, SM_ATTRIBUTE ** attp);
extern int obt_desc_set (OBJ_TEMPLATE * template_ptr, SM_DESCRIPTOR * desc, DB_VALUE * value);
extern int obt_check_missing_assignments (OBJ_TEMPLATE * template_ptr);
extern int obt_populate_known_arguments (OBJ_TEMPLATE * template_ptr);
extern void obt_retain_after_finish (OBJ_TEMPLATE * template_ptr);
extern void obt_begin_insert_values (void);
#endif /* _OBJECT_TEMPLATE_H_ */
