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
#include "db_date.h"

static CLASS_STATS *stats_client_unpack_statistics (char *buffer);

/*
 * stats_get_statistics () - Get class statistics
 *   return:
 *   classoid(in): OID of the class
 *   timestamp(in):
 *
 * Note: This function provides an easier interface for the client for
 *       obtaining statistics to the client side by taking care of the
 *       communication details . (Note that the upper levels shouldn't have to
 *       worry about the communication buffer.)
 */
CLASS_STATS *
stats_get_statistics (OID * class_oid_p, unsigned int time_stamp)
{
  CLASS_STATS *stats_p = NULL;
  char *buffer_p = NULL;
  int length = -1;

  buffer_p = stats_get_statistics_from_server (class_oid_p, time_stamp, &length);
  if (buffer_p)
    {
      assert (length > 0);

      stats_p = stats_client_unpack_statistics (buffer_p);
      free_and_init (buffer_p);
    }

  return stats_p;
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
  int max_unique_keys;
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

  /* correct estimated num_objects with unique keys */
  max_unique_keys = OR_GET_INT (buf_p);
  buf_p += OR_INT_SIZE;
  if (max_unique_keys > 0)
    {
      class_stats_p->heap_num_objects = max_unique_keys;
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
      fprintf (file_p, " Attribute: %s\n", (name_p ? name_p : "not found"));
      fprintf (file_p, "    id: %d\n", attr_stats_p->id);
      fprintf (file_p, "    Type: ");

      switch (attr_stats_p->type)
	{
	case DB_TYPE_SHORT:
	  fprintf (file_p, "DB_TYPE_SHORT\n");
	  break;

	case DB_TYPE_INTEGER:
	  fprintf (file_p, "DB_TYPE_INTEGER\n");
	  break;

	case DB_TYPE_BIGINT:
	  fprintf (file_p, "DB_TYPE_BIGINT\n");
	  break;

	case DB_TYPE_FLOAT:
	  fprintf (file_p, "DB_TYPE_FLOAT\n");
	  break;

	case DB_TYPE_DOUBLE:
	  fprintf (file_p, "DB_TYPE_DOUBLE\n");
	  break;

	case DB_TYPE_STRING:
	  fprintf (file_p, "DB_TYPE_STRING\n");
	  break;

	case DB_TYPE_OBJECT:
	  fprintf (file_p, "DB_TYPE_OBJECT\n");
	  break;

	case DB_TYPE_SET:
	  fprintf (file_p, "DB_TYPE_SET\n");
	  break;

	case DB_TYPE_MULTISET:
	  fprintf (file_p, "DB_TYPE_MULTISET\n");
	  break;

	case DB_TYPE_SEQUENCE:
	  fprintf (file_p, "DB_TYPE_SEQUENCE\n");
	  break;

	case DB_TYPE_TIME:
	  fprintf (file_p, "DB_TYPE_TIME\n");
	  break;

	case DB_TYPE_TIMELTZ:
	  fprintf (file_p, "DB_TYPE_TIMELTZ\n");
	  break;

	case DB_TYPE_TIMETZ:
	  fprintf (file_p, "DB_TYPE_TIMETZ\n");
	  break;

	case DB_TYPE_UTIME:
	  fprintf (file_p, "DB_TYPE_UTIME\n");
	  break;

	case DB_TYPE_TIMESTAMPLTZ:
	  fprintf (file_p, "DB_TYPE_TIMESTAMPLTZ\n");
	  break;

	case DB_TYPE_TIMESTAMPTZ:
	  fprintf (file_p, "DB_TYPE_TIMESTAMPTZ\n");
	  break;

	case DB_TYPE_DATETIME:
	  fprintf (file_p, "DB_TYPE_DATETIME\n");
	  break;

	case DB_TYPE_DATETIMELTZ:
	  fprintf (file_p, "DB_TYPE_DATETIMELTZ\n");
	  break;

	case DB_TYPE_DATETIMETZ:
	  fprintf (file_p, "DB_TYPE_DATETIMETZ\n");
	  break;

	case DB_TYPE_MONETARY:
	  fprintf (file_p, "DB_TYPE_MONETARY\n");
	  break;

	case DB_TYPE_DATE:
	  fprintf (file_p, "DB_TYPE_DATE\n");
	  break;

	case DB_TYPE_BLOB:
	  fprintf (file_p, "DB_TYPE_BLOB\n");
	  break;

	case DB_TYPE_CLOB:
	  fprintf (file_p, "DB_TYPE_CLOB\n");
	  break;

	case DB_TYPE_VARIABLE:
	  fprintf (file_p, "DB_TYPE_VARIABLE\n");
	  break;

	case DB_TYPE_SUB:
	  fprintf (file_p, "DB_TYPE_SUB\n");
	  break;

	case DB_TYPE_POINTER:
	  fprintf (file_p, "DB_TYPE_POINTER\n");
	  break;

	case DB_TYPE_NULL:
	  fprintf (file_p, "DB_TYPE_NULL\n");
	  break;

	case DB_TYPE_NUMERIC:
	  fprintf (file_p, "DB_TYPE_NUMERIC\n");
	  break;

	case DB_TYPE_BIT:
	  fprintf (file_p, "DB_TYPE_BIT\n");
	  break;

	case DB_TYPE_VARBIT:
	  fprintf (file_p, "DB_TYPE_VARBIT\n");
	  break;

	case DB_TYPE_CHAR:
	  fprintf (file_p, "DB_TYPE_CHAR\n");
	  break;

	case DB_TYPE_NCHAR:
	  fprintf (file_p, "DB_TYPE_NCHAR\n");
	  break;

	case DB_TYPE_VARNCHAR:
	  fprintf (file_p, "DB_TYPE_VARNCHAR\n");
	  break;

	case DB_TYPE_DB_VALUE:
	  fprintf (file_p, "DB_TYPE_DB_VALUE\n");
	  break;

	case DB_TYPE_ENUMERATION:
	  fprintf (file_p, "DB_TYPE_ENUMERATION\n");
	  break;

	default:
	  assert (false);
	  fprintf (file_p, "UNKNOWN_TYPE\n");
	  break;
	}

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
	      for (k = 0; k < bt_stats_p->pkeys_size; k++)
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
