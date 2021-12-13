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
 * work_space.c - Workspace Manager
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "memory_alloc.h"
#include "area_alloc.h"
#include "message_catalog.h"
#include "memory_hash.h"
#include "error_manager.h"
#include "oid.h"
#include "work_space.h"
#include "schema_manager.h"
#include "authenticate.h"
#include "object_accessor.h"
#include "locator_cl.h"
#include "storage_common.h"
#include "system_parameter.h"
#include "set_object.h"
#include "virtual_object.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "class_object.h"
#include "environment_variable.h"
#include "db.h"
#include "transaction_cl.h"
#include "object_template.h"
#include "server_interface.h"
#include "view_transform.h"
#include "dbtype.h"
#include "execute_statement.h"

extern unsigned int db_on_server;

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

WS_MOP_TABLE_ENTRY *ws_Mop_table = NULL;

/*
 * ws_Mop_table_size
 *    Records the current size of the OID to MOP hash table.
 */

unsigned int ws_Mop_table_size = 0;

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
 */

WS_STATISTICS ws_Stats = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

int ws_Num_dirty_mop = 0;

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


/*
 * Objlist_area
 *    Area for allocating external object list links.
 */
static AREA *Objlist_area = NULL;

/* When MVCC is enabled, fetched objects are not locked. Which means next
 * fetch call would go to server and check if object was changed. However,
 * if the same snapshot is used, the visible object is not changed. To avoid
 * checking on server, mark fetched object with the snapshot version and
 * don't re-fetch until snapshot version is changed.
 */
static unsigned int ws_MVCC_snapshot_version = 0;

/*
 * ws_area_init
 *    Initialize the areas used by the workspace manager.
 *
 */

int ws_Error_ignore_list[-ER_LAST_ERROR];
int ws_Error_ignore_count = 0;

#define OBJLIST_AREA_COUNT 4096

static WS_REPL_FLUSH_ERR *ws_Repl_error_link = NULL;

static WS_REPL_LIST ws_Repl_objs;

static MOP ws_make_mop (const OID * oid);
static void ws_free_mop (MOP op);
static void emergency_remove_dirty (MOP op);
static void ws_unlink_from_commit_mops_list (MOP op);
static int ws_map_dirty_internal (MAPFUNC function, void *args, bool classes_only);
static int add_class_object (MOP class_mop, MOP obj);
static void remove_class_object (MOP class_mop, MOP obj);
static int mark_instance_deleted (MOP op, void *args);
static void ws_clear_internal (bool clear_vmop_keys);
static void ws_print_oid (OID * oid);
#if defined (CUBRID_DEBUG)
static int ws_describe_mop (MOP mop, void *args);
#endif
static int ws_check_hash_link (int slot);
static void ws_insert_mop_on_hash_link (MOP mop, int slot);
static void ws_insert_mop_on_hash_link_with_position (MOP mop, int slot, MOP prev);

#if !defined (NDEBUG)
static void ws_examine_no_mop_has_cached_lock (void);
#endif /* !NDEBUG */

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
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) 0);
	}
    }
  else
    {
      /* might want to keep a chunk of memory in reserve here so we can free it in case we need to do any small
       * allocations during the abort process */

      (void) tran_unilaterally_abort ();

      /* couldn't get to the catalog, use hard coded strings */
      fprintf (stdout, "CUBRID cannot allocate main memory and must halt execution.\n");
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
ws_make_mop (const OID * oid)
{
  MOP op;

  op = (MOP) malloc (sizeof (DB_OBJECT));
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
      op->pruning_type = DB_NOT_PARTITIONED_CLASS;
      op->hash_link = NULL;
      op->commit_link = NULL;
      op->oid_info.oid.volid = 0;
      op->oid_info.oid.pageid = 0;
      op->oid_info.oid.slotid = 0;
      op->is_vid = 0;
      op->is_temp = 0;
      op->released = 0;
      op->decached = 0;
      op->label_value_list = NULL;
      /* Initialize mvcc snapshot version to be sure it doesn't match with current mvcc snapshot version. */
      op->mvcc_snapshot_version = ws_get_mvcc_snapshot_version () - 1;

      op->trigger_involved = 0;

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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (DB_OBJECT));

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
  DB_VALUE *keys = NULL;
  unsigned int flags;

  if (op->commit_link != NULL)
    {
      /* safe-guard to prevent FMR to access ws_Commit_mops list */
      ws_unlink_from_commit_mops_list (op);
    }

  ws_clean_label_value_list (op);

  if (op->is_vid)
    {
      keys = ws_keys (op, &flags);
      if (keys != NULL)
	{
	  pr_clear_value (keys);
	}

      if (WS_VID_INFO (op))
	{
	  free (WS_VID_INFO (op));
	  WS_VID_INFO (op) = NULL;
	}
    }

  free (op);
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

static int
ws_check_hash_link (int slot)
{
  MOP head, tail;
  MOP p, q;
  int c;

  head = ws_Mop_table[slot].head;
  tail = ws_Mop_table[slot].tail;

  p = head;
  if (p == NULL)
    {
      /* empty list */
      assert (head == NULL && tail == NULL);
    }
  else if (p->hash_link == NULL)
    {
      /* only one node */
      assert (head == p && tail == p);
    }
  else
    {
      /* more than one node */
      for (q = p->hash_link; q; p = q, q = q->hash_link)
	{
	  c = oid_compare (WS_OID (p), WS_OID (q));
	  assert (c <= 0);
	}
      assert (p == tail);
    }

  return NO_ERROR;
}

/*
 * ws_insert_mop_on_hash_link () - Insert a new mop in hash table at the given
 *				   slot. The list of mop's is kept ordered
 *				   by OID's.
 *
 * return    : Void.
 * mop (in)  : New mop.
 * slot (in) : Hash slot.
 *
 * NOTE: There are cases when real objects may have duplicate OID's,
 *	 especially when MVCC is enabled. Duplicates cannot be removed
 *	 because they may be still referenced. We can only discard the cached
 *	 object and remove the mop from class.
 */
static void
ws_insert_mop_on_hash_link (MOP mop, int slot)
{
  MOP p;
  MOP prev = NULL;
  int c;

  /* to find the appropriate position */
  p = ws_Mop_table[slot].tail;
  if (p)
    {
      c = oid_compare (WS_OID (mop), WS_OID (p));

      if (c > 0)
	{
	  /* mop is greater than the tail */
	  p->hash_link = mop;
	  mop->hash_link = NULL;
	  ws_Mop_table[slot].tail = mop;

	  return;
	}

      /* Unfortunately, we have to navigate the list when c == 0, because there can be redundancies of mops which have
       * the same oid, in case of VID. Under 'Create table A -> rollback -> Create table B' scenario, the oid of the
       * mop of table B can be same as that of table A. Because the newest one is located at the head of redundancies
       * in that case, we use the first fit method. */
    }

  for (p = ws_Mop_table[slot].head; p != NULL; prev = p, p = p->hash_link)
    {
      c = oid_compare (WS_OID (mop), WS_OID (p));
      if (c > 0)
	{
	  continue;
	}

      if (c == 0)
	{
	  if (WS_ISVID (mop))
	    {
	      break;
	    }

	  /* For real objects, must first discard the duplicate object and remove it from class_mop. */
	  /* TODO: This can happen in non-mvcc now. We can have a duplicate mop in a insert->rollback->insert, where
	   * the duplicate is the first inserted object. That object does not exist anymore and should probably be
	   * marked accordingly. */

	  /* Decache object */
	  ws_decache (p);

	  if (p->class_mop != NULL)
	    {
	      if (p->class_mop->class_link != NULL)
		{
		  remove_class_object (p->class_mop, p);
		}
	      else
		{
		  p->class_mop = NULL;
		  p->class_link = NULL;
		}
	    }

	  break;
	}
      else			/* if (c < 0) */
	{
	  break;
	}
    }

  if (p == NULL)
    {
      /* empty or reach at the tail of the list */
      ws_Mop_table[slot].tail = mop;
    }

  if (prev == NULL)
    {
      mop->hash_link = ws_Mop_table[slot].head;
      ws_Mop_table[slot].head = mop;
    }
  else
    {
      mop->hash_link = prev->hash_link;
      prev->hash_link = mop;
    }
}

/*
 * ws_insert_mop_on_hash_link_with_position () - Insert a mop in hash table
 *						 at the given slot, after prev
 *						 mop.
 *
 * return    : Void.
 * mop (in)  : New mop.
 * slot (in) : Hash slot.
 * prev (in) : Mop in hash list after which the new_mop should be added.
 *
 * NOTE: Real objects should have only one mop instance. This function does
 *	 not check for duplicates. Therefore, make sure to use it only if
 *	 OID conflict is not possible, or if duplicate was removed beforehand.
 */
static void
ws_insert_mop_on_hash_link_with_position (MOP mop, int slot, MOP prev)
{
  if (prev == NULL)
    {
      if (ws_Mop_table[slot].tail == NULL)
	{
	  /* empty list */
	  ws_Mop_table[slot].tail = mop;
	}
      mop->hash_link = ws_Mop_table[slot].head;
      ws_Mop_table[slot].head = mop;
    }
  else
    {
      if (prev->hash_link == NULL)
	{
	  /* append mop on the tail of the list */
	  ws_Mop_table[slot].tail = mop;
	}
      mop->hash_link = prev->hash_link;
      prev->hash_link = mop;
    }
}

/*
 * ws_mop_if_exists () - Get object mop if it exists in mop table.
 *
 * return	  : MOP or NULL if not found.
 * oid (in)	  : Object identifier.
 */
MOP
ws_mop_if_exists (OID * oid)
{
  MOP mop = NULL;
  unsigned int slot;
  int c;

  if (OID_ISNULL (oid))
    {
      return NULL;
    }

  /* look for existing entry */
  slot = OID_PSEUDO_KEY (oid);
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  /* compare with the last mop */
  mop = ws_Mop_table[slot].tail;
  if (mop)
    {
      c = oid_compare (oid, WS_OID (mop));
      if (c > 0)
	{
	  /* 'oid' is greater than the tail, which means 'oid' does not exist in the list NO need to traverse the
	   * list! */
	  return NULL;
	}
      else
	{
	  /* c <= 0 */

	  /* Unfortunately, we have to navigate the list when c == 0 */
	  /* See the comment of ws_insert_mop_on_hash_link() */

	  for (mop = ws_Mop_table[slot].head; mop != NULL; mop = mop->hash_link)
	    {
	      c = oid_compare (oid, WS_OID (mop));
	      if (c <= 0)
		{
		  return (c == 0) ? mop : NULL;
		}
	    }
	}
    }

  return NULL;
}

/*
 * ws_mop - given a oid, find or create the corresponding MOP and add it to
 * the workspace object table.
 *    return: MOP
 *    oid(in): oid
 *    class_mop(in): optional class MOP (can be null if not known)
 *
 * Note: If the class argument is NULL, it will be added to the class list
 * when the object is cached.
 */

MOP
ws_mop (const OID * oid, MOP class_mop)
{
  MOP mop, new_mop, prev;
  unsigned int slot;
  int c;

  if (OID_ISNULL (oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CANT_INSTALL_NULL_OID, 0);
      return NULL;
    }

  /* look for existing entry */
  slot = OID_PSEUDO_KEY (oid);
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  /* compare with the last mop */
  prev = NULL;
  mop = ws_Mop_table[slot].tail;
  if (mop)
    {
      c = oid_compare (oid, WS_OID (mop));
      if (c > 0)
	{
	  /* 'oid' is greater than the tail, which means 'oid' does not exist in the list NO need to traverse the
	   * list! */
	  prev = ws_Mop_table[slot].tail;
	}
      else
	{
	  /* c <= 0 */

	  /* Unfortunately, we have to navigate the list when c == 0 */
	  /* See the comment of ws_insert_mop_on_hash_link() */

	  for (mop = ws_Mop_table[slot].head; mop != NULL; prev = mop, mop = mop->hash_link)
	    {
	      c = oid_compare (oid, WS_OID (mop));
	      if (c > 0)
		{
		  continue;
		}
	      else if (c == 0)
		{
		  if (mop->decached)
		    {
		      /*
		       * If a decached instance object has a class mop,
		       * we need to clear the information related the class mop,
		       * such as class_mop and class_link.
		       * Actually the information should be cleared when the mop
		       * is decached. The current implementation, however, assumes
		       * that decached objects have the information and there are
		       * many codes based on the assumption. So we clear them here,
		       * when reusing decached objects.
		       */
		      if (mop->class_mop != sm_Root_class_mop && class_mop != mop->class_mop)
			{
			  if (mop->class_mop != NULL)
			    {
			      remove_class_object (mop->class_mop, mop);
			    }
			  /* temporary disable assert */
			  /* assert (mop->class_mop == NULL && mop->class_link == NULL); */
			  mop->class_mop = mop->class_link = NULL;
			  if (class_mop != NULL)
			    {
			      add_class_object (class_mop, mop);
			    }
			}
		      mop->decached = 0;
		    }
		  return mop;
		}
	      else		/* if (c < 0) */
		{
		  /* find the node which is greater than I */
		  break;
		}
	    }
	}
    }

  /* make a new mop entry */
  new_mop = ws_make_mop (oid);
  if (new_mop == NULL)
    {
      return NULL;
    }

  if (class_mop != NULL)
    {
      if (add_class_object (class_mop, new_mop))
	{
	  ws_free_mop (new_mop);
	  return NULL;
	}
    }

  /* install it into this slot list */
  ws_insert_mop_on_hash_link_with_position (new_mop, slot, prev);
  assert (ws_check_hash_link (slot) == NO_ERROR);

  return new_mop;
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
ws_vmop (MOP class_mop, int flags, DB_VALUE * keys)
{
  MOP mop, new_mop;
  int slot, is_vclass = 0;
  VID_INFO *vid_info;
  DB_TYPE keytype;

  vid_info = NULL;
  keytype = DB_VALUE_DOMAIN_TYPE (keys);

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
      is_vclass = db_is_vclass (class_mop);
      if (is_vclass < 0)
	{
	  return NULL;
	}
      if (!is_vclass)
	{
	  return mop;
	}
      mop = db_real_instance (mop);
      if (mop == NULL)
	{
	  return NULL;
	}

      /* In this case, we need to set the class_mop. Or we may fail to get mop->object when fetch dirty version. See
       * xlocator_fetch, locator_cache and locator_cache_have_object */
      if (mop->class_mop == NULL)
	{
	  mop->class_mop = mq_fetch_one_real_class (class_mop);
	}

      db_make_object (keys, mop);
      break;
    case DB_TYPE_OID:
      /*
       * a non-virtual object mop
       * This will occur when reading the oid keys field of a virtual object
       * if it was read through some interface that does NOT swizzle.
       * oid's to objects.
       */
      mop = ws_mop (&keys->data.oid, class_mop);
      is_vclass = db_is_vclass (class_mop);
      if (is_vclass < 0)
	{
	  return NULL;
	}
      if (!is_vclass)
	{
	  return mop;
	}
      db_make_object (keys, mop);
      break;
    default:
      /* otherwise fall through to generic keys case */
      break;
    }

  slot = mht_valhash (keys, ws_Mop_table_size);
  if (!(flags & VID_NEW))
    {
      for (mop = ws_Mop_table[slot].head; mop != NULL; mop = mop->hash_link)
	{
	  if (mop->is_vid)
	    {
	      vid_info = WS_VID_INFO (mop);
	      if (class_mop == mop->class_mop)
		{
		  /*
		   * NOTE, formerly called pr_value_equal. Don't coerce
		   * with the new tp_value_equal function but that may
		   * actually be desired here.
		   */
		  if (tp_value_equal (keys, &vid_info->keys, 0))
		    {
		      return mop;
		    }
		}
	    }
	}
    }

  new_mop = ws_make_mop (NULL);
  if (new_mop == NULL)
    {
      return NULL;
    }

  new_mop->is_vid = 1;

  vid_info = WS_VID_INFO (new_mop) = (VID_INFO *) malloc (sizeof (VID_INFO));
  if (vid_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (VID_INFO));
      goto abort_it;
    }

  vid_info->flags = flags;
  db_make_null (&vid_info->keys);

  if (pr_clone_value (keys, &vid_info->keys))
    {
      goto abort_it;
    }

  if (add_class_object (class_mop, new_mop))
    {
      goto abort_it;
    }

  /* install it into this slot list */
  ws_insert_mop_on_hash_link (new_mop, slot);
  assert (ws_check_hash_link (slot) == NO_ERROR);

  return new_mop;

abort_it:
  if (new_mop != NULL)
    {
      ws_free_mop (new_mop);
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
      inst = vid_fetch_instance (mop, DB_FETCH_READ, TM_TRAN_READ_FETCH_VERSION ());
    }
  if (!inst)
    {
      return false;
    }

  vid_info = WS_VID_INFO (mop);
  keys = &vid_info->keys;

  slot = mht_valhash (keys, ws_Mop_table_size);

  for (found = ws_Mop_table[slot].head, prev = NULL; found != mop && found != NULL; found = found->hash_link)
    {
      prev = found;
    }

  if (found != mop)
    {
      return false;
    }

  /* get new relational key */
  no_keys = 0;
  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
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
      for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
	{
	  if ((att->flags & SM_ATTFLAG_VID) && (classobj_get_prop (att->properties, SM_PROPERTY_VID_KEY, &val)))
	    {
	      att_seq_val = db_get_int (&val);
	      if (att_seq_val == key_index)
		{
		  /* Sets won't work as key components */
		  mem = inst + att->offset;
		  db_value_domain_init (&val, att->type->id, att->domain->precision, att->domain->scale);
		  att->type->getmem (mem, att->domain, &val);
		  if ((DB_VALUE_TYPE (value) == DB_TYPE_STRING) && (db_get_string (value) == NULL))
		    {
		      db_make_null (value);
		    }

		  if (no_keys > 1)
		    {
		      if (set_put_element (db_get_set (&new_key), key_index, &val) < 0)
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

  pr_clear_value (keys);
  pr_clone_value (&new_key, keys);
  pr_clear_value (&new_key);

  /* remove it from the original list */
  if (prev == NULL)
    {
      ws_Mop_table[slot].head = mop->hash_link;
    }
  else
    {
      prev->hash_link = mop->hash_link;
    }

  if (ws_Mop_table[slot].tail == mop)
    {
      /* I was the tail of the list */
      ws_Mop_table[slot].tail = prev;
    }

  assert (ws_check_hash_link (slot) == NO_ERROR);

  mop->hash_link = NULL;

  /* move to the new list */
  slot = mht_valhash (keys, ws_Mop_table_size);
  ws_insert_mop_on_hash_link (mop, slot);
  assert (ws_check_hash_link (slot) == NO_ERROR);

  return true;
}

/*
 * ws_new_mop - optimized version of ws_mop when OID being entered into the
 * workspace is guaranteed to be unique.
 *    return: new MOP
 *    oid(in): object OID
 *    class_mop(in): class mop of object
 *
 * Note:
 *    This happens when temporary OIDs are generated for newly created objects.
 *    It assumes that the MOP must be created and does not bother searching
 *    the hash table collision list looking for duplicates.
 */
MOP
ws_new_mop (OID * oid, MOP class_mop)
{
  MOP mop;
  unsigned int slot;

  mop = NULL;
  if (OID_ISNULL (oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CANT_INSTALL_NULL_OID, 0);
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

  if (class_mop != NULL)
    {
      if (add_class_object (class_mop, mop))
	{
	  ws_free_mop (mop);
	  return NULL;
	}
    }

  ws_insert_mop_on_hash_link (mop, slot);
  assert (ws_check_hash_link (slot) == NO_ERROR);

  return (mop);
}

/*
 * ws_update_oid_and_class - change the OID of a MOP and recache the class mop
 *			   if it has been changed
 *    return: void
 *    mop(in/out)   : MOP whose OID needs to be changed
 *    newoid(in)    : new OID
 *    new_class_oid : new class OID
 *
 * Note:
 *    This is called in three cases:
 *    1. Newly created objects are flushed and are given a permanent OID.
 *    2. An object changes partition after update.
 *
 *    If the object belongs to a partitioned class, it will
 *    have a different class oid here (i.e. the partition in
 *    which it was placed). We have to fetch the partition mop
 *    and recache it here.
 */
int
ws_update_oid_and_class (MOP mop, OID * new_oid, OID * new_class_oid)
{
  MOP class_mop = NULL;
  bool relink = false;
  class_mop = ws_class_mop (mop);

  if (class_mop == NULL || !OID_EQ (WS_OID (class_mop), new_class_oid))
    {
      /* we also need to disconnect this instance from class_mop and add it to new_class_oid */
      if (class_mop != NULL)
	{
	  remove_class_object (class_mop, mop);
	}
      relink = true;
      class_mop = ws_mop (new_class_oid, NULL);
      if (class_mop == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}
    }

  /* Make sure that we have the new class in workspace */
  if (class_mop->object == NULL)
    {
      int error = NO_ERROR;
      SM_CLASS *smclass = NULL;
      /* No need to check authorization here */
      error = au_fetch_class_force (class_mop, &smclass, AU_FETCH_READ);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  mop->class_mop = class_mop;
  ws_update_oid (mop, new_oid);
  if (relink)
    {
      add_class_object (class_mop, mop);
    }
  return NO_ERROR;
}

/*
 * ws_update_oid - change the OID of a MOP. Also update position in
 *		   hash table.
 *
 *    return: void
 *    mop(in/out): MOP whose OID needs to be changed
 *    newoid(in): new OID
 *
 * Note:
 *    This is called in three cases:
 *    1. Newly created objects are flushed and are given a permanent OID.
 *    2. An object changes partition after update.
 *    3. MVCC is enabled and object changes OID after update.
 */
void
ws_update_oid (MOP mop, OID * newoid)
{
  MOP mops, prev;
  unsigned int slot;

  /* find current entry */
  slot = OID_PSEUDO_KEY (WS_OID (mop));
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  mops = ws_Mop_table[slot].head;
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
      ws_Mop_table[slot].head = mop->hash_link;
    }
  else
    {
      prev->hash_link = mop->hash_link;
    }

  if (ws_Mop_table[slot].tail == mop)
    {
      /* I was the tail of the list */
      ws_Mop_table[slot].tail = prev;
    }

  assert (ws_check_hash_link (slot) == NO_ERROR);

  mop->hash_link = NULL;

  /* assign the new oid */
  COPY_OID (WS_REAL_OID (mop), newoid);

  /* force the MOP into the table at the new slot position */
  slot = OID_PSEUDO_KEY (newoid);
  if (slot >= ws_Mop_table_size)
    {
      slot = slot % ws_Mop_table_size;
    }

  ws_insert_mop_on_hash_link (mop, slot);
  assert (ws_check_hash_link (slot) == NO_ERROR);
}

/*
 * INTERNAL GARBAGE COLLECTOR
 */

/*
 * ws_disconnect_deleted_instances - called when a class MOP is being garbage
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
void
ws_disconnect_deleted_instances (MOP classop)
{
  MOP m, next;

  if (classop == sm_Root_class_mop)
    {
      return;
    }

  for (m = classop->class_link, next = NULL; m != Null_object && m != NULL; m = next)
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
      ws_disconnect_deleted_instances (classop);
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
  if (op->dirty_link != NULL && op->class_mop != NULL && op->class_mop->dirty_link != NULL)
    {
      /* search for op in op's class' dirty list */
      prev = NULL;

      for (mop = op->class_mop->dirty_link; mop != Null_object && mop != op; mop = mop->dirty_link)
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
 * ws_unlink_from_commit_mops_list -
 *    return: void
 *    op(in): mop that needs to be unlinked from ws_Commit_mops list
 *
 */
static void
ws_unlink_from_commit_mops_list (MOP op)
{
  MOP mop, prev, next, to_be_next;

  assert (op != NULL);

  prev = NULL;
  mop = ws_Commit_mops;
  while (mop)
    {
      next = mop->commit_link;
      if (next == mop)
	{
	  /* the last node */
	  to_be_next = prev;
	}
      else
	{
	  to_be_next = next;
	}

      if (mop == op)
	{
	  if (prev == NULL)
	    {
	      assert (ws_Commit_mops == mop);

	      ws_Commit_mops = to_be_next;
	    }
	  else
	    {
	      prev->commit_link = to_be_next;
	    }

	  op->commit_link = NULL;
	  return;
	}

      prev = mop;
      if (next == mop)
	{
	  /* the last node */
	  mop = NULL;
	}
      else
	{
	  mop = next;
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
void
ws_cull_mops (void)
{
  MOP mops, prev, next;
  DB_OBJLIST *m;
  unsigned int slot, count;

  for (m = ws_Resident_classes; m != NULL; m = m->next)
    {
      prev = NULL;
      for (mops = m->op->class_link; mops != NULL && mops != Null_object; mops = next)
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
      for (mops = ws_Mop_table[slot].head; mops != NULL; mops = next)
	{
	  next = mops->hash_link;
	  if (!mops->released)
	    {
	      prev = mops;
	    }
	  else
	    {
	      if (mops == sm_Root_class_mop)
		{
		  /* can't have rootclass on the garbage list */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CORRUPTED, 0);
		  ws_Stats.corruptions++;
		  /* probably fatal to continue here */
		}

	      if (mops->class_mop == sm_Root_class_mop && mops->dirty)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_GC_DIRTY_MOP, 0);
		  ws_Stats.dirty_list_emergencies++;
		  emergency_remove_dirty (mops);
		}
	      if (mops->class_mop != sm_Root_class_mop && mops->dirty_link)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_GC_DIRTY_MOP, 0);
		  ws_Stats.dirty_list_emergencies++;
		  emergency_remove_dirty (mops);
		}

	      /* remove the mop from the hash table */
	      if (prev == NULL)
		{
		  ws_Mop_table[slot].head = next;
		}
	      else
		{
		  prev->hash_link = next;
		}

	      if (ws_Mop_table[slot].tail == mops)
		{
		  /* I was the tail of the list */
		  ws_Mop_table[slot].tail = prev;
		}

	      assert (ws_check_hash_link (slot) == NO_ERROR);

	      /* free the associated object, note for classes, this could be a fairly complex operation since all the
	       * instances are decached as well */
	      ws_decache (mops);

	      /* if this is a vmop, we need to clear the keys */
	      if (mops->is_vid && WS_VID_INFO (mops))
		{
		  pr_clear_value (&WS_VID_INFO (mops)->keys);
		}

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

	      ws_unlink_from_commit_mops_list (mops);

	      /* return mop to the garbage collector */
	      ws_free_mop (mops);

	      count++;
	    }
	}
    }

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
    {
      return;
    }

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
 * ws_release_user_instance - set the released field only for a user mop
 *    return: void
 *    mop(out): mop to set released field
 */
void
ws_release_user_instance (MOP mop)
{
  /* to keep instances of system classes, for instance, db_serial's. This prevents from dangling references to serial
   * objects during replication. The typical scenario is to update serials, cull mops which clears the mop up, and then
   * truncate the table which leads updating the serial mop to reset its values. */
  if (db_is_system_class (mop->class_mop) > 0)
    {
      return;
    }

  ws_release_instance (mop);
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

  if (prm_get_integer_value (PRM_ID_SUPPLEMENTAL_LOG))
    {
      WS_SET_TRIGGER_INVOLVED (op);
    }

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
      /* in case of SERIALIZABLE conflict the object will be decached later */
      if (er_errid () != ER_MVCC_SERIALIZABLE_CONFLICT)
	{
	  /* SERIOUS INTERNAL ERROR */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CLASS_NOT_CACHED, 0);
	}

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
  for (m = ws_Resident_classes; m != NULL && status == WS_MAP_CONTINUE && (class_mop = m->op) != NULL; m = m->next)
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
      for (op = class_mop->dirty_link; op != Null_object && op->dirty == 0; op = next)
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
      /* must make sure this gets initialized, should have been done already when the class was cached in the clause
       * above */
      if (class_mop->class_link == NULL)
	{
	  class_mop->class_link = Null_object;
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
	      error = ER_WS_CHANGING_OBJECT_CLASS;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      ws_Stats.ignored_class_assignments++;
	    }
	  else
	    {
	      obj->class_mop = class_mop;
	      if (obj->class_link == NULL)
		{
		  obj->class_link = class_mop->class_link;
		  class_mop->class_link = obj;
		}
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

  for (o = class_mop->class_link, prev = NULL; o != Null_object && o != obj; o = o->class_link)
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
      for (l = ws_Resident_classes; l != NULL && status == WS_MAP_CONTINUE; l = l->next)
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
      for (op = class_op->dirty_link, next = Null_object; op != Null_object && op->dirty == 0; op = next)
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
  MOP op = NULL, save_class_link = NULL;
  DB_OBJLIST *l = NULL;
  int status = WS_MAP_CONTINUE;

  if (class_op == sm_Root_class_mop)
    {
      /* rootclass, must map through resident class list */
      for (l = ws_Resident_classes; l != NULL && status == WS_MAP_CONTINUE; l = l->next)
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
	  for (op = class_op->class_link; op != Null_object && status == WS_MAP_CONTINUE; op = save_class_link)
	    {
	      save_class_link = op->class_link;
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

  if (classobj == NULL || classmop == NULL)
    {
      return;
    }

  current = (MOP) mht_get (Classname_cache, sm_ch_name (classobj));

  if (current == NULL)
    {
      mht_put (Classname_cache, cl_name, classmop);
    }
  else
    {
      if (current != classmop)
	{
	  mht_rem (Classname_cache, sm_ch_name (classobj), NULL, NULL);
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
  const char *class_name = NULL;

  if (classobj == NULL)
    {
      return;
    }

  class_name = sm_ch_name (classobj);
  if (class_name == NULL)
    {
      return;			// ignore
    }

  mht_rem (Classname_cache, class_name, NULL, NULL);
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
 * ws_init - initialize workspace
 *    return: NO_ERROR if successful, error code otherwise
 *
 * Note: This function should be called once early in the database
 *    initialization phase.
 */
int
ws_init (void)
{
  int error_code = NO_ERROR;
  unsigned int i;
  size_t allocsize;

  /* ws_init() shouldn't invoked twice without ws_final() */
  if (ws_Mop_table != NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return ER_FAILED;
    }

  if (db_create_workspace_heap () == 0)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /*
   * area_init() must have been called earlier.
   * These need to all be returning errors !
   */
  error_code = ws_area_init ();	/* object lists */
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = pr_area_init ();	/* DB_VALUE */
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = set_area_init ();	/* set reference */
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = obt_area_init ();	/* object templates, assignment templates */
  if (error_code != NO_ERROR)
    {
      goto error;
    }
  error_code = classobj_area_init ();	/* schema templates */
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* build the MOP table */
  ws_Mop_table_size = prm_get_integer_value (PRM_ID_WS_HASHTABLE_SIZE);
  allocsize = sizeof (WS_MOP_TABLE_ENTRY) * ws_Mop_table_size;
  ws_Mop_table = (WS_MOP_TABLE_ENTRY *) malloc (allocsize);

  if (ws_Mop_table == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, allocsize);
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  for (i = 0; i < ws_Mop_table_size; i++)
    {
      ws_Mop_table[i].head = NULL;
      ws_Mop_table[i].tail = NULL;
    }

  /* create the internal Null object mop */
  Null_object = ws_make_mop (NULL);
  if (Null_object == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error_code = er_errid ();
      goto error;
    }

  /* start with nothing dirty */
  Ws_dirty = false;

  /* build the classname cache */
  Classname_cache = mht_create ("Workspace class name cache", 256, mht_1strhash, mht_compare_strings_are_equal);

  if (Classname_cache == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      goto error;
    }

  /* Can't have any resident classes yet */
  ws_Resident_classes = NULL;

  return NO_ERROR;

error:
  db_destroy_workspace_heap ();

  ws_area_final ();
  pr_area_final ();
  set_area_final ();
  obt_area_final ();
  classobj_area_final ();

  if (ws_Mop_table != NULL)
    {
      free (ws_Mop_table);
      ws_Mop_table = NULL;
    }

  if (Null_object != NULL)
    {
      ws_free_mop (Null_object);
      Null_object = NULL;
    }

  if (Classname_cache != NULL)
    {
      mht_destroy (Classname_cache);
      Classname_cache = NULL;
    }

  return error_code;
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

  tr_final ();

  if (prm_get_bool_value (PRM_ID_WS_MEMORY_REPORT))
    {
      /* this is for debugging only */
      fprintf (stdout, "*** Database client statistics before shutdown ***\n");
      ws_dump (stdout);
      /*
       * Check for dangling allocations in the workspace.
       * First decache everything, must do this before the
       * MOP tables are destroyed.
       */
      ws_clear_internal (true);
    }
  ws_clear_all_repl_errors_of_error_link ();
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
	  for (mop = ws_Mop_table[slot].head, next = NULL; mop != NULL; mop = next)
	    {
	      next = mop->hash_link;
	      ws_free_mop (mop);
	    }
	}
      ws_free_mop (Null_object);
      free_and_init (ws_Mop_table);
    }

  db_destroy_workspace_heap ();

  /* clean up misc globals */
  ws_Mop_table = NULL;
  ws_Mop_table_size = 0;
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
      for (mop = ws_Mop_table[slot].head; mop != NULL; mop = mop->hash_link)
	{
	  ws_decache (mop);

	  /* if this is a vmop, we may need to clear the keys */
	  if (mop->is_vid && WS_VID_INFO (mop) && clear_vmop_keys)
	    {
	      pr_clear_value (&WS_VID_INFO (mop)->keys);
	    }

	  mop->lock = NULL_LOCK;
	  mop->deleted = 0;
	  mop->commit_link = NULL;
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
 * MOP CACHING AND DECACHING
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
  /* third clause applies if the sm_Root_class_mop is still being initialized */
  if ((class_mop == sm_Root_class_mop) || (mop->class_mop == sm_Root_class_mop) || (mop == class_mop))
    {

      /* caching a class */
      if ((mop->object != NULL) && (mop->object != (MOBJ) (&sm_Root_class)))
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

      if (obj != (MOBJ) (&sm_Root_class))
	{
	  /* this initializes the class_link list and adds it to the list of resident classes */
	  if (add_class_object (class_mop, mop))
	    {
	      goto abort_it;
	    }

	  /* add to the classname cache */
	  ws_add_classname (obj, mop, sm_ch_name (obj));
	}

      if (prm_get_bool_value (PRM_ID_CLIENT_CLASS_CACHE_DEBUG))
	{
	  er_print_callstack (ARG_FILE_LINE, "Cache class %s mop %d|%d|%d.\n", sm_ch_name ((MOBJ) mop->object),
			      ws_oid (mop)->volid, ws_oid (mop)->pageid, ws_oid (mop)->slotid);
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CLASS_NOT_CACHED, 0);
	      ws_Stats.uncached_classes++;
	      goto abort_it;
	    }
	  else
	    {
	      obj_free_memory ((SM_CLASS *) mop->class_mop->object, (MOBJ) mop->object);
	      mop->object = NULL;
	    }
	}

      mop->object = obj;
      ws_class_has_object_dependencies (class_mop);

      if (mop->class_mop != class_mop)
	{
	  if (mop->class_mop != NULL)
	    {
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_WS_CHANGING_OBJECT_CLASS, 0);
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

  return;

abort_it:
  /*
   * NULL the MOP since we're in an unknown state, this function
   * should be returning an error
   */
  mop->object = NULL;
}

/*
 * ws_cache_with_oid - first find or create a MOP for the object's OID and
 * then cache the object.
 *    return: mop of cached object
 *    obj(in): memory representation of object
 *    oid(in): object identifier
 *    class(in): class of object
 *
 */
MOP
ws_cache_with_oid (MOBJ obj, OID * oid, MOP class_mop)
{
  MOP mop;

  mop = ws_mop (oid, class_mop);
  if (mop != NULL)
    {
      ws_cache (obj, mop, class_mop);
    }

  return mop;
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_WS_CLASS_NOT_CACHED, 0);
	      ws_Stats.uncached_classes++;
	      mop->object = NULL;
	    }
	  else
	    {
	      obj_free_memory ((SM_CLASS *) mop->class_mop->object, (MOBJ) mop->object);
	      if (WS_IS_DELETED (mop))
		{
		  remove_class_object (mop->class_mop, mop);
		}
	      mop->object = NULL;
	    }
	}
    }
  else
    {
      if (prm_get_bool_value (PRM_ID_CLIENT_CLASS_CACHE_DEBUG))
	{
	  er_print_callstack (ARG_FILE_LINE, "Decache class %s " "mop %d|%d|%d.\n", sm_ch_name ((MOBJ) mop->object),
			      ws_oid (mop)->volid, ws_oid (mop)->pageid, ws_oid (mop)->slotid);
	}
      /* free class object, not sure if this should be done here */
      if (mop->object != NULL && mop->object != (MOBJ) (&sm_Root_class))
	{
	  ws_drop_classname ((MOBJ) mop->object);

	  ws_decache_all_instances (mop);
	  classobj_free_class ((SM_CLASS *) mop->object);
	}
    }

  mop->object = NULL;
  ws_clean (mop);

  /* this no longer apples */
  mop->composition_fetch = 0;
  mop->decached = 1;
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
  MOP obj = NULL, save_class_link = NULL;

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
	  for (obj = mop->class_link; obj != Null_object; obj = save_class_link)
	    {
	      save_class_link = obj->class_link;
	      ws_decache (obj);
	    }
	}
    }
}

/*
 *  MOP ACCESSOR FUNCTIONS
 */

/*
 * ws_identifier() - This function returns the permanent object identifier of
 *                   the given object.
 * return : Pointer to object identifier
 * mop(in):
 * Note: This function should not be used if the object can be a
 *       non-referable instance as it will return a reference to the object;
 *       use db_identifier () instead to perform the needed check.
 */
OID *
ws_identifier (MOP mop)
{
  return ws_identifier_with_check (mop, false);
}

/*
 * ws_identifier_with_check() - This function returns the permanent object
 *                              identifier of the given object.
 * return : Pointer to object identifier
 * mop(in):
 * check_non_referable(in): whether to check that a reference to the instance
 *                          can be returned. Instances of reusable OID classes
 *                          are non-referable.
 */
OID *
ws_identifier_with_check (MOP mop, const bool check_non_referable)
{
  OID *oid = NULL;
  MOP class_mop;

  if (mop == NULL || WS_IS_DELETED (mop))
    {
      goto end;
    }

  if (WS_ISVID (mop))
    {
      mop = db_real_instance (mop);
      if (mop == NULL)
	{
	  /* non-updatable view has no oid */
	  goto end;
	}
      if (WS_ISVID (mop))
	{
	  /* a proxy has no oid */
	  goto end;
	}
    }

  if (check_non_referable)
    {
      int is_class = locator_is_class (mop, DB_FETCH_READ);

      if (is_class < 0)
	{
	  goto end;
	}
      else if (is_class == 0)
	{
	  class_mop = ws_class_mop (mop);
	  if (sm_is_reuse_oid_class (class_mop))
	    {
	      /* should not return the oid of a non-referable instance */
	      goto end;
	    }
	}
    }

  oid = ws_oid (mop);
  if (OID_ISTEMP (oid))
    {
      (void) locator_flush_instance (mop);
      oid = ws_oid (mop);
    }

end:
  return oid;
}

/*
 * These provide access shells for the fields in the MOP structure.  These
 * are simple enough that callers should change to use the corresponding
 * macros.
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
  if (prm_get_bool_value (PRM_ID_CLIENT_CLASS_CACHE_DEBUG) && mop->class_mop == sm_Root_class_mop
      && mop != sm_Root_class_mop && mop->object != NULL && lock != mop->lock)
    {
      er_print_callstack (ARG_FILE_LINE, "Change class %s mop %d|%d|%d " "lock from %d to %d.\n",
			  sm_ch_name ((MOBJ) mop->object), ws_oid (mop)->volid, ws_oid (mop)->pageid,
			  ws_oid (mop)->slotid, mop->lock, lock);
    }
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
 *    the mop before it is cached.
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
 * ws_restore_pin - resotre pin flag of a object and its class object
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
 *    obj(out): return pointer to memory representation of object
 *
 * Note:
 *    This is used to access the memory representation of an object.
 *    The memory representation is returned through the supplied pointer
 *    as long as the mop is not marked as deleted.
 */
int
ws_find (MOP mop, MOBJ * obj)
{
  int status;

  *obj = NULL;
  if (mop && !WS_IS_DELETED (mop))
    {
      *obj = (MOBJ) mop->object;

      status = WS_FIND_MOP_NOTDELETED;
    }
  else
    {
      status = WS_FIND_MOP_DELETED;
    }

  return status;
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
ws_class_has_object_dependencies (MOP class_mop)
{
  if (class_mop != NULL)
    {
      class_mop->no_objects = 0;
    }
}

/*
 * ws_class_has_cached_objects - check if there are cached redident instance
 *    return: non zero if there are cached redident instances
 *    class(in): class to examin
 */
int
ws_class_has_cached_objects (MOP class_mop)
{
  MOP obj;
  int cached = 0;

  for (obj = class_mop->class_link; obj != Null_object && !cached; obj = obj->class_link)
    {
      if (obj->object != NULL)
	{
	  cached = 1;
	}
    }
  return cached;
}

#if defined (CUBRID_DEBUG)
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
  int num_ws_continue_on_error = 0;

  if (ws_Mop_table != NULL)
    {
      for (slot = 0; slot < ws_Mop_table_size && status == WS_MAP_CONTINUE; slot++)
	{
	  for (mop = ws_Mop_table[slot].head; mop != NULL && status == WS_MAP_CONTINUE; mop = mop->hash_link)
	    {
	      status = (*(function)) (mop, args);
	      if (status == WS_MAP_CONTINUE_ON_ERROR)
		{
		  num_ws_continue_on_error++;
		  stauts = WS_MAP_CONTINUE;
		}
	    }
	}
    }
  if (status != WS_MAP_FAIL)
    {
      status = WS_MAP_SUCCESS;
    }

  return (status);
}
#endif

/*
 * TRANSACTION MANAGEMENT SUPPORT
 */

/*
 * ws_clear_hints - clear all of the hint bits in the MOP.
 *    return: void
 *    mop(in): object pointer
 *    leave_pinned(in): flag to keep from modifying pinned field
 *
 * Note:
 *    This is called by the transaction manager to clear all of the hint
 *    bits in the MOP.  This is guaranteed to be called at the end of a
 *    transaction commit.  Note that we always clear the no_objects field
 *    for classes because once they are commited to a database, we must
 *    assume that other users have access to the current representation and
 *    can create instances with that representation.
 */
void
ws_clear_hints (MOP mop, bool leave_pinned)
{
  /*
   * Don't decache non-updatable view objects because they cannot be
   * recreated.  Let garbage collection eventually decache them.
   */
  if (prm_get_bool_value (PRM_ID_CLIENT_CLASS_CACHE_DEBUG) && mop->class_mop == sm_Root_class_mop
      && mop != sm_Root_class_mop && mop->object != NULL)
    {
      er_print_callstack (ARG_FILE_LINE, "Clear class %s mop %d|%d|%d.\n", sm_ch_name ((MOBJ) mop->object),
			  ws_oid (mop)->volid, ws_oid (mop)->pageid, ws_oid (mop)->slotid);
    }

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

  au_reset_authorization_caches ();

  /* clear hints */
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

#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_EXAMINE_CLIENT_CACHED_LOCKS) == true)
    {
      ws_examine_no_mop_has_cached_lock ();
    }
#endif /* NDEBUG */
}

/*
 * ws_abort_mops - called by the transaction manager when a transaction is
 * aborted
 *    return: void
 *    only_unpinned(in): flag whether is it safe to abort pinned mops
 *
 */
void
ws_abort_mops (bool only_unpinned)
{
  MOP mop;
  MOP next;

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
      if (!only_unpinned || !mop->pinned)
	{
	  /* always remove this so we can decache things without error */
	  mop->pinned = 0;

	  /*
	   * Decache all objects in commit link. Even though they are not
	   * marked as dirty and do not have exclusive locks, they may have
	   * been decached (and reset) during a partial rollback. Now we are
	   * not allowed to miss any cached objects that may have been
	   * modified during last transaction.
	   */
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

  ws_Commit_mops = NULL;
  ws_Num_dirty_mop = 0;

#if !defined (NDEBUG)
  if (prm_get_bool_value (PRM_ID_EXAMINE_CLIENT_CACHED_LOCKS) == true)
    {
      ws_examine_no_mop_has_cached_lock ();
    }
#endif /* NDEBUG */
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
  MOP mop = NULL, save_hash_link = NULL;
  unsigned int slot;

  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot].head; mop != NULL; mop = save_hash_link)
	{
	  save_hash_link = mop->hash_link;
	  if (mop->pinned == 0 && (IS_WRITE_EXCLUSIVE_LOCK (ws_get_lock (mop)) || WS_ISDIRTY (mop))
	      && (mop->class_mop != sm_Root_class_mop || mop->object == NULL
		  || ((SM_CLASS *) mop->object)->class_type == SM_CLASS_CT))
	    {
	      ws_decache (mop);
	      ws_clear_hints (mop, false);
	    }
	}
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
  fprintf (stdout, "%d/%d/%d", (int) oid->volid, (int) oid->pageid, (int) oid->slotid);
}

#if defined (CUBRID_DEBUG)
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
		  fprintf (stdout, "%s ", sm_get_ch_name (mop));
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
		  fprintf (stdout, "%s ", sm_get_ch_name (mop->class_mop));
		}
	    }
	}
    }
  if (mop->dirty)
    {
      fprintf (stdout, " dirty");
    }
  if (WS_IS_DELETED (mop))
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
#endif

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
  int mops, root, unknown, classes, cached_classes, instances, cached_instances;
  int count, actual, decached, weird;
  unsigned int slot;
  int classtotal, insttotal, size, isize, icount, deleted;
  MOP mop, inst;
  DB_OBJLIST *m;

  /* get mop totals */
  mops = root = unknown = classes = cached_classes = instances = cached_instances = 0;
  weird = 0;
  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot].head; mop != NULL; mop = mop->hash_link)
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

  fprintf (fpp, "%d mops in the workspace (including one rootclass mop)\n", mops);
  fprintf (fpp, "%d class mops (%d cached, %d uncached)\n", classes, cached_classes, classes - cached_classes);
  fprintf (fpp, "%d instance mops (%d cached, %d uncached)\n", instances, cached_instances,
	   instances - cached_instances);

  fprintf (fpp, "%d unknown mops\n", unknown);
  if (weird)
    {
      fprintf (fpp, "*** %d unknown mops with cached objects\n", weird);
    }
  fprintf (fpp, "%d attempts to clean pinned mops\n", ws_Stats.pinned_cleanings);

  /* gc stats */
  fprintf (fpp, "%d MOPs allocated, %d freed\n", ws_Stats.mops_allocated, ws_Stats.mops_freed);

  /* misc stats */
  fprintf (fpp, "%d dirty list emergencies, %d uncached classes, %d corruptions\n", ws_Stats.dirty_list_emergencies,
	   ws_Stats.uncached_classes, ws_Stats.corruptions);
  fprintf (fpp, "%d ignored class assignments\n", ws_Stats.ignored_class_assignments);


  fprintf (fpp, "%d total set mops allocated, %d total set mops freed\n", ws_Stats.set_mops_allocated,
	   ws_Stats.set_mops_freed);

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
  fprintf (fpp, "%d dirty objects, %d clean objects in dirty list\n", actual, count - actual);

  /* get class totals */
  fprintf (fpp, "RESIDENT INSTANCE TOTALS: \n");
  count = classtotal = insttotal = deleted = 0;
  for (m = ws_Resident_classes; m != NULL; m = m->next)
    {
      mop = m->op;
      if (WS_IS_DELETED (mop))
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
	      for (inst = mop->class_link; inst != Null_object; inst = inst->class_link)
		{
		  icount++;
		  if (inst->object != NULL)
		    {
		      isize += sm_object_size_quick ((SM_CLASS *) mop->object, (MOBJ) inst->object);
		    }
		  else
		    {
		      decached++;
		    }
		}
	      fprintf (fpp, "  %-20s : %d instances, %d decached, %d bytes used\n", sm_ch_name ((MOBJ) (mop->object)),
		       icount, decached, isize);
	      insttotal += isize;
	    }
	}
    }
  if (deleted)
    {
      fprintf (fpp, "*** %d deleted MOPs in the resident class list \n", deleted);
    }

  /* just to make sure */
  if (count != cached_classes)
    {
      fprintf (fpp, "*** Mops claiming to be classes %d, resident class list length %d\n", cached_classes, count);
    }

  fprintf (fpp, "Total bytes for class storage     %d\n", classtotal);
  fprintf (fpp, "Total bytes for instance storage  %d\n", insttotal);
  fprintf (fpp, "Total bytes for object storage    %d\n", classtotal + insttotal);

  fprintf (fpp, "WORKSPACE AREAS:\n");
  area_dump (fpp);
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
  *isvirt = (op && !WS_IS_DELETED (op) && op->object && (((SM_CLASS *) (op->object))->class_type == SM_VCLASS_CT));

  return (op && !WS_IS_DELETED (op) && op->object && op->dirty_link && op->dirty_link != Null_object);
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

/*
 * ws_area_init - initialize area for object list links.
 *    return: NO_ERROR or error code.
 */
int
ws_area_init (void)
{
  Objlist_area = area_create ("Object list links", sizeof (DB_OBJLIST), OBJLIST_AREA_COUNT);
  if (Objlist_area == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * ws_area_final - finalize area for object list links.
 *    return: none.
 */
void
ws_area_final (void)
{
  if (Objlist_area != NULL)
    {
      area_destroy (Objlist_area);
      Objlist_area = NULL;
    }
}

/*
 * LIST UTILITIES
 */
/*
 * These operations assume a structure with a single link field at the top.
 *
 * struct link {
 *   struct link *next;
 * };
 *
 */


/*
 * ws_list_append - append element to the end of a list
 *    return: none
 *    root(in/out): pointer to pointer to list head
 *    element(in): element to add
 */
void
ws_list_append (DB_LIST ** root, DB_LIST * element)
{
  DB_LIST *el;

  for (el = *root; (el != NULL) && (el->next != NULL); el = el->next);
  if (el == NULL)
    {
      *root = element;
    }
  else
    {
      el->next = element;
    }
}

/*
 * ws_list_remove - Removes an element from a list if it exists.
 *    return: non-zero if the element was removed
 *    root(): pointer to pointer to list head
 *    element(): element to remove
 */
int
ws_list_remove (DB_LIST ** root, DB_LIST * element)
{
  DB_LIST *el, *prev;
  int removed;

  removed = 0;
  for (el = *root, prev = NULL; el != NULL && el != element; el = el->next)
    {
      prev = el;
    }

  if (el != element)
    {
      return removed;
    }
  if (prev == NULL)
    {
      *root = element->next;
    }
  else
    {
      prev->next = element->next;
    }
  removed = 1;

  return (removed);
}

/*
 * ws_list_length - return the number of elements in a list
 *    return: length of the list (zero if empty)
 *    list(in): list to examine
 */
int
ws_list_length (DB_LIST * list)
{
  DB_LIST *el;
  int length = 0;

  for (el = list; el != NULL; el = el->next)
    {
      length++;
    }

  return (length);
}

/*
 * ws_list_free - apply (free) function over the elements of a list
 *    return: none
 *    list(in): list to free
 *    function(in): function to perform the freeing of elements
 */
void
ws_list_free (DB_LIST * list, LFREEER function)
{
  DB_LIST *link, *next;

  for (link = list, next = NULL; link != NULL; link = next)
    {
      next = link->next;
      (*function) (link);
    }
}

/*
 * ws_list_total - maps a function over the elements of a list and totals up
 * the integers returned by the mapping function.
 *    return: total of all calls to mapping function
 *    list(in): list to examine
 *    function(in): function to call on list elements
 */
int
ws_list_total (DB_LIST * list, LTOTALER function)
{
  DB_LIST *el;
  int total = 0;

  for (el = list; el != NULL; el = el->next)
    {
      total += (*function) (el);
    }

  return (total);
}

/*
 * ws_list_copy - Copies a list by calling a copier function for each element.
 *    return: new list
 *    src(in): list to copy
 *    copier(in): function to copy the elements
 *    freeer(in): function to free the elements
 */
DB_LIST *
ws_list_copy (DB_LIST * src, LCOPIER copier, LFREEER freeer)
{
  DB_LIST *list, *last, *new_;

  list = last = NULL;
  for (; src != NULL; src = src->next)
    {
      new_ = (DB_LIST *) (*copier) (src);
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->next = NULL;
      if (list == NULL)
	{
	  list = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }
  return (list);

memory_error:
  if (freeer != NULL)
    {
      ws_list_free (list, freeer);
    }
  return NULL;
}

/*
 * ws_list_nconc - concatenate list2 to list1
 *    return: list pointer
 *    list1(out): first list
 *    list2(in): list to concatenate
 * Note:
 *    If list1 was NULL, it returns a pointer to list2.
 */
DB_LIST *
ws_list_nconc (DB_LIST * list1, DB_LIST * list2)
{
  DB_LIST *el, *result;

  if (list1 == NULL)
    {
      result = list2;
    }
  else
    {
      result = list1;
      for (el = list1; el->next != NULL; el = el->next)
	;
      el->next = list2;
    }

  return result;
}

/*
 * NAMED LIST UTILITIES
 */
/*
 * These utilities assume elements with a link field and a name.
 * struct named_link {
 *   struct named_link *next;
 *   const char *name;
 * }
 */

/*
 * nlist_find - Search a name list for an entry with the given name.
 *    return: namelist entry
 *    list(in): list to search
 *    name(in): element name to look for
 *    fcn(in): compare function
 */
DB_NAMELIST *
nlist_find (DB_NAMELIST * list, const char *name, NLSEARCHER fcn)
{
  DB_NAMELIST *el, *found;

  found = NULL;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  for (el = list; el != NULL && found == NULL; el = el->next)
    {
      if ((el->name == name) || ((el->name != NULL) && (name != NULL) && (*fcn) (el->name, name) == 0))
	{
	  found = el;
	}
    }

  return found;
}


/*
 * nlist_remove - Removes a named element from a list.
 *    return: removed element (if found), NULL otherwise
 *    root(in/out): pointer to pointer to list head
 *    name(in): name of entry to remove
 *    fcn(in): compare function
 * Note:
 *    If an element with the given name was found it is removed and returned.
 *    If an element was not found, NULL is returned.
 */
DB_NAMELIST *
nlist_remove (DB_NAMELIST ** root, const char *name, NLSEARCHER fcn)
{
  DB_NAMELIST *el, *prev, *found;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  found = NULL;

  for (el = *root, prev = NULL; el != NULL && found == NULL; el = el->next)
    {
      if ((el->name == name) || ((el->name != NULL) && (name != NULL) && (*fcn) (el->name, name) == 0))
	{
	  found = el;
	}
      else
	{
	  prev = el;
	}
    }

  if (found != NULL)
    {
      if (prev == NULL)
	{
	  *root = found->next;
	}
      else
	{
	  prev->next = found->next;
	}
    }

  return found;
}

/*
 * nlist_add - Adds an element to a namelist if it does not already exist.
 *    return: NO_ERROR if the element was added , error code otherwise
 *    list(in/out): pointer to pointer to list head
 *    name(in): element name to add
 *    fcn(in):  compare function
 *    added_ptr(out): set to 1 if added
 */
int
nlist_add (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn, int *added_ptr)
{
  DB_NAMELIST *found, *new_;
  int status = 0;

  found = nlist_find (*list, name, fcn);

  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
  if (new_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->name = ws_copy_string (name);
  if (new_->name == NULL)
    {
      db_ws_free (new_);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->next = *list;
  *list = new_;

  status = 1;

error:
  if (added_ptr != NULL)
    {
      *added_ptr = status;
    }

  return NO_ERROR;
}

/*
 * nlist_append - appends an element to a namelist if it does not exist.
 *    return: NO_ERROR if the element was added , error code otherwise
 *    list(in/out): pointer to pointer to list head
 *    name(in): entry name to append
 *    fcn(in): compare function
 *    added_ptr(out): set to 1 if added
 */
int
nlist_append (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn, int *added_ptr)
{
  DB_NAMELIST *el, *found, *last, *new_;
  int status = 0;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  if (name == NULL)
    {
      goto error;
    }

  found = NULL;
  last = NULL;

  for (el = *list; el != NULL && found == NULL; el = el->next)
    {
      if ((el->name == name) || ((el->name != NULL) && (name != NULL) && (*fcn) (el->name, name) == 0))
	{
	  found = el;
	}
      last = el;
    }

  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
  if (new_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->name = ws_copy_string (name);
  if (new_->name == NULL)
    {
      db_ws_free (new_);

      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->next = NULL;

  if (last == NULL)
    {
      *list = new_;
    }
  else
    {
      last->next = new_;
    }

  status = 1;

error:
  if (added_ptr != NULL)
    {
      *added_ptr = status;
    }

  return NO_ERROR;
}


#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * nlist_find_or_append - searches for a name or appends the element.
 *    return: error code
 *    list(in/out): pointer to pointer to list head
 *    name(in): name of element to add
 *    fcn(in): compare funciont
 *    position(out): position of element if found or inserted
 */
int
nlist_find_or_append (DB_NAMELIST ** list, const char *name, NLSEARCHER fcn, int *position)
{
  DB_NAMELIST *el, *found, *last, *new_;
  int psn = -1;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  if (name != NULL)
    {
      found = last = NULL;
      for (el = *list, psn = 0; el != NULL && found == NULL; el = el->next)
	{
	  if ((el->name == name) || ((el->name != NULL) && (*fcn) (el->name, name) == 0))
	    {
	      found = el;
	    }
	  else
	    {
	      psn++;
	    }
	  last = el;
	}
      if (found == NULL)
	{
	  new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
	  if (new_ == NULL)
	    {
	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }

	  new_->name = ws_copy_string (name);
	  if (new_->name == NULL)
	    {
	      db_ws_free (new_);

	      assert (er_errid () != NO_ERROR);
	      return er_errid ();
	    }

	  new_->next = NULL;
	  if (last == NULL)
	    {
	      *list = new_;
	    }
	  else
	    {
	      last->next = new_;
	    }
	}
    }
  *position = psn;
  return NO_ERROR;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * nlist_free - frees a name list
 *    return: none
 *    list(in/out): list to free
 */
void
nlist_free (DB_NAMELIST * list)
{
  DB_NAMELIST *el, *next;

  for (el = list, next = NULL; el != NULL; el = next)
    {
      next = el->next;
      db_ws_free ((char *) el->name);
      db_ws_free (el);
    }
}

/*
 * nlist_copy - makes a copy of a named list
 *    return: new namelist
 *    list(in): namelist to copy
 */
DB_NAMELIST *
nlist_copy (DB_NAMELIST * list)
{
  DB_NAMELIST *first, *last, *el, *new_;

  first = last = NULL;
  for (el = list; el != NULL; el = el->next)
    {
      new_ = (DB_NAMELIST *) db_ws_alloc (sizeof (DB_NAMELIST));
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->name = ws_copy_string (el->name);
      if (new_->name == NULL)
	{
	  db_ws_free (new_);
	  goto memory_error;
	}

      new_->next = NULL;
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }

  return first;

memory_error:
  nlist_free (first);
  return NULL;
}

/*
 * nlist_filter - remove all elements with the given name from a list
 * and return a list of the removed elements.
 *    return: filtered list of elements
 *    root(in/out): pointer to pointer to list head
 *    name(in): name of elements to filter
 *    fcn(in): compare function
 */
DB_NAMELIST *
nlist_filter (DB_NAMELIST ** root, const char *name, NLSEARCHER fcn)
{
  DB_NAMELIST *el, *prev, *next, *head, *filter;

  if (fcn == NULL)
    {
      fcn = (NLSEARCHER) strcmp;
    }

  filter = NULL;
  head = *root;

  for (el = head, prev = NULL, next = NULL; el != NULL; el = next)
    {
      next = el->next;
      if ((el->name == name) || ((el->name != NULL) && (name != NULL) && (*fcn) (el->name, name) == 0))
	{
	  if (prev == NULL)
	    {
	      head = next;
	    }
	  else
	    {
	      prev->next = next;
	    }
	  el->next = filter;
	  filter = el;
	}
      else
	{
	  prev = el;
	}
    }

  *root = head;
  return filter;
}

/*
 * MOP LIST UTILITIES
 */
/*
 * These utilities operate on a list of MOP links.
 * This is such a common operation for the workspace and schema manager that
 * it merits its own optimized implementation.
 *
 */

/*
 * ml_find - searches a list for the given mop.
 *    return: non-zero if mop was in the list
 *    list(in): list to search
 *    mop(in): mop we're looking for
 */
int
ml_find (DB_OBJLIST * list, MOP mop)
{
  DB_OBJLIST *l;
  int found = 0;

  for (l = list; l != NULL && !found; l = l->next)
    {
      if (ws_is_same_object (l->op, mop))
	{
	  found = 1;
	}
    }

  return found;
}

/*
 * ml_add - Adds a MOP to the list if it isn't already present.
 *    return: NO_ERROR or error code
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to add to the list
 *    added_ptr(out): set to 1 if added
 * Note:
 *    There is no guarentee where the MOP will be added in the list although
 *    currently it will push it at the head of the list.  Use ml_append
 *    if you must ensure ordering.
 */
int
ml_add (DB_OBJLIST ** list, MOP mop, int *added_ptr)
{
  DB_OBJLIST *l, *found, *new_;
  int added;

  added = 0;
  if (mop == NULL)
    {
      goto error;
    }

  for (l = *list, found = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (ws_is_same_object (l->op, mop))
	{
	  found = l;
	}
    }

  /* since we can get the end of list easily, may want to append here */
  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
  if (new_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->op = mop;
  new_->next = *list;
  *list = new_;

  added = 1;

error:
  if (added_ptr != NULL)
    {
      *added_ptr = added;
    }

  return NO_ERROR;
}

/*
 * ml_append - Appends a MOP to the list if it isn't already present.
 *    return: NO_ERROR or error code
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to add
 *    added_ptr(out): set to 1 if added
 */
int
ml_append (DB_OBJLIST ** list, MOP mop, int *added_ptr)
{
  DB_OBJLIST *l, *found, *new_, *last;
  int added;

  added = 0;
  if (mop == NULL)
    {
      goto error;
    }

  last = NULL;
  for (l = *list, found = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (ws_is_same_object (l->op, mop))
	{
	  found = l;
	}
      last = l;
    }

  /* since we can get the end of list easily, may want to append here */

  if (found != NULL)
    {
      goto error;
    }

  new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
  if (new_ == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  new_->op = mop;
  new_->next = NULL;
  if (last == NULL)
    {
      *list = new_;
    }
  else
    {
      last->next = new_;
    }

  added = 1;

error:

  if (added_ptr != NULL)
    {
      *added_ptr = added;
    }

  return NO_ERROR;
}

/*
 * ml_remove - removes a mop from a mop list if it is found.
 *    return: non-zero if mop was removed
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to remove from the list
 */
int
ml_remove (DB_OBJLIST ** list, MOP mop)
{
  DB_OBJLIST *l, *found, *prev;
  int deleted;

  deleted = 0;
  for (l = *list, found = NULL, prev = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (ws_is_same_object (l->op, mop))
	{
	  found = l;
	}
      else
	{
	  prev = l;
	}
    }

  if (found != NULL)
    {
      if (prev == NULL)
	{
	  *list = found->next;
	}
      else
	{
	  prev->next = found->next;
	}
      db_ws_free (found);
      deleted = 1;
    }

  return deleted;
}

/*
 * ml_free - free a list of MOPs.
 *    return: none
 *    list(in/out): list to free
 */
void
ml_free (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *next;

  for (l = list, next = NULL; l != NULL; l = next)
    {
      next = l->next;
      db_ws_free (l);
    }
}

/*
 * ml_copy - copy a list of mops.
 *    return: new list
 *    list(in): list to copy
 */
DB_OBJLIST *
ml_copy (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *new_, *first, *last;

  first = last = NULL;
  for (l = list; l != NULL; l = l->next)
    {
      new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->next = NULL;
      new_->op = l->op;
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;
    }

  return first;

memory_error:
  ml_free (first);
  return NULL;
}

/*
 * ml_size - This calculates the number of bytes of memory required for the
 * storage of a MOP list.
 *    return: memory size of list
 *    list(in): list to examine
 */
int
ml_size (DB_OBJLIST * list)
{
  int size = 0;

  size = ws_list_length ((DB_LIST *) list) * sizeof (DB_OBJLIST);

  return (size);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ml_filter - maps a function over the mops in a list selectively removing
 * elements based on the results of the filter function.
 *    return: void
 *    list(in/out): pointer to pointer to mop list
 *    filter(in): filter function
 *    args(in): args to pass to filter function
 * Note:
 *    If the filter function returns zero, the mop will be removed.
 */
void
ml_filter (DB_OBJLIST ** list, MOPFILTER filter, void *args)
{
  DB_OBJLIST *l, *prev, *next;
  int keep;

  prev = NULL;
  next = NULL;

  for (l = *list; l != NULL; l = next)
    {
      next = l->next;
      keep = (*filter) (l->op, args);
      if (keep)
	{
	  prev = l;
	}
      else
	{
	  if (prev != NULL)
	    {
	      prev->next = next;
	    }
	  else
	    {
	      *list = next;
	    }
	}
    }
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * DB_OBJLIST AREA ALLOCATION
 */


/*
 * ml_ext_alloc_link - This is used to allocate a mop list link for return to
 * the application layer.
 *    return: new mop list link
 * Note:
 *    These links must be allocated in areas outside the workspace
 *    so they serve as roots to the garabage collector.
 */
DB_OBJLIST *
ml_ext_alloc_link (void)
{
  return ((DB_OBJLIST *) area_alloc (Objlist_area));
}


/*
 * ml_ext_free_link - frees a mop list link that was allocated with
 * ml_ext_alloc_link.
 *    return: void
 *    link(in/out): link to free
 */
void
ml_ext_free_link (DB_OBJLIST * link)
{
  if (link != NULL)
    {
      link->op = NULL;		/* this is important */
      (void) area_free (Objlist_area, (void *) link);
    }
}

/*
 * ml_ext_free - frees a complete list of links allocated with the
 * ml_ext_alloc_link function.
 *    return: void
 *    list(in/out): list to free
 */
void
ml_ext_free (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *next;

  if (list == NULL)
    {
      return;
    }

  if (area_validate (Objlist_area, (void *) list) == NO_ERROR)
    {
      for (l = list, next = NULL; l != NULL; l = next)
	{
	  next = l->next;
	  ml_ext_free_link (l);
	}
    }
}

/*
 * ml_ext_copy - Like ml_copy except that it allocates the mop list links using
 * ml_ext_alloc_link so they can be returned to the application level.
 *    return: new mop list
 *    list(in): list to copy
 */
DB_OBJLIST *
ml_ext_copy (DB_OBJLIST * list)
{
  DB_OBJLIST *l, *new_, *first, *last;

  first = NULL;
  last = NULL;

  for (l = list; l != NULL; l = l->next)
    {
      new_ = ml_ext_alloc_link ();
      if (new_ == NULL)
	{
	  goto memory_error;
	}

      new_->next = NULL;
      new_->op = l->op;
      if (first == NULL)
	{
	  first = new_;
	}
      else
	{
	  last->next = new_;
	}

      last = new_;
    }

  return first;

memory_error:
  ml_ext_free (first);
  return NULL;
}

/*
 * ml_ext_add - same as ml_add except that it allocates a mop in the external
 * area.
 *    return: NO_ERROR or error code
 *    list(in/out): pointer to pointer to list head
 *    mop(in): mop to add to the list
 *    added_ptr(out): set to 1 if added
 */
int
ml_ext_add (DB_OBJLIST ** list, MOP mop, int *added_ptr)
{
  DB_OBJLIST *l, *found, *new_;
  int added;

  added = 0;
  if (mop == NULL)
    {
      goto error;
    }

  for (l = *list, found = NULL; l != NULL && found == NULL; l = l->next)
    {
      if (ws_is_same_object (l->op, mop))
	{
	  found = l;
	}
    }

  /* since we can get the end of list easily, may want to append here */
  if (found == NULL)
    {
      new_ = (DB_OBJLIST *) area_alloc (Objlist_area);
      if (new_ == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      new_->op = mop;
      new_->next = *list;
      *list = new_;
      added = 1;
    }

error:
  if (added_ptr != NULL)
    {
      *added_ptr = added;
    }
  return NO_ERROR;
}

/*
 * ws_set_ignore_error_list_for_mflush() -
 *    return: NO_ERROR or error code
 *    error_count(in):
 *    error_list(in):
 */
int
ws_set_ignore_error_list_for_mflush (int error_count, int *error_list)
{
  if (error_count > 0)
    {
      ws_Error_ignore_count = error_count;
      memcpy (ws_Error_ignore_list, error_list, sizeof (int) * error_count);
    }

  return NO_ERROR;
}

/*
 * ws_set_error_into_error_link() -
 *    return: void
 *    mop(in):
 */
void
ws_set_repl_error_into_error_link (LC_COPYAREA_ONEOBJ * obj, char *content_ptr)
{
  WS_REPL_FLUSH_ERR *flush_err;
  char *ptr;

  flush_err = (WS_REPL_FLUSH_ERR *) malloc (sizeof (WS_REPL_FLUSH_ERR));
  if (flush_err == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WS_REPL_FLUSH_ERR));
      return;
    }

  flush_err->class_oid = obj->class_oid;
  flush_err->operation = obj->operation;

  ptr = content_ptr;
  ptr = or_unpack_mem_value (ptr, &flush_err->pkey_value);
  ptr = or_unpack_int (ptr, &flush_err->error_code);
  ptr = or_unpack_string_alloc (ptr, &flush_err->error_msg);

  flush_err->error_link = ws_Repl_error_link;
  ws_Repl_error_link = flush_err;

  return;
}

/*
 * ws_get_mvcc_snapshot_version () - Get current snapshot version.
 *
 * return : Current snapshot version.
 */
unsigned int
ws_get_mvcc_snapshot_version (void)
{
  return ws_MVCC_snapshot_version;
}

/*
 * ws_increment_mvcc_snapshot_version () - Increment current snapshot version.
 *
 * return : Void.
 */
void
ws_increment_mvcc_snapshot_version (void)
{
  ws_MVCC_snapshot_version++;
}

/*
 * ws_is_mop_fetched_with_current_snapshot () - Check if mop was fetched
 *						during current snapshot.
 *
 * return   : True if mop was already fetched during current snapshot.
 * mop (in) : Cached object pointer.
 */
bool
ws_is_mop_fetched_with_current_snapshot (MOP mop)
{
  return (mop->mvcc_snapshot_version == ws_MVCC_snapshot_version);
}

/*
 * ws_set_mop_fetched_with_current_snapshot () - Set current snapshot version
 *						 to mop.
 *
 * return   : Void.
 * mop (in) : Mop fetched with current snapshot.
 */
void
ws_set_mop_fetched_with_current_snapshot (MOP mop)
{
  mop->mvcc_snapshot_version = ws_MVCC_snapshot_version;
}

/*
 * ws_is_dirty () - Is object dirty.
 *
 * return   : True/false.
 * mop (in) : Checked object (latest mvcc version is checked).
 */
int
ws_is_dirty (MOP mop)
{
  return mop->dirty;
}

/*
 * ws_is_deleted () - Is object deleted.
 *
 * return   : True if deleted, false otherwise
 * mop (in) : Checked object (latest mvcc version is checked).
 */
int
ws_is_deleted (MOP mop)
{
  return mop->deleted;
}

/*
 * ws_set_deleted () - Marks an object as deleted
 *
 * return   :
 * mop (in) : Object to be set as deleted
 *
 * Note: Latest mvcc version is marked
 */
void
ws_set_deleted (MOP mop)
{
  mop->deleted = 1;
  WS_PUT_COMMIT_MOP (mop);
}

/*
 * ws_is_same_object () - Check if the mops are the same
 *
 * return    : True if the same object.
 * mop1 (in) : First mop.
 * mop2 (in) : Second mop.
 */
bool
ws_is_same_object (MOP mop1, MOP mop2)
{
  return (mop1 == mop2);
}

bool
ws_is_trigger_involved ()
{
  return cdc_Trigger_involved;
}

/*
 * ws_get_error_from_error_link() -
 *    return: void
 */
WS_REPL_FLUSH_ERR *
ws_get_repl_error_from_error_link (void)
{
  WS_REPL_FLUSH_ERR *flush_err;

  flush_err = ws_Repl_error_link;
  if (flush_err == NULL)
    {
      return NULL;
    }

  ws_Repl_error_link = flush_err->error_link;
  flush_err->error_link = NULL;

  return flush_err;
}

/*
 * ws_clear_all_errors_of_error_link() -
 *    return: void
 */
void
ws_clear_all_repl_errors_of_error_link (void)
{
  WS_REPL_FLUSH_ERR *flush_err, *next;

  for (flush_err = ws_Repl_error_link; flush_err; flush_err = next)
    {
      next = flush_err->error_link;
      ws_free_repl_flush_error (flush_err);
    }
  ws_Repl_error_link = NULL;

  return;
}

/*
 * ws_free_flush_error() -
 *    return: void
 */
void
ws_free_repl_flush_error (WS_REPL_FLUSH_ERR * flush_err)
{
  assert (flush_err != NULL);

  if (flush_err->error_msg != NULL)
    {
      free_and_init (flush_err->error_msg);
    }

  db_value_clear (&flush_err->pkey_value);

  free_and_init (flush_err);

  return;
}

/*
 * ws_move_label_value_list() - move label value list
 *    return: void
 * dest_mop (in) : destination mop
 * src_mop (in) : source mop
 */
void
ws_move_label_value_list (MOP dest_mop, MOP src_mop)
{
  WS_VALUE_LIST *value_node;

  if (dest_mop == NULL || src_mop == NULL)
    {
      return;
    }

  /* move src_mop->label_value_list to dest_mop->label_value_list */
  if (dest_mop->label_value_list == NULL)
    {
      dest_mop->label_value_list = src_mop->label_value_list;
    }
  else
    {
      value_node = dest_mop->label_value_list;
      while (value_node->next != NULL)
	{
	  value_node = value_node->next;
	}

      value_node->next = src_mop->label_value_list;
    }

  /* update mop for each db_value from src_mop->label_value_list */
  for (value_node = src_mop->label_value_list; value_node != NULL; value_node = value_node->next)
    {
      if (DB_VALUE_TYPE (value_node->val) == DB_TYPE_OBJECT)
	{
	  value_node->val->data.op = dest_mop;
	}
    }

  src_mop->label_value_list = NULL;
}

/*
 * ws_remove_label_value_from_mop() - remove label value from mop value list
 *    return: void
 * mop (in) : mop
 * val (in) : value to remove from mop value list
 */
void
ws_remove_label_value_from_mop (MOP mop, DB_VALUE * val)
{
  WS_VALUE_LIST *prev_value_node, *value_node;

  if (mop == NULL || val == NULL)
    {
      return;
    }

  if (mop->label_value_list == NULL)
    {
      return;
    }

  /* search for val into mop->label_value_list */
  prev_value_node = NULL;
  value_node = mop->label_value_list;
  while (value_node != NULL)
    {
      if (value_node->val == val)
	{
	  break;
	}

      prev_value_node = value_node;
      value_node = value_node->next;
    }

  if (value_node == NULL)
    {
      /* not found */
      return;
    }

  /* remove val from mop->label_value_list */
  if (value_node == mop->label_value_list)
    {
      mop->label_value_list = mop->label_value_list->next;
    }
  else
    {
      prev_value_node->next = value_node->next;
    }

  value_node->val = NULL;
  db_ws_free (value_node);
}

/*
 * ws_add_label_value_to_mop() - add label value to mop value list
 *    return: error code
 * mop (in) : mop.
 * val (in) : value to add to mop value list
 */
int
ws_add_label_value_to_mop (MOP mop, DB_VALUE * val)
{
  WS_VALUE_LIST *value_node;

  if (mop == NULL || val == NULL)
    {
      return NO_ERROR;
    }

  value_node = (WS_VALUE_LIST *) db_ws_alloc (sizeof (WS_VALUE_LIST));
  if (value_node == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  value_node->val = val;

  if (mop->label_value_list == NULL)
    {
      value_node->next = NULL;
      mop->label_value_list = value_node;
    }
  else
    {
      value_node->next = mop->label_value_list;
      mop->label_value_list = value_node;
    }

  return NO_ERROR;
}

#if !defined (NDEBUG)
static void
ws_examine_no_mop_has_cached_lock (void)
{
  MOP mop;
  DB_OBJLIST *m = NULL;
  unsigned int slot;

  for (m = ws_Resident_classes; m != NULL; m = m->next)
    {
      for (mop = m->op->class_link; mop != NULL && mop != Null_object; mop = mop->class_link)
	{
	  assert (mop->lock == NULL_LOCK);
	}
    }

  for (slot = 0; slot < ws_Mop_table_size; slot++)
    {
      for (mop = ws_Mop_table[slot].head; mop != NULL; mop = mop->hash_link)
	{
	  assert (mop->lock == NULL_LOCK);
	}
    }
}
#endif /* !NDEBUG */

/*
 * ws_clean_label_value_list() - clean mop value list
 *    return: void
 * mop (in) : mop.
 */
void
ws_clean_label_value_list (MOP mop)
{
  WS_VALUE_LIST *next_value_node, *value_node;

  value_node = mop->label_value_list;
  while (value_node != NULL)
    {
      next_value_node = value_node->next;
      value_node->val = NULL;
      db_ws_free (value_node);
      value_node = next_value_node;
    }

  mop->label_value_list = NULL;
}

/*
 * ws_init_repl_objs() -
 *    free_func (in): function for freeing data in recdes
 *    return: void
 */
void
ws_init_repl_objs (void)
{
  ws_Repl_objs.head = NULL;
  ws_Repl_objs.tail = NULL;
  ws_Repl_objs.num_items = 0;
}

/*
 * ws_get_repl_obj_from_list() -
 *    return: repl object
 */
WS_REPL_OBJ *
ws_get_repl_obj_from_list (void)
{
  WS_REPL_OBJ *repl_obj;

  repl_obj = ws_Repl_objs.head;
  if (repl_obj != NULL)
    {
      ws_Repl_objs.head = repl_obj->next;
      if (ws_Repl_objs.head == NULL)
	{
	  ws_Repl_objs.tail = NULL;
	}

      ws_Repl_objs.num_items--;
    }

  assert (ws_Repl_objs.num_items >= 0);

  return repl_obj;
}

/*
 * ws_free_repl_obj() -
 *    return:
 */
void
ws_free_repl_obj (WS_REPL_OBJ * obj)
{
  assert (obj != NULL);

  if (obj->packed_pkey_value != NULL)
    {
      free_and_init (obj->packed_pkey_value);
    }
  free_and_init (obj);
}

/*
 * ws_clear_all_repl_objs() -
 *    return:
 */
void
ws_clear_all_repl_objs (void)
{
  WS_REPL_OBJ *obj, *next_obj;

  obj = ws_Repl_objs.head;
  while (obj != NULL)
    {
      next_obj = obj->next;
      ws_free_repl_obj (obj);

      obj = next_obj;
    }

  ws_Repl_objs.head = NULL;
  ws_Repl_objs.tail = NULL;
  ws_Repl_objs.num_items = 0;

  return;
}

/*
 * ws_add_to_repl_obj_list() - create a repl object and add it
 *  class_oid(in):
 *  key(in):
 *  recdes(in):
 *  operation(in):
 *  has_index(in):
 *
 *    return:
 */
int
ws_add_to_repl_obj_list (OID * class_oid, char *packed_pkey_value, int packed_pkey_value_length, RECDES * recdes,
			 int operation, bool has_index)
{
  WS_REPL_OBJ *repl_obj = NULL;

  repl_obj = (WS_REPL_OBJ *) malloc (sizeof (WS_REPL_OBJ));
  if (repl_obj == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WS_REPL_OBJ));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  assert (class_oid != NULL && OID_ISNULL (class_oid) == false);
  assert (packed_pkey_value != NULL && 0 < packed_pkey_value_length);

  COPY_OID (&repl_obj->class_oid, class_oid);

  repl_obj->packed_pkey_value_length = packed_pkey_value_length;
  repl_obj->packed_pkey_value = (char *) malloc (packed_pkey_value_length);
  if (repl_obj->packed_pkey_value == NULL)
    {
      free (repl_obj);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (WS_REPL_OBJ));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  memcpy (repl_obj->packed_pkey_value, packed_pkey_value, packed_pkey_value_length);

  repl_obj->recdes = recdes;
  repl_obj->has_index = has_index;
  repl_obj->operation = operation;
  repl_obj->next = NULL;

  if (ws_Repl_objs.tail == NULL)
    {
      ws_Repl_objs.head = repl_obj;
      ws_Repl_objs.tail = repl_obj;
    }
  else
    {
      ws_Repl_objs.tail->next = repl_obj;
      ws_Repl_objs.tail = repl_obj;
    }
  ws_Repl_objs.num_items++;

  return NO_ERROR;
}
