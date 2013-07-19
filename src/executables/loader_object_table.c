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
 * loader_object_table.c - the object table for the loader
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "porting.h"
#include "utility.h"
#include "oid.h"
#include "work_space.h"
#include "db.h"
#include "message_catalog.h"
#include "memory_alloc.h"

#include "loader_object_table.h"

CLASS_TABLE *Classes = NULL;

/*
 * otable_find_class - Locate the class table entry for a class.
 *    return: class table
 *    class(in): class object
 * Note:
 *    If one is not already on the list, a new one is created, added
 *    to the list and returned.
 */
CLASS_TABLE *
otable_find_class (MOP class_)
{
  CLASS_TABLE *table = Classes;

  while (table != NULL && table->class_ != class_)
    {
      table = table->next;
    }
  if (table == NULL)
    {
      table = (CLASS_TABLE *) malloc (sizeof (CLASS_TABLE));
      if (table == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	}
      else
	{
	  table->next = Classes;
	  Classes = table;
	  table->class_ = class_;
	  table->instances = NULL;
	  table->count = 0;
	  table->presize = 0;
	  table->total_inserts = 0;
	}
    }

  return (table);
}


/*
 * flush_class_tables - Free storage for all the class tables.
 *    return: void
 */
static void
flush_class_tables ()
{
  CLASS_TABLE *table, *next;

  for (table = Classes, next = NULL; table != NULL; table = next)
    {
      next = table->next;
      if (table->instances)
	{
	  free_and_init (table->instances);
	}
      free_and_init (table);
    }
  Classes = NULL;
}


/*
 * init_instance_table - Initialized the contents of an instance array
 * within a class table.
 *    return: none
 *    table(out): class table
 */
static void
init_instance_table (CLASS_TABLE * table)
{
  int i;

  for (i = 0; i < table->count; i++)
    {
      table->instances[i].flags = 0;
      OID_SET_NULL (&(table->instances[i].oid));
    }
}


/*
 * realloc_instance_table - Extends the instance array within a CLASS_TABLE.
 *    return: NO_ERROR if successful, error code otherwise
 *    table(in/out): class table to extend
 *    newcount(in): new size of the instance table
 */
static int
realloc_instance_table (CLASS_TABLE * table, int newcount)
{
  INST_INFO *tmp_inst_info;
  int i;

  /*
   * only do this if the new count is larger than the existing
   * table, shouldn't see this
   */
  if (newcount > table->count)
    {
      tmp_inst_info =
	(INST_INFO *) realloc (table->instances,
			       newcount * sizeof (INST_INFO));
      if (tmp_inst_info == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LDR_MEMORY_ERROR, 0);
	  return er_errid ();
	}

      for (i = table->count; i < newcount; i++)
	{
	  tmp_inst_info[i].flags = 0;
	  OID_SET_NULL (&(tmp_inst_info[i].oid));
	}

      table->instances = tmp_inst_info;
      table->count = newcount;
    }
  return NO_ERROR;
}


/*
 * grow_instance_table - extends the instance array in a CLASS_TABLE to be
 * at least as large as the instance id given.
 *    return: NO_ERROR if successful, error code otherwise
 *    table(out): class table
 *    id(in): instance id of interest
 */
static int
grow_instance_table (CLASS_TABLE * table, int id)
{
  return realloc_instance_table (table, id + 1000);
}


/*
 * otable_find - Searches the class table for an instance with the given id.
 *    return: instance info structure
 *    table(in): class table
 *    id(in): instance id
 */
INST_INFO *
otable_find (CLASS_TABLE * table, int id)
{
  if (table->count > id && table->instances[id].flags)
    {
      return &(table->instances[id]);
    }
  return NULL;
}


/*
 * otable_insert - This inserts a new entry in the instance array of a class
 * table.
 *    return: NO_ERROR if successful, error code otherwise
 *    table(out): class table
 *    instance(in): instance OID
 *    id(in): instance id number
 */
int
otable_insert (CLASS_TABLE * table, OID * instance, int id)
{
  int error = NO_ERROR;
  INST_INFO *inst;

  if (id >= table->count)
    {
      error = grow_instance_table (table, id);
    }

  if (error == NO_ERROR)
    {
      inst = &table->instances[id];
      if (inst->flags & INST_FLAG_INSERTED)
	/* lame, should pass in a stream for this */
	fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					 MSGCAT_UTIL_SET_LOADDB,
					 LOADDB_MSG_REDEFINING_INSTANCE), id,
		 db_get_class_name (table->class_));

      inst->oid = *instance;
      inst->flags = INST_FLAG_INSERTED;
    }
  return error;
}


/*
 * otable_reserve - This is used to reserve an element for this instance id.
 *    return: NO_ERROR if successful, error code otherwise
 *    table(out): class table
 *    instance(in): instance OID
 *    id(in): instance id
 * Note:
 *    This is exactly the same as otable_insert except that the
 *    instance element is flagged with INST_FLAG_RESERVED.
 */
int
otable_reserve (CLASS_TABLE * table, OID * instance, int id)
{
  int error = NO_ERROR;
  INST_INFO *inst;

  if (id >= table->count)
    {
      error = grow_instance_table (table, id);
    }

  if (error == NO_ERROR)
    {
      inst = &table->instances[id];
      if (inst->flags)
	{
	  /* should pass in an appropriate stream here */
	  if (inst->flags & INST_FLAG_INSERTED)
	    fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_INSTANCE_DEFINED), id,
		     db_get_class_name (table->class_));
	  else
	    fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_INSTANCE_RESERVED),
		     id, db_get_class_name (table->class_));
	}
      else
	{
	  inst->oid = *instance;
	  inst->flags = INST_FLAG_RESERVED;
	}
    }
  return error;
}


/*
 * otable_class_att_ref - This is used to mark an instance to indicate it is
 * referenced by a class attribute. The instance element is flagged with
 * INST_FLAG_CLASS_ATT.
 *    return:  void
 *    inst(out): instance info
 * Note:
 *    This is used by the loader to mark instances that should not be culled
 *    until commit since they are pointed to directly by a class attribute.
 */
void
otable_class_att_ref (INST_INFO * inst)
{
  if (inst)
    {
      inst->flags = inst->flags | INST_FLAG_CLASS_ATT;
    }
  return;
}


/*
 * otable_update - This is used to mark an existing instance element in a
 * class table as being inserted.
 *    return: NO_ERROR
 *    table(out): class table
 *    id(in): instance id
 */
int
otable_update (CLASS_TABLE * table, int id)
{
  if (table->count > id)
    {
      table->instances[id].flags = INST_FLAG_INSERTED;
    }
  return NO_ERROR;
}


/*
 * otable_map_reserved - maps over all the reserved elements in the class
 * table and calls the supplied function for each one.
 *    return: NO_ERROR if successful, error code otherwise
 *    mapfunc(in): function to call
 *    stop_on_error(in): if set, it stops mapping with given func
 * Note:
 *    THe function is passed the class pointer and the instance OID.
 */
int
otable_map_reserved (OTABLE_MAPFUNC mapfunc, int stop_on_error)
{
  int error = NO_ERROR;
  CLASS_TABLE *table;
  int i;

  for (table = Classes; table != NULL && error == NO_ERROR;
       table = table->next)
    {
      for (i = 0; i < table->count; i++)
	{
	  if (table->instances[i].flags & INST_FLAG_RESERVED)
	    {
	      error = (*mapfunc) (table, &(table->instances[i].oid));
	      if (!stop_on_error)
		{
		  error = NO_ERROR;
		}
	    }
	}
    }
  return error;
}


/*
 * otable_set_presize - set the estimated instance table size to a specific
 * value.
 *    return: void
 *    table(out): class table
 *    id(in): estimated size
 * Note:
 *    This will be used later by otable_prepare to allocate the
 *    actual instance table.
 *    Note that the size passed here is an instance id NOT a table size.
 *    The instance id's are used to index the table so the actual table
 *    size must be 1+ the maximum instance id.
 */
void
otable_set_presize (CLASS_TABLE * table, int id)
{
  if (table != NULL && (id + 1) > table->presize)
    {
      table->presize = id + 1;
    }
}


/*
 * otable_init - initialize the class table module
 *    return: void
 */
int
otable_init (void)
{
  Classes = NULL;
  return NO_ERROR;
}


/*
 * otable_prepare - set up the instance tables
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    This will be called after the syntax check to set up the instance
 *    tables.  During the syntax check we will have built the load_Classes
 *    list and incremented the presize field so we now know exactly
 *    how many instances to expect.
 *
 *    This was originally written to allow the instance tables to have
 *    been previously allocated either as part of the original initialization
 *    with Estimated_size or incrementally during the syntax check.
 *    This is no longer done so the grow_instance_table function above
 *    should never get called.  We now always perform a syntax check
 *    and use the presize calculation to allocate the tables of exactly
 *    the right size.  This is important for performance.
 */
int
otable_prepare (void)
{
  int error = NO_ERROR;
  CLASS_TABLE *table;

  for (table = Classes; table != NULL && !error; table = table->next)
    {
      /*
       * If we already have an instance table, initialize the fields it
       * contains. This shouldn't be necessary.
       */
      init_instance_table (table);

      /* Allocate the table according to the presize count */
      error = realloc_instance_table (table, table->presize);
    }
  return error;
}


/*
 * otable_final - shutdown the class table module
 *    return: void
 */
void
otable_final (void)
{
  flush_class_tables ();
}
