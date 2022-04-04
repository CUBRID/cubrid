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
 * locator.h - transaction object locator (at client and server)
 *
 */

#ifndef _LOCATOR_H_
#define _LOCATOR_H_

#ident "$Id$"

#include "object_representation_constants.h"
#include "oid.h"
#include "storage_common.h"
#include "thread_compat.hpp"

#define LC_AREA_ONEOBJ_PACKED_SIZE (OR_INT_SIZE * 4 + \
                                    OR_HFID_SIZE + \
                                    OR_OID_SIZE * 2)

#define LC_MANYOBJS_PTR_IN_COPYAREA(copy_areaptr) \
  ((LC_COPYAREA_MANYOBJS *) ((char *)(copy_areaptr)->mem + \
                                    (copy_areaptr)->length - \
                                    DB_SIZEOF(LC_COPYAREA_MANYOBJS)))

#define LC_START_ONEOBJ_PTR_IN_COPYAREA(manyobjs_ptr) (&(manyobjs_ptr)->objs)
#define LC_LAST_ONEOBJ_PTR_IN_COPYAREA(manyobjs_ptr) \
  (&(manyobjs_ptr)->objs - ((manyobjs_ptr)->num_objs - 1))

#define LC_NEXT_ONEOBJ_PTR_IN_COPYAREA(oneobj_ptr)    ((oneobj_ptr) - 1)
#define LC_PRIOR_ONEOBJ_PTR_IN_COPYAREA(oneobj_ptr)   ((oneobj_ptr) + 1)

#define LC_FIND_ONEOBJ_PTR_IN_COPYAREA(manyobjs_ptr, obj_num) \
  (&(manyobjs_ptr)->objs - (obj_num))

#define LC_RECDES_TO_GET_ONEOBJ(copy_area_ptr, oneobj_ptr, recdes_ptr) \
  do { \
    (recdes_ptr)->data = (char *) ((copy_area_ptr)->mem + \
                                   (oneobj_ptr)->offset); \
    (recdes_ptr)->length = (recdes_ptr)->area_size = (oneobj_ptr)->length; \
    if (!OID_IS_ROOTOID (&((oneobj_ptr)->class_oid))) \
      {	\
	(recdes_ptr)->area_size += \
	(OR_MVCC_MAX_HEADER_SIZE - OR_MVCC_INSERT_HEADER_SIZE); \
      }	\
  } while (0)

#define LC_REPL_RECDES_FOR_ONEOBJ(copy_area_ptr, oneobj_ptr, key_length, recdes_ptr)    \
  do {                                                                                  \
      (recdes_ptr)->data = (char *)((copy_area_ptr)->mem                                \
                                    + (oneobj_ptr)->offset + (key_length));             \
      (recdes_ptr)->length = (recdes_ptr)->area_size = (oneobj_ptr)->length             \
                                    - (key_length);                                     \
  } while (0)

#define LC_RECDES_IN_COPYAREA(copy_area_ptr, recdes_ptr) \
  do { \
    (recdes_ptr)->data = (copy_area_ptr)->mem; \
    (recdes_ptr)->area_size = \
      (copy_area_ptr)->length - DB_SIZEOF(LC_COPYAREA_MANYOBJS); \
  } while (0)

#define LC_REQOBJ_PACKED_SIZE (OR_OID_SIZE + OR_INT_SIZE * 2)
#define LC_CLASS_OF_REQOBJ_PACKED_SIZE (OR_OID_SIZE + OR_INT_SIZE)

#define LC_LOCKSET_PACKED_SIZE(req) \
  (OR_INT_SIZE * 9 + \
   LC_CLASS_OF_REQOBJ_PACKED_SIZE * req->max_reqobjs + \
   LC_REQOBJ_PACKED_SIZE * req->max_reqobjs)

#define LC_LOCKHINT_CLASS_PACKED_SIZE (OR_OID_SIZE + OR_INT_SIZE * 3)

#define LC_LOCKHINT_PACKED_SIZE(lockhint) \
  (OR_INT_SIZE * 4 + \
   LC_LOCKHINT_CLASS_PACKED_SIZE * lockhint->max_classes)

#define LC_COPY_ONEOBJ(new_obj, old_obj)                            \
  do {                                                              \
      (new_obj)->operation = (old_obj)->operation;                  \
      (new_obj)->flag = (old_obj)->flag;                            \
      HFID_COPY(&((new_obj)->hfid), &((old_obj)->hfid));            \
      COPY_OID(&((new_obj)->class_oid), &((old_obj)->class_oid));   \
      COPY_OID(&((new_obj)->oid), &((old_obj)->oid));               \
      (new_obj)->length = (old_obj)->length;                        \
      (new_obj)->offset = (old_obj)->offset;                        \
  } while(0)

typedef enum
{
  LC_FETCH,
  LC_FETCH_DELETED,
  LC_FETCH_DECACHE_LOCK,
  LC_FLUSH_INSERT,
  LC_FLUSH_INSERT_PRUNE,
  LC_FLUSH_INSERT_PRUNE_VERIFY,
  LC_FLUSH_DELETE,
  LC_FLUSH_UPDATE,
  LC_FLUSH_UPDATE_PRUNE,
  LC_FLUSH_UPDATE_PRUNE_VERIFY,
  LC_FETCH_VERIFY_CHN
} LC_COPYAREA_OPERATION;

#define LC_IS_FLUSH_INSERT(operation) \
  (operation == LC_FLUSH_INSERT || operation == LC_FLUSH_INSERT_PRUNE \
   || operation == LC_FLUSH_INSERT_PRUNE_VERIFY)

#define LC_IS_FLUSH_UPDATE(operation) \
  (operation == LC_FLUSH_UPDATE || operation == LC_FLUSH_UPDATE_PRUNE \
   || operation == LC_FLUSH_UPDATE_PRUNE_VERIFY)

/*
 *   Currently, classes does not have versions. So, fetching current, MVCC or
 * dirty version lead to same result when classes are fetched. However,
 * using LC_FETCH_CURRENT_VERSION is recommended in this case.
 *
 *   When need to read an instance (DB_FETCH_READ) for SELECT purpose, use MVCC
 * version type. This means visible version for current transaction, without
 * locking. In this way, we respect the rule "do not lock instances at select"
 * (reader does not block writer and writer does not block reader)
 *
 *   When need to read an instance (DB_FETCH_READ) for UPDATE purpose, use DIRTY
 * version type. In this case, the updatable version will be S-locked, if.
 * exists. This guarantees that the object can't be deleted by concurrent
 * transaction. If we don't lock the object, unexpected results may be obtained.
 * That's because we will try to update later an object which we consider
 * "alive", but was deleted meanwhile by concurrent transaction. Also,
 * "..does_exists.." functions must use this version type (the only way to know
 * if the object exists, is to lock it). Also, there are other particular
 * commands like ";trigger" that need to use this version type (need locking).
 *
 *   In some particular cases, you can use current version when read an instance
 * (DB_FETCH_READ). For instance, if the object was already locked, you can
 * use CURRENT version instead DIRTY (if the current transaction hold an lock
 * on OID -> is the last version of the object -> no need to lock it again
 * or to apply snapshot).
 *
 *   When need to update an instance (DB_FETCH_WRITE), and the instance is not
 * locked yet, use MVCC version type. In this case the visible version is
 * searched into MVCC chain. Then, if exists, starting from visible version,
 * updatable version is searched and X-locked. This is similar with
 * update/delete executed on server side.
 *
 *   ODKU use find unique (dirty version with S-lock). Since the object is
 * locked, will be fetched using current or dirty version not MVCC version.
 *
 *   In read committed, if have S, SIX, X or SCH-M lock on class, the instance
 * must be fetched with current or dirty version. In RR, MVCC snapshot must
 * be used in order to allow SERIALIZABLE conflicts checking.
 *
 *   If the instance is not locked and its class doesn't have shared or
 *  exclusive mode, use MVCC version.
 *
 *   au_get_new_auth uses IX-lock with dirty version when fetch all _db_auth
 *  instances. Each instance is fetched using S-lock and if an error occur, the
 *  instance is skipped.
 *
 *   Currently, CUBRID tools use MVCC version when need to read instances.
 *
 */
typedef enum
{
  LC_FETCH_CURRENT_VERSION = 0x01,	/* fetch current version */
  LC_FETCH_MVCC_VERSION = 0x02,	/* fetch MVCC - visible version */
  LC_FETCH_DIRTY_VERSION = 0x03,	/* fetch dirty version - S-locked */
  LC_FETCH_CURRENT_VERSION_NO_CHECK = 0x04,	/* fetch current version and not check server side */
} LC_FETCH_VERSION_TYPE;

#define LC_FETCH_IS_MVCC_VERSION_NEEDED(fetch_type) \
  ((fetch_type) == LC_FETCH_MVCC_VERSION)

#define LC_FETCH_IS_DIRTY_VERSION_NEEDED(fetch_type) \
  ((fetch_type) == LC_FETCH_DIRTY_VERSION)

#define LC_FETCH_IS_CURRENT_VERSION_NEEDED(fetch_type) \
  ((fetch_type) == LC_FETCH_CURRENT_VERSION)

#define LC_FLAG_HAS_INDEX_MASK  0x05
#define LC_ONEOBJ_GET_INDEX_FLAG(obj)  \
  ((obj)->flag & LC_FLAG_HAS_INDEX_MASK)

#define LC_FLAG_HAS_INDEX	0x01	/* Used for flushing, set if object has index */
#define LC_FLAG_UPDATED_BY_ME	0x02	/* Used by MVCC to identify that an object was updated by current transaction. */
#define LC_FLAG_HAS_UNIQUE_INDEX 0x04	/* Used for flushing, set if object has unique index */
#define LC_FLAG_TRIGGER_INVOLVED 0x08	/* Used for supplemental logging to know whether trigger is involved
					   or not, set if do_Trigger_involved is true */

#define LC_ONEOBJ_SET_HAS_INDEX(obj) \
  (obj)->flag |= LC_FLAG_HAS_INDEX

#define LC_ONEOBJ_SET_HAS_UNIQUE_INDEX(obj) \
  (obj)->flag |= LC_FLAG_HAS_UNIQUE_INDEX

#define LC_ONEOBJ_IS_UPDATED_BY_ME(obj) \
  (((obj)->flag & LC_FLAG_UPDATED_BY_ME) != 0)

#define LC_ONEOBJ_SET_UPDATED_BY_ME(obj) \
  (obj)->flag |= LC_FLAG_UPDATED_BY_ME

#define LC_ONEOBJ_SET_TRIGGER_INVOLVED(obj) \
  (obj)->flag |= LC_FLAG_TRIGGER_INVOLVED

#define LC_ONEOBJ_IS_TRIGGER_INVOLVED(obj) \
  (((obj)->flag & LC_FLAG_TRIGGER_INVOLVED) != 0)

typedef struct lc_copyarea_oneobj LC_COPYAREA_ONEOBJ;
struct lc_copyarea_oneobj
{
  LC_COPYAREA_OPERATION operation;	/* Insert, delete, update */
  int flag;			/* Info flag for the object */
  HFID hfid;			/* Valid only for flushing */
  OID class_oid;		/* Oid of the Class of the object */
  OID oid;			/* Oid of the object */
  int length;			/* Length of the object */
  int offset;			/* location in the copy area where the content of the object is stored */
};

enum MULTI_UPDATE_FLAG
{
  IS_MULTI_UPDATE = 0x01,
  START_MULTI_UPDATE = 0x02,
  END_MULTI_UPDATE = 0x04
};

typedef struct lc_copyarea_manyobjs LC_COPYAREA_MANYOBJS;
struct lc_copyarea_manyobjs
{
  LC_COPYAREA_ONEOBJ objs;
  int multi_update_flags;	/* start/is/end/ for unique statistics gathering */
  int num_objs;			/* How many objects */
};

/* Copy area for flushing and fetching */
typedef struct lc_copy_area LC_COPYAREA;
struct lc_copy_area
{
  char *mem;			/* Pointer to location of chunk of area */
  int length;			/* The size of the area */
};

typedef struct lc_copyarea_desc LC_COPYAREA_DESC;
struct lc_copyarea_desc
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ **obj;	/* Describe on object in area */
  int *offset;			/* Place to store next object in area */
  RECDES *recdes;
};

/*
 * Fetching multiple objects (vector fetch)
 */
typedef struct lc_lockset_reqobj LC_LOCKSET_REQOBJ;
struct lc_lockset_reqobj
{
  OID oid;			/* Oid of the object */
  int chn;			/* Cache coherence number of the object */
  int class_index;		/* Where is the desired class. A value of -1 means that the class_oid is unknown. */
};

typedef struct lc_lockset_classof LC_LOCKSET_CLASSOF;
struct lc_lockset_classof
{
  OID oid;			/* Class_oid */
  int chn;			/* Cache coherence of class */
};

typedef struct lc_lock_set LC_LOCKSET;
struct lc_lock_set
{				/* Fetch many area definition */
  char *mem;			/* Pointer to location of chunk of area where the desired objects and their classes are
				 * described */
  int length;			/* Length of the area */

  /* *** Things related to requested objects *** */
  int max_reqobjs;		/* Max number of requested objects */
  int num_reqobjs;		/* Number of requested objects to fetch and lock. An object can be an instance or a
				 * class */
  int num_reqobjs_processed;	/* Number of instances processed by the server */
  int last_reqobj_cached;	/* Last requested object that has been cached by workspace manager. Used only by
				 * client. Don't need to be send to server or from server. */
  LOCK reqobj_inst_lock;	/* Lock to acquire for the requested objects that are instances */
  LOCK reqobj_class_lock;	/* Lock to acquire for the requested objects that are classes */

  /* *** Things related to classes of the requested objects *** */
  int num_classes_of_reqobjs;	/* Number of known classes for the requested objects. */
  int num_classes_of_reqobjs_processed;	/* Number of classes processed by the server */
  int last_classof_reqobjs_cached;	/* Last requested object that has been cached by workspace manger. Used only bt
					 * client. Don't need to be send to server or from server. */
  int quit_on_errors;		/* Quit when errors are found */
  int packed_size;		/* Size of packed lock request area */
  char *packed;			/* Ptr to packed lock request area */
  LC_LOCKSET_CLASSOF *classes;	/* Description of set of classes. The number of class structures are num_classes.. Ptr
				 * into mem */
  LC_LOCKSET_REQOBJ *objects;	/* Description of requested objects. The number of structures are num_reqobjs */
  bool first_fetch_lockset_call;	/* First client call to fetch_request */
};

typedef struct lc_lockhint_class LC_LOCKHINT_CLASS;
struct lc_lockhint_class
{
  OID oid;			/* Class_oid */
  int chn;			/* Cache coherence of class */
  LOCK lock;			/* The desired lock */
  int need_subclasses;		/* Are subclasses needed ? */
};

typedef struct lc_lock_hint LC_LOCKHINT;
struct lc_lock_hint
{				/* Fetch many area definition */
  char *mem;			/* Pointer to location of chunk of area where the prefetched classes are described */
  int length;			/* Length of the area */
  int max_classes;		/* Max number of classes */
  int num_classes;		/* Number of classes to prefetch and lock. */
  int num_classes_processed;	/* Number of classes that have been processed */
  int quit_on_errors;		/* Quit when errors are found */
  int packed_size;		/* Size of packed lock lockhint area */
  char *packed;			/* Ptr to packed lock lockhint area */
  LC_LOCKHINT_CLASS *classes;	/* Description of set of classes. The number of class structures are num_classes.. Ptr
				 * into mem */
  bool first_fetch_lockhint_call;	/* First client call to fetch_lockhint */
};

enum lc_prefetch_flags
{
  LC_PREF_FLAG_LOCK = 0x00000001,
  LC_PREF_FLAG_COUNT_OPTIM = 0x00000002
};
typedef enum lc_prefetch_flags LC_PREFETCH_FLAGS;



typedef struct lc_oidmap LC_OIDMAP;
struct lc_oidmap
{
  struct lc_oidmap *next;
  /* Appendages for client side use, locator_unpack_oid_set must leave these unchanged when unpacking into an existing
   * structure. */
  void *mop;
  void *client_data;

  OID oid;
  int est_size;
};

/* LC_CLASS_OIDSET
 *
 * Information about the permanent OID's that need to be assigned for a
 * particular class.  This will be embedded inside the LC_OIDSET structure.
 */

typedef struct lc_class_oidset LC_CLASS_OIDSET;
struct lc_class_oidset
{
  struct lc_class_oidset *next;
  LC_OIDMAP *oids;
  OID class_oid;
  HFID hfid;
  int num_oids;

  /* set if oids is allocated as a linked list */
  bool is_list;
};

/* LC_OIDSET
 *
 * Structure used to hold information about permanent OID's that need
 * to be assigned.  OIDS for multiple classes can be assigned.
 */

typedef struct lc_oidset LC_OIDSET;
struct lc_oidset
{
  int total_oids;
  int num_classes;
  LC_CLASS_OIDSET *classes;

  /* set if classes is allocated as a linked list */
  bool is_list;
};

#if defined (ENABLE_UNUSED_FUNCTION)
extern LC_COPYAREA *locator_allocate_copyarea (DKNPAGES npages);
#endif
extern LC_COPYAREA *locator_allocate_copy_area_by_length (int length);
extern LC_COPYAREA *locator_reallocate_copy_area_by_length (LC_COPYAREA * old_area, int new_length);

extern void locator_free_copy_area (LC_COPYAREA * copyarea);
extern char *locator_pack_copy_area_descriptor (int num_objs, LC_COPYAREA * copyarea, char *desc, int desc_len);
extern char *locator_unpack_copy_area_descriptor (int num_objs, LC_COPYAREA * copyarea, char *desc);
extern int locator_send_copy_area (LC_COPYAREA * copyarea, char **contents_ptr, int *contents_length, char **desc_ptr,
				   int *desc_length);
#if defined(SERVER_MODE)
extern LC_COPYAREA *locator_recv_allocate_copyarea (int num_objs, char **contents_ptr, int contents_length);
#else /* SERVER_MODE */
extern LC_COPYAREA *locator_recv_allocate_copyarea (int num_objs, char **packed_desc, int packed_desc_length,
						    char **contents_ptr, int contents_length);
#endif /* SERVER_MODE */
extern LC_LOCKSET *locator_allocate_lockset (int max_reqobjs, LOCK reqobj_inst_lock, LOCK reqobj_class_lock,
					     int quit_on_errors);
#if defined (ENABLE_UNUSED_FUNCTION)
extern LC_LOCKSET *locator_allocate_lockset_by_length (int length);
#endif
extern LC_LOCKSET *locator_reallocate_lockset (LC_LOCKSET * lockset, int max_reqobjs);
extern void locator_free_lockset (LC_LOCKSET * lockset);
extern LC_LOCKSET *locator_allocate_and_unpack_lockset (char *unpacked, int unpacked_size, bool unpack_classes,
							bool unpack_objects, bool reg_unpacked);
extern int locator_pack_lockset (LC_LOCKSET * lockset, bool pack_classes, bool pack_objects);
extern int locator_unpack_lockset (LC_LOCKSET * lockset, bool unpack_classes, bool unpack_objects);
extern LC_LOCKHINT *locator_allocate_lockhint (int max_classes, bool quit_on_errors);
extern LC_LOCKHINT *locator_reallocate_lockhint (LC_LOCKHINT * lockhint, int max_classes);
extern void locator_free_lockhint (LC_LOCKHINT * lockhint);
extern int locator_pack_lockhint (LC_LOCKHINT * lockhint, bool pack_classes);
extern int locator_unpack_lockhint (LC_LOCKHINT * lockhint, bool unpack_classes);
extern LC_LOCKHINT *locator_allocate_and_unpack_lockhint (char *unpacked, int unpacked_size, bool unpack_classes,
							  bool reg_unpacked);
extern void locator_initialize_areas (void);
extern void locator_free_areas (void);

extern LC_OIDSET *locator_make_oid_set (void);
extern void locator_clear_oid_set (THREAD_ENTRY * thrd, LC_OIDSET * oidset);
extern void locator_free_oid_set (THREAD_ENTRY * thread_p, LC_OIDSET * oidset);
extern LC_OIDMAP *locator_add_oid_set (THREAD_ENTRY * thrd, LC_OIDSET * set, HFID * heap, OID * class_oid,
				       OID * obj_oid);
extern int locator_get_packed_oid_set_size (LC_OIDSET * oidset);
extern char *locator_pack_oid_set (char *buffer, LC_OIDSET * oidset);
extern LC_OIDSET *locator_unpack_oid_set_to_new (THREAD_ENTRY * thread_p, char *buffer);
extern bool locator_unpack_oid_set_to_exist (char *buffer, LC_OIDSET * use);

extern bool locator_manyobj_flag_is_set (LC_COPYAREA_MANYOBJS * copyarea, enum MULTI_UPDATE_FLAG muf);
extern void locator_manyobj_flag_remove (LC_COPYAREA_MANYOBJS * copyarea, enum MULTI_UPDATE_FLAG muf);
extern void locator_manyobj_flag_set (LC_COPYAREA_MANYOBJS * copyarea, enum MULTI_UPDATE_FLAG muf);

/* For Debugging */
#if defined(CUBRID_DEBUG)
extern void locator_dump_copy_area (FILE * out_fp, const LC_COPYAREA * copyarea, int print_rec);
extern void locator_dump_lockset (FILE * out_fp, LC_LOCKSET * lockset);
extern void locator_dump_lockhint (FILE * out_fp, LC_LOCKHINT * lockhint);
#endif
#endif /* _LOCATOR_H_ */
