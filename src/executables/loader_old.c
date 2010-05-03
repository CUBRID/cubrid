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
 * loader_old.c - database loader
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "area_alloc.h"
#include "dbtype.h"
#include "class_object.h"
#include "object_domain.h"
#include "schema_manager.h"
#include "object_accessor.h"
#include "authenticate.h"
#include "elo_class.h"
#include "set_object.h"
#include "db.h"
#include "loader_object_table.h"
#include "loader_disk.h"
#include "loader_old.h"
#include "oid.h"
#include "locator_cl.h"		/* for locator_flush_instance */
#include "message_catalog.h"
#include "utility.h"
#include "memory_alloc.h"
#include "server_interface.h"
#include "environment_variable.h"
#include "object_print.h"

/* this must be the last header file included!!! */
#include "dbval.h"

extern void display_error_line ();

#if 0
extern int get_error_line ();
extern const char *Unique_violation_dump;
extern FILE *uvd_fp;
#endif
extern bool No_oid_hint;

#define LDR_MAX_ARGS 32
#define LDR_ATT_GROW_SIZE 128
#define LDR_ARG_GROW_SIZE 128
#define ENV_LOADDB_STATUS "LOADDB_STATUS"

/*
 * LOADER_STATE
 *    Run time state structure for the loader.
 *    This essentially encapsulates all the global variables that
 *    would normally be used.
 *    The code is still written to assume a global Loader object, but
 *    if we have to support concurrent loader sessions, it wouldn't
 *    be too hard.
 */
typedef struct loader_state
{

  int valid;

  char name[SM_MAX_IDENTIFIER_LENGTH];
  MOP class;
  CLASS_TABLE *table;
  int inum;
  int icount;

  /* bound attribute state */
  int att_count;
  int maxatt;
  SM_ATTRIBUTE **atts;

  /* Optional constructor state.
     Bypass the outer SM_METHOD structure
     and point directly at the sincle signature since we can
     have only one.  When we support overloading, this will have to
     be more complicated.
   */
  SM_METHOD *constructor;
  int arg_count;
  int maxarg;
  SM_METHOD_ARGUMENT **args;

  /* includes both attribute & argument values */
  int value_count;
  int maxval;
  DB_VALUE *values;
#if 0
  DB_GC_CALLBACK_TICKET ticket;
#endif

  /* temporary state during set population */
  DB_SET *set;
  TP_DOMAIN *set_domain;

  unsigned int instance_started:1;
  unsigned int validation_only:1;
  unsigned int verbose:1;
  unsigned int inhibit_instances:1;

  /* attribute type restriction */
  int attribute_type;
  int updates_on_the_class;	/* counter of class/shared/default updates */

  /* statistics */
  int default_count;
  int errors;

  /* periodic commit state */
  int periodic_commit;
  int commit_counter;

  /* periodic instance counter status */
  int status_count;
  int status_counter;

} LOADER_STATE;

/* global loader state */
static LOADER_STATE Loader;

/* global total object count */
static int Total_objects = 0;

/* Post commit callback function. Called with number of instances committed. */
static LDR_POST_COMMIT_HANDLER ldr_post_commit_handler = NULL;

/* Post commit interrupt handler. Called with number of instances committed. */
static LDR_POST_COMMIT_HANDLER ldr_post_interrupt_handler = NULL;

/*
 *    Global list of internal_classes.
 *    These are the classes that we don't allow to be loaded.
 *    Initialized by clist_init().
 */
static DB_OBJLIST *internal_classes = NULL;

/*
 *    Global flag which is turned on when an interrupt is received, via
 *    ldr_interrupt_has_occurred()
 *    Values : LDR_NO_INTERRUPT
 *             LDR_STOP_AND_ABORT_INTERRUPT
 *             LDR_STOP_AND_COMMIT_INTERRUPT
 */

int ldr_Load_interrupted = LDR_NO_INTERRUPT;
static jmp_buf *ldr_Jmp_buf = NULL;

static int default_count = 0;

/* The global class id map. */
static MOP *Id_map = NULL;
static int Id_map_size = 0;



static int get_domain (TP_DOMAIN ** domain_ptr);
static int domain_error (TP_DOMAIN * domain, DB_TYPE src_type, MOP class);
static int check_domain (DB_TYPE src_type, DB_TYPE * appropriate_type);
static int check_object_domain (MOP class, MOP * actual_class);
static int check_class_domain (void);
static int clist_init (void);
static void clist_final (void);
static int is_internal_class (MOP class);
static int check_commit (void);
static MOP construct_instance (void);
static int insert_instance (void);
static int find_instance (MOP class, OID * oid, int id);
static int insert_default_instance (CLASS_TABLE * table, OID * oid);
static int insert_default_instances (void);
static void idmap_init (void);
static void idmap_final (void);
static int idmap_grow (int size);
static int validate_elos (void);
static int import_elos (void);
static void init_loader_state (void);
static void reset_loader_values (void);
static void reset_loader_state (void);
static int update_class_and_shared_attributes (void);
static int finish_instance (void);
static int finish_class (void);
static int realloc_values (void);
static int add_element (void ***elements, int *count, int *max, int grow);
static int add_attribute (void);
static int add_argument (void);
static int select_set_domain (TP_DOMAIN * domain,
			      TP_DOMAIN ** set_domain_ptr);

/*
 * get_domain - determines the target domain for next incoming value
 *    return: loader error code, zero if successful
 *    domain_ptr(out): returned domain pointer
 * Note:
 *    If the value array is already full, an error is returned indicating
 *    LDR_VALUE_OVERFLOW.
 *
 */
static int
get_domain (TP_DOMAIN ** domain_ptr)
{
  int error = NO_ERROR;
  TP_DOMAIN *domain = NULL;

  /* Find the domain for the incoming value */

  if (Loader.set != NULL)
    domain = Loader.set_domain;

  else if (Loader.value_count < Loader.att_count)
    domain = Loader.atts[Loader.value_count]->domain;

  else if (Loader.value_count < Loader.att_count + Loader.arg_count)
    {
      int index = Loader.value_count - Loader.att_count;
      if (Loader.args[index] != NULL)
	domain = Loader.args[index]->domain;
    }
  else
    {
      error = ER_LDR_VALUE_OVERFLOW;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
	      Loader.att_count + Loader.arg_count);
    }

  if (domain_ptr != NULL)
    *domain_ptr = domain;

  return error;
}


/*
 * domain_error - select an appropriate error message for a mismatch between
 * an incoming data value and the target domain.
 *    return: error code
 *    domain(in): current target domain
 *    src_type(in): supplied input type
 *    class(in): supplied input class
 */
static int
domain_error (TP_DOMAIN * domain, DB_TYPE src_type, MOP class)
{
  int error;
  const char *component;

  if (Loader.set != NULL)
    {
      /*
       * We are within a set constant, the value_count is decremented
       * in order to get to the attribute we are interested in because
       * it actually got incremented in ldr_start_set before the domain
       * is validated.
       */
      component = Loader.atts[Loader.value_count - 1]->header.name;
      error = ER_LDR_SET_DOMAIN_MISMATCH;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 3,
	      component, Loader.name, db_get_type_name (src_type));
    }
  else if (Loader.value_count < Loader.att_count)
    {
      /* Looking for a value for a non-set attribute */
      component = Loader.atts[Loader.value_count]->header.name;
      if (src_type == DB_TYPE_OBJECT)
	{
	  if (class == NULL)
	    {
	      error = ER_LDR_AMBIGUOUS_DOMAIN;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		      component, Loader.name);
	    }
	  else
	    {
	      error = ER_LDR_OBJECT_DOMAIN_MISMATCH;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 3,
		      component, Loader.name, db_get_class_name (class));
	    }
	}
      else
	{
	  error = ER_LDR_DOMAIN_MISMATCH;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4,
		  component, Loader.name,
		  domain->type->name, db_get_type_name (src_type));
	}
    }
  else if (Loader.value_count < Loader.att_count + Loader.arg_count)
    {

      /* Looking for a value for a constructor argument */
      if (src_type == DB_TYPE_OBJECT)
	{
	  if (class == NULL)
	    {
	      error = ER_LDR_ARGUMENT_AMBIGUOUS_DOMAIN;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		      Loader.arg_count, Loader.constructor->header.name);
	    }
	  else
	    {
	      error = ER_LDR_ARGUMENT_OBJECT_DOMAIN_MISMATCH;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4,
		      Loader.arg_count, Loader.constructor->header.name,
		      db_get_class_name (class));
	    }
	}
      else
	{
	  error = ER_LDR_ARGUMENT_DOMAIN_MISMATCH;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 4,
		  Loader.arg_count, Loader.constructor->header.name,
		  domain->type->name, db_get_type_name (src_type));
	}
    }
  else
    {
      /* This should have been detected in get_domain but its here for
         completeness */
      error = ER_LDR_VALUE_OVERFLOW;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
	      Loader.att_count + Loader.arg_count);
    }

  return error;
}


/*
 * check_domain - checks the type of an incoming value against the target
 * domain.
 *    return: NO_ERROR if successful, error code otherwise
 *    src_type(in): token type
 *    appropriate_type(in): type we should try to use
 * Note:
 *    If they don't match LDR_DOMAIN_MISMATCH is returned.
 *    If we can't accept any more values, LDR_VALUE_OVERFLOW is returned.
 *    If we need to coerce the value to a different type, the
 *    appropriate_type will be returned and should be used.
 */
static int
check_domain (DB_TYPE src_type, DB_TYPE * appropriate_type)
{
  TP_DOMAIN *domain, *best;
  DB_TYPE type;
  int error = NO_ERROR;

  /* always allow NULL */
  if (src_type == DB_TYPE_NULL)
    {
      *appropriate_type = src_type;
      return NO_ERROR;
    }

  if ((error = get_domain (&domain)))
    return error;

  type = DB_TYPE_NULL;
  if (domain == NULL)
    type = src_type;
  else
    {
      /* select the most appropriate domain from the list */
      best = tp_domain_select_type (domain, src_type, NULL, 1);
      if (best != NULL)
	type = best->type->id;
      else
	error = domain_error (domain, src_type, NULL);
    }

  if (appropriate_type != NULL)
    *appropriate_type = type;

  return error;
}


/*
 * check_object_domain - checks the type of an incoming value against the
 * target domain.
 *    return: error code
 *    class(in): class of incoming object reference
 *    actual_class(in): class to expect (if first arg is NULL)
 * Note:
 *    If they don't match LDR_DOMAIN_MISMATCH is returned.
 *    If we can't accept any more values, LDR_VALUE_OVERFLOW is returned.
 *    If "class" is NULL, we try to determine what the class will really
 *    be by examining the domain.  If there is only one possible class
 *    in the domain, we return it through "actual_class".
 *    If there are more than one possible classes in the domain,
 *    we return the LDR_AMBIGUOUS_DOMAIN error.
 */
static int
check_object_domain (MOP class, MOP * actual_class)
{
  int error = NO_ERROR;
  TP_DOMAIN *domain, *best, *d;

  if ((error = get_domain (&domain)))
    return error;

  if (class == NULL)
    {
      /* its an object but no domain was specified, see if we can unambiguously
         select one. */
      if (domain == NULL)
	/* ambigous */
	error = domain_error (NULL, DB_TYPE_OBJECT, NULL);
      else
	{
	  for (d = domain; d != NULL; d = d->next)
	    {
	      if (d->type == tp_Type_object)
		{
		  if (class == NULL && d->class_mop != NULL)
		    class = d->class_mop;
		  else
		    {
		      class = NULL;
		      break;
		    }
		}
	    }
	  if (class == NULL)
	    /* ambigous */
	    error = domain_error (domain, DB_TYPE_OBJECT, NULL);

	  if (actual_class != NULL)
	    *actual_class = class;
	  return 0;
	}
    }
  else
    {
      if (domain != NULL)
	{
	  /* make sure we have a compabile class in the domain list */
	  best = tp_domain_select_type (domain, DB_TYPE_OBJECT, class, 1);
	  if (best == NULL)
	    error = domain_error (domain, DB_TYPE_OBJECT, class);
	}
    }

  if (actual_class != NULL)
    *actual_class = class;

  return error;
}


/*
 * check_class_domain - checks the domain for an incoming reference to an
 * actual class object (not an instance).
 *    return: error code
 * Note:
 *    For these references, the target domain must contain a wildcard
 *    "object" domain.
 */
static int
check_class_domain (void)
{
  int error = NO_ERROR;
  TP_DOMAIN *domain, *d;
  int ok;

  if (!(error = get_domain (&domain)))
    {
      /* the domain must support "object" */
      if (domain != NULL)
	{
	  ok = 0;
	  for (d = domain; d != NULL && !ok; d = d->next)
	    {
	      if (d->type == tp_Type_object && d->class_mop == NULL)
		ok = 1;
	    }
	  if (!ok)
	    {
	      /* could make this more specific but not worth the trouble
	         right now, can only happen in internal trigger objects */
	      error = ER_LDR_CLASS_OBJECT_REFERENCE;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	    }
	}
    }
  return error;
}



/*
 * clist_init - initializes internal classes list
 *    return: void
 * Note:
 *    These are the classes that we don't allow to be loaded.
 */
static int
clist_init (void)
{
  MOP class;

  internal_classes = NULL;

  class = db_find_class (AU_ROOT_CLASS_NAME);
  if (class != NULL)
    {
      if (ml_ext_add (&internal_classes, class, NULL))
	return er_errid ();
    }
  class = db_find_class (AU_USER_CLASS_NAME);
  if (class != NULL)
    {
      if (ml_ext_add (&internal_classes, class, NULL))
	return er_errid ();
    }
  class = db_find_class (AU_PASSWORD_CLASS_NAME);
  if (class != NULL)
    {
      if (ml_ext_add (&internal_classes, class, NULL))
	return er_errid ();
    }
  class = db_find_class (AU_AUTH_CLASS_NAME);
  if (class != NULL)
    {
      if (ml_ext_add (&internal_classes, class, NULL))
	return er_errid ();
    }
  return NO_ERROR;
}


/*
 * clist_final - free the internal classes list
 *    return: void
 */
static void
clist_final (void)
{
  ml_ext_free (internal_classes);
  internal_classes = NULL;
}


/*
 * is_internal_class - Tests to see if a class is an internal class
 *    return: non-zero if this is an internal class
 *    class(in): class to examine
 */
static int
is_internal_class (MOP class)
{
  return (ml_find (internal_classes, class));
}


/*
 * check_commit -  Checks to see if the interrupt flag was raised. If it was
 * we check the interrupt type. We abort or commit based on the interrupt type
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    The interrupt type is determined by whether logging was enabled or not.
 *    Checks the state of the periodic commit counters.
 *    If this is enabled and the counter goes to zero, we commit
 *    the transaction.
 */
static int
check_commit (void)
{
  int error = NO_ERROR;

  /* Check interrupt flag */
  if (ldr_Load_interrupted)
    {
      int committed_instances = 0;
      if (ldr_Load_interrupted == LDR_STOP_AND_ABORT_INTERRUPT)
	{
	  error = db_abort_transaction ();
	  if (error)
	    {
	      display_error_line (-1);
	      fprintf (stderr, "%s\n", db_error_string (3));
	      committed_instances = (-1);
	    }
	  else
	    {
	      display_error_line (-1);
	      fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_INTERRUPTED_ABORT));
	      if (Loader.periodic_commit
		  && Total_objects >= Loader.periodic_commit)
		committed_instances =
		  Total_objects - (Loader.periodic_commit -
				   Loader.commit_counter);
	      else
		committed_instances = 0;
	    }
	}
      else
	{
	  if (Loader.class != NULL)
	    /* Check for uniques now to prevent violating a unique constraint */
	    error = sm_class_check_uniques (Loader.class);
	  if (error)
	    {
	      display_error_line (-1);
	      fprintf (stderr, "%s\n", db_error_string (3));
	      committed_instances = (-1);
	    }
	  else
	    {
	      error = db_commit_transaction ();
	      if (!error)
		{
		  committed_instances = Total_objects + 1;
		  display_error_line (-1);
		  fprintf (stderr, msgcat_message (MSGCAT_CATALOG_UTILS,
						   MSGCAT_UTIL_SET_LOADDB,
						   LOADDB_MSG_INTERRUPTED_COMMIT));
		}
	      else
		{
		  display_error_line (-1);
		  fprintf (stderr, "%s\n", db_error_string (3));
		  committed_instances = (-1);
		}
	    }
	}

      /* Invoke post interrupt callback function */
      if (ldr_post_interrupt_handler != NULL)
	(*ldr_post_interrupt_handler) (committed_instances);

      if (ldr_Jmp_buf != NULL)
	longjmp (*ldr_Jmp_buf, 1);
      else
	return (error);
    }

  if (Loader.periodic_commit)
    {
      Loader.commit_counter--;
      if (Loader.commit_counter <= 0)
	{
	  if (Loader.class != NULL)
	    /* Check for uniques now to prevent violating a unique constraint */
	    error = sm_class_check_uniques (Loader.class);
	  if (!error)
	    {
	      print_log_msg (Loader.verbose,
			     msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_COMMITTING));
	      error = db_commit_transaction ();
	    }
	  if (!error)
	    {
	      /* Invoke post commit callback function */
	      if (ldr_post_commit_handler != NULL)
		(*ldr_post_commit_handler) ((Total_objects + 1));
	    }

	  Loader.commit_counter = Loader.periodic_commit;
	}
    }

  return error;
}


/*
 * construct_instance - insert an instance using the current constructor method.
 *    return: object pointer
 */
static MOP
construct_instance (void)
{
  DB_VALUE *args[LDR_MAX_ARGS];
  DB_VALUE retval;
  int error;
  MOP obj;
  int i, a;

  obj = NULL;

  /* sigh, would be nice to have a method invocation function that
     could take a packed array of values and a count.
   */
  for (i = 0, a = Loader.att_count; i < Loader.arg_count; i++, a++)
    args[i] = &Loader.values[a];
  args[i] = NULL;

  error = db_send_argarray (Loader.class, Loader.constructor->header.name,
			    &retval, args);

  if (!error && DB_VALUE_TYPE (&retval) == DB_TYPE_OBJECT)
    {
      obj = DB_GET_OBJECT (&retval);
      /* now we have to initialize the instance with the supplied values */
      for (i = 0; i < Loader.att_count && !error; i++)
	{
	  /* should be using descriptors for this, requires we
	     store them somewhere */
	  error =
	    db_put_internal (obj, Loader.atts[i]->header.name,
			     &Loader.values[i]);
	}
      if (error)
	{
	  (void) db_drop (obj);
	  obj = NULL;
	}
    }

  if (obj != NULL)
    {
      if (locator_flush_instance (obj) != NO_ERROR)
	{
	  /* could delete it here but if we got a flush failure
	     its probably pointless */
	  db_drop (obj);
	  obj = NULL;
	}
    }

  return obj;
}


/*
 * insert_instance - inserts the pending instance into the database
 *    return: void
 */
static int
insert_instance ()
{
  int error = NO_ERROR;
  OID oid;
  DESC_OBJ obj;
  INST_INFO *inst;
  MOP real_obj;
#if 0
  LOG_LSA savept_lsa;
#endif

  if (Loader.validation_only)
    {
      if (Loader.inum != -1)
	otable_set_presize (Loader.table, Loader.inum);
#if 0
/* OLD WAY: This makes actual dummy entries into the class tables, keep around
   until we're sure it isn't needed.
*/
      if (Loader.inum != -1)
	{
	  /* if we're performing a validation pass, don't bother
	     to call the constructor if one was specified, just
	     make an entry into the otable like we do normally */
	  oid.volid = 0;
	  oid.slotid = 0;
	  oid.pageid = Loader.inum;

	  inst = otable_find (Loader.table, Loader.inum);
	  if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
	    error = otable_insert (Loader.table, &oid, Loader.inum);
	  else
	    error = otable_update (Loader.table, Loader.inum);
	}
#endif
    }
  else
    {
      /* check unique values that weren't detected in the validation pass */

      if (Loader.constructor != NULL)
	{
	  real_obj = construct_instance ();
	  if (real_obj == NULL)
	    error = er_errid ();
	  else
	    {
	      if (Loader.inum != -1)
		{
		  inst = otable_find (Loader.table, Loader.inum);
		  if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
		    error =
		      otable_insert (Loader.table, WS_OID (real_obj),
				     Loader.inum);
		  else
		    {
		      error = ER_LDR_FORWARD_CONSTRUCTOR;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		    }
		}
	    }
	}
      else
	{
	  /* build brief object descriptor */
	  obj.classop = Loader.class;
	  obj.class_ = (SM_CLASS *) Loader.class->object;	/* sigh */
	  obj.count = Loader.value_count;
	  obj.atts = &Loader.atts[0];
	  obj.values = &Loader.values[0];

#if 0
	  if (Unique_violation_dump)
	    {
	      if (tran_server_savepoint ("LoaderUnique", &savept_lsa) !=
		  NO_ERROR)
		{
		  printf ("error in tran_savepoint\n");
		}
	    }
#endif
	  if (Loader.inum == -1)
	    {
	      error = disk_insert_instance (Loader.class, &obj, &oid);
	    }
	  else
	    {
	      if (No_oid_hint)
		{
		  error = disk_insert_instance (Loader.class, &obj, &oid);
		}
	      else
		{
		  inst = otable_find (Loader.table, Loader.inum);
		  if (inst == NULL || !(inst->flags & INST_FLAG_RESERVED))
		    {
		      if (!
			  (error =
			   disk_insert_instance (Loader.class, &obj, &oid)))
			{
			  error =
			    otable_insert (Loader.table, &oid, Loader.inum);
			}
		    }
		  else
		    {
		      if (!
			  (error =
			   disk_update_instance (Loader.class, &obj,
						 &inst->oid)))
			{
			  error = otable_update (Loader.table, Loader.inum);
			}
		    }
		}
	    }
	}
      if (!error)
	{
	  Loader.table->total_inserts++;
	  error = check_commit ();
	}
#if 0
      else if (Unique_violation_dump && error == ER_BTREE_UNIQUE_FAILED)
	{
	  int i;
	  fprintf (uvd_fp, "Line %d: %s(", get_error_line (), Loader.name);
	  for (i = 0; i < Loader.value_count; i++)
	    {
	      fprintf (uvd_fp, "%s", Loader.atts[i]->header.name);
	      if (i + 1 < Loader.value_count)
		fprintf (uvd_fp, ", ");
	    }
	  fprintf (uvd_fp, ") (");
	  for (i = 0; i < Loader.value_count; i++)
	    {
	      help_fprint_value (uvd_fp, &Loader.values[i]);
	      if (i + 1 < Loader.value_count)
		fprintf (uvd_fp, ", ");
	    }
	  fprintf (uvd_fp, ")\n");
	  tran_server_partial_abort ("LoaderUnique", &savept_lsa);
	}
#endif
    }

  if (error)
    {
#if 0
      if (Unique_violation_dump && error == ER_BTREE_UNIQUE_FAILED)
	error = NO_ERROR;
      else
	ldr_internal_error ();
#else
      ldr_internal_error ();
#endif
    }
  else
    {
      Loader.icount++;
      Total_objects++;

      /* Hack to give running status indication during large inserts */
      if (Loader.verbose && Loader.status_count)
	{
	  Loader.status_counter++;
	  if (Loader.status_counter >= Loader.status_count)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_INSTANCE_COUNT_EX),
		       Loader.icount);
	      fflush (stdout);
	      Loader.status_counter = 0;
	    }
	}
    }

  return error;
}


/*
 * find_instance - his locates an instance OID given the class and an instance
 * id.
 *    return: error code
 *    class(in): class object
 *    oid(in): instance OID (returned)
 *    id(in): instance identifier
 * Note:
 *    If the instance does not exist, an OID is reserved for this
 *    instance.
 */
static int
find_instance (MOP class, OID * oid, int id)
{
  int error = NO_ERROR;
  CLASS_TABLE *table;
  INST_INFO *inst;

  table = otable_find_class (class);

  if (table == NULL)
    {
      OID_SET_NULL (oid);
      error = er_errid ();
    }
  else
    {
      inst = otable_find (table, id);
      if (inst != NULL)
	*oid = inst->oid;
      else
	{
	  OID_SET_NULL (oid);

	  /* sigh, should try to catch this at a higher level */
	  if (is_internal_class (class))
	    {
	      error = ER_LDR_INTERNAL_REFERENCE;
	      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
		      db_get_class_name (class));
	    }
	  else
	    {
	      if (Loader.validation_only)
		{
		  otable_set_presize (Loader.table, id);
		}
	      else
		{
		  if (!(error = disk_reserve_instance (class, oid)))
		    error = otable_reserve (table, oid, id);
		  else
		    ldr_internal_error ();
		}
	    }
	}
    }
  return (error);
}



/*
 * insert_default_instance - Work function for insert_default_instances.
 *    return: NO_ERROR if successful, error code otherwise
 *    table(out):
 *    oid(in): oid of the instance to insert
 * Note:
 *    This inserts an instance of a class.  The attribute descriptor
 *    list is empty so the default values will be used for all attributes
 *    that have them.
 */
static int
insert_default_instance (CLASS_TABLE * table, OID * oid)
{
  int error = NO_ERROR;
  DESC_OBJ obj;

  if (!Loader.validation_only)
    {
      /* build brief object descriptor */
      obj.classop = table->class_;
      obj.class_ = (SM_CLASS *) (table->class_->object);	/* sigh */
      obj.count = 0;
      obj.atts = NULL;
      obj.values = NULL;

      /* don't have to worry about UNIQUE here since these can't have
         DEAFULT values.
       */
      if ((error = disk_update_instance (table->class_, &obj, oid)))
	ldr_internal_error ();
    }
  default_count++;
  table->total_inserts++;
  error = check_commit ();

  return error;
}


/*
 * insert_default_instances - Inserts instances for all instances that were
 * referenced but never actually defined in the loader input file.
 *    return: void
 * Note:
 *    These will be inserted with all default values.
 */
static int
insert_default_instances (void)
{
  int error;

  default_count = 0;

  /*
   * note that if we run out of space, the Loader.valid flag will get
   * turned off, insert_default_instance needs to check this
   */
  error = otable_map_reserved (insert_default_instance, 1);

  Loader.default_count = default_count;

  return error;
}

/*
 * The class id map provides a textually shorter way to reference classes.
 * When the %class <name> <id> statement is parsed, an entry will be made
 * in this map table so the class can be referenced by id number rather
 * than continually using the full class name.
 *
 */



/*
 * idmap_init - initialize the global class id map
 *    return: void
 */
static void
idmap_init (void)
{
  Id_map = NULL;
  Id_map_size = 0;
}


/*
 * idmap_final - free the class id map
 *    return: void
 */
static void
idmap_final (void)
{
  if (Id_map != NULL)
    {
      free_and_init (Id_map);
      Id_map_size = 0;
    }
}


/*
 * idmap_grow - makes sure the class id map is large enough to accomodate
 * the given index.
 *    return: NO_ERROR if successful, error code otherwise
 *    size(in): element index we want to set
 */
static int
idmap_grow (int size)
{
  int error = NO_ERROR;
  MOP *map;
  int newsize, i;

  if (size > Id_map_size)
    {
      newsize = size + 10;	/* some extra for growth */
      map = malloc (sizeof (MOP) * newsize);
      if (map == NULL)
	{
	  error = ER_LDR_MEMORY_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	}
      else
	{
	  i = 0;
	  if (Id_map != NULL)
	    {
	      for (; i < Id_map_size; i++)
		map[i] = Id_map[i];
	      free_and_init (Id_map);
	    }
	  for (; i < newsize; i++)
	    map[i] = NULL;

	  Id_map = map;
	  Id_map_size = newsize;
	}
    }
  return error;
}


/*
 * ldr_assign_class_id - assigning an id to a class.
 *    return: NO_ERROR if successful, error code otherwise
 *    class(in): class object
 *    id(in): id for this class
 */
int
ldr_assign_class_id (MOP class, int id)
{
  int error;

  if (!(error = idmap_grow (id + 1)))
    Id_map[id] = class;

  return error;
}


/*
 * ldr_get_class_from_id - searching the class id map.
 *    return: class object
 *    id(in): class id
 */
MOP
ldr_get_class_from_id (int id)
{
  MOP class = NULL;

  if (id <= Id_map_size)
    class = Id_map[id];

  return (class);
}


/*
 * validate_elos - validate ELOs of loaded values
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    Maps over the values for the next pending object to see if any of
 *    them are initializers for ELO attributes.  If they are, we
 *    make sure that the indicated file actually exists.  If not, an
 *    error is signalled.
 *    If we need to get really fancy, we could keep track of the total
 *    size required for the imported ELOs and reserve that much space
 *    before continuing with the load.  Could be time consuming.
 */
static int
validate_elos (void)
{
  int error = NO_ERROR;
  DB_ELO *elo;
  int i, fd;

  for (i = 0; i < Loader.att_count && !error; i++)
    {
      if (DB_VALUE_TYPE (&Loader.values[i]) == DB_TYPE_ELO)
	{
	  elo = DB_GET_ELO (&Loader.values[i]);
	  if (elo != NULL && elo->type == ELO_LO && elo->pathname != NULL)
	    {

	      fd = open (elo->pathname, O_RDONLY, 0);
	      if (fd > 0)
		close (fd);
	      else
		{
		  error = ER_LDR_ELO_INPUT_FILE;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
			  elo->pathname);
		  Loader.errors++;
		}
	    }
	}
    }
  return (error);
}


/*
 * import_elos - maps through the values for te pending instance to see if
 * there are any initilizers for ELO attributes.
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    If there are, it fixes up the contents of the ELO structure and calls
 *    lo_migrate_in to actually bring in the data.
 */
static int
import_elos (void)
{
  int error = NO_ERROR;
  DB_ELO *elo;
  int i;
  const char *filename;

  for (i = 0; i < Loader.att_count && !error; i++)
    {
      if (DB_VALUE_TYPE (&Loader.values[i]) == DB_TYPE_ELO)
	{
	  elo = DB_GET_ELO (&Loader.values[i]);
	  if (elo != NULL && elo->type == ELO_LO && elo->pathname != NULL)
	    {
	      /* fixup the elo */
	      filename = elo->pathname;
	      ws_free_string ((char *) elo->original_pathname);
	      elo->pathname = NULL;
	      elo->original_pathname = NULL;

	      error = lo_migrate_in (&elo->loid, filename);
	      ws_free_string ((char *) filename);
	    }
	}
    }

  if (error)
    ldr_internal_error ();

  return error;
}


/*
 * init_loader_state - initialize the global loader state
 *    return: void
 * Note:
 *    This should be called once prior to loader operation.
 */
static void
init_loader_state (void)
{
  Total_objects = 0;
  Loader.valid = 0;
  Loader.name[0] = '\0';
  Loader.class = NULL;
  Loader.table = NULL;

  Loader.att_count = 0;
  Loader.maxatt = 0;
  Loader.atts = NULL;

  Loader.constructor = NULL;
  Loader.arg_count = 0;
  Loader.maxarg = 0;
  Loader.args = NULL;

  Loader.value_count = 0;
  Loader.maxval = 0;
  Loader.values = NULL;

  Loader.inum = -1;
  Loader.icount = 0;
  Loader.instance_started = 0;
  Loader.set = NULL;
  Loader.set_domain = NULL;

  Loader.validation_only = 0;
  Loader.errors = 0;
  Loader.default_count = 0;
  Loader.inhibit_instances = 0;

  Loader.attribute_type = LDR_ATTRIBUTE_ANY;
  Loader.updates_on_the_class = 0;
}


/*
 * reset_loader_values - This clears out any values that have stored in the
 * loader state.
 *    return: void
 * Note:
 *    This is called immediately after each instance has been inserted
 *    and also at various points to free storage after errors.
 */
static void
reset_loader_values (void)
{
  int i;

  for (i = 0; i < Loader.value_count; i++)
    db_value_clear (&Loader.values[i]);

  /*
   * since the loader set is in one of the loader values, it will
   * have been freed by the above clear_value call
   */
  Loader.set = NULL;
  Loader.set_domain = NULL;
}


/*
 * reset_loader_state - Clears the loader values and also loses any
 * information on the class/attribute/argument definitions that have been made.
 *    return: void
 * Note:
 *    Called after errors have been detected.
 */
static void
reset_loader_state (void)
{
  Loader.instance_started = 0;

  reset_loader_values ();

  Loader.name[0] = '\0';
  Loader.class = NULL;
  Loader.table = NULL;
  Loader.att_count = 0;
  Loader.arg_count = 0;
  Loader.value_count = 0;
  Loader.inum = -1;
  Loader.constructor = NULL;
  Loader.valid = 0;
}


/*
 * update_class_and_shared_attributes - Maps over the pending value list and
 * assigns them directly to the class as either shared or class attributes.
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    This is only used for class/shared attributes.
 *    NEW: This can now be used to set the default values of
 *    non-shared and non-class attributes too.  If we encounter
 *    a normal instance attribute in the list AND the
 *    mode is LDR_ATTRIBUTE_DEFAULT, we change the default value.
 */
static int
update_class_and_shared_attributes (void)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  DB_VALUE *value;
  int i;

  /* Would be better if the lower level attribute assignment functions
   * in the obj_ module were visible here since we've already located
   * the attribute structure.
   */

  for (i = 0; i < Loader.att_count && !error; i++)
    {
      att = Loader.atts[i];

      value = &Loader.values[i];

      /*
       * NOTE: convert DB_TYPE_OID values to DB_TYPE_OBJECT values before
       * sending them down to the obj_set() interface.  Really, should
       * change tp_value_cast() so that it handles coercion of
       * OID types to OBJECT types but that's checked out right now.
       */
      if (DB_VALUE_TYPE (value) == DB_TYPE_OID)
	{
	  /* swizzle the pointer */
	  DB_OBJECT *mop;
	  mop = db_object (db_pull_oid (value));
	  DB_MAKE_OBJECT (value, mop);
	}

      if (att->header.name_space == ID_SHARED_ATTRIBUTE)
	error = obj_set_shared (Loader.class, att->header.name, value);

      else if (att->header.name_space == ID_CLASS_ATTRIBUTE)
	error = obj_set (Loader.class, att->header.name, value);

      else if (Loader.attribute_type == LDR_ATTRIBUTE_DEFAULT)
	error = db_change_default (Loader.class, att->header.name, value);
      /*
       * else, its a normal attribute and we're not updating default values,
       * ignore it
       */
    }

  if (error)
    ldr_internal_error ();

  return error;
}


/*
 * finish_instance - insert pending instance
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    Called when we're done specifying values for an instance.
 *    If the values contained class/shared attribute values rather than
 *    initial values for instances, we go directly to the class and change
 *    the attribute values.
 */
static int
finish_instance ()
{
  int error = NO_ERROR;

  if (Loader.valid)
    {
      if (Loader.value_count != Loader.att_count + Loader.arg_count)
	{
	  error = ER_LDR_MISSING_ATTRIBUTES;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		  Loader.att_count, Loader.value_count);
	  ldr_invalid_object ();
	}
      else
	{
	  if (Loader.validation_only)
	    {
	      if (!(error = validate_elos ()))
		{
		  if (!Loader.inhibit_instances)
		    error = insert_instance ();
		}
	    }
	  else
	    {
	      /* build any internal ELO's that are required */
	      if (!(error = import_elos ()))
		{
		  if (Loader.updates_on_the_class)
		    error = update_class_and_shared_attributes ();

		  if (!error && !Loader.inhibit_instances)
		    error = insert_instance ();
		}
	    }
	}

      reset_loader_values ();
      Loader.value_count = 0;
      Loader.instance_started = 0;
    }

  return error;
}


/*
 * finish_class - called when we've finished processing all of the data lines
 * for a class and we need to reset the state to prepare for a new class.
 *    return: void
 */
static int
finish_class ()
{
  int error = NO_ERROR;

  if (Loader.valid && Loader.instance_started)
    error = finish_instance ();

  if (!error && Loader.class != NULL)
    error = sm_class_check_uniques (Loader.class);

  if (error)
    {
      ldr_invalid_class ();
    }
  else
    {
      if (Loader.verbose)
	{
	  if (Loader.class != NULL && Loader.icount)
	    fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					     MSGCAT_UTIL_SET_LOADDB,
					     LOADDB_MSG_INSTANCE_COUNT),
		     Loader.icount);
	}
    }
  reset_loader_state ();
  Loader.icount = 0;
  Loader.status_counter = 0;

  return error;
}


/*
 * realloc_values - reallocate the loader's value array
 *    return: success code (non-zero if ok)
 * Note:
 *    This reallocations the loader's value array so that it is as large
 *    as the combined attribute & argument count.
 *    This will be called after add_elemetn is used to grow one of these
 *    lists.
 */
static int
realloc_values (void)
{
  DB_VALUE *vals;
  int newmax, i;

  newmax = Loader.maxatt + Loader.maxarg;
  if (Loader.maxval < newmax)
    {

      vals = malloc (sizeof (DB_VALUE) * newmax);
      if (vals == NULL)
	return 0;

#if 0
      if (Loader.ticket)
	{
	  (void) mgc_unregister_callback (Loader.ticket, true);
	  Loader.ticket = 0;
	}
      (void) mgc_register_callback (DB_GC_PHASE_MARK,
				    db_gc_scan_region,
				    (void *) vals,
				    (sizeof (DB_VALUE) * newmax),
				    &Loader.ticket, true);
#endif

      for (i = 0; i < Loader.value_count; i++)
	vals[i] = Loader.values[i];

      for (; i < newmax; i++)
	DB_MAKE_NULL (&vals[i]);

      if (Loader.values != NULL)
	free_and_init (Loader.values);
      Loader.values = vals;
      Loader.maxval = newmax;
    }
  return 1;
}


/*
 * add_element - add an element to either the loader's attribute array or
 * argument array.
 *    return: success code (non-zero if ok)
 *    elements(in/out): pointer to an array of pointers
 *    count(in/out): current index (and returned new index)
 *    max(in/out): maximum size (and returned maximum size)
 *    grow(in): amount to grow if necessary
 * Note:
 *    Since these are so similar, we can use the same "grow" logic for both.
 *    We first check to see if *count is less than *max, if so we simply
 *    increment count.  If not, we extend the array, return the incremented
 *    count and also returned the new max size.
 */
static int
add_element (void ***elements, int *count, int *max, int grow)
{
  int new, resize, i;
  void **ptrs;

  if (*count >= *max)
    {
      /* realloc the attribute array */
      resize = *max + grow;

      ptrs = malloc (sizeof (void *) * resize);
      if (ptrs == NULL)
	return -1;
      for (i = 0; i < *count; i++)
	ptrs[i] = (*elements)[i];
      for (; i < resize; i++)
	ptrs[i] = NULL;

      if (*elements != NULL)
	free_and_init (*elements);
      *elements = ptrs;
      *max = resize;
    }

  new = *count;
  (*elements)[new] = NULL;
  *count = new + 1;
  return new;
}


/*
 * add_attribute - Extends the loader attribute array.
 *    return: next attribute index or negative value if error
 */
static int
add_attribute (void)
{
  int index;

  index = add_element ((void ***) (&Loader.atts), &Loader.att_count,
		       &Loader.maxatt, LDR_ATT_GROW_SIZE);
  if (index >= 0)
    {
      if (!realloc_values ())
	index = -1;
    }
  return index;
}


/*
 * add_argument - Extends the loader's argument array.
 *    return: next argument index or negative value if error.
 */
static int
add_argument (void)
{
  int index;

  index = add_element ((void ***) (&Loader.args), &Loader.arg_count,
		       &Loader.maxarg, LDR_ARG_GROW_SIZE);
  if (index >= 0)
    {
      if (!realloc_values ())
	index = -1;
    }
  return index;
}

/*
 * These are the external routines for controlling the loader.
 *
 * The intended scenario is this:
 *   ldr_init()
 *   .. parse the file as a vaildation pass
 *      ldr_finish() flush out any pending definitions
 *   ldr_start()
 *   .. parse the file again with real insertions
 *      ldr_finish() flush out any pending insertions
 *   ldr_final() free resources
 */


/*
 * ldr_init - prepares the loader for use.
 *    return: NO_ERROR if successful, error code otherwise
 *    verbose(in): non-zero to enable printing of status messages
 * Note:
 *    If the verbose flag is on, status messages will be displayed
 *    as the load proceeds.
 *    The loader is initially configured for validation mode.
 *    Call ldr_start() to clear the current state and enter
 *    insertion mode.
 */
int
ldr_init (bool verbose)
{
  init_loader_state ();
  idmap_init ();

  if (otable_init ())
    return er_errid ();

  if (disk_init ())
    return er_errid ();

  if (clist_init ())
    return er_errid ();

  Loader.validation_only = 1;
  Loader.verbose = verbose ? 1 : 0;

  Loader.status_count = 0;
  Loader.status_counter = 0;	/* used to monitor number of insertions
				 * performed.
				 */
  Loader.status_count = 10;

  return NO_ERROR;
}


/*
 * ldr_start - prepares the loader for actual insertion of data.
 *    return: NO_ERROR if successful, error code otherwise
 *    periodic_commit(in): periodic commit size
 * Note:
 *    It is commonly called after the input file has been processed
 *    once for syntax checking.  It can also be called immediately
 *    after ldr_init() to skip the syntax check and go right into
 *    insertion mode.
 */
int
ldr_start (int periodic_commit)
{
  reset_loader_state ();

  /* in case we did any presizing, allocate the tables */
  if (otable_prepare ())
    return er_errid ();

  Loader.validation_only = 0;

  if (periodic_commit <= 0)
    Loader.periodic_commit = 0;
  else
    {
      Loader.periodic_commit = periodic_commit;
      Loader.commit_counter = periodic_commit;
    }

  /* make sure we reset this to get accurate statistics */
  Total_objects = 0;

  return NO_ERROR;
}


/*
 * ldr_finish - cause the loader to finish
 *    return: error code
 *    error(in): non-zero if external error was detected
 * Note:
 *    This includes finishing whatever instance is buffered for insertion.
 *    If the error flag is non-zero it means that the caller wishes
 *    to abort the load for some reason, in this case we immediately
 *    reset the loader state and don't insert the buffered instance.
 *    We DO NOT clear out the class tables and other resources, this
 *    is retained so that the loader can be used again without losing
 *    all of this information.
 *    To completely shut down the loader, call ldr_final().
 */
int
ldr_finish (int error)
{
  int finish_error = NO_ERROR;

  if (error)
    reset_loader_state ();

  else if (Loader.valid)
    {
      finish_error = finish_class ();
      if (!finish_error)
	error = insert_default_instances ();
    }

  return finish_error;
}


/*
 * ldr_final - shut down the loader
 *    return: void
 * Note:
 *    Since there may be a pending instance that has not yet been inserted,
 *    you must always call ldr_finish to make sure this occurrs.
 *    If the caller detects an error and wishes to abort the load immediate,
 *    pass in non-zero and the pending instance will not be inserted.
 *    After this call, the loader cannot be used and the session
 *    must begin again with a ldr_init() call.
 */
int
ldr_final (void)
{
  int shutdown_error = NO_ERROR;

  shutdown_error = ldr_finish (1);
  if (Loader.values != NULL)
    free_and_init (Loader.values);
  if (Loader.args != NULL)
    free_and_init (Loader.args);
  if (Loader.atts != NULL)
    free_and_init (Loader.atts);
  clist_final ();
  idmap_final ();
  otable_final ();
  disk_final ();

  return shutdown_error;
}


/*
 * ldr_update_statistics - update the statistics for the classes that were
 * involved in the load.
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    This can be called after loading has finished but BEFORE ldr_final.
 */
int
ldr_update_statistics (void)
{
  int error = NO_ERROR;
  CLASS_TABLE *table;

  for (table = Classes; table != NULL && !error; table = table->next)
    {
      if (table->total_inserts)
	{
	  if (Loader.verbose)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_CLASS_TITLE),
		       sm_class_name (table->class_));
	      fflush (stdout);
	    }
	  error = sm_update_statistics (table->class_);
	}
    }
  return error;
}


/*
 * ldr_start_class - begin a class description
 *    return: NO_ERROR if successful, error code otherwise
 *    class(in): class object
 *    classname(in): name of this class
 * Note:
 *    After this function, the loader will expect one or more calls
 *    to ldr_add_attibute.
 */
int
ldr_start_class (MOP class, const char *classname)
{
  int error = NO_ERROR;

  error = finish_class ();
  if (!error)
    {
      if (is_internal_class (class))
	{
	  error = ER_LDR_SYSTEM_CLASS;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, classname);
	}
      else
	{
	  /* should check authorization here ! */
	  if (Loader.verbose)
	    {
	      fprintf (stdout, msgcat_message (MSGCAT_CATALOG_UTILS,
					       MSGCAT_UTIL_SET_LOADDB,
					       LOADDB_MSG_CLASS_TITLE),
		       classname);
	      fflush (stdout);
	    }
	  Loader.valid = 1;
	  Loader.class = class;

	  /* need to check for memory problems */
	  Loader.table = otable_find_class (class);
	  if (Loader.table == NULL)
	    {
	      error = ER_LDR_MEMORY_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      ldr_internal_error ();
	    }
	  else
	    {
	      Loader.attribute_type = LDR_ATTRIBUTE_ANY;
	      Loader.inhibit_instances = 0;
	      Loader.updates_on_the_class = 0;
	      strcpy (Loader.name, classname);
	    }
	}
    }
  return (error);
}


/*
 * ldr_restrict_attributes - inhibit loader from inserting instances
 *    return: none
 *    type(in): type of attributes to expect
 * Note:
 *    This is called after ldr_start_class to indicate that the attributes
 *    being added are class/shared attributes rather than normal instance
 *    attributes.
 */
int
ldr_restrict_attributes (int type)
{
  Loader.attribute_type = type;
  if (type == LDR_ATTRIBUTE_SHARED || type == LDR_ATTRIBUTE_CLASS ||
      type == LDR_ATTRIBUTE_DEFAULT)
    Loader.inhibit_instances = 1;

  return NO_ERROR;
}


/*
 * ldr_add_attribute - adds an attribute to a class description.
 *    return: NO_ERROR if successful, error code otherwise
 *    attname(in): attribute name
 * Note:
 *    This would be called one or more times after the ldr_start_class
 *    function to specify the names of the attributes that
 *    will have values on subsequent data lines.
 */
int
ldr_add_attribute (const char *attname)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  int index;

  if (Loader.valid)
    {
      att = NULL;
      if (Loader.attribute_type == LDR_ATTRIBUTE_ANY)
	{
	  att = db_get_attribute (Loader.class, attname);
	  if (att == NULL)
	    {
	      error = er_errid ();
	      if (error == ER_OBJ_INVALID_ATTRIBUTE)
		{
		  /* try again for a shared attribute */
		  error = NO_ERROR;
		  att = db_get_shared_attribute (Loader.class, attname);
		}
	    }
	}
      else if (Loader.attribute_type == LDR_ATTRIBUTE_INSTANCE ||
	       Loader.attribute_type == LDR_ATTRIBUTE_DEFAULT)
	att = db_get_attribute (Loader.class, attname);

      else if (Loader.attribute_type == LDR_ATTRIBUTE_SHARED)
	att = db_get_shared_attribute (Loader.class, attname);

      else if (Loader.attribute_type == LDR_ATTRIBUTE_CLASS)
	att = db_get_class_attribute (Loader.class, attname);

      if (att == NULL)
	{
	  /* don't overwrite the error here, especially if its an
	     authorization error. */
	  error = er_errid ();
#if 0
	  error = ER_LDR_INVALID_ATTRIBUTE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		  attname, Loader.name);
#endif
	  ldr_invalid_class ();
	}
      else
	{
	  /* keep a counter of updates that must be applied to the class object
	     itself. */
	  if (att->header.name_space == ID_SHARED_ATTRIBUTE ||
	      att->header.name_space == ID_CLASS_ATTRIBUTE ||
	      Loader.attribute_type == LDR_ATTRIBUTE_DEFAULT)
	    Loader.updates_on_the_class++;

	  index = add_attribute ();
	  if (index >= 0)
	    Loader.atts[index] = att;
	  else
	    {
	      error = ER_LDR_MEMORY_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      ldr_internal_error ();
	    }
	}
    }
  return (error);
}


#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ldr_add_class_attribute - Adds a class attribute to the current class
 * description.
 *    return: NO_ERROR if successful, error code otherwise
 *    attname(in): attribute name
 */
int
ldr_add_class_attribute (const char *attname)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  int index;

  if (Loader.valid)
    {
      att = db_get_class_attribute (Loader.class, attname);
      if (att == NULL)
	{
	  error = ER_LDR_INVALID_ATTRIBUTE;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
		  attname, Loader.name);
	}
      else
	{
	  index = add_attribute ();
	  if (index >= 0)
	    {
	      Loader.atts[index] = att;
	      Loader.updates_on_the_class++;
	    }
	  else
	    {
	      error = ER_LDR_MEMORY_ERROR;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      ldr_internal_error ();
	    }
	}
    }
  return (error);
}
#endif /* ENABLE_UNUSED_FUNCTION */


/*
 * ldr_set_constructor - Specifies a constructor method for the instances of
 * the current class.
 *    return: NO_ERROR if successful, error code otherwise
 *    name(in): method dname
 * Note:
 *    The name must be the name of a class method that when
 *    called will return an isntance.  After this function, the loader
 *    will expect zero or more calls to ldr_add_argument to specify
 *    any arguments that must be passed to the constructor method.
 */
int
ldr_set_constructor (const char *name)
{
  int error = NO_ERROR;
  SM_METHOD *meth;

  if (Loader.valid)
    {
      meth = db_get_class_method (Loader.class, name);
      if (meth == NULL)
	{
	  error = ER_LDR_INVALID_CONSTRUCTOR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, name,
		  Loader.name);
	}
      else
	{
	  /* for now, a method can have exactly one signature */
	  Loader.constructor = meth;
	  Loader.arg_count = 0;
	}
    }
  return error;
}


/*
 * ldr_add_argument - spefify paramters to an instance constructor method
 * previously specified with ldr_set_constructor.
 *    return: NO_ERROR if successful, error code otherwise
 *    name(in): argument name
 * Note:
 *    The name isn't really important here since method argumetns don't
 *    have domains.  It is however important to specify as many arguments
 *    as the method expects because the domain validation will be
 *    done positionally according to the method signature in the schema.
 */
int
ldr_add_argument (const char *name)
{
  int error = NO_ERROR;
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;
  int index;

  if (Loader.valid)
    {
      if (Loader.constructor == NULL)
	{
	  error = ER_LDR_UNEXPECTED_ARGUMENT;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, 0);
	}
      else
	{
	  sig = Loader.constructor->signatures;
	  /* arg count of zero currently means "variable", not good */
	  if (sig->num_args && Loader.arg_count >= sig->num_args)
	    {
	      error = ER_LDR_UNEXPECTED_ARGUMENT;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
		      sig->num_args);
	    }
	  else
	    {
	      /* Locate the argument descriptor, remember to adjust for
	         1 based argument numbering.
	       */
	      arg =
		classobj_find_method_arg (&sig->args, Loader.arg_count + 1,
					  0);
	      index = add_argument ();
	      if (index >= 0)
		Loader.args[index] = arg;
	      else
		{
		  error = ER_LDR_MEMORY_ERROR;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		  ldr_internal_error ();
		}
	    }
	}
    }
  return error;
}


/*
 * ldr_add_instance - specify a new instance for the previously defined class.
 *    return: NO_ERROR if successful, error code otherwise
 *    id(in): instance reference number (-1 if not referencable)
 * Note:
 *    Any pending instance will be inserted before setting up for the
 *    next one.
 *    The loader will expect calls to ldr_add_value, ldr_add_object_value
 *    and ldr_start_set between calls to ldr_add_instance.
 */
int
ldr_add_instance (int id)
{
  int error = NO_ERROR;

  if (Loader.valid)
    {
      if (Loader.instance_started)
	error = finish_instance ();

      if (!error)
	{
	  Loader.instance_started = 1;
	  /* must be -1 to indicate non-referenced instance */
	  Loader.inum = id;
	}
    }

  return error;
}

/*
 * ldr_invalid_class - error is detected and we need to reset the interal
 * loader state.
 *    return: void
 * Note:
 *    This indicates that there is some problem with the currently
 *    defined class.
 *    After the state is reset, the caller will to begin again
 *    with a class definition.
 */
void
ldr_invalid_class (void)
{
  Loader.valid = 0;
  Loader.errors++;
}


/*
 * ldr_invalid_object - called by upper layer user when invalid object met.
 *    return: void
 * Note:
 *    This indicates that there is some problem with the pending
 *    object.
 *    After the state is reset, the caller will to begin again
 *    with a class definition.
 *    NOTE: We could just abort the current object here, not sure
 *    why we have to blow off the whole class.
 */
void
ldr_invalid_object (void)
{
  Loader.valid = 0;
  Loader.errors++;
}


#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ldr_invalid_file - called by upper layer user when invalid input file met.
 *    return: void
 * Note:
 *    This indicates that there is some fundamental problem with the
 *    entire input file.
 *    After the state is reset, the caller will to begin again
 *    with a class definition.
 */
void
ldr_invalid_file (void)
{
  reset_loader_state ();
  Loader.errors++;
}
#endif /* ENABLE_UNUSED_FUNCTION */


/*
 * ldr_internal_error - called by an upper level when an error is detected
 * and we need to reset the interal loader state.
 *    return: void
 * Note:
 *    Call this when serious errors are encountered and we need to
 *    stop immediately.  Probably we could longjmp out of ANTLR here to
 *    avoid parsing the rest of the file.
 */
void
ldr_internal_error (void)
{
  reset_loader_state ();
  Loader.errors++;
}


/*
 * ldr_stats - access the statistics maintained during loading.
 *    return: void
 *    errors(out): return error count
 *    objects(out): return object count
 *    defaults(out): return default object count
 */
void
ldr_stats (int *errors, int *objects, int *defaults)
{
  if (errors != NULL)
    *errors = Loader.errors;

  if (objects != NULL)
    *objects = Total_objects;

  if (defaults != NULL)
    *defaults = Loader.default_count;
}


/*
 * select_set_domain - looks through a domain list and selects a domain that
 * is one of the set types.
 *    return: NO_ERROR if successful, error code otherwise
 *    domain(in): target doain
 *    set_domain_ptr(out): returned set domain
 * Note:
 *    Work function for ldr_start_set().
 *    In all current cases, there will be only one element in the target
 *    domain list.
 *    Assuming there were more than one, we should be smarter and select
 *    the most "general" domain, for now, just pick the first one.
 */
static int
select_set_domain (TP_DOMAIN * domain, TP_DOMAIN ** set_domain_ptr)
{
  int error = NO_ERROR;
  TP_DOMAIN *best, *d;

  /*
   * Must pick an appropriate set domain, probably we should pick
   * the most general if there are more than one possibilities.
   * In practice, this won't ever happen until we allow nested
   * sets or union domains.
   */
  best = NULL;
  for (d = domain; d != NULL && best == NULL; d = d->next)
    {
      if (TP_IS_SET_TYPE (d->type->id))
	/* pick the first one */
	best = d;
    }

  if (best == NULL)
    error = domain_error (domain, DB_TYPE_SET, NULL);
  else
    {
      if (set_domain_ptr != NULL)
	*set_domain_ptr = best;
    }
  return error;
}


/*
 * ldr_start_set - Prepares the loader to accept incoming data values as the
 * elements of a set.
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    This mode will remain in effect until ldr_end_set is called.
 */
int
ldr_start_set (void)
{
  TP_DOMAIN *domain, *actual = NULL;
  int error = 0;

  /*
   * Formerly we issued an error if the Loader.valid flag was not on,
   * this can result in reams of messages until we get to the next class,
   * Just ignore the operation.
   */
  if (!Loader.valid)
    return NO_ERROR;

  if (Loader.set != NULL)
    {
      error = ER_LDR_NESTED_SET;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      /* get the domain for the current target */
      if ((error = get_domain (&domain)))
	return error;

      /* select an appropriate set domain from the target */
      if ((error = select_set_domain (domain, &actual)))
	return error;

      /* Create an appropriate set, need to get an initial size here ! */
      Loader.set = set_create_with_domain (actual, 0);

      if (Loader.set == NULL)
	{
	  error = er_errid ();
	  ldr_internal_error ();
	}
      else
	{
	  /* store the set subdomain here for further processing */
	  Loader.set_domain = actual->setdomain;

	  switch (setobj_type (Loader.set->set))
	    {
	    case DB_TYPE_SET:
	      DB_MAKE_SET (&Loader.values[Loader.value_count], Loader.set);
	      break;
	    case DB_TYPE_MULTISET:
	      DB_MAKE_MULTISET (&Loader.values[Loader.value_count],
				Loader.set);
	      break;
	    case DB_TYPE_SEQUENCE:
	    case DB_TYPE_VOBJ:
	      DB_MAKE_SEQUENCE (&Loader.values[Loader.value_count],
				Loader.set);
	      break;
	    default:
	      break;
	    }

	  Loader.value_count++;
	}
    }
  return error;
}


/*
 * ldr_end_set - stop the population of a set
 *    return: NO_ERROR if successful, error code otherwise
 * Note:
 *    After calling this function, incoming values will again be treated
 *    as attribute values.
 */
int
ldr_end_set (void)
{
  if (Loader.valid)
    {
      if (Loader.set != NULL)
	{
	  /* should filter the set for duplicates ? */


	  Loader.set = NULL;
	}
    }

  return NO_ERROR;
}


/*
 * ldr_add_value - This is called to prepare storage for an incoming value.
 *    return: NO_ERROR if successful, error code otherwise
 *    token_type(in): type of proposed value
 *    retval(out): returned value container
 * Note:
 *    Assuming the types match, a value container is returned to be
 *    filled in by the caller.
 *    If the type does not match the current target domain, an error
 *    is returned.
 */
int
ldr_add_value (DB_TYPE token_type, DB_VALUE ** retval)
{
  int error = NO_ERROR;
  DB_VALUE *value;
  DB_TYPE value_type;
  DB_DOMAIN *domain;

  /* just ignore the value if we're not in a valid state */
  if (Loader.valid)
    {
      error = check_domain (token_type, &value_type);
      if (error != NO_ERROR)
	{
	  /* make sure we ignore this object and prevent the actual load
	     phase */
	  ldr_invalid_object ();
	}
      else
	{
	  if (Loader.set == NULL)
	    {
	      value = &Loader.values[Loader.value_count];
	      error = get_domain (&domain);
	      if (error != NO_ERROR || domain == NULL)
		{
		  ldr_internal_error ();
		}
	      else
		{
		  db_value_domain_init (value, value_type, domain->precision,
					domain->scale);
		  *retval = value;
		  Loader.value_count++;
		}
	    }
	  else
	    {
	      /*
	       * its a new set element, add an empty value to the set and
	       * initialize the domain.
	       */
	      error = get_domain (&domain);
	      if (error != NO_ERROR)
		{
		  ldr_internal_error ();
		}
	      else
		{
		  value = set_new_element (Loader.set);
		  if (value == NULL)
		    {
		      error = er_errid ();
		      ldr_internal_error ();
		    }
		  else
		    {
		      if (domain == NULL)
			{
			  db_value_domain_init (value, value_type,
						DB_DEFAULT_PRECISION,
						DB_DEFAULT_SCALE);
			}
		      else
			{
			  db_value_domain_init (value, domain->type->id,
						domain->precision,
						domain->scale);
			}
		      *retval = value;
		    }
		}
	    }
	}
    }
  return (error);
}


/*
 * ldr_add_reference - add a value which is a reference to an instance.
 *    return: NO_ERROR if successful, error code otherwise
 *    class(in): class object
 *    id(in): instance id
 * Note:
 *    You cannot use ldr_add_value for this purpose.
 *    The behavior is similar to ldr_add_value except that we don't
 *    allow the caller to fill in the next availabe value container,
 *    instead we locate the referenced instance and fill in the next
 *    value ourselves.
 */
int
ldr_add_reference (MOP class, int id)
{
  int error = NO_ERROR;
  DB_VALUE *value;
  MOP actual_class;
  OID oid;

  /* Don't set an error if we're in an invalid state, instead just ignore
   * the operation.  This prevents reams of messages after the first error
   * is encountered for this class.
   */
  if (!Loader.valid)
    {
      return NO_ERROR;
    }

  error = check_object_domain (class, &actual_class);
  if (error == NO_ERROR)
    {
      if (Loader.set == NULL)
	{
	  value = &Loader.values[Loader.value_count];
	  error = find_instance (actual_class, &oid, id);
	  if (error == ER_LDR_INTERNAL_REFERENCE)
	    {
	      /* allow this, convert the reference to NULL */
	      DB_MAKE_NULL (value);
	    }
	  else
	    {
	      DB_ATTRIBUTE *att;

	      att = Loader.atts[Loader.value_count];
	      if (att->header.name_space == ID_SHARED_ATTRIBUTE ||
		  att->header.name_space == ID_CLASS_ATTRIBUTE ||
		  Loader.attribute_type == LDR_ATTRIBUTE_DEFAULT ||
		  Loader.constructor != NULL)
		{
		  DB_OBJECT *mop;

		  mop = ws_mop (&oid, actual_class);
		  DB_MAKE_OBJECT (value, mop);
		}
	      else
		{
		  /* its not special, just leave the OID in the value */
		  DB_MAKE_OID (value, &oid);
		}
	    }
	  Loader.value_count++;
	}
      else
	{
	  /*
	   * Uses a special set function that appends a new element to the set
	   * and returns a pointer directly to the value.
	   */
	  value = set_new_element (Loader.set);
	  if (value == NULL)
	    {
	      ldr_internal_error ();
	      return er_errid ();
	    }

	  error = find_instance (actual_class, &oid, id);
	  if (error == ER_LDR_INTERNAL_REFERENCE)
	    {
	      /* allow this, convert the reference to NULL */
	      DB_MAKE_NULL (value);
	    }
	  else
	    {
	      DB_MAKE_OID (value, &oid);
	    }
	}
    }
  return (error);
}


/*
 * ldr_add_reference_to_class - add a reference to the class object itself,
 * not an instance of the class.
 *    return: NO_ERROR if successful, error code otherwise
 *    class(in): class object
 * Note:
 *    The current target domain must include the wildcard object domain
 *    "object".
 */
int
ldr_add_reference_to_class (MOP class)
{
  int error = NO_ERROR;
  DB_VALUE *value;

  /* If an error was detected on this class, just ignore the operation,
   * don't set another error or else we clog the log with an error for
   * every instance line for this class.
   */
  if (!Loader.valid)
    {
      return NO_ERROR;
    }

  error = check_class_domain ();
  if (error == NO_ERROR)
    {
      if (Loader.set == NULL)
	{
	  value = &Loader.values[Loader.value_count];
	  DB_MAKE_OBJECT (value, class);
	  Loader.value_count++;
	}
      else
	{
	  value = set_new_element (Loader.set);
	  if (value == NULL)
	    {
	      ldr_internal_error ();
	      return er_errid ();
	    }

	  DB_MAKE_OBJECT (value, class);
	}
    }
  return (error);
}


/*
 * ldr_add_elo - add a reference to an ELO.
 *    return: NO_ERROR if successful, error code otherwise
 *    filename(in): input file
 *    external(in): non-zero if this is an external "fbo"
 * Note:
 *    This can be used only for the system Glo instances.  Normal "users"
 *    of the loader are not allowed to manipulate elo attribute values
 *    directly.
 */
int
ldr_add_elo (const char *filename, int external)
{
  int error = NO_ERROR;
  DB_VALUE *value;
  DB_ELO *elo;

  if (!Loader.valid)
    {
      error = ER_LDR_INVALID_STATE;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
    }
  else
    {
      error = ldr_add_value (DB_TYPE_ELO, &value);
      if (!error)
	{
	  elo = elo_create (filename);
	  if (elo == NULL)
	    error = er_errid ();
	  else
	    {
	      if (!external)
		/* change the type to LO even though we have a pathname */
		elo->type = ELO_LO;
	      db_make_elo (value, elo);
	    }
	}
    }
  return (error);
}


/*
 * ldr_att_name - Returns the name of the current target attribute or argument.
 *    return: name
 */
const char *
ldr_att_name (void)
{
  const char *name = NULL;

  if (Loader.value_count < Loader.att_count)
    name = Loader.atts[Loader.value_count]->header.name;
  else
    /* should return some kind of string representation for
       the current method argument */
    name = "";
  return (name);
}


/*
 * ldr_prev_att_name - Returns the name of the attribute we just finished with.
 *    return: name
 * Note:
 *    This can be used after ldr_add_value has allocated and returned
 *    a value but we detect some kind of parse error after ward that has
 *    to be matched with the previous attribute since ldr_add_value
 *    will have incremented the value count.
 */
const char *
ldr_prev_att_name (void)
{
  const char *name = NULL;

  if (Loader.valid)
    {
      if (Loader.value_count < Loader.att_count)
	{
	  if (Loader.value_count)
	    name = Loader.atts[Loader.value_count - 1]->header.name;
	  else
	    /* haven't processed an attribute yet */
	    name = "";
	}
      else
	/* should return some kind of string representation for
	   the current method argument */
	name = "";
    }
  return (name);
}


/*
 * ldr_att_domain - Returns the current target domain.
 *    return: domain structure
 * Note:
 *    If there are no more domains (i.e. all the values have been filled
 *    in) it returns NULL.
 */
SM_DOMAIN *
ldr_att_domain (void)
{
  SM_DOMAIN *domain = NULL;

  if (Loader.valid)
    (void) get_domain (&domain);

  return domain;
}


/*
 * ldr_class_name - Returns the name of the class currently being loaded.
 *    return: name
 */
const char *
ldr_class_name (void)
{
  const char *name = NULL;

  name = Loader.name;

  return (name);
}


#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ldr_constructor_name -  Returns the name of the constructor method
 * currently defined  for the class.
 *    return: constructor name
 */
const char *
ldr_constructor_name (void)
{
  const char *name = NULL;

  if (Loader.valid && Loader.constructor != NULL)
    name = Loader.constructor->header.name;

  return (name);
}
#endif /* ENABLE_UNUSED_FUNCTION */


/*
 * ldr_att_domain_name - Returns the name of the current target domain.
 *    return: current target domain name
 * Note:
 *    This assumes that there is only one domain in the list and just returns
 *    the name of the primitive type.
 */
const char *
ldr_att_domain_name (void)
{
  TP_DOMAIN *domain;
  const char *name = NULL;

  if (Loader.valid)
    {
      if (!get_domain (&domain))
	{
	  if (domain != NULL)
	    name = domain->type->name;
	}
    }
  return (name);
}


#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * ldr_expected_values - returns the number of values that are expected
 * to be specified to complete an instance description.
 *    return: number of attribute & argument values expected
 */
int
ldr_expected_values (void)
{
  int count = 0;

  if (Loader.valid)
    count = Loader.att_count + Loader.arg_count;

  return count;
}
#endif /* ENABLE_UNUSED_FUNCTION */


/*
 * ldr_register_post_interrupt_handler - registers a post interrupt callback
 * function.
 *    return: void
 *    handler(in): post interrupt handler
 *    ldr_jmp_buf(in): jump buffer
 */
void
ldr_register_post_interrupt_handler (LDR_POST_INTERRUPT_HANDLER handler,
				     jmp_buf * ldr_jmp_buf)
{
  ldr_Jmp_buf = ldr_jmp_buf;
  ldr_post_interrupt_handler = handler;
}


/*
 * ldr_register_post_commit_handler - registers a post commit callback
 * function.
 *    return: void return
 *    handler(in): post commit handler
 */
void
ldr_register_post_commit_handler (LDR_POST_COMMIT_HANDLER handler)
{
  ldr_post_commit_handler = handler;
}


/*
 * ldr_interrupt_has_occurred - set global interrupt type value
 *    return: void
 *    type(in): interrupt type
 */
void
ldr_interrupt_has_occurred (int type)
{
  ldr_Load_interrupted = type;
}
