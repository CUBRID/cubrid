/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * work_space.c - Workspace Manager
 *
 */
#ident "$Id$"

#include "config.h"
#include "gc.h"			/* external/gc6.7 */

#include <stdlib.h>
#include <string.h>

#include "memory_manager_2.h"
#include "memory_manager_1.h"
#include "message_catalog.h"
#include "memory_hash.h"
#include "error_manager.h"
#include "oid.h"
#include "work_space.h"
#include "schema_manager_3.h"
#include "authenticate.h"
#include "object_accessor.h"
#include "locator_cl.h"
#include "common.h"
#include "system_parameter.h"
#include "set_object_1.h"
#include "virtual_object_1.h"
#include "object_primitive.h"
#include "class_object.h"
#include "environment_variable.h"
#include "db.h"
#include "transaction_cl.h"
#include "object_template.h"
#include "server.h"

/* this must be the last header file included!!! */
#include "dbval.h"

/*
 * need these to get the allocation areas initialized, avoid including
 * the entire file
 */

/*
 * ws_Commit_mops
 *    Linked list of mops to be reset at commit/abort.
 */

MOP ws_Commit_mops = NULL;

/*
 * ws_Mop_table
 *    This is the OID to MOP hash table.  This is public ONLY to allow
 *    some performance related mapping macros to be used by the
 *    transaction manager.
 */

MOP *ws_Mop_table = NULL;

/*
 * ws_Mop_table_size
 *    Records the current size of the OID to MOP hash table.
 */

unsigned int ws_Mop_table_size = 0;

#if 0				/* unused feature */
/*
 * ws_Reference_mops
 *    Linked list of reference mops.
 */

MOP ws_Reference_mops = NULL;
#endif


/*
 * ws_Set_mops
 *    Linked list of set MOPs.
 *    This is a temporary solution for the disconnected set GC problem.
 *    See description in set.c for more details.
 *    All MOPs in this list are to serve as roots for the garbage collector.
 */

MOP ws_Set_mops = NULL;

/*
 * ws_Resident_classes
 *    This is a global list of resident class objects.
 *    Since the root of the class' resident instance list is kept in
 *    the class_link field of the class MOP, we can't use this field
 *    to chain the list of resident class objects.  Instead keep an object
 *    list.
 */

DB_OBJLIST *ws_Resident_classes = NULL;

/*
 * ws_Stats
 *    Workspace statistics structure.
 *    This contains random information about the state of the workspace.
 *    See definition in ws.h
 */

WS_STATISTICS ws_Stats =
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int ws_Num_dirty_mop = 0;

/*
 * ws_Gc_enabled - whether use GC or not; set by PRM_GC_ENABLE
 */
static bool ws_Gc_enabled = false;

/*
 * We used to keep a global dirty list here. But for more efficient traversals
 * the dirty list has been reorganized into a dirty list by class. To visit
 * all the dirty objects in this workspace, start with the resident class list
 * and visit each class' dirty list. The dirty flag here is consulted by
 * ws_has_updated to determine if there are dirty objects in this workspace.
 */
static bool Ws_dirty;

/*
 * Null_object
 *    This is used at the terminator for the dirty_link and class_link fieids
 *    of the MOP if they are the last in the list.  This allows us to
 *    determine if the MOP has been added to a class list simply by
 *    checking to see if the field is NULL.  If it is non-NULL, we know it
 *    must be in the list (even if it is at the end of the list).  This
 *    avoids having to keep an extra bit in the MOP structre.
 */

static MOP Null_object;

/*
 * Classname_cache
 *    This is a hash table used to cache the class name to MOP mapping
 *    on the client side.  This avoids repeated calls to the server to
 *    find class OIDs.
 */

static MHT_TABLE *Classname_cache = NULL;

static MOP ws_make_mop (OID * oid);
static void ws_free_mop (MOP op);
static void disconnect_deleted_instances (MOP classop);
static void emergency_remove_dirty (MOP op);
static void ws_cull_mops (void);
static int ws_map_dirty_internal (MAPFUNC function, void *args,
				  bool classes_only);
static int add_class_object (MOP class_mop, MOP obj);
static void remove_class_object (MOP class_mop, MOP obj);
static int mark_instance_deleted (MOP op, void *args);
static void ws_clear_internal (bool clear_vmop_keys);
static void ws_reset_class_cache (void);
static void ws_print_oid (OID * oid);
static int ws_describe_mop (MOP mop, void *args);
static void ws_flush_properties (MOP op);

/*
 * MEMORY CRISES
 */

/*
 * ws_abort_transaction - callback routine for the qf module that is called
 *                        when storage is exhausted and an allocation can
 *                        not be serviced
 *     return: void
 */
void
ws_abort_transaction (void)
{
  if (db_Disable_modifications)
    {
      if (er_errid () != ER_OUT_OF_VIRTUAL_MEMORY)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, 0);
	}
    }
  else
    {
      /* might want to keep a chunk of memory in reserve here so we can free it
         in case we need to do any small allocations during the abort process */

      (void) tran_unilaterally_abort ();

      /* couldn't get to the catalog, use hard coded strings */
      fprintf (stdout,
	       "CUBRID cannot allocate main memory and must halt execution.\n");
      fprintf (stdout, "The current transaction has been aborted.\n");
      fprintf (stdout, "Data integrity has been preserved.\n");
    }
}

/*
 * MOP ALLOCATION AND TABLE MAINTENANCE
 */

/*
 * ws_make_mop - allocates a storate for a new mop
 *    return: MOP structure
 *    oid(in): oid for a new mop
 */
static MOP
ws_make_mop (OID * oid)
{
  MOP op;

  op = GC_MALLOC (sizeof (DB_OBJECT));
  if (op != NULL)
    {
      op->class_mop = NULL;
      op->object = NULL;
      op->class_link = NULL;
      op->dirty_link = NULL;
      op->dirty = 0;
      op->deleted = 0;
      op->pinned = 0;
      op->no_objects = 0;
      op->lock = NULL_LOCK;
      op->hash_link = NULL;
      op->commit_link = NULL;
      op->reference = 0;
      op->version = NULL;
      op->oid_info.oid.volid = 0;
      op->oid_info.oid.pageid = 0;
      op->oid_info.oid.slotid = 0;
      op->is_vid = 0;
      op->is_set = 0;
      op->is_temp = 0;
      op->released = 0;

      /* this is NULL only for the Null_object hack */
      if (oid != NULL)
	{
	  COPY_OID (WS_REAL_OID (op), oid);
	}
      else
	{
	  OID_SET_NULL (WS_REAL_OID (op));
	}

      ws_Stats.mops_allocated++;
    }
  else
    {
      /* couldnt' allocate a MOP, mgc should have set an error by now */
      ws_abort_transaction ();
    }

  return (op);
}

/*
 * ws_free_mop - frees a MOP
 *    return: void
 *    op(in/out): MOP pointer
 *
 * Note: This was introduced primarily to handle the new MOP property
 * lists.  MOPS can only really be freed through garbage collection.
 */
static void
ws_free_mop (MOP op)
{
  DB_VALUE *keys;
  unsigned int flags;

  keys = ws_keys (op, &flags);

  if (keys != NULL)
    {
      pr_clear_value (keys);
    }

  if (op->version != NULL)
    {
      ws_flush_properties (op);
    }

  GC_FREE (op);
}

/*
 * ws_make_set_mop - makes a special set MOP.
 *    return: mop created
 *    setptr(in): opaque set pointer
 *
 * Note: These are a temporary solution to prevent objects in disconnected
 *       sets from being garbage collected. See commentary in om/set.c
 *       for more information
 */
MOP
ws_make_set_mop (void *setptr)
{
  MOP op;

  op = ws_make_mop (NULL);

  if (op == NULL)
    {
      return NULL;
    }
  op->is_set = 1;
  op->object = setptr;
  ws_Stats.set_mops_allocated++;

  /*
   * chain these on the ws_Set_mops list, doubly linked so its quick
   * to get them off, class_link is next pointer, dirty_link is prev pointer
   */
  if (ws_Set_mops != NULL)
    {
      ws_Set_mops->dirty_link = op;
    }
  op->class_link = ws_Set_mops;
  ws_Set_mops = op;


  return (op);
}

/*
 * ws_free_set_mop - frees a temporary set MOP.
 *    return: void
 *    op(in/out): set mop
 */
void
ws_free_set_mop (MOP op)
{
  if (op != NULL && op->is_set)
    {
      /* remove it from the list */
      if (op->class_link != NULL)
	{
	  (op->class_link)->dirty_link = op->dirty_link;
	}

      if (op->dirty_link != NULL)
	{
	  (op->dirty_link)->class_link = op->class_link;
	}
      else
	{
	  ws_Set_mops = op->class_link;
	}

      ws_free_mop (op);
      ws_Stats.set_mops_freed++;
    }
}

/*
 * ws_make_temp_mop - create a temporary MOP
 *    return: new temporary MOP
 * Note: It is assumed the caller will set up the MOP fields in the right way.
 * The ws_ module will ensure that MOPs with the is_temp field set are not
 * subject to garbage collection scans and are kept off the resident instance
 * lists of the classes.
 */
MOP
ws_make_temp_mop (void)
{
  MOP op;

  op = ws_make_mop (NULL);

  if (op != NULL)
    {
      op->is_temp = 1;
      ws_Stats.temp_mops_allocated++;
    }
  return (op);
}

/*
 * ws_free_temp_mop - frees a temporary MOP.
 *    return: void
 *    op(in/out): temporary mop to free
 */
void
ws_free_temp_mop (MOP op)
{
  if (op != NULL && op->is_temp)
    {
      ws_free_mop (op);
      ws_Stats.temp_mops_freed++;
    }
}

#if 0				/* unused feature */
/*
 * ws_make_reference_mop - constructs a reference mop for an attribute.
 *    return: reference mop
 *    owner(in): owning object
 *    attid(in): attribute id of reference
 *    refobj(in): pointer to reference header
 *    collector(in): callback function to perform garbage collection
 *
 * Note: The reference MOP support was added with the intention that they
 *    be used for set MOPs.  This was never actually used.  There is currently
 *    no use for reference MOPs.  Support remains in this file however
 *    for the day when we actually implement set MOPs.  Note that the
 *    kludge support for set MOPs elsewhere in this file is a temporary
 *    solution for the disconnected set garbage collection problem.
 *    It is solved in a less general way than what reference mops
 *    may ultimately provide.
 */
static MOP
ws_make_reference_mop (MOP owner, int attid, WS_REFERENCE * refobj,
		       WS_REFCOLLECTOR collector)
{
  MOP op;

  op = GC_MALLOC (sizeof (DB_OBJECT));
  if (op != NULL)
    {
      op->class_ = NULL;
      op->object = NULL;	/* points to referenced thing */
      op->lock = NULL_LOCK;
      op->class_link = NULL;	/* chain of reference mops */
      op->dirty_link = NULL;
      op->hash_link = NULL;
      op->commit_link = NULL;
      op->version = NULL;
      op->dirty = 0;
      op->deleted = 0;
      op->no_objects = 0;
      op->pinned = 0;
      op->released = 0;
      op->reference = 1;
      OID_SET_NULL (WS_REAL_OID (op));

      op->hash_link = ws_Reference_mops;
      ws_Reference_mops = op;

      /* point the reference mop at the owner */
      WS_SET_REFMOP_OWNER (op, owner);
      WS_SET_REFMOP_ID (op, attid);
      WS_SET_REFMOP_OBJECT (op, refobj);
      WS_SET_REFMOP_COLLECTOR (op, collector);

      /* pointer the reference object back at the mop */
      refobj->handle = op;

      ws_Stats.refmops_allocated++;
    }
  else
    {
      /* couldnt' allocate a MOP, mgc should have set an error by now */
      ws_abort_transaction ();
    }

  return (op);
}

/*
 * ws_find_reference_mop - find or make reference MOP
 *    return: reference MOP
 *    owner(in): owning object
 *    attid(in): attribute id of reference
 *    refobj(in): structure header of referenced object
 *    collector(in): callback function to perform garbage collection
 *
 * Note: This is used to install a reference mop in the workspace.  Since we
 *    want to avoid duplication of reference mops, we must check to see
 *    if there is already a reference mop installed and if so use it.  If
 *    there is no matching mop, create a new one.
 *    Currently this uses a simple list search on the assumption that
 *    the number of reference mops will be small relative to regular mops and
 *    that once cached, they will tend to remain connected to the reference
 *    object.  Reference objects are decached only when the containing
 *    object is decached.
 *    There are a number of alternatives to this scheme that should be
 *    considered.
 *    1) The reference mop list could be maintained in a hash table so this
 *       lookup is more effecient.
 *    2) We avoid this lookup entirely by allowing duplicate reference
 *       mops to be created but chain them together as they are detected.
 *       e.g. A reference mop is created and the object becomes decached
 *       leaving the reference mop in the unconnected state.  Another
 *       request is made for the attribute, since the object is decached
 *       we don't know about the existing reference mop, the object is
 *       cached and a new reference mop is created.  When an access to
 *       the old reference mop is made, we see that it is unconnected and
 *       go through normal attribute lookup to get the referenced object.
 *       When we get the referenced object, we see that it already has a
 *       reference mop cached in its header.  At this point we chain the
 *       old reference mop and the new reference mop together with the root
 *       in the referenced object header.  Now when the object is swapped
 *       out, we must remember that we have to unconnect all reference
 *       mops in the list, not just one.  When a reference mop is garbage
 *       collected, we must remember to remove it from the list of
 *       reference mops rooted in the referenced object header.
 *       This scheme will avoid a potentially costly lookup when creating
 *       the initial reference mop at the expense of a more complexity.
 */
MOP
ws_find_reference_mop (MOP owner, int attid, WS_REFERENCE * refobj,
		       WS_REFCOLLECTOR collector)
{
  MOP m, found = NULL;

  for (m = ws_Reference_mops; m != NULL && found == NULL; m = m->hash_link)
    {
      if (WS_GET_REFMOP_OWNER (m) == owner && WS_GET_REFMOP_ID (m) == attid)
	found = m;
    }
  if (found == NULL)
    found = ws_make_reference_mop (owner, attid, refobj, collector);

  return (found);
}

/*
 * ws_set_reference_mop_owner - sets the owner and attribute information for a
 * reference mop.
 *    return: void
 *    refmop(out): a reference mop
 *    owner(in): new owing object (can be NULL if disconnecting)
 *    attid(in): reference id of reference object (-1 if disconnecting )
 *
 * Note:
 *    Sets the owner and attribute information for a reference mop.
 *    If the reference object is being disconnected from its owner, this
 *    function can be called with NULL as the owner.  When a reference mop
 *    is garbage collected, and the owner field is NULL, the attached
 *    reference object can be freed as well provided a collector function
 *    was supplied when the reference mop was created.
 */
void
ws_set_reference_mop_owner (MOP refmop, MOP owner, int attid)
{
  WS_SET_REFMOP_OWNER (refmop, owner);
  WS_SET_REFMOP_ID (refmop, attid);
}

/*
 * ws_cull_reference_mops - cull MOPs in the reference mop list that are no
 * longer being used.
 *    return: void
 *    table(in): not used
 *    table2(in): not used
 *    size(in): not used
 *    check(in): check function
 */
static void
ws_cull_reference_mops (void *table, char *table2, long size, check_fn check)
{
  MOP mop, prev, next, owner;
  WS_REFERENCE *ref;
  WS_REFCOLLECTOR collector;
  int count;

  count = 0;
  for (mop = ws_Reference_mops, prev = NULL, next = NULL; mop != NULL;
       mop = next)
    {
      next = mop->hash_link;
      if ((*check) (mop))
	prev = mop;
      else
	{
	  if (!mop->reference)
	    {
	      /* only supposed to have reference mops on this list */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CORRUPTED, 0);
	      ws_Stats.corruptions++;
	      /* probably fatal to continue here */
	      continue;
	    }
	  /* remove it from the list */
	  if (prev == NULL)
	    ws_Reference_mops = next;
	  else
	    prev->hash_link = next;

	  /*
	   * if there is no owner, this is a free standing reference and can
	   * be freed if there is a supplied collector function, otherwise
	   * just NULL out the back pointer in the reference object
	   */
	  ref = WS_GET_REFMOP_OBJECT (mop);
	  if (ref != NULL)
	    {
	      ref->handle = NULL;
	      owner = WS_GET_REFMOP_OWNER (mop);
	      if (owner == NULL)
		{
		  collector = WS_GET_REFMOP_COLLECTOR (mop);
		  if (collector != NULL)
		    (*collector) (ref);
		}
	    }

	  /* return mop to the garbage collector */
	  ws_free_mop (mop);
	  count++;
	}
    }

  ws_Stats.refmops_last_gc = count;
  ws_Stats.refmops_freed += count;
}
#endif

/*
 * ws_begin_mop_iteration - begin iteration
 *    return: mop iterator
 *
 * Note:
 * Initializes static MOP_ITERATOR, which is used by ws_next_mop()
 * to iterate over ws_Mop_table.
 */
MOP_ITERATOR *
ws_begin_mop_iteration (void)
{
  static MOP_ITERATOR temp;
  MOP_ITERATOR *it;

  it = &temp;
  it->index = 0;
  it->next = NULL;

  while (it->index < ws_Mop_table_size && it->next == NULL)
    {
      it->next = ws_Mop_table[it->index];
      if (it->next == NULL)
	{
	  it->index++;
	}
    }

  return (it);
}


/*
 * ws_next_mop - returns the MOP currently pointed to by the MOP_ITERATOR
 *    return: next mop
 *    it(in/out): address of static MOP_ITERATOR
 *
 * Note:
 * If the MOP returned is a valid MOP, the MOP_ITERATOR is updated
 * to point to the next entry in the MOP table (post-increments the
 * MOP_ITERATOR).
 */
MOP
ws_next_mop (MOP_ITERATOR * it)
{
  MOP next;

  next = it->next;

  if (next == NULL)
    {
      return NULL;
    }
  it->next = next->hash_link;

  if (it->next != NULL)
    {
      return next;
    }

  it->index++;

  while (it->index < ws_Mop_table_size && it->next == NULL)
    {
      it->next = ws_Mop_table[it->index];
      if (it->next == NULL)
	{
	  it->index++;
	}
    }

  return (next);
}

/*
 * ws_mop - given a oid, find or create the corresponding MOP and add it to
 * the workspace object table.
 *    return: MOP
 *    oid(in): oid
 *    class(in): optional class MOP (can be null if not known)
 *
 * Note: If the class argument is NULL, it will be added to the class list
 * when the object is cached.
 */
MOP
ws_mop (OID * oid, MOP class_)
{
  MOP mop, found;
  unsigned int slot;

  found = NULL;
  if (OID_ISNULL (oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CANT_INSTALL_NULL_OID,
	      0);
      return NULL;
    }
  /* look for existing entry */
  slot = OID_PSEUDO_KEY (oid);
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  for (mop = ws_Mop_table[slot]; found == NULL && mop != NULL;
       mop = mop->hash_link)
    {
      if (oid_compare (oid, WS_OID (mop)) == 0)
	{
	  found = mop;
	}
    }
  if (found != NULL)
    {
      return found;
    }
  found = ws_make_mop (oid);

  if (found == NULL)
    {
      return NULL;
    }

  if (class_ != NULL)
    {
      if (add_class_object (class_, found))
	{
	  ws_free_mop (found);
	  return NULL;
	}
    }
  /* install it into this slot list */
  found->hash_link = ws_Mop_table[slot];
  ws_Mop_table[slot] = found;

  return (found);
}

/*
 * ws_keys - return vid's keys and flags
 *    return: vid's keys
 *    vid(in): a virtual object if all OK, NULL otherwise
 *    flags(out): bit encoded properties of vid
 */
DB_VALUE *
ws_keys (MOP vid, unsigned int *flags)
{
  VID_INFO *vid_info;

  if (!vid || !vid->is_vid)
    {
      return NULL;
    }

  vid_info = WS_VID_INFO (vid);

  if (vid_info == NULL)
    {
      return NULL;
    }

  *flags = vid_info->flags;
  return &vid_info->keys;
}

/*
 * ws_vmop -
 *    return:
 *    class(in):
 *    flags(in):
 *    keys(in):
 */
MOP
ws_vmop (MOP class_, int flags, DB_VALUE * keys)
{
  MOP mop, found;
  int slot;
  VID_INFO *vid_info;
  DB_TYPE keytype;

  found = NULL;
  vid_info = NULL;
  keytype = (DB_TYPE) PRIM_TYPE (keys);

  switch (keytype)
    {
    case DB_TYPE_OBJECT:
      /*
       * a non-virtual object mop
       * This will occur when reading the oid keys field of a vobject
       * if it was read thru some interface that automatically
       * swizzles oid's to objects.
       */
      mop = db_get_object (keys);
      if (!db_is_vclass (class_))
	{
	  return mop;
	}
      db_make_object (keys, mop);
      break;
    case DB_TYPE_OID:
      /*
       * a non-virtual object mop
       * This will occur when reading the oid keys feild of a vobject
       * if it was read thru some interface that does NOT swizzle.
       * oid's to objects.
       */
      mop = ws_mop (&keys->data.oid, class_);
      if (!db_is_vclass (class_))
	{
	  return mop;
	}
      db_make_object (keys, mop);
      break;
    default:
      /* otherwise fall thru to generic keys case */
      break;
    }

  slot = mht_valhash (keys, ws_Mop_table_size);
  if (!(flags & VID_NEW))
    {
      for (mop = ws_Mop_table[slot]; found == NULL && mop != NULL;
	   mop = mop->hash_link)
	{
	  if (mop->is_vid)
	    {
	      vid_info = WS_VID_INFO (mop);
	      if (class_ == mop->class_mop)
		{
		  /*
		   * NOTE, formerly called pr_value_equal. Don't coerce
		   * with the new tp_value_equal function but that may
		   * actually be desired here.
		   */
		  if (tp_value_equal (keys, &vid_info->keys, 0))
		    {
		      found = mop;
		    }
		}
	    }
	}
    }
  if (found != NULL)
    {
      return found;
    }

  found = ws_make_mop (NULL);
  if (found == NULL)
    {
      return NULL;
    }
  found->is_vid = 1;
  vid_info = WS_VID_INFO (found) =
    (VID_INFO *) db_ws_alloc (sizeof (VID_INFO));

  if (vid_info == NULL)
    {
      goto abort_it;
    }

  vid_info->flags = flags;
  db_make_null (&vid_info->keys);

  if (pr_clone_value (keys, &vid_info->keys))
    {
      goto abort_it;
    }

  if (add_class_object (class_, found))
    {
      goto abort_it;
    }

  /* install it into this slot list */
  found->hash_link = ws_Mop_table[slot];
  ws_Mop_table[slot] = found;

  return (found);

abort_it:
  if (found != NULL)
    {
      ws_free_mop (found);
    }

  if (vid_info != NULL)
    {
      pr_clear_value (&vid_info->keys);
      db_ws_free (vid_info);
    }
  return NULL;
}

/*
 * ws_rehash_vmop - remove the old hash entry, copy the object id attribute
 * values and rehash.
 *    return: true if success
 *            false if mop is not vid or vid_fetch_instance failed or not found
 *    mop(in): Mop of virtual object to rehash
 *    classobj(in): Object for the class
 *    newkey(in): NULL for relational mop; newkey for OO mop
 */
bool
ws_rehash_vmop (MOP mop, MOBJ classobj, DB_VALUE * newkey)
{
  SM_CLASS *class_ = (SM_CLASS *) classobj;
  DB_VALUE *keys, new_key;
  MOP found, prev;
  int slot;
  VID_INFO *vid_info;
  SM_ATTRIBUTE *att;
  char *mem;
  int no_keys;
  int key_index;
  DB_VALUE val;
  DB_VALUE *value = &val;
  int att_seq_val;
  MOBJ inst;

  if (!mop->is_vid)
    {
      return false;
    }
  ws_find (mop, &inst);
  if (!inst)
    {
      inst = vid_fetch_instance (mop, DB_FETCH_READ);
    }
  if (!inst)
    {
      return false;
    }

  vid_info = WS_VID_INFO (mop);
  keys = &vid_info->keys;
  /* DB_TYPE_STRING is used for ldb's with OID's */
  if (vid_class_has_intrinsic_oid (class_))
    {
      if (!newkey)
	{
	  /*
	   * must be from vid_record_update.
	   * so, do what the old ws_rehash_vmop did.
	   */
	  if (DB_VALUE_TYPE (keys) == DB_TYPE_NULL)
	    {
	      db_make_string (keys, NULL);
	    }
	  return true;
	}
    }
  slot = mht_valhash (keys, ws_Mop_table_size);

  for (found = ws_Mop_table[slot], prev = NULL;
       found != mop && found != NULL; found = found->hash_link)
    {
      prev = found;
    }

  if (found != mop)
    {
      return false;
    }

  if (vid_class_has_intrinsic_oid (class_))
    {
      /* must be from a bulk OO flush call site */
      no_keys = 1;
      if (newkey)
	{
	  new_key = *newkey;
	}
    }
  else
    {
      /* get new relational key */
      no_keys = 0;
      for (att = class_->attributes; att != NULL;
	   att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if (att->flags & SM_ATTFLAG_VID)
	    {
	      ++no_keys;
	    }
	}
      if (no_keys > 1)
	{
	  db_make_sequence (&new_key, set_create_sequence (no_keys));
	}

      for (key_index = 0; key_index < no_keys; ++key_index)
	{
	  for (att = class_->attributes; att != NULL;
	       att = (SM_ATTRIBUTE *) att->header.next)
	    {
	      if ((att->flags & SM_ATTFLAG_VID) &&
		  (classobj_get_prop
		   (att->properties, SM_PROPERTY_VID_KEY, &val)))
		{
		  att_seq_val = DB_GET_INTEGER (&val);
		  if (att_seq_val == key_index)
		    {
		      /* Sets won't work as key components */
		      mem = inst + att->offset;
		      db_value_domain_init (&val, att->type->id,
					    att->domain->precision,
					    att->domain->scale);
		      PRIM_GETMEM (att->type, att->domain, mem, &val);
		      if ((DB_VALUE_TYPE (value) == DB_TYPE_STRING) &&
			  (DB_GET_STRING (value) == NULL))
			{
			  DB_MAKE_NULL (value);
			}

		      if (no_keys > 1)
			{
			  if (set_put_element (db_get_set (&new_key),
					       key_index, &val) < 0)
			    {
			      return false;
			    }
			}
		      else
			{
			  pr_clone_value (&val, &new_key);
			}
		      pr_clear_value (&val);
		    }
		}
	    }
	}
    }
  pr_clear_value (keys);
  pr_clone_value (&new_key, keys);
  pr_clear_value (&new_key);

  if (prev == NULL)
    {
      ws_Mop_table[slot] = mop->hash_link;
    }
  else
    {
      prev->hash_link = mop->hash_link;
    }

  slot = mht_valhash (keys, ws_Mop_table_size);
  mop->hash_link = ws_Mop_table[slot];
  ws_Mop_table[slot] = mop;

  return true;
}

/*
 * ws_new_mop - optimized version of ws_mop when OID being entered into the
 * workspace is guarenteed to be unique.
 *    return: new MOP
 *    oid(in): object OID
 *    class(in): class of object
 *
 * Note:
 *    This happens when temporary OIDs are generated for newly created objects.
 *    It assumes that the MOP must be created and does not bother searching
 *    the hash table collision list looking for duplicates.
 */
MOP
ws_new_mop (OID * oid, MOP class_)
{
  MOP mop;
  unsigned long slot;

  mop = NULL;
  if (OID_ISNULL (oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CANT_INSTALL_NULL_OID,
	      0);
      return NULL;
    }

  slot = OID_PSEUDO_KEY (oid);

  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }
  mop = ws_make_mop (oid);

  if (mop == NULL)
    {
      return NULL;
    }

  if (class_ != NULL)
    {
      if (add_class_object (class_, mop))
	{
	  ws_free_mop (mop);
	  return NULL;
	}
    }
  mop->hash_link = ws_Mop_table[slot];
  ws_Mop_table[slot] = mop;

  return (mop);
}

/*
 * ws_perm_oid - change the OID of a MOP
 *    return: void
 *    mop(in/out): MOP whose OID needs to be changed
 *    newoid(in): new OID
 *
 * Note:
 *    This is only called by the transaction locator as OIDs need to be
 *    flushed and must be converted to permanent OIDs before they are given
 *    to the server.
 *
 *    This assumes that the new permanent OID is guarenteed to be
 *    unique and we can avoid searching the hash table collision list
 *    for existing MOPs with this OID.  This makes the conversion faster.
 */
void
ws_perm_oid (MOP mop, OID * newoid)
{
  MOP mops, prev;
  unsigned long slot;

  if (!OID_ISTEMP ((OID *) WS_OID (mop)))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_MOP_NOT_TEMPORARY, 0);
      return;
    }

  /* find current entry */
  slot = OID_PSEUDO_KEY (WS_OID (mop));
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  mops = ws_Mop_table[slot];
  for (prev = NULL; mops != mop && mops != NULL; mops = mops->hash_link)
    {
      prev = mops;
    }

  if (mops != mop)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_MOP_NOT_FOUND, 0);
      return;
    }
  /* remove the current entry */
  if (prev == NULL)
    {
      ws_Mop_table[slot] = mop->hash_link;
    }
  else
    {
      prev->hash_link = mop->hash_link;
    }
  mop->hash_link = NULL;

  /* assign the new oid */
  COPY_OID (WS_REAL_OID (mop), newoid);

  /* force the MOP into the table at the new slot position */
  slot = OID_PSEUDO_KEY (newoid);
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  mop->hash_link = ws_Mop_table[slot];
  ws_Mop_table[slot] = mop;

}

/*
 * INTERNAL GARBAGE COLLECTOR
 */

/*
 * disconnect_deleted_instances - called when a class MOP is being garbage
 * collected
 *    return: void
 *    classop(in/out): class MOP
 *
 * Note:
 *    This should only happen if the class has been deleted and is no
 *    longer on the resident class list.
 *    At this point, all instances should have been flushed and decached.
 *    Here we make sure that any instance MOPs connected to this class
 *    get disconnected.
 */
static void
disconnect_deleted_instances (MOP classop)
{
  MOP m, next;

  if (classop == sm_Root_class_mop)
    {
      return;
    }

  for (m = classop->class_link, next = NULL;
       m != Null_object && m != NULL; m = next)
    {
      next = m->class_link;

      if (m->object != NULL)
	{
	  /*
	   * there should be no cached object here ! since the class is gone,
	   * we no longer no how to free this. If this becomes a normal case,
	   * we'll have to wait and decache the class AFTER all the instances
	   * have been decached
	   */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CORRUPTED, 0);
	  m->object = NULL;
	}

      m->class_link = NULL;
      m->class_mop = NULL;
    }
  classop->class_link = Null_object;

}

/*
 * ws_remove_resident_class - remove a class from the resident class list.
 *    return: void
 *    classop(in/out): class mop
 *
 * Note:
 *    This should ONLY be called if the class has been deleted and all of
 *    the instances have been decached.
 *    Once the class MOP is removed from the resident class list, it will
 *    be subject to garbage collection.
 *    This can be called both from ws_cull_mops where the class has
 *    already been decached and from the schema manager when it
 *    finishes deleting a class.
 */
void
ws_remove_resident_class (MOP classop)
{
  if (classop != sm_Root_class_mop)
    {
      /* make sure we don't have anyone referencing us */
      disconnect_deleted_instances (classop);
      ml_remove (&ws_Resident_classes, classop);
    }
}

/*
 * emergency_remove_dirty - reclaim a MOP that is on the dirty list.
 *    return: void
 *    op(in/out): mop that needs to be garbage collected
 *
 * Note:
 *    This should never be called.  It will be called by ws_cull_mops if
 *    the garbage collector attempts to reclaim a MOP that is on the
 *    dirty list.  If this happens, try to avoid system crash by removing
 *    it gracefully but an error must be signalled because the database
 *    will be in an inconsistent state since the dirty objects are being
 *    freed without flushing them to the server.
 */
static void
emergency_remove_dirty (MOP op)
{
  MOP mop, prev;

  /*
   * make sure we can get to op's class dirty list because without that
   * there is no dirty list from which we can remove op.
   */
  if (op->dirty_link != NULL &&
      op->class_mop != NULL && op->class_mop->dirty_link != NULL)
    {
      /* search for op in op's class' dirty list */
      prev = NULL;

      /*
       * NB: there is an important assumption in this ws.c module that when an
       * object is on a list (e.g., on a dirty list or a resident class list),
       * it never has a non-NULL dirty_link or class_link. The end of the list
       * is indicated by a pointer to the magical Null_object. This allows us
       * to look at the dirty_link field, for example, to determine whether
       * the object is dirty or not: if the dirty_link is NULL, the object is
       * not dirty, and if the dirty_link is non-NULL, the object is dirty.
       * This simple list membership test is very important for performance
       * reasons. So, if you ever need to make changes that affect the dirty
       * lists or the resident class list, make sure you preserve this very
       * important invariant. In particular, don't try to add a test for a
       * NULL dirty_link here because that will probably hide a violation of
       * this invariant. That is, if any change introduces a mop with a NULL
       * dirty_link into a dirty list we want to see a crash here! The
       * alternative will be an obscure bug that may be very difficult to
       * catch and fix.
       */
      for (mop = op->class_mop->dirty_link;
	   mop != Null_object && mop != op; mop = mop->dirty_link)
	{
	  prev = mop;
	}

      /* remove op from op's class' dirty list */
      if (mop == op)
	{
	  if (prev == NULL)
	    {
	      op->class_mop->dirty_link = op->dirty_link;
	    }
	  else
	    {
	      prev->dirty_link = op->dirty_link;
	    }
	  op->dirty_link = NULL;
	}
    }
}

/*
 * ws_cull_mops - callback function for the garbage collector
 *    return: void
 *
 * Note:
 *    This is a callback function for the garbage collector.  Here we map
 *    through the MOP table and check for unmarked mops.  Unmarked mops
 *    may be removed from the table and freed.  This is the only
 *    function that is allowed to remove MOPs from the table once they
 *    have been interned.
 *    I forget exactly what the two tables here are for.
 */
static void
ws_cull_mops (void)
{
  MOP mops, prev, next;
  DB_OBJLIST *m;
  unsigned int slot, count;

  /*
   * Before we map the hash table, whip through the resident instance
   * list of all classes and remove any MOPs that are about to
   * be deleted.  This saves having to remove them one at a time
   * from the loop below which is especially slow since the instance
   * list isn't doubly linked and it requires a full traversal to
   * remove them. It would be faster to keep these doubly linked
   * so we wouldn't have to traverse the MOP space twice but that makes
   * the MOPs larger which isn't good either.
   */

  for (m = ws_Resident_classes; m != NULL; m = m->next)
    {
      prev = NULL;
      for (mops = m->op->class_link; mops != NULL && mops != Null_object;
	   mops = next)
	{
	  next = mops->class_link;
	  if (!mops->released)
	    {
	      prev = mops;
	    }
	  else
	    {
	      if (prev == NULL)
		{
		  m->op->class_link = next;
		}
	      else
		{
		  prev->class_link = next;
		}
	      mops->class_link = NULL;
	    }
	}
    }

  /* should make sure table is the same as ws_Mop_table */
  count = 0;
  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      prev = NULL;
      for (mops = ws_Mop_table[slot]; mops != NULL; mops = next)
	{
	  next = mops->hash_link;
	  if (!mops->released)
	    {
	      prev = mops;
	    }
	  else
	    {
	      if (mops->reference)
		{
		  /* reference mop, these aren't allowed in the MOP table */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CORRUPTED,
			  0);
		  ws_Stats.corruptions++;
		  /* probably fatal to continue here */
		  continue;
		}

	      if (mops->is_set)
		{
		  /* reference mop, these aren't allowed in the MOP table */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CORRUPTED,
			  0);
		  ws_Stats.corruptions++;
		  continue;
		}

	      if (mops == sm_Root_class_mop)
		{
		  /* can't have rootclass on the garbage list */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CORRUPTED,
			  0);
		  ws_Stats.corruptions++;
		  /* probably fatal to continue here */
		}

	      /*
	       * After the recent reorganization of the client workspace dirty
	       * list from a global dirty list into a dirty list by class, the
	       * simple test for a dirty object:
	       *   "mops->dirty_link != NULL"
	       * applies only to dirty instances. It cannot be applied to dirty
	       * classes because the dirty_link of a class is always non-null.
	       * Even if a class' dirty list is empty, the class' dirty_link
	       * will be pointing to the magical Null_object. So we have to
	       * split this dirty test into two cases: one for classes and one
	       * for instances.
	       *
	       * These two cases have been separated so that they have
	       * different line numbers in the error message; that will help us
	       * determine which case has been violated.
	       */
	      if (mops->class_mop == sm_Root_class_mop && mops->dirty)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_WS_GC_DIRTY_MOP, 0);
		  ws_Stats.dirty_list_emergencies++;
		  emergency_remove_dirty (mops);
		}
	      if (mops->class_mop != sm_Root_class_mop && mops->dirty_link)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
			  ER_WS_GC_DIRTY_MOP, 0);
		  ws_Stats.dirty_list_emergencies++;
		  emergency_remove_dirty (mops);
		}

	      /* remove the mop from the hash table */
	      if (prev == NULL)
		{
		  ws_Mop_table[slot] = next;
		}
	      else
		{
		  prev->hash_link = next;
		}

	      /* free the associated object, note for classes,
	         this could be a fairly complex operation since all the instances
	         are decached as well */
	      ws_decache (mops);

	      /* if this is a vmop, we need to clear the keys */
	      if (mops->is_vid && WS_VID_INFO (mops))
		{
		  pr_clear_value (&WS_VID_INFO (mops)->keys);
		}

	      /* if we just gc'd a class, could go ahead and gc the instances
	         immediately since they have to go with the class */

	      if (mops->class_mop != NULL)
		{
		  if (mops->class_mop != sm_Root_class_mop)
		    {
		      /*
		       * Since we removed the GC'd MOPs from the resident
		       * instance list before we started the hash table
		       * map, we shouldn't see any at this point.  If
		       * we do, its either a corruption or the class
		       * wasn't on the resident class list for some
		       * reason. Remove it and increment the counter.
		       */
		      if (mops->class_link != NULL)
			{
			  remove_class_object (mops->class_mop, mops);
			  ws_Stats.instance_list_emergencies++;
			}
		    }
		  else
		    {
		      /*
		       * carefully remove the class from the resident
		       * instance list and make sure no instances are
		       * still going to be referencing this thing
		       */
		      ws_remove_resident_class (mops);
		    }
		}

	      /* return mop to the garbage collector */
	      ws_free_mop (mops);
	      count++;
	    }
	}
    }

  ws_Stats.mops_last_gc = count;
  ws_Stats.mops_freed += count;
}

/*
 * ws_intern_instances - flush and cull all MOPs of given class MOP
 *    return: void
 *    class_mop(in): class MOP
 */
void
ws_intern_instances (MOP class_mop)
{
  if (locator_flush_all_instances (class_mop, DECACHE) != NO_ERROR)
    return;

  ws_filter_dirty ();
  ws_cull_mops ();
}

/*
 * ws_release_instance - set the released field of a mop
 *    return: void
 *    mop(out): mop to set released field
 */
void
ws_release_instance (MOP mop)
{
  if (mop != NULL)
    {
      mop->released = 1;
    }
}

/*
 * ws_gc_mop - callback function for the garbage collector that will be
 * called for every MOP in the list of referenced MOPs.
 *    return: void
 *    mop(in/out): MOP that has been marked as referenced
 *    gcmarker(in): function to call to mark other mops
 *
 * Note:
 * This must examine the contents of this object and mark any other objects
 * that are referenced by this object.
 */
void
ws_gc_mop (MOP mop, void (*gcmarker) (MOP))
{
  MOP owner;

  if (mop == NULL)
    {
      return;
    }
  /* ignore the rootclass mop */

  if (mop == sm_Root_class_mop)
    {
      return;
    }

  if (mop->reference)
    {
      /* its a reference mop, mark the owning object */
      owner = WS_GET_REFMOP_OWNER (mop);
      if (owner != NULL)
	{
	  (*gcmarker) (owner);
	}
    }
  else if (mop->is_set)
    {
      pr_gc_set ((SETOBJ *) mop->object, gcmarker);
    }
  else if (mop->class_mop == sm_Root_class_mop)
    {
      /* its a class */
      if (mop->object != NULL)
	sm_gc_class (mop, gcmarker);
    }
  /* ignore temporary MOPs */
  else if (!mop->is_temp)
    {

      /* its an instance */
      if (mop->is_vid)
	{
	  vid_gc_vmop (mop, gcmarker);
	}
      else if (mop->object != NULL)
	{
	  sm_gc_object (mop, gcmarker);
	}
    }
}

/*
 * DIRTY LIST MAINTENANCE
 */

/*
 * The dirty list which used to be a global dirty list is now kept by class.
 * A dirty list (possibly empty) is rooted at each class' dirty_link and is
 * chained through the dirty_link field in the object_pointer.  This makes
 * maintenance of the dirty_list very simple at the expense of an object_pointer
 * that is one word larger.
 *
 * When an object is marked as "clean" it is not immediately removed from the
 * dirty list.  Since we don`t have a doubly linked list, we will need to
 * perform a linear search of the dirty list in order to remove the element.
 * Physicaly altering the dirty list as objects are "cleaned" also has
 * unpleasant side effects for the dirty object iterator function below.
 *
 * Instead, the dirty object iterator will remove objects from the dirty list
 * as it sweeps through them.
 *
 * Note that doing this also requires an extra "dirty bit" in addition to the
 * dirty list link field.
 */

/*
 * ws_dirty - Add an object to the dirty list of its class.
 *    return: void
 *    op(in/out): mop to make dirty
 */
void
ws_dirty (MOP op)
{
  /*
   * don't add the root class to any dirty list. otherwise, later traversals
   * of that dirty list will loop forever.
   */

  if (op == NULL || op == sm_Root_class_mop)
    {
      return;
    }
  WS_SET_DIRTY (op);
  /*
   * add_class_object makes sure each class' dirty list (even an empty one)
   * is always terminated by the magical Null_object. Therefore, this test
   * "op->dirty_link == NULL" makes sure class objects are not added to
   * the Rootclass' dirty list.
   */
  if (op->dirty_link != NULL)
    {
      return;
    }

  if (op->class_mop == NULL)
    {
      /* SERIOUS INTERNAL ERROR */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CLASS_NOT_CACHED, 0);
      ws_Stats.uncached_classes++;
    }
  else
    {
      /*
       * add op to op's class' dirty list only if op is not yet there.
       * The preceding "op->dirty_link == NULL" asserts that op is not
       * on any dirty list so we can simply prepend op to op's class'
       * dirty list.
       */
      op->dirty_link = op->class_mop->dirty_link;
      op->class_mop->dirty_link = op;
    }

}

/*
 * ws_clean - clears the dirty bit of a mop
 *    return: void
 *    op(in/out): mop to mark clean
 *
 * Note:
 * Making dirty bit cleared will cause the object to be ignored by the
 * dirty list iterator
 */
void
ws_clean (MOP op)
{
  /*
   * because pinned objects can be in a state of direct modification, we
   * can't reset the dirty bit after a workspace panic flush because this
   * would lose any changes made to the pinned object after the flush
   */

  if (!op->pinned)
    {
      WS_RESET_DIRTY (op);
    }
  else
    {
      ws_Stats.pinned_cleanings++;	/* need to know how often this happens */
    }
}

/*
 * ws_map_dirty_internal - iterate over elements in the dirty list calling map
 * function with the element.
 *    return: map status code
 *    function(in): function to apply to the dirty list elements
 *    args(in): arguments to pass to map function
 *    classes_only(in): flag indicating map over class objects only
 *
 * Note:
 *    As a side effect, non-dirty objects that are still in the dirty list
 *    are removed.  The map function must return WS_MAP_CONTINUE each time
 *    to map over the entire list.  If the map function returns any other
 *    value, the loop will terminate.  The function will return
 *    WS_MAP_SUCCESS if the loop completed or if the map function never
 *    returned WS_MAP_FAIL.  If the map function returns WS_MAP_FAIL, the
 *    loop will terminate and this will be returned from the function.
 */
static int
ws_map_dirty_internal (MAPFUNC function, void *args, bool classes_only)
{
  MOP op, op2, next, prev, class_mop;
  DB_OBJLIST *m;
  int status = WS_MAP_CONTINUE;
  int collected_num_dirty_mop = 0;

  /* traverse the resident classes to get to their dirty lists */
  for (m = ws_Resident_classes;
       m != NULL && status == WS_MAP_CONTINUE && (class_mop = m->op) != NULL;
       m = m->next)
    {

      /* is this a dirty class? */
      if (class_mop->class_mop == sm_Root_class_mop && class_mop->dirty)
	{
	  if (!classes_only)
	    {
	      collected_num_dirty_mop++;
	    }

	  Ws_dirty = true;
	  /* map given function over this dirty class */
	  if (function != NULL)
	    {
	      status = (*function) (class_mop, args);
	    }

	  /* Don't continue if something bad happened */
	  if (status == WS_MAP_FAIL)
	    {
	      break;
	    }
	}

      /* skip over all non-dirty objects at the start of each dirty list */
      for (op = class_mop->dirty_link; op != Null_object && op->dirty == 0;
	   op = next)
	{
	  next = op->dirty_link;
	  op->dirty_link = NULL;
	}
      class_mop->dirty_link = op;

      prev = NULL;
      next = Null_object;

      /* map given function over this class' dirty list */
      for (; op != Null_object && status == WS_MAP_CONTINUE; op = next)
	{

	  /*
	   * if we get here, then op must be dirty. So turn the static dirty
	   * flag on (just in case we've been called from ws_has_updated).
	   * ws_has_updated uses this static flag to check for the presence
	   * of dirty objects.
	   */
	  if (!classes_only)
	    {
	      collected_num_dirty_mop++;
	    }

	  Ws_dirty = true;

	  if (function != NULL)
	    {
	      if (!classes_only)
		{
		  status = (*function) (op, args);
		}

	      else if (op->class_mop == sm_Root_class_mop)
		{
		  status = (*function) (op, args);
		}
	    }

	  /* Don't continue if something bad happened */
	  if (status == WS_MAP_FAIL)
	    {
	      break;
	    }

	  next = op->dirty_link;

	  /* remember the last dirty object in the list */
	  if (op->dirty == 1)
	    {
	      prev = op;
	    }
	  else
	    {
	      op->dirty_link = NULL;	/* remove it from the list */
	    }

	  /* find the next non-dirty object */
	  for (op2 = next; op2 != Null_object && op2->dirty == 0; op2 = next)
	    {
	      next = op2->dirty_link;
	      op2->dirty_link = NULL;
	    }
	  next = op2;

	  /* remove intervening clean objects */
	  if (prev == NULL)
	    {
	      class_mop->dirty_link = next;
	    }
	  else
	    {
	      prev->dirty_link = next;
	    }
	}
    }

  if (status != WS_MAP_FAIL)
    {
      status = WS_MAP_SUCCESS;
      if (!classes_only && ws_Num_dirty_mop != collected_num_dirty_mop)
	{
	  ws_Num_dirty_mop = collected_num_dirty_mop;
	}
    }

  return (status);
}

/*
 * ws_map_dirty - specializations of ws_map_dirty_internal function
 *    return: map status code
 *    function(in): map function
 *    args(in): map function argument
 */
int
ws_map_dirty (MAPFUNC function, void *args)
{
  return (ws_map_dirty_internal (function, args, false));
}

/*
 * ws_filter_dirty - remove any mops that don't have their dirty bit set.
 *    return: void
 */
void
ws_filter_dirty (void)
{
  ws_map_dirty_internal (NULL, NULL, false);
}

/*
 *       	       RESIDENT INSTANCE LIST MAINTENANCE
 */
/*
 * Each class object in the workspace maintains a list of all the instances
 * for that class.  This list is rooted in the class_link field of the class
 * MOP and the instances are chained through their class_link field.
 */

/*
 * add_class_object - Add an instance MOP to the class' resident instance list.
 *    return: NO_ERROR if successful, error code otherwise
 *    class_mop(in/out): class mop
 *    obj(in/out): instance mop
 */
static int
add_class_object (MOP class_mop, MOP obj)
{
  int error = NO_ERROR;

  if (class_mop == sm_Root_class_mop)
    {
      /*
       * class MOP, initialize the object list, do this only if it isn't
       * already initialized, this may happen if the workspace is cleared
       * and nothing is cached.  In this case the class_link lists are still
       * valid.  When the class comes back in, we don't want to destroy the
       * previously built instance lists.
       */
      if (obj->class_link == NULL)
	{
	  obj->class_link = Null_object;
	}
      if (obj->dirty_link == NULL)
	{
	  obj->dirty_link = Null_object;
	}
      obj->class_mop = class_mop;

      /* add the class object to the root memory resident class list */
      error = ml_add (&ws_Resident_classes, obj, NULL);
    }
  else
    {
      /* must make sure this gets initialized, should have been done
         already when the class was cached in the clause above */
      if (class_mop->class_link == NULL)
	{
	  class_mop->class_link = Null_object;
	}

      if (obj->class_link == NULL)
	{
	  obj->class_link = class_mop->class_link;
	  class_mop->class_link = obj;
	}
      if (class_mop->object == NULL)
	{
	  error = ER_WS_CLASS_NOT_CACHED;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  ws_Stats.uncached_classes++;
	}
      else
	{
	  if ((obj->class_mop != NULL) && (obj->class_mop != class_mop))
	    {
	      /*
	       * note, in FCS, we allowed this to unconditionally
	       * overwrite the class field. I don't think this is
	       * valid in any case and in fact it caused a problem
	       * trashing a MOP fro a database that was probably
	       * corrupted. The resulting bug was VERY subtle.
	       * Its best to ignore it if it happens. If we need
	       * a way to migrate objects from one class to another,
	       * this will have to be a much more controlled operation.
	       */
	      error = ER_WS_CHANGING_OBJECT_CLASS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      ws_Stats.ignored_class_assignments++;
	    }
	  else
	    {
	      obj->class_mop = class_mop;
	    }
	}
    }
  return error;
}

/*
 * remove_class_object - Remove an instance from a class' resident instance
 * list.
 *    return:void
 *    class_mop(in/out): class mop
 *    obj(in): instance mop
 */
static void
remove_class_object (MOP class_mop, MOP obj)
{
  MOP o, prev;

  if (class_mop->class_link == NULL)
    {
      return;
    }

  for (o = class_mop->class_link, prev = NULL;
       o != Null_object && o != obj; o = o->class_link)
    {
      if (o != obj)
	{
	  prev = o;
	}
    }

  if (o == Null_object)
    {
      return;
    }

  if (prev == NULL)
    {
      class_mop->class_link = o->class_link;
    }
  else
    {
      prev->class_link = o->class_link;
    }
  o->class_link = NULL;
  o->class_mop = NULL;

}

/*
 * ws_set_class - set the class of an instance mop.
 *    return: void
 *    inst(in/out): instance mop
 *    class_mop(in/out): class mop
 *
 * Note:
 *    This will make sure the MOP is tagged with the class and that the
 *    instance is added to the class' resident instance list.
 */
void
ws_set_class (MOP inst, MOP class_mop)
{
  /* need to change this to return an error to lccl.c */
  if (inst->class_mop != class_mop)
    {
      (void) add_class_object (class_mop, inst);
    }
}

/*
 * ws_map_class_dirty - iterate over all of the dirty instances of a class and
 * calls supplied function.
 *    return: WS_MAP_SUCCESS or WS_MAP_FAIL
 *    class_op(in/out): class of a mop to iterate over
 *    function(in): map function
 *    args(in): map function argument
 *
 * Note:
 * The mapping (calling the map function) will continue as long as the
 * map function returns WS_MAP_CONTINUE
 */
int
ws_map_class_dirty (MOP class_op, MAPFUNC function, void *args)
{
  MOP op, op2, next, prev;
  DB_OBJLIST *l;
  int status = WS_MAP_CONTINUE;

  if (class_op == sm_Root_class_mop)
    {
      /* rootclass, must map through dirty resident class list */
      for (l = ws_Resident_classes; l != NULL && status == WS_MAP_CONTINUE;
	   l = l->next)
	{
	  /* should we be ignoring deleted class MOPs ? */
	  if (l->op && l->op->dirty && function != NULL)
	    {
	      status = (*function) (l->op, args);
	    }
	}
    }
  else if (class_op->class_mop == sm_Root_class_mop)
    {				/* normal class */
      /* skip over all non-dirty objects at the start of dirty list */
      for (op = class_op->dirty_link, next = Null_object;
	   op != Null_object && op->dirty == 0; op = next)
	{
	  next = op->dirty_link;
	  op->dirty_link = NULL;
	}
      class_op->dirty_link = op;

      prev = NULL;
      next = Null_object;

      /* map given function over this class' dirty list */
      for (; op != Null_object && status == WS_MAP_CONTINUE; op = next)
	{

	  /* what if it is deleted ? */
	  if (function != NULL)
	    {
	      status = (*function) (op, args);
	    }

	  /* Don't continue if something bad happened */
	  if (status == WS_MAP_FAIL)
	    {
	      break;
	    }

	  next = op->dirty_link;

	  /* remember the last dirty object in the list */
	  if (op->dirty == 1)
	    {
	      prev = op;
	    }
	  else
	    {
	      op->dirty_link = NULL;	/* remove it from the list */
	    }

	  /* find the next non-dirty object */
	  for (op2 = next; op2 != Null_object && op2->dirty == 0; op2 = next)
	    {
	      next = op2->dirty_link;
	      op2->dirty_link = NULL;
	    }
	  next = op2;

	  /* remove intervening clean objects */
	  if (prev == NULL)
	    {
	      class_op->dirty_link = next;
	    }
	  else
	    {
	      prev->dirty_link = next;
	    }
	}
    }
  /* else we got an object MOP, don't do anything */

  if (status != WS_MAP_FAIL)
    {
      status = WS_MAP_SUCCESS;
    }

  return (status);
}

/*
 * ws_map_class - iterates over all of the resident instances of a class
 * and calls the supplied function.
 *    return: WS_MAP_SUCCESS or WS_MAP_FAIL
 *    class_op(in): class of interest
 *    function(in): map function
 *    args(in): map function argument
 *
 * Note:
 * The map will continue as long as the map function returns WS_MAP_CONTINUE.
 */
int
ws_map_class (MOP class_op, MAPFUNC function, void *args)
{
  MOP op;
  DB_OBJLIST *l;
  int status = WS_MAP_CONTINUE;

  if (class_op == sm_Root_class_mop)
    {
      /* rootclass, must map through resident class list */
      for (l = ws_Resident_classes; l != NULL && status == WS_MAP_CONTINUE;
	   l = l->next)
	{
	  /* should we be ignoring deleted class MOPs ? */
	  status = (*function) (l->op, args);
	}
    }
  else if (class_op->class_mop == sm_Root_class_mop)
    {
      /* normal class */
      if (class_op->class_link != NULL)
	{
	  for (op = class_op->class_link;
	       op != Null_object && status == WS_MAP_CONTINUE;
	       op = op->class_link)
	    {
	      /*
	       * should we only call the function if the object has been
	       * loaded ? what if it is deleted ?
	       */
	      status = (*function) (op, args);
	    }
	}
    }
  /* else we got an object MOP, don't do anything */

  if (status != WS_MAP_FAIL)
    {
      status = WS_MAP_SUCCESS;
    }

  return (status);
}

/*
 * mark_instance_deleted - mark a mop as deleted
 *    return: WS_MAP_CONTINUE
 *    op(in/out): mop of interest
 *    args(in): not used
 *
 */
static int
mark_instance_deleted (MOP op, void *args)
{
  WS_SET_DELETED (op);
  if (op->pinned)
    {
/*  er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_PIN_VIOLATION, 0); */
      op->pinned = 0;
    }

  return (WS_MAP_CONTINUE);
}

/*
 * ws_mark_instances_deleted - mark class mops as deleted
 *    return: void
 *    class_op(in): mop class of interest
 *
 * Note:
 *    This is called by the schema manager when a class is deleted.  It will
 *    loop through all of the MOPs for instances of this class and mark
 *    them as deleted.  This makes it more effecient to detect deleted
 *    objects in the upper layers.  This may be something the locator should
 *    do when locator_remove_class is called ?
 */
void
ws_mark_instances_deleted (MOP class_op)
{
  ws_map_class (class_op, mark_instance_deleted, NULL);
}

/*
 * CLASS NAME CACHE
 */

/*
 * ws_add_classname - caches a classname in the workspace classname table.
 *    return: void
 *    classobj(in): pointer to class strucure
 *    classmop(in): mop for this class
 *    cl_name(in): class name
 *
 * Note:
 *    It should be called by ws_cache when a class is given to the workspace.
 */
void
ws_add_classname (MOBJ classobj, MOP classmop, const char *cl_name)
{
  MOP current;
  SM_CLASS *class_;

  class_ = (SM_CLASS *) classobj;

  if (class_ == NULL || classmop == NULL)
    {
      return;
    }

  current = (MOP) mht_get (Classname_cache, class_->header.name);

  if (current == NULL)
    {
      mht_put (Classname_cache, cl_name, classmop);
    }
  else
    {
      if (current != classmop)
	{
	  mht_rem (Classname_cache, class_->header.name, NULL, NULL);
	  mht_put (Classname_cache, cl_name, classmop);
	}
    }
}

/*
 * ws_drop_classname - remove a classname from the workspace cache.
 *    return: void
 *    classobj(in): pointer to class strucutre
 *
 * Note:
 * It should be called by ws_cache and ws_decache or whenever the name
 * needs to be removed.
 */
void
ws_drop_classname (MOBJ classobj)
{
  SM_CLASS *class_;

  class_ = (SM_CLASS *) classobj;
  if (class_ != NULL)
    {
      mht_rem (Classname_cache, class_->header.name, NULL, NULL);
    }
}

/*
 * ws_reset_classname_cache - clear the class name cache since this must be
 * reverified for the next transaction.
 *    return: void
 *
 * Note:
 *    This is called whenever a transaction is committed or aborted.
 *    We might consider must NULLing out the current class pointers and
 *    leaving the key strings in the table so we can avoid reallocating
 *    them again the next time names are cached.
 */
void
ws_reset_classname_cache (void)
{
  if (Classname_cache != NULL)
    {
      /*
       * don't need to map over entries because the name strings
       * are part of the class structure
       */
      (void) mht_clear (Classname_cache);
    }
}

/*
 * ws_find_class - search in the workspace classname cache for the MOP of
 * a class.
 *    return: class pointer
 *    name(in): class name to search
 *
 * Note:
 *    This avoids going to the server each time a name to OID mapping needs
 *    to be made.  The cache will remain valid for the duration of the
 *    current transaction.  This should be called by locator_find_class to
 *    check the schema before calling the server.
 */
MOP
ws_find_class (const char *name)
{
  MOP class_mop;

  class_mop = (MOP) mht_get (Classname_cache, name);

  return (class_mop);
}

/*
 * MAIN INITIALIZATION AND SHUTDOWN
 */

/*
 * ws_init - initialize workspace and associated modules (qfit, GC)
 *    return: NO_ERROR if successful, error code otherwise
 *
 * Note: This function should be called once early in the database
 *    initialization phase.
 */
int
ws_init (void)
{
  int error = NO_ERROR;
  unsigned int i;
  size_t allocsize;

  /*
   * this function needs to be left active after a database shutdown,
   * (in case of server crash).  Because of this, it must be able
   * to restart itself if initialized twice
   */
  if (ws_Mop_table != NULL)
    {
      ws_final ();
    }

  /* initialize the garbage collector */
  GC_INIT ();

  ws_Gc_enabled = PRM_GC_ENABLE;
  if (!ws_Gc_enabled)
    {
      GC_disable ();
    }

  if (db_create_workspace_heap () == 0)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /*
   * area_init() must have been called earlier.
   * These need to all be returning errors !
   */
  ws_area_init ();		/* object lists */
  pr_area_init ();		/* DB_VALUE */
  set_area_init ();		/* set reference */
  obt_area_init ();		/* object templates, assignment templates */
  classobj_area_init ();	/* schema templates */

  /* build the MOP table */
  ws_Mop_table_size = PRM_WS_HASHTABLE_SIZE;
  allocsize = sizeof (MOP) * ws_Mop_table_size;
  ws_Mop_table = (MOP *) malloc (allocsize);

  if (ws_Mop_table == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  for (i = 0; i < ws_Mop_table_size; i++)
    {
      ws_Mop_table[i] = NULL;
    }

  /* create the internal Null object mop */
  Null_object = ws_make_mop (NULL);
  if (Null_object == NULL)
    {
      return (er_errid ());
    }

  /* start with nothing dirty */
  Ws_dirty = false;

  /* build the classname cache */
  Classname_cache = mht_create ("Workspace class name cache",
				256, mht_1strhash, mht_strcmpeq);

  if (Classname_cache == NULL)
    {
      /* overwrite mht's error ? */
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      return (error);
    }

  /* Can't have any resident classes yet */
  ws_Resident_classes = NULL;

  ws_Set_mops = NULL;

  return (NO_ERROR);
}

/*
 * ws_final - Close the workspace and release all allocated storage.
 *    return: void
 *
 * Note: Must only be called prior to closing the database.
 */
void
ws_final (void)
{
  MOP mop, next;
  unsigned int slot;

  /*
   * kludge, bocl.c is checked out right now, this should
   * go in boot_client_all_finalize immediately prior to ws_final().
   */
  tr_final ();

  if (PRM_WS_MEMORY_REPORT)
    {
      /* this is for debugging only */
      fprintf (stdout,
	       "*** Database client statistics before shutdown ***\n");
      ws_dump (stdout);
      /*
       * Check for dangling allocations in the workspace.
       * First decache everything, must do this before the
       * MOP tables are destroyed.
       */
      ws_clear_internal (true);
    }
  ws_clear ();

  /* destroy the classname cache */
  if (Classname_cache != NULL)
    {
      mht_destroy (Classname_cache);
      Classname_cache = NULL;
    }

  /* destroy list of resident classes */
  ml_free (ws_Resident_classes);
  ws_Resident_classes = NULL;

  /* destroy the MOP table */
  if (ws_Mop_table != NULL)
    {
      for (slot = 0; slot < ws_Mop_table_size; slot++)
	{
	  for (mop = ws_Mop_table[slot], next = NULL; mop != NULL; mop = next)
	    {
	      next = mop->hash_link;
	      ws_free_mop (mop);
	    }
	}
      ws_free_mop (Null_object);
      free_and_init (ws_Mop_table);
      ws_Mop_table = NULL;
    }

  db_destroy_workspace_heap ();

  /* clean up misc globals */
  ws_Mop_table = NULL;
  ws_Mop_table_size = 0;
  ws_Set_mops = NULL;
  Null_object = NULL;
  Ws_dirty = false;
}

/*
 * ws_clear_internal - Debugging function that decaches all objects in the
 * workspace and clears all locks.
 *    return: void
 *    clear_vmop_keys(in): if set clear the keys of a vmop
 *
 * Note: Used to make sure objects are flushed correctly.
 */
static void
ws_clear_internal (bool clear_vmop_keys)
{
  MOP mop;
  unsigned int slot;

  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot]; mop != NULL; mop = mop->hash_link)
	{
	  ws_decache (mop);

	  /* if this is a vmop, we may need to clear the keys */
	  if (mop->is_vid && WS_VID_INFO (mop) && clear_vmop_keys)
	    {
	      pr_clear_value (&WS_VID_INFO (mop)->keys);
	    }

	  mop->lock = NULL_LOCK;
	  mop->deleted = 0;
	}
    }
  ws_Commit_mops = NULL;
  ws_filter_dirty ();
}

/*
 * ws_clear - ws_clear_internal wrapper. see comments of ws_clear_internal
 *    return: void
 *
 */
void
ws_clear (void)
{
  ws_clear_internal (false);
}

/*
 * ws_vid_clear - decaches all virtual objects in the workspace and clear all
 * locks
 *    return: void
 *
 * Note:
 *    Used to make sure that virtual objects are consistent.
 *    This function should be called only when all the dirty virtual objects
 *    have been flushed.
 */
void
ws_vid_clear (void)
{
  MOP mop;
  unsigned int slot;

  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot]; mop != NULL; mop = mop->hash_link)
	{
	  /* Don't decache non-updatable view objects because they cannot be
	     recreated.  Let garbage collection eventually decache them.
	   */
	  if ((WS_ISVID (mop)) && (vid_is_updatable (mop)))
	    {
	      ws_decache (mop);
	      mop->lock = NULL_LOCK;
	      mop->deleted = 0;
	    }
	}
    }
}

/*
 * ws_has_updated - see if there are any dirty objects in the workspace
 *    return: true if updated, false otherwise
 */
bool
ws_has_updated (void)
{
  /*
   * We used to be able to test the global dirty list (Dirty_objects) for
   * the presence of workspace updates. Now, we have to be a bit sneaky. To
   * do the same test, we set this static dirty flag to false and let the
   * ws_filter_dirty traversal turn this dirty flag on if it finds any
   * dirty objects in the workspace.
   */
  Ws_dirty = false;

  /*
   * wouldn't need to filter the whole list but this seems like
   * a reasonable time to do this
   */
  ws_filter_dirty ();

  return (Ws_dirty);
}

/*
 * MOP CACHEING AND DECACHEING
 */

/*
 * ws_cache - sets the object content of a mop
 *    return: void
 *    obj(in): memory representation of object
 *    mop(in): mop of the object
 *    class_mop(in): class of the object
 *
 * Note:
 *    First, we must check for any existing contents and free them.
 *    Note that when a class is decached, all instances of that class must
 *    also be decached because the class definition may have changed.
 *    We force this here but it really has to be checked by the transaction
 *    manager since the dirty instances must be flushed.
 */
void
ws_cache (MOBJ obj, MOP mop, MOP class_mop)
{
  /* no gc's during this period */
  ws_gc_disable ();

  /* third clause applies if the sm_Root_class_mop is still being initialized */
  if ((class_mop == sm_Root_class_mop)
      || (mop->class_mop == sm_Root_class_mop) || (mop == class_mop))
    {

      /* caching a class */
      if ((mop->object != NULL) && (mop->object != (MOBJ) & sm_Root_class))
	{
	  /* remove information for existing class */
	  ws_drop_classname ((MOBJ) mop->object);
	  ws_decache_all_instances (mop);
	  classobj_free_class ((SM_CLASS *) mop->object);
	}
      mop->object = obj;
      mop->class_mop = class_mop;

      /*
       * must always call this when caching a class because we don't know
       * if there are any objects on disk
       */
      ws_class_has_object_dependencies (mop);

      if (obj != (MOBJ) & sm_Root_class)
	{
	  /* this initializes the class_link list and adds it to the
	     list of resident classes */
	  if (add_class_object (class_mop, mop))
	    {
	      goto abort_it;
	    }

	  /* add to the classname cache */
	  ws_add_classname (obj, mop, ((SM_CLASS *) obj)->header.name);
	}
    }
  else
    {
      if (mop->object != NULL)
	{
	  /* free the current contents */
	  if (mop->class_mop == NULL || mop->class_mop->object == NULL)
	    {
	      /* SERIOUS INTERNAL ERROR */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_WS_CLASS_NOT_CACHED, 0);
	      ws_Stats.uncached_classes++;
	      goto abort_it;
	    }
	  else
	    {
	      obj_free_memory ((SM_CLASS *) mop->class_mop->object,
			       (MOBJ) mop->object);
	      mop->object = NULL;
	    }
	}

      mop->object = obj;
      ws_class_has_object_dependencies (class_mop);

      if (mop->class_mop != class_mop)
	{
	  if (mop->class_mop != NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
		      ER_WS_CHANGING_OBJECT_CLASS, 0);
	      if (mop->class_link != NULL)
		{
		  remove_class_object (mop->class_mop, mop);
		}
	    }
	  if (add_class_object (class_mop, mop))
	    {
	      goto abort_it;
	    }
	}
    }

  ws_gc_enable ();
  return;

abort_it:
  /*
   * NULL the MOP since we're in an unknown state, this function
   * should be returning an error
   */
  mop->object = NULL;
  ws_gc_disable ();
}

/*
 * ws_cache_dirty - caches an object and also marks it as dirty
 *    return: void
 *    obj(in): memory representation of object
 *    op(in): mop of object
 *    class(in): class of object
 */
void
ws_cache_dirty (MOBJ obj, MOP op, MOP class_)
{
  ws_cache (obj, op, class_);
  ws_dirty (op);
}

/*
 * ws_cache_with_oid - first find or create a MOP for the object's OID and
 * then cache the object.
 *    return: mop of cached object
 *    obj(in): memory representation of object
 *    oid(in): object identifier
 *    class(in): class of object
 *
 * Note:
 *    We used to disable GC during this period since in theory, the
 *    garbage collector can remove things that are referenced by the
 *    object we're caching during the call to ws_mop().
 *    Unfortunately, this prevented us from ever performing GC on
 *    repeated INSERT operations since locator_add_instance would always
 *    call this function to allocate the new instance MOP.
 *    Currently, we have ensured that the object cached here for both
 *    locator_add_instance and locator_add_class will be empty and not contain
 *    any important object references.  It is therefore OK to allow GC
 *    during this function.
 *    The flow of control between om/wm and lc has always been rather odd,
 *    we could try to remove some of the caching logic from
 *    locator_add_instance and instead try to pre-allocate the MOPs before any
 *    attempt is made to cache their contents.  Perhaps lc_new_instance
 *    can do everything locator_add_instance does except cache the MOP contents
 *    and then return a new MOP.  This MOP can then be cached later
 *    possibly in obj.c when we know we're ready to assign the object
 *    contents.  This may result in a temporary window where we have installed
 *    MOPs with temporary OIDs but no contents which hasn't happened before.
 *    This window would only exist during the execution of obt_update and
 *    sm_update.
 *
 *    The disable statements are left in comments in case we need to put them
 *    back in the event of some unforseen emergency.
 */
MOP
ws_cache_with_oid (MOBJ obj, OID * oid, MOP class_)
{
  MOP mop;

  mop = ws_mop (oid, class_);
  if (mop != NULL)
    {
      ws_cache (obj, mop, class_);
    }

  return (mop);
}

/*
 * ws_decache - Free the memory representation of an object.
 *    return: void
 *    mop(in/out): object to decache
 *
 * Note:
 *    This must only be called from functions that understand the rules
 *    involved with decaching objects.  Specifically, you cannot decache
 *    a class unless all the instances are decached first.
 *    You must not decache an object if it is dirty and has not been
 *    flushed to the server.
 */
void
ws_decache (MOP mop)
{
  /* these should be caught before we get here, issue a warning message */
#if 0
  if (mop->pinned)
    er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_PIN_VIOLATION, 0);
#endif

  if (mop->class_mop != sm_Root_class_mop)
    {
      if (mop->object != NULL)
	{
	  if (mop->class_mop == NULL || mop->class_mop->object == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_WS_CLASS_NOT_CACHED, 0);
	      ws_Stats.uncached_classes++;
	      mop->object = NULL;
	    }
	  else
	    {
	      obj_free_memory ((SM_CLASS *) mop->class_mop->object,
			       (MOBJ) mop->object);
	      if (mop->deleted)
		{
		  mop->class_mop = NULL;
		}
	      mop->object = NULL;
	    }
	}
    }
  else
    {
      /* free class object, not sure if this should be done here */
      if (mop->object != NULL && mop->object != (MOBJ) & sm_Root_class)
	{
	  ws_drop_classname ((MOBJ) mop->object);

	  /*
	   * WARNING: since we're decaching the class, we must not have
	   * any instances remaining in memory since their representation
	   * is dependent on the current class object.  Must decache
	   * instances when removing the class.
	   *
	   * This should already have been done but we do it to
	   * prevent crashes in error conditions.  If the instances are
	   * dirty we lose the changes.
	   */
	  ws_decache_all_instances (mop);
	  classobj_free_class ((SM_CLASS *) mop->object);
	}
    }

  mop->object = NULL;
  ws_clean (mop);

  /* this no longer apples */
  mop->composition_fetch = 0;
}

/*
 * ws_decache_all_instances - Decache all the instances of a class.
 *    return: void
 *    mop(in): class whose instances are to be decached
 *
 * Note:
 * Commonly used after a schema modification. If the rootclass mop is given,
 * decache all classes (which in turn will decache all instances).
 */
void
ws_decache_all_instances (MOP mop)
{
  DB_OBJLIST *l;
  MOP obj;

  if (mop == sm_Root_class_mop)
    {
      /* decache all class objects */
      for (l = ws_Resident_classes; l != NULL; l = l->next)
	{
	  ws_decache (l->op);
	}
    }
  else if (mop->class_mop == sm_Root_class_mop)
    {
      if (mop->class_link != NULL)
	{
	  for (obj = mop->class_link; obj != Null_object;
	       obj = obj->class_link)
	    {
	      ws_decache (obj);
	    }
	}
    }
}

/*
 *  MOP ACCESSOR FUNCTIONS
 */

/*
 * These provide access shells for the fields in the MOP structure.  These
 * are simple enough that callers should change to use the corresponding
 * macros in ws.h.
 */

/*
 * ws_oid - oid field accessor
 *    return: pointer to oid structure
 *    mop(in): object pointer
 */
OID *
ws_oid (MOP mop)
{
  if (mop && !WS_ISVID (mop))
    {
      return (WS_OID (mop));
    }
  if (mop)
    {
      mop = vid_base_instance (mop);
      if (mop && !WS_ISVID (mop))
	{
	  return (WS_OID (mop));
	}
    }
  return NULL;
}

/*
 * ws_class_mop - class accessor
 *    return: pointer to the class mop of an object
 *    mop(in): object mop
 */
MOP
ws_class_mop (MOP mop)
{
  return (mop->class_mop);
}

/*
 * ws_chn - get cache coherency number (chn) from an object.
 *    return: cache coherency number
 *    obj(in): memory representation of object
 *
 * Note:
 *    Use WS_CHN macro only if you can *guarantee* that the pointer won't be
 *    NULL.
 */
int
ws_chn (MOBJ obj)
{
  if (obj)
    {
      WS_OBJECT_HEADER *mobj;
      mobj = (WS_OBJECT_HEADER *) obj;
      return mobj->chn;
    }
  else
    {
      return NULL_CHN;
    }
}

/*
 * ws_get_lock - lock field accessor
 *    return: lock field of a mop
 *    mop(in): object pointer
 */
LOCK
ws_get_lock (MOP mop)
{
  return (mop->lock);
}

/*
 * ws_set_lock - lock field setter
 *    return: void
 *    mop(in): object pointer
 *    lock(in): lock type
 */
void
ws_set_lock (MOP mop, LOCK lock)
{
  if (mop != NULL)
    {
      WS_SET_LOCK (mop, lock);
    }
}

/*
 * ws_pin - sets the pin flag for a MOP
 *    return: previous pin flag value
 *    mop(in/out): object pointer
 *    pin(in): pin flag value
 *
 * Note:
 *    Pinning a MOP will make sure that it is not decached
 *    (garbage collected) for the duration of the transaction.
 *    The pin flag will be cleared with the other mop flags when a
 *    transaction is aborted or committed.
 *    It is OK to call this for a MOP that has no current contents.  This
 *    would happen in the case where we have just prefetched some objects
 *    and are attempting to load and cache all of them.  Since a panic
 *    can ocurr during the loading of one of the prefetched objects, we
 *    must make sure that the original object we were attempting to fetch
 *    is not swapped out as part of the panic.  To prevent this, we pin
 *    the mop before it is cached.  This is currently done by the
 *    authorization manager in au_fetch_insance but should probably be
 *    done by the lc_ level.
 */
int
ws_pin (MOP mop, int pin)
{
  int old = 0;

  /* We don't deal with MOPs on the server */
  if (db_on_server)
    {
      return old;
    }

  if (mop != NULL)
    {
      if (mop->class_mop != sm_Root_class_mop)
	{
	  old = mop->pinned;
	  mop->pinned = pin;
	}
      /* else, its a class MOP, they're implicitly pinned */
    }

  return (old);
}

/*
 * ws_pin_instance_and_class - pin object and the class of the object
 *    return: void
 *    obj(in/out): object pointer
 *    opin(out): previous pin flag value of a object
 *    cpin(out): previous pin flag value of a class of the object
 */
void
ws_pin_instance_and_class (MOP obj, int *opin, int *cpin)
{
  if (obj->class_mop != NULL && obj->class_mop != sm_Root_class_mop)
    {
      *opin = obj->pinned;
      obj->pinned = 1;
      if (obj->class_mop == NULL)
	{
	  *cpin = 0;
	}
      else
	{
	  *cpin = obj->class_mop->pinned;
	  obj->class_mop->pinned = 1;
	}
    }
  else
    {
      /* classes have no explicit pinning */
      *opin = 0;
      *cpin = 0;
    }
}

/*
 * ws_restore_pin - resotre pin flag of a object and it's class object
 *    return: void
 *    obj(in/out): object pointer
 *    opin(in): class pin flag value to set
 *    cpin(in): object pin flag value to set
 */
void
ws_restore_pin (MOP obj, int opin, int cpin)
{
  obj->pinned = opin;
  if (obj->class_mop != NULL)
    {
      obj->class_mop->pinned = cpin;
    }
}

/*
 * ws_mark_deleted
 *
 * arguments:
 *
 * returns/side-effects:
 *
 * description:
 *    This marks an object as deleted.  It will also add the object to the
 *    dirty list if it isn't already there.  The object will be flushed
 *    to disk at the end of the transaction.
 */

/*
 * ws_mark_deleted - marks an object as deleted
 *    return: void
 *    mop(in): object pointer
 *
 */
void
ws_mark_deleted (MOP mop)
{
  ws_dirty (mop);

  WS_SET_DELETED (mop);

  /* should be unpinning before deleting */
  if (mop->pinned)
    {
/*    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_PIN_VIOLATION, 0); */
      mop->pinned = 0;
    }

}

/*
 * MISC UTILITY FUNCTIONS
 */

/*
 * ws_find -
 *    return: mop status code (WS_FIND_MOP_DELETED, WS_FIND_MOP_NOTDELETED)
 *    mop(in): object pointer
 *    obj(out): return pointer to memory representatino of object
 *
 * Note:
 *    This is used to access the memory representation of an object.
 *    The memory representation is returned through the supplied pointer
 *    as long as the mop is not marked as deleted.
 */
int
ws_find (MOP mop, MOBJ * obj)
{
  int status = WS_FIND_MOP_NOTDELETED;

  *obj = NULL;
  if (mop && !mop->deleted)
    {
      *obj = (MOBJ) mop->object;
    }
  else
    {
      status = WS_FIND_MOP_DELETED;
    }

  return (status);
}

/*
 * ws_mop_compare - compare MOPs
 *    return: 0 if equal, non-zero if not equal
 *    mop1(in): object pointer
 *    mop2(in): object pointer
 *
 * Note:
 *    Currently, MOPs with the same OID will always be exactly the same
 *    structure so comparison with '==' in C is acceptable.  It has been
 *    discussed that this may not be acceptable for future use so this
 *    function will compare based on the OIDs.
 */
int
ws_mop_compare (MOP mop1, MOP mop2)
{
  return (oid_compare (WS_OID (mop1), WS_OID (mop2)));
}

/*
 * ws_class_has_object_dependencies - set no_object fields to 0
 *    return: void
 *    class(out): class mop
 *
 * Note:
 *    This controls a flag that is used to optimize the generation of
 *    represetations by the schema manager when a class is altered.
 *    A bit is kept in the class MOP that is set whenever we are sure
 *    that no object have been stored in the database that are dependent on
 *    the current class representation.  This bit is set after a
 *    representation is installed but is cleared whenever a class is
 *    cached, an object of the class is cached, an object of the class is
 *    updated, or an object of the class is created.  It will also be cleared
 *    at the end of a transaction by ws_clear_hints.
 */
void
ws_class_has_object_dependencies (MOP class_)
{
  class_->no_objects = 0;
}

/*
 * ws_class_has_cached_objects - check if there are cached redident instance
 *    return: non zero if there are cached redident instances
 *    class(in): class to examin
 */
int
ws_class_has_cached_objects (MOP class_)
{
  MOP obj;
  int cached = 0;

  for (obj = class_->class_link; obj != Null_object && !cached;
       obj = obj->class_link)
    {
      if (obj->object != NULL)
	{
	  cached = 1;
	}
    }
  return (cached);
}

/*
 * ws_map - map over all MOPs currently in the workspace.
 *    return: WS_MAP_ status code
 *    function(in): mapping function to alpply to the mops
 *    args(in): map function argument
 *
 * Note:
 *    The loop will continue as long as the mapping function returns
 *    WS_MAP_CONTINUE.
 */
int
ws_map (MAPFUNC function, void *args)
{
  MOP mop;
  unsigned int slot;
  int status = WS_MAP_CONTINUE;

  if (ws_Mop_table != NULL)
    {
      for (slot = 0; slot < ws_Mop_table_size && status == WS_MAP_CONTINUE;
	   slot++)
	{
	  for (mop = ws_Mop_table[slot];
	       mop != NULL && status == WS_MAP_CONTINUE; mop = mop->hash_link)
	    {
	      status = (*(function)) (mop, args);
	    }
	}
    }
  if (status != WS_MAP_FAIL)
    {
      status = WS_MAP_SUCCESS;
    }

  return (status);
}

/*
 * TRANSACTION MANAGEMENT SUPPORT
 */

/*
 * ws_reset_class_cache - clear any cached state that may exist in class
 * objects on a transaction boundary.
 *    return: void
 */
static void
ws_reset_class_cache (void)
{
  DB_OBJLIST *cl;
  SM_CLASS *class_;

  for (cl = ws_Resident_classes; cl != NULL; cl = cl->next)
    {
      /* only reset the cache if the class is in memory */
      class_ = (SM_CLASS *) cl->op->object;
      if (class_ != NULL)
	{
	  sm_reset_transaction_cache ((SM_CLASS *) cl->op->object);
	}
    }
}

/*
 * ws_clear_hints - clear all of the hint bits in the MOP.
 *    return: void
 *    mop(in): object pointer
 *    leave_pinned(in): flag to keep from modifying pinned field
 *
 * Note:
 *    This is called by the transaction manager to clear all of the hint
 *    bits in the MOP.  This is guarenteed to be called at the end of a
 *    transaction commit.  Note that we always clear the no_objects field
 *    for classes because once they are commited to a database, we must
 *    assume that other users have access to the current representation and
 *    can create instances with that represenatation.
 */
void
ws_clear_hints (MOP mop, bool leave_pinned)
{
  /*
   * Don't decache non-updatable view objects because they cannot be
   * recreated.  Let garbage collection eventually decache them.
   */
  if (WS_ISVID (mop))
    {
      if (!vid_is_updatable (mop))
	{
	  return;
	}
      else
	{
	  ws_decache (mop);
	}
    }
  mop->lock = NULL_LOCK;
  mop->composition_fetch = 0;
  mop->deleted = 0;
  WS_RESET_DIRTY (mop);
  mop->no_objects = 0;

  if (!leave_pinned)
    {
      mop->pinned = 0;
    }
}

/*
 * ws_clear_all_hints - reset all hint flags in the mops after a transaction
 * has been committeed.
 *    return: void
 *    retain_lock(in): if set to true retain locks (no operation)
 *
 * Note:
 *    Called by the transaction manager to reset all hint flags in the mops
 *    after a transaction has been committeed.  Also reset the
 *    authorization cache.
 */
void
ws_clear_all_hints (bool retain_lock)
{
  MOP mop;
  MOP next;

  if (retain_lock)
    {
      return;
    }

  ws_reset_class_cache ();
  au_reset_authorization_caches ();

  mop = ws_Commit_mops;
  while (mop)
    {
      ws_clear_hints (mop, false);
      next = mop->commit_link;
      mop->commit_link = NULL;	/* remove mop from commit link (it's done) */

      if (next == mop)
	{
	  mop = NULL;
	}
      else
	{
	  mop = next;
	}
    }
  ws_Commit_mops = NULL;
  ws_Num_dirty_mop = 0;
}

/*
 * ws_abort_mops - called by the transaction manager when a transaction is
 * aborted
 *    return: void
 *    only_unpinned(in): flag whether is it safe to abort pinned mops
 *
 * Note:
 *    This is called by the transaction manager when a transaction is
 *    aborted.  If the mop has an exclusive lock or if it is dirty, decache it
 *    so that the changes made during the transaction are thrown away. Note
 *    that it is not enough to check for dirty mop since objects are flushed
 *    by the transaction. When an object is flushed, its cache coherence
 *    number is increased by one. If an object is updated, flushed, and the
 *    transaction is rolled back, and we do not dechae the object. We may
 *    incorrectly assume that the object is the workspace is upto date since
 *    it has the same cache coherencey number that the one in the server. This
 *    happens since another client updated the object and commits after the
 *    rollback of the first client and before the first client accesses the
 *    object after the rollback. That is, the second client increases the
 *    cache coherency number by 1 which makes it the same that the one in the
 *    workspace of the first client.. But the objects are different.
 *
 *    Technically this should also free the mop strucure but it may be
 *    referenced in the application program space so we must rely on
 *    garbage collection to reclaim the storage.
 *
 *    NOTE: I think this could also remove the MOP from the various lists
 *    including the workspace table and the OID be set to NULL.  Is it
 *    possible for the OID to get reassigned in the next transaction which
 *    causes a cached MOP pointer in the application to suddenly change
 *    when the OID gets reused ?  Since technically, it is illegal to
 *    retain pointers to mops that have never been flushed when a
 *    transaction aborts this isn't really a problem but it could be
 *    confusing if a user inadvertently encounters this situation.
 */
void
ws_abort_mops (bool only_unpinned)
{
  MOP mop;
  MOP next;

  ws_reset_class_cache ();
  au_reset_authorization_caches ();

  mop = ws_Commit_mops;
  while (mop)
    {
      next = mop->commit_link;
      mop->commit_link = NULL;	/* remove mop from commit link (it's done) */

      /*
       * In some cases we cannot clear up pinned stuff, because we
       * may already be looping through the object or dirty list somewhere
       * in the calling chain, and we would be removing something out
       * from under them.
       */
      if (!only_unpinned)
	{
	  /* always remove this so we can decache things without error */
	  mop->pinned = 0;

	  /*
	   * If the object has an exclusive lock, decache the object. As
	   * a security measure we also check for the dirty bit.
	   */
	  if (ws_get_lock (mop) == X_LOCK || WS_ISDIRTY (mop))
	    ws_decache (mop);
	}

      /* clear all hint fields including the lock */
      ws_clear_hints (mop, only_unpinned);

      if (next == mop)
	{
	  mop = NULL;
	}
      else
	{
	  mop = next;
	}
    }
  if (!only_unpinned)
    {
      ws_Commit_mops = NULL;
      ws_Num_dirty_mop = 0;
    }
}

/*
 * ws_decache_allxlockmops_but_norealclasses - called by the transaction
 * manager when a savepoint is only rolled back by CUBRID system
 * (not by the application/user).
 *    return: void
 *
 * Note:
 *    All mops that are not class mops of views and proxies are decached when
 *    ther are not pinned.
 *
 *    Technically this should also free the mop strucure but it may be
 *    referenced in the application program space so we must rely on
 *    garbage collection to reclaim the storage.
 */
void
ws_decache_allxlockmops_but_norealclasses (void)
{
  MOP mop;
  unsigned int slot;

  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot]; mop != NULL; mop = mop->hash_link)
	{

	  if (mop->pinned == 0 &&
	      (ws_get_lock (mop) == X_LOCK || WS_ISDIRTY (mop)) &&
	      (mop->class_mop != sm_Root_class_mop || mop->object == NULL ||
	       ((SM_CLASS *) mop->object)->class_type == SM_CLASS_CT))
	    {
	      ws_decache (mop);
	      ws_clear_hints (mop, false);
	    }
	}
    }
}

/*
 * EXTERNAL GARBAGE COLLECTOR
 */

/*
 * ws_gc - cause a garbage collection if the flag is enabled
 *    return: void
 */
void
ws_gc (void)
{
  if (ws_Gc_enabled)
    {
      GC_gcollect ();
    }
  /* TODO: consider using GC_try_to_collect() or GC_collect_a_little() */
}

/*
 * ws_gc_enable - enable GC
 *    return: void
 */
void
ws_gc_enable (void)
{
  if (!ws_Gc_enabled && PRM_GC_ENABLE)
    {
      ws_Gc_enabled = true;
      GC_enable ();
    }
}

/*
 * ws_gc_disable - disable GC
 *    return: void
 */
void
ws_gc_disable (void)
{
  if (ws_Gc_enabled)
    {
      ws_Gc_enabled = false;
      GC_disable ();
    }
}

/*
 * STRING UTILITIES
 */

/*
 * ws_copy_string - copies a string storage allocated whthin the workspace
 *    return: copied string
 *    str(in): string to copy
 */
char *
ws_copy_string (const char *str)
{
  char *copy;

  copy = NULL;
  if (str != NULL)
    {
      copy = (char *) db_ws_alloc (strlen (str) + 1);
      if (copy != NULL)
	{
	  strcpy ((char *) copy, (char *) str);
	}
    }

  return (copy);
}

/*
 * ws_free_string - frees a string that was allocated by ws_copy_string.
 *    return: void
 *    str(out): workspace string to free
 */
void
ws_free_string (const char *str)
{
  char *s;

  if (str != NULL)
    {
      s = (char *) str;		/* avoid compiler warnings */
      db_ws_free (s);
    }
}

/*
 * DEBUG FUNCTIONS
 */

/*
 * ws_print_oid - print oid to standard out
 *    return: void
 *    oid(in): oid to print
 */
static void
ws_print_oid (OID * oid)
{
  fprintf (stdout, "%d/%d/%d",
	   (int) oid->volid, (int) oid->pageid, (int) oid->slotid);
}

/*
 * ws_describe_mop - print MOP information
 *    return: void
 *    mop(in): object pointer to describe
 *    args(in): not used
 */
static int
ws_describe_mop (MOP mop, void *args)
{
  ws_print_oid (WS_OID (mop));
  fprintf (stdout, " ");
  if (ws_mop_compare (mop, sm_Root_class_mop) == 0)
    {
      fprintf (stdout, "Root class ");
    }
  else
    {
      if (mop->class_mop == NULL)
	{
	  fprintf (stdout, "class MOP not available\n");
	}
      else
	{
	  if (ws_mop_compare (mop->class_mop, sm_Root_class_mop) == 0)
	    {
	      fprintf (stdout, "class ");
	      if (mop->object == NULL)
		{
		  fprintf (stdout, "not cached ");
		}
	      else
		{
		  fprintf (stdout, "%s ", sm_class_name (mop));
		}
	    }
	  else
	    {
	      fprintf (stdout, "instance of ");
	      if (mop->class_mop->object == NULL)
		{
		  fprintf (stdout, "uncached class ");
		}
	      else
		{
		  fprintf (stdout, "%s ", sm_class_name (mop->class_mop));
		}
	    }
	}
    }
  if (mop->dirty)
    {
      fprintf (stdout, " dirty");
    }
  if (mop->deleted)
    {
      fprintf (stdout, " deleted");
    }
  if (mop->pinned)
    {
      fprintf (stdout, " pinned");
    }
  if (mop->no_objects)
    {
      fprintf (stdout, " no_objects");
    }

  fprintf (stdout, "\n");
  return (WS_MAP_CONTINUE);
}

/*
 * ws_dump_mops - print information of all mops
 *    return: void
 */
void
ws_dump_mops (void)
{
  fprintf (stdout, "WORKSPACE MOP TABLE:\n\n");
  (void) ws_map (ws_describe_mop, NULL);
  fprintf (stdout, "\n");
}

/*
 * ws_makemop
 *
 * arguments:
 *	 volid: volume identifier
 *	pageid: page identifier
 *	slotid: slot identifier
 *
 * returns/side-effects: object pointer
 *
 * description:
 *    This will build (or find) a MOP whose OID contains the indicated
 *    identifiers.  This is intended as a debugging functions to get a
 *    handle on an object pointer given the three OID numbers.
 */

/*
 * ws_makemop - find or create a MOP whose OID contains the indicated
 * identifiers
 *    return: mop found or created
 *    volid(in): volumn id
 *    pageid(in): page id
 *    slotid(in): slot id
 *
 * Note:
 *    This is intended as a debugging functions to get a
 *    handle on an object pointer given the three OID numbers.
 */
MOP
ws_makemop (int volid, int pageid, int slotid)
{
  OID oid;
  MOP mop;

  oid.volid = volid;
  oid.pageid = pageid;
  oid.slotid = slotid;
  mop = ws_mop (&oid, NULL);

  return (mop);
}

/*
 * ws_count_mops - count the number of mops in the workspace
 *    return: mop count in the workspace
 */
int
ws_count_mops (void)
{
  MOP mop;
  unsigned int slot, count;

  count = 0;
  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot]; mop != NULL; mop = mop->hash_link)
	{
	  count++;
	}
    }
  return (count);
}

/*
 * WORKSPACE STATISTICS
 */

/*
 * ws_dump - print worksapce information to FILE output
 *    return: void
 *    fpp(in): FILE * to print the workspace information
 */
void
ws_dump (FILE * fpp)
{
  int mops, root, unknown, classes, cached_classes, instances,
    cached_instances;
  int count, actual, decached, weird;
  unsigned int slot;
  int classtotal, insttotal, size, isize, icount, deleted;
  MOP mop, inst, setmop;
  DB_OBJLIST *m;

  /* get mop totals */
  mops = root = unknown = classes = cached_classes = instances =
    cached_instances = 0;
  weird = 0;
  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot]; mop != NULL; mop = mop->hash_link)
	{
	  mops++;

	  if (mop == sm_Root_class_mop)
	    {
	      continue;
	    }

	  if (mop->class_mop == NULL)
	    {
	      unknown++;
	      if (mop->object != NULL)
		{
		  weird++;
		}
	    }
	  else if (mop->class_mop == sm_Root_class_mop)
	    {
	      classes++;
	      if (mop->object != NULL)
		{
		  cached_classes++;
		}
	    }
	  else
	    {
	      instances++;
	      if (mop->object != NULL)
		{
		  cached_instances++;
		}
	    }

	}
    }

  fprintf (fpp, "%d mops in the workspace (including one rootclass mop)\n",
	   mops);
  fprintf (fpp, "%d class mops (%d cached, %d uncached)\n", classes,
	   cached_classes, classes - cached_classes);
  fprintf (fpp, "%d instance mops (%d cached, %d uncached)\n", instances,
	   cached_instances, instances - cached_instances);

  fprintf (fpp, "%d unkown mops\n", unknown);
  if (weird)
    {
      fprintf (fpp, "*** %d unknown mops with cached objects\n", weird);
    }
  fprintf (fpp, "%d attempts to clean pinned mops\n",
	   ws_Stats.pinned_cleanings);

  /* gc stats */
  fprintf (fpp, "%d garbage collections have occurred\n", ws_Stats.gcs);
  fprintf (fpp, "%d MOPs allocated, %d freed, %d freed during last gc\n",
	   ws_Stats.mops_allocated, ws_Stats.mops_freed,
	   ws_Stats.mops_last_gc);
  fprintf (fpp,
	   "%d reference MOPs allocated, %d freed, %d freed during last gc\n",
	   ws_Stats.refmops_allocated, ws_Stats.refmops_freed,
	   ws_Stats.refmops_last_gc);

  /* misc stats */
  fprintf (fpp,
	   "%d dirty list emergencies, %d uncached classes, %d corruptions\n",
	   ws_Stats.dirty_list_emergencies, ws_Stats.uncached_classes,
	   ws_Stats.corruptions);
  fprintf (fpp, "%d ignored class assignments\n",
	   ws_Stats.ignored_class_assignments);


  for (setmop = ws_Set_mops, count = 0; setmop != NULL;
       setmop = setmop->class_link, count++);
  fprintf (fpp,
	   "%d active set mops, %d total set mops allocated, %d total set mops freed\n",
	   count, ws_Stats.set_mops_allocated, ws_Stats.set_mops_freed);

  /* dirty stats */
  count = actual = 0;
  for (m = ws_Resident_classes; m != NULL; m = m->next)
    {
      for (mop = m->op->dirty_link; mop != Null_object; mop = mop->dirty_link)
	{
	  count++;
	  if (mop->dirty)
	    {
	      actual++;
	    }
	}
    }
  fprintf (fpp, "%d dirty objects, %d clean objects in dirty list\n",
	   actual, count - actual);

  /* get class totals */
  fprintf (fpp, "RESIDENT INSTANCE TOTALS: \n");
  count = classtotal = insttotal = deleted = 0;
  for (m = ws_Resident_classes; m != NULL; m = m->next)
    {
      mop = m->op;
      if (mop->deleted)
	{
	  deleted++;
	}
      else
	{
	  count++;
	  if (mop != sm_Root_class_mop && mop->object != NULL)
	    {
	      size = classobj_class_size ((SM_CLASS *) mop->object);
	      classtotal += size;
	      icount = isize = decached = 0;
	      for (inst = mop->class_link; inst != Null_object;
		   inst = inst->class_link)
		{
		  icount++;
		  if (inst->object != NULL)
		    {
		      isize +=
			sm_object_size_quick ((SM_CLASS *) mop->object,
					      (MOBJ) inst->object);
		    }
		  else
		    {
		      decached++;
		    }
		}
	      fprintf (fpp,
		       "  %-20s : %d instances, %d decached, %d bytes used\n",
		       sm_classobj_name ((MOBJ) mop->object), icount,
		       decached, isize);
	      insttotal += isize;
	    }
	}
    }
  if (deleted)
    {
      fprintf (fpp, "*** %d deleted MOPs in the resident class list \n",
	       deleted);
    }

  /* just to make sure */
  if (count != cached_classes)
    {
      fprintf (fpp,
	       "*** Mops claiming to be classes %d, resident class list length %d\n",
	       cached_classes, count);
    }

  fprintf (fpp, "Total bytes for class storage     %d\n", classtotal);
  fprintf (fpp, "Total bytes for instance storage  %d\n", insttotal);
  fprintf (fpp, "Total bytes for object storage    %d\n",
	   classtotal + insttotal);

  fprintf (fpp, "WORKSPACE AREAS:\n");
  area_dump (fpp);
}

/*
 * PROPERTY LISTS
 */

/*
 * This is a rather hasty initial implemenatation of a soon-to-be more
 * general purpose property list mechanism for MOPs.
 */

/*
 * ws_put_prop - add a property (key value pair) to mop
 *    return: -1 if error, 0 if value replaced, 1 if a property created
 *    op(in/out): object pointer to add property
 *    key(in): key
 *    value(in): value
 */
int
ws_put_prop (MOP op, int key, void *value)
{
  WS_PROPERTY *p;
  int status = -1;

  /* Error if connect status is invalid */
  if (db_Connect_status)
    {
      for (p = (WS_PROPERTY *) op->version; p != NULL && p->key != key;
	   p = p->next);
      if (p != NULL)
	{
	  p->value = value;
	  status = 0;
	}
      else
	{
	  /*
	   * for now, allocate them in the workspace to avoid shutdown
	   * messages.  We might not be able to do this ultimately if
	   * they are allowed to contain other object pointers.
	   */
	  p = (WS_PROPERTY *) db_ws_alloc (sizeof (WS_PROPERTY));
	  if (p != NULL)
	    {
	      p->key = key;
	      p->value = value;
	      p->next = (WS_PROPERTY *) op->version;
	      op->version = (void *) p;
	      status = 1;
	    }
	}
    }
  return status;
}

/*
 * ws_get_prop - get a property value
 *    return: -1 if error, 0 if not found, 1 if found
 *    op(in): object pointer
 *    key(in): property key
 *    value(out): returned property value
 */
int
ws_get_prop (MOP op, int key, void **value)
{
  WS_PROPERTY *p;
  int status = -1;

  /* Error if connect status is invalid */
  if (db_Connect_status)
    {
      for (p = (WS_PROPERTY *) op->version; p != NULL && p->key != key;
	   p = p->next);
      if (p == NULL)
	{
	  status = 0;
	}
      else
	{
	  if (value != NULL)
	    {
	      *value = p->value;
	    }
	  status = 1;
	}
    }
  return status;
}

/*
 * ws_rem_prop - remove an object property
 *    return: -1 if error, 0 if property not found, 1 if successful
 *    op(in/out): object pointer
 *    key(in): property key
 */
int
ws_rem_prop (MOP op, int key)
{
  WS_PROPERTY *p, *prev;
  int status = -1;

  /* currently no possible error conditions */
  for (p = (WS_PROPERTY *) op->version, prev = NULL;
       p != NULL && p->key != key; p = p->next)
    {
      prev = p;
    }

  if (p == NULL)
    {
      status = 0;
    }
  else
    {
      if (prev != NULL)
	{
	  prev->next = p->next;
	}
      else
	{
	  op->version = (void *) p->next;
	}
      db_ws_free (p);
      status = 1;
    }
  return status;
}

/*
 * ws_flush_properties - frees all property of an object
 *    return: void
 *    op(in/out): object pointer
 */
static void
ws_flush_properties (MOP op)
{
  WS_PROPERTY *p, *next;

  for (p = (WS_PROPERTY *) op->version, next = NULL; p != NULL; p = next)
    {
      next = p->next;
      db_ws_free (p);
    }
}

/*
 * ws_has_dirty_objects - check if object has any dirty instance
 *    return: nonzero iff op has any dirty instances
 *    op(in): object pointer
 *    isvirt(out): 1 iff op is a proxy of vclass
 */
int
ws_has_dirty_objects (MOP op, int *isvirt)
{
  *isvirt = op && !op->deleted && op->object &&
    (((SM_CLASS *) (op->object))->class_type == SM_VCLASS_CT ||
     ((SM_CLASS *) (op->object))->class_type == SM_LDBVCLASS_CT);
  return op && !op->deleted && op->object &&
    op->dirty_link && op->dirty_link != Null_object;
}

/*
 * ws_hide_new_old_trigger_obj - temporarily hide "new" or "old" object
 *    return: 1 iff obj is a non-temp dirty pinned proxy instance
 *    op(in/out): a trigger's "new" or "old" object instance
 */
int
ws_hide_new_old_trigger_obj (MOP op)
{
  if (op && op->is_vid && !op->is_temp && op->dirty && op->pinned)
    {
      WS_RESET_DIRTY (op);
      return 1;
    }
  else
    {
      return 0;
    }
}

/*
 * ws_unhide_new_old_trigger_obj: unhide "new" or "old" trigger object
 *
 * arguments:
 *   obj  : (IN) a trigger's "new" or "old" object instance
 *
 * returns: none
 *
 * description:
 *   requires: obj is a trigger's "new" or "old" object instance whose dirty
 *             bit was temporarily turned off by ws_hide_new_old_trigger_obj
 *   modifies: obj's dirty bit
 *   effects : if obj is a non-temp pinned proxy instance then turn on its
 *             dirty bit.
 */

/*
 * ws_unhide_new_old_trigger_obj - unhide "new" or "old" trigger object
 *    return: void
 *    op(in/out): object pointer
 */
void
ws_unhide_new_old_trigger_obj (MOP op)
{
  if (op && op->is_vid && !op->is_temp && op->pinned)
    {
      WS_SET_DIRTY (op);
    }
}

/*
 * ws_need_flush - check if workspace has dirty mop
 *    return: 1 | 0
 */
bool
ws_need_flush (void)
{
  return (ws_Num_dirty_mop > 0);
}
