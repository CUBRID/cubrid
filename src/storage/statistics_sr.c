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
 * statistics_sr.c - statistics manager (server)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "xserver_interface.h"
#include "memory_alloc.h"
#include "statistics_sr.h"
#include "object_representation.h"
#include "error_manager.h"
#include "storage_common.h"
#include "system_catalog.h"
#include "btree.h"
#include "extendible_hash.h"
#include "heap_file.h"
#include "db.h"

/* Used by the "stats_update_statistics" routine to create the list of all
   classes from the extendible hashing directory used by the catalog manager. */
typedef struct class_id_list CLASS_ID_LIST;
struct class_id_list
{
  OID class_id;
  CLASS_ID_LIST *next;
};

static void stats_free_class_list (CLASS_ID_LIST * clsid_list);
static int stats_get_class_list (THREAD_ENTRY * thread_p, void *key,
				 void *val, void *args);
static int stats_compare_data (DB_DATA * data1, DB_DATA * data2,
			       DB_TYPE type);
static int stats_compare_date (DB_DATE * date1, DB_DATE * date2);
static int stats_compare_time (DB_TIME * time1, DB_TIME * time2);
static int stats_compare_utime (DB_UTIME * utime1, DB_UTIME * utime2);
static int stats_compare_datetime (DB_DATETIME * datetime1_p,
				   DB_DATETIME * datetime2_p);
static int stats_compare_money (DB_MONETARY * mn1, DB_MONETARY * mn2);
static unsigned int stats_get_time_stamp (void);
#if defined(CUBRID_DEBUG)
static void stats_print_min_max (ATTR_STATS * attr_stats, FILE * fpp);
#endif /* CUBRID_DEBUG */

/*
 * xstats_update_class_statistics () -  Updates the statistics for the objects
 *                                    of a given class
 *   return:
 *   class_id(in): Identifier of the class
 *
 * Note: It first retrieves the whole catalog information about this class,
 *       including all possible forms of disk representations for the instance
 *       objects. Then, it performs a complete pass on the heap file of the
 *       class, reading in all of the instance objects one by one and
 *       calculating the ranges of numeric attribute values (ie. min. & max.
 *       values for each numeric attribute).
 *
 *       During this pass on the heap file, these values are maintained
 *       separately for objects with the same representation. Each minimum and
 *       maximum value is initialized when the first instance of the class
 *       with the corresponding representation is encountered. These values are
 *       continually updated as attribute values exceeding the known range are
 *       encountered. At the end of this pass, these individual ranges for
 *       each representation are uniformed in the last (the current)
 *       representation, building the global range values for the attributes
 *       of the class. Then, the btree statistical information is obtained for
 *       each attribute that is indexed and stored in this final representation
 *       structure. Finally, a new timestamp is obtained for these class
 *       statistics and they are stored to disk within the catalog structure
 *       for the last class representation.
 */
int
xstats_update_class_statistics (THREAD_ENTRY * thread_p, OID * class_id_p)
{
  CLS_INFO *cls_info_p = NULL;
  REPR_ID repr_id;
  DISK_REPR *disk_repr_p = NULL;
  DISK_ATTR *disk_attr_p = NULL;
  BTREE_STATS *btree_stats_p = NULL;
  HEAP_SCANCACHE hf_scan_cache, *hf_scan_cache_p = NULL;
  HEAP_CACHE_ATTRINFO hf_cache_attr_info, *hf_cache_attr_info_p = NULL;
  RECDES recdes;
  OID oid;
  SCAN_CODE scan_rc;
  DB_VALUE *db_value_p;
  DB_DATA *db_data_p;
  int i, j;

  cls_info_p = catalog_get_class_info (thread_p, class_id_p);
  if (cls_info_p == NULL)
    {
      goto error;
    }

  /* if class information was not obtained */
  if (cls_info_p->hfid.vfid.fileid < 0 || cls_info_p->hfid.vfid.volid < 0)
    {
      /* The class does not have a heap file (i.e. it has no instances);
         so no statistics can be obtained for this class; just set
         'tot_objects' field to 0 and return. */

      cls_info_p->tot_objects = 0;

      if (catalog_add_class_info (thread_p, class_id_p, cls_info_p) !=
	  NO_ERROR)
	{
	  goto error;
	}

      catalog_free_class_info (cls_info_p);
      return NO_ERROR;
    }

  if (catalog_get_last_representation_id (thread_p, class_id_p, &repr_id) !=
      NO_ERROR)
    {
      goto error;
    }

  disk_repr_p = catalog_get_representation (thread_p, class_id_p, repr_id);
  if (disk_repr_p == NULL)
    {
      goto error;
    }

  cls_info_p->tot_pages = file_get_numpages (thread_p,
					     &cls_info_p->hfid.vfid);
  cls_info_p->tot_objects = 0;
  disk_repr_p->num_objects = 0;

  /* scan whole object of the class and update the statistics */

  if (heap_scancache_start (thread_p, &hf_scan_cache, &(cls_info_p->hfid),
			    class_id_p, true, false,
			    LOCKHINT_NONE) != NO_ERROR)
    {
      goto error;
    }

  hf_scan_cache_p = &hf_scan_cache;

  if (heap_attrinfo_start (thread_p, class_id_p, -1, NULL,
			   &hf_cache_attr_info) != NO_ERROR)
    {
      goto error;
    }
  hf_cache_attr_info_p = &hf_cache_attr_info;

  /* Obtain minimum and maximum value of the instances for each attribute of
     the class and count the number of objects by scanning heap file */

  recdes.area_size = -1;
  scan_rc = heap_first (thread_p, &(cls_info_p->hfid), class_id_p, &oid,
			&recdes, hf_scan_cache_p, PEEK);

  while (scan_rc == S_SUCCESS)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, &oid, &recdes,
				       hf_cache_attr_info_p) != NO_ERROR)
	{
	  scan_rc = S_ERROR;
	  break;
	}

      /* Consider attributes only whose type are fixed because min/max value
         statistics are useful only for those type when calculating the cost
         of query plan by query optimizer. Variable type attributes, for
         example VARCHAR(STRING), take constant number of selectivity. */

      for (i = 0; i < disk_repr_p->n_fixed; i++)
	{
	  disk_attr_p = &(disk_repr_p->fixed[i]);

	  db_value_p = heap_attrinfo_access (disk_attr_p->id,
					     hf_cache_attr_info_p);
	  if (db_value_p != NULL && db_value_is_null (db_value_p) != true)
	    {
	      db_data_p = db_value_get_db_data (db_value_p);

	      if (disk_repr_p->num_objects == 0)
		{
		  /* first object */
		  disk_attr_p->min_value = *db_data_p;
		  disk_attr_p->max_value = *db_data_p;
		}
	      else
		{
		  /* compare with previous values */
		  if (stats_compare_data (db_data_p, &disk_attr_p->min_value,
					  disk_attr_p->type) < 0)
		    {
		      disk_attr_p->min_value = *db_data_p;
		    }

		  if (stats_compare_data (db_data_p, &disk_attr_p->max_value,
					  disk_attr_p->type) > 0)
		    {
		      disk_attr_p->max_value = *db_data_p;
		    }
		}
	    }
	}

      cls_info_p->tot_objects++;
      disk_repr_p->num_objects++;

      scan_rc = heap_next (thread_p, &(cls_info_p->hfid), class_id_p, &oid,
			   &recdes, hf_scan_cache_p, PEEK);
    }

  if (scan_rc == S_ERROR)
    {
      goto error;
    }

  heap_attrinfo_end (thread_p, hf_cache_attr_info_p);
  if (heap_scancache_end (thread_p, hf_scan_cache_p) != NO_ERROR)
    {
      goto error;
    }

  /* update the index statistics for each attribute */

  for (i = 0; i < disk_repr_p->n_fixed + disk_repr_p->n_variable; i++)
    {
      if (i < disk_repr_p->n_fixed)
	{
	  disk_attr_p = disk_repr_p->fixed + i;
	}
      else
	{
	  disk_attr_p = disk_repr_p->variable + (i - disk_repr_p->n_fixed);
	}

      for (j = 0, btree_stats_p = disk_attr_p->bt_stats;
	   j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  if (btree_get_stats (thread_p, &btree_stats_p->btid, btree_stats_p,
			       true) != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* replace the current disk representation structure/information in the
     catalog with the newly computed statistics */

  if (catalog_add_representation (thread_p, class_id_p, repr_id, disk_repr_p)
      != NO_ERROR)
    {
      goto error;
    }

  cls_info_p->time_stamp = stats_get_time_stamp ();

  if (catalog_add_class_info (thread_p, class_id_p, cls_info_p) != NO_ERROR)
    {
      goto error;
    }

  if (disk_repr_p)
    {
      catalog_free_representation (disk_repr_p);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return NO_ERROR;

error:
  if (hf_cache_attr_info_p)
    {
      heap_attrinfo_end (thread_p, hf_cache_attr_info_p);
    }

  if (hf_scan_cache_p)
    {
      (void) heap_scancache_end (thread_p, hf_scan_cache_p);
    }

  if (disk_repr_p)
    {
      catalog_free_representation (disk_repr_p);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return er_errid ();
}

/*
 * xstats_update_statistics () - Updates the statistics for all the classes of
 *                             the database
 *   return:
 *
 * Note: It performs this by getting the list of all classes existing in the
 *       database and their OID's from the catalog's class collection
 *       (maintained in an extendible hashing structure) and calling the
 *       "stats_update_class_statistics" function for each one of the elements
 *       of this list one by one.
 */
int
xstats_update_statistics (THREAD_ENTRY * thread_p)
{
  int error;
  OID class_id;
  CLASS_ID_LIST *class_id_list_p = NULL, *class_id_item_p;

  ehash_map (thread_p, &catalog_Id.xhid, stats_get_class_list,
	     (void *) &class_id_list_p);

  for (class_id_item_p = class_id_list_p;
       class_id_item_p->next != NULL; class_id_item_p = class_id_item_p->next)
    {
      class_id.volid = class_id_item_p->class_id.volid;
      class_id.pageid = class_id_item_p->class_id.pageid;
      class_id.slotid = class_id_item_p->class_id.slotid;

      error = xstats_update_class_statistics (thread_p, &class_id);
      if (error != NO_ERROR)
	{
	  stats_free_class_list (class_id_list_p);
	  return (error);
	}
    }

  stats_free_class_list (class_id_list_p);
  return (NO_ERROR);
}

/*
 * xstats_get_statistics_from_server () - Retrieves the class statistics
 *   return: buffer contaning class statistics, or NULL on error
 *   class_id(in): Identifier of the class
 *   timestamp(in):
 *   length(in): Length of the buffer
 *
 * Note: This function retrieves the statistics for the given class from the
 *       catalog manager and stores them into a buffer. Note that since the
 *       statistics are kept on the current (last) representation structure of
 *       the catalog, only this structure is retrieved. Note further that
 *       since the statistics are used only on the client side they are not
 *       put into a structure here on the server side (not even temporarily),
 *       but stored into a buffer area to be transmitted to the client side.
 */
char *
xstats_get_statistics_from_server (THREAD_ENTRY * thread_p, OID * class_id_p,
				   unsigned int time_stamp, int *length_p)
{
  CLS_INFO *cls_info_p;
  REPR_ID repr_id;
  DISK_REPR *disk_repr_p;
  DISK_ATTR *disk_attr_p;
  BTREE_STATS *btree_stats_p;
  int estimated_npages, estimated_nobjs, dummy_avg_length;
  int i, j, k, size, n_attrs, tot_n_btstats, tot_key_info_size;
  char *buf_p, *start_p;

  *length_p = -1;

  cls_info_p = catalog_get_class_info (thread_p, class_id_p);
  if (!cls_info_p)
    {
      return NULL;
    }

  if (time_stamp > 0 && time_stamp >= cls_info_p->time_stamp)
    {
      catalog_free_class_info (cls_info_p);
      *length_p = 0;
      return NULL;
    }

  if (catalog_get_last_representation_id (thread_p, class_id_p, &repr_id) !=
      NO_ERROR)
    {
      catalog_free_class_info (cls_info_p);
      return NULL;
    }

  disk_repr_p = catalog_get_representation (thread_p, class_id_p, repr_id);
  if (!disk_repr_p)
    {
      catalog_free_class_info (cls_info_p);
      return NULL;
    }

  n_attrs = disk_repr_p->n_fixed + disk_repr_p->n_variable;

  tot_n_btstats = tot_key_info_size = 0;
  for (i = 0; i < n_attrs; i++)
    {
      if (i < disk_repr_p->n_fixed)
	{
	  disk_attr_p = disk_repr_p->fixed + i;
	}
      else
	{
	  disk_attr_p = disk_repr_p->variable + (i - disk_repr_p->n_fixed);
	}

      tot_n_btstats += disk_attr_p->n_btstats;
      for (j = 0, btree_stats_p = disk_attr_p->bt_stats;
	   j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  tot_key_info_size +=
	    (or_packed_domain_size (btree_stats_p->key_type, 0) +
	     (OR_INT_SIZE * btree_stats_p->key_size));
	}
    }

  size = (OR_INT_SIZE		/* time_stamp of CLS_INFO */
	  + OR_INT_SIZE		/* tot_objects of CLS_INFO */
	  + OR_INT_SIZE		/* tot_pages of CLS_INFO */
	  + OR_INT_SIZE		/* n_attrs from DISK_REPR */
	  + (OR_INT_SIZE	/* id of DISK_ATTR */
	     + OR_INT_SIZE	/* type of DISK_ATTR */
	     + STATS_MIN_MAX_SIZE	/* min_value of DISK_ATTR */
	     + STATS_MIN_MAX_SIZE	/* max_value of DISK_ATTR */
	     + OR_INT_SIZE	/* n_btstats of DISK_ATTR */
	  ) * n_attrs);		/* number of attributes */

  size += ((OR_BTID_ALIGNED_SIZE	/* btid of BTREE_STATS */
	    + OR_INT_SIZE	/* leafs of BTREE_STATS */
	    + OR_INT_SIZE	/* pages of BTREE_STATS */
	    + OR_INT_SIZE	/* height of BTREE_STATS */
	    + OR_INT_SIZE	/* keys of BTREE_STATS */
	    + OR_INT_SIZE	/* oids of BTREE_STATS */
	    + OR_INT_SIZE	/* nulls of BTREE_STATS */
	    + OR_INT_SIZE	/* ukeys of BTREE_STATS */
	   ) * tot_n_btstats);	/* total number of indexes */

  size += tot_key_info_size;	/* key_type, pkeys[] of BTREE_STATS */

  start_p = buf_p = (char *) malloc (size);
  if (buf_p == NULL)
    {
      catalog_free_representation (disk_repr_p);
      catalog_free_class_info (cls_info_p);
      return NULL;
    }
  memset (start_p, 0, size);

  OR_PUT_INT (buf_p, cls_info_p->time_stamp);
  buf_p += OR_INT_SIZE;

  estimated_npages = estimated_nobjs = dummy_avg_length = 0;

  if (!HFID_IS_NULL (&cls_info_p->hfid)
      && heap_estimate (thread_p, &cls_info_p->hfid, &estimated_npages,
			&estimated_nobjs, &dummy_avg_length) != ER_FAILED)
    {
      /* use estimates from the heap since it is likely that its estimates
         are more accurate than the ones gathered at update statistics time */
      assert (estimated_nobjs >= 0 && estimated_npages >= 0);

      /* heuristic is that big nobjs is better than small */
      estimated_nobjs = MAX (estimated_nobjs, cls_info_p->tot_objects);
      OR_PUT_INT (buf_p, estimated_nobjs);
      buf_p += OR_INT_SIZE;

      OR_PUT_INT (buf_p, estimated_npages);
      buf_p += OR_INT_SIZE;
    }
  else
    {
      /* cannot get estimates from the heap, use ones from the catalog */
      assert (cls_info_p->tot_objects >= 0 && cls_info_p->tot_pages >= 0);

      OR_PUT_INT (buf_p, cls_info_p->tot_objects);
      buf_p += OR_INT_SIZE;

      OR_PUT_INT (buf_p, cls_info_p->tot_pages);
      buf_p += OR_INT_SIZE;
    }

  OR_PUT_INT (buf_p, n_attrs);
  buf_p += OR_INT_SIZE;

  /* put the statistics information of each attribute to the buffer */
  for (i = 0; i < n_attrs; i++)
    {
      if (i < disk_repr_p->n_fixed)
	{
	  disk_attr_p = disk_repr_p->fixed + i;
	}
      else
	{
	  disk_attr_p = disk_repr_p->variable + (i - disk_repr_p->n_fixed);
	}

      OR_PUT_INT (buf_p, disk_attr_p->id);
      buf_p += OR_INT_SIZE;

      OR_PUT_INT (buf_p, disk_attr_p->type);
      buf_p += OR_INT_SIZE;

      switch (disk_attr_p->type)
	{
	case DB_TYPE_INTEGER:
	  OR_PUT_INT (buf_p, disk_attr_p->min_value.i);
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_INT (buf_p, disk_attr_p->max_value.i);
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_BIGINT:
	  OR_PUT_BIGINT (buf_p, &(disk_attr_p->min_value.bigint));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_BIGINT (buf_p, &(disk_attr_p->max_value.bigint));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_SHORT:
	  /* store these as full integers because of alignment */
	  OR_PUT_INT (buf_p, disk_attr_p->min_value.i);
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_INT (buf_p, disk_attr_p->max_value.i);
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_FLOAT:
	  OR_PUT_FLOAT (buf_p, &(disk_attr_p->min_value.f));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_FLOAT (buf_p, &(disk_attr_p->max_value.f));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_DOUBLE:
	  OR_PUT_DOUBLE (buf_p, &(disk_attr_p->min_value.d));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_DOUBLE (buf_p, &(disk_attr_p->max_value.d));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_DATE:
	  OR_PUT_DATE (buf_p, &(disk_attr_p->min_value.date));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_DATE (buf_p, &(disk_attr_p->max_value.date));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_TIME:
	  OR_PUT_TIME (buf_p, &(disk_attr_p->min_value.time));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_TIME (buf_p, &(disk_attr_p->max_value.time));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_UTIME:
	  OR_PUT_UTIME (buf_p, &(disk_attr_p->min_value.utime));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_UTIME (buf_p, &(disk_attr_p->max_value.utime));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_DATETIME:
	  OR_PUT_DATETIME (buf_p, &(disk_attr_p->min_value.datetime));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_DATETIME (buf_p, &(disk_attr_p->max_value.datetime));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	case DB_TYPE_MONETARY:
	  OR_PUT_MONETARY (buf_p, &(disk_attr_p->min_value.money));
	  buf_p += STATS_MIN_MAX_SIZE;
	  OR_PUT_MONETARY (buf_p, &(disk_attr_p->max_value.money));
	  buf_p += STATS_MIN_MAX_SIZE;
	  break;

	default:
	  break;
	}

      OR_PUT_INT (buf_p, disk_attr_p->n_btstats);
      buf_p += OR_INT_SIZE;

      for (j = 0, btree_stats_p = disk_attr_p->bt_stats;
	   j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  OR_PUT_BTID (buf_p, &btree_stats_p->btid);
	  buf_p += OR_BTID_ALIGNED_SIZE;

	  /* If the btree file has currently more pages than when we gathered
	     statistics, assume that all growth happen at the leaf level. If
	     the btree is smaller, we use the gathered statistics since the
	     btree may have an external file (unknown at this level) to keep
	     overflow keys. */
	  estimated_npages = file_get_numpages (thread_p,
						&btree_stats_p->btid.vfid);
	  if (estimated_npages > btree_stats_p->pages)
	    {
	      OR_PUT_INT (buf_p, (btree_stats_p->leafs +
				  (estimated_npages - btree_stats_p->pages)));
	      buf_p += OR_INT_SIZE;

	      OR_PUT_INT (buf_p, estimated_npages);
	      buf_p += OR_INT_SIZE;
	    }
	  else
	    {
	      OR_PUT_INT (buf_p, btree_stats_p->leafs);
	      buf_p += OR_INT_SIZE;

	      OR_PUT_INT (buf_p, btree_stats_p->pages);
	      buf_p += OR_INT_SIZE;
	    }

	  OR_PUT_INT (buf_p, btree_stats_p->height);
	  buf_p += OR_INT_SIZE;

	  /* If the estimated objects from heap manager is greater than the
	     estimate when the statistics were gathered, assume that the
	     difference is in distinct keys. */
	  if (estimated_nobjs > cls_info_p->tot_objects)
	    {
	      OR_PUT_INT (buf_p, (btree_stats_p->keys +
				  (estimated_nobjs -
				   cls_info_p->tot_objects)));
	      buf_p += OR_INT_SIZE;
	    }
	  else
	    {
	      OR_PUT_INT (buf_p, btree_stats_p->keys);
	      buf_p += OR_INT_SIZE;
	    }

	  OR_PUT_INT (buf_p, btree_stats_p->oids);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->nulls);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->ukeys);
	  buf_p += OR_INT_SIZE;

	  buf_p = or_pack_domain (buf_p, btree_stats_p->key_type, 0, 0);

	  for (k = 0; k < btree_stats_p->key_size; k++)
	    {
	      OR_PUT_INT (buf_p, btree_stats_p->pkeys[k]);
	      buf_p += OR_INT_SIZE;
	    }
	}
    }

  catalog_free_representation (disk_repr_p);
  catalog_free_class_info (cls_info_p);

  *length_p = CAST_STRLEN (buf_p - start_p);

  return start_p;
}

/*
 * qst_get_class_list () - Build the list of OIDs of classes
 *   return: NO_ERROR or error code
 *   key(in): next class OID to be added to the list
 *   val(in): data value associated with the class id on the ext. hash entry
 *   args(in): class list being built
 *
 * Note: This function builds the next node of the class id list. It is passed
 *       to the ehash_map function to be called once on each item kept on the
 *       extendible hashing structure used by the catalog manager.
 */
static int
stats_get_class_list (THREAD_ENTRY * thread_p, void *key, void *val,
		      void *args)
{
  CLASS_ID_LIST *class_id_item_p, **p;

  p = (CLASS_ID_LIST **) args;
  class_id_item_p = (CLASS_ID_LIST *) db_private_alloc (thread_p,
							sizeof
							(CLASS_ID_LIST));
  if (class_id_item_p == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  class_id_item_p->class_id.volid = ((OID *) key)->volid;
  class_id_item_p->class_id.pageid = ((OID *) key)->pageid;
  class_id_item_p->class_id.slotid = ((OID *) key)->slotid;
  class_id_item_p->next = *p;

  *p = class_id_item_p;
  return NO_ERROR;
}

/*
 * qst_free_class_list () - Frees the dynamic memory area used by the passed
 *                          linked list
 *   return: void
 *   class_id_list(in): list to be freed
 */
static void
stats_free_class_list (CLASS_ID_LIST * class_id_list_p)
{
  CLASS_ID_LIST *p, *next_p;

  if (class_id_list_p == NULL)
    {
      return;
    }

  for (p = class_id_list_p; p; p = next_p)
    {
      next_p = p->next;
      db_private_free_and_init (NULL, p);
    }

  class_id_list_p = NULL;
}

/*
 * qst_date_compar () -
 *   return:
 *   date1(in): First date value
 *   date2(in): Second date value
 *
 * Note: This function compares two date values and returns an integer less
 *       than, equal to, or greater than 0, if the first one is less than,
 *       equal to, or greater than the second one, respectively.
 */
static int
stats_compare_date (DB_DATE * date1_p, DB_DATE * date2_p)
{
  return (*date1_p - *date2_p);
}

/*
 * qst_time_compar () -
 *   return:
 *   time1(in): First time value
 *   time2(in): Second time value
 *
 * Note: This function compares two time values and returns an integer less
 *       than, equal to, or greater than 0, if the first one is less than,
 *       equal to, or greater than the second one, respectively.
 */
static int
stats_compare_time (DB_TIME * time1_p, DB_TIME * time2_p)
{
  return (int) (*time1_p - *time2_p);
}

/*
 * qst_utime_compar () -
 *   return:
 *   utime1(in): First utime value
 *   utime2(in): Second utime value
 *
 * Note: This function compares two utime values and returns an integer less
 *       than, equal to, or greater than 0, if the first one is less than,
 *       equal to, or greater than the second one, respectively.
 */
static int
stats_compare_utime (DB_UTIME * utime1_p, DB_UTIME * utime2_p)
{
  return (int) (*utime1_p - *utime2_p);
}

/*
 * stats_compare_datetime () -
 *   return:
 *   datetime1(in): First datetime value
 *   datetime2(in): Second datetime value
 *
 * Note: This function compares two datetime values and returns an integer less
 *       than, equal to, or greater than 0, if the first one is less than,
 *       equal to, or greater than the second one, respectively.
 */
static int
stats_compare_datetime (DB_DATETIME * datetime1_p, DB_DATETIME * datetime2_p)
{
  if (datetime1_p->date < datetime2_p->date)
    {
      return -1;
    }
  else if (datetime1_p->date > datetime2_p->date)
    {
      return 1;
    }
  else if (datetime1_p->time < datetime2_p->time)
    {
      return -1;
    }
  else if (datetime1_p->time > datetime2_p->time)
    {
      return 1;
    }
  else
    {
      return 0;
    }
}

/*
 * qst_money_compar () -
 *   return:
 *   mn1(in): First money value
 *   ,n2(in): Second money value
 *
 * Note: This function compares two money values and returns an integer less
 *       than, equal to, or greater than 0, if the first one is less than,
 *       equal to, or greater than the second one, respectively.
 */
static int
stats_compare_money (DB_MONETARY * money1_p, DB_MONETARY * money2_p)
{
  double comp_result = money1_p->amount - money2_p->amount;

  if (comp_result == 0.0)
    {
      return 0;
    }
  else if (comp_result < 0.0)
    {
      return -1;
    }
  else
    {
      return 1;
    }
}

/*
 * qst_get_time_stamp () - returns the current system time
 *   return: current system time
 */
static unsigned int
stats_get_time_stamp (void)
{
  time_t tloc;

  return (unsigned int) time (&tloc);
}

/*
 * qst_data_compare () -
 *   return:
 *   data1(in):
 *   data2(in):
 *   type(in):
 */
static int
stats_compare_data (DB_DATA * data1_p, DB_DATA * data2_p, DB_TYPE type)
{
  int status = 0;

  switch (type)
    {
    case DB_TYPE_INTEGER:
      status = ((data1_p->i < data2_p->i)
		? DB_LT : ((data1_p->i > data2_p->i) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_BIGINT:
      status = ((data1_p->bigint < data2_p->bigint)
		? DB_LT : ((data1_p->bigint > data2_p->bigint) ? DB_GT :
			   DB_EQ));
      break;

    case DB_TYPE_SHORT:
      status = ((data1_p->sh < data2_p->sh)
		? DB_LT : ((data1_p->sh > data2_p->sh) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_FLOAT:
      status = ((data1_p->f < data2_p->f)
		? DB_LT : ((data1_p->f > data2_p->f) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_DOUBLE:
      status = ((data1_p->d < data2_p->d)
		? DB_LT : ((data1_p->d > data2_p->d) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_DATE:
      status = stats_compare_date (&data1_p->date, &data2_p->date);
      break;

    case DB_TYPE_TIME:
      status = stats_compare_time (&data1_p->time, &data2_p->time);
      break;

    case DB_TYPE_UTIME:
      status = stats_compare_utime (&data1_p->utime, &data2_p->utime);
      break;

    case DB_TYPE_DATETIME:
      status = stats_compare_datetime (&data1_p->datetime,
				       &data2_p->datetime);
      break;

    case DB_TYPE_MONETARY:
      status = stats_compare_money (&data1_p->money, &data2_p->money);
      break;

    default:
      status = 0;
      break;
    }
  return (status);
}

#if defined(CUBRID_DEBUG)
/*
 * stats_print_min_max () -
 *   return:
 *   attr_stats(in): attribute description
 *   fpp(in):
 */
static void
stats_print_min_max (ATTR_STATS * attr_stats, FILE * fpp)
{
  (void) fprintf (fpp, "    Mininum value: ");
  db_print_data (attr_stats->type, &attr_stats->min_value, fpp);

  (void) fprintf (fpp, "\n    Maxinum value: ");
  db_print_data (attr_stats->type, &attr_stats->max_value, fpp);

  (void) fprintf (fpp, "\n");
}

/*
 * stats_dump_class_stats () - Dumps the given statistics about a class
 *   return:
 *   class_stats(in): statistics to be printed
 *   fpp(in):
 */
void
stats_dump_class_statistics (CLASS_STATS * class_stats, FILE * fpp)
{
  int i, j, k;
  const char *prefix = "";
  time_t tloc;

  if (class_stats == NULL)
    {
      return;
    }

  fprintf (fpp, "\nCLASS STATISTICS\n");
  fprintf (fpp, "****************\n");
  tloc = (time_t) class_stats->time_stamp;
  fprintf (fpp, " Timestamp: %s", ctime (&tloc));
  fprintf (fpp, " Total Pages in Class Heap: %d\n", class_stats->heap_size);
  fprintf (fpp, " Total Objects: %d\n", class_stats->num_objects);
  fprintf (fpp, " Number of attributes: %d\n", class_stats->n_attrs);

  for (i = 0; i < class_stats->n_attrs; i++)
    {
      fprintf (fpp, "\n Atrribute :\n");
      fprintf (fpp, "    id: %d\n", class_stats->attr_stats[i].id);
      fprintf (fpp, "    Type: ");

      switch (class_stats->attr_stats[i].type)
	{
	case DB_TYPE_INTEGER:
	  fprintf (fpp, "DB_TYPE_INTEGER \n");
	  break;

	case DB_TYPE_BIGINT:
	  fprintf (fpp, "DB_TYPE_BIGINT \n");
	  break;

	case DB_TYPE_SHORT:
	  fprintf (fpp, "DB_TYPE_SHORT \n");
	  break;

	case DB_TYPE_FLOAT:
	  fprintf (fpp, "DB_TYPE_FLOAT \n");
	  break;

	case DB_TYPE_DOUBLE:
	  fprintf (fpp, "DB_TYPE_DOUBLE \n");
	  break;

	case DB_TYPE_STRING:
	  fprintf (fpp, "DB_TYPE_STRING \n");
	  break;

	case DB_TYPE_OBJECT:
	  fprintf (fpp, "DB_TYPE_OBJECT \n");
	  break;

	case DB_TYPE_SET:
	  fprintf (fpp, "DB_TYPE_SET \n");
	  break;

	case DB_TYPE_MULTI_SET:
	  fprintf (fpp, "DB_TYPE_MULTI_SET \n");
	  break;

	case DB_TYPE_SEQUENCE:
	  fprintf (fpp, "DB_TYPE_SEQUENCE \n");
	  break;

	case DB_TYPE_TIME:
	  fprintf (fpp, "DB_TYPE_TIME \n");
	  break;

	case DB_TYPE_UTIME:
	  fprintf (fpp, "DB_TYPE_UTIME \n");
	  break;

	case DB_TYPE_DATETIME:
	  fprintf (fpp, "DB_TYPE_DATETIME \n");
	  break;

	case DB_TYPE_MONETARY:
	  fprintf (fpp, "DB_TYPE_MONETARY \n");
	  break;

	case DB_TYPE_DATE:
	  fprintf (fpp, "DB_TYPE_DATE \n");
	  break;

	case DB_TYPE_ELO:
	  fprintf (fpp, "DB_TYPE_ELO \n");
	  break;

	case DB_TYPE_BLOB:
	  fprintf (fpp, "DB_TYPE_BLOB \n");
	  break;

	case DB_TYPE_CLOB:
	  fprintf (fpp, "DB_TYPE_CLOB \n");
	  break;

	case DB_TYPE_VARIABLE:
	  fprintf (fpp, "DB_TYPE_VARIABLE  \n");
	  break;

	case DB_TYPE_SUB:
	  fprintf (fpp, "DB_TYPE_SUB \n");
	  break;

	case DB_TYPE_POINTER:
	  fprintf (fpp, "DB_TYPE_POINTER \n");
	  break;

	case DB_TYPE_NULL:
	  fprintf (fpp, "DB_TYPE_NULL \n");
	  break;

	case DB_TYPE_NUMERIC:
	  fprintf (fpp, "DB_TYPE_NUMERIC \n");
	  break;

	case DB_TYPE_BIT:
	  fprintf (fpp, "DB_TYPE_BIT \n");
	  break;

	case DB_TYPE_VARBIT:
	  fprintf (fpp, "DB_TYPE_VARBIT \n");
	  break;

	case DB_TYPE_CHAR:
	  fprintf (fpp, "DB_TYPE_CHAR \n");
	  break;

	case DB_TYPE_NCHAR:
	  fprintf (fpp, "DB_TYPE_NCHARL \n");
	  break;

	case DB_TYPE_VARNCHAR:
	  fprintf (fpp, "DB_TYPE_VARNCHARL \n");
	  break;

	default:
	  break;
	}

      stats_print_min_max (&(class_stats->attr_stats[i]), fpp);
      fprintf (fpp, "    BTree statistics:\n");

      for (j = 0; j < class_stats->attr_stats[i].n_btstats; j++)
	{
	  BTREE_STATS *bt_statsp = &class_stats->attr_stats[i].bt_stats[j];
	  fprintf (fpp, "        BTID: { %d , %d }\n",
		   bt_statsp->btid.vfid.volid, bt_statsp->btid.vfid.fileid);
	  fprintf (fpp, "        Cardinality: %d (", bt_statsp->keys);

	  prefix = "";
	  for (k = 0; k < bt_statsp->key_size; k++)
	    {
	      fprintf (fpp, "%s%d", prefix, bt_statsp->pkeys[k]);
	      prefix = ",";
	    }

	  fprintf (fpp, ") ,");
	  fprintf (fpp, " Total Pages: %d , Leaf Pages: %d ,"
		   " Height: %d\n",
		   bt_statsp->pages, bt_statsp->leafs, bt_statsp->height);
	}
      fprintf (fpp, "\n");
    }

  fprintf (fpp, "\n\n");
}
#endif /* CUBRID_DEBUG */
