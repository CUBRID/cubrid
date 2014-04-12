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
#include "partition.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define DATA_INIT(data, type) memset(data, 0, sizeof(DB_DATA))
#define OR_ARRAY_EXTENT 10

/*
 * VARIABLE OFFSET TABLE ACCESSORS
 * The variable offset table is present in the headers of objects and sets.
 */

#define OR_VAR_TABLE_ELEMENT_OFFSET(table, index) 			\
            OR_VAR_TABLE_ELEMENT_OFFSET_INTERNAL(table, index, 		\
                                                 BIG_VAR_OFFSET_SIZE)

#define OR_VAR_TABLE_ELEMENT_LENGTH(table, index) 			\
            OR_VAR_TABLE_ELEMENT_LENGTH_INTERNAL(table, index, 		\
                                                 BIG_VAR_OFFSET_SIZE)

/* ATTRIBUTE LOCATION */

#define OR_FIXED_ATTRIBUTES_OFFSET(nvars) \
   (OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL(nvars, BIG_VAR_OFFSET_SIZE))

#define OR_FIXED_ATTRIBUTES_OFFSET_INTERNAL(nvars, offset_size) \
   (OR_HEADER_SIZE + OR_VAR_TABLE_SIZE_INTERNAL (nvars, offset_size))


typedef struct or_btree_property OR_BTREE_PROPERTY;
struct or_btree_property
{
  const char *name;
  DB_SEQ *seq;
  BTREE_TYPE type;
  int length;
};

static int or_get_hierarchy_helper (THREAD_ENTRY * thread_p,
				    OID * source_class, OID * class_,
				    BTID * btid, OID ** class_oids,
				    HFID ** hfids, int *num_classes,
				    int *max_classes,
				    int *partition_local_index);
static TP_DOMAIN *or_get_domain_internal (char *ptr);
static TP_DOMAIN *or_get_domain_and_cache (char *ptr);
static void or_get_att_index (char *ptr, BTID * btid);
static int or_get_default_value (OR_ATTRIBUTE * attr, char *ptr, int length);
static int or_get_current_default_value (OR_ATTRIBUTE * attr, char *ptr,
					 int length);
static int or_cl_get_prop_nocopy (DB_SEQ * properties, const char *name,
				  DB_VALUE * pvalue);
static void or_install_btids_foreign_key (const char *fkname, DB_SEQ * fk_seq,
					  OR_INDEX * index);
static void or_install_btids_foreign_key_ref (DB_SEQ * fk_container,
					      OR_INDEX * index);
static void or_install_btids_prefix_length (DB_SEQ * prefix_seq,
					    OR_INDEX * index, int num_attrs);
static int or_install_btids_filter_pred (DB_SEQ * pred_seq, OR_INDEX * index);
static void or_install_btids_class (OR_CLASSREP * rep, BTID * id,
				    DB_SEQ * constraint_seq, int seq_size,
				    BTREE_TYPE type, const char *cons_name);
static int or_install_btids_attribute (OR_CLASSREP * rep, int att_id,
				       BTID * id);
static void or_install_btids_constraint (OR_CLASSREP * rep,
					 DB_SEQ * constraint_seq,
					 BTREE_TYPE type,
					 const char *cons_name);
static void or_install_btids_function_info (DB_SEQ * fi_seq,
					    OR_INDEX * index);
static void or_install_btids (OR_CLASSREP * rep, DB_SEQ * props);
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
  OR_INDEX *or_idx = NULL;

  VPID root_vpid;
  PAGE_PTR root;
  RECDES rec;
  BTREE_ROOT_HEADER *root_header = NULL;
  BTID_INT btid_int;

  or_rep = or_get_classrep (record, NULL_REPRID);
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
#if 0				/* reserved for future use */
  rep->repr_reserved_1 = 0;
#endif
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

      if (att == NULL)
	{
	  goto error;
	}

      att->type = or_att->type;
      att->id = or_att->id;
      att->location = or_att->location;
      att->position = or_att->position;
      att->val_length = or_att->default_value.val_length;
      att->value = or_att->default_value.value;
      att->default_expr = or_att->default_value.default_expr;
      or_att->default_value.value = NULL;
      att->classoid = or_att->classoid;

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
	      bt_statsp->has_function = 0;
	      for (k = 0; k < or_rep->n_indexes; k++)
		{
		  or_idx = &or_rep->indexes[k];
		  if (or_idx &&
		      BTID_IS_EQUAL (&or_idx->btid, &bt_statsp->btid) &&
		      or_idx->func_index_info &&
		      or_idx->func_index_info->col_id == 0)
		    {
		      bt_statsp->has_function = 1;
		      break;
		    }
		}

	      bt_statsp->key_type = NULL;
	      bt_statsp->pkeys_size = 0;
	      bt_statsp->pkeys = NULL;

#if 0				/* reserved for future use */
	      for (k = 0; k < BTREE_STATS_RESERVED_NUM; k++)
		{
		  bt_statsp->reserved[k] = 0;
		}
#endif

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

	      (void) pgbuf_check_page_ptype (thread_p, root, PAGE_BTREE);

	      root_header = btree_get_root_header (root);
	      if (root_header == NULL)
		{
		  pgbuf_unfix_and_init (thread_p, root);
		  continue;
		}

	      /* construct BTID_INT structure */
	      btid_int.sys_btid = &bt_statsp->btid;
	      if (btree_glean_root_header_info (thread_p,
						root_header,
						&btid_int) != NO_ERROR)
		{
		  pgbuf_unfix_and_init (thread_p, root);
		  continue;
		}

	      pgbuf_unfix_and_init (thread_p, root);

	      bt_statsp->key_type = btid_int.key_type;
	      if (TP_DOMAIN_TYPE (bt_statsp->key_type) == DB_TYPE_MIDXKEY)
		{
		  bt_statsp->pkeys_size =
		    tp_domain_size (bt_statsp->key_type->setdomain);
		}
	      else
		{
		  bt_statsp->pkeys_size = 1;
		}

	      /* cut-off to stats */
	      if (bt_statsp->pkeys_size > BTREE_STATS_PKEYS_NUM)
		{
		  bt_statsp->pkeys_size = BTREE_STATS_PKEYS_NUM;
		}

	      bt_statsp->pkeys =
		(int *) malloc (bt_statsp->pkeys_size * sizeof (int));
	      if (bt_statsp->pkeys == NULL)
		{
		  bt_statsp->pkeys_size = 0;
		  continue;
		}

	      assert (bt_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	      for (k = 0; k < bt_statsp->pkeys_size; k++)
		{
		  bt_statsp->pkeys[k] = 0;
		}
	    }			/* for (j = 0, ...) */
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
  if (info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (CLS_INFO));
      return NULL;
    }

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
      if (array == NULL || (insert + nsubs + 1) > max)
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
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_OUT_OF_VIRTUAL_MEMORY, 1, newsize * sizeof (OID));
	      return ER_OUT_OF_VIRTUAL_MEMORY;
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

      assert (array != NULL);

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
 * orc_superclasses_from_record () - Extracts the OID's of the immediate
 *				     superclasses
 *   return: error code
 *   record(in): record containing a class
 *   array_size(out): pointer to int containing max size of array
 *   array_ptr(out): pointer to OID array
 *
 * Note: The array is maintained as an array of OID's, the last element in the
 *       array will satisfy the OID_ISNULL() test.  The array_size has
 *       the number of actual elements allocated in the array which may be
 *	 more than the number of slots that have non-NULL OIDs.
 *       The function adds the subclass oids to the existing array.  If the
 *       array is not large enough, it is reallocated using realloc.
 */
int
orc_superclasses_from_record (RECDES * record, int *array_size,
			      OID ** array_ptr)
{
  int error = NO_ERROR;
  OID *oid_array = NULL;
  char *ptr = NULL;
  int nsupers = 0, i = 0;
  char *superset = NULL;

  assert (array_ptr != NULL);
  assert (*array_ptr == NULL);
  assert (array_size != NULL);

  nsupers = 0;
  if (OR_VAR_IS_NULL (record->data, ORC_SUPERCLASSES_INDEX))
    {
      /* no superclasses, just return */
      return NO_ERROR;
    }

  superset =
    (char *) (record->data) + OR_VAR_OFFSET (record->data,
					     ORC_SUPERCLASSES_INDEX);
  nsupers = OR_SET_ELEMENT_COUNT (superset);
  if (nsupers <= 0)
    {
      /* This is probably an error but there's no point in reporting it here.
       * We just assume that there are no supers */
      assert (false);
      return NO_ERROR;
    }

  oid_array = (OID *) malloc (nsupers * sizeof (OID));

  if (oid_array == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      nsupers * sizeof (OID));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  /* Advance past the set header, the domain size, and the "object" domain.
   * Note that this assumes we are not using a bound bit array even though
   * this is a fixed width homogeneous set.  Probably not a good assumption.
   */
  ptr = superset + OR_SET_HEADER_SIZE + OR_INT_SIZE + OR_INT_SIZE;

  /* add the new OIDs */
  for (i = 0; i < nsupers; i++)
    {
      OR_GET_OID (ptr, &oid_array[i]);
      ptr += OR_OID_SIZE;
    }

  /* return these in case there were changes */
  *array_size = nsupers;
  *array_ptr = oid_array;

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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

  ptr = record->data + OR_FIXED_ATTRIBUTES_OFFSET (ORC_CLASS_VAR_ATT_COUNT);
  hfid->vfid.fileid = OR_GET_INT (ptr + ORC_HFID_FILEID_OFFSET);
  hfid->vfid.volid = OR_GET_INT (ptr + ORC_HFID_VOLID_OFFSET);
  hfid->hpgid = OR_GET_INT (ptr + ORC_HFID_PAGEID_OFFSET);
}

#if defined (ENABLE_UNUSED_FUNCTION)
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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

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

      if (array != NULL)
	{
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
      else
	{
	  assert (false);
	}
    }

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

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
 *   partition_local_index(out):
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
			 HFID ** hfids, int *num_classes, int *max_classes,
			 int *partition_local_index)
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

  or_rep = or_get_classrep (&record, NULL_REPRID);
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
      OID partition_info;
      REPR_ID repr_id;
      int is_global_index = 0;

      /* check if we are dealing with a partition class in which the unique
       * constraint stands as a local index and each partition has it's own btree
       */
      if (or_class_get_partition_info (&record, &partition_info, &repr_id)
	  != NO_ERROR)
	{
	  goto error;
	}
      if (!OID_ISNULL (&partition_info) && partition_local_index != NULL)
	{
	  if (partition_is_global_index (thread_p, NULL, source_class, btid,
					 NULL, &is_global_index) != NO_ERROR)
	    {
	      if (OID_EQ (class_, source_class))
		{
		  /* it has been attempted to check the hierarchy for a partition
		   * class and its local btree. It was not found in the source
		   * class and is surely not an inherited one.
		   */
		  er_clear ();
		  *partition_local_index = 1;
		  goto end;
		}
	      else
		{
		  goto error;
		}
	    }
	  else
	    {
	      if (is_global_index == 1)
		{
		  /* this should not happen, since the btid has been searched and
		   * was not found, the index must have been a local one.
		   */
		  goto error;
		}
	      *partition_local_index = 1;
	    }
	}
      else
	{
	  goto success;
	}
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
	  if (or_get_hierarchy_helper (thread_p, source_class, &sub_class,
				       btid, class_oids, hfids, num_classes,
				       max_classes, partition_local_index)
	      != NO_ERROR)
	    {
	      goto error;
	    }

	  ptr += OR_OID_SIZE;
	}
    }

end:

  /* If we have a valid HFID, then add this class to the array */
  or_class_hfid (&record, &hfid);
  if (HFID_IS_NULL (&hfid))
    {
      goto success;
    }

  /* Need to remove duplicates from a multiple inheritance hierarchy */
  for (i = 0; i < *num_classes; i++)
    {
      if (*class_oids != NULL && OID_EQ (class_, &((*class_oids)[i])))
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

      if (*class_oids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, newsize * sizeof (OID));
	  goto error;
	}

      if (*hfids == NULL)
	{
	  *hfids = (HFID *) malloc (newsize * sizeof (HFID));
	}
      else
	{
	  *hfids = (HFID *) realloc (*hfids, newsize * sizeof (HFID));
	}

      if (*hfids == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, newsize * sizeof (HFID));
	  goto error;
	}

      *max_classes = newsize;
    }

  if (*class_oids == NULL || *hfids == NULL)
    {
      goto error;
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
 *   partition_local_index(out):
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
			 int *num_classes, int *partition_local_index)
{
  int n_attributes, n_fixed, n_variable, i;
  int id, found, max_classes;
  char *attr_name, *start, *ptr, *attset, *diskatt = NULL;
  OID source_class;

  *num_classes = 0;
  max_classes = 0;
  *class_oids = NULL;
  *hfids = NULL;

  if (partition_local_index != NULL)
    {
      *partition_local_index = 0;
    }

  /* find the source class of the attribute from the record */
  start = record->data;

  assert (OR_GET_OFFSET_SIZE (start) == BIG_VAR_OFFSET_SIZE);

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
  if (diskatt == NULL)
    {
      goto error;
    }

  attr_name = (diskatt + OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						      ORC_ATT_NAME_INDEX));

  if (!found
      ||
      (OR_VAR_TABLE_ELEMENT_LENGTH (diskatt, ORC_ATT_NAME_INDEX) == 0)
      ||
      (or_get_hierarchy_helper (thread_p, &source_class, &source_class,
				btid, class_oids, hfids, num_classes,
				&max_classes, partition_local_index)
       != NO_ERROR))
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
  int n_domains, offset, i, error = NO_ERROR;
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
	  while (domain != NULL)
	    {
	      TP_DOMAIN *next = domain->next;
	      tp_domain_free (domain);
	      domain = next;
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
      if (typeid_ == DB_TYPE_ENUMERATION && new_->codeset == 0)
	{
	  assert (new_->collation_id == LANG_COLL_ISO_BINARY);
	  new_->codeset = INTL_CODESET_ISO88591;
	}
      new_->collation_id = OR_GET_INT (fixed +
				       ORC_DOMAIN_COLLATION_ID_OFFSET);

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

      DOM_SET_ENUM (new_, NULL, 0);
      if (OR_VAR_TABLE_ELEMENT_LENGTH (dstart, ORC_DOMAIN_ENUMERATION_INDEX)
	  != 0)
	{
	  OR_BUF buf;

	  offset =
	    OR_VAR_TABLE_ELEMENT_OFFSET (dstart,
					 ORC_DOMAIN_ENUMERATION_INDEX);

	  or_init (&buf, dstart + offset, 0);

	  new_->enumeration.collation_id = new_->collation_id;

	  error = or_get_enumeration (&buf, &DOM_GET_ENUMERATION (new_));
	  if (error != NO_ERROR)
	    {
	      while (domain != NULL)
		{
		  TP_DOMAIN *next = domain->next;
		  tp_domain_free (domain);
		  domain = next;
		}
	      return NULL;
	    }
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
      attr->default_value.val_length = length;
      attr->default_value.value = malloc (length);
      if (attr->default_value.value != NULL)
	{
	  memcpy (attr->default_value.value, vptr, length);
	  success = 1;
	}
    }

  return success;
}

/*
 * or_get_current_default_value () - Copies the current default value of an
 *				    attribute from disk
 *   return: zero to indicate error
 *   attr(in): disk attribute structure
 *   ptr(in): pointer to beginning of value
 *   length(in): length of value on disk
 */
static int
or_get_current_default_value (OR_ATTRIBUTE * attr, char *ptr, int length)
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
      attr->current_default_value.val_length = length;
      attr->current_default_value.value = malloc (length);
      if (attr->current_default_value.value != NULL)
	{
	  memcpy (attr->current_default_value.value, vptr, length);
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
		  prop_name = DB_PULL_STRING (&value);
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

      args = classobj_decompose_property_oid (DB_PULL_STRING (&val),
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
  OR_FOREIGN_KEY *fk, *p = NULL;
  char *fkname;

  size = set_size (fk_container);

  for (i = 0; i < size; i++)
    {
      if (set_get_element_nocopy (fk_container, i, &fkval) != NO_ERROR)
	{
	  return;
	}

      fk_seq = DB_PULL_SEQUENCE (&fkval);

      fk = (OR_FOREIGN_KEY *) malloc (sizeof (OR_FOREIGN_KEY));
      if (fk != NULL)
	{
	  fk->next = NULL;

	  if (set_get_element_nocopy (fk_seq, 0, &val) != NO_ERROR)
	    {
	      free_and_init (fk);
	      return;
	    }

	  args = classobj_decompose_property_oid (DB_PULL_STRING (&val),
						  &pageid, &slotid, &volid);

	  if (args != 3)
	    {
	      free_and_init (fk);
	      return;
	    }

	  fk->self_oid.pageid = (PAGEID) pageid;
	  fk->self_oid.slotid = (PGSLOTID) slotid;
	  fk->self_oid.volid = (VOLID) volid;

	  if (set_get_element_nocopy (fk_seq, 1, &val) != NO_ERROR)
	    {
	      free_and_init (fk);
	      return;
	    }

	  args = classobj_decompose_property_oid (DB_PULL_STRING (&val),
						  &volid, &fileid, &pageid);

	  if (args != 3)
	    {
	      free_and_init (fk);
	      return;
	    }

	  fk->self_btid.vfid.volid = (VOLID) volid;
	  fk->self_btid.root_pageid = (PAGEID) pageid;
	  fk->self_btid.vfid.fileid = (FILEID) fileid;

	  if (set_get_element_nocopy (fk_seq, 2, &val) != NO_ERROR)
	    {
	      free_and_init (fk);
	      return;
	    }
	  fk->del_action = DB_GET_INT (&val);

	  if (set_get_element_nocopy (fk_seq, 3, &val) != NO_ERROR)
	    {
	      free_and_init (fk);
	      return;
	    }
	  fk->upd_action = DB_GET_INT (&val);

	  if (set_get_element_nocopy (fk_seq, 4, &val) != NO_ERROR)
	    {
	      free_and_init (fk);
	      return;
	    }
	  fkname = DB_PULL_STRING (&val);
	  fk->fkname = strdup (fkname);

	  if (set_get_element_nocopy (fk_seq, 5, &val) != NO_ERROR)
	    {
	      free_and_init (fk->fkname);
	      free_and_init (fk);
	      return;
	    }
	  fk->cache_attr_id = DB_GET_INT (&val);

	  if (i == 0)
	    {
	      index->fk = fk;
	      p = index->fk;
	    }
	  else
	    {
	      if (p != NULL)
		{
		  p->next = fk;
		  p = p->next;
		}
	      else
		{
		  free_and_init (fk->fkname);
		  free_and_init (fk);
		}
	    }
	}
    }
}

/*
 * or_install_btids_prefix_length () - Load prefix length information
 *   return:
 *   prefix_seq(in): sequence which contains the prefix length
 *   index(in): index info structure
 *   num_attrs(in): key attribute count
 */
static void
or_install_btids_prefix_length (DB_SEQ * prefix_seq, OR_INDEX * index,
				int num_attrs)
{
  DB_VALUE val;
  int i;

  assert (prefix_seq != NULL && set_size (prefix_seq) == num_attrs);
  index->attrs_prefix_length = (int *) malloc (sizeof (int) * num_attrs);
  if (index->attrs_prefix_length == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (int) * num_attrs);
      return;
    }

  for (i = 0; i < num_attrs; i++)
    {
      if (set_get_element_nocopy (prefix_seq, i, &val) != NO_ERROR)
	{
	  free_and_init (index->attrs_prefix_length);
	  return;
	}

      index->attrs_prefix_length[i] = DB_GET_INT (&val);
    }
}

/*
 * or_install_btids_filter_pred () - Load index filter predicate information
 *   return: error code
 *   pred_seq(in): sequence which contains the filter predicate
 *   index(in): index info structure
 */
static int
or_install_btids_filter_pred (DB_SEQ * pred_seq, OR_INDEX * index)
{
  DB_VALUE val1, val2;
  int error = NO_ERROR;
  char *pred_stream = NULL;
  char *pred_string = NULL;
  int buffer_len = 0;
  char *buffer = NULL;
  OR_PREDICATE *filter_predicate = NULL;

  index->filter_predicate = NULL;
  if (set_get_element_nocopy (pred_seq, 0, &val1) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      return ER_SM_INVALID_PROPERTY;
    }

  switch (DB_VALUE_TYPE (&val1))
    {
    case DB_TYPE_NULL:
      return NO_ERROR;

    case DB_TYPE_STRING:
      /*continue */
      break;

    default:
      error = ER_SM_INVALID_PROPERTY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      return ER_SM_INVALID_PROPERTY;
    }

  if (set_get_element_nocopy (pred_seq, 1, &val2) != NO_ERROR)
    {
      error = ER_SM_INVALID_PROPERTY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      return ER_SM_INVALID_PROPERTY;
    }

  switch (DB_VALUE_TYPE (&val2))
    {
    case DB_TYPE_NULL:
      return NO_ERROR;

    case DB_TYPE_CHAR:
      /*continue */
      break;

    default:
      error = ER_SM_INVALID_PROPERTY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      return ER_SM_INVALID_PROPERTY;
    }

  /* currently, element 2 from pred_seq is used only on client side */

  filter_predicate = (OR_PREDICATE *) malloc (sizeof (OR_PREDICATE));
  if (filter_predicate == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (OR_PREDICATE));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  filter_predicate->pred_string = strdup (DB_PULL_STRING (&val1));
  if (filter_predicate->pred_string == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      strlen (DB_PULL_STRING (&val1)) * sizeof (char));
      goto err;
    }

  buffer = DB_GET_STRING (&val2);
  buffer_len = DB_GET_STRING_SIZE (&val2);
  filter_predicate->pred_stream =
    (char *) malloc (buffer_len * sizeof (char));
  if (filter_predicate->pred_stream == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, buffer_len * sizeof (char));
      goto err;
    }

  memcpy (filter_predicate->pred_stream, buffer, buffer_len);
  filter_predicate->pred_stream_size = buffer_len;
  index->filter_predicate = filter_predicate;
  return NO_ERROR;

err:
  if (filter_predicate)
    {
      if (filter_predicate->pred_string)
	{
	  free_and_init (filter_predicate->pred_string);
	}

      if (filter_predicate->pred_stream)
	{
	  free_and_init (filter_predicate->pred_stream);
	}

      free_and_init (filter_predicate);
    }
  return error;
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
 *       { [attrID, asc_desc]+, {fk_info} or {key prefix length} or
 *	                              {function index} or {filter index}}
 */
static void
or_install_btids_class (OR_CLASSREP * rep, BTID * id, DB_SEQ * constraint_seq,
			int seq_size, BTREE_TYPE type, const char *cons_name)
{
  DB_VALUE att_val;
  int i, j, e;
  int att_id, att_cnt;
  OR_ATTRIBUTE *att;
  OR_ATTRIBUTE *ptr = NULL;
  OR_INDEX *index;

  if (seq_size < 2)
    {
      /* No attributes IDs here */
      return;
    }

  index = &(rep->indexes[rep->n_indexes]);

  att_cnt = (seq_size - 1) / 2;

  index->atts = (OR_ATTRIBUTE **) malloc (sizeof (OR_ATTRIBUTE *) * att_cnt);
  if (index->atts == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (OR_ATTRIBUTE *) * att_cnt);
      return;
    }

  index->asc_desc = (int *) malloc (sizeof (int) * att_cnt);
  if (index->asc_desc == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
	      1, sizeof (int) * att_cnt);
      return;
    }

  (rep->n_indexes)++;
  index->btid = *id;
  index->n_atts = 0;
  index->type = type;
  index->fk = NULL;
  index->attrs_prefix_length = NULL;
  index->filter_predicate = NULL;
  index->func_index_info = NULL;

  /*
   * For each attribute ID in the set,
   *   Extract the attribute ID,
   *   Find the matching attribute and insert the pointer into the array.
   */

  /* Remember that the attribute IDs start in the second position. */
  e = 1;

  for (i = 0; i < att_cnt; i++)
    {
      if (set_get_element_nocopy (constraint_seq, e++, &att_val) == NO_ERROR)
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

      /* asc_desc info */
      if (set_get_element_nocopy (constraint_seq, e++, &att_val) == NO_ERROR)
	{
	  index->asc_desc[i] = DB_GET_INTEGER (&att_val);
	}
    }
  index->btname = strdup (cons_name);

  if (type == BTREE_FOREIGN_KEY)
    {
      if (set_get_element_nocopy (constraint_seq, seq_size - 1, &att_val)
	  == NO_ERROR)
	{
	  or_install_btids_foreign_key (cons_name,
					DB_PULL_SEQUENCE (&att_val), index);
	}
    }
  else if (type == BTREE_PRIMARY_KEY)
    {
      if (set_get_element_nocopy (constraint_seq, seq_size - 1, &att_val)
	  == NO_ERROR)
	{
	  if (DB_VALUE_TYPE (&att_val) == DB_TYPE_SEQUENCE)
	    {
	      or_install_btids_foreign_key_ref (DB_GET_SEQUENCE (&att_val),
						index);
	    }
	}
    }
  else
    {
      if (set_get_element_nocopy (constraint_seq, seq_size - 1, &att_val)
	  == NO_ERROR)
	{
	  if (DB_VALUE_TYPE (&att_val) == DB_TYPE_SEQUENCE)
	    {
	      DB_SEQ *seq = DB_GET_SEQUENCE (&att_val);
	      DB_VALUE val;

	      if (set_get_element_nocopy (seq, 0, &val) == NO_ERROR)
		{
		  if (DB_VALUE_TYPE (&val) == DB_TYPE_INTEGER)
		    {
		      or_install_btids_prefix_length (DB_GET_SEQUENCE
						      (&att_val), index,
						      att_cnt);
		    }
		  else if (DB_VALUE_TYPE (&val) == DB_TYPE_SEQUENCE)
		    {
		      DB_VALUE avalue;
		      DB_SET *child_seq = DB_GET_SEQUENCE (&val);
		      int seq_size = set_size (seq);
		      int flag;

		      j = 0;
		      while (true)
			{
			  flag = 0;
			  if (set_get_element_nocopy (child_seq, 0, &avalue)
			      != NO_ERROR)
			    {
			      goto next_child;
			    }

			  if (DB_IS_NULL (&avalue) ||
			      DB_VALUE_TYPE (&avalue) != DB_TYPE_STRING)
			    {
			      goto next_child;
			    }

			  if (strcmp (DB_PULL_STRING (&avalue),
				      SM_FILTER_INDEX_ID) == 0)
			    {
			      flag = 0x01;
			    }
			  else if (strcmp (DB_PULL_STRING (&avalue),
					   SM_FUNCTION_INDEX_ID) == 0)
			    {
			      flag = 0x02;
			    }
			  else if (strcmp (DB_PULL_STRING (&avalue),
					   SM_PREFIX_INDEX_ID) == 0)
			    {
			      flag = 0x03;
			    }

			  if (set_get_element_nocopy (child_seq, 1, &avalue)
			      != NO_ERROR)
			    {
			      goto next_child;
			    }

			  if (DB_VALUE_TYPE (&avalue) != DB_TYPE_SEQUENCE)
			    {
			      goto next_child;
			    }

			  switch (flag)
			    {
			    case 0x01:
			      or_install_btids_filter_pred (DB_GET_SEQUENCE
							    (&avalue), index);
			      break;

			    case 0x02:
			      or_install_btids_function_info (DB_GET_SEQUENCE
							      (&avalue),
							      index);
			      break;

			    case 0x03:
			      or_install_btids_prefix_length (DB_GET_SEQUENCE
							      (&avalue),
							      index, att_cnt);
			      break;

			    default:
			      break;
			    }

			next_child:
			  j++;
			  if (j >= seq_size)
			    {
			      break;
			    }

			  if (set_get_element_nocopy (seq, j, &val) !=
			      NO_ERROR)
			    {
			      continue;
			    }

			  if (DB_VALUE_TYPE (&val) != DB_TYPE_SEQUENCE)
			    {
			      continue;
			    }

			  child_seq = DB_GET_SEQUENCE (&val);
			}

		      if (index->func_index_info)
			{
			  /* function index and prefix length not
			     allowed, yet */
			  index->attrs_prefix_length =
			    (int *) malloc (sizeof (int) * att_cnt);
			  if (index->attrs_prefix_length == NULL)
			    {
			      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
				      ER_OUT_OF_VIRTUAL_MEMORY, 1,
				      sizeof (int) * att_cnt);
			      return;
			    }
			  for (i = 0; i < att_cnt; i++)
			    {
			      index->attrs_prefix_length[i] = -1;
			    }
			}
		    }
		  else
		    {
		      assert (0);
		    }
		}
	    }
	  else
	    {
	      assert (0);
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
  int i, seq_size, args;
  int volid, fileid, pageid;
  BTID id;
  DB_VALUE id_val, att_val;

  /*  Extract the first element of the sequence which is the
     encoded B-tree ID */
  /* { btid, [attrID, asc_desc]+, {fk_info} or {key prefix length} } */
  seq_size = set_size (constraint_seq);

  if (set_get_element_nocopy (constraint_seq, 0, &id_val) != NO_ERROR)
    {
      return;
    }

  if (DB_VALUE_TYPE (&id_val) != DB_TYPE_STRING
      || DB_GET_STRING (&id_val) == NULL)
    {
      return;
    }

  args = classobj_decompose_property_oid (DB_PULL_STRING (&id_val),
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
      assert (DB_VALUE_TYPE (&att_val) == DB_TYPE_INTEGER);
      att_id = DB_GET_INTEGER (&att_val);	/* The first attrID */
      (void) or_install_btids_attribute (rep, att_id, &id);
    }

  /*
   *  Assign the B-tree ID to the class.
   *  Cache the constraint in the class with pointer to the attributes.
   *  This is just a different way to store the BTID's.
   */
  or_install_btids_class (rep, &id, constraint_seq, seq_size, type,
			  cons_name);
}

/*
 * or_install_btids () - Install the constraints found on the property list
 *                       into the class and attribute structures
 *   return: void
 *   rep(in): Class representation
 *   props(in): Class property list
 */
static void
or_install_btids (OR_CLASSREP * rep, DB_SEQ * props)
{
  OR_BTREE_PROPERTY property_vars[SM_PROPERTY_NUM_INDEX_FAMILY] = {
    {SM_PROPERTY_FOREIGN_KEY, NULL, BTREE_FOREIGN_KEY, 0},
    {SM_PROPERTY_PRIMARY_KEY, NULL, BTREE_PRIMARY_KEY, 0},
    {SM_PROPERTY_UNIQUE, NULL, BTREE_UNIQUE, 0},
    {SM_PROPERTY_REVERSE_UNIQUE, NULL, BTREE_REVERSE_UNIQUE, 0},
    {SM_PROPERTY_INDEX, NULL, BTREE_INDEX, 0},
    {SM_PROPERTY_REVERSE_INDEX, NULL, BTREE_REVERSE_INDEX, 0}
  };

  DB_VALUE vals[SM_PROPERTY_NUM_INDEX_FAMILY];
  int i;
  int n_btids;

  /*
   *  The first thing to do is to determine how many unique and index
   *  BTIDs we have.  We need this up front so that we can allocate
   *  the OR_INDEX structure in the class (rep).
   */
  n_btids = 0;
  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
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
  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
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
		  cons_name = DB_PULL_STRING (&cons_name_val);
		}

	      error = set_get_element_nocopy (property_vars[i].seq, j + 1,
					      &ids_val);
	      if (error == NO_ERROR && cons_name != NULL)
		{
		  if (DB_VALUE_TYPE (&ids_val) == DB_TYPE_SEQUENCE)
		    {
		      ids_seq = DB_PULL_SEQUENCE (&ids_val);
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
  char *start, *ptr, *attset, *diskatt, *valptr, *dptr, *valptr1, *valptr2;
  int i, start_offset, offset, vallen, n_fixed, n_variable, vallen1, vallen2;
  int n_shared_attrs, n_class_attrs;
  OR_BUF buf;
  DB_VALUE val, def_expr;
  DB_SEQ *att_props = NULL;

  rep = (OR_CLASSREP *) malloc (sizeof (OR_CLASSREP));
  if (rep == NULL)
    {
      return (NULL);
    }

  start = record->data;

  assert (OR_GET_OFFSET_SIZE (start) == BIG_VAR_OFFSET_SIZE);

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
  start_offset = offset = 0;

  for (i = 0, att = rep->attributes; i < rep->n_attributes; i++, att++)
    {
      /* diskatt will now be pointing at the offset table for this attribute.
         this is logically the "start" of this nested object. */
      diskatt = attset + OR_SET_ELEMENT_OFFSET (attset, i);

      /* find out where the original default value is kept */
      valptr = (diskatt +
		OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
					     ORC_ATT_ORIGINAL_VALUE_INDEX));

      vallen = OR_VAR_TABLE_ELEMENT_LENGTH (diskatt,
					    ORC_ATT_ORIGINAL_VALUE_INDEX);

      valptr2 = (diskatt +
		 OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
					      ORC_ATT_CURRENT_VALUE_INDEX));

      vallen2 = OR_VAR_TABLE_ELEMENT_LENGTH (diskatt,
					     ORC_ATT_CURRENT_VALUE_INDEX);

      valptr1 = (diskatt +
		 OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
					      ORC_ATT_PROPERTIES_INDEX));

      vallen1 = OR_VAR_TABLE_ELEMENT_LENGTH (diskatt,
					     ORC_ATT_PROPERTIES_INDEX);

      or_init (&buf, valptr1, vallen1);

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

      if (OR_GET_INT (ptr + ORC_ATT_FLAG_OFFSET) & SM_ATTFLAG_NON_NULL)
	{
	  att->is_notnull = 1;
	}
      else
	{
	  att->is_notnull = 0;
	}

      att->type = (DB_TYPE) OR_GET_INT (ptr + ORC_ATT_TYPE_OFFSET);
      att->id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      att->def_order = OR_GET_INT (ptr + ORC_ATT_DEF_ORDER_OFFSET);
      att->position = i;
      att->default_value.val_length = 0;
      att->default_value.value = NULL;
      att->current_default_value.val_length = 0;
      att->current_default_value.value = NULL;
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
	  att->domain = tp_domain_resolve_default (att->type);
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

      /* get the current default value */
      if (vallen2)
	{
	  if (or_get_current_default_value (att, valptr2, vallen2) == 0)
	    {
	      goto error_cleanup;
	    }
	}

      /* get the default value, this could be using a new DB_VALUE ? */
      if (vallen)
	{
	  if (or_get_default_value (att, valptr, vallen) == 0)
	    {
	      goto error_cleanup;
	    }
	}

      att->default_value.default_expr = DB_DEFAULT_NONE;
      att->current_default_value.default_expr = DB_DEFAULT_NONE;
      if (vallen1 > 0)
	{
	  db_make_null (&val);
	  db_make_null (&def_expr);
	  or_get_value (&buf, &val,
			tp_domain_resolve_default (DB_TYPE_SEQUENCE),
			vallen1, true);
	  att_props = DB_GET_SEQUENCE (&val);
	  if (att_props != NULL &&
	      classobj_get_prop (att_props, "default_expr", &def_expr) > 0)
	    {
	      att->default_value.default_expr = DB_GET_INT (&def_expr);
	      att->current_default_value.default_expr = DB_GET_INT
		(&def_expr);
	    }

	  pr_clear_value (&def_expr);
	  pr_clear_value (&val);
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

      vallen = OR_VAR_TABLE_ELEMENT_LENGTH (diskatt,
					    ORC_ATT_CURRENT_VALUE_INDEX);

      /* set ptr to the beginning of the fixed attributes */
      ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);

      att->is_autoincrement = 0;
      if (OR_GET_INT (ptr + ORC_ATT_FLAG_OFFSET) & SM_ATTFLAG_NON_NULL)
	{
	  att->is_notnull = 1;
	}
      else
	{
	  att->is_notnull = 0;
	}

      att->type = (DB_TYPE) OR_GET_INT (ptr + ORC_ATT_TYPE_OFFSET);
      att->id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      att->def_order = OR_GET_INT (ptr + ORC_ATT_DEF_ORDER_OFFSET);
      att->position = i;
      att->default_value.val_length = 0;
      att->default_value.value = NULL;
      att->default_value.default_expr = DB_DEFAULT_NONE;
      att->current_default_value.val_length = 0;
      att->current_default_value.value = NULL;
      att->current_default_value.default_expr = DB_DEFAULT_NONE;
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
	  att->domain = tp_domain_resolve_default (att->type);
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

	  if (att->default_value.val_length > 0)
	    {
	      att->current_default_value.value =
		malloc (att->default_value.val_length);
	      if (att->current_default_value.value == NULL)
		{
		  goto error_cleanup;
		}

	      memcpy (att->current_default_value.value,
		      att->default_value.value,
		      att->default_value.val_length);
	      att->current_default_value.val_length =
		att->default_value.val_length;
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

      vallen = OR_VAR_TABLE_ELEMENT_LENGTH (diskatt,
					    ORC_ATT_CURRENT_VALUE_INDEX);

      /* set ptr to the beginning of the fixed attributes */
      ptr = diskatt + OR_VAR_TABLE_SIZE (ORC_ATT_VAR_ATT_COUNT);

      att->is_autoincrement = 0;
      att->is_notnull = 0;

      att->type = (DB_TYPE) OR_GET_INT (ptr + ORC_ATT_TYPE_OFFSET);
      att->id = OR_GET_INT (ptr + ORC_ATT_ID_OFFSET);
      att->def_order = OR_GET_INT (ptr + ORC_ATT_DEF_ORDER_OFFSET);
      att->position = i;
      att->default_value.val_length = 0;
      att->default_value.value = NULL;
      att->default_value.default_expr = DB_DEFAULT_NONE;
      att->current_default_value.val_length = 0;
      att->current_default_value.value = NULL;
      att->current_default_value.default_expr = DB_DEFAULT_NONE;
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
	  att->domain = tp_domain_resolve_default (att->type);
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
	  if (att->default_value.val_length > 0)
	    {
	      att->current_default_value.value =
		malloc (att->default_value.val_length);
	      if (att->current_default_value.value == NULL)
		{
		  goto error_cleanup;
		}

	      memcpy (att->current_default_value.value,
		      att->default_value.value,
		      att->default_value.val_length);
	      att->current_default_value.val_length =
		att->default_value.val_length;
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
  free_and_init (rep);

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

  if (repid == NULL_REPRID)
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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

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
  start = offset = 0;

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
      att->default_value.val_length = 0;
      att->default_value.value = NULL;
      att->default_value.default_expr = DB_DEFAULT_NONE;
      att->current_default_value.val_length = 0;
      att->current_default_value.value = NULL;
      att->current_default_value.default_expr = DB_DEFAULT_NONE;

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
	  att->domain = tp_domain_resolve_default (att->type);
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
  /* must align up to a word boundar ! */
  rep->fixed_length = DB_ATT_ALIGN (offset - start);

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
 * or_get_all_representation () - Extracts the description of all
 *                                representation from the disk image of a
 *                                class.
 *   return:
 *   record(in): record with class diskrep
 *   count(out): the number of representation to be returned
 *   do_indexes(in):
 */
OR_CLASSREP **
or_get_all_representation (RECDES * record, bool do_indexes, int *count)
{
  OR_ATTRIBUTE *att;
  OR_CLASSREP *rep, **rep_arr = NULL;
  char *repset, *disk_rep, *attset, *repatt, *dptr, *fixed = NULL;
  int old_rep_count = 0, i, j, offset, start, n_variable, n_fixed;

  if (count)
    {
      *count = 0;
    }

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

  if (!OR_VAR_IS_NULL (record->data, ORC_REPRESENTATIONS_INDEX))
    {
      repset = (record->data +
		OR_VAR_OFFSET (record->data, ORC_REPRESENTATIONS_INDEX));
      old_rep_count = OR_SET_ELEMENT_COUNT (repset);
    }

  /* add one for current representation */
  rep_arr =
    (OR_CLASSREP **) malloc (sizeof (OR_CLASSREP *) * (old_rep_count + 1));
  if (rep_arr == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      (sizeof (OR_CLASSREP *) * (old_rep_count + 1)));
      return NULL;
    }

  memset (rep_arr, 0x0, sizeof (OR_CLASSREP *) * (old_rep_count + 1));

  /* current representation */
  rep_arr[0] = or_get_current_representation (record, 1);
  if (rep_arr[0] == NULL)
    {
      goto error;
    }

  disk_rep = NULL;
  for (i = 0; i < old_rep_count; i++)
    {
      rep_arr[i + 1] = (OR_CLASSREP *) malloc (sizeof (OR_CLASSREP));
      if (rep_arr[i + 1] == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (OR_CLASSREP));
	  goto error;
	}
      rep = rep_arr[i + 1];

      /* set disk_rep to the beginning of the i'th set element */
      disk_rep = repset + OR_SET_ELEMENT_OFFSET (repset, i);

      /* move ptr up to the beginning of the fixed width attributes in this
         object */
      fixed = disk_rep + OR_VAR_TABLE_SIZE (ORC_REP_VAR_ATT_COUNT);

      /* extract the id of this representation */
      rep->id = OR_GET_INT (fixed + ORC_REP_ID_OFFSET);

      n_fixed = OR_GET_INT (fixed + ORC_REP_FIXED_COUNT_OFFSET);
      n_variable = OR_GET_INT (fixed + ORC_REP_VARIABLE_COUNT_OFFSET);

      rep->n_variable = n_variable;
      rep->n_attributes = n_fixed + n_variable;
      rep->n_indexes = 0;
      rep->n_shared_attrs = 0;
      rep->n_class_attrs = 0;
      rep->fixed_length = 0;

      rep->next = NULL;
      rep->attributes = NULL;
      rep->shared_attrs = NULL;
      rep->class_attrs = NULL;
      rep->indexes = NULL;

      if (rep->n_attributes == 0)
	{
	  continue;
	}

      rep->attributes =
	(OR_ATTRIBUTE *) malloc (sizeof (OR_ATTRIBUTE) * rep->n_attributes);
      if (rep->attributes == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, (sizeof (OR_ATTRIBUTE) * rep->n_attributes));
	  goto error;
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
       * total fixed width size.
       */
      start = offset = 0;

      /* build up the attribute descriptions */
      for (j = 0, att = rep->attributes; j < rep->n_attributes; j++, att++)
	{
	  /* set repatt to the beginning of the rep_attribute object in the set */
	  repatt = attset + OR_SET_ELEMENT_OFFSET (attset, j);

	  /* set fixed to the beginning of the fixed width attributes for this
	     object */
	  fixed = repatt + OR_VAR_TABLE_SIZE (ORC_REPATT_VAR_ATT_COUNT);

	  att->id = OR_GET_INT (fixed + ORC_REPATT_ID_OFFSET);
	  att->type = (DB_TYPE) OR_GET_INT (fixed + ORC_REPATT_TYPE_OFFSET);
	  att->position = j;
	  att->default_value.val_length = 0;
	  att->default_value.value = NULL;
	  att->default_value.default_expr = DB_DEFAULT_NONE;
	  att->current_default_value.val_length = 0;
	  att->current_default_value.value = NULL;
	  att->current_default_value.default_expr = DB_DEFAULT_NONE;

	  /* We won't know if there are any B-tree ID's for unique constraints
	     until we read the class property list later on */
	  att->n_btids = 0;
	  att->btids = NULL;

	  /* not currently available, will this be a problem ? */
	  OID_SET_NULL (&(att->classoid));
	  BTID_SET_NULL (&(att->index));

	  /* Extract the full domain for this attribute, think about caching here
	     it will add some time that may not be necessary. */
	  if (OR_VAR_TABLE_ELEMENT_LENGTH (repatt,
					   ORC_REPATT_DOMAIN_INDEX) == 0)
	    {
	      /* shouldn't happen, fake one up from the type ! */
	      att->domain = tp_domain_resolve_default (att->type);
	    }
	  else
	    {
	      dptr =
		repatt + OR_VAR_TABLE_ELEMENT_OFFSET (repatt,
						      ORC_REPATT_DOMAIN_INDEX);
	      att->domain = or_get_domain_and_cache (dptr);
	    }

	  if (j < n_fixed)
	    {
	      att->is_fixed = 1;
	      att->location = offset;
	      offset += tp_domain_disk_size (att->domain);
	    }
	  else
	    {
	      att->is_fixed = 0;
	      att->location = j - n_fixed;
	    }
	}

      /* Offset at this point contains the total fixed size of the
       * representation plus the starting offset, remove the starting offset
       * to get the length of just the fixed width attributes.
       */
      /* must align up to a word boundar ! */
      rep->fixed_length = DB_ATT_ALIGN (offset - start);

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
    }

  if (count)
    {
      *count = old_rep_count + 1;
    }
  return rep_arr;

error:
  for (i = 0; i < old_rep_count + 1; i++)
    {
      or_free_classrep (rep_arr[i]);
    }
  free_and_init (rep_arr);

  return NULL;
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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

  if (repid == NULL_REPRID)
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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

  if (repid == NULL_REPRID)
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
 * or_class_get_partition_info () - Get partition information from a record
 *				    descriptor of a class record
 * return : error code or NO_ERROR
 * record (in) : record descriptor
 * partition_info (in/out) : partition information
 * repr_id (in/out):  representation id from record
 *
 * Note: This function extracts the partition information from a class record.
 * Partition information is represented by the OID of the tuple from the
 * _db_partition class in which we can find the actual partition info.
 * This OID is stored in the class properties sequence
 *
 * If the class is not a partition or is not a partitioned class,
 * partition_info will be returned as a NULL OID
 */
int
or_class_get_partition_info (RECDES * record, OID * partition_info,
			     REPR_ID * repr_id)
{
  int error = NO_ERROR;
  int i = 0, nparts = 0;
  char *subset = NULL;
  DB_SET *setref = NULL;
  DB_VALUE value;
  nparts = 0;

  if (partition_info == NULL)
    {
      assert (false);
      return ER_FAILED;
    }

  if (repr_id != NULL)
    {
      *repr_id = or_class_repid (record);
    }

  OID_SET_NULL (partition_info);

  if (OR_VAR_IS_NULL (record->data, ORC_PROPERTIES_INDEX))
    {
      return NO_ERROR;
    }

  subset = (char *) (record->data) +
    OR_VAR_OFFSET (record->data, ORC_PROPERTIES_INDEX);

  or_unpack_setref (subset, &setref);
  if (setref == NULL)
    {
      return ER_FAILED;
    }

  nparts = set_size (setref);
  for (i = 0; i < nparts; i += 2)
    {
      const char *prop_name = NULL;
      error = set_get_element_nocopy (setref, i, &value);
      if (error != NO_ERROR || DB_VALUE_TYPE (&value) != DB_TYPE_STRING)
	{
	  /* internal storage error */
	  assert (false);
	  set_free (setref);
	  return ER_FAILED;
	}

      prop_name = DB_PULL_STRING (&value);
      if (prop_name == NULL)
	{
	  /* internal storage error */
	  assert (false);
	  set_free (setref);
	  return ER_FAILED;
	}

      if (strcmp (prop_name, SM_PROPERTY_PARTITION) != 0)
	{
	  continue;
	}
      error = set_get_element_nocopy (setref, i + 1, &value);
      if (error != NO_ERROR || DB_VALUE_TYPE (&value) != DB_TYPE_OID)
	{
	  /* internal storage error */
	  assert (false);
	  set_free (setref);
	  return ER_FAILED;
	}
      COPY_OID (partition_info, DB_GET_OID (&value));
      if (OID_ISNULL (partition_info))
	{
	  /* if it exists in the schema then it shouldn't be NULL */
	  set_free (setref);
	  return ER_FAILED;
	}
      break;
    }

  set_free (setref);
  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
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
#endif /* ENABLE_UNUSED_FUNCTION */

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
	      if (att->default_value.value != NULL)
		{
		  free_and_init (att->default_value.value);
		}

	      if (att->current_default_value.value != NULL)
		{
		  free_and_init (att->current_default_value.value);
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
	      if (att->default_value.value != NULL)
		{
		  free_and_init (att->default_value.value);
		}

	      if (att->current_default_value.value != NULL)
		{
		  free_and_init (att->current_default_value.value);
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
	      if (att->default_value.value != NULL)
		{
		  free_and_init (att->default_value.value);
		}

	      if (att->current_default_value.value != NULL)
		{
		  free_and_init (att->current_default_value.value);
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

	      if (index->filter_predicate)
		{
		  if (index->filter_predicate->pred_string)
		    {
		      free_and_init (index->filter_predicate->pred_string);
		    }

		  if (index->filter_predicate->pred_stream)
		    {
		      free_and_init (index->filter_predicate->pred_stream);
		    }

		  free_and_init (index->filter_predicate);
		}

	      if (index->asc_desc != NULL)
		{
		  free_and_init (index->asc_desc);
		}

	      if (index->attrs_prefix_length != NULL)
		{
		  free_and_init (index->attrs_prefix_length);
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
	      if (index->func_index_info)
		{
		  if (index->func_index_info->expr_string)
		    {
		      free_and_init (index->func_index_info->expr_string);
		    }
		  if (index->func_index_info->expr_stream)
		    {
		      free_and_init (index->func_index_info->expr_stream);
		    }
		  free_and_init (index->func_index_info);
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

  assert (OR_GET_OFFSET_SIZE (record->data) == BIG_VAR_OFFSET_SIZE);

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
	  unsigned char len;

	  attr_name = (diskatt +
		       OR_VAR_TABLE_ELEMENT_OFFSET (diskatt,
						    ORC_ATT_NAME_INDEX));

	  /*
	   * kludge kludge kludge
	   * This is now an encoded "varchar" string, we need to skip over the
	   * length before returning it.  Note that this also depends on the
	   * stored string being NULL terminated.
	   */
	  len = *((unsigned char *) attr_name);
	  if (len < 0xFFU)
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

/*
 * or_install_btids_function_info () - Install (add) function index
 *				      information to the index structure
 *				      of the class representation
 *   return: void
 *   index(in): index structure
 *   fi_seq(in): Set which contains the function index information
 */
static void
or_install_btids_function_info (DB_SEQ * fi_seq, OR_INDEX * index)
{
  OR_FUNCTION_INDEX *fi_info = NULL;
  DB_VALUE val, val1;
  char *buffer;

  index->func_index_info = NULL;
  if (fi_seq == NULL)
    {
      return;
    }

  if (set_get_element_nocopy (fi_seq, 0, &val1) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  switch (DB_VALUE_TYPE (&val1))
    {
    case DB_TYPE_NULL:
      return;

    case DB_TYPE_STRING:
      /*continue */
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      return;
    }

  if (set_get_element_nocopy (fi_seq, 1, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }

  switch (DB_VALUE_TYPE (&val))
    {
    case DB_TYPE_NULL:
      return;

    case DB_TYPE_CHAR:
      /*continue */
      break;

    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      return;
    }


  fi_info = (OR_FUNCTION_INDEX *) malloc (sizeof (OR_FUNCTION_INDEX));
  if (fi_info == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      sizeof (OR_FUNCTION_INDEX));
      goto error;
    }

  fi_info->expr_string = strdup (DB_PULL_STRING (&val1));
  if (fi_info->expr_string == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      strlen (DB_PULL_STRING (&val1)) * sizeof (char));
      goto error;
    }

  buffer = DB_GET_STRING (&val);
  fi_info->expr_stream_size = DB_GET_STRING_SIZE (&val);
  fi_info->expr_stream = (char *) malloc (fi_info->expr_stream_size);
  if (fi_info->expr_stream == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      fi_info->expr_stream_size * sizeof (char));
      goto error;
    }
  memcpy (fi_info->expr_stream, buffer, fi_info->expr_stream_size);

  if (set_get_element_nocopy (fi_seq, 2, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  fi_info->col_id = DB_GET_INT (&val);

  if (set_get_element_nocopy (fi_seq, 3, &val) != NO_ERROR)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
      goto error;
    }
  fi_info->attr_index_start = DB_GET_INT (&val);

  index->func_index_info = fi_info;
  return;

error:
  if (fi_info)
    {
      if (fi_info->expr_string)
	{
	  free_and_init (fi_info->expr_string);
	}

      if (fi_info->expr_stream)
	{
	  free_and_init (fi_info->expr_stream);
	}

      free_and_init (fi_info);
    }

  return;
}
