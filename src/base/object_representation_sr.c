/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/*
 * object_representation_sr.c - Class representation parsing for the server only
 * This is used for updating the catalog manager when class objects are
 * flushed to the server.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "error_manager.h"
#include "oid.h"
#include "storage_common.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "system_catalog.h"
#include "object_domain.h"
#include "set_object.h"
#include "btree_load.h"
#include "page_buffer.h"
#include "heap_file.h"
#include "class_object.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define DATA_INIT(data, type) memset(data, 0, sizeof(DB_DATA))
#define OR_ARRAY_EXTENT 10

typedef struct or_btree_property OR_BTREE_PROPERTY;
struct or_btree_property
{
  BTREE_TYPE type;
  const char *name;
  DB_SEQ *seq;
  int length;
};

static int or_get_hierarchy_helper (THREAD_ENTRY * thread_p,
				    OID * source_class, OID * class_,
				    BTID * btid, OID ** class_oids,
				    HFID ** hfids, int *num_classes,
				    int *max_classes);
static TP_DOMAIN *or_get_domain_internal (char *ptr);
static TP_DOMAIN *or_get_domain_and_cache (char *ptr);
static void or_get_att_index (char *ptr, BTID * btid);
static int or_get_default_value (OR_ATTRIBUTE * attr, char *ptr, int length);
static int or_cl_get_prop_nocopy (DB_SEQ * properties, const char *name,
				  DB_VALUE * pvalue);
static void or_install_btids_foreign_key (const char *fkname, DB_SEQ * fk_seq,
					  OR_INDEX * index);
static void or_install_btids_foreign_key_ref (DB_SEQ * fk_container,
					      OR_INDEX * index);
static void or_install_btids_class (OR_CLASSREP * rep, BTID * id,
				    DB_SEQ * constraint_seq, int max,
				    BTREE_TYPE type, const char *cons_name);
static int or_install_btids_attribute (OR_CLASSREP * rep, int att_id,
				       BTID * id);
static void or_install_btids_constraint (OR_CLASSREP * rep,
					 DB_SEQ * constraint_seq,
					 BTREE_TYPE type,
					 const char *cons_name);
static void or_install_btids (OR_CLASSREP * rep, DB_SET * props);
static OR_CLASSREP *or_get_current_representation (RECDES * record,
						   int do_indexes);
static OR_CLASSREP *or_get_old_representation (RECDES * record, int repid,
					       int do_indexes);

/*
 * orc_class_repid () - Extracts the current representation id from a record
 *                      containing the disk representation of a class
 *   return: repid of the class object
 *   record(in): disk record
 */
int
orc_class_repid (RECDES * record)
{
  char *ptr;
  int id;

  ptr = (char *) record->data +
    OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT) + ORC_REPID_OFFSET;

  id = OR_GET_INT (ptr);

  return (id);
}

/*
 * orc_class_hfid_from_record () - Extracts just the HFID from the disk
 *                                 representation of a class
 *   return: void
 *   record(in): packed disk record containing class
 *   hfid(in): pointer to HFID structure to be filled in
 *
 * Note: It is used by the catalog manager to update the class information
 *       structure when the HFID is assigned.  Since HFID's are assigned only
 *       when instances are created, a class may be entered into the catalog
 *       before the HFID is known.
 */
void
orc_class_hfid_from_record (RECDES * record, HFID * hfid)
{
  char *ptr;

  ptr = record->data + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);
  hfid->vfid.fileid = OR_GET_INT (ptr + ORC_HFID_FILEID_OFFSET);
  hfid->vfid.volid = OR_GET_INT (ptr + ORC_HFID_VOLID_OFFSET);
  hfid->hpgid = OR_GET_INT (ptr + ORC_HFID_PAGEID_OFFSET);
}

/*
 * orc_diskrep_from_record () - Calculate the corresponding DISK_REPR structure
 *                              for the catalog
 *   return: disk representation structure
 *   record(in): disk record
 */
DISK_REPR *
orc_diskrep_from_record (THREAD_ENTRY * thread_p, RECDES * record)
{
  DISK_ATTR *att, *att_fixed, *att_variable;
  OR_ATTRIBUTE *or_att;
  int i, j, k, n_attributes, n_btstats;
  BTREE_STATS *bt_statsp;

  DISK_REPR *rep = NULL;
  OR_CLASSREP *or_rep = NULL;

  VPID root_vpid;
  PAGE_PTR root;
  RECDES rec;
  BTREE_ROOT_HEADER root_header;
  BTID_INT btid_int;

  or_rep = or_get_classrep (record, -1);
  if (or_rep == NULL)
    {
      goto error;
    }

  rep = (DISK_REPR *) malloc (sizeof (DISK_REPR));
  if (rep == NULL)
    {
      goto error;
    }

  rep->id = or_rep->id;
  rep->n_fixed = 0;
  rep->n_variable = 0;
  rep->fixed_length = or_rep->fixed_length;
  rep->num_objects = 0;
  rep->fixed = NULL;
  rep->variable = NULL;

  /* Calculate the number of fixed and variable length attributes */
  n_attributes = or_rep->n_attributes;
  or_att = or_rep->attributes;
  for (i = 0; i < n_attributes; i++, or_att++)
    {
      if (or_att->is_fixed)
	{
	  (rep->n_fixed)++;
	}
      else
	{
	  (rep->n_variable)++;
	}
    }

  if (rep->n_fixed)
    {
      rep->fixed = (DISK_ATTR *) malloc (sizeof (DISK_ATTR) * rep->n_fixed);
      if (rep->fixed == NULL)
	{
	  goto error;
	}
    }

  if (rep->n_variable)
    {
      rep->variable =
	(DISK_ATTR *) malloc (sizeof (DISK_ATTR) * rep->n_variable);
      if (rep->variable == NULL)
	{
	  goto error;
	}
    }

  /* Copy the attribute information */
  att_fixed = rep->fixed;
  att_variable = rep->variable;
  or_att = or_rep->attributes;

  for (i = 0; i < n_attributes; i++, or_att++)
    {
      if (or_att->is_fixed)
	{
	  att = att_fixed;
	  att_fixed++;
	}
      else
	{
	  att = att_variable;
	  att_variable++;
	}

      att->type = or_att->type;
      att->id = or_att->id;
      att->location = or_att->location;
      att->position = or_att->position;
      att->val_length = or_att->val_length;
      att->value = or_att->value;
      or_att->value = NULL;
      att->classoid = or_att->classoid;

      DATA_INIT (&att->min_value, att->type);
      DATA_INIT (&att->max_value, att->type);

      /* initialize B+tree statisitcs information */

      n_btstats = att->n_btstats = or_att->n_btids;
      if (n_btstats > 0)
	{
	  att->bt_stats =
	    (BTREE_STATS *) malloc (sizeof (BTREE_STATS) * n_btstats);
	  if (att->bt_stats == NULL)
	    {
	      goto error;
	    }

	  for (j = 0, bt_statsp = att->bt_stats; j < n_btstats;
	       j++, bt_statsp++)
	    {
	      bt_statsp->btid = or_att->btids[j];

	      bt_statsp->leafs = 0;
	      bt_statsp->pages = 0;
	      bt_statsp->height = 0;
	      bt_statsp->keys = 0;
	      bt_statsp->oids = 0;
	      bt_statsp->nulls = 0;
	      bt_statsp->ukeys = 0;
	      bt_statsp->key_type = NULL;
	      bt_statsp->key_size = 0;
	      bt_statsp->pkeys = NULL;

	      /* read B+tree Root page header info */
	      root_vpid.pageid = bt_statsp->btid.root_pageid;
	      root_vpid.volid = bt_statsp->btid.vfid.volid;

	      if (VPID_ISNULL (&root_vpid))
		{
		  /* after create the catalog record of the class, and
		   * before create the catalog record of the constraints for the class
		   *
		   * currently, does not know BTID
		   */
		  continue;
		}

	      root = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE,
				PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
	      if (root == NULL)
		{
		  continue;
		}

	      if (spage_get_record (root, HEADER, &rec, PEEK) != S_SUCCESS)
		{
		  pgbuf_unfix (thread_p, root);
		  continue;
		}

	      btree_read_root_header (&rec, &root_header);
	      pgbuf_unfix (thread_p, root);

	      /* construct BTID_INT structure */
	      btid_int.sys_btid = &bt_statsp->btid;
	      if (btree_glean_root_header_info (&root_header, &btid_int) !=
		  NO_ERROR)
		{
		  continue;
		}

	      bt_statsp->key_type = btid_int.key_type;
	      if (bt_statsp->key_type->type->id == DB_TYPE_MIDXKEY)
		{
		  /* get full-size stats in memory */
		  bt_statsp->key_size =
		    tp_domain_size (bt_statsp->key_type->setdomain);
		}
	      else
		{
		  bt_statsp->key_size = 1;
		}

	      bt_statsp->pkeys =
		(int *) malloc (sizeof (int) * bt_statsp->key_size);
	      if (bt_statsp->pkeys == NULL)
		{
		  bt_statsp->key_size = 0;
		  continue;
		}

	      for (k = 0; k < bt_statsp->key_size; k++)
		{
		  bt_statsp->pkeys[k] = 0;
		}
	      for (k = 0; k < BTREE_STATS_RESERVED_NUM; k++)
		{
		  bt_statsp->reserved[k] = 0;
		}
	    }
	}
      else
	{
	  att->bt_stats = NULL;
	}
    }

  or_free_classrep (or_rep);
  return (rep);

error:
  if (rep != NULL)
    {
      orc_free_diskrep (rep);
    }

  if (or_rep != NULL)
    {
      or_free_classrep (or_rep);
    }

  return (NULL);
}

/*
 * orc_free_diskrep () - Frees a DISK_REPR structure that was built with
 *                       orc_diskrep_from_record
 *   return: void
 *   rep(in): representation structure
 */
void
orc_free_diskrep (DISK_REPR * rep)
{
  int i, j;

  if (rep != NULL)
    {
      if (rep->fixed != NULL)
	{
	  for (i = 0; i < rep->n_fixed; i++)
	    {
	      if (rep->fixed[i].value != NULL)
		{
		  free_and_init (rep->fixed[i].value);
		}

	      if (rep->fixed[i].bt_stats != NULL)
		{
		  for (j = 0; j < rep->fixed[i].n_btstats; j++)
		    {
		      if (rep->fixed[i].bt_stats[j].pkeys)
			{
			  free_and_init (rep->fixed[i].bt_stats[j].pkeys);
			}
		    }

		  free_and_init (rep->fixed[i].bt_stats);
		  rep->fixed[i].bt_stats = NULL;
		}
	    }

	  free_and_init (rep->fixed);
	}

      if (rep->variable != NULL)
	{
	  for (i = 0; i < rep->n_variable; i++)
	    {
	      if (rep->variable[i].value != NULL)
		{
		  free_and_init (rep->variable[i].value);
		}

	      if (rep->variable[i].bt_stats != NULL)
		{
		  for (j = 0; j < rep->variable[i].n_btstats; j++)
		    {
		      if (rep->variable[i].bt_stats[j].pkeys)
			{
			  free_and_init (rep->variable[i].bt_stats[j].pkeys);
			}
		    }

		  free_and_init (rep->variable[i].bt_stats);
		  rep->variable[i].bt_stats = NULL;
		}
	    }

	  free_and_init (rep->variable);
	}

      free_and_init (rep);
    }
}

/*
 * orc_class_info_from_record () - Extract the information necessary to build
 *                                 a CLS_INFO
 *    structure for the catalog
 *   return: class info structure
 *   record(in): disk record with class
 */
CLS_INFO *
orc_class_info_from_record (RECDES * record)
{
  CLS_INFO *info;

  info = (CLS_INFO *) malloc (sizeof (CLS_INFO));

  info->tot_pages = 0;
  info->tot_objects = 0;
  info->time_stamp = 0;

  orc_class_hfid_from_record (record, &info->hfid);

  return (info);
}

/*
 * orc_free_class_info () - Frees a CLS_INFO structure that was allocated by
 *                          orc_class_info_from_record
 *   return: void
 *   info(in): class info structure
 */
void
orc_free_class_info (CLS_INFO * info)
{
  free_and_init (info);
}

/*
 * orc_subclasses_from_record () - Extracts the OID's of the immediate
 *                                 subclasses
 *   return: error code
 *   record(in): record containing a class
 *   array_size(out): pointer to int containing max size of array
 *   array_ptr(out): pointer to OID array
 *
 * Note: The array is maintained as an array of OID's, the last element in the
 *       array will satisfy the OID_ISNULL() test.  The array_size has
 *       the number of actual elements allocated in the array which may be more
 *       than the number of slots that have non-NULL OIDs.
 *       The function adds the subclass oids to the existing array.  If the
 *       array is not large enough, it is reallocated using realloc.
 */
int
orc_subclasses_from_record (RECDES * record, int *array_size,
			    OID ** array_ptr)
{
  int error = NO_ERROR;
  OID *array;
  char *ptr;
  int max, insert, i, newsize, nsubs;
  char *subset = NULL;

  nsubs = 0;

  if (!OR_VAR_IS_NULL (record->data, ORC_SUBCLASSES_INDEX))
    {
      subset =
	(char *) (record->data) + OR_VAR_OFFSET (record->data,
						 ORC_SUBCLASSES_INDEX);
      nsubs = OR_SET_ELEMENT_COUNT (subset);
    }

  if (nsubs)
    {
      max = *array_size;
      array = *array_ptr;
      if (array == NULL)
	{
	  max = 0;
	}

      /* find the last element in the array */
      for (i = 0; i < max && !OID_ISNULL (&array[i]); i++)
	{
	  ;
	}
      insert = i;

      /*
       * check for array extension.
       * Add one in the comparison since a NULL_OID is set at the end of the
       * array
       */
      if ((insert + nsubs + 1) > max)
	{
	  newsize = insert + nsubs + 10;
	  if (array == NULL)
	    {
	      array = (OID *) malloc (newsize * sizeof (OID));
	    }
	  else
	    {
	      array = (OID *) realloc (array, newsize * sizeof (OID));
	    }

	  if (array == NULL)
	    {
	      return er_errid ();
	    }

	  for (i = max; i < newsize; i++)
	    {
	      OID_SET_NULL (&array[i]);
	    }

	  max = newsize;
	}

      /* Advance past the set header, the domain size, and the "object" domain.
       * Note that this assumes we are not using a bound bit array even though
       * this is a fixed width homogeneous set.  Probably not a good assumption.
       */
      ptr = subset + OR_SET_HEADER_SIZE + OR_INT_SIZE + OR_INT_SIZE;

      /* add the new OIDs */
      for (i = 0; i < nsubs; i++)
	{
	  OR_GET_OID (ptr, &array[insert + i]);
	  ptr += OR_OID_SIZE;
	}
      OID_SET_NULL (&array[insert + nsubs]);

      /* return these in case there were changes */
      *array_size = max;
      *array_ptr = array;
    }

  return error;
}

/*
 * or_class_repid () - extracts the current representation id from a record
 *                     containing the disk representation of a class
 *   return: disk record
 *   record(in): repid of the class object
 */
int
or_class_repid (RECDES * record)
{
  char *ptr;
  int id;

  ptr = (char *) record->data +
    OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT) + ORC_REPID_OFFSET;

  id = OR_GET_INT (ptr);

  return (id);
}

/*
 * or_class_hfid () - Extracts just the HFID from the disk representation of
 *                    a class
 *   return: void
 *   record(in): packed disk record containing class
 *   hfid(in): pointer to HFID structure to be filled in
 *
 * Note: It is used by the catalog manager to update the class information
 *       structure when the HFID is assigned.  Since HFID's are assigned only
 *       when instances are created, a class may be entered into the catalog
 *       before the HFID is known.
 */
void
or_class_hfid (RECDES * record, HFID * hfid)
{
  char *ptr;

  ptr = record->data + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);
  hfid->vfid.fileid = OR_GET_INT (ptr + ORC_HFID_FILEID_OFFSET);
  hfid->vfid.volid = OR_GET_INT (ptr + ORC_HFID_VOLID_OFFSET);
  hfid->hpgid = OR_GET_INT (ptr + ORC_HFID_PAGEID_OFFSET);
}

/*
 * or_class_statistics () - extracts the OID of the statistics instance for
 *                          this class from the disk representation of a class
 *   return: void
 *   record(in): packed disk record containing class
 *   oid(in): pointer to OID structure to be filled in
 */
void
or_class_statistics (RECDES * record, OID * oid)
{
  char *ptr;

  ptr = record->data + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);

  /* this doesn't exist yet, return NULL */
  OID_SET_NULL (oid);
}

/*
 * or_class_subclasses () - Extracts the OID's of the immediate subclasses
 *   return: error code
 *   record(in): record containing a class
 *   array_size(out): pointer to int containing max size of array
 *   array_ptr(out): pointer to OID array
 *
 * Note: The array is maintained as an array of OID's, the last element in the
 *       array will satisfy the OID_ISNULL() test.  The array_size has
 *       the number of actual elements allocated in the array which may be more
 *       than the number of slots that have non-NULL OIDs.
 *       The function adds the subclass oids to the existing array.  If the
 *       array is not large enough, it is reallocated using realloc.
 */
int
or_class_subclasses (RECDES * record, int *array_size, OID ** array_ptr)
{
  int error = NO_ERROR;
  OID *array;
  char *ptr;
  int max, insert, i, newsize, nsubs;
  char *subset = NULL;

  nsubs = 0;
  if (!OR_VAR_IS_NULL (record->data, ORC_SUBCLASSES_INDEX))
    {
      subset =
	(char *) (record->data) + OR_VAR_OFFSET (record->data,
						 ORC_SUBCLASSES_INDEX);
      nsubs = OR_SET_ELEMENT_COUNT (subset);
    }

  if (nsubs)
    {
      max = *array_size;
      array = *array_ptr;
      if (array == NULL)
	{
	  max = 0;
	}

      /* find the last element in the array */
      for (i = 0; i < max && !OID_ISNULL (&array[i]); i++)
	{
	  ;
	}
      insert = i;

      /*
       * check for array extension.
       * Add one in the comparison since a NULL_OID is set at the end of the
       * array
       */
      if ((insert + nsubs + 1) > max)
	{
	  newsize = insert + nsubs + 10;
	  if (array == NULL)
	    {
	      array = (OID *) malloc (newsize * sizeof (OID));
	    }
	  else
	    {
	      array = (OID *) realloc (array, newsize * sizeof (OID));
	    }

	  if (array == NULL)
	    {
	      return er_errid ();
	    }

	  for (i = max; i < newsize; i++)
	    {
	      OID_SET_NULL (&array[i]);
	    }
	  max = newsize;
	}

      /* Advance past the set header, the domain size, and the "object" domain.
       * Note that this assumes we are not using a bound bit array even though
       * this is a fixed width homogeneous set.  Probably not a good assumption.
       */
      ptr = subset + OR_SET_HEADER_SIZE + OR_INT_SIZE + OR_INT_SIZE;

      /* add the new OIDs */
      for (i = 0; i < nsubs; i++)
	{
	  OR_GET_OID (ptr, &array[insert + i]);
	  ptr += OR_OID_SIZE;
	}
      OID_SET_NULL (&array[insert + nsubs]);

      /* return these in case there were changes */
      *array_size = max;
      *array_ptr = array;
    }

  return error;
}

/*
 * or_get_hierarchy_helper () -
 *   return: error code
 *   source_class(in):
 *   class(in):
 *   btid(in):
 *   class_oids(out):
 *   hfids(out):
 *   num_classes(out):
 *   max_classes(out):
 *
 * Note: This routine gets checks the class to see if it has an attribute named
 *       attr_name, that has a source class equal to source_class.  This would
 *       mean that the given class participates in the hierachy.  We add its
 *       heap and attribute id to the arrays and recurse for any subclasses
 *       that it might have.
 */
static int
or_get_hierarchy_helper (THREAD_ENTRY * thread_p, OID * source_class,
			 OID * class_, BTID * btid, OID ** class_oids,
			 HFID ** hfids, int *num_classes, int *max_classes)
{
  char *ptr;
  char *subset = NULL;
  OID sub_class;
  int i, nsubs, found, newsize;
  RECDES record;
  OR_CLASSREP *or_rep = NULL;
  OR_INDEX *or_index;
  HFID hfid;

  record.data = NULL;
  record.area_size = 0;
  if (heap_get_alloc (thread_p, class_, &record) != NO_ERROR)
    {
      goto error;
    }

  or_rep = or_get_classrep (&record, -1);
  if (or_rep == NULL)
    {
      goto error;
    }

  found = 0;
  or_index = &(or_rep->indexes[0]);
  for (i = 0; i < or_rep->n_indexes && !found; i++, or_index++)
    {
      if (BTID_IS_EQUAL (&(or_index->btid), btid))
	{
	  found = 1;
	}
    }

  if (!found)
    {
      goto success;
    }

  /*
   *  For each subclass, recurse ...
   *  Unfortunately, this information is not available in the OR_CLASSREP
   *  structure, so we'll digress into the RECDES structure for it.  It
   *  might be a good idea to add subclass information to the OR_CLASSREP
   *  structure.
   */
  nsubs = 0;
  if (!OR_VAR_IS_NULL (record.data, ORC_SUBCLASSES_INDEX))
    {
      subset = (char *) (record.data) +
	OR_VAR_OFFSET (record.data, ORC_SUBCLASSES_INDEX);
      nsubs = OR_SET_ELEMENT_COUNT (subset);
    }

  if (nsubs)
    {
      /* Advance past the set header, the domain size, and the "object" domain.
       * Note that this assumes we are not using a bound bit array even though
       * this is a fixed width homogeneous set.  Probably not a good assumption.
       */
      ptr = subset + OR_SET_HEADER_SIZE + OR_INT_SIZE + OR_INT_SIZE;

      for (i = 0; i < nsubs; i++)
	{
	  OR_GET_OID (ptr, &sub_class);
	  if (or_get_hierarchy_helper
	      (thread_p, source_class, &sub_class, btid, class_oids, hfids,
	       num_classes, max_classes) != NO_ERROR)
	    {
	      goto error;
	    }

	  ptr += OR_OID_SIZE;
	}
    }

  /* If we have a valid HFID, then add this class to the array */
  or_class_hfid (&record, &hfid);
  if (HFID_IS_NULL (&hfid))
    {
      goto success;
    }

  /* Need to remove duplicates from a multiple inheritance hierarchy */
  for (i = 0; i < *num_classes; i++)
    {
      if (OID_EQ (class_, &((*class_oids)[i])))
	{
	  goto success;
	}
    }

  /* do we need to extend the arrays? */
  if ((*num_classes + 1) > *max_classes)
    {
      newsize = *max_classes + OR_ARRAY_EXTENT;

      if (*class_oids == NULL)
	{
	  *class_oids = (OID *) malloc (newsize * sizeof (OID));
	}
      else
	{
	  *class_oids = (OID *) realloc (*class_oids, newsize * sizeof (OID));
	}

      if (*hfids == NULL)
	{
	  *hfids = (HFID *) malloc (newsize * sizeof (HFID));
	}
      else
	{
	  *hfids = (HFID *) realloc (*hfids, newsize * sizeof (HFID));
	}

      if (*class_oids == NULL || *hfids == NULL)
	{
	  goto error;
	}

      *max_classes = newsize;
    }

  COPY_OID (&((*class_oids)[*num_classes]), class_);
  (*hfids)[*num_classes] = hfid;
  *num_classes += 1;

success:
  or_free_classrep (or_rep);
  free_and_init (record.data);
  return NO_ERROR;

error:
  if (or_rep != NULL)
    {
      or_free_classrep (or_rep);
    }

  if (record.data)
    {
      free_and_init (record.data);
    }

  return er_errid ();
}

/*
 * or_get_unique_hierarchy () -
 *   return:
 *   record(in): record containing a class
 *   attrid(in): unique attrid (the one we'll get the hierarchy for)
 *   btid(in):
 *   class_oids(out):
 *   hfids(out): pointer to HFID array
 *   num_classes(out):
 *
 * Note: This function uses the attribute represented by the attrid and finds
 *       the source class for that attribute (where it was defined in the class
 *       hierarchy).  Then the heap files for all the classes that are in the
 *       hierarchy rooted at the source class that contain the (non shadowed)
 *       attribute are returned in the hfids array and their respective attrids
 *       are returned in the attrids array.  num_heaps will indicate how many
 *       positions of the arrays are valid.
 *
 *       it is the callers responsibility to free the hfids and attrids
 *       arrays if this routine returns successfully.
 *
 *       <attrid> is currently used only to find the source class for
 *       the BTID.  The index attributes can be obtained through the
 *       BTID if needed so it might be a good idea to get rid of the
 *       attribute ID parameter since it no longer represents the
 *       index well.
 */
int
or_get_unique_hierarchy (THREAD_ENTRY * thread_p, RECDES * record, int attrid,
			 BTID * btid, OID ** class_oids, HFID ** hfids,
			 int *num_classes)
{
  int n_attributes, n_fixed, n_variable, i;
  int id, found, max_classes;
  char *attr_name, *start, *ptr, *attset, *diskatt = NULL;
  OID source_class;

  *num_classes = 0;
  max_classes = 0;
  *class_oids = NULL;
  *hfids = NULL;

  /* find the source class of the attribute from the record */
  start = record->data;
  ptr = start + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);

  n_fixed = OR_GET_INT (ptr + ORC_FIXED_COUNT_OFFSET);
  n_variable = OR_GET_INT (ptr + ORC_VARIABLE_COUNT_OFFSET);
  n_attributes = n_fixed + n_variable;

  /* find the start of the "set_of(attribute)" attribute inside the class */
  attset = start + OR_VAR_OFFSET (start, ORC_ATTRIBUTES_INDEX);

  /* loop over each attribute in the class record to find our attribute */
  for (i = 0, found = 0; i < n_attributes && !found; i++)
    {
      /* diskatt will now be pointing at the offset table for this attribute.
         this is logically the "start" of this nested object. */
      diskatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);

      /* set ptr to the beginning of the fixed attributes */
      ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);

      /* is this the attribute we want? */
      id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      if (id == attrid)
	{
	  found = 1;
	  OR_GET_OID (ptr + ORC_ATT_CLASS_OFFSET, &source_class);
	}
    }

  /* diskatt now points to the attribute that we are interested in.
     Get the attribute name. */
  attr_name = (diskatt + OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						      ORC_ATT_NAME_INDEX));

  if (!found
      || (OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_NAME_INDEX) == 0)
      || (or_get_hierarchy_helper (thread_p, &source_class, &source_class,
				   btid, class_oids, hfids,
				   num_classes, &max_classes) != NO_ERROR))
    {
      goto error;
    }

  return NO_ERROR;

error:
  if (*class_oids)
    {
      free_and_init (*class_oids);
    }

  if (*hfids)
    {
      free_and_init (*hfids);
    }

  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);

  return ER_GENERIC_ERROR;
}

/*
 * or_get_domain_internal () -
 *   return: transient domain
 *   ptr(in): pointer to the beginning of a domain in a class
 */
static TP_DOMAIN *
or_get_domain_internal (char *ptr)
{
  TP_DOMAIN *domain, *last, *new_;
  int n_domains, offset, i;
  char *dstart, *fixed;
  DB_TYPE typeid_;

  domain = last = NULL;

  /* ptr has the beginning of a substructure set of domains */
  n_domains = OR_SET_ELEMENT_COUNT (ptr);
  for (i = 0; i < n_domains; i++)
    {
      /* find the start of the domain in the set */
      dstart = ptr + OR_SET_ELEMENT_OFFSET (ptr, i);

      /* dstart points to the offset table for this substructure,
         get the position of the first fixed attribute. */
      fixed = dstart + OR_VAR_TABLE_SIZE (ORC_DOMAIN_VAR_ATT_COUNT);

      typeid_ = (DB_TYPE) OR_GET_INT (fixed + ORC_DOMAIN_TYPE_OFFSET);

      new_ = tp_domain_new (typeid_);
      if (new_ == NULL)
	{
	  if (domain != NULL)
	    {
	      tp_domain_free (domain);
	    }
	  return NULL;
	}

      if (last == NULL)
	{
	  domain = new_;
	}
      else
	{
	  last->next = new_;
	}
      last = new_;

      new_->precision = OR_GET_INT (fixed + ORC_DOMAIN_PRECISION_OFFSET);
      new_->scale = OR_GET_INT (fixed + ORC_DOMAIN_SCALE_OFFSET);
      new_->codeset = OR_GET_INT (fixed + ORC_DOMAIN_CODESET_OFFSET);

      OR_GET_OID (fixed + ORC_DOMAIN_CLASS_OFFSET, &new_->class_oid);
      /* can't swizzle the pointer on the server */
      new_->class_mop = NULL;

      if (OR_VAR_TABLE_ELEMENT_LENGTH (dstart, ORC_DOMAIN_SETDOMAIN_INDEX) ==
	  0)
	{
	  new_->setdomain = NULL;
	}
      else
	{
	  offset =
	    OR_VAR_TABLE_ELEMENT_OFFSET (dstart, ORC_DOMAIN_SETDOMAIN_INDEX);
	  new_->setdomain = or_get_domain_internal (dstart + offset);
	}
    }

  return domain;
}

/*
 * or_get_domain_and_cache () -
 *   return:
 *   ptr(in):
 */
static TP_DOMAIN *
or_get_domain_and_cache (char *ptr)
{
  TP_DOMAIN *domain;

  domain = or_get_domain_internal (ptr);
  if (domain != NULL)
    {
      domain = tp_domain_cache (domain);
    }

  return domain;
}

/*
 * or_get_att_index () - Extracts a BTID from the disk representation of an
 *                       attribute
 *   return: void
 *   ptr(in): buffer pointer
 *   btid(out): btree identifier
 */
static void
or_get_att_index (char *ptr, BTID * btid)
{
  unsigned int uval;

  btid->vfid.fileid = (FILEID) OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  btid->root_pageid = (PAGEID) OR_GET_INT (ptr);
  ptr += OR_INT_SIZE;
  uval = (unsigned int) OR_GET_INT (ptr);
  btid->vfid.volid = (VOLID) (uval & 0xFFFF);
}

/*
 * or_get_default_value () - Copies the default value of an attribute from disk
 *   return: zero to indicate error
 *   attr(in): disk attribute structure
 *   ptr(in): pointer to beginning of value
 *   length(in): length of value on disk
 *
 * Note: The data manipulation for this is a bit odd, owing to the "rich"
 *       and varied history of default value manipulation in the catalog.
 *       The callers expect to be given a value buffer in disk representation
 *       format, which as it turns out they will immediately turn around and
 *       use the "readval" function on to get it into a DB_VALUE.  This prevents
 *       us from actually returning the value in a DB_VALUE here because then
 *       the callers would have to deal with two different value formats,
 *       diskrep for non-default values and DB_VALUE rep for default values.
 *       This might not be hard to do and should be considered at some point.
 *
 *       As it stands, we have to perform some of the same operations as
 *       or_get_value here and return a buffer containing a copy of the disk
 *       representation of the value only (not the domain).
 */
static int
or_get_default_value (OR_ATTRIBUTE * attr, char *ptr, int length)
{
  int success, is_null;
  TP_DOMAIN *domain;
  char *vptr;

  if (length == 0)
    {
      return 1;
    }

  /* skip over the domain tag, check for tagged NULL */
  success = 0;
  domain = NULL;
  vptr = or_unpack_domain (ptr, &domain, &is_null);
  if (domain == NULL)
    {
      return 0;
    }

  /* reduce the expected size by the amount consumed with the domain tag */
  length -= (int) (vptr - ptr);

  if (is_null || length == 0)
    {
      success = 1;
    }
  else
    {
      attr->val_length = length;
      attr->value = malloc (length);
      if (attr->value != NULL)
	{
	  memcpy (attr->value, vptr, length);
	  success = 1;
	}
    }

  return success;
}

/*
 * or_cl_get_prop_nocopy () - Modified version of classobj_get_prop that tries to
 *                            avoid copying of the values
 *   return: non-zero if the property was found
 *   properties(in):  property sequence
 *   name(in): name of property to find
 *   pvalue(in): property value
 *
 * Note: This was written for object_representation_sr.c but could be used in other cases if
 *       you're careful.
 *       Uses the hacked set_get_element_nocopy function above, this is
 *       probably what we should be doing anyway, it would make property list
 *       operations faster.
 */
static int
or_cl_get_prop_nocopy (DB_SEQ * properties, const char *name,
		       DB_VALUE * pvalue)
{
  int error;
  int found, max, i;
  DB_VALUE value;
  char *prop_name;

  error = NO_ERROR;
  found = 0;

  if (properties != NULL && name != NULL && pvalue != NULL)
    {
      max = set_size (properties);
      for (i = 0; i < max && !found && error == NO_ERROR; i += 2)
	{
	  error = set_get_element_nocopy (properties, i, &value);
	  if (error == NO_ERROR)
	    {
	      if (DB_VALUE_TYPE (&value) != DB_TYPE_STRING
		  || DB_GET_STRING (&value) == NULL)
		{
		  error = ER_SM_INVALID_PROPERTY;
		}
	      else
		{
		  prop_name = DB_GET_STRING (&value);
		  if (strcmp (name, prop_name) == 0)
		    {
		      if ((i + 1) >= max)
			{
			  error = ER_SM_INVALID_PROPERTY;
			}
		      else
			{
			  error =
			    set_get_element_nocopy (properties, i + 1,
						    pvalue);
			  if (error == NO_ERROR)
			    found = i + 1;
			}
		    }
		}
	    }
	}
    }

  if (error)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }

  return (found);
}

/*
 * or_install_btids_foreign_key () -
 *   return:
 *   fkname(in):
 *   fk_seq(in):
 *   index(in):
 */
static void
or_install_btids_foreign_key (const char *fkname, DB_SEQ * fk_seq,
			      OR_INDEX * index)
{
  DB_VALUE val;
  int args;
  int pageid, slotid, volid, fileid;

  index->fk = (OR_FOREIGN_KEY *) malloc (sizeof (OR_FOREIGN_KEY));
  if (index->fk != NULL)
    {
      if (set_get_element_nocopy (fk_seq, 0, &val) != NO_ERROR)
	{
	  return;
	}

      index->fk->next = NULL;
      index->fk->fkname = strdup (fkname);

      args = classobj_decompose_property_oid (DB_GET_STRING (&val),
					      &pageid, &slotid, &volid);
      if (args != 3)
	{
	  return;
	}

      index->fk->ref_class_oid.pageid = (PAGEID) pageid;
      index->fk->ref_class_oid.slotid = (PGSLOTID) slotid;
      index->fk->ref_class_oid.volid = (VOLID) volid;

      if (set_get_element_nocopy (fk_seq, 1, &val) != NO_ERROR)
	{
	  return;
	}

      args = classobj_decompose_property_oid (DB_GET_STRING (&val),
					      &volid, &fileid, &pageid);

      if (args != 3)
	{
	  return;
	}

      index->fk->ref_class_pk_btid.vfid.volid = (VOLID) volid;
      index->fk->ref_class_pk_btid.root_pageid = (PAGEID) pageid;
      index->fk->ref_class_pk_btid.vfid.fileid = (FILEID) fileid;

      set_get_element_nocopy (fk_seq, 2, &val);
      index->fk->del_action = DB_GET_INT (&val);

      set_get_element_nocopy (fk_seq, 3, &val);
      index->fk->upd_action = DB_GET_INT (&val);

      set_get_element_nocopy (fk_seq, 4, &val);
      if (DB_IS_NULL (&val))
	{
	  index->fk->is_cache_obj = false;
	}
      else
	{
	  index->fk->is_cache_obj = true;
	}

      set_get_element_nocopy (fk_seq, 5, &val);
      index->fk->cache_attr_id = DB_GET_INT (&val);
    }
}

/*
 * or_install_btids_foreign_key_ref () -
 *   return:
 *   fk_container(in):
 *   index(in):
 */
static void
or_install_btids_foreign_key_ref (DB_SEQ * fk_container, OR_INDEX * index)
{
  DB_VALUE val, fkval;
  int args, size, i;
  int pageid, slotid, volid, fileid;
  DB_SEQ *fk_seq;
  OR_FOREIGN_KEY *fk, *p;
  char *fkname;

  size = set_size (fk_container);

  for (i = 0; i < size; i++)
    {
      if (set_get_element_nocopy (fk_container, i, &fkval) != NO_ERROR)
	{
	  return;
	}

      fk_seq = DB_GET_SEQUENCE (&fkval);

      fk = (OR_FOREIGN_KEY *) malloc (sizeof (OR_FOREIGN_KEY));
      if (fk != NULL)
	{
	  fk->next = NULL;

	  if (set_get_element_nocopy (fk_seq, 0, &val) != NO_ERROR)
	    {
	      return;
	    }

	  args = classobj_decompose_property_oid (DB_GET_STRING (&val),
						  &pageid, &slotid, &volid);

	  if (args != 3)
	    {
	      return;
	    }

	  fk->self_oid.pageid = (PAGEID) pageid;
	  fk->self_oid.slotid = (PGSLOTID) slotid;
	  fk->self_oid.volid = (VOLID) volid;

	  if (set_get_element_nocopy (fk_seq, 1, &val) != NO_ERROR)
	    {
	      return;
	    }

	  args = classobj_decompose_property_oid (DB_GET_STRING (&val),
						  &volid, &fileid, &pageid);

	  if (args != 3)
	    {
	      return;
	    }

	  fk->self_btid.vfid.volid = (VOLID) volid;
	  fk->self_btid.root_pageid = (PAGEID) pageid;
	  fk->self_btid.vfid.fileid = (FILEID) fileid;

	  set_get_element_nocopy (fk_seq, 2, &val);
	  fk->del_action = DB_GET_INT (&val);

	  set_get_element_nocopy (fk_seq, 3, &val);
	  fk->upd_action = DB_GET_INT (&val);

	  set_get_element_nocopy (fk_seq, 4, &val);
	  fkname = DB_GET_STRING (&val);
	  fk->fkname = strdup (fkname);

	  set_get_element_nocopy (fk_seq, 5, &val);
	  fk->cache_attr_id = DB_GET_INT (&val);

	  if (i == 0)
	    {
	      index->fk = fk;
	      p = index->fk;
	    }
	  else
	    {
	      p->next = fk;
	      p = p->next;
	    }
	}
    }
}

/*
 * or_install_btids_class () - Install (add) the B-tree ID to the index
 *                             structure of the class representation
 *   return: void
 *   rep(in): Class representation
 *   id(in): B-tree ID
 *   constraint_seq(in): Set which contains the attribute ID's
 *   max(in): Number of elements in the set
 *   type(in):
 *   cons_name(in):
 *
 * Note: The index structure (OR_INDEX) is assumed to
 *       be allocated before this function is called.  We will allocate
 *       room for the attribute pointer array which will be filled with
 *       pointers to attributes (also from the class representation) which
 *       share this B-tree ID (and associated constraint).
 *
 *       The purpose of this function is to provide a list of B-tree IDs at
 *       that belong to the class and to provide a reference to the attributes
 *       that are associated with each B-tree ID.  This complements the other
 *       structures which are in place that provide a list of B-tree IDs
 *       associated with an attribute in each attribute structure
 *       (OR_ATTRIBUTE).
 *       { [attrID, asc_desc]+, {fk_info} }
 */
static void
or_install_btids_class (OR_CLASSREP * rep, BTID * id, DB_SEQ * constraint_seq,
			int max, BTREE_TYPE type, const char *cons_name)
{
  DB_VALUE att_val;
  int i, j, e;
  int att_id, att_cnt;
  OR_ATTRIBUTE *att;
  OR_ATTRIBUTE *ptr = NULL;

  /* Remember that the attribute IDs start in the second position. */
  if (max > 1)
    {
      OR_INDEX *index = &(rep->indexes[rep->n_indexes]);

      att_cnt = (max - 1) / 2;
      index->atts =
	(OR_ATTRIBUTE **) malloc (sizeof (OR_ATTRIBUTE *) * att_cnt);
      if (index->atts != NULL)
	{
	  (rep->n_indexes)++;
	  index->btid = *id;
	  index->n_atts = 0;
	  index->type = type;
	  index->fk = NULL;

	  /*
	   * For each attribute ID in the set,
	   *   Extract the attribute ID,
	   *   Find the matching attribute and insert the pointer into the array.
	   */
	  e = 1;		/* init */

	  for (i = 0; i < att_cnt; i++)
	    {
	      if (set_get_element_nocopy (constraint_seq, e++, &att_val) ==
		  NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&att_val) == DB_TYPE_SEQUENCE)
		    {
		      break;
		    }

		  att_id = DB_GET_INTEGER (&att_val);

		  for (j = 0, att = rep->attributes, ptr = NULL;
		       j < rep->n_attributes && ptr == NULL; j++, att++)
		    {
		      if (att->id == att_id)
			{
			  ptr = att;
			  index->atts[index->n_atts] = ptr;
			  (index->n_atts)++;
			}
		    }

		}

	      /* currently, skip asc_desc info
	       * DO NOT DELETE ME FOR FUTURE NEEDS
	       */
	      e++;
	    }
	  index->btname = strdup (cons_name);

	  if (type == BTREE_FOREIGN_KEY)
	    {
	      if (set_get_element_nocopy (constraint_seq, max - 1, &att_val)
		  == NO_ERROR)
		{
		  or_install_btids_foreign_key (cons_name,
						DB_GET_SEQUENCE (&att_val),
						index);
		}
	    }
	  else if (type == BTREE_PRIMARY_KEY)
	    {
	      index->fk = NULL;
	      if (set_get_element_nocopy (constraint_seq, max - 1, &att_val)
		  == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&att_val) == DB_TYPE_SEQUENCE)
		    {
		      or_install_btids_foreign_key_ref (DB_GET_SEQUENCE
							(&att_val), index);
		    }
		}
	    }
	}
    }
}

/*
 * or_install_btids_attribute () - Install (add) the B-tree ID to the
 *                                 appropriate attribute in the class
 *                                 representation
 *   return:
 *   rep(in): Class representation
 *   att_id(in): Attribute ID
 *   id(in): B-tree ID
 */
static int
or_install_btids_attribute (OR_CLASSREP * rep, int att_id, BTID * id)
{
  int i;
  OR_ATTRIBUTE *att;
  int success = 1;
  OR_ATTRIBUTE *ptr = NULL;
  int size;

  /*  Find the attribute with the matching attribute ID */
  for (i = 0, att = rep->attributes;
       i < rep->n_attributes && ptr == NULL; i++, att++)
    {
      if (att->id == att_id)
	{
	  ptr = att;
	}
    }

  /* Allocate storage for the ID and store it */
  if (ptr != NULL)
    {
      if (ptr->btids == NULL)
	{
	  /* we've never had one before, use the local pack */
	  ptr->btids = ptr->btid_pack;
	  ptr->max_btids = OR_ATT_BTID_PREALLOC;
	}
      else
	{
	  /* we've already got one, continue to use the local pack until that
	     runs out and then start mallocing. */
	  if (ptr->n_btids >= ptr->max_btids)
	    {
	      if (ptr->btids == ptr->btid_pack)
		{
		  /* allocate a bigger array and copy over our local pack */
		  size = ptr->n_btids + OR_ATT_BTID_PREALLOC;
		  ptr->btids = (BTID *) malloc (sizeof (BTID) * size);
		  if (ptr->btids != NULL)
		    {
		      memcpy (ptr->btids, ptr->btid_pack,
			      (sizeof (BTID) * ptr->n_btids));
		    }
		  ptr->max_btids = size;
		}
	      else
		{
		  /* we already have an externally allocated array, make it
		     bigger */
		  size = ptr->n_btids + OR_ATT_BTID_PREALLOC;
		  ptr->btids =
		    (BTID *) realloc (ptr->btids, size * sizeof (BTID));
		  ptr->max_btids = size;
		}
	    }
	}

      if (ptr->btids)
	{
	  ptr->btids[ptr->n_btids] = *id;
	  ptr->n_btids += 1;
	}
      else
	{
	  success = 0;
	}
    }

  return success;
}

/*
 * or_install_btids_constraint () - Install the constraint into the appropriate
 *                                  attributes
 *   return:
 *   rep(in): Class representation
 *   constraint_seq(in): Constraint
 *   type(in):
 *   cons_name(in):
 *
 * Note: The constraint may be associated with multiple attributes.
 *       The form of the constraint is:
 *
 *       {btid, [attribute_ID, asc_desc]+ {fk_info}}
 */
static void
or_install_btids_constraint (OR_CLASSREP * rep, DB_SEQ * constraint_seq,
			     BTREE_TYPE type, const char *cons_name)
{
  int att_id;
  int i, max, args;
  int volid, fileid, pageid;
  BTID id;
  DB_VALUE id_val, att_val;

  /*  Extract the first element of the sequence which is the
     encoded B-tree ID */
  max = set_size (constraint_seq);	/* {btid, [attrID, asc_desc]+, {fk_info}} */

  if (set_get_element_nocopy (constraint_seq, 0, &id_val) != NO_ERROR)
    {
      return;
    }

  if (DB_VALUE_TYPE (&id_val) != DB_TYPE_STRING
      || DB_GET_STRING (&id_val) == NULL)
    {
      return;
    }

  args = classobj_decompose_property_oid (DB_GET_STRING (&id_val),
					  &volid, &fileid, &pageid);

  if (args != 3)
    {
      return;
    }

  /*
   *  Assign the B-tree ID.
   *  For the first attribute name in the constraint,
   *    cache the constraint in the attribute.
   */
  id.vfid.volid = (VOLID) volid;
  id.root_pageid = (PAGEID) pageid;
  id.vfid.fileid = (FILEID) fileid;

  i = 1;
  if (set_get_element_nocopy (constraint_seq, i, &att_val) == NO_ERROR)
    {
      att_id = DB_GET_INTEGER (&att_val);	/* The first attrID */
      (void) or_install_btids_attribute (rep, att_id, &id);
    }

  /*
   *  Assign the B-tree ID to the class.
   *  Cache the constraint in the class with pointer to the attributes.
   *  This is just a different way to store the BTID's.
   */
  or_install_btids_class (rep, &id, constraint_seq, max, type, cons_name);
}

/*
 * or_install_btids () - Install the constraints found on the property list
 *                       into the class and attribute structures
 *   return: void
 *   rep(in): Class representation
 *   props(in): Class property list
 */
static void
or_install_btids (OR_CLASSREP * rep, DB_SET * props)
{
  OR_BTREE_PROPERTY property_vars[] = {
    {BTREE_INDEX, SM_PROPERTY_INDEX, NULL, 0},
    {BTREE_UNIQUE, SM_PROPERTY_UNIQUE, NULL, 0},
    {BTREE_REVERSE_INDEX, SM_PROPERTY_REVERSE_INDEX, NULL, 0},
    {BTREE_REVERSE_UNIQUE, SM_PROPERTY_REVERSE_UNIQUE, NULL, 0},
    {BTREE_PRIMARY_KEY, SM_PROPERTY_PRIMARY_KEY, NULL, 0},
    {BTREE_FOREIGN_KEY, SM_PROPERTY_FOREIGN_KEY, NULL, 0}
  };

  DB_VALUE vals[DIM (property_vars)];
  int i;
  int n_btids;

  /*
   *  The first thing to do is to determine how many unique and index
   *  BTIDs we have.  We need this up front so that we can allocate
   *  the OR_INDEX structure in the class (rep).
   */
  n_btids = 0;
  for (i = 0; i < (int) DIM (property_vars); i++)
    {
      if (props != NULL
	  && or_cl_get_prop_nocopy (props, property_vars[i].name, &vals[i]))
	{
	  if (DB_VALUE_TYPE (&vals[i]) == DB_TYPE_SEQUENCE)
	    {
	      property_vars[i].seq = DB_GET_SEQUENCE (&vals[i]);
	    }

	  if (property_vars[i].seq)
	    {
	      property_vars[i].length = set_size (property_vars[i].seq);
	      n_btids += property_vars[i].length;
	    }
	}
    }

  n_btids /= 2;

  if (n_btids > 0)
    {
      rep->indexes = (OR_INDEX *) malloc (sizeof (OR_INDEX) * n_btids);
    }

  /* Now extract the unique and index BTIDs from the property list and
     install them into the class and attribute structures. */
  for (i = 0; i < (int) DIM (property_vars); i++)
    {
      if (property_vars[i].seq)
	{
	  int j;
	  DB_VALUE ids_val, cons_name_val;
	  DB_SEQ *ids_seq;
	  const char *cons_name = NULL;
	  int error = NO_ERROR;

	  for (j = 0; j < property_vars[i].length && error == NO_ERROR;
	       j += 2)
	    {
	      error = set_get_element_nocopy (property_vars[i].seq, j,
					      &cons_name_val);
	      if (error == NO_ERROR)
		{
		  cons_name = DB_GET_STRING (&cons_name_val);
		}

	      error = set_get_element_nocopy (property_vars[i].seq, j + 1,
					      &ids_val);
	      if (error == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&ids_val) == DB_TYPE_SEQUENCE)
		    {
		      ids_seq = DB_GET_SEQUENCE (&ids_val);
		      or_install_btids_constraint (rep, ids_seq,
						   property_vars[i].type,
						   cons_name);
		    }
		}
	    }
	}
    }
}

/*
 * or_get_current_representation () - build an OR_CLASSREP structure for the
 *                                    most recent representation
 *   return: disk representation structure
 *   record(in): disk record
 *   do_indexes(in):
 *
 * Note: This is similar to the old function orc_diskrep_from_record, but is
 *       a little simpler now that we don't need to maintain a separate
 *       list for the fixed and variable length attributes.
 *
 *       The logic is different from the logic in get_old_representation
 *       because the structures used to hold the most recent representation
 *       are different than the simplified structures used to hold the old
 *       representations.
 */
static OR_CLASSREP *
or_get_current_representation (RECDES * record, int do_indexes)
{
  OR_CLASSREP *rep;
  OR_ATTRIBUTE *att;
  OID oid;
  char *start, *ptr, *attset, *diskatt, *valptr, *dptr;
  int i, start_offset, offset, vallen, n_fixed, n_variable;
  int n_shared_attrs, n_class_attrs;

  rep = (OR_CLASSREP *) malloc (sizeof (OR_CLASSREP));
  if (rep == NULL)
    {
      return (NULL);
    }

  start = record->data;
  ptr = start + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);

  rep->id = OR_GET_INT (ptr + ORC_REPID_OFFSET);
  rep->fixed_length = OR_GET_INT (ptr + ORC_FIXED_LENGTH_OFFSET);
  rep->attributes = NULL;
  rep->shared_attrs = NULL;
  rep->class_attrs = NULL;
  rep->indexes = NULL;

  n_fixed = OR_GET_INT (ptr + ORC_FIXED_COUNT_OFFSET);
  n_variable = OR_GET_INT (ptr + ORC_VARIABLE_COUNT_OFFSET);
  n_shared_attrs = OR_GET_INT (ptr + ORC_SHARED_COUNT_OFFSET);
  n_class_attrs = OR_GET_INT (ptr + ORC_CLASS_ATTR_COUNT_OFFSET);
  rep->n_attributes = n_fixed + n_variable;
  rep->n_variable = n_variable;
  rep->n_shared_attrs = n_shared_attrs;
  rep->n_class_attrs = n_class_attrs;
  rep->n_indexes = 0;

  if (rep->n_attributes)
    {
      rep->attributes = (OR_ATTRIBUTE *) malloc (sizeof (OR_ATTRIBUTE) *
						 rep->n_attributes);
      if (rep->attributes == NULL)
	{
	  goto error_cleanup;
	}
    }

  if (rep->n_shared_attrs)
    {
      rep->shared_attrs = (OR_ATTRIBUTE *) malloc (sizeof (OR_ATTRIBUTE) *
						   rep->n_shared_attrs);
      if (rep->shared_attrs == NULL)
	{
	  goto error_cleanup;
	}
    }

  if (rep->n_class_attrs)
    {
      rep->class_attrs = (OR_ATTRIBUTE *) malloc (sizeof (OR_ATTRIBUTE) *
						  rep->n_class_attrs);
      if (rep->class_attrs == NULL)
	{
	  goto error_cleanup;
	}
    }

  /* find the beginning of the "set_of(attribute)" attribute inside the class */
  attset = start + OR_VAR_OFFSET (start, ORC_ATTRIBUTES_INDEX);

  /* calculate the offset to the first fixed width attribute in instances
     of this class. */
  start_offset = offset = OR_FIXED_ATTRIBUTES_OFFSET (n_variable);

  for (i = 0, att = rep->attributes; i < rep->n_attributes; i++, att++)
    {
      /* diskatt will now be pointing at the offset table for this attribute.
         this is logically the "start" of this nested object. */
      diskatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);

      /* find out where the original default value is kept */
      valptr = (diskatt +
		OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
					     ORC_ATT_ORIGINAL_VALUE_INDEX));

      vallen =
	OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_ORIGINAL_VALUE_INDEX);

      /* set ptr to the beginning of the fixed attributes */
      ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);

      if (OR_GET_INT (ptr + ORC_ATT_FLAG_OFFSET) & SM_ATTFLAG_AUTO_INCREMENT)
	{
	  att->is_autoincrement = 1;
	}
      else
	{
	  att->is_autoincrement = 0;
	}

      att->type = (DB_TYPE) OR_GET_INT (ptr + ORC_ATT_TYPE_OFFSET);
      att->id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      att->position = i;
      att->val_length = 0;
      att->value = NULL;
      OR_GET_OID (ptr + ORC_ATT_CLASS_OFFSET, &oid);
      att->classoid = oid;

      OID_SET_NULL (&(att->serial_obj));

      /* get the btree index id if an index has been assigned */
      or_get_att_index (ptr + ORC_ATT_INDEX_OFFSET, &att->index);

      /* We won't know if there are any B-tree ID's for unique constraints
         until we read the class property list later on */
      att->n_btids = 0;
      att->btids = NULL;

      /* Extract the full domain for this attribute, think about caching here
         it will add some time that may not be necessary. */
      if (OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_DOMAIN_INDEX) == 0)
	{
	  /* shouldn't happen, fake one up from the type ! */
	  att->domain = tp_Domains[att->type];
	}
      else
	{
	  dptr =
	    diskatt + OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						   ORC_ATT_DOMAIN_INDEX);
	  att->domain = or_get_domain_and_cache (dptr);
	}

      if (i < n_fixed)
	{
	  att->is_fixed = 1;
	  att->location = offset;
	  offset += tp_domain_disk_size (att->domain);
	}
      else
	{
	  att->is_fixed = 0;
	  att->location = i - n_fixed;
	}

      /* get the default value, this could be using a new DB_VALUE ? */
      if (vallen)
	{
	  if (or_get_default_value (att, valptr, vallen) == 0)
	    {
	      goto error_cleanup;
	    }
	}
    }

  /* find the beginning of the "set_of(shared attributes)" attribute
     inside the class */
  attset = start + OR_VAR_OFFSET (start, ORC_SHARED_ATTRS_INDEX);

  for (i = 0, att = rep->shared_attrs; i < rep->n_shared_attrs; i++, att++)
    {
      /* diskatt will now be pointing at the offset table for this attribute.
         this is logically the "start" of this nested object. */
      diskatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);

      /* find out where the current default value is kept */
      valptr = (diskatt +
		OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
					     ORC_ATT_CURRENT_VALUE_INDEX));

      vallen =
	OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_CURRENT_VALUE_INDEX);

      /* set ptr to the beginning of the fixed attributes */
      ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);

      att->type = (DB_TYPE) OR_GET_INT (ptr + ORC_ATT_TYPE_OFFSET);
      att->id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      att->position = i;
      att->val_length = 0;
      att->value = NULL;
      OR_GET_OID (ptr + ORC_ATT_CLASS_OFFSET, &oid);
      att->classoid = oid;	/* structure copy */

      /* get the btree index id if an index has been assigned */
      or_get_att_index (ptr + ORC_ATT_INDEX_OFFSET, &att->index);

      /* there won't be any indexes or uniques for shared attrs */
      att->n_btids = 0;
      att->btids = NULL;

      /* Extract the full domain for this attribute, think about caching here
         it will add some time that may not be necessary. */
      if (OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_DOMAIN_INDEX) == 0)
	{
	  /* shouldn't happen, fake one up from the type ! */
	  att->domain = tp_Domains[att->type];
	}
      else
	{
	  dptr =
	    diskatt + OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						   ORC_ATT_DOMAIN_INDEX);
	  att->domain = or_get_domain_and_cache (dptr);
	}

      att->is_fixed = 0;
      att->location = 0;

      /* get the default value, it is the container for the shared value */
      if (vallen)
	{
	  if (or_get_default_value (att, valptr, vallen) == 0)
	    {
	      goto error_cleanup;
	    }
	}
    }

  /* find the beginning of the "set_of(class_attrs)" attribute
     inside the class */
  attset = start + OR_VAR_OFFSET (start, ORC_CLASS_ATTRS_INDEX);

  for (i = 0, att = rep->class_attrs; i < rep->n_class_attrs; i++, att++)
    {
      /* diskatt will now be pointing at the offset table for this attribute.
         this is logically the "start" of this nested object. */
      diskatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);

      /* find out where the current default value is kept */
      valptr = (diskatt +
		OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
					     ORC_ATT_CURRENT_VALUE_INDEX));

      vallen =
	OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_CURRENT_VALUE_INDEX);

      /* set ptr to the beginning of the fixed attributes */
      ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);

      att->type = (DB_TYPE) OR_GET_INT (ptr + ORC_ATT_TYPE_OFFSET);
      att->id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      att->position = i;
      att->val_length = 0;
      att->value = NULL;
      OR_GET_OID (ptr + ORC_ATT_CLASS_OFFSET, &oid);
      att->classoid = oid;

      /* get the btree index id if an index has been assigned */
      or_get_att_index (ptr + ORC_ATT_INDEX_OFFSET, &att->index);

      /* there won't be any indexes or uniques for shared attrs */
      att->n_btids = 0;
      att->btids = NULL;

      /* Extract the full domain for this attribute, think about caching here
         it will add some time that may not be necessary. */
      if (OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_DOMAIN_INDEX) == 0)
	{
	  /* shouldn't happen, fake one up from the type ! */
	  att->domain = tp_Domains[att->type];
	}
      else
	{
	  dptr =
	    diskatt + OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						   ORC_ATT_DOMAIN_INDEX);
	  att->domain = or_get_domain_and_cache (dptr);
	}

      att->is_fixed = 0;
      att->location = 0;

      /* get the default value, it is the container for the class attr value */
      if (vallen)
	{
	  if (or_get_default_value (att, valptr, vallen) == 0)
	    {
	      goto error_cleanup;
	    }
	}
    }

  /* Read the B-tree IDs from the class property list */
  if (do_indexes)
    {
      char *propptr;
      DB_SET *props;

      if (!OR_VAR_IS_NULL (record->data, ORC_PROPERTIES_INDEX))
	{
	  propptr = record->data + OR_VAR_OFFSET (record->data,
						  ORC_PROPERTIES_INDEX);
	  (void) or_unpack_setref (propptr, &props);
	  or_install_btids (rep, props);
	  db_set_free (props);
	}
      rep->needs_indexes = 0;
    }
  else
    {
      rep->needs_indexes = 1;
    }

  return rep;

error_cleanup:

  if (rep->attributes)
    {
      free_and_init (rep->attributes);
    }

  if (rep->shared_attrs)
    {
      free_and_init (rep->shared_attrs);
    }

  if (rep->class_attrs)
    {
      free_and_init (rep->class_attrs);
    }

  return NULL;
}

/*
 * or_get_old_representation () - Extracts the description of an old
 *                                representation from the disk image of a
 *                                class
 *   return:
 *   record(in): record with class diskrep
 *   repid(in): representation id to extract
 *   do_indexes(in):
 *
 * Note: It is similar to get_current_representation
 *       except that it must get its information out of the compressed
 *       SM_REPRESENTATION & SM_REPR_ATTRIBUTE structures which are used for
 *       storing the old representations.  The current representation is stored
 *       in top-level SM_ATTRIBUTE structures which are much larger.
 *
 *       If repid is -1 here, it returns the current representation.
 *       It returns NULL on error.  This can happen during memory allocation
 *       failure but is more likely to happen if the repid given was not
 *       found within the class.
 */
static OR_CLASSREP *
or_get_old_representation (RECDES * record, int repid, int do_indexes)
{
  OR_CLASSREP *rep;
  OR_ATTRIBUTE *att;
  char *repset, *disk_rep, *attset, *repatt, *dptr;
  int rep_count, i, n_fixed, n_variable, offset, start, id;
  char *fixed = NULL;

  if (repid == -1)
    {
      return or_get_current_representation (record, do_indexes);
    }

  /* find the beginning of the "set_of(representation)" attribute inside
   * the class.
   * If this attribute is NULL, we're missing the representations, its an
   * error.
   */
  if (OR_VAR_IS_NULL (record->data, ORC_REPRESENTATIONS_INDEX))
    {
      return NULL;
    }

  repset = (record->data +
	    OR_VAR_OFFSET (record->data, ORC_REPRESENTATIONS_INDEX));

  /* repset now points to the beginning of a complex set representation,
     find out how many elements are in the set. */
  rep_count = OR_SET_ELEMENT_COUNT (repset);

  /* locate the beginning of the representation in this set whose id matches
     the given repid. */
  disk_rep = NULL;
  for (i = 0; i < rep_count; i++)
    {
      /* set disk_rep to the beginning of the i'th set element */
      disk_rep = repset + OR_SET_ELEMENT_OFFSET (repset, i);

      /* move ptr up to the beginning of the fixed width attributes in this
         object */
      fixed = disk_rep + OR_VAR_TABLE_SIZE (ORC_REP_VAR_ATT_COUNT);

      /* extract the id of this representation */
      id = OR_GET_INT (fixed + ORC_REP_ID_OFFSET);

      if (id == repid)
	{
	  break;
	}
      else
	{
	  disk_rep = NULL;
	}
    }

  if (disk_rep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_CT_UNKNOWN_REPRID, 1,
	      repid);
      return NULL;
    }

  /* allocate a new memory structure for this representation */
  rep = (OR_CLASSREP *) malloc (sizeof (OR_CLASSREP));
  if (rep == NULL)
    {
      return NULL;
    }

  rep->attributes = NULL;
  rep->shared_attrs = NULL;
  rep->class_attrs = NULL;
  rep->indexes = NULL;

  /* at this point, disk_rep points to the beginning of the representation
     object and "fixed" points at the first fixed width attribute. */

  n_fixed = OR_GET_INT (fixed + ORC_REP_FIXED_COUNT_OFFSET);
  n_variable = OR_GET_INT (fixed + ORC_REP_VARIABLE_COUNT_OFFSET);

  rep->id = repid;
  rep->fixed_length = 0;
  rep->n_attributes = n_fixed + n_variable;
  rep->n_variable = n_variable;
  rep->n_indexes = 0;

  if (!rep->n_attributes)
    {
      /* its an empty representation, return it */
      return rep;
    }

  rep->attributes =
    (OR_ATTRIBUTE *) malloc (sizeof (OR_ATTRIBUTE) * rep->n_attributes);
  if (rep->attributes == NULL)
    {
      free_and_init (rep);
      return NULL;
    }

  /* Calculate the beginning of the set_of(rep_attribute) in the representation
   * object. Assume that the start of the disk_rep points directly at the the
   * substructure's variable offset table (which it does) and use
   * OR_VAR_TABLE_ELEMENT_OFFSET.
   */
  attset =
    disk_rep + OR_VAR_TABLE_ELEMENT_OFFSET (disk_rep,
					    ORC_REP_ATTRIBUTES_INDEX);

  /* Calculate the offset to the first fixed width attribute in instances
   * of this class.  Save the start of this region so we can calculate the
   * total fixed witdh size.
   */
  start = offset = OR_FIXED_ATTRIBUTES_OFFSET (n_variable);

  /* build up the attribute descriptions */
  for (i = 0, att = rep->attributes; i < rep->n_attributes; i++, att++)
    {
      /* set repatt to the beginning of the rep_attribute object in the set */
      repatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);

      /* set fixed to the beginning of the fixed width attributes for this
         object */
      fixed = repatt + OR_VAR_TABLE_SIZE (ORC_REPATT_VAR_ATT_COUNT);

      att->id = OR_GET_INT (fixed + ORC_REPATT_ID_OFFSET);
      att->type = (DB_TYPE) OR_GET_INT (fixed + ORC_REPATT_TYPE_OFFSET);
      att->position = i;
      att->val_length = 0;
      att->value = NULL;

      /* We won't know if there are any B-tree ID's for unique constraints
         until we read the class property list later on */
      att->n_btids = 0;
      att->btids = NULL;

      /* not currently available, will this be a problem ? */
      OID_SET_NULL (&(att->classoid));
      BTID_SET_NULL (&(att->index));

      /* Extract the full domain for this attribute, think about caching here
         it will add some time that may not be necessary. */
      if (OR_VAR_TABLE_ELEMENT_LENGTH (repatt, ORC_REPATT_DOMAIN_INDEX) == 0)
	{
	  /* shouldn't happen, fake one up from the type ! */
	  att->domain = tp_Domains[att->type];
	}
      else
	{
	  dptr =
	    repatt + OR_VAR_TABLE_ELEMENT_OFFSET (repatt,
						  ORC_REPATT_DOMAIN_INDEX);
	  att->domain = or_get_domain_and_cache (dptr);
	}

      if (i < n_fixed)
	{
	  att->is_fixed = 1;
	  att->location = offset;
	  offset += tp_domain_disk_size (att->domain);
	}
      else
	{
	  att->is_fixed = 0;
	  att->location = i - n_fixed;
	}
    }

  /* Offset at this point contains the total fixed size of the
   * representation plus the starting offset, remove the starting offset
   * to get the length of just the fixed width attributes.
   */
  rep->fixed_length = offset - start;
  /* must align up to a word boundar ! */
  DB_ATT_ALIGN (rep->fixed_length);

  /* Read the B-tree IDs from the class property list */
  if (do_indexes)
    {
      char *propptr;
      DB_SET *props;

      if (!OR_VAR_IS_NULL (record->data, ORC_PROPERTIES_INDEX))
	{
	  propptr = record->data + OR_VAR_OFFSET (record->data,
						  ORC_PROPERTIES_INDEX);
	  (void) or_unpack_setref (propptr, &props);
	  or_install_btids (rep, props);
	  db_set_free (props);
	}
      rep->needs_indexes = 0;
    }
  else
    {
      rep->needs_indexes = 1;
    }

  return rep;
}

/*
 * or_get_classrep () - builds an in-memory OR_CLASSREP that describes the
 *                      class
 *   return: OR_CLASSREP structure
 *   record(in): disk record
 *   repid(in): representation of interest (-1) for current
 *
 * Note: This structure is in turn used to navigate over the instances of this
 *       class stored in the heap.
 *       It calls either get_current_representation or get_old_representation
 *       to do the work.
 */
OR_CLASSREP *
or_get_classrep (RECDES * record, int repid)
{
  OR_CLASSREP *rep;
  char *fixed;
  int current;

  if (repid == -1)
    {
      rep = or_get_current_representation (record, 1);
    }
  else
    {
      /* find out what the most recent representation is */
      fixed =
	record->data + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);
      current = OR_GET_INT (fixed + ORC_REPID_OFFSET);

      if (current == repid)
	{
	  rep = or_get_current_representation (record, 1);
	}
      else
	{
	  rep = or_get_old_representation (record, repid, 1);
	}
    }

  return rep;
}

/*
 * or_get_classrep_noindex () -
 *   return:
 *   record(in):
 *   repid(in):
 */
OR_CLASSREP *
or_get_classrep_noindex (RECDES * record, int repid)
{
  OR_CLASSREP *rep;
  char *fixed;
  int current;

  if (repid == -1)
    {
      rep = or_get_current_representation (record, 0);
    }
  else
    {
      /* find out what the most recent representation is */
      fixed =
	record->data + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);
      current = OR_GET_INT (fixed + ORC_REPID_OFFSET);

      if (current == repid)
	{
	  rep = or_get_current_representation (record, 0);
	}
      else
	{
	  rep = or_get_old_representation (record, repid, 0);
	}
    }
  return rep;
}

/*
 * or_classrep_load_indexes () -
 *   return:
 *   rep(in):
 *   record(in):
 */
OR_CLASSREP *
or_classrep_load_indexes (OR_CLASSREP * rep, RECDES * record)
{
  REPR_ID id;

  /* eventually could be smarter about trying to reuse the existing
     structure. */
  if (rep->needs_indexes)
    {
      id = rep->id;
      or_free_classrep (rep);
      rep = or_get_classrep (record, id);
    }

  return rep;
}

/*
 * or_classrep_needs_indexes () -
 *   return:
 *   rep(in):
 */
int
or_classrep_needs_indexes (OR_CLASSREP * rep)
{
  return rep->needs_indexes;
}

/*
 * or_free_classrep () - Frees an OR_CLASSREP structure returned by
 *                       or_get_classrep
 *   return: void
 *   rep(in): representation structure
 */
void
or_free_classrep (OR_CLASSREP * rep)
{
  int i;
  OR_ATTRIBUTE *att;
  OR_INDEX *index;
  OR_FOREIGN_KEY *fk, *fk_next;

  if (rep != NULL)
    {
      if (rep->attributes != NULL)
	{
	  for (i = 0, att = rep->attributes; i < rep->n_attributes;
	       i++, att++)
	    {
	      if (att->value != NULL)
		{
		  free_and_init (att->value);
		}

	      if (att->btids != NULL && att->btids != att->btid_pack)
		{
		  free_and_init (att->btids);
		}
	    }
	  free_and_init (rep->attributes);
	}

      if (rep->shared_attrs != NULL)
	{
	  for (i = 0, att = rep->shared_attrs;
	       i < rep->n_shared_attrs; i++, att++)
	    {
	      if (att->value != NULL)
		{
		  free_and_init (att->value);
		}

	      if (att->btids != NULL && att->btids != att->btid_pack)
		{
		  free_and_init (att->btids);
		}
	    }
	  free_and_init (rep->shared_attrs);
	}

      if (rep->class_attrs != NULL)
	{
	  for (i = 0, att = rep->class_attrs; i < rep->n_class_attrs;
	       i++, att++)
	    {
	      if (att->value != NULL)
		{
		  free_and_init (att->value);
		}

	      if (att->btids != NULL && att->btids != att->btid_pack)
		{
		  free_and_init (att->btids);
		}
	    }
	  free_and_init (rep->class_attrs);
	}

      if (rep->indexes != NULL)
	{
	  for (i = 0, index = rep->indexes; i < rep->n_indexes; i++, index++)
	    {
	      if (index->atts != NULL)
		{
		  free_and_init (index->atts);
		}

	      if (index->btname != NULL)
		{
		  free_and_init (index->btname);
		}

	      if (index->fk)
		{
		  for (fk = index->fk; fk; fk = fk_next)
		    {
		      fk_next = fk->next;
		      if (fk->fkname)
			{
			  free_and_init (fk->fkname);
			}
		      free_and_init (fk);
		    }
		}
	    }

	  free_and_init (rep->indexes);
	}

      free_and_init (rep);
    }
}

/*
 * or_get_attrname () - Find the name of the given attribute
 *   return: attr_name
 *   record(in): disk record
 *   attrid(in): desired attribute
 *
 * Note: The name returned is an actual pointer to the record structure
 *       if the record is changed, the pointer may be trashed.
 *
 *       The name retruned is the name of the actual representation.
 *       If the given attribute identifier does not exist for current
 *       representation, NULL is retruned.
 */
const char *
or_get_attrname (RECDES * record, int attrid)
{
  int n_fixed, n_variable, n_shared, n_class;
  int n_attrs;
  int type_attr, i, id;
  bool found;
  char *attr_name, *start, *ptr, *attset, *diskatt = NULL;

  start = record->data;
  ptr = start + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);
  attr_name = NULL;

  n_fixed = OR_GET_INT (ptr + ORC_FIXED_COUNT_OFFSET);
  n_variable = OR_GET_INT (ptr + ORC_VARIABLE_COUNT_OFFSET);
  n_shared = OR_GET_INT (ptr + ORC_SHARED_COUNT_OFFSET);
  n_class = OR_GET_INT (ptr + ORC_CLASS_ATTR_COUNT_OFFSET);

  for (type_attr = 0, found = false;
       type_attr < 3 && found == false; type_attr++)
    {
      if (type_attr == 0)
	{
	  /*
	   * INSTANCE ATTRIBUTES
	   *
	   * find the start of the "set_of(attribute)" fix/variable attribute
	   * list inside the class
	   */
	  attset = start + OR_VAR_OFFSET (start, ORC_ATTRIBUTES_INDEX);
	  n_attrs = n_fixed + n_variable;
	}
      else if (type_attr == 1)
	{
	  /*
	   * SHARED ATTRIBUTES
	   *
	   * find the start of the "set_of(shared attributes)" attribute
	   * list inside the class
	   */
	  attset = start + OR_VAR_OFFSET (start, ORC_SHARED_ATTRS_INDEX);
	  n_attrs = n_shared;
	}
      else
	{
	  /*
	   * CLASS ATTRIBUTES
	   *
	   * find the start of the "set_of(class attributes)" attribute
	   * list inside the class
	   */
	  attset = start + OR_VAR_OFFSET (start, ORC_CLASS_ATTRS_INDEX);
	  n_attrs = n_class;
	}

      for (i = 0, found = false; i < n_attrs && found == false; i++)
	{
	  /*
	   * diskatt will now be pointing at the offset table for this attribute.
	   * this is logically the "start" of this nested object.
	   *
	   * set ptr to the beginning of the fixed attributes
	   */
	  diskatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);
	  ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);
	  id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
	  if (id == attrid)
	    {
	      found = true;
	    }
	}

      /*
       * diskatt now points to the attribute that we are interested in.
       Get the attribute name. */
      if (found == true)
	{
	  attr_name = (diskatt +
		       OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						    ORC_ATT_NAME_INDEX));

	  /* kludge kludge kludge
	   * This is now an encoded "varchar" string, we need to skip over the
	   * length before returning it.  Note that this also depends on the
	   * stored string being NULL terminated.
	   */
	  if (*attr_name < 0xFF)
	    {
	      attr_name += 1;
	    }
	  else
	    {
	      attr_name = attr_name + 1 + OR_INT_SIZE;
	    }
	}
    }

  return attr_name;
}
