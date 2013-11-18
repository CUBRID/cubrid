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


#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "porting.h"
#include "dbtype.h"
#include "dbdef.h"
#include "load_object.h"
#include "db.h"
#include "locator_cl.h"
#include "locator_sr.h"
#include "schema_manager.h"
#include "heap_file.h"
#include "system_catalog.h"
#include "object_accessor.h"
#include "set_object.h"
#include "btree.h"
#include "message_catalog.h"
#include "network_interface_cl.h"
#include "server_interface.h"
#include "system_parameter.h"
#include "utility.h"
#include "authenticate.h"
#include "transaction_cl.h"
#include "execute_schema.h"

#define COMPACT_MIN_PAGES 1
#define COMPACT_MAX_PAGES 20

#define COMPACT_INSTANCE_MIN_LOCK_TIMEOUT 1
#define COMPACT_INSTANCE_MAX_LOCK_TIMEOUT 10

#define COMPACT_CLASS_MIN_LOCK_TIMEOUT 1
#define COMPACT_CLASS_MAX_LOCK_TIMEOUT 10

static int is_not_system_class (MOBJ class_);
static int do_reclaim_addresses (const OID ** const class_oids,
				 const int num_class_oids,
				 int *const num_classes_fully_processed,
				 const bool verbose,
				 const int class_lock_timeout);
static int do_reclaim_class_addresses (const OID class_oid,
				       char **clas_name,
				       bool * const
				       any_class_can_be_referenced,
				       bool * const correctly_processed,
				       bool * const addresses_reclaimed,
				       int *const error_while_processing);
static int class_instances_can_be_referenced (MOP mop, MOP parent_mop,
					      bool *
					      const class_can_be_referenced,
					      bool *
					      const
					      any_class_can_be_referenced,
					      MOP * const all_mops,
					      const int num_mops);
static int class_referenced_by_class (MOP referenced_mop, MOP parent_mop,
				      MOP referring_mop,
				      bool * const class_can_be_referenced,
				      bool *
				      const any_class_can_be_referenced);
static int class_referenced_by_attributes (MOP referenced_class,
					   MOP parent_mop,
					   SM_ATTRIBUTE *
					   const attributes_list,
					   bool *
					   const class_can_be_referenced,
					   bool *
					   const any_class_can_be_referenced);
static void class_referenced_by_domain (MOP referenced_class,
					TP_DOMAIN * const domain,
					bool * const class_can_be_referenced,
					bool *
					const any_class_can_be_referenced);


/*
 * compact_usage() - print an usage of the compactdb-utility
 *   return: void
 *   exec_name(in): a name of this application
 */
static void
compactdb_usage (const char *argv0)
{
  const char *exec_name;

  exec_name = basename ((char *) argv0);
  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB, 60), exec_name);
}

/*
 * get_num_requested_class - Get the number of class from specified
 * input file
 *    return: error code
 *    input_filename(in): input file name
 *    num_class(out) : pointer to returned number of classes
 */
static int
get_num_requested_class (const char *input_filename, int *num_class)
{
  FILE *input_file;
  char buffer[DB_MAX_IDENTIFIER_LENGTH];

  if (input_filename == NULL || num_class == NULL)
    {
      return ER_FAILED;
    }

  input_file = fopen (input_filename, "r");
  if (input_file == NULL)
    {
      perror (input_filename);
      return ER_FAILED;
    }

  *num_class = 0;
  while (fgets ((char *) buffer, DB_MAX_IDENTIFIER_LENGTH,
		input_file) != NULL)
    {
      (*num_class)++;
    }

  fclose (input_file);

  return NO_ERROR;
}

/*
 * get_class_mops - Get the list of mops of specified classes
 *    return: error code
 *    class_names(in): the names of the classes
 *    num_class(in): the number of classes
 *    class_list(out): pointer to returned list of mops
 *    num_class_list(out): pointer to returned number of mops
 */
static int
get_class_mops (char **class_names, int num_class,
		MOP ** class_list, int *num_class_list)
{
  int i, status = NO_ERROR;
  char downcase_class_name[SM_MAX_IDENTIFIER_LENGTH];
  DB_OBJECT *class_ = NULL;

  if (class_names == NULL || num_class <= 0 || class_list == NULL ||
      num_class_list == NULL)
    {
      return ER_FAILED;
    }

  *num_class_list = 0;
  *class_list = (DB_OBJECT **) malloc (DB_SIZEOF (DB_OBJECT *) * (num_class));
  if (*class_list == NULL)
    {
      return ER_FAILED;
    }

  for (i = 0; i < num_class; ++i)
    {
      (*class_list)[i] = NULL;
    }

  for (i = 0; i < num_class; i++)
    {
      if (class_names[i] == NULL || strlen (class_names[i]) == 0)
	{
	  status = ER_FAILED;
	  goto error;
	}

      sm_downcase_name (class_names[i], downcase_class_name,
			SM_MAX_IDENTIFIER_LENGTH);

      class_ = locator_find_class (downcase_class_name);
      if (class_ != NULL)
	{
	  (*class_list)[(*num_class_list)] = class_;
	  (*num_class_list)++;
	}
      else
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_CLASS), downcase_class_name);

	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_INVALID_CLASS));
	}
    }

  return status;

error:
  if (*class_list)
    {
      free (*class_list);
      *class_list = NULL;
    }

  if (num_class_list)
    {
      *num_class_list = 0;
    }

  return status;
}

/*
 * get_class_mops_from_file - Get a list of class mops from specified file
 *    return: error code
 *    input_filename(in): input file name
 *    class_list(out): pointer to returned list of class mops
 *    num_class_mops(out): pointer to returned number of mops
 */
static int
get_class_mops_from_file (const char *input_filename, MOP ** class_list,
			  int *num_class_mops)
{
  int status = NO_ERROR;
  int i = 0, j = 0;
  FILE *input_file;
  char buffer[DB_MAX_IDENTIFIER_LENGTH];
  char **class_names = NULL;
  int num_class = 0;
  int len = 0;
  char *ptr = NULL;

  if (input_filename == NULL || class_list == NULL || num_class_mops == NULL)
    {
      return ER_FAILED;
    }

  *class_list = NULL;
  *num_class_mops = 0;
  if (get_num_requested_class (input_filename, &num_class) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (num_class == 0)
    {
      return ER_FAILED;
    }

  input_file = fopen (input_filename, "r");
  if (input_file == NULL)
    {
      perror (input_filename);
      return ER_FAILED;
    }

  class_names = (char **) malloc (DB_SIZEOF (char *) * num_class);
  if (class_names == NULL)
    {
      return ER_FAILED;
    }
  for (i = 0; i < num_class; i++)
    {
      class_names[i] = NULL;
    }

  for (i = 0; i < num_class; ++i)
    {
      if (fgets ((char *) buffer, DB_MAX_IDENTIFIER_LENGTH, input_file) ==
	  NULL)
	{
	  status = ER_FAILED;
	  goto end;
	}

      ptr = strchr (buffer, '\n');
      if (ptr)
	{
	  len = ptr - buffer;
	}
      else
	{
	  len = strlen (buffer);
	}

      if (len < 1)
	{
	  status = ER_FAILED;
	  goto end;
	}

      class_names[i] = (char *) malloc (DB_SIZEOF (char) * (len + 1));
      if (class_names[i] == NULL)
	{
	  status = ER_FAILED;
	  goto end;
	}

      strncpy (class_names[i], buffer, len);
      class_names[i][len] = 0;
    }

  status = get_class_mops (class_names, num_class, class_list,
			   num_class_mops);

end:

  if (input_file)
    {
      fclose (input_file);
    }

  if (class_names)
    {
      for (i = 0; i < num_class; i++)
	{
	  if (class_names[i] != NULL)
	    {
	      free (class_names[i]);
	      class_names[i] = NULL;
	    }
	}

      free (class_names);
      class_names = NULL;
    }

  return status;
}

/*
 * find_oid - search an oid in a list of oids
 *    return: the index of the oid on success, -1 otherwise
 *    oid(in): the searched oid
 *    oids_list(in): the list where oid is searched
 *    num_class_mops(in): the length of oids_list
 */
static int
find_oid (OID * oid, OID ** oids_list, int num_oids)
{
  int i;
  if (oid == NULL || oids_list == NULL || num_oids <= 0)
    {
      return -1;
    }

  for (i = 0; i < num_oids; i++)
    {
      if (oids_list[i] == NULL)
	{
	  continue;
	}

      if (OID_EQ (oid, oids_list[i]))
	{
	  return i;
	}
    }

  return -1;
}

/*
 * show_statistics - show the statistics for specified class oids
 *    return:
 *    class_oid(in) : class oid
 *    unlocked_class(in) : true if the class was not locked
 *    valid_class(in): true if the class was valid
 *    processed_class(in): true if the class was processed
 *    total_objects(in): total class objects
 *    failed_objects(in): failed class objects
 *    modified_objects(in): modified class objects
 *    big_objects(in): big class objects
 *    delete_old_repr_flag: delete old representation flag
 *    old_repr_deleted(in): old class representations removed from catalog
 */
static void
show_statistics (OID * class_oid,
		 bool unlocked_class,
		 bool valid_class, bool processed_class,
		 int total_objects, int failed_objects,
		 int modified_objects, int big_objects,
		 bool delete_old_repr_flag, bool old_repr_deleted)
{
  MOP class_mop = NULL;
  char *temp_class_name;

  class_mop = db_object (class_oid);
  if (class_mop == NULL)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_INVALID_CLASS));
      return;
    }

  temp_class_name = (char *) db_get_class_name (class_mop);
  if (temp_class_name == NULL || strlen (temp_class_name) == 0)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_UNKNOWN_CLASS_NAME));
    }
  else
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_CLASS), temp_class_name);
    }

  if (!valid_class)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_INVALID_CLASS));

      return;
    }

  if (!processed_class)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PROCESS_CLASS_ERROR));

    }

  if (!unlocked_class)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_LOCKED_CLASS));
    }

  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB,
			  COMPACTDB_MSG_TOTAL_OBJECTS), total_objects);

  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB,
			  COMPACTDB_MSG_FAILED_OBJECTS), failed_objects);

  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB,
			  COMPACTDB_MSG_MODIFIED_OBJECTS), modified_objects);

  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			  MSGCAT_UTIL_SET_COMPACTDB,
			  COMPACTDB_MSG_BIG_OBJECTS), big_objects);

  if (delete_old_repr_flag)
    {
      if (old_repr_deleted)
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_REPR_DELETED));
	}
      else
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_REPR_CANT_DELETE));
	}
    }
}

/*
 * get_name_from_class_oid - get the name of the class from class oid
 *    return:  the name of the class
 *    class_oid(in) : class oid
 */
static char *
get_name_from_class_oid (OID * class_oid)
{
  MOP class_mop = NULL;
  char *temp_class_name;
  char *result;

  if (!class_oid)
    {
      return NULL;
    }

  class_mop = db_object (class_oid);
  if (class_mop == NULL)
    {
      return NULL;
    }

  temp_class_name = (char *) db_get_class_name (class_mop);
  if (temp_class_name == NULL)
    {
      return NULL;
    }

  result = (char *) malloc (sizeof (char) * (strlen (temp_class_name) + 1));
  if (result == NULL)
    {
      return NULL;
    }

  strcpy (result, temp_class_name);

  return result;
}

/*
 * compactdb_start - Compact classes
 *    return: error code
 *    verbose_flag(in):
 *    delete_old_repr_flag(in): delete old class representations from catalog
 *    input_filename(in): classes file name
 *    input_class_names(in): classes list
 *    input_class_length(in): classes list length
 *    max_processed_space(in): maximum space to process for one iteration
 */
static int
compactdb_start (bool verbose_flag, bool delete_old_repr_flag,
		 char *input_filename,
		 char **input_class_names, int input_class_length,
		 int max_processed_space, int instance_lock_timeout,
		 int class_lock_timeout, DB_TRAN_ISOLATION tran_isolation)
{
  int status = NO_ERROR;
  OID **class_oids = NULL, *next_oid = NULL;
  int i, num_classes = 0;
  LIST_MOPS *class_table = NULL;
  OID last_processed_class_oid, last_processed_oid;
  int *total_objects = NULL, *iteration_total_objects = NULL;
  int *failed_objects = NULL, *iteration_failed_objects = NULL;
  int *modified_objects = NULL, *iteration_modified_objects = NULL;
  char *incomplete_processing = NULL;
  int *big_objects = NULL, *iteration_big_objects = NULL;
  int *initial_last_repr = NULL;
  MOP *class_mops = NULL;
  int last_completed_class_index, temp_index;
  int num_class_mops = 0;
  SM_CLASS *class_ptr = NULL;
  int num_classes_fully_compacted = 0;
  char *class_name = NULL;
  MOP *processed_class_mops = NULL;
  MOBJ *obj_ptr = NULL;

  if (input_filename && input_class_names && input_class_length > 0)
    {
      return ER_FAILED;
    }

  status = compact_db_start ();
  if (status != NO_ERROR)
    {
      if (status == ER_COMPACTDB_ALREADY_STARTED)
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_ALREADY_STARTED));
	}

      return ER_FAILED;
    }

  tran_reset_wait_times (class_lock_timeout);

  if (input_class_names && input_class_length > 0)
    {
      status = get_class_mops (input_class_names, input_class_length,
			       &class_mops, &num_class_mops);
      if (status != NO_ERROR)
	{
	  goto error;
	}
    }
  else if (input_filename)
    {
      status = get_class_mops_from_file (input_filename, &class_mops,
					 &num_class_mops);
      if (status != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      class_table =
	locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_READ);
      if (!class_table)
	{
	  status = ER_FAILED;
	  goto error;
	}

      class_mops = class_table->mops;
      num_class_mops = class_table->num;
    }

  class_oids = (OID **) malloc (DB_SIZEOF (OID *) * (num_class_mops));
  if (class_oids == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  for (i = 0; i < num_class_mops; i++)
    {
      class_oids[i] = NULL;
    }

  processed_class_mops = (DB_OBJECT **) malloc (DB_SIZEOF (DB_OBJECT *) *
						(num_class_mops));
  if (processed_class_mops == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  for (i = 0; i < num_class_mops; i++)
    {
      processed_class_mops[i] = NULL;
    }

  num_classes = 0;
  for (i = 0; i < num_class_mops; i++)
    {
      obj_ptr = (void *) &class_ptr;
      ws_find (class_mops[i], obj_ptr);
      if (class_ptr == NULL)
	{
	  continue;
	}

      if (class_ptr->class_type != SM_CLASS_CT)
	{
	  continue;
	}

      class_oids[num_classes] = ws_oid (class_mops[i]);
      if (class_oids[num_classes] != NULL)
	{
	  processed_class_mops[num_classes] = class_mops[i];
	  num_classes++;
	}
    }

  if (num_classes == 0)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_NOTHING_TO_PROCESS));
      goto error;
    }

  total_objects = (int *) malloc (num_classes * sizeof (int));
  if (total_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  iteration_total_objects = (int *) malloc (num_classes * sizeof (int));
  if (iteration_total_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  failed_objects = (int *) malloc (num_classes * sizeof (int));
  if (failed_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  iteration_failed_objects = (int *) malloc (num_classes * sizeof (int));
  if (iteration_failed_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  modified_objects = (int *) malloc (num_classes * sizeof (int));
  if (modified_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  iteration_modified_objects = (int *) malloc (num_classes * sizeof (int));
  if (iteration_modified_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  big_objects = (int *) malloc (num_classes * sizeof (int));
  if (big_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  iteration_big_objects = (int *) malloc (num_classes * sizeof (int));
  if (iteration_big_objects == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  initial_last_repr = (int *) malloc (num_classes * sizeof (int));
  if (initial_last_repr == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  incomplete_processing = (char *) malloc (num_classes * sizeof (char));
  if (incomplete_processing == NULL)
    {
      status = ER_FAILED;
      goto error;
    }

  for (i = 0; i < num_classes; i++)
    {
      total_objects[i] = 0;
      failed_objects[i] = 0;
      modified_objects[i] = 0;
      big_objects[i] = 0;
      incomplete_processing[i] = 0;
      initial_last_repr[i] = NULL_REPRID;
    }

  for (i = 0; i < num_class_mops; i++)
    {
      status = locator_flush_all_instances (class_mops[i],
					    DECACHE, LC_STOP_ON_ERROR);
      if (status != NO_ERROR)
	{
	  goto error;
	}
    }

  status = db_commit_transaction ();
  if (status != NO_ERROR)
    {
      goto error;
    }

  COPY_OID (&last_processed_class_oid, class_oids[0]);
  OID_SET_NULL (&last_processed_oid);
  temp_index = -1;
  last_completed_class_index = -1;

  if (verbose_flag)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PASS1));
    }

  while (true)
    {
      status = db_set_isolation (tran_isolation);
      if (status != NO_ERROR)
	{
	  if (verbose_flag)
	    {
	      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_COMPACTDB,
				      COMPACTDB_MSG_ISOLATION_LEVEL_FAILURE));
	    }

	  status = ER_FAILED;
	  goto error;
	}

      status = boot_compact_classes (class_oids, num_classes,
				     max_processed_space,
				     instance_lock_timeout,
				     class_lock_timeout,
				     delete_old_repr_flag,
				     &last_processed_class_oid,
				     &last_processed_oid,
				     iteration_total_objects,
				     iteration_failed_objects,
				     iteration_modified_objects,
				     iteration_big_objects,
				     initial_last_repr);

      if (OID_ISNULL (&last_processed_class_oid))
	{
	  temp_index = num_classes;
	}
      else
	{
	  temp_index = find_oid (&last_processed_class_oid,
				 class_oids, num_classes);
	}

      switch (status)
	{
	case NO_ERROR:
	  if (delete_old_repr_flag &&
	      temp_index - 1 > last_completed_class_index)
	    {
	      for (i = last_completed_class_index + 1; i < temp_index; i++)
		{
		  if (initial_last_repr[i] == COMPACTDB_REPR_DELETED)
		    {
		      sm_destroy_representations (processed_class_mops[i]);
		    }
		}
	    }

	  status = db_commit_transaction ();
	  if (status != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	case ER_LK_UNILATERALLY_ABORTED:
	  status = tran_abort_only_client (false);
	  if (status != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	case ER_FAILED:
	  status = db_abort_transaction ();
	  if (status != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	default:
	  db_abort_transaction ();
	  status = ER_FAILED;
	  goto error;
	}

      for (i = 0; i < num_classes; i++)
	{
	  if (iteration_total_objects[i] >= 0)
	    {
	      total_objects[i] += iteration_total_objects[i];
	      failed_objects[i] += iteration_failed_objects[i];
	      modified_objects[i] += iteration_modified_objects[i];
	      big_objects[i] += iteration_big_objects[i];
	    }
	  else
	    {
	      incomplete_processing[i] = iteration_total_objects[i];
	    }
	}

      if (temp_index - 1 > last_completed_class_index)
	{
	  for (i = last_completed_class_index + 1; i < temp_index; i++)
	    {
	      status = db_set_isolation (tran_isolation);
	      if (status != NO_ERROR)
		{
		  if (verbose_flag)
		    {
		      printf (msgcat_message
			      (MSGCAT_CATALOG_UTILS,
			       MSGCAT_UTIL_SET_COMPACTDB,
			       COMPACTDB_MSG_ISOLATION_LEVEL_FAILURE));
		    }

		  status = ER_FAILED;
		  goto error;
		}

	      tran_reset_wait_times (class_lock_timeout);

	      show_statistics
		(class_oids[i],
		 incomplete_processing[i] != COMPACTDB_LOCKED_CLASS,
		 incomplete_processing[i] != COMPACTDB_INVALID_CLASS,
		 incomplete_processing[i] != COMPACTDB_UNPROCESSED_CLASS,
		 total_objects[i], failed_objects[i],
		 modified_objects[i], big_objects[i],
		 delete_old_repr_flag,
		 initial_last_repr[i] == COMPACTDB_REPR_DELETED);

	      db_commit_transaction ();
	    }

	  last_completed_class_index = temp_index - 1;
	}

      if (OID_ISNULL (&last_processed_class_oid))
	{
	  break;
	}
    }

  if (verbose_flag)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PASS2));
    }
  status = do_reclaim_addresses ((const OID ** const) class_oids, num_classes,
				 &num_classes_fully_compacted, verbose_flag,
				 class_lock_timeout);
  if (status != NO_ERROR)
    {
      goto error;
    }

  if (verbose_flag)
    {
      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
			      MSGCAT_UTIL_SET_COMPACTDB,
			      COMPACTDB_MSG_PASS3));
    }

  for (i = 0; i < num_classes; i++)
    {
      status = db_set_isolation (tran_isolation);
      if (status != NO_ERROR)
	{
	  if (verbose_flag)
	    {
	      printf (msgcat_message
		      (MSGCAT_CATALOG_UTILS,
		       MSGCAT_UTIL_SET_COMPACTDB,
		       COMPACTDB_MSG_ISOLATION_LEVEL_FAILURE));
	    }

	  status = ER_FAILED;
	  goto error;
	}

      tran_reset_wait_times (class_lock_timeout);

      status = boot_heap_compact (class_oids[i]);
      switch (status)
	{
	case NO_ERROR:
	  status = db_commit_transaction ();
	  if (status != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	case ER_LK_UNILATERALLY_ABORTED:
	  status = tran_abort_only_client (false);
	  if (status != NO_ERROR)
	    {
	      goto error;
	    }
	  break;

	default:
	  status = db_abort_transaction ();
	  if (status != NO_ERROR)
	    {
	      goto error;
	    }
	  break;
	}

      class_name = get_name_from_class_oid (class_oids[i]);
      if (class_name == NULL)
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_UNKNOWN_CLASS_NAME));
	}
      else
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_CLASS), class_name);
	}

      if (status != NO_ERROR)
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_HEAP_COMPACT_FAILED));
	}
      else
	{
	  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				  MSGCAT_UTIL_SET_COMPACTDB,
				  COMPACTDB_MSG_HEAP_COMPACT_SUCCEEDED));
	}

      if (class_name)
	{
	  free (class_name);
	  class_name = NULL;
	}

      db_commit_transaction ();
    }

error:
  if (class_oids)
    {
      free_and_init (class_oids);
    }

  if (processed_class_mops)
    {
      free_and_init (processed_class_mops);
    }

  if (total_objects)
    {
      free_and_init (total_objects);
    }

  if (iteration_total_objects)
    {
      free_and_init (iteration_total_objects);
    }

  if (failed_objects)
    {
      free_and_init (failed_objects);
    }

  if (iteration_failed_objects)
    {
      free_and_init (iteration_failed_objects);
    }

  if (modified_objects)
    {
      free_and_init (modified_objects);
    }

  if (iteration_modified_objects)
    {
      free_and_init (iteration_modified_objects);
    }

  if (big_objects)
    {
      free_and_init (big_objects);
    }

  if (iteration_big_objects)
    {
      free_and_init (iteration_big_objects);
    }

  if (initial_last_repr)
    {
      free_and_init (initial_last_repr);
    }

  if (incomplete_processing)
    {
      free_and_init (incomplete_processing);
    }

  if (class_table)
    {
      locator_free_list_mops (class_table);
    }
  else
    {
      if (class_mops)
	{
	  for (i = 0; i < num_class_mops; i++)
	    {
	      class_mops[i] = NULL;
	    }

	  free_and_init (class_mops);
	}
    }

  compact_db_stop ();

  return status;
}

/*
 * compactdb - compactdb main routine
 *    return: 0 if successful, error code otherwise
 *    arg(in): a map of command line arguments
 */
int
compactdb (UTIL_FUNCTION_ARG * arg)
{
  UTIL_ARG_MAP *arg_map = arg->arg_map;
  const char *exec_name = arg->command_name;
  int error;
  int i, status = 0;
  const char *database_name;
  bool verbose_flag = 0, delete_old_repr_flag = 0;
  char *input_filename = NULL;
  DB_OBJECT **req_class_table = NULL;
  LIST_MOPS *all_class_table = NULL;
  int maximum_processed_space = 10 * DB_PAGESIZE, pages;
  int instance_lock_timeout, class_lock_timeout;
  char **tables = NULL;
  int table_size = 0;

  database_name =
    utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  verbose_flag = utility_get_option_bool_value (arg_map, COMPACT_VERBOSE_S);
  input_filename =
    utility_get_option_string_value (arg_map, COMPACT_INPUT_CLASS_FILE_S, 0);

  pages =
    utility_get_option_int_value (arg_map, COMPACT_PAGES_COMMITED_ONCE_S);
  if (pages < COMPACT_MIN_PAGES || pages > COMPACT_MAX_PAGES)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COMPACTDB,
				       COMPACTDB_MSG_FAILURE));
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS,
			       MSGCAT_UTIL_SET_COMPACTDB,
			       COMPACTDB_MSG_OUT_OF_RANGE_PAGES),
	       COMPACT_MIN_PAGES, COMPACT_MAX_PAGES);

      return ER_GENERIC_ERROR;
    }

  if (database_name == NULL || database_name[0] == '\0'
      || utility_get_option_string_table_size (arg_map) < 1)
    {
      compactdb_usage (arg->argv0);
      return ER_GENERIC_ERROR;
    }

  table_size = utility_get_option_string_table_size (arg_map);
  if (table_size > 1 && input_filename != NULL)
    {
      compactdb_usage (arg->argv0);
      return ER_GENERIC_ERROR;
    }

  instance_lock_timeout = utility_get_option_int_value
    (arg_map, COMPACT_INSTANCE_LOCK_TIMEOUT_S);
  if (instance_lock_timeout < COMPACT_INSTANCE_MIN_LOCK_TIMEOUT ||
      instance_lock_timeout > COMPACT_INSTANCE_MAX_LOCK_TIMEOUT)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COMPACTDB,
				       COMPACTDB_MSG_FAILURE));

      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS,
			       MSGCAT_UTIL_SET_COMPACTDB,
			       COMPACTDB_MSG_OUT_OF_RANGE_INSTANCE_LOCK_TIMEOUT),
	       COMPACT_INSTANCE_MIN_LOCK_TIMEOUT,
	       COMPACT_INSTANCE_MAX_LOCK_TIMEOUT);

      return ER_GENERIC_ERROR;
    }

  class_lock_timeout = utility_get_option_int_value
    (arg_map, COMPACT_CLASS_LOCK_TIMEOUT_S);
  if (class_lock_timeout < COMPACT_CLASS_MIN_LOCK_TIMEOUT ||
      class_lock_timeout > COMPACT_CLASS_MAX_LOCK_TIMEOUT)
    {
      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
				       MSGCAT_UTIL_SET_COMPACTDB,
				       COMPACTDB_MSG_FAILURE));

      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_UTILS,
			       MSGCAT_UTIL_SET_COMPACTDB,
			       COMPACTDB_MSG_OUT_OF_RANGE_CLASS_LOCK_TIMEOUT),
	       COMPACT_CLASS_MIN_LOCK_TIMEOUT,
	       COMPACT_CLASS_MAX_LOCK_TIMEOUT);

      return ER_GENERIC_ERROR;
    }

  delete_old_repr_flag =
    utility_get_option_bool_value (arg_map, COMPACT_DELETE_OLD_REPR_S);

  maximum_processed_space = pages * DB_PAGESIZE;

  if (table_size > 1)
    {
      tables = (char **) malloc (sizeof (char *) * table_size - 1);
      if (tables == NULL)
	{
	  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					   MSGCAT_UTIL_SET_COMPACTDB,
					   COMPACTDB_MSG_FAILURE));
	  return ER_GENERIC_ERROR;
	}

      for (i = 1; i < table_size; i++)
	{
	  tables[i - 1] = utility_get_option_string_value
	    (arg_map, OPTION_STRING_TABLE, i);
	}
    }

  sysprm_set_force (prm_get_name (PRM_ID_JAVA_STORED_PROCEDURE), "no");

  AU_DISABLE_PASSWORDS ();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  if ((error = db_login ("DBA", NULL))
      || (error = db_restart (arg->argv0, TRUE, database_name)))
    {
      fprintf (stderr, "%s: %s.\n\n", exec_name, db_error_string (3));
      status = error;
    }
  else
    {
      status = db_set_isolation (TRAN_REP_CLASS_UNCOMMIT_INSTANCE);
      if (status == NO_ERROR)
	{
	  if (class_lock_timeout > 0)
	    {
	      class_lock_timeout = class_lock_timeout * 1000;
	    }
	  if (instance_lock_timeout > 0)
	    {
	      instance_lock_timeout = instance_lock_timeout * 1000;
	    }
	  status = compactdb_start (verbose_flag, delete_old_repr_flag,
				    input_filename, tables, table_size - 1,
				    maximum_processed_space,
				    instance_lock_timeout,
				    class_lock_timeout,
				    TRAN_REP_CLASS_UNCOMMIT_INSTANCE);

	  if (status == ER_FAILED)
	    {
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_COMPACTDB,
					       COMPACTDB_MSG_FAILURE));
	    }
	}

      db_shutdown ();
    }

  if (tables)
    {
      free (tables);
      tables = NULL;
    }
  return status;
}

static int
do_reclaim_addresses (const OID ** const class_oids, const int num_class_oids,
		      int *const num_classes_fully_processed,
		      const bool verbose, const int class_lock_timeout)
{
  bool any_class_can_be_referenced = false;
  int i = 0;
  int error_code = NO_ERROR;

  assert (num_class_oids >= 0);
  assert (class_oids != NULL);

  assert (num_classes_fully_processed != NULL);
  *num_classes_fully_processed = 0;

  tran_reset_wait_times (class_lock_timeout);

  for (i = 0; i < num_class_oids; ++i)
    {
      char *class_name = NULL;
      int error_while_processing = NO_ERROR;
      bool correctly_processed = false;
      bool addresses_reclaimed = false;

      error_code = do_reclaim_class_addresses (*class_oids[i], &class_name,
					       &any_class_can_be_referenced,
					       &correctly_processed,
					       &addresses_reclaimed,
					       &error_while_processing);
      if (correctly_processed)
	{
	  assert (error_code == NO_ERROR);
	  assert (error_while_processing == NO_ERROR);
	  (*num_classes_fully_processed)++;
	}

      if (verbose)
	{
	  if (class_name == NULL || strlen (class_name) == 0)
	    {
	      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_COMPACTDB,
				      COMPACTDB_MSG_UNKNOWN_CLASS_NAME));
	    }
	  else
	    {
	      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_COMPACTDB,
				      COMPACTDB_MSG_CLASS), class_name);
	    }
	  if (correctly_processed)
	    {
	      if (addresses_reclaimed)
		{
		  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_COMPACTDB,
					  COMPACTDB_MSG_RECLAIMED));
		}
	      else
		{
		  printf (msgcat_message (MSGCAT_CATALOG_UTILS,
					  MSGCAT_UTIL_SET_COMPACTDB,
					  COMPACTDB_MSG_RECLAIM_SKIPPED));
		}
	    }
	  else
	    {
	      printf (msgcat_message (MSGCAT_CATALOG_UTILS,
				      MSGCAT_UTIL_SET_COMPACTDB,
				      COMPACTDB_MSG_RECLAIM_ERROR));
	    }
	}

      if (class_name != NULL)
	{
	  free_and_init (class_name);
	}
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
    }

  return error_code;

error_exit:
  return error_code;
}

static int
is_not_system_class (MOBJ class_)
{
  return !(((SM_CLASS *) class_)->flags & SM_CLASSFLAG_SYSTEM);
}

static int
do_reclaim_class_addresses (const OID class_oid, char **class_name,
			    bool * const any_class_can_be_referenced,
			    bool * const correctly_processed,
			    bool * const addresses_reclaimed,
			    int *const error_while_processing)
{
  DB_OBJECT *class_mop = NULL;
  DB_OBJECT *parent_mop = NULL;
  SM_CLASS *class_ = NULL;
  SM_CLASS *parent_class_ = NULL;
  int error_code = NO_ERROR;
  int skipped_error_code = NO_ERROR;
  bool do_abort_on_error = true;
  bool can_reclaim_addresses = true;
  LIST_MOPS *lmops = NULL;
  HFID *hfid = NULL;

  assert (!OID_ISNULL (&class_oid));
  assert (any_class_can_be_referenced != NULL);
  assert (correctly_processed != NULL);
  assert (addresses_reclaimed != NULL);
  assert (error_while_processing != NULL);
  assert (class_name != NULL);

  *correctly_processed = false;
  *addresses_reclaimed = false;
  *error_while_processing = NO_ERROR;

  error_code = db_commit_transaction ();
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  error_code = db_set_isolation (TRAN_READ_COMMITTED);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  /*
   * Trying to force an ISX_LOCK on the root class. It somehow happens that
   * we are left with an IX_LOCK in the end...
   */
  if (locator_fetch_class (sm_Root_class_mop, DB_FETCH_QUERY_WRITE) == NULL)
    {
      error_code = ER_FAILED;
      goto error_exit;
    }

  class_mop = db_object ((OID *) (&class_oid));
  if (class_mop == NULL)
    {
      skipped_error_code = ER_FAILED;
      goto error_exit;
    }

  if (!locator_is_class (class_mop, DB_FETCH_WRITE))
    {
      skipped_error_code = ER_FAILED;
      goto error_exit;
    }

  /*
   * We need an SCH-M lock on the class to process as early as possible so that
   * other transactions don't add references to it in the schema.
   */
  class_ = (SM_CLASS *) locator_fetch_class (class_mop, DB_FETCH_WRITE);
  if (class_ == NULL)
    {
      skipped_error_code = er_errid ();
      goto error_exit;
    }

  assert (*class_name == NULL);
  *class_name = strdup (class_->header.name);
  if (*class_name == NULL)
    {
      error_code = ER_FAILED;
      goto error_exit;
    }

  if (class_->partition_of != NULL)
    {
      /*
       * If the current class is a partition of a partitioned class we need
       * to get its parent partitioned table and check for references to its
       * parent too. If table tbl has partition tbl__p__p0, a reference to tbl
       * can point to tbl__p__p0 instances too.
       */
      skipped_error_code = do_get_partition_parent (class_mop, &parent_mop);
      if (skipped_error_code != NO_ERROR)
	{
	  goto error_exit;
	}
      if (parent_mop != NULL)
	{
	  parent_class_ =
	    (SM_CLASS *) locator_fetch_class (parent_mop, DB_FETCH_WRITE);
	  if (parent_class_ == NULL)
	    {
	      skipped_error_code = er_errid ();
	      goto error_exit;
	    }
	}
    }

  skipped_error_code =
    locator_flush_all_instances (class_mop, DECACHE, LC_STOP_ON_ERROR);
  if (skipped_error_code != NO_ERROR)
    {
      goto error_exit;
    }

  if (class_->class_type != SM_CLASS_CT)
    {
      can_reclaim_addresses = false;
    }
  else
    {
      hfid = sm_heap ((MOBJ) class_);
      if (HFID_IS_NULL (hfid))
	{
	  can_reclaim_addresses = false;
	}
    }

  if (class_->flags & SM_CLASSFLAG_SYSTEM)
    {
      /*
       * It should be safe to process system classes also but we skip them for
       * now. Please note that class_instances_can_be_referenced () does not
       * check for references from system classes.
       * If this is ever changed please consider the impact of reusing system
       * objects OIDs.
       */
      can_reclaim_addresses = false;
    }
  else if (class_->flags & SM_CLASSFLAG_REUSE_OID)
    {
      /*
       * Nobody should be able to hold references to reusable OID tables so it
       * should be safe to reclaim their OIDs and pages no matter what.
       */
      can_reclaim_addresses = true;
    }
  else
    {
      if (*any_class_can_be_referenced)
	{
	  /*
	   * Some class attribute has OBJECT or SET OF OBJECT as the domain.
	   * This means it can point to instances of any class so we're not
	   * safe reclaiming OIDs.
	   */
	  can_reclaim_addresses = false;
	}
      else
	{
	  bool class_can_be_referenced = false;

	  /*
	   * IS_LOCK should be enough for what we need but
	   * locator_get_all_class_mops seems to lock the instances with the
	   * lock that it has on their class. So we end up with IX_LOCK on all
	   * classes in the schema...
	   */

	  lmops = locator_get_all_class_mops (DB_FETCH_CLREAD_INSTREAD,
					      is_not_system_class);
	  if (lmops == NULL)
	    {
	      skipped_error_code = ER_FAILED;
	      goto error_exit;
	    }

	  skipped_error_code =
	    class_instances_can_be_referenced (class_mop, parent_mop,
					       &class_can_be_referenced,
					       any_class_can_be_referenced,
					       lmops->mops, lmops->num);
	  if (skipped_error_code != NO_ERROR)
	    {
	      goto error_exit;
	    }
	  /*
	   * If some attribute has OBJECT or the current class as its domain
	   * then it's not safe to reclaim the OIDs as some of the references
	   * might point to deleted objects. We skipped the system classes as
	   * they should not point to any instances of the non-system classes.
	   */
	  can_reclaim_addresses = !class_can_be_referenced &&
	    !*any_class_can_be_referenced;
	  if (lmops != NULL)
	    {
	      /*
	       * It should be safe now to release all the locks we hold on the
	       * schema classes (except for the X_LOCK on the current class).
	       * However, we don't currently have a way of releasing those
	       * locks so we're stuck with them till the end of the current
	       * transaction.
	       */
	      locator_free_list_mops (lmops);
	      lmops = NULL;
	    }
	}
    }

  if (can_reclaim_addresses)
    {
      assert (hfid != NULL && !HFID_IS_NULL (hfid));

      skipped_error_code = heap_reclaim_addresses (hfid);
      if (skipped_error_code != NO_ERROR)
	{
	  goto error_exit;
	}
      *addresses_reclaimed = true;
    }

  error_code = db_commit_transaction ();
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }

  assert (error_code == NO_ERROR && skipped_error_code == NO_ERROR);
  *correctly_processed = true;
  class_mop = NULL;
  class_ = NULL;
  parent_mop = NULL;
  parent_class_ = NULL;
  return error_code;

error_exit:
  *error_while_processing = skipped_error_code;
  class_mop = NULL;
  class_ = NULL;
  parent_mop = NULL;
  parent_class_ = NULL;
  if (lmops != NULL)
    {
      locator_free_list_mops (lmops);
      lmops = NULL;
    }
  if (do_abort_on_error)
    {
      int tmp_error_code = NO_ERROR;

      if (skipped_error_code == ER_LK_UNILATERALLY_ABORTED ||
	  error_code == ER_LK_UNILATERALLY_ABORTED)
	{
	  tmp_error_code = tran_abort_only_client (false);
	}
      else
	{
	  tmp_error_code = db_abort_transaction ();
	}
      if (tmp_error_code != NO_ERROR)
	{
	  if (error_code == NO_ERROR)
	    {
	      error_code = tmp_error_code;
	    }
	}
    }
  if (skipped_error_code == NO_ERROR && error_code == NO_ERROR)
    {
      error_code = ER_FAILED;
    }
  return error_code;
}

static int
class_instances_can_be_referenced (MOP mop, MOP parent_mop,
				   bool * const class_can_be_referenced,
				   bool * const any_class_can_be_referenced,
				   MOP * const all_mops, const int num_mops)
{
  int error_code = NO_ERROR;
  int i = 0;

  assert (mop != NULL);
  assert (class_can_be_referenced != NULL);
  assert (any_class_can_be_referenced != NULL);
  assert (all_mops != NULL);
  assert (num_mops > 0);

  *class_can_be_referenced = false;

  for (i = 0; i < num_mops; ++i)
    {
      error_code = class_referenced_by_class (mop, parent_mop, all_mops[i],
					      class_can_be_referenced,
					      any_class_can_be_referenced);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
      if (*any_class_can_be_referenced)
	{
	  break;
	}
    }

  return error_code;

error_exit:
  return error_code;
}

static int
class_referenced_by_class (MOP referenced_mop, MOP parent_mop,
			   MOP referring_mop,
			   bool * const class_can_be_referenced,
			   bool * const any_class_can_be_referenced)
{
  SM_CLASS *referring_class = NULL;
  int error_code = NO_ERROR;
  SM_ATTRIBUTE *attributes_list = NULL;

  referring_class = (SM_CLASS *) locator_fetch_class (referring_mop,
						      DB_FETCH_READ);
  if (referring_class == NULL)
    {
      error_code = er_errid ();
      goto error_exit;
    }

  /*
   * System classes should not point to any instances of the non-system
   * classes.
   */
  if (referring_class->flags & SM_CLASSFLAG_SYSTEM)
    {
      goto end;
    }

  attributes_list = referring_class->attributes;
  if (referring_class->class_type == SM_CLASS_CT)
    {
      error_code = class_referenced_by_attributes (referenced_mop, parent_mop,
						   attributes_list,
						   class_can_be_referenced,
						   any_class_can_be_referenced);
      if (error_code != NO_ERROR)
	{
	  goto error_exit;
	}
      if (*any_class_can_be_referenced)
	{
	  goto end;
	}
    }
  else
    {
      /*
       * View attributes are not "real" references so we can safely ignore
       * them.
       */
    }

  attributes_list = referring_class->class_attributes;
  error_code = class_referenced_by_attributes (referenced_mop, parent_mop,
					       attributes_list,
					       class_can_be_referenced,
					       any_class_can_be_referenced);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }
  if (*any_class_can_be_referenced)
    {
      goto end;
    }

  attributes_list = referring_class->shared;
  error_code = class_referenced_by_attributes (referenced_mop, parent_mop,
					       attributes_list,
					       class_can_be_referenced,
					       any_class_can_be_referenced);
  if (error_code != NO_ERROR)
    {
      goto error_exit;
    }
  if (*any_class_can_be_referenced)
    {
      goto end;
    }

end:
  return error_code;

error_exit:
  if (error_code == NO_ERROR)
    {
      error_code = ER_FAILED;
    }
  return error_code;
}

static int
class_referenced_by_attributes (MOP referenced_class, MOP parent_mop,
				SM_ATTRIBUTE * const attributes_list,
				bool * const class_can_be_referenced,
				bool * const any_class_can_be_referenced)
{
  SM_ATTRIBUTE *crt_attr = NULL;

  for (crt_attr = attributes_list; crt_attr != NULL;
       crt_attr = (SM_ATTRIBUTE *) crt_attr->header.next)
    {
      class_referenced_by_domain (referenced_class, crt_attr->domain,
				  class_can_be_referenced,
				  any_class_can_be_referenced);
      if (*any_class_can_be_referenced)
	{
	  goto end;
	}
      if (parent_mop != NULL)
	{
	  class_referenced_by_domain (parent_mop, crt_attr->domain,
				      class_can_be_referenced,
				      any_class_can_be_referenced);
	  if (*any_class_can_be_referenced)
	    {
	      goto end;
	    }
	}
    }

end:
  return NO_ERROR;
}

static void
class_referenced_by_domain (MOP referenced_class,
			    TP_DOMAIN * const domain,
			    bool * const class_can_be_referenced,
			    bool * const any_class_can_be_referenced)
{
  TP_DOMAIN *crt_domain = NULL;

  assert (domain != NULL);

  for (crt_domain = domain; crt_domain != NULL;
       crt_domain = db_domain_next (crt_domain))
    {
      const DB_TYPE type = TP_DOMAIN_TYPE (crt_domain);

      if (type == DB_TYPE_OBJECT)
	{
	  DB_OBJECT *class_ = db_domain_class (crt_domain);
	  if (class_ == NULL)
	    {
	      *any_class_can_be_referenced = true;
	    }
	  else if (referenced_class == class_ ||
		   db_is_subclass (referenced_class, class_))
	    {
	      *class_can_be_referenced = true;
	    }
	  else
	    {
	      /* Cannot reference instances of the given class. */
	    }
	}
      else if (pr_is_set_type (type))
	{
	  class_referenced_by_domain (referenced_class,
				      db_domain_set (crt_domain),
				      class_can_be_referenced,
				      any_class_can_be_referenced);
	}
      else
	{
	  /* Cannot reference an object. */
	}

      if (*any_class_can_be_referenced)
	{
	  return;
	}
    }
}
