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
 * work_space.h: External definitions for the workspace manager.
 *
 */

#ifndef _WORK_SPACE_H_
#define _WORK_SPACE_H_

#ident "$Id$"

#include <stdio.h>
#include "oid.h"
#include "storage_common.h"
#include "quick_fit.h"
#include "locator.h"
#include "dbtype_def.h"

#if defined (SERVER_MODE)
#error does not belong to server
#endif // SERVER_MODE

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

enum vid_info_flag
{
  VID_BASE = 1,			/* whether the VID is a base instance */
  VID_UPDATABLE = 2,		/* whether the VID is updatable */
  VID_NEW = 4,			/* whether the VID is new */
  VID_INSERTING = 8		/* whether the VID is in an insert */
};
typedef enum vid_info_flag VID_INFO_FLAG;

typedef union vid_oid VID_OID;
union vid_oid
{
  VID_INFO *vid_info;		/* Matches OID slot and volume */
  OID oid;			/* physical oid */
};

typedef struct ws_repl_flush_err WS_REPL_FLUSH_ERR;
struct ws_repl_flush_err
{
  struct ws_repl_flush_err *error_link;
  OID class_oid;
  int operation;
  int error_code;
  char *error_msg;
  DB_VALUE pkey_value;
};

typedef struct ws_repl_obj WS_REPL_OBJ;
struct ws_repl_obj
{
  struct ws_repl_obj *next;
  OID class_oid;
  char *packed_pkey_value;
  int packed_pkey_value_length;
  bool has_index;
  int operation;
  RECDES *recdes;
};

typedef struct ws_repl_list WS_REPL_LIST;
struct ws_repl_list
{
  WS_REPL_OBJ *head;
  WS_REPL_OBJ *tail;
  int num_items;
};

typedef struct ws_value_list WS_VALUE_LIST;
struct ws_value_list
{
  struct ws_value_list *next;
  DB_VALUE *val;
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
  struct db_object *class_mop;	/* pointer to class mop */
  /* Do not ever set this to NULL without removing object from class link. */
  void *object;			/* pointer to attribute values */

  struct db_object *class_link;	/* link for class instances list */
  /* Careful whenever looping through object using class_link to save it and advance using this saved class link if the
   * current mop can be removed from class. */
  struct db_object *dirty_link;	/* link for dirty list */
  struct db_object *hash_link;	/* link for workspace hash table */
  /* Careful whenever looping through objects using hash_link to save it and advance using this saved hash link if the
   * current mop can be removed or relocated in hash table. */
  struct db_object *commit_link;	/* link for obj to be reset at commit/abort */
  WS_VALUE_LIST *label_value_list;	/* label value list */
  LOCK lock;			/* object lock */
  unsigned int mvcc_snapshot_version;	/* The snapshot version at the time mop object is fetched and cached.
					 * Used only when MVCC is enabled. */

  unsigned char pruning_type;	/* no pruning, prune as partitioned class, prune as partition */
  unsigned char composition_fetch;	/* set the left-most bit if this MOP */
  /* has been composition fetched and */
  /* set the prune level into the */
  /* right-most 7 bits */

  unsigned dirty:1;		/* dirty flag */
  unsigned deleted:1;		/* deleted flag */
  unsigned no_objects:1;	/* optimization for classes */
  unsigned pinned:1;		/* to prevent swapping */
  unsigned is_vid:1;		/* set if oid is vid */
  unsigned is_temp:1;		/* set if template MOP (for triggers) */
  unsigned released:1;		/* set by code that knows that an instance can be released, used currently by the
				 * loader only */
  unsigned decached:1;		/* set if mop is decached by calling ws_decache function */
  unsigned trigger_involved:1;	/* set if mop is involved in trigger, it is used only for cdc */
};


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
  int mops_allocated;		/* total number of mops allocated */
  int mops_freed;		/* total reclaimed mops */

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
 * MOP access macros
 *    Miscellaneous macros that access fields in the MOP structure.
 *    Should use these rather than direct references into the MOP.
 *
 */

#define WS_PUT_COMMIT_MOP(mop) \
  do \
    { \
      if (!(mop)->commit_link) \
        { \
          (mop)->commit_link = ws_Commit_mops ? ws_Commit_mops : (mop); \
          ws_Commit_mops = (mop); \
        } \
    } \
  while (0)

#define WS_ISDIRTY(mop) (ws_is_dirty (mop))

#define WS_SET_DIRTY(mop) \
  do \
    { \
      if (!WS_ISDIRTY(mop)) \
        { \
          (mop)->dirty = 1; \
          WS_PUT_COMMIT_MOP(mop); \
          ws_Num_dirty_mop++; \
        } \
    } \
  while (0)

#define WS_RESET_DIRTY(mop) \
  do \
    { \
      if (WS_ISDIRTY(mop)) \
        { \
          (mop)->dirty = 0; \
          ws_Num_dirty_mop--; \
        } \
    } \
  while (0)

#define WS_IS_DELETED(mop) (ws_is_deleted (mop))

#define WS_SET_DELETED(mop) (ws_set_deleted (mop))

#define WS_ISVID(mop) ((mop)->is_vid)
/*
 * There are also functions for these, should use the macro since they
 * aren't very complicated
 */
#define WS_OID(mop) (WS_ISVID (mop) ? (OID *) (&oid_Null_oid) : &(mop)->oid_info.oid)
#define WS_REAL_OID(mop) (&(mop)->oid_info.oid)
#define WS_VID_INFO(mop) ((mop)->oid_info.vid_info)
#define WS_CLASS_MOP(mop) ((mop)->class_mop)
#define WS_SET_LOCK(mop, lock) \
  do \
    { \
      (mop)->lock = lock; \
      if (lock != NULL_LOCK) \
        { \
          WS_PUT_COMMIT_MOP (mop); \
        } \
    } \
  while (0)

#define WS_GET_LOCK(mop) ((mop)->lock)
#define WS_CHN(obj) (((WS_OBJECT_HEADER *) (obj))->chn)
#define WS_ISPINNED(mop) ((mop)->pinned)
#define WS_SET_NO_OBJECTS(mop) \
  do \
    { \
      (mop)->no_objects = 1; \
      WS_PUT_COMMIT_MOP (mop); \
    } \
  while (0)

/*
 * WS_MOP_IS_NULL
 *    Tests for logical "NULLness" of the MOP.
 *    This is true if the MOP pointer is NULL, the MOP has been marked as
 *    deleted, or if the MOP has the "NULL OID".
 *    Note that we have to test for non-virtual MOPs before comparing
 *    against the NULL OID.
 */

#define WS_MOP_IS_NULL(mop) \
  (((mop == NULL) || WS_IS_DELETED (mop) || (OID_ISNULL (&(mop)->oid_info.oid) && !(mop)->is_vid)) ? 1 : 0)

/*
 * WS_MOP_GET_COMPOSITION_FETCH
 * WS_MOP_SET_COMPOSITION_FETCH
 *    These macros access the "composition_fetch" right-most 1 bit inside the
 *    MOP. They should be used only by the transaction locator.
 *
 */

#define WS_MOP_COMPOSITION_FETCH_BIT 0x80	/* 1000 0000 */

#define WS_MOP_GET_COMPOSITION_FETCH(mop) \
  ((mop)->composition_fetch & WS_MOP_COMPOSITION_FETCH_BIT)

#define WS_MOP_SET_COMPOSITION_FETCH(mop) \
  do \
    { \
      (mop)->composition_fetch |= WS_MOP_COMPOSITION_FETCH_BIT; \
      WS_PUT_COMMIT_MOP (mop); \
    } \
  while (0)

/*
 * WS_MOP_GET_PRUNE_LEVEL
 * WS_MOP_SET_PRUNE_LEVEL
 *    These macros access the "composition_fetch" left-most 7 bits inside the
 *    MOP.
 *
 */

#define WS_MOP_GET_PRUNE_LEVEL(mop) ((mop)->composition_fetch & ~WS_MOP_COMPOSITION_FETCH_BIT)
#define WS_MOP_SET_PRUNE_LEVEL(mop, value) \
  do \
    { \
      if (value <= 0) \
        { \
          (mop)->composition_fetch &= WS_MOP_COMPOSITION_FETCH_BIT; /* zero */ \
        } \
      else \
        { \
          (mop)->composition_fetch |= (value & ~WS_MOP_COMPOSITION_FETCH_BIT); \
        } \
    } \
  while (0)

/* free_and_init routine */
#define ws_free_string_and_init(str) \
  do \
    { \
      ws_free_string ((str)); \
      (str) = NULL; \
    } \
  while (0)

#define ml_free_and_init(list) \
  do \
    { \
      ml_free ((list)); \
      (list) = NULL; \
    } \
  while (0)

#define ws_list_free_and_init(list, function) \
  do \
    { \
      ws_list_free ((DB_LIST *)(list), (LFREEER)(function)); \
      (list) = NULL; \
    } \
  while (0)

#define WS_IS_TRIGGER_INVOLVED(mop) ((mop)->trigger_involved)

#define WS_SET_TRIGGER_INVOLVED(mop) \
  do \
    { \
      if (ws_is_trigger_involved ()) \
        { \
          (mop)->trigger_involved = 1; \
        } \
      else \
        { \
          (mop)->trigger_involved = 0; \
        } \
    } \
  while (0)

/*
 * WS_MAP constants
 *    These are returned as status codes by the workspace mapping functions.
 */


enum ws_map_status
{
  WS_MAP_CONTINUE = 0,
  WS_MAP_FAIL = 1,
  WS_MAP_STOP = 2,
  WS_MAP_SUCCESS = 3
};
typedef enum ws_map_status WS_MAP_STATUS;

/*
 * WS_FIND_MOP constants
 *    These are returned as status codes by the ws_find function.
 */
enum ws_find_mop_status
{
  WS_FIND_MOP_DELETED = 0,
  WS_FIND_MOP_NOTDELETED = 1
};
typedef enum ws_find_mop_status WS_FIND_MOP_STATUS;

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
extern int ws_area_init (void);
extern void ws_area_final (void);

/* MOP allocation functions */
extern MOP ws_mop (const OID * oid, MOP class_mop);
extern MOP ws_mop_if_exists (OID * oid);
extern MOP ws_vmop (MOP class_mop, int flags, DB_VALUE * keys);
extern bool ws_rehash_vmop (MOP mop, MOBJ class_obj, DB_VALUE * newkey);
extern MOP ws_new_mop (OID * oid, MOP class_mop);
extern void ws_update_oid (MOP mop, OID * newoid);
extern int ws_update_oid_and_class (MOP mop, OID * new_oid, OID * new_class_oid);
extern DB_VALUE *ws_keys (MOP vid, unsigned int *flags);

/* Temp MOPs */
extern MOP ws_make_temp_mop (void);
extern void ws_free_temp_mop (MOP op);

/* Dirty list maintenance */
extern void ws_dirty (MOP op);
extern int ws_is_dirty (MOP mop);
extern int ws_is_deleted (MOP mop);
extern void ws_set_deleted (MOP mop);
extern void ws_clean (MOP op);
extern int ws_map_dirty (MAPFUNC function, void *args);
extern void ws_filter_dirty (void);
extern int ws_map_class_dirty (MOP class_op, MAPFUNC function, void *args);

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
extern MOP ws_cache_with_oid (MOBJ obj, OID * oid, MOP class_mop);
extern void ws_decache (MOP mop);
extern void ws_decache_all_instances (MOP classmop);

/* Class name cache */
extern MOP ws_find_class (const char *name);
extern void ws_add_classname (MOBJ classobj, MOP classmop, const char *cl_name);
extern void ws_drop_classname (MOBJ classobj);

/* MOP accessor functions */
extern OID *ws_identifier (MOP mop);
extern OID *ws_identifier_with_check (MOP mop, const bool check_non_referable);
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

/* String utilities */
extern char *ws_copy_string (const char *str);
extern void ws_free_string (const char *str);

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
  ws_list_length ((DB_LIST *) (lst))
#define WS_LIST_FREE(lst, func) \
  ws_list_free ((DB_LIST *) (lst), (LFREEER) func)
#define WS_LIST_APPEND(lst, element) \
  ws_list_append ((DB_LIST **) (lst), (DB_LIST *) element)
#define WS_LIST_COPY(lst, copier) \
  ws_list_copy ((DB_LIST *) (lst), copier, NULL)
#define WS_LIST_REMOVE(lst, element) \
  ws_list_remove ((DB_LIST **) (lst), (DB_LIST *) element)
#define WS_LIST_NCONC(lst1, lst2) \
  ws_list_nconc ((DB_LIST *) (lst1), (DB_LIST *) lst2)

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

extern DB_NAMELIST *nlist_find (DB_NAMELIST * list, const char *name, NLSEARCHER fcn);
extern DB_NAMELIST *nlist_remove (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn);
extern int nlist_add (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn, int *added);
extern int nlist_append (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn, int *added);
extern int nlist_find_or_append (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn, int *position);
extern DB_NAMELIST *nlist_filter (DB_NAMELIST ** root, const char *name, NLSEARCHER fcn);
extern DB_NAMELIST *nlist_copy (DB_NAMELIST * list);
extern void nlist_free (DB_NAMELIST * list);

#define NLIST_FIND(lst, name) nlist_find ((DB_NAMELIST *) (lst), name, NULL)

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

extern int ws_set_ignore_error_list_for_mflush (int error_count, int *error_list);

extern int ws_add_to_repl_obj_list (OID * class_oid, char *packed_pkey_value, int packed_pkey_value_length,
				    RECDES * recdes, int operation, bool has_index);
extern void ws_init_repl_objs (void);
extern void ws_clear_all_repl_objs (void);
extern void ws_free_repl_obj (WS_REPL_OBJ * obj);
extern WS_REPL_OBJ *ws_get_repl_obj_from_list (void);

extern void ws_set_repl_error_into_error_link (LC_COPYAREA_ONEOBJ * obj, char *content_ptr);

extern WS_REPL_FLUSH_ERR *ws_get_repl_error_from_error_link (void);
extern void ws_clear_all_repl_errors_of_error_link (void);
extern void ws_free_repl_flush_error (WS_REPL_FLUSH_ERR * flush_err);

extern unsigned int ws_get_mvcc_snapshot_version (void);
extern void ws_increment_mvcc_snapshot_version (void);
extern bool ws_is_mop_fetched_with_current_snapshot (MOP mop);
extern void ws_set_mop_fetched_with_current_snapshot (MOP mop);

extern bool ws_is_same_object (MOP mop1, MOP mop2);
extern void ws_move_label_value_list (MOP dest_mop, MOP src_mop);
extern void ws_remove_label_value_from_mop (MOP mop, DB_VALUE * val);
extern int ws_add_label_value_to_mop (MOP mop, DB_VALUE * val);
extern void ws_clean_label_value_list (MOP mop);

extern bool ws_is_trigger_involved ();

#endif /* _WORK_SPACE_H_ */
