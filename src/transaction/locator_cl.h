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
 * locator_cl.h - Transaction object locator interfacee (at client)
 *
 * Note: See .c file for overview and description of the interface functions.
 */

#ifndef _LOCATOR_CL_H_
#define _LOCATOR_CL_H_

#ident "$Id$"

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include "config.h"
#include "error_manager.h"
#include "work_space.h"
#include "storage_common.h"
#include "locator.h"
#include "replication.h"

// forward declarations
struct log_lsa;

#define ONE_MFLUSH    true
#define MANY_MFLUSHES false

#define DECACHE       true
#define DONT_DECACHE  false

#define OID_BATCH_SIZE  2000

#define LC_INSERT_OPERATION_TYPE(p) \
        ((p)==DB_NOT_PARTITIONED_CLASS ? LC_FLUSH_INSERT :   \
         ((p)==DB_PARTITIONED_CLASS ? LC_FLUSH_INSERT_PRUNE :\
				      LC_FLUSH_INSERT_PRUNE_VERIFY))

#define LC_UPDATE_OPERATION_TYPE(p) \
        ((p)==DB_NOT_PARTITIONED_CLASS ? LC_FLUSH_UPDATE :   \
         ((p)==DB_PARTITIONED_CLASS ? LC_FLUSH_UPDATE_PRUNE :\
				      LC_FLUSH_UPDATE_PRUNE_VERIFY))

typedef struct list_mops LIST_MOPS;
struct list_mops
{
  int num;
  MOP mops[1];
};

typedef enum
{
  LC_OBJECT,			/* Don't know if it is an instance or a class */
  LC_CLASS,			/* A class */
  LC_INSTANCE			/* An instance */
} LC_OBJTYPE;

extern bool locator_is_root (MOP mop);
extern int locator_is_class (MOP mop, DB_FETCH_MODE hint_purpose);
extern LOCK locator_fetch_mode_to_lock (DB_FETCH_MODE purpose, LC_OBJTYPE type,
					LC_FETCH_VERSION_TYPE fetch_version_type);
extern int locator_get_cache_coherency_number (MOP mop);
extern MOBJ locator_fetch_object (MOP mop, DB_FETCH_MODE purpose, LC_FETCH_VERSION_TYPE fetch_version_type);
extern MOBJ locator_fetch_class (MOP class_mop, DB_FETCH_MODE purpose);
extern MOBJ locator_fetch_class_of_instance (MOP inst_mop, MOP * class_mop, DB_FETCH_MODE purpose);
extern MOBJ locator_fetch_instance (MOP mop, DB_FETCH_MODE purpose, LC_FETCH_VERSION_TYPE fetch_version_type);
extern MOBJ locator_fetch_set (int num_mops, MOP * mop_set, DB_FETCH_MODE inst_purpose, DB_FETCH_MODE class_purpose,
			       int quit_on_errors);
extern MOBJ locator_fetch_nested (MOP mop, DB_FETCH_MODE purpose, int prune_level, int quit_on_errors);
extern int locator_flush_class (MOP class_mop);
extern int locator_flush_instance (MOP mop);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int locator_flush_and_decache_instance (MOP mop);
#endif
extern int locator_flush_all_instances (MOP class_mop, bool decache);
extern int locator_flush_for_multi_update (MOP class_mop);
extern int locator_all_flush (void);
extern int locator_repl_flush_all (void);
extern MOP locator_add_class (MOBJ classobj, const char *classname);
extern MOP locator_add_instance (MOBJ instance, MOP class_mop);
extern MOP locator_add_root (OID * root_oid, MOBJ class_root);
extern int locator_remove_class (MOP class_mop);
extern void locator_remove_instance (MOP mop);
extern MOBJ locator_update_instance (MOP mop);
extern MOBJ locator_update_class (MOP mop);
extern int locator_update_tree_classes (MOP * classes_mop_set, int num_classes);
extern MOBJ locator_prepare_rename_class (MOP class_mop, const char *old_classname, const char *new_classname);
extern OID *locator_assign_permanent_oid (MOP mop);
extern MOP locator_find_class (const char *classname);
extern MOP locator_find_class_with_purpose (const char *classname, bool for_update);

#if defined (ENABLE_UNUSED_FUNCTION)
extern LC_FIND_CLASSNAME locator_find_query_class (const char *classname, DB_FETCH_MODE purpose, MOP * class_mop);
#endif
extern LC_FIND_CLASSNAME locator_lockhint_classes (int num_classes, const char **many_classnames, LOCK * many_locks,
						   int *need_subclasses, LC_PREFETCH_FLAGS * flags, int quit_on_errors,
						   LOCK lock_rr_tran);
extern int locator_does_exist_object (MOP mop, DB_FETCH_MODE purpose);
extern LIST_MOPS *locator_get_all_mops (MOP class_mop, DB_FETCH_MODE class_purpose,
					LC_FETCH_VERSION_TYPE * force_fetch_version_type);
extern LIST_MOPS *locator_get_all_class_mops (DB_FETCH_MODE purpose, int (*fun) (MOBJ class_obj));
#if defined (ENABLE_UNUSED_FUNCTION)
extern LIST_MOPS *locator_get_all_nested_mops (MOP mop, int prune_level, DB_FETCH_MODE inst_purpose);
#endif
extern void locator_free_list_mops (LIST_MOPS * mops);
extern void locator_synch_isolation_incons (void);
extern void locator_set_sig_interrupt (int set);
extern int locator_get_sig_interrupt ();

extern MOBJ locator_create_heap_if_needed (MOP class_mop, bool reuse_oid);
extern MOBJ locator_has_heap (MOP class_mop);

typedef void (*LC_OIDMAP_CALLBACK) (LC_OIDMAP * map);

extern LC_OIDMAP *locator_add_oidset_object (LC_OIDSET * oidset, DB_OBJECT * obj);
extern int locator_assign_oidset (LC_OIDSET * oidset, LC_OIDMAP_CALLBACK callback);
extern int locator_assign_all_permanent_oids (void);

extern int locator_decache_all_lock_instances (MOP class_mop);

extern int locator_get_append_lsa (struct log_lsa *lsa);
extern int locator_flush_replication_info (REPL_INFO * repl_info);

extern LC_FIND_CLASSNAME locator_reserve_class_name (const char *class_name, OID * class_oid);

#endif /* _LOCATOR_CL_H_ */
