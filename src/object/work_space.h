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
 * work_space.h: External definitions for the workspace manager.
 *
 */

#ifndef _WORK_SPACE_H_
#define _WORK_SPACE_H_

#ident "$Id$"

#include <stdio.h>
#include "oid.h"
#include "storage_common.h"
#include "dbtype.h"
#include "dbdef.h"
#include "quick_fit.h"

/*
 * VID_INFO
 *    typedefs for virtual objects.
 */
typedef struct vid_info VID_INFO;
struct vid_info
{				/* Virtual Object Info */
  unsigned int flags;
  DB_VALUE keys;
};

typedef enum vid_info_flag VID_INFO_FLAG;
enum vid_info_flag
{
  VID_BASE = 1,			/* whether the VID is a base instance */
  VID_UPDATABLE = 2,		/* whether the VID is updatable */
  VID_NEW = 4,			/* whether the VID is new */
  VID_INSERTING = 8		/* whether the VID is in an insert */
};


typedef union vid_oid VID_OID;
union vid_oid
{
  VID_INFO *vid_info;		/* Matches OID slot and volume                  */
  OID oid;			/* physical oid */
};

/*
 * DB_OBJECT
 *    This is the primary workspace structure used as a reference to a
 *    persistent database object.  Commonly known as a "memory object
 *    pointer" or MOP.
 *    This is typedefd as DB_OBJECT rather than WS_OBJECT because this
 *    structure is visible at all levels and it makes it easier to pass
 *    these around.
 */

/* typede for DB_OBJECT & MOP is in dbtype.h */

struct db_object
{

  VID_OID oid_info;		/* local copy of the OID or VID pointer */
  struct db_object *class_mop;	/* pointer to class */
  void *object;			/* pointer to attribute values */

  struct db_object *class_link;	/* link for class instances list */
  struct db_object *dirty_link;	/* link for dirty list */
  struct db_object *hash_link;	/* link for workspace hash table */
  struct db_object *commit_link;	/* link for obj to be reset at commit/abort */
  struct db_object *updated_obj;	/* link to the updated object after a flush
					 * operation. In the case of partitioned
					 * classes, this member points to the newly
					 * inserted object in the case of a
					 * partition change */
  struct db_object *error_link;	/* link for error list */
  void *version;		/* versioning information */
  LOCK lock;			/* object lock */

  unsigned char pruning_type;	/* no pruning, prune as partitioned class,
				 * prune as partition */
  unsigned char composition_fetch;	/* set the left-most bit if this MOP */
  /* has been composition fetched and  */
  /* set the prune level into the      */
  /* right-most 7 bits */

  unsigned dirty:1;		/* dirty flag */
  unsigned deleted:1;		/* deleted flag */
  unsigned no_objects:1;	/* optimization for classes */
  unsigned pinned:1;		/* to prevent swapping */
  unsigned reference:1;		/* set if non-object reference mop */
  unsigned is_vid:1;		/* set if oid is vid */
  unsigned is_set:1;		/* temporary kludge for disconnected sets */
  unsigned is_temp:1;		/* set if template MOP (for triggers) */
  unsigned released:1;		/* set by code that knows that an instance can be released, used currently by the loader only */
  unsigned decached:1;		/* set if mop is decached by calling ws_decache function */
  unsigned is_error:1;		/* set if error while flushing dirty object */
};



typedef struct ws_reference WS_REFERENCE;
struct ws_reference
{

  DB_OBJECT *handle;

};

/*
 * WS_REFCOLLECTOR
 */
typedef void (*WS_REFCOLLECTOR) (WS_REFERENCE * refobj);



#define WS_IS_REFMOP(mop) 		((mop)->reference)

#define WS_GET_REFMOP_OBJECT(mop) 	(WS_REFERENCE *)(mop)->object
#define WS_SET_REFMOP_OBJECT(mop, ref) 	(mop)->object = (void *)ref

#define WS_GET_REFMOP_OWNER(mop) 	(mop)->class_link
#define WS_SET_REFMOP_OWNER(mop, cont) 	(mop)->class_link = cont

#define WS_GET_REFMOP_ID(mop) 		(int)(mop)->dirty_link
#define WS_SET_REFMOP_ID(mop, id) 	(mop)->dirty_link = (MOP)id

#define WS_GET_REFMOP_COLLECTOR(mop) 	(WS_REFCOLLECTOR) (mop)->version
#define WS_SET_REFMOP_COLLECTOR(mop, coll) (mop)->class_link = (MOP) coll



typedef struct ws_memoid WS_MEMOID;
typedef struct ws_memoid *MOID;

struct ws_memoid
{
  OID oid;
  MOP pointer;
};

/*
 * MOBJ
 *    Defines a pointer to the memory allocated for an object in the
 *    workspace.  This is not a MOP but rather the space used for the
 *    storage of the object's attributes.
 *    Might need to make this void* if the pointer sizes are different.
 *
 */

typedef char *MOBJ;

/*
 * WS_OBJECT_HEADER
 *    This structure is always found at the top of any block of memory
 *    allocated for storing an object in the workspace.
 *    It contains only the cache coherency number which is used by the
 *    locator to validate the cached representation of the object.
 */

typedef struct ws_object_header WS_OBJECT_HEADER;

struct ws_object_header
{
  int chn;
};

/*
 * MAPFUNC
 *    Shorthand typedef for the function that is passed to the workspace
 *    mapping functions.
 */

typedef int (*MAPFUNC) (MOP mop, void *args);

/*
 * WS_MAP
 *    Performance hack used by the locator.
 *    Used to map over the MOPs in the workspace without calling a mapping
 *    function.  Probably minor performence increase by avoiding some
 *    function calls.
 */

#define WS_MAP(mopvar, stuff) \
  { int __slot__; MOP __mop__; \
    for (__slot__ = 0 ; __slot__ < ws_Mop_table_size ; __slot__++) { \
      for (__mop__ = ws_Mop_table[__slot__].head ; __mop__ != NULL ; \
           __mop__ = __mop__->hash_link) { \
        mopvar = __mop__; \
        stuff \
	} \
    } \
  }

/*
 * IS_CLASS_MOP, IS_ROOT_MOP
 *    Macros for testing types of MOPs.
 *    Could make these functions so we don't need to introduce the
 *    rootclass globals here.
 *    IS_ROOT_MOP is non-zero if the MOP is the rootclass.
 *    IS_CLASS_MOP is non-zero if the class MOP of the object is the
 *    rootclass.
 */

#define IS_CLASS_MOP(mop) (((mop)->class_mop == sm_Root_class_mop) ? 1 : 0)
#define IS_ROOT_MOP(mop) (((mop) == sm_Root_class_mop) ? 1 : 0)

/*
 * WS_STATISTICS
 *    This maintains misc information about the workspace.
 *    It is public primarily for testing purposes.
 */


typedef struct ws_statistics WS_STATISTICS;
struct ws_statistics
{

  int gcs;			/* number of garbage collections */

  int mops_allocated;		/* total number of mops allocated */
  int mops_freed;		/* total reclaimed mops */
  int mops_last_gc;		/* number of mops freed during last gc */

  int refmops_allocated;
  int refmops_freed;
  int refmops_last_gc;

  int bytes_last_gc;
  int bytes_total_gc;

  int dirty_list_emergencies;
  int corruptions;
  int uncached_classes;

  int pinned_cleanings;
  int ignored_class_assignments;

  int set_mops_allocated;
  int set_mops_freed;

  int instance_list_emergencies;

  int temp_mops_allocated;
  int temp_mops_freed;

};


/*
 * WS_PROPERTY
 *    Early implementation of MOP property lists.
 *    Will be more general in the future.
 *    These can be chained and stored in the "version" field of the MOP.
 */

typedef struct ws_property
{
  struct ws_property *next;

  int key;
  DB_BIGINT value;

} WS_PROPERTY;

/*
 * MOP_ITERATOR
 *   Iterator used to walk the MOP table.
 *   Used when it is not possible to access global ws_Mop_table_size
 *   and ws_Mop_table directly.
 */

typedef struct mop_iterator
{
  MOP next;
  unsigned int index;
} MOP_ITERATOR;


/*
 * MOP access macros
 *    Miscellaneous macros that access fields in the MOP structure.
 *    Should use these rather than direct references into the MOP.
 *
 */

#define WS_PUT_COMMIT_MOP(mop)                                     \
  do {                                                              \
    if (!(mop)->commit_link) {                                      \
      (mop)->commit_link = ws_Commit_mops ? ws_Commit_mops : (mop); \
      ws_Commit_mops = (mop);                                       \
     }                                                              \
  } while (0)

#define WS_ISDIRTY(mop) ((mop)->dirty)

#define WS_SET_DIRTY(mop)            \
  do {                               \
    if (!WS_ISDIRTY(mop)) {          \
      (mop)->dirty = 1;              \
      WS_PUT_COMMIT_MOP(mop);        \
      ws_Num_dirty_mop++;            \
    }                                \
  } while (0)

#define WS_RESET_DIRTY(mop)          \
  do {                               \
    if (WS_ISDIRTY(mop)) {           \
      (mop)->dirty = 0;              \
      ws_Num_dirty_mop--;            \
    }                                \
  } while (0)

#define WS_ISMARK_DELETED(mop) ((mop)->deleted)
#define WS_MARKED_DELETED(mop) ((mop)->deleted)

#define WS_SET_DELETED(mop)          \
  do {                               \
    (mop)->deleted = 1;              \
    WS_PUT_COMMIT_MOP(mop);          \
  } while (0)

#define WS_ISVID(mop) ((mop)->is_vid)
/*
 * There are also functions for these, should use the macro since they
 * aren't very complicated
 */
#define WS_OID(mop) \
(WS_ISVID(mop) ? (OID *)&oid_Null_oid : &(mop)->oid_info.oid)
#define WS_REAL_OID(mop)	(&(mop)->oid_info.oid)
#define WS_VID_INFO(mop) 	((mop)->oid_info.vid_info)
#define WS_CLASS_MOP(mop) 	((mop)->class_mop)
#define WS_SET_LOCK(mop, lock)      \
  do {                              \
    (mop)->lock = lock;             \
    if (lock != NULL_LOCK)          \
      WS_PUT_COMMIT_MOP(mop);       \
  } while (0)

#define WS_GET_LOCK(mop)	((mop)->lock)
#define WS_CHN(obj) 		(((WS_OBJECT_HEADER *)(obj))->chn)
#define WS_ISPINNED(mop) 	((mop)->pinned)
#define WS_SET_NO_OBJECTS(mop)      \
  do {                              \
    (mop)->no_objects = 1;          \
    WS_PUT_COMMIT_MOP(mop);         \
  } while (0)

/*
 * WS_MOP_IS_NULL
 *    Tests for logical "NULLness" of the MOP.
 *    This is true if the MOP pointer is NULL, the MOP has been marked as
 *    deleted, or if the MOP has the "NULL OID".
 *    Note that we have to test for non-virtual MOPs before comparing
 *    against the NULL OID.
 */

#define WS_MOP_IS_NULL(mop)                                          \
  (((mop == NULL) || WS_ISMARK_DELETED(mop) ||                       \
    (OID_ISNULL(&(mop)->oid_info.oid) && !(mop)->is_vid)) ? 1 : 0)

/*
 * WS_MOP_GET_COMPOSITION_FETCH
 * WS_MOP_SET_COMPOSITION_FETCH
 *    These macros access the "composition_fetch" right-most 1 bit inside the
 *    MOP. They should be used only by the transaction locator.
 *
 */

#define WS_MOP_COMPOSITION_FETCH_BIT 0x80	/* 1000 0000 */

#define WS_MOP_GET_COMPOSITION_FETCH(mop)                                  \
  ((mop)->composition_fetch & WS_MOP_COMPOSITION_FETCH_BIT)

#define WS_MOP_SET_COMPOSITION_FETCH(mop)                                  \
  do {                                                                     \
    (mop)->composition_fetch |= WS_MOP_COMPOSITION_FETCH_BIT;              \
    WS_PUT_COMMIT_MOP(mop);                                                \
  } while (0)

/*
 * WS_MOP_GET_PRUNE_LEVEL
 * WS_MOP_SET_PRUNE_LEVEL
 *    These macros access the "composition_fetch" left-most 7 bits inside the
 *    MOP.
 *
 */

#define WS_MOP_GET_PRUNE_LEVEL(mop)                                        \
  ((mop)->composition_fetch & ~WS_MOP_COMPOSITION_FETCH_BIT)
#define WS_MOP_SET_PRUNE_LEVEL(mop, value)                                 \
  do {                                                                     \
    if (value <= 0)                                                        \
      (mop)->composition_fetch &= WS_MOP_COMPOSITION_FETCH_BIT; /* zero */ \
    else                                                                   \
      (mop)->composition_fetch |= (value & ~WS_MOP_COMPOSITION_FETCH_BIT); \
  } while (0)

/*
 * WS_MAP constants
 *    These are returned as status codes by the workspace mapping functions.
 */


typedef enum ws_map_status_ WS_MAP_STATUS;
enum ws_map_status_
{
  WS_MAP_CONTINUE = 0,
  WS_MAP_FAIL = 1,
  WS_MAP_STOP = 2,
  WS_MAP_SUCCESS = 3,
  WS_MAP_CONTINUE_ON_ERROR = 4
};

/*
 * WS_FIND_MOP constants
 *    These are returned as status codes by the ws_find function.
 */
typedef enum ws_find_mop_status_ WS_FIND_MOP_STATUS;
enum ws_find_mop_status_
{
  WS_FIND_MOP_DELETED = 0,
  WS_FIND_MOP_NOTDELETED = 1
};

/*
 * WS_MOP_TABLE_ENTRY
 */
typedef struct ws_mop_table_entry WS_MOP_TABLE_ENTRY;
struct ws_mop_table_entry
{
  MOP head;
  MOP tail;
};

/*
 * WORKSPACE GLOBALS
 */
extern WS_MOP_TABLE_ENTRY *ws_Mop_table;
extern unsigned int ws_Mop_table_size;
extern MOP ws_Reference_mops;
extern MOP ws_Set_mops;
extern DB_OBJLIST *ws_Resident_classes;
extern MOP ws_Commit_mops;
extern WS_STATISTICS ws_Stats;
extern int ws_Num_dirty_mop;
extern int ws_Error_ignore_list[-ER_LAST_ERROR];
extern int ws_Error_ignore_count;

/*
 *  WORKSPACE FUNCTIONS
 */
/* memory crisis */
extern void ws_abort_transaction (void);

/* startup, shutdown, reset functions */
extern int ws_init (void);
extern void ws_final (void);
extern void ws_clear (void);
extern void ws_area_init (void);

/* MOP allocation functions */
extern MOP ws_mop (OID * oid, MOP class_mop);
extern MOP ws_vmop (MOP class_mop, int flags, DB_VALUE * keys);
extern bool ws_rehash_vmop (MOP mop, MOBJ class_obj, DB_VALUE * newkey);
#if defined (ENABLE_UNUSED_FUNCTION)
extern MOP ws_new_mop (OID * oid, MOP class_mop);
#endif
extern void ws_perm_oid (MOP mop, OID * newoid);
extern int ws_perm_oid_and_class (MOP mop, OID * new_oid,
				  OID * new_class_oid);
extern int ws_update_oid_and_class (MOP mop, OID * new_oid, OID * new_class);
extern DB_VALUE *ws_keys (MOP vid, unsigned int *flags);

/* Reference mops */

/* so we don't have to include or.h in wspace.c, might have to anyway */
extern MOP ws_find_reference_mop (MOP owner, int attid, WS_REFERENCE * refobj,
				  WS_REFCOLLECTOR collector);
extern void ws_set_reference_mop_owner (MOP refmop, MOP owner, int attid);

/* Set mops */
extern MOP ws_make_set_mop (void *setptr);
extern void ws_free_set_mop (MOP op);

/* Temp MOPs */
extern MOP ws_make_temp_mop (void);
extern void ws_free_temp_mop (MOP op);

/* garbage collection support */
#if defined (ENABLE_UNUSED_FUNCTION)
extern void ws_gc_mop (MOP mop, void (*gcmarker) (MOP));
#endif
extern void ws_gc (void);
extern void ws_gc_enable (void);
extern void ws_gc_disable (void);

/* Dirty list maintenance */
extern void ws_dirty (MOP op);
extern void ws_clean (MOP op);
extern int ws_map_dirty (MAPFUNC function, void *args,
			 bool reverse_dirty_link);
extern void ws_filter_dirty (void);
extern int ws_map_class_dirty (MOP class_op, MAPFUNC function, void *args,
			       bool reverse_dirty_link);

/* Resident instance list maintenance */
extern void ws_set_class (MOP inst, MOP class_mop);
extern int ws_map_class (MOP class_op, MAPFUNC function, void *args);
extern void ws_mark_instances_deleted (MOP class_op);
extern void ws_remove_resident_class (MOP class_op);
extern void ws_intern_instances (MOP class_mop);
extern void ws_release_instance (MOP class_mop);
extern void ws_release_user_instance (MOP mop);
extern void ws_disconnect_deleted_instances (MOP class_mop);

/* object cache */
extern void ws_cache (MOBJ obj, MOP mop, MOP class_mop);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void ws_cache_dirty (MOBJ obj, MOP mop, MOP class_mop);
#endif
extern MOP ws_cache_with_oid (MOBJ obj, OID * oid, MOP class_mop);
extern void ws_decache (MOP mop);
extern void ws_decache_all_instances (MOP classmop);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void ws_vid_clear (void);
#endif

/* Class name cache */
extern MOP ws_find_class (const char *name);
extern void ws_add_classname (MOBJ classobj, MOP classmop,
			      const char *cl_name);
extern void ws_drop_classname (MOBJ classobj);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void ws_reset_classname_cache (void);
#endif

/* MOP accessor functions */
extern OID *ws_identifier (MOP mop);
extern OID *ws_identifier_with_check (MOP mop,
				      const bool check_non_referable);
extern OID *ws_oid (MOP mop);
extern MOP ws_class_mop (MOP mop);
extern int ws_chn (MOBJ obj);
extern LOCK ws_get_lock (MOP mop);
extern void ws_set_lock (MOP mop, LOCK lock);
extern void ws_mark_deleted (MOP mop);

/* pin functions */
extern int ws_pin (MOP mop, int pin);
extern void ws_pin_instance_and_class (MOP obj, int *opin, int *cpin);
extern void ws_restore_pin (MOP obj, int opin, int cpin);

/* Misc info */
extern void ws_cull_mops (void);
extern int ws_find (MOP mop, MOBJ * obj);
extern int ws_mop_compare (MOP mop1, MOP mop2);
extern void ws_class_has_object_dependencies (MOP mop);
extern int ws_class_has_cached_objects (MOP class_mop);
extern bool ws_has_updated (void);

/* MOP mapping functions */
#if defined (CUBRID_DEBUG)
extern int ws_map (MAPFUNC function, void *args);
#endif

/* Transaction management support */
extern void ws_reset_authorization_cache (void);
extern void ws_clear_hints (MOP obj, bool leave_pinned);
extern void ws_clear_all_hints (bool retain_lock);
extern void ws_abort_mops (bool only_unpinned);
extern void ws_decache_allxlockmops_but_norealclasses (void);

/* Debugging functions */
extern void ws_dump (FILE * fpp);
#if defined (CUBRID_DEBUG)
extern void ws_dump_mops (void);
#endif

#if defined (ENABLE_UNUSED_FUNCTION)
extern MOP ws_makemop (int volid, int pageid, int slotid);
extern int ws_count_mops (void);
#endif

/* String utilities */
extern char *ws_copy_string (const char *str);
extern void ws_free_string (const char *str);

/* Property lists */

extern int ws_get_prop (MOP op, int key, DB_BIGINT * value);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int ws_rem_prop (MOP op, int key);
#endif
extern int ws_put_prop (MOP op, int key, DB_BIGINT value);

/*
 * DB_LIST functions
 *    General purpose list functions, don't have to be part of the workspace
 *    except that the coyp functions assume that they can allocate
 *    within the workspace.
 *    Assumes that the first slot in the structure is a "next" pointer.
 *    Technically, this should use imbedded structures but this hasn't
 *    been a problem for any system yet.
 */

typedef void *(*LCOPIER) (void *);
typedef void (*LFREEER) (void *);
typedef int (*LTOTALER) (void *);

extern void ws_list_free (DB_LIST * list, LFREEER function);
extern int ws_list_total (DB_LIST * list, LTOTALER function);
extern int ws_list_remove (DB_LIST ** list, DB_LIST * element);
extern int ws_list_length (DB_LIST * list);
extern void ws_list_append (DB_LIST ** list, DB_LIST * element);
extern DB_LIST *ws_list_copy (DB_LIST * list, LCOPIER copier, LFREEER freeer);
extern DB_LIST *ws_list_nconc (DB_LIST * list1, DB_LIST * list2);

#define WS_LIST_LENGTH(lst) \
  ws_list_length((DB_LIST *)(lst))
#define WS_LIST_FREE(lst, func) \
  ws_list_free((DB_LIST *)(lst), (LFREEER) func)
#define WS_LIST_APPEND(lst, element) \
  ws_list_append((DB_LIST **)(lst), (DB_LIST *)element)
#define WS_LIST_COPY(lst, copier) \
  ws_list_copy((DB_LIST *)(lst), copier, NULL)
#define WS_LIST_REMOVE(lst, element) \
  ws_list_remove((DB_LIST **)(lst), (DB_LIST *)element)
#define WS_LIST_NCONC(lst1, lst2) \
  ws_list_nconc((DB_LIST *)(lst1), (DB_LIST *)lst2)

/*
 * DB_NAMELIST functions
 *    This is an extension of the basic LIST functions that provide
 *    an additional slot for a name string.  Manipulating lists of this
 *    form is extremely common in the schema manager.
 *    Modified 4/20/93 to supply an optional function to perform
 *    the comparison.  This is primarily for case insensitivity.
 */

/*
 * can't use int return code as this must remain compatible with
 * strcmp() and mbs_strcmp.
 */
typedef DB_C_INT (*NLSEARCHER) (const void *, const void *);

extern DB_NAMELIST *nlist_find (DB_NAMELIST * list, const char *name,
				NLSEARCHER fcn);
extern DB_NAMELIST *nlist_remove (DB_NAMELIST ** list, const char *name,
				  NLSEARCHER fcn);
extern int nlist_add (DB_NAMELIST ** list, const char *name,
		      NLSEARCHER fcn, int *added);
extern int nlist_append (DB_NAMELIST ** list, const char *name,
			 NLSEARCHER fcn, int *added);
extern int nlist_find_or_append (DB_NAMELIST ** list, const char *name,
				 NLSEARCHER fcn, int *position);
extern DB_NAMELIST *nlist_filter (DB_NAMELIST ** root, const char *name,
				  NLSEARCHER fcn);
extern DB_NAMELIST *nlist_copy (DB_NAMELIST * list);
extern void nlist_free (DB_NAMELIST * list);

#define NLIST_FIND(lst, name)   nlist_find((DB_NAMELIST *)(lst), name, NULL)

/*
 * DB_OBJLIST functions
 *    This is an extension of the basic LIST functions that provide an
 *    additional slot for a MOP pointer.  Manipulating lists of MOPs is
 *    an extremely common operation in the schema manager.
 *    This has a DB_ prefix because it is visible at the application level.
 */

typedef int (*MOPFILTER) (MOP op, void *args);

extern int ml_find (DB_OBJLIST * list, MOP mop);
extern int ml_add (DB_OBJLIST ** list, MOP mop, int *added);
extern int ml_append (DB_OBJLIST ** list, MOP mop, int *added);
extern int ml_remove (DB_OBJLIST ** list, MOP mop);
extern void ml_free (DB_OBJLIST * list);
extern int ml_size (DB_OBJLIST * list);
extern DB_OBJLIST *ml_copy (DB_OBJLIST * list);
#if defined (ENABLE_UNUSED_FUNCTION)
extern void ml_filter (DB_OBJLIST ** list, MOPFILTER filter, void *args);
#endif

/*
 * These are external MOP lists that are passed beyond the db_ layer into
 * user application space.  They must be allocated in a special region so they
 * are visible as roots for the garbage collector
 */

extern DB_OBJLIST *ml_ext_alloc_link (void);
extern void ml_ext_free_link (DB_OBJLIST * list);
extern DB_OBJLIST *ml_ext_copy (DB_OBJLIST * list);
extern void ml_ext_free (DB_OBJLIST * list);
extern int ml_ext_add (DB_OBJLIST ** list, MOP mop, int *added);
extern int ws_has_dirty_objects (MOP op, int *isvirt);
extern int ws_hide_new_old_trigger_obj (MOP op);
extern void ws_unhide_new_old_trigger_obj (MOP op);

extern bool ws_need_flush (void);

extern int ws_set_ignore_error_list_for_mflush (int error_count,
						int *error_list);

extern void ws_reverse_dirty_link (MOP class_mop);

extern void ws_set_error_into_error_link (MOP mop);
extern MOP ws_get_error_from_error_link (void);
extern void ws_clear_all_errors_of_error_link (void);
#endif /* _WORK_SPACE_H_ */
