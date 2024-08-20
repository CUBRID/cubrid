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
 * statistics_sr.c - statistics manager (server)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "statistics_sr.h"

#include "btree.h"
#include "heap_file.h"
#include "boot_sr.h"
#include "partition_sr.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "thread_entry.hpp"
#include "system_parameter.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define SQUARE(n) ((n)*(n))

/* Used by the "stats_update_all_statistics" routine to create the list of all
   classes from the extensible hashing directory used by the catalog manager. */
typedef struct class_id_list CLASS_ID_LIST;
struct class_id_list
{
  OID class_id;
  CLASS_ID_LIST *next;
};

typedef struct partition_stats_acumulator PARTITION_STATS_ACUMULATOR;
struct partition_stats_acumulator
{
  double leafs;			/* number of leaf pages including overflow pages */
  double pages;			/* number of total pages */
  double height;		/* the height of the B+tree */
  double keys;			/* number of keys */
  int pkeys_size;		/* pkeys array size */
  double *pkeys;		/* partial keys info for example: index (a, b, ..., x) pkeys[0] -> # of {a} pkeys[1] ->
				 * # of {a, b} ... pkeys[pkeys_size-1] -> # of {a, b, ..., x} */
};

#if defined(ENABLE_UNUSED_FUNCTION)
static int stats_compare_data (DB_DATA * data1, DB_DATA * data2, DB_TYPE type);
static int stats_compare_date (DB_DATE * date1, DB_DATE * date2);
static int stats_compare_time (DB_TIME * time1, DB_TIME * time2);
static int stats_compare_utime (DB_UTIME * utime1, DB_UTIME * utime2);
static int stats_compare_datetime (DB_DATETIME * datetime1_p, DB_DATETIME * datetime2_p);
static int stats_compare_money (DB_MONETARY * mn1, DB_MONETARY * mn2);
#endif
static int stats_update_partitioned_statistics (THREAD_ENTRY * thread_p, OID * class_oid, OID * partitions, int count,
						bool with_fullscan, CLASS_ATTR_NDV * class_attr_ndv);

/*
 * xstats_update_statistics () -  Updates the statistics for the objects
 *                                of a given class
 *   return:
 *   class_id(in): Identifier of the class
 *   with_fullscan(in): true iff WITH FULLSCAN
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
xstats_update_statistics (THREAD_ENTRY * thread_p, OID * class_id_p, bool with_fullscan,
			  CLASS_ATTR_NDV * class_attr_ndv)
{
  CLS_INFO *cls_info_p = NULL;
  REPR_ID repr_id;
  DISK_REPR *disk_repr_p = NULL;
  DISK_ATTR *disk_attr_p = NULL;
  BTREE_STATS *btree_stats_p = NULL;
  OID dir_oid;
  int npages, estimated_nobjs;
  char *class_name = NULL;
  int i, j;
  OID *partitions = NULL;
  int count = 0, error_code = NO_ERROR;
  int lk_grant_code = 0;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;

  thread_p->push_resource_tracks ();

  OID_SET_NULL (&dir_oid);

  if (heap_get_class_name (thread_p, class_id_p, &class_name) != NO_ERROR || class_name == NULL)
    {
      /* something wrong. give up. */
      ASSERT_ERROR_AND_SET (error_code);
      thread_p->pop_resource_tracks ();

      return error_code;
    }

  /* before go further, we should get the lock to disable updating schema */
  lk_grant_code = lock_object (thread_p, class_id_p, oid_Root_class_oid, SCH_S_LOCK, LK_COND_LOCK);
  if (lk_grant_code != LK_GRANTED)
    {
      error_code = ER_UPDATE_STAT_CANNOT_GET_LOCK;
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, error_code, 1, class_name ? class_name : "*UNKNOWN-CLASS*");

      goto error;
    }

  if (catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid) != NO_ERROR)
    {
      goto error;
    }

  catalog_access_info.class_oid = class_id_p;
  catalog_access_info.dir_oid = &dir_oid;
  catalog_access_info.class_name = class_name;
  error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  cls_info_p = catalog_get_class_info (thread_p, class_id_p, &catalog_access_info);
  if (cls_info_p == NULL)
    {
      goto error;
    }

  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_STARTED_TO_UPDATE_STATISTICS, 4,
	  class_name ? class_name : "*UNKNOWN-CLASS*", class_id_p->volid, class_id_p->pageid, class_id_p->slotid);

  /* if class information was not obtained */
  if (cls_info_p->ci_hfid.vfid.fileid < 0 || cls_info_p->ci_hfid.vfid.volid < 0)
    {
      /* The class does not have a heap file (i.e. it has no instances); so no statistics can be obtained for this
       * class; just set to 0 and return. */

      cls_info_p->ci_tot_pages = 0;
      cls_info_p->ci_tot_objects = 0;

      error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      error_code = catalog_add_class_info (thread_p, class_id_p, cls_info_p, &catalog_access_info);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      goto end;
    }

  error_code = partition_get_partition_oids (thread_p, class_id_p, &partitions, &count);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (count != 0)
    {
      /* Update statistics for all partitions and the partitioned class */
      assert (partitions != NULL);
      catalog_free_class_info_and_init (cls_info_p);
      error_code =
	stats_update_partitioned_statistics (thread_p, class_id_p, partitions, count, with_fullscan, class_attr_ndv);
      db_private_free (thread_p, partitions);
      if (error_code != NO_ERROR)
	{
	  goto error;
	}

      goto end;
    }

  error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  if (catalog_get_last_representation_id (thread_p, class_id_p, &repr_id) != NO_ERROR)
    {
      goto error;
    }

  disk_repr_p = catalog_get_representation (thread_p, class_id_p, repr_id, &catalog_access_info);
  if (disk_repr_p == NULL)
    {
      goto error;
    }
  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  npages = estimated_nobjs = 0;

  /* do not use estimated npages, get correct info */
  if (file_get_num_user_pages (thread_p, &(cls_info_p->ci_hfid.vfid), &npages) != NO_ERROR)
    {
      goto error;
    }
  assert (npages > 0);
  cls_info_p->ci_tot_pages = MAX (npages, 0);

  /* use value from "select --+ sampling count(*) ..." */
  cls_info_p->ci_tot_objects = class_attr_ndv->attr_ndv[class_attr_ndv->attr_cnt].ndv;

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

      for (j = 0, btree_stats_p = disk_attr_p->bt_stats; j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  assert_release (!BTID_IS_NULL (&btree_stats_p->btid));
	  assert_release (btree_stats_p->pkeys_size > 0);
	  assert_release (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);

	  if (btree_get_stats (thread_p, btree_stats_p, with_fullscan) != NO_ERROR)
	    {
	      goto error;
	    }

	  assert_release (btree_stats_p->keys >= 0);
	}			/* for (j = 0; ...) */

      /* put ndv of columns */
      for (int k = 0; k < class_attr_ndv->attr_cnt; k++)
	{
	  if (disk_attr_p->id == class_attr_ndv->attr_ndv[k].id)
	    {
	      disk_attr_p->ndv = class_attr_ndv->attr_ndv[k].ndv;
	      break;
	    }
	}
    }				/* for (i = 0; ...) */

  error_code = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  /* replace the current disk representation structure/information in the catalog with the newly computed statistics */
  assert (!OID_ISNULL (&(cls_info_p->ci_rep_dir)));
  error_code =
    catalog_add_representation (thread_p, class_id_p, repr_id, disk_repr_p, &(cls_info_p->ci_rep_dir),
				&catalog_access_info);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

  cls_info_p->ci_time_stamp = stats_get_time_stamp ();

  error_code = catalog_add_class_info (thread_p, class_id_p, cls_info_p, &catalog_access_info);
  if (error_code != NO_ERROR)
    {
      goto error;
    }

end:

  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, error_code);

  lock_unlock_object (thread_p, class_id_p, oid_Root_class_oid, SCH_S_LOCK, false);

  if (disk_repr_p)
    {
      catalog_free_representation_and_init (disk_repr_p);
    }

  if (cls_info_p)
    {
      catalog_free_class_info_and_init (cls_info_p);
    }

  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LOG_FINISHED_TO_UPDATE_STATISTICS, 5,
	  class_name ? class_name : "*UNKNOWN-CLASS*", class_id_p->volid, class_id_p->pageid, class_id_p->slotid,
	  error_code);

  if (class_name)
    {
      free_and_init (class_name);
    }

  thread_p->pop_resource_tracks ();

  return error_code;

error:

  if (error_code == NO_ERROR && (error_code = er_errid ()) == NO_ERROR)
    {
      error_code = ER_FAILED;
    }
  goto end;
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
xstats_get_statistics_from_server (THREAD_ENTRY * thread_p, OID * class_id_p, unsigned int time_stamp, int *length_p)
{
  CLS_INFO *cls_info_p;
  REPR_ID repr_id;
  DISK_REPR *disk_repr_p;
  DISK_ATTR *disk_attr_p;
  BTREE_STATS *btree_stats_p;
  OID dir_oid;
  int i, j, k, size, n_attrs, tot_n_btstats, tot_key_info_size;
  char *buf_p, *start_p;
  int key_size;
  int lk_grant_code;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;

  /* init */
  cls_info_p = NULL;
  disk_repr_p = NULL;

  thread_p->push_resource_tracks ();

  *length_p = -1;

  /* the lock on class to avoid changes of representation from rollbacked UPDATE statistics */
  lk_grant_code = lock_object (thread_p, class_id_p, oid_Root_class_oid, SCH_S_LOCK, LK_UNCOND_LOCK);
  if (lk_grant_code != LK_GRANTED)
    {
      char *class_name = NULL;

      if (heap_get_class_name (thread_p, class_id_p, &class_name) != NO_ERROR)
	{
	  /* ignore */
	  er_clear ();
	}

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UPDATE_STAT_CANNOT_GET_LOCK, 1,
	      class_name ? class_name : "*UNKNOWN-CLASS*");

      if (class_name != NULL)
	{
	  free_and_init (class_name);
	}
      goto exit_on_error;
    }

  if (catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  catalog_access_info.class_oid = class_id_p;
  catalog_access_info.dir_oid = &dir_oid;
  if (catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK) != NO_ERROR)
    {
      goto exit_on_error;
    }

  cls_info_p = catalog_get_class_info (thread_p, class_id_p, &catalog_access_info);
  if (cls_info_p == NULL)
    {
      goto exit_on_error;
    }

  if (time_stamp > 0 && time_stamp >= cls_info_p->ci_time_stamp)
    {
      *length_p = 0;
      goto exit_on_error;
    }

  if (catalog_get_last_representation_id (thread_p, class_id_p, &repr_id) != NO_ERROR)
    {
      goto exit_on_error;
    }

  disk_repr_p = catalog_get_representation (thread_p, class_id_p, repr_id, &catalog_access_info);
  if (disk_repr_p == NULL)
    {
      goto exit_on_error;
    }

  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  lock_unlock_object (thread_p, class_id_p, oid_Root_class_oid, SCH_S_LOCK, false);

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
      for (j = 0, btree_stats_p = disk_attr_p->bt_stats; j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  tot_key_info_size += or_packed_domain_size (btree_stats_p->key_type, 0);
	  assert (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  tot_key_info_size += (btree_stats_p->pkeys_size * OR_INT_SIZE);	/* pkeys[] */
	}
    }

  size = (OR_INT_SIZE		/* time_stamp of CLS_INFO */
	  + OR_INT_SIZE		/* tot_objects of CLS_INFO */
	  + OR_INT_SIZE		/* tot_pages of CLS_INFO */
	  + OR_INT_SIZE		/* n_attrs from DISK_REPR */
	  + (OR_INT_SIZE	/* id of DISK_ATTR */
	     + OR_INT_SIZE	/* type of DISK_ATTR */
	     + OR_INT_SIZE	/* n_btstats of DISK_ATTR */
	     + OR_INT64_SIZE	/* Number of Distinct Values */
	  ) * n_attrs);		/* number of attributes */

  size += ((OR_BTID_ALIGNED_SIZE	/* btid of BTREE_STATS */
	    + OR_INT_SIZE	/* leafs of BTREE_STATS */
	    + OR_INT_SIZE	/* pages of BTREE_STATS */
	    + OR_INT_SIZE	/* height of BTREE_STATS */
	    + OR_INT_SIZE	/* keys of BTREE_STATS */
	    + OR_INT_SIZE	/* dedup_idx of BTREE_STATS *//* support for SUPPORT_DEDUPLICATE_KEY_MODE */
	    + OR_INT_SIZE	/* does the BTREE_STATS correspond to a function index */
	   ) * tot_n_btstats);	/* total number of indexes */

  size += tot_key_info_size;	/* key_type, pkeys[] of BTREE_STATS */

  start_p = buf_p = (char *) malloc (size);
  if (buf_p == NULL)
    {
      goto exit_on_error;
    }
  memset (start_p, 0, size);

  OR_PUT_INT (buf_p, cls_info_p->ci_time_stamp);
  buf_p += OR_INT_SIZE;

  assert (cls_info_p->ci_tot_objects >= 0);
  assert (cls_info_p->ci_tot_pages >= 0);

  /* use statistics info */
  OR_PUT_INT (buf_p, cls_info_p->ci_tot_objects);	/* #objects */
  buf_p += OR_INT_SIZE;

  OR_PUT_INT (buf_p, MAX (cls_info_p->ci_tot_pages, 1));	/* #pages */
  buf_p += OR_INT_SIZE;

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

      OR_PUT_INT (buf_p, disk_attr_p->n_btstats);
      buf_p += OR_INT_SIZE;

      OR_PUT_INT64 (buf_p, &disk_attr_p->ndv);
      buf_p += OR_INT64_SIZE;

      for (j = 0, btree_stats_p = disk_attr_p->bt_stats; j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  OR_PUT_BTID (buf_p, &btree_stats_p->btid);
	  buf_p += OR_BTID_ALIGNED_SIZE;

	  /* defense for not gathered statistics */
	  btree_stats_p->leafs = MAX (1, btree_stats_p->leafs);
	  btree_stats_p->pages = MAX (1, btree_stats_p->pages);
	  btree_stats_p->height = MAX (1, btree_stats_p->height);

	  OR_PUT_INT (buf_p, btree_stats_p->leafs);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->pages);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->height);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->has_function);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->keys);
	  buf_p += OR_INT_SIZE;

	  OR_PUT_INT (buf_p, btree_stats_p->dedup_idx);
	  buf_p += OR_INT_SIZE;

	  assert_release (btree_stats_p->leafs >= 1);
	  assert_release (btree_stats_p->pages >= 1);
	  assert_release (btree_stats_p->height >= 1);
	  assert_release (btree_stats_p->keys >= 0);

	  buf_p = or_pack_domain (buf_p, btree_stats_p->key_type, 0, 0);

	  /* get full key size */
	  if (TP_DOMAIN_TYPE (btree_stats_p->key_type) == DB_TYPE_MIDXKEY)
	    {
	      key_size = tp_domain_size (btree_stats_p->key_type->setdomain);
	    }
	  else
	    {
	      key_size = 1;
	    }
	  assert (key_size >= btree_stats_p->pkeys_size);

	  assert (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  for (k = 0; k < btree_stats_p->pkeys_size; k++)
	    {
	      if (k + 1 == key_size)
		{
		  /* this last pkeys must be equal to keys */
		  btree_stats_p->pkeys[k] = btree_stats_p->keys;
		}

	      OR_PUT_INT (buf_p, btree_stats_p->pkeys[k]);
	      buf_p += OR_INT_SIZE;
	    }
	}			/* for (j = 0, ...) */
    }

  catalog_free_representation_and_init (disk_repr_p);
  catalog_free_class_info_and_init (cls_info_p);

  *length_p = CAST_STRLEN (buf_p - start_p);

  thread_p->pop_resource_tracks ();

  return start_p;

exit_on_error:

  if (catalog_access_info.access_started)
    {
      (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, ER_FAILED);
    }

  if (disk_repr_p)
    {
      catalog_free_representation_and_init (disk_repr_p);
    }
  if (cls_info_p)
    {
      catalog_free_class_info_and_init (cls_info_p);
    }

  thread_p->pop_resource_tracks ();

  return NULL;
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * stats_compare_date () -
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
 * stats_compare_time () -
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
 * stats_compare_utime () -
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
 * stats_compare_money () -
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
      status = ((data1_p->i < data2_p->i) ? DB_LT : ((data1_p->i > data2_p->i) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_BIGINT:
      status = ((data1_p->bigint < data2_p->bigint) ? DB_LT : ((data1_p->bigint > data2_p->bigint) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_SHORT:
      status = ((data1_p->sh < data2_p->sh) ? DB_LT : ((data1_p->sh > data2_p->sh) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_FLOAT:
      status = ((data1_p->f < data2_p->f) ? DB_LT : ((data1_p->f > data2_p->f) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_DOUBLE:
      status = ((data1_p->d < data2_p->d) ? DB_LT : ((data1_p->d > data2_p->d) ? DB_GT : DB_EQ));
      break;

    case DB_TYPE_DATE:
      status = stats_compare_date (&data1_p->date, &data2_p->date);
      break;

    case DB_TYPE_TIME:
      status = stats_compare_time (&data1_p->time, &data2_p->time);
      break;

    case DB_TYPE_TIMESTAMP:
    case DB_TYPE_TIMESTAMPLTZ:
      status = stats_compare_utime (&data1_p->utime, &data2_p->utime);
      break;

    case DB_TYPE_TIMESTAMPTZ:
      status = stats_compare_utime (&data1_p->timestamptz.timestamp, &data2_p->timestamptz.timestamp);
      break;

    case DB_TYPE_DATETIME:
    case DB_TYPE_DATETIMELTZ:
      status = stats_compare_datetime (&data1_p->datetime, &data2_p->datetime);
      break;

    case DB_TYPE_DATETIMETZ:
      status = stats_compare_datetime (&data1_p->datetimetz.datetime, &data2_p->datetimetz.datetime);
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
#endif

/*
 * stats_get_time_stamp () - returns the current system time
 *   return: current system time
 */
unsigned int
stats_get_time_stamp (void)
{
  time_t tloc;

  return (unsigned int) time (&tloc);
}

#if defined(CUBRID_DEBUG)
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
  fprintf (fpp, " Total Pages in Class Heap: %d\n", class_stats->heap_num_pages);
  fprintf (fpp, " Total Objects: %d\n", class_stats->heap_num_objects);
  fprintf (fpp, " Number of attributes: %d\n", class_stats->n_attrs);

  for (i = 0; i < class_stats->n_attrs; i++)
    {
      fprintf (fpp, "\n Attribute :\n");
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

	case DB_TYPE_JSON:
	  fprintf (fpp, "DB_TYPE_JSON \n");
	  break;

	case DB_TYPE_MULTISET:
	  fprintf (fpp, "DB_TYPE_MULTISET \n");
	  break;

	case DB_TYPE_SEQUENCE:
	  fprintf (fpp, "DB_TYPE_SEQUENCE \n");
	  break;

	case DB_TYPE_TIME:
	  fprintf (fpp, "DB_TYPE_TIME \n");
	  break;

	case DB_TYPE_TIMESTAMP:
	  fprintf (fpp, "DB_TYPE_TIMESTAMP \n");
	  break;

	case DB_TYPE_TIMESTAMPLTZ:
	  fprintf (fpp, "DB_TYPE_TIMESTAMPLTZ \n");
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  fprintf (fpp, "DB_TYPE_TIMESTAMPTZ \n");
	  break;

	case DB_TYPE_DATETIME:
	  fprintf (fpp, "DB_TYPE_DATETIME \n");
	  break;

	case DB_TYPE_DATETIMELTZ:
	  fprintf (fpp, "DB_TYPE_DATETIMELTZ \n");
	  break;

	case DB_TYPE_DATETIMETZ:
	  fprintf (fpp, "DB_TYPE_DATETIMETZ \n");
	  break;

	case DB_TYPE_MONETARY:
	  fprintf (fpp, "DB_TYPE_MONETARY \n");
	  break;

	case DB_TYPE_DATE:
	  fprintf (fpp, "DB_TYPE_DATE \n");
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
	  fprintf (fpp, "DB_TYPE_NCHAR \n");
	  break;

	case DB_TYPE_VARNCHAR:
	  fprintf (fpp, "DB_TYPE_VARNCHAR \n");
	  break;

	default:
	  break;
	}

      fprintf (fpp, "    BTree statistics:\n");

      for (j = 0; j < class_stats->attr_stats[i].n_btstats; j++)
	{
	  BTREE_STATS *bt_statsp = &class_stats->attr_stats[i].bt_stats[j];
	  fprintf (fpp, "        BTID: { %d , %d }\n", bt_statsp->btid.vfid.volid, bt_statsp->btid.vfid.fileid);
	  fprintf (fpp, "        Cardinality: %d (", bt_statsp->keys);

	  prefix = "";
	  assert (bt_statsp->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  assert (bt_stats_p->dedup_idx != 0);
	  int pkeys_size = (bt_stats_p->dedup_idx >= 0) ? bt_stats_p->dedup_idx : bt_stats_p->pkeys_size;
	  for (k = 0; k < pkeys_size; k++)
	    {
	      fprintf (fpp, "%s%d", prefix, bt_statsp->pkeys[k]);
	      prefix = ",";
	    }

	  fprintf (fpp, ") ,");
	  fprintf (fpp, " Total Pages: %d , Leaf Pages: %d , Height: %d\n", bt_statsp->pages, bt_statsp->leafs,
		   bt_statsp->height);
	}
      fprintf (fpp, "\n");
    }

  fprintf (fpp, "\n\n");
}
#endif /* CUBRID_DEBUG */

/*
 * stats_update_partitioned_statistics () - compute statistics for a partitioned class
 * return : error code or NO_ERROR
 * thread_p (in) :
 * class_id_p (in) : oid of the partitioned class
 * partitions (in) : oids of partitions
 * int partitions_count (in) : number of partitions
 * with_fullscan(in): true iff WITH FULLSCAN
 *
 * Note: Since, during plan generation we only have access to the partitioned
 * class, we have to keep an estimate of average statistics in this class. We
 * are using the average because we consider the case in which only one
 * partition is pruned to be the most common case.
 *
 * The average is computed as the mean (1 + coefficient of variation). We
 * use this coefficient to account for situations in which an index is not
 * balanced in the hierarchy (i.e: has lots of keys in some partitions but
 * very few keys in others). This type of distribution should be considered
 * worse than a balanced distribution since we don't know which partition
 * will be used in the query.
 */
static int
stats_update_partitioned_statistics (THREAD_ENTRY * thread_p, OID * class_id_p, OID * partitions, int partitions_count,
				     bool with_fullscan, CLASS_ATTR_NDV * class_attr_ndv)
{
  int i, j, k, btree_iter, m;
  int error = NO_ERROR;
  CLS_INFO *cls_info_p = NULL;
  CLS_INFO *subcls_info = NULL;
  DISK_REPR *disk_repr_p = NULL, *subcls_disk_rep = NULL;
  REPR_ID repr_id = NULL_REPRID, subcls_repr_id = NULL_REPRID;
  DISK_ATTR *disk_attr_p = NULL, *subcls_attr_p = NULL;
  BTREE_STATS *btree_stats_p = NULL;
  int n_btrees = 0;
  PARTITION_STATS_ACUMULATOR *sum = NULL;
  OR_CLASSREP *cls_rep = NULL;
  OR_CLASSREP *subcls_rep = NULL;
  int cls_idx_cache = 0, subcls_idx_cache = 0;
  CATALOG_ACCESS_INFO catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  CATALOG_ACCESS_INFO part_catalog_access_info = CATALOG_ACCESS_INFO_INITIALIZER;
  OID dir_oid;
  OID part_dir_oid;

  assert_release (class_id_p != NULL);
  assert_release (partitions != NULL);
  assert_release (partitions_count > 0);

  for (i = 0; i < partitions_count; i++)
    {
      error = xstats_update_statistics (thread_p, &partitions[i], with_fullscan, class_attr_ndv);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}
    }

  error = catalog_get_dir_oid_from_cache (thread_p, class_id_p, &dir_oid);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  catalog_access_info.class_oid = class_id_p;
  catalog_access_info.dir_oid = &dir_oid;
  error = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, S_LOCK);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  cls_info_p = catalog_get_class_info (thread_p, class_id_p, &catalog_access_info);
  if (cls_info_p == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  error = catalog_get_last_representation_id (thread_p, class_id_p, &repr_id);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  cls_info_p->ci_tot_pages = 0;
  cls_info_p->ci_tot_objects = 0;

  disk_repr_p = catalog_get_representation (thread_p, class_id_p, repr_id, &catalog_access_info);
  if (disk_repr_p == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto cleanup;
    }

  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, NO_ERROR);

  /* partitions_count number of btree_stats we will need to use */
  n_btrees = 0;
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

      for (j = 0, btree_stats_p = disk_attr_p->bt_stats; j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  n_btrees++;
	}
    }

  if (n_btrees != 0)
    {
      sum = (PARTITION_STATS_ACUMULATOR *) db_private_alloc (thread_p, n_btrees * sizeof (PARTITION_STATS_ACUMULATOR));
      if (sum == NULL)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}

      memset (sum, 0, n_btrees * sizeof (PARTITION_STATS_ACUMULATOR));

      /* initialize pkeys */
      btree_iter = 0;
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

	  for (j = 0, btree_stats_p = disk_attr_p->bt_stats; j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	    {
	      sum[btree_iter].pkeys_size = btree_stats_p->pkeys_size;
	      sum[btree_iter].pkeys =
		(double *) db_private_alloc (thread_p, sum[btree_iter].pkeys_size * sizeof (double));
	      if (sum[btree_iter].pkeys == NULL)
		{
		  error = ER_FAILED;
		  goto cleanup;
		}
	      memset (sum[btree_iter].pkeys, 0, sum[btree_iter].pkeys_size * sizeof (double));
	      btree_iter++;
	    }
	}


    }

  /*
   * The statistics of the main table is generated as the sum of the statistics of each sub-partition.
   * Only the index height is averaged. In the case of Number of Distinct Values, the value may vary depending on
   * whether data is duplicated. In here it is calculated as a sum under the assumption that it is evenly distributed.
   * Because sum of NDV is the maximum value, it may differ from the NDV of the column.
   * When calculating the selectivity of the predicate in OPTIMIZER, the NDV value of btree is used as the maximum value.
   */
  cls_rep = heap_classrepr_get (thread_p, class_id_p, NULL, NULL_REPRID, &cls_idx_cache);
  if (cls_rep == NULL)
    {
      error = ER_FAILED;
      goto cleanup;
    }

  for (i = 0; i < partitions_count; i++)
    {
      /* clean subclass loaded in previous iteration */
      if (subcls_info != NULL)
	{
	  catalog_free_class_info_and_init (subcls_info);
	}
      if (subcls_disk_rep != NULL)
	{
	  catalog_free_representation_and_init (subcls_disk_rep);
	}
      if (subcls_rep != NULL)
	{
	  heap_classrepr_free_and_init (subcls_rep, &subcls_idx_cache);
	}

      if (catalog_get_dir_oid_from_cache (thread_p, &partitions[i], &part_dir_oid) != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto cleanup;
	}

      part_catalog_access_info.class_oid = &partitions[i];
      part_catalog_access_info.dir_oid = &part_dir_oid;
      if (catalog_start_access_with_dir_oid (thread_p, &part_catalog_access_info, S_LOCK) != NO_ERROR)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}

      /* load new subclass */
      subcls_info = catalog_get_class_info (thread_p, &partitions[i], &part_catalog_access_info);
      if (subcls_info == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}
      cls_info_p->ci_tot_pages += subcls_info->ci_tot_pages;
      cls_info_p->ci_tot_objects = class_attr_ndv->attr_ndv[class_attr_ndv->attr_cnt].ndv;;

      /* get disk repr for subclass */
      error = catalog_get_last_representation_id (thread_p, &partitions[i], &subcls_repr_id);
      if (error != NO_ERROR)
	{
	  goto cleanup;
	}

      subcls_disk_rep = catalog_get_representation (thread_p, &partitions[i], subcls_repr_id,
						    &part_catalog_access_info);
      if (subcls_disk_rep == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto cleanup;
	}

      (void) catalog_end_access_with_dir_oid (thread_p, &part_catalog_access_info, NO_ERROR);

      subcls_rep = heap_classrepr_get (thread_p, &partitions[i], NULL, NULL_REPRID, &subcls_idx_cache);
      if (subcls_rep == NULL)
	{
	  error = ER_FAILED;
	  goto cleanup;
	}

      /* add partition information to the accumulators and also update min/max values for partitioned class disk
       * representation */
      btree_iter = 0;
      for (j = 0; j < subcls_disk_rep->n_fixed + subcls_disk_rep->n_variable; j++)
	{
	  if (j < subcls_disk_rep->n_fixed)
	    {
	      subcls_attr_p = subcls_disk_rep->fixed + j;
	      disk_attr_p = disk_repr_p->fixed + j;
	    }
	  else
	    {
	      subcls_attr_p = subcls_disk_rep->variable + (j - subcls_disk_rep->n_fixed);
	      disk_attr_p = disk_repr_p->variable + (j - disk_repr_p->n_fixed);
	    }

	  /* check for partitions schema changes are not yet finished */
	  if (subcls_attr_p->id != disk_attr_p->id || subcls_attr_p->n_btstats != disk_attr_p->n_btstats)
	    {
	      error = NO_ERROR;
	      goto cleanup;
	    }

	  assert_release (subcls_attr_p->id == disk_attr_p->id);
	  assert_release (subcls_attr_p->n_btstats == disk_attr_p->n_btstats);

	  for (k = 0, btree_stats_p = disk_attr_p->bt_stats; k < disk_attr_p->n_btstats; k++, btree_stats_p++)
	    {
	      const BTREE_STATS *subcls_stats;

	      subcls_stats = stats_find_inherited_index_stats (cls_rep, subcls_rep, subcls_attr_p,
							       &btree_stats_p->btid);
	      if (subcls_stats == NULL)
		{
		  error = ER_FAILED;
		  goto cleanup;
		}

	      sum[btree_iter].leafs += subcls_stats->leafs;

	      sum[btree_iter].pages += subcls_stats->pages;

	      sum[btree_iter].height += subcls_stats->height;

	      sum[btree_iter].keys += subcls_stats->keys;

	      assert (subcls_stats->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	      for (m = 0; m < subcls_stats->pkeys_size; m++)
		{
		  sum[btree_iter].pkeys[m] += subcls_stats->pkeys[m];
		}

	      btree_iter++;
	    }
	}
    }

  /* recalculate the height of the btree to the average */
  for (btree_iter = 0; btree_iter < n_btrees; btree_iter++)
    {
      sum[btree_iter].height = ceil (sum[btree_iter].height / partitions_count);
    }

  /* put new statistics */
  btree_iter = 0;
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

      /* put ndv of columns */
      for (int k = 0; k < class_attr_ndv->attr_cnt; k++)
	{
	  if (disk_attr_p->id == class_attr_ndv->attr_ndv[k].id)
	    {
	      disk_attr_p->ndv = class_attr_ndv->attr_ndv[k].ndv;
	      break;
	    }
	}
      /* put btree stats */
      for (j = 0, btree_stats_p = disk_attr_p->bt_stats; j < disk_attr_p->n_btstats; j++, btree_stats_p++)
	{
	  btree_stats_p->leafs = sum[btree_iter].leafs;
	  btree_stats_p->pages = sum[btree_iter].pages;
	  btree_stats_p->height = sum[btree_iter].height;	/* average height */
	  btree_stats_p->keys = sum[btree_iter].keys;

	  assert (sum[btree_iter].pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  for (m = 0; m < sum[btree_iter].pkeys_size; m++)
	    {
	      if (sum[btree_iter].pkeys[m] != 0)
		{
		  btree_stats_p->pkeys[m] = sum[btree_iter].pkeys[m];
		}
	    }
	  btree_iter++;
	}
    }

  error = catalog_start_access_with_dir_oid (thread_p, &catalog_access_info, X_LOCK);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  /* replace the current disk representation structure/information in the catalog with the newly computed statistics */
  assert (!OID_ISNULL (&(cls_info_p->ci_rep_dir)));
  error = catalog_add_representation (thread_p, class_id_p, repr_id, disk_repr_p, &(cls_info_p->ci_rep_dir),
				      &catalog_access_info);
  if (error != NO_ERROR)
    {
      goto cleanup;
    }

  cls_info_p->ci_time_stamp = stats_get_time_stamp ();

  error = catalog_add_class_info (thread_p, class_id_p, cls_info_p, &catalog_access_info);

cleanup:
  (void) catalog_end_access_with_dir_oid (thread_p, &catalog_access_info, error);
  (void) catalog_end_access_with_dir_oid (thread_p, &part_catalog_access_info, error);

  if (cls_rep != NULL)
    {
      heap_classrepr_free_and_init (cls_rep, &cls_idx_cache);
    }
  if (subcls_rep != NULL)
    {
      heap_classrepr_free_and_init (subcls_rep, &subcls_idx_cache);
    }
  if (sum != NULL)
    {
      for (i = 0; i < n_btrees; i++)
	{
	  if (sum[i].pkeys != NULL)
	    {
	      db_private_free_and_init (thread_p, sum[i].pkeys);
	    }
	}
      db_private_free_and_init (thread_p, sum);
    }
  if (subcls_info)
    {
      catalog_free_class_info_and_init (subcls_info);
    }
  if (subcls_disk_rep != NULL)
    {
      catalog_free_representation_and_init (subcls_disk_rep);
    }
  if (cls_info_p != NULL)
    {
      catalog_free_class_info_and_init (cls_info_p);
    }
  if (disk_repr_p != NULL)
    {
      catalog_free_representation_and_init (disk_repr_p);
    }

  return error;
}

/*
 * stats_find_inherited_index_stats () - find the btree statistics corresponding to an index in a subclass
 * return : btree statistics
 * cls_rep (in)	   : superclass representation
 * subcls_rep (in) : subclass representation
 * subcls_attr (in): subclass attribute representation which contains the
 *		     statistics
 * cls_btid (in)   : BTID of the index in the superclass
 */
const BTREE_STATS *
stats_find_inherited_index_stats (OR_CLASSREP * cls_rep, OR_CLASSREP * subcls_rep, DISK_ATTR * subcls_attr,
				  BTID * cls_btid)
{
  int i;
  const char *cls_btname = NULL;
  const BTID *subcls_btid = NULL;
  BTREE_STATS *btstats = NULL;

  /* find the index representation in the superclass */
  for (i = 0; i < cls_rep->n_indexes; i++)
    {
      if (BTID_IS_EQUAL (&cls_rep->indexes[i].btid, cls_btid))
	{
	  cls_btname = cls_rep->indexes[i].btname;
	  break;
	}
    }

  if (cls_btname == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  /* find the inherited index in the subclass */
  for (i = 0; i < subcls_rep->n_indexes; i++)
    {
      if (strcasecmp (cls_btname, subcls_rep->indexes[i].btname) == 0)
	{
	  subcls_btid = &subcls_rep->indexes[i].btid;
	  break;
	}
    }

  if (subcls_btid == NULL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  for (i = 0, btstats = subcls_attr->bt_stats; i < subcls_attr->n_btstats; i++, btstats++)
    {
      if (BTID_IS_EQUAL (subcls_btid, &btstats->btid))
	{
	  return btstats;
	}
    }

  assert (false);
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  return NULL;
}
