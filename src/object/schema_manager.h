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
 * schema_manager.h - External definitions for the schema manager
 */

#ifndef _SCHEMA_MANAGER_H_
#define _SCHEMA_MANAGER_H_

#ident "$Id$"

#include "language_support.h"	/* for international string functions */
#include "storage_common.h"	/* for HFID */
#include "object_domain.h"	/* for TP_DOMAIN */
#include "work_space.h"		/* for MOP */
#include "class_object.h"	/* for SM_CLASS */
#include "schema_template.h"	/* template interface */
#include "trigger_manager.h"	/* for TR_EVENT_TYPE */
#include "dbdef.h"

/*
 * This is NOT the "object" class but rather functions more like
 * the meta-class of class objects.
 * This formerly stored the list of classes that had no super classes,
 * in that way it was kind of like the root "object" of the class
 * hierarchy.  Unfortunately, maintaining this list caused contention
 * problems on the the root object so it was removed.  The list
 * of base classes is now generated manually by examining all classes.
 */
typedef struct root_class ROOT_CLASS;

struct root_class
{
  SM_CLASS_HEADER header;
};

extern ROOT_CLASS sm_Root_class;

extern const char TEXT_CONSTRAINT_PREFIX[];

extern MOP sm_Root_class_mop;
extern HFID *sm_Root_class_hfid;
extern const char *sm_Root_class_name;
extern bool sm_Disable_updating_statistics;

extern int sm_finish_class (SM_TEMPLATE * template_, MOP * classmop);
extern int sm_update_class (SM_TEMPLATE * template_, MOP * classmop);
extern int sm_update_class_auto (SM_TEMPLATE * template_, MOP * classmop);
extern int sm_delete_class_mop (MOP op);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int sm_delete_class (const char *name);
#endif

extern int sm_add_index (MOP classop,
			 const char **attnames,
			 const int *asc_desc,
			 const char *constraint_name, int reverse_index);

extern int sm_get_index (MOP classop, const char *attname, BTID * index);
extern char *sm_produce_constraint_name (const char *class_name,
					 DB_CONSTRAINT_TYPE constraint_type,
					 const char **att_names,
					 const int *asc_desc,
					 const char *given_name);
extern char *sm_produce_constraint_name_mop (MOP classop,
					     DB_CONSTRAINT_TYPE
					     constraint_type,
					     const char **att_names,
					     const char *given_name);
extern char *sm_produce_constraint_name_tmpl (SM_TEMPLATE * tmpl,
					      DB_CONSTRAINT_TYPE
					      constraint_type,
					      const char **att_names,
					      const char *given_name);
extern void sm_free_constraint_name (char *);
extern int sm_add_constraint (MOP classop,
			      DB_CONSTRAINT_TYPE constraint_type,
			      const char *constraint_name,
			      const char **att_names,
			      const int *asc_desc, int class_attributes);
extern int sm_drop_constraint (MOP classop,
			       DB_CONSTRAINT_TYPE constraint_type,
			       const char *constraint_name,
			       const char **att_names, int class_attributes);

/* Misc schema operations */
extern int sm_rename_class (MOP op, const char *new_name);
extern void sm_mark_system_classes (void);
extern int sm_update_all_catalog_statistics (void);
extern int sm_update_catalog_statistics (const char *class_name);
extern int sm_force_write_all_classes (void);
#ifdef SA_MODE
extern void sm_mark_system_class_for_catalog (void);
#endif /* SA_MODE */
extern int sm_mark_system_class (MOP classop, int on_or_off);
extern int sm_is_system_class (MOP op);
extern bool sm_is_reuse_oid_class (MOP op);
extern int sm_set_class_flag (MOP classop, SM_CLASS_FLAG flag, int onoff);
extern int sm_get_class_flag (MOP op, SM_CLASS_FLAG flag);
extern int sm_destroy_representations (MOP op);

extern void sm_add_static_method (const char *name, void (*function) ());
extern void sm_delete_static_method (const char *name);
extern void sm_flush_static_methods (void);

extern int sm_link_method (SM_CLASS * class_, SM_METHOD * method);
extern int sm_prelink_methods (DB_OBJLIST * classes);
extern int sm_force_method_link (MOP obj);


extern char *sm_get_method_source_file (MOP obj, const char *name);


/* Utility functions */
extern int sm_check_name (const char *name);
extern SM_NAME_SPACE sm_resolution_space (SM_NAME_SPACE name_space);

/* Class location functions */
extern MOP sm_get_class (MOP obj);
extern SM_CLASS_TYPE sm_get_class_type (SM_CLASS * class_);

extern DB_OBJLIST *sm_fetch_all_classes (int external_list,
					 DB_FETCH_MODE purpose);
extern DB_OBJLIST *sm_fetch_all_base_classes (int external_list,
					      DB_FETCH_MODE purpose);
extern DB_OBJLIST *sm_fetch_all_objects (DB_OBJECT * op,
					 DB_FETCH_MODE purpose);

/* Domain maintanance */
extern int sm_filter_domain (TP_DOMAIN * domain);
extern int sm_check_class_domain (TP_DOMAIN * domain, MOP class_);
extern int sm_check_object_domain (TP_DOMAIN * domain, MOP object);
extern int sm_coerce_object_domain (TP_DOMAIN * domain, MOP object,
				    MOP * dest_object);

/* Extra cached state */
extern int sm_clean_class (MOP classmop, SM_CLASS * class_);

/* Statistics functions */
extern SM_CLASS *sm_get_class_with_statistics (MOP classop);
extern CLASS_STATS *sm_get_statistics_force (MOP classop);
extern int sm_update_statistics (MOP classop);
extern int sm_update_all_statistics (void);

/* Misc information functions */
extern const char *sm_class_name (MOP op);

extern int sm_is_subclass (MOP classmop, MOP supermop);
#if defined(ENABLE_UNUSED_FUNCTION)
extern int sm_object_size (MOP op);
#endif
extern int sm_object_size_quick (SM_CLASS * class_, MOBJ obj);
extern SM_CLASS_CONSTRAINT *sm_class_constraints (MOP classop);

/* Locator support functions */
extern const char *sm_classobj_name (MOBJ classobj);
extern HFID *sm_heap (MOBJ clobj);
extern HFID *sm_get_heap (MOP classmop);
extern bool sm_has_indexes (MOBJ class_);

/* Interpter support functions */
extern void sm_downcase_name (const char *name, char *buf, int maxlen);
extern MOP sm_find_class (const char *name);

extern const char *sm_get_att_name (MOP classop, int id);
extern int sm_att_id (MOP classop, const char *name);
extern DB_TYPE sm_att_type_id (MOP classop, const char *name);

extern MOP sm_att_class (MOP classop, const char *name);
extern int sm_att_info (MOP classop, const char *name, int *idp,
			TP_DOMAIN ** domainp, int *sharedp, int class_attr);
extern int sm_att_constrained (MOP classop, const char *name,
			       SM_ATTRIBUTE_FLAG cons);

extern int sm_class_check_uniques (MOP classop);
extern BTID *sm_find_index (MOP classop, char **att_names,
			    int num_atts,
			    bool unique_index_only, BTID * btid);


/* Query processor support functions */
extern int sm_get_class_repid (MOP classop);
extern unsigned int sm_schema_version (void);
extern void sm_bump_schema_version (void);
extern struct parser_context *sm_virtual_queries (DB_OBJECT * class_object);


extern int sm_flush_objects (MOP obj);
extern int sm_flush_and_decache_objects (MOP obj, int decache);
extern int sm_flush_for_multi_update (MOP class_mop);

/* Workspace & Garbage collection functions */
extern int sm_issystem (SM_CLASS * class_);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void sm_gc_class (MOP mop, void (*gcmarker) (MOP));
extern void sm_gc_object (MOP mop, void (*gcmarker) (MOP));
#endif

/* Internationalization hack for csql */
extern int sm_set_inhibit_identifier_check (int inhibit);

/* Trigger support */
extern int sm_class_has_triggers (DB_OBJECT * classop, int *status,
				  DB_TRIGGER_EVENT event_type);

extern int sm_get_trigger_cache (DB_OBJECT * class_, const char *attribute,
				 int class_attribute, void **cache);


extern int sm_invalidate_trigger_cache (DB_OBJECT * classop);

extern int sm_add_trigger (DB_OBJECT * classop,
			   const char *attribute,
			   int class_attribute, DB_OBJECT * trigger);

extern int sm_drop_trigger (DB_OBJECT * classop,
			    const char *attribute,
			    int class_attribute, DB_OBJECT * trigger);

/* Optimized trigger checker for the object manager */
extern int sm_active_triggers (SM_CLASS * class_,
			       DB_TRIGGER_EVENT event_type);


/* Attribute & Method descriptors */
extern int sm_get_attribute_descriptor (DB_OBJECT * op, const char *name,
					int class_attribute,
					int for_update,
					SM_DESCRIPTOR ** desc);

extern int sm_get_method_descriptor (DB_OBJECT * op, const char *name,
				     int class_method, SM_DESCRIPTOR ** desc);

extern void sm_free_descriptor (SM_DESCRIPTOR * desc);

extern int sm_get_descriptor_component (MOP op, SM_DESCRIPTOR * desc,
					int for_update,
					SM_CLASS ** class_ptr,
					SM_COMPONENT ** comp_ptr);


/* Module control */
extern void sm_final (void);
extern void sm_transaction_boundary (void);

extern void sm_create_root (OID * rootclass_oid, HFID * rootclass_hfid);
extern void sm_init (OID * rootclass_oid, HFID * rootclass_hfid);
#if defined (ENABLE_UNUSED_FUNCTION)	/* to disable TEXT */
extern int sm_has_text_domain (DB_ATTRIBUTE * attributes, int check_all);
#endif /* ENABLE_UNUSED_FUNCTION */
extern int sm_att_unique_constrained (MOP classop, const char *name);
extern int sm_is_att_fk_cache (MOP classop, const char *name);

extern bool classobj_is_exist_foreign_key_ref (MOP refop,
					       SM_FOREIGN_KEY_INFO * fk_info);

extern int classobj_put_foreign_key_ref (DB_SEQ ** properties,
					 SM_FOREIGN_KEY_INFO * fk_info);
extern int classobj_drop_foreign_key_ref (DB_SEQ ** properties, BTID * btid);

/* currently this is a private function to be called only by AU_SET_USER */
extern int sc_set_current_schema (MOP user);
/* Obtain (pointer to) current schema name.                            */

#endif /* _SCHEMA_MANAGER_H_ */
