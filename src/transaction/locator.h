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
 * locator.h - transaction object locator (at client and server)
 *
 */

#ifndef _LOCATOR_H_
#define _LOCATOR_H_

#ident "$Id$"

#include "storage_common.h"
#include "oid.h"
#include "object_representation.h"
#include "thread.h"

#define LC_AREA_ONEOBJ_PACKED_SIZE (OR_INT_SIZE * 5 + \
                                    OR_HFID_SIZE + \
                                    OR_OID_SIZE * 3)

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
    if (prm_get_bool_value (PRM_ID_MVCC_ENABLED) \
	&& !OID_IS_ROOTOID (&((oneobj_ptr)->class_oid))) \
      {	\
	(recdes_ptr)->area_size += \
	(OR_MVCC_MAX_HEADER_SIZE - OR_MVCC_INSERT_HEADER_SIZE); \
      }	\
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
  LC_FETCH_VERIFY_CHN,
  LC_FETCH_NO_OP
} LC_COPYAREA_OPERATION;

#define LC_IS_FLUSH_INSERT(operation) \
  (operation == LC_FLUSH_INSERT || operation == LC_FLUSH_INSERT_PRUNE \
   || operation == LC_FLUSH_INSERT_PRUNE_VERIFY)

#define LC_IS_FLUSH_UPDATE(operation) \
  (operation == LC_FLUSH_UPDATE || operation == LC_FLUSH_UPDATE_PRUNE \
   || operation == LC_FLUSH_UPDATE_PRUNE_VERIFY)

#define LC_FLAG_HAS_INDEX_MASK  0x05
#define LC_ONEOBJ_GET_INDEX_FLAG(obj)  \
  ((obj)->flag & LC_FLAG_HAS_INDEX_MASK)

#define LC_FLAG_HAS_INDEX	0x01	/* Used for flushing, set if
					 * object has index
					 */
#define LC_FLAG_UPDATED_BY_ME	0x02	/* Used by MVCC to identify
					 * that an object was updated
					 * by current transaction.
					 */
#define LC_FLAG_HAS_UNIQUE_INDEX 0x04	/* Used for flushing, set if
					 * object has unique index
					 */

#define LC_ONEOBJ_SET_HAS_INDEX(obj) \
  (obj)->flag |= LC_FLAG_HAS_INDEX

#define LC_ONEOBJ_SET_HAS_UNIQUE_INDEX(obj) \
  (obj)->flag |= LC_FLAG_HAS_UNIQUE_INDEX

#define LC_ONEOBJ_IS_UPDATED_BY_ME(obj) \
  (((obj)->flag & LC_FLAG_UPDATED_BY_ME) != 0)

#define LC_ONEOBJ_SET_UPDATED_BY_ME(obj) \
  (obj)->flag |= LC_FLAG_UPDATED_BY_ME


typedef struct lc_copyarea_oneobj LC_COPYAREA_ONEOBJ;
struct lc_copyarea_oneobj
{
  LC_COPYAREA_OPERATION operation;	/* Insert, delete, update */
  int flag;			/* Info flag for the object */
  HFID hfid;			/* Valid only for flushing */
  OID class_oid;		/* Oid of the Class of the object */
  OID oid;			/* Oid of the object */
  OID updated_oid;		/* Stores new object OID in case it has
				 * changed.
				 */
  int length;			/* Length of the object */
  int offset;			/* location in the copy area where the
				 * content of the object is stored
				 */
  int error_code;
};

typedef struct lc_copyarea_manyobjs LC_COPYAREA_MANYOBJS;
struct lc_copyarea_manyobjs
{
  LC_COPYAREA_ONEOBJ objs;
  int start_multi_update;	/* the start of flush request */
  int end_multi_update;		/* the end of flush request */
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
  int class_index;		/* Where is the desired class. A value of -1 means that
				 * the class_oid is unknown.
				 */
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
  char *mem;			/* Pointer to location of chunk of
				 * area where the desired objects and
				 * their classes are described
				 */
  int length;			/* Length of the area */

  /* *** Things related to requested objects *** */
  int max_reqobjs;		/* Max number of requested objects */
  int num_reqobjs;		/* Number of requested objects to
				 * fetch and lock. An object can be an
				 * instance or a class
				 */
  int num_reqobjs_processed;	/* Number of instances processed by
				 * the server
				 */
  int last_reqobj_cached;	/* Last requested object that has been
				 * cached by workspace manager. Used
				 * only by client. Don't need to be
				 * send to server or from server.
				 */
  LOCK reqobj_inst_lock;	/* Lock to acquire for the requested
				 * objects that are instances
				 */
  LOCK reqobj_class_lock;	/* Lock to acquire for the requested
				 * objects that are classes
				 */

  /* *** Things related to classes of the requested objects *** */
  int num_classes_of_reqobjs;	/* Number of known classes for the
				 * requested objects.
				 */
  int num_classes_of_reqobjs_processed;	/* Number of classes processed by the
					 * server
					 */
  int last_classof_reqobjs_cached;	/* Last requested object that has been
					 * cached by workspace manger. Used
					 * only bt client. Don't need to be
					 * send to server or from server.
					 */
  int quit_on_errors;		/* Quit when errors are found  */
  int packed_size;		/* Size of packed lock request area */
  char *packed;			/* Ptr to packed lock request area */
  LC_LOCKSET_CLASSOF *classes;	/* Description of set of classes.
				 * The number of class structures
				 * are num_classes.. Ptr into mem
				 */
  LC_LOCKSET_REQOBJ *objects;	/* Description of requested objects.
				 * The number of structures are
				 * num_reqobjs
				 */
  bool first_fetch_lockset_call;	/* First client call to
					 * fetch_request
					 */
};

typedef struct lc_lockhint_class LC_LOCKHINT_CLASS;
struct lc_lockhint_class
{
  OID oid;			/* Class_oid                */
  int chn;			/* Cache coherence of class */
  LOCK lock;			/* The desired lock         */
  int need_subclasses;		/* Are subclasses needed ?  */
};

typedef struct lc_lock_hint LC_LOCKHINT;
struct lc_lock_hint
{				/* Fetch many area definition */
  char *mem;			/* Pointer to location of chunk of
				 * area where the prefetched classes
				 * are described
				 */
  int length;			/* Length of the area */
  int max_classes;		/* Max number of classes */
  int num_classes;		/* Number of classes to prefetch and
				 * lock.
				 */
  int num_classes_processed;	/* Number of classes that have been
				 * processed
				 */
  int quit_on_errors;		/* Quit when errors are found  */
  int packed_size;		/* Size of packed lock lockhint area */
  char *packed;			/* Ptr to packed lock lockhint area */
  LC_LOCKHINT_CLASS *classes;	/* Description of set of classes. The
				 * number of class structures are
				 * num_classes.. Ptr into mem
				 */
  bool first_fetch_lockhint_call;	/* First client call to
					 * fetch_lockhint
					 */
};

typedef enum lc_prefetch_flags LC_PREFETCH_FLAGS;
enum lc_prefetch_flags
{
  LC_PREF_FLAG_LOCK = 0x00000001,
  LC_PREF_FLAG_COUNT_OPTIM = 0x00000002
};



typedef struct lc_oidmap LC_OIDMAP;
struct lc_oidmap
{
  struct lc_oidmap *next;
  /* Appendages for client side use, locator_unpack_oid_set must leave these unchanged
   * when unpacking into an existing structure.
   */
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

typedef enum
{
  LC_STOP_ON_ERROR,
  LC_CONTINUE_ON_ERROR		/* Until now, it is only for log_applier */
} LC_ON_ERROR;

#if defined (ENABLE_UNUSED_FUNCTION)
extern LC_COPYAREA *locator_allocate_copyarea (DKNPAGES npages);
#endif
extern LC_COPYAREA *locator_allocate_copy_area_by_length (int length);

extern void locator_free_copy_area (LC_COPYAREA * copyarea);
extern char *locator_pack_copy_area_descriptor (int num_objs,
						LC_COPYAREA * copyarea,
						char *desc);
extern char *locator_unpack_copy_area_descriptor (int num_objs,
						  LC_COPYAREA * copyarea,
						  char *desc);
extern int locator_send_copy_area (LC_COPYAREA * copyarea,
				   char **contents_ptr, int *contents_length,
				   char **desc_ptr, int *desc_length);
#if defined(SERVER_MODE)
extern LC_COPYAREA *locator_recv_allocate_copyarea (int num_objs,
						    char **contents_ptr,
						    int contents_length);
#else /* SERVER_MODE */
extern LC_COPYAREA *locator_recv_allocate_copyarea (int num_objs,
						    char **packed_desc,
						    int packed_desc_length,
						    char **contents_ptr,
						    int contents_length);
#endif /* SERVER_MODE */
extern LC_LOCKSET *locator_allocate_lockset (int max_reqobjs,
					     LOCK reqobj_inst_lock,
					     LOCK reqobj_class_lock,
					     int quit_on_errors);
#if defined (ENABLE_UNUSED_FUNCTION)
extern LC_LOCKSET *locator_allocate_lockset_by_length (int length);
#endif
extern LC_LOCKSET *locator_reallocate_lockset (LC_LOCKSET * lockset,
					       int max_reqobjs);
extern void locator_free_lockset (LC_LOCKSET * lockset);
extern LC_LOCKSET *locator_allocate_and_unpack_lockset (char *unpacked,
							int unpacked_size,
							bool unpack_classes,
							bool unpack_objects,
							bool reg_unpacked);
extern int locator_pack_lockset (LC_LOCKSET * lockset, bool pack_classes,
				 bool pack_objects);
extern int locator_unpack_lockset (LC_LOCKSET * lockset, bool unpack_classes,
				   bool unpack_objects);
extern LC_LOCKHINT *locator_allocate_lockhint (int max_classes,
					       int quit_on_errors);
extern LC_LOCKHINT *locator_reallocate_lockhint (LC_LOCKHINT * lockhint,
						 int max_classes);
extern void locator_free_lockhint (LC_LOCKHINT * lockhint);
extern int locator_pack_lockhint (LC_LOCKHINT * lockhint, bool pack_classes);
extern int
locator_unpack_lockhint (LC_LOCKHINT * lockhint, bool unpack_classes);
extern LC_LOCKHINT *locator_allocate_and_unpack_lockhint (char *unpacked,
							  int unpacked_size,
							  bool unpack_classes,
							  bool reg_unpacked);
extern void locator_initialize_areas (void);
extern void locator_free_areas (void);

extern LC_OIDSET *locator_make_oid_set (void);
extern void locator_clear_oid_set (THREAD_ENTRY * thrd, LC_OIDSET * oidset);
extern void locator_free_oid_set (THREAD_ENTRY * thread_p,
				  LC_OIDSET * oidset);
extern LC_OIDMAP *locator_add_oid_set (THREAD_ENTRY * thrd, LC_OIDSET * set,
				       HFID * heap, OID * class_oid,
				       OID * obj_oid);
extern int locator_get_packed_oid_set_size (LC_OIDSET * oidset);
extern char *locator_pack_oid_set (char *buffer, LC_OIDSET * oidset);
extern LC_OIDSET *locator_unpack_oid_set_to_new (THREAD_ENTRY * thread_p,
						 char *buffer);
extern bool locator_unpack_oid_set_to_exist (char *buffer, LC_OIDSET * use);

/* For Debugging */
#if defined(CUBRID_DEBUG)
extern void
locator_dump_copy_area (FILE * out_fp, const LC_COPYAREA * copyarea,
			int print_rec);
extern void locator_dump_lockset (FILE * out_fp, LC_LOCKSET * lockset);
extern void locator_dump_lockhint (FILE * out_fp, LC_LOCKHINT * lockhint);
#endif
#endif /* _LOCATOR_H_ */
