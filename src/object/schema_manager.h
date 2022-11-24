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
 * schema_manager.h - External definitions for the schema manager
 */

#ifndef _SCHEMA_MANAGER_H_
#define _SCHEMA_MANAGER_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* defined (SERVER_MODE) */

#include "language_support.h"	/* for international string functions */
#include "storage_common.h"	/* for HFID */
#include "object_domain.h"	/* for TP_DOMAIN */
#include "work_space.h"		/* for MOP */
#include "class_object.h"	/* for SM_CLASS */
#include "schema_template.h"	/* template interface */
#include "trigger_manager.h"	/* for TR_EVENT_TYPE */
#include "tde.h"

/*
 * This is NOT the "object" class but rather functions more like
 * the meta-class of class objects.
 * This formerly stored the list of classes that had no super classes,
 * in that way it was kind of like the root "object" of the class
 * hierarchy.  Unfortunately, maintaining this list caused contention
 * problems on the the root object so it was removed.  The list
 * of base classes is now generated manually by examining all classes.
 */

/*
 * Use full SM_CLASS definition for ROOT_CLASS, instead of just SM_CLASS_HEADER.
 * This avoids to handle particular case of sm_Root_class object in many usage of 'au_fetch_class_..' functions.
 * However serialization functions will use only the header part of the object.
*/
typedef SM_CLASS ROOT_CLASS;

/*
 * Structure used when truncating a class and changing an attribute.
 * During these operations, indexes are dropped and recreated.
 * The information needed to recreate the constraints (indexes) are saved in
 * this structure.
 */
typedef struct sm_constraint_info SM_CONSTRAINT_INFO;

struct sm_constraint_info
{
  struct sm_constraint_info *next;
  char *name;
  char **att_names;
  int *asc_desc;
  int *prefix_length;
  SM_PREDICATE_INFO *filter_predicate;
  char *ref_cls_name;
  char **ref_attrs;
  SM_FUNCTION_INFO *func_index_info;
  SM_FOREIGN_KEY_ACTION fk_delete_action;
  SM_FOREIGN_KEY_ACTION fk_update_action;
  DB_CONSTRAINT_TYPE constraint_type;
  const char *comment;
  SM_INDEX_STATUS index_status;	// Used to save index_status in case of rebuild or moving the constraint
};

extern ROOT_CLASS sm_Root_class;

extern const char TEXT_CONSTRAINT_PREFIX[];

extern MOP sm_Root_class_mop;
extern HFID *sm_Root_class_hfid;
extern const char *sm_Root_class_name;

extern int sm_finish_class (SM_TEMPLATE * template_, MOP * classmop);
extern int sm_update_class (SM_TEMPLATE * template_, MOP * classmop);
extern int sm_update_class_with_auth (SM_TEMPLATE * template_, MOP * classmop, DB_AUTH auth, bool lock_hierarchy);
extern int sm_update_class_auto (SM_TEMPLATE * template_, MOP * classmop);
extern int sm_delete_class_mop (MOP op, bool is_cascade_constraints);
extern int ib_get_thread_count ();
#if defined(ENABLE_UNUSED_FUNCTION)
extern int sm_delete_class (const char *name);
#endif

extern int sm_get_index (MOP classop, const char *attname, BTID * index);
extern char *sm_produce_constraint_name (const char *class_name, DB_CONSTRAINT_TYPE constraint_type,
					 const char **att_names, const int *asc_desc, const char *given_name);
extern char *sm_produce_constraint_name_mop (MOP classop, DB_CONSTRAINT_TYPE constraint_type, const char **att_names,
					     const int *asc_desc, const char *given_name);
extern char *sm_produce_constraint_name_tmpl (SM_TEMPLATE * tmpl, DB_CONSTRAINT_TYPE constraint_type,
					      const char **att_names, const int *asc_desc, const char *given_name);
extern int sm_add_constraint (MOP classop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
			      const char **att_names, const int *asc_desc, const int *attrs_prefix_length,
			      int class_attributes, SM_PREDICATE_INFO * predicate_info, SM_FUNCTION_INFO * fi_info,
			      const char *comment, SM_INDEX_STATUS index_status);
extern int sm_drop_constraint (MOP classop, DB_CONSTRAINT_TYPE constraint_type, const char *constraint_name,
			       const char **att_names, bool class_attributes, bool mysql_index_name);
extern int sm_drop_index (MOP classop, const char *constraint_name);
extern int sm_exist_index (MOP classop, const char *idxname, BTID * btid);

/* Misc schema operations */
extern int sm_rename_class (MOP op, const char *new_name);
extern void sm_mark_system_classes (void);
extern int sm_update_all_catalog_statistics (bool with_fullscan);
extern int sm_update_catalog_statistics (const char *class_name, bool with_fullscan);
extern int sm_force_write_all_classes (void);
#ifdef SA_MODE
extern void sm_mark_system_class_for_catalog (void);
#endif /* SA_MODE */
extern int sm_mark_system_class (MOP classop, int on_or_off);
extern int sm_is_system_class (MOP op);
extern bool sm_check_system_class_by_name (const char *name);
extern bool sm_is_reuse_oid_class (MOP op);
extern int sm_check_reuse_oid_class (MOP op);
extern int sm_is_partitioned_class (MOP op);
extern int sm_partitioned_class_type (DB_OBJECT * classop, int *partition_type, char *keyattr, MOP ** partitions);
extern int sm_set_class_flag (MOP classop, SM_CLASS_FLAG flag, int onoff);
extern int sm_set_class_tde_algorithm (MOP classop, TDE_ALGORITHM tde_algo);
extern int sm_get_class_tde_algorithm (MOP classop, TDE_ALGORITHM * tde_algo);
extern int sm_get_class_flag (MOP op, SM_CLASS_FLAG flag);
extern int sm_set_class_collation (MOP classop, int collation_id);
extern int sm_get_class_collation (MOP classop, int *collation_id);
extern int sm_set_class_comment (MOP classop, const char *comment);
extern int sm_destroy_representations (MOP op);

extern void sm_add_static_method (const char *name, void (*function) ());
extern void sm_delete_static_method (const char *name);
extern void sm_flush_static_methods (void);

extern int sm_link_method (SM_CLASS * class_, SM_METHOD * method);
extern int sm_prelink_methods (DB_OBJLIST * classes);
extern int sm_force_method_link (MOP obj);


extern char *sm_get_method_source_file (MOP obj, const char *name);

extern int sm_truncate_class (MOP class_mop, const bool is_cascade);
extern int sm_truncate_using_delete (MOP class_mop);
extern int sm_truncate_using_destroy_heap (MOP class_mop);

bool sm_is_possible_to_recreate_constraint (MOP class_mop, const SM_CLASS * const class_,
					    const SM_CLASS_CONSTRAINT * const constraint);


extern int sm_save_constraint_info (SM_CONSTRAINT_INFO ** save_info, const SM_CLASS_CONSTRAINT * const c);
extern int sm_save_function_index_info (SM_FUNCTION_INFO ** save_info, SM_FUNCTION_INFO * func_index_info);
extern int sm_save_filter_index_info (SM_PREDICATE_INFO ** save_info, SM_PREDICATE_INFO * filter_index_info);
extern void sm_free_constraint_info (SM_CONSTRAINT_INFO ** save_info);


/* Utility functions */
extern int sm_check_name (const char *name);
extern int sm_check_catalog_rep_dir (MOP classmop, SM_CLASS * class_);
extern SM_NAME_SPACE sm_resolution_space (SM_NAME_SPACE name_space);

/* Class location functions */
extern MOP sm_get_class (MOP obj);
extern SM_CLASS_TYPE sm_get_class_type (SM_CLASS * class_);

extern DB_OBJLIST *sm_fetch_all_classes (int external_list, DB_FETCH_MODE purpose);
extern DB_OBJLIST *sm_fetch_all_base_classes (int external_list, DB_FETCH_MODE purpose);
extern DB_OBJLIST *sm_fetch_all_objects (DB_OBJECT * op, DB_FETCH_MODE purpose);
extern DB_OBJLIST *sm_fetch_all_objects_of_dirty_version (DB_OBJECT * op, DB_FETCH_MODE purpose);

/* Domain maintenance */
extern int sm_filter_domain (TP_DOMAIN * domain, int *changes);
extern int sm_check_class_domain (TP_DOMAIN * domain, MOP class_);
extern int sm_check_object_domain (TP_DOMAIN * domain, MOP object);
extern int sm_coerce_object_domain (TP_DOMAIN * domain, MOP object, MOP * dest_object);

/* Extra cached state */
extern int sm_clean_class (MOP classmop, SM_CLASS * class_);
extern int sm_touch_class (MOP classmop);

/* Statistics functions */
extern SM_CLASS *sm_get_class_with_statistics (MOP classop);
extern CLASS_STATS *sm_get_statistics_force (MOP classop);
extern int sm_update_statistics (MOP classop, bool with_fullscan);
extern int sm_update_all_statistics (bool with_fullscan);

/* Misc information functions */
extern const char *sm_get_ch_name (MOP op);
extern HFID *sm_get_ch_heap (MOP classmop);
#if 0				/* TODO - do not use */
extern OID *sm_get_ch_rep_dir (MOP classmop);
#endif

extern int sm_is_subclass (MOP classmop, MOP supermop);
extern int sm_is_partition (MOP classmop, MOP supermop);
extern int sm_object_size_quick (SM_CLASS * class_, MOBJ obj);
extern SM_CLASS_CONSTRAINT *sm_class_constraints (MOP classop);

/* Locator support functions */
extern const char *sm_ch_name (const MOBJ clobj);
extern HFID *sm_ch_heap (MOBJ clobj);
extern OID *sm_ch_rep_dir (MOBJ clobj);

extern bool sm_has_indexes (MOBJ class_);

/* Interpreter support functions */
extern char *sm_downcase_name (const char *name, char *buf, int buf_size);
extern char *sm_user_specified_name (const char *name, char *buf, int buf_size);
extern char *sm_qualifier_name (const char *name, char *buf, int buf_size);
extern const char *sm_remove_qualifier_name (const char *name);
extern MOP sm_find_class (const char *name);
extern MOP sm_find_class_with_purpose (const char *name, bool for_update);
extern MOP sm_find_synonym (const char *name);
extern char *sm_get_synonym_target_name (MOP synonym, char *buf, int buf_size);

extern const char *sm_get_att_name (MOP classop, int id);
extern int sm_att_id (MOP classop, const char *name);
extern DB_TYPE sm_att_type_id (MOP classop, const char *name);

extern MOP sm_att_class (MOP classop, const char *name);
extern int sm_att_info (MOP classop, const char *name, int *idp, TP_DOMAIN ** domainp, int *sharedp, int class_attr);
extern int sm_att_constrained (MOP classop, const char *name, SM_ATTRIBUTE_FLAG cons);
extern bool sm_att_auto_increment (MOP classop, const char *name);
extern int sm_att_default_value (MOP classop, const char *name, DB_VALUE * value, DB_DEFAULT_EXPR ** default_expr,
				 DB_DEFAULT_EXPR_TYPE ** on_update_expr);

extern int sm_class_check_uniques (MOP classop);
extern BTID *sm_find_index (MOP classop, char **att_names, int num_atts, bool unique_index_only,
			    bool skip_prefix_length_index, BTID * btid);


/* Query processor support functions */
extern int sm_get_class_repid (MOP classop);
extern unsigned int sm_local_schema_version (void);
extern void sm_bump_local_schema_version (void);
extern unsigned int sm_global_schema_version (void);
extern void sm_bump_global_schema_version (void);
extern struct parser_context *sm_virtual_queries (struct parser_context *parser, DB_OBJECT * class_object);


extern int sm_flush_objects (MOP obj);
extern int sm_decache_mop (MOP mop, void *info);
extern int sm_decache_instances_after_query_executed_with_commit (MOP class_mop);
extern int sm_flush_and_decache_objects (MOP obj, int decache);
extern int sm_flush_for_multi_update (MOP class_mop);

/* Workspace & Garbage collection functions */
extern int sm_issystem (SM_CLASS * class_);


/* Trigger support */
extern int sm_class_has_triggers (DB_OBJECT * classop, int *status, DB_TRIGGER_EVENT event_type);

extern int sm_get_trigger_cache (DB_OBJECT * class_, const char *attribute, int class_attribute, void **cache);


extern int sm_invalidate_trigger_cache (DB_OBJECT * classop);

extern int sm_add_trigger (DB_OBJECT * classop, const char *attribute, int class_attribute, DB_OBJECT * trigger);

extern int sm_drop_trigger (DB_OBJECT * classop, const char *attribute, int class_attribute, DB_OBJECT * trigger);

/* Optimized trigger checker for the object manager */
extern int sm_active_triggers (MOP class_mop, SM_CLASS * class_, DB_TRIGGER_EVENT event_type);


/* Attribute & Method descriptors */
extern int sm_get_attribute_descriptor (DB_OBJECT * op, const char *name, int class_attribute, int for_update,
					SM_DESCRIPTOR ** desc);

extern int sm_get_method_descriptor (DB_OBJECT * op, const char *name, int class_method, SM_DESCRIPTOR ** desc);

extern void sm_free_descriptor (SM_DESCRIPTOR * desc);

extern int sm_get_descriptor_component (MOP op, SM_DESCRIPTOR * desc, int for_update, SM_CLASS ** class_ptr,
					SM_COMPONENT ** comp_ptr);

extern void sm_fee_resident_classes_virtual_query_cache (void);

/* Module control */
extern void sm_final (void);
extern void sm_transaction_boundary (void);

extern void sm_create_root (OID * rootclass_oid, HFID * rootclass_hfid);
extern void sm_init (OID * rootclass_oid, HFID * rootclass_hfid);
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
extern int sm_has_text_domain (DB_ATTRIBUTE * attributes, int check_all);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int sm_class_has_unique_constraint (MOBJ classobj, MOP classop, bool check_subclasses, bool * has_unique);
extern int sm_att_unique_constrained (MOP classop, const char *name);
extern int sm_att_in_unique_filter_constraint_predicate (MOP classop, const char *name);
extern int sm_att_fk_constrained (MOP classop, const char *name);

extern bool classobj_is_exist_foreign_key_ref (MOP refop, SM_FOREIGN_KEY_INFO * fk_info);

extern int classobj_put_foreign_key_ref (DB_SEQ ** properties, SM_FOREIGN_KEY_INFO * fk_info);
#if defined (ENABLE_RENAME_CONSTRAINT)
extern int classobj_rename_foreign_key_ref (DB_SEQ ** properties, const BTID * btid, const char *old_name,
					    const char *new_name);
#endif
extern int classobj_drop_foreign_key_ref (DB_SEQ ** properties, const BTID * btid, const char *name);

/* currently this is a private function to be called only by AU_SET_USER */
extern int sc_set_current_schema (MOP user);
extern const char *sc_current_schema_name (void);
extern MOP sc_current_schema_owner (void);
/* Obtain (pointer to) current schema name. */

extern int sm_has_non_null_attribute (SM_ATTRIBUTE ** attrs);
extern void sm_free_function_index_info (SM_FUNCTION_INFO * func_index_info);
extern void sm_free_filter_index_info (SM_PREDICATE_INFO * filter_index_info);

extern int sm_is_global_only_constraint (MOP classmop, SM_CLASS_CONSTRAINT * constraint, int *is_global,
					 SM_TEMPLATE * template_);

#if defined (ENABLE_RENAME_CONSTRAINT)
extern int sm_rename_foreign_key_ref (MOP ref_clsop, const BTID * btid, const char *old_name, const char *new_name);
#endif

extern int sm_find_subclass_in_hierarchy (MOP hierarchy, MOP class_mop, bool * found);
extern bool sm_is_index_visible (SM_CLASS_CONSTRAINT * constraint_list, BTID btid);

SM_DOMAIN *sm_domain_alloc ();
void sm_domain_free (SM_DOMAIN * ptr);
SM_DOMAIN *sm_domain_copy (SM_DOMAIN * ptr);

#endif /* _SCHEMA_MANAGER_H_ */
