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
 * statistics_cl.c - statistics manager (client)
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>

#include "object_representation.h"
#include "statistics.h"
#include "object_primitive.h"
#include "memory_alloc.h"
#include "work_space.h"
#include "schema_manager.h"
#include "network_interface_cl.h"
#include "tz_support.h"
#include "db_date.h"
#include "db.h"
#include "dbtype_function.h"

static CLASS_STATS *stats_client_unpack_statistics (char *buffer);

/*
 * stats_get_statistics () - Get class statistics
 *   return: error code
 *   classoid(in): OID of the class
 *   timestamp(in):
 *   stats_p(in/out):
 *
 * Note: This function provides an easier interface for the client for
 *       obtaining statistics to the client side by taking care of the
 *       communication details . (Note that the upper levels shouldn't have to
 *       worry about the communication buffer.)
 */
int
stats_get_statistics (OID * class_oid_p, unsigned int time_stamp, CLASS_STATS ** stats_p)
{
  char *buffer_p = NULL;
  int length = -1;
  int error;
  *stats_p = NULL;

  error = stats_get_statistics_from_server (class_oid_p, time_stamp, &length, &buffer_p);
  if (error == NO_ERROR && buffer_p != NULL)
    {
      assert (length > 0);

      *stats_p = stats_client_unpack_statistics (buffer_p);
      free_and_init (buffer_p);
    }

  return error;
}

/*
 * qst_client_unpack_statistics () - Unpack the buffer containing statistics
 *   return: CLASS_STATS or NULL in case of error
 *   bufp(in): buffer containing the class statistics
 *
 * Note: This function unpacks the statistics on the buffer received from the
 *       server side, and builds a CLASS_STATS structure on the work space
 *       area. This sturucture is returned to the caller.
 */
static CLASS_STATS *
stats_client_unpack_statistics (char *buf_p)
{
  CLASS_STATS *class_stats_p;
  ATTR_STATS *attr_stats_p;
  BTREE_STATS *btree_stats_p;
  int i, j, k;

  if (buf_p == NULL)
    {
      return NULL;
    }

  class_stats_p = (CLASS_STATS *) db_ws_alloc (sizeof (CLASS_STATS));
  if (class_stats_p == NULL)
    {
      return NULL;
    }

  class_stats_p->time_stamp = (unsigned int) OR_GET_INT (buf_p);
  buf_p += OR_INT_SIZE;

  class_stats_p->heap_num_objects = OR_GET_INT (buf_p);
  buf_p += OR_INT_SIZE;
  if (class_stats_p->heap_num_objects < 0)
    {
      assert (false);
      class_stats_p->heap_num_objects = 0;
    }

  class_stats_p->heap_num_pages = OR_GET_INT (buf_p);
  buf_p += OR_INT_SIZE;
  if (class_stats_p->heap_num_pages < 0)
    {
      assert (false);
      class_stats_p->heap_num_pages = 0;
    }

  /* to get the doubtful statistics to be updated, need to clear timestamp */
  if (class_stats_p->heap_num_objects == 0 || class_stats_p->heap_num_pages == 0)
    {
      class_stats_p->time_stamp = 0;
    }

  class_stats_p->n_attrs = OR_GET_INT (buf_p);
  buf_p += OR_INT_SIZE;
  if (class_stats_p->n_attrs == 0)
    {
      db_ws_free (class_stats_p);
      return NULL;
    }

  class_stats_p->attr_stats = (ATTR_STATS *) db_ws_alloc (class_stats_p->n_attrs * sizeof (ATTR_STATS));
  if (class_stats_p->attr_stats == NULL)
    {
      db_ws_free (class_stats_p);
      return NULL;
    }

  for (i = 0, attr_stats_p = class_stats_p->attr_stats; i < class_stats_p->n_attrs; i++, attr_stats_p++)
    {
      attr_stats_p->id = OR_GET_INT (buf_p);
      buf_p += OR_INT_SIZE;

      attr_stats_p->type = (DB_TYPE) OR_GET_INT (buf_p);
      buf_p += OR_INT_SIZE;

      attr_stats_p->n_btstats = OR_GET_INT (buf_p);
      buf_p += OR_INT_SIZE;

      OR_GET_INT64 (buf_p, &attr_stats_p->ndv);
      buf_p += OR_INT64_SIZE;

      if (attr_stats_p->n_btstats <= 0)
	{
	  attr_stats_p->bt_stats = NULL;
	  continue;
	}

      attr_stats_p->bt_stats = (BTREE_STATS *) db_ws_alloc (attr_stats_p->n_btstats * sizeof (BTREE_STATS));
      if (attr_stats_p->bt_stats == NULL)
	{
	  stats_free_statistics (class_stats_p);
	  return NULL;
	}
      memset (attr_stats_p->bt_stats, 0, attr_stats_p->n_btstats * sizeof (BTREE_STATS));

      for (j = 0, btree_stats_p = attr_stats_p->bt_stats; j < attr_stats_p->n_btstats; j++, btree_stats_p++)
	{
	  OR_GET_BTID (buf_p, &btree_stats_p->btid);
	  buf_p += OR_BTID_ALIGNED_SIZE;

	  btree_stats_p->leafs = OR_GET_INT (buf_p);
	  buf_p += OR_INT_SIZE;

	  btree_stats_p->pages = OR_GET_INT (buf_p);
	  buf_p += OR_INT_SIZE;

	  btree_stats_p->height = OR_GET_INT (buf_p);
	  buf_p += OR_INT_SIZE;

	  btree_stats_p->has_function = OR_GET_INT (buf_p);
	  buf_p += OR_INT_SIZE;

	  btree_stats_p->keys = OR_GET_INT (buf_p);
	  buf_p += OR_INT_SIZE;

	  btree_stats_p->dedup_idx = OR_GET_INT (buf_p);
	  buf_p += OR_INT_SIZE;

	  buf_p = or_unpack_domain (buf_p, &btree_stats_p->key_type, 0);

	  if (TP_DOMAIN_TYPE (btree_stats_p->key_type) == DB_TYPE_MIDXKEY)
	    {
	      btree_stats_p->pkeys_size = tp_domain_size (btree_stats_p->key_type->setdomain);
	    }
	  else
	    {
	      btree_stats_p->pkeys_size = 1;
	    }

	  /* cut-off to stats */
	  if (btree_stats_p->pkeys_size > BTREE_STATS_PKEYS_NUM)
	    {
	      btree_stats_p->pkeys_size = BTREE_STATS_PKEYS_NUM;
	    }

	  btree_stats_p->pkeys = (int *) db_ws_alloc (btree_stats_p->pkeys_size * sizeof (int));
	  if (btree_stats_p->pkeys == NULL)
	    {
	      stats_free_statistics (class_stats_p);
	      return NULL;
	    }

	  assert (btree_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	  for (k = 0; k < btree_stats_p->pkeys_size; k++)
	    {
	      btree_stats_p->pkeys[k] = OR_GET_INT (buf_p);
	      buf_p += OR_INT_SIZE;
	    }
	}
    }

  /* validate key stats info */
  assert (class_stats_p->heap_num_objects >= 0);
  for (i = 0, attr_stats_p = class_stats_p->attr_stats; i < class_stats_p->n_attrs; i++, attr_stats_p++)
    {
      for (j = 0, btree_stats_p = attr_stats_p->bt_stats; j < attr_stats_p->n_btstats; j++, btree_stats_p++)
	{
	  assert (btree_stats_p->keys >= 0);
	  btree_stats_p->keys = MIN (btree_stats_p->keys, class_stats_p->heap_num_objects);
	  for (k = 0; k < btree_stats_p->pkeys_size; k++)
	    {
	      assert (btree_stats_p->pkeys[k] >= 0);
	      btree_stats_p->pkeys[k] = MIN (btree_stats_p->pkeys[k], btree_stats_p->keys);
	    }
	}
    }

  return class_stats_p;
}

/*
 * stats_free_statistics () - Frees the given CLASS_STAT structure
 *   return: void
 *   class_statsp(in): class statistics to be freed
 */
void
stats_free_statistics (CLASS_STATS * class_statsp)
{
  ATTR_STATS *attr_statsp;
  int i, j;

  if (class_statsp)
    {
      if (class_statsp->attr_stats)
	{
	  for (i = 0, attr_statsp = class_statsp->attr_stats; i < class_statsp->n_attrs; i++, attr_statsp++)
	    {
	      if (attr_statsp->bt_stats)
		{
		  for (j = 0; j < attr_statsp->n_btstats; j++)
		    {
		      if (attr_statsp->bt_stats[j].pkeys)
			{
			  db_ws_free (attr_statsp->bt_stats[j].pkeys);
			  attr_statsp->bt_stats[j].pkeys = NULL;
			}
		    }

		  db_ws_free (attr_statsp->bt_stats);
		  attr_statsp->bt_stats = NULL;
		}
	    }
	  db_ws_free (class_statsp->attr_stats);
	  class_statsp->attr_stats = NULL;
	}

      db_ws_free (class_statsp);
    }
}

/*
 * stats_dump () - Dumps the given statistics about a class
 *   return:
 *   classname(in): The name of class to be printed
 *   fp(in):
 */
void
stats_dump (const char *class_name_p, FILE * file_p)
{
  MOP class_mop;
  CLASS_STATS *class_stats_p;
  ATTR_STATS *attr_stats_p;
  BTREE_STATS *bt_stats_p;
  SM_CLASS *smclass_p;
  int i, j, k;
  const char *name_p;
  const char *prefix_p = "";
  time_t tloc;

  class_mop = sm_find_class (class_name_p);
  if (class_mop == NULL)
    {
      return;
    }

  smclass_p = sm_get_class_with_statistics (class_mop);
  if (smclass_p == NULL)
    {
      return;
    }

  class_stats_p = smclass_p->stats;
  if (class_stats_p == NULL)
    {
      return;
    }

  tloc = (time_t) class_stats_p->time_stamp;

  fprintf (file_p, "\nCLASS STATISTICS\n");
  fprintf (file_p, "****************\n");
  fprintf (file_p, " Class name: %s Timestamp: %s", class_name_p, ctime (&tloc));
  fprintf (file_p, " Total pages in class heap: %d\n", class_stats_p->heap_num_pages);
  fprintf (file_p, " Total objects: %d\n", class_stats_p->heap_num_objects);
  fprintf (file_p, " Number of attributes: %d\n", class_stats_p->n_attrs);

  for (i = 0; i < class_stats_p->n_attrs; i++)
    {
      attr_stats_p = &(class_stats_p->attr_stats[i]);

      name_p = sm_get_att_name (class_mop, attr_stats_p->id);
      fprintf (file_p, " Attribute: %s (", (name_p ? name_p : "not found"));
      fprintf (file_p, "%s)\n", pr_type_name (attr_stats_p->type));
      fprintf (file_p, "    Number of Distinct Values: %ld\n", attr_stats_p->ndv);

      if (attr_stats_p->n_btstats > 0)
	{
	  fprintf (file_p, "    B+tree statistics:\n");

	  for (j = 0; j < attr_stats_p->n_btstats; j++)
	    {
	      bt_stats_p = &(attr_stats_p->bt_stats[j]);

	      fprintf (file_p, "        BTID: { %d , %d }\n", bt_stats_p->btid.vfid.volid,
		       bt_stats_p->btid.vfid.fileid);
	      fprintf (file_p, "        Cardinality: %d (", bt_stats_p->keys);

	      prefix_p = "";
	      assert (bt_stats_p->pkeys_size <= BTREE_STATS_PKEYS_NUM);
	      assert (bt_stats_p->dedup_idx != 0);
	      int pkeys_size = (bt_stats_p->dedup_idx >= 0) ? bt_stats_p->dedup_idx : bt_stats_p->pkeys_size;
	      for (k = 0; k < pkeys_size; k++)
		{
		  fprintf (file_p, "%s%d", prefix_p, bt_stats_p->pkeys[k]);
		  prefix_p = ",";
		}
	      fprintf (file_p, ") ,");
	      fprintf (file_p, " Total pages: %d , Leaf pages: %d , Height: %d\n", bt_stats_p->pages,
		       bt_stats_p->leafs, bt_stats_p->height);
	    }
	}
      fprintf (file_p, "\n");
    }

  fprintf (file_p, "\n\n");
}

/*
 * stats_ndv_dump () - Dumps the NDV about a class
 *   return:
 *   classname(in): The name of class to be printed
 *   fp(in):
 */
void
stats_ndv_dump (const char *class_name_p, FILE * file_p)
{
  MOP class_mop;
  CLASS_ATTR_NDV class_attr_ndv = CLASS_ATTR_NDV_INITIALIZER;
  int i;

  class_mop = sm_find_class (class_name_p);
  if (class_mop == NULL)
    {
      return;
    }

  /* get NDV by query */
  if (stats_get_ndv_by_query (class_mop, &class_attr_ndv, file_p, STATS_WITH_SAMPLING) != NO_ERROR)
    {
      goto end;
    }

  /* print NDV info */
  fprintf (file_p, "\nNumber of Distinct Values\n");
  fprintf (file_p, "****************\n");
  fprintf (file_p, " Class name: %s\n", sm_get_ch_name (class_mop));
  for (i = 0; i < class_attr_ndv.attr_cnt; i++)
    {
      fprintf (file_p, "  %s (%ld)\n", sm_get_att_name (class_mop, class_attr_ndv.attr_ndv[i].id),
	       class_attr_ndv.attr_ndv[i].ndv);
    }
  fprintf (file_p, "total count : %ld\n", class_attr_ndv.attr_ndv[i].ndv);
  fprintf (file_p, "\n");

end:
  if (class_attr_ndv.attr_ndv != NULL)
    {
      free_and_init (class_attr_ndv.attr_ndv);
    }
  return;
}

/*
 * stats_get_ndv_by_query () - get NDV by query
 *   return:
 *   class_mop(in):
 *   attr_ndv(out):
 */
int
stats_get_ndv_by_query (const MOP class_mop, CLASS_ATTR_NDV * class_attr_ndv, FILE * file_p, int with_fullscan)
{
  DB_ATTRIBUTE *att;
  int error = NO_ERROR;
  char *select_list = NULL;
  const char *query = NULL;
  char *query_buf = NULL;
  int buf_size = 0;
  DB_QUERY_RESULT *query_result = NULL;
  DB_QUERY_ERROR query_error;
  DB_VALUE value;
  INT64 v1, v2;
  DB_DOMAIN *dom;
  int i;
  const char *class_name_p = NULL;

  /* if class is not normal class */
  if (!db_is_class (class_mop))
    {
      /* The class does not have a heap file (i.e. it has no instances); so no statistics can be obtained for this
       * class; just set to 0 and return. */
      class_attr_ndv->attr_cnt = 0;
      class_attr_ndv->attr_ndv = (ATTR_NDV *) malloc (sizeof (ATTR_NDV));
      class_attr_ndv->attr_ndv[0].ndv = 0;
      class_attr_ndv->attr_ndv[0].id = 0;
      goto end;
    }

  /* get class_name */
  class_name_p = db_get_class_name (class_mop);
  /* count number of the columns */
  class_attr_ndv->attr_cnt = 0;
  att = (DB_ATTRIBUTE *) db_get_attributes_force (class_mop);
  while (att != NULL)
    {
      class_attr_ndv->attr_cnt++;
      att = db_attribute_next (att);
    }

  class_attr_ndv->attr_ndv = (ATTR_NDV *) malloc (sizeof (ATTR_NDV) * (class_attr_ndv->attr_cnt + 1));
  if (class_attr_ndv->attr_ndv == NULL)
    {
      return ER_FAILED;
    }

  select_list = stats_make_select_list_for_ndv (class_mop, &class_attr_ndv->attr_ndv);
  if (select_list == NULL)
    {
      return ER_FAILED;
    }

  /* create sampling SQL statement */
  if (with_fullscan == STATS_WITH_FULLSCAN)
    {
      query = "SELECT %s FROM [%s]";
    }
  else
    {
      query = "SELECT /*+ SAMPLING_SCAN */ %s FROM [%s]";
    }

  buf_size = strlen (select_list) + strlen (class_name_p) + 40;
  query_buf = (char *) malloc (sizeof (char) * buf_size);
  if (query_buf == NULL)
    {
      error = ER_FAILED;
      goto end;
    }
  snprintf (query_buf, buf_size, query, select_list, class_name_p);

  if (file_p != NULL)
    {
      fprintf (file_p, "Query : %s\n", query_buf);
    }

  /* execute sampling SQL */
  error = db_compile_and_execute_local (query_buf, &query_result, &query_error);
  if (error < NO_ERROR)
    {
      goto end;
    }

  /* save result to NDV info */
  error = db_query_first_tuple (query_result);
  if (error != DB_CURSOR_SUCCESS)
    {
      goto end;
    }

  /* get NDV from tuple */
  for (i = 0; i < class_attr_ndv->attr_cnt; i++)
    {
      error = db_query_get_tuple_value (query_result, i, &value);
      if (error != NO_ERROR)
	{
	  goto end;
	}
      /* if NDV is 0, it is all null values. */
      class_attr_ndv->attr_ndv[i].ndv = DB_GET_BIGINT (&value) == 0 ? 1 : DB_GET_BIGINT (&value);
    }

  /* get count(*) */
  error = db_query_get_tuple_value (query_result, i, &value);
  if (error != NO_ERROR)
    {
      goto end;
    }
  class_attr_ndv->attr_ndv[i].ndv = DB_GET_BIGINT (&value);

end:
  if (select_list)
    {
      free_and_init (select_list);
    }
  if (query_buf)
    {
      free_and_init (query_buf);
    }
  if (query_result)
    {
      db_query_end (query_result);
      query_result = NULL;
    }

  return error;
}

/*
 * stats_make_select_list_for_ndv () - make select-list for ndv
 *   return:
 *   class_mop(in): ....
 *   select_list(in/out):
 */
char *
stats_make_select_list_for_ndv (const MOP class_mop, ATTR_NDV ** attr_ndv)
{
  DB_ATTRIBUTE *att;
  char column[DB_MAX_IDENTIFIER_LENGTH + 20] = { '\0' };
  DB_DOMAIN *dom;
  size_t buf_size = 1024;
  ATTR_NDV *att_ndv = *attr_ndv;
  int i = 0;
  char *select_list = NULL, *select_buf = NULL;

  select_list = (char *) malloc (sizeof (char) * buf_size);
  if (select_list == NULL)
    {
      return NULL;
    }
  /* init select_list */
  *select_list = '\0';

  /* make select_list */
  att = (DB_ATTRIBUTE *) db_get_attributes_force (class_mop);
  while (att != NULL)
    {
      /* check if type is varchar or lob or json. */
      dom = db_attribute_domain (att);
      if (TP_IS_LOB_TYPE (TP_DOMAIN_TYPE (dom)) || TP_DOMAIN_TYPE (dom) == DB_TYPE_JSON ||
	  (TP_IS_CHAR_TYPE (TP_DOMAIN_TYPE (dom)) && dom->precision > STATS_MAX_PRECISION))
	{
	  /* These types are not gathered for statistics. */
	  snprintf (column, 23, "cast (-1 as BIGINT), ");
	}
      else
	{
	  /* make column */
	  snprintf (column, DB_MAX_IDENTIFIER_LENGTH + 20, "count(distinct [%s]), ", db_attribute_name (att));
	}

      /* alloc memory */
      if (strlen (select_list) + strlen (column) > buf_size)
	{
	  buf_size += 1024;
	  select_buf = (char *) realloc (select_list, sizeof (char) * buf_size);
	  if (select_buf == NULL)
	    {
	      goto end;
	    }
	  else
	    {
	      select_list = select_buf;
	    }
	}

      /* concat column */
      strcat (select_list, column);

      /* set column id */
      att_ndv[i++].id = db_attribute_id (att);

      /* advance to next attribute */
      att = db_attribute_next (att);
    }

  /* add "count(*)" */
  if (strlen (select_list) + strlen ("count(*)") > buf_size)
    {
      buf_size += 10;
      select_list = (char *) realloc (select_list, sizeof (char) * buf_size);
      if (select_list == NULL)
	{
	  goto end;
	}
    }
  att_ndv[i].id = -1;
  strcat (select_list, "count(*)");

  return select_list;

end:
  if (select_list)
    {
      free_and_init (select_list);
    }
  return NULL;
}
