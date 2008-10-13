/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * sm.c - Schema access functions
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "error_manager.h"
#include "language_support.h"
#include "object_representation.h"
#include "object_domain.h"
#include "work_space.h"
#include "object_primitive.h"
#include "class_object.h"
#include "schema_manager_3.h"
#include "set_object_1.h"
#include "authenticate.h"
#include "virtual_object_1.h"
#include "transform_sky.h"
#include "locator_cl.h"
#include "statistics.h"
#include "network_interface_sky.h"
#include "parser.h"
#include "trigger_manager.h"
#include "common.h"
#include "qp_str.h"
#include "transform.h"
#include "system_parameter.h"
#include "object_template.h"
#include "execute_schema_8.h"
#include "transaction_cl.h"
#include "release_string.h"
#include "execute_statement_10.h"

/* Shorthand for simple warnings and errors */
#define ERROR(error, code) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); } while (0)

#define ERROR1(error, code, arg1) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 1, arg1); } while (0)

#define ERROR2(error, code, arg1, arg2) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 2, arg1, arg2);\
       } while (0)

#define ERROR3(error, code, arg1, arg2, arg3) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 3, arg1, arg2, arg3); \
       } while (0)

#define ERROR4(error, code, arg1, arg2, arg3, arg4) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 4, \
	   arg1, arg2, arg3, arg4); } while (0)

/*
 *    This is used only in the internationalized version.
 *    If this flag is non-zero, it will cause sm_check_name to not perform
 *    any validation of the names supplied to the API.
 *    This is used when the API functions are called by the interpreter
 *    which has already performed the validation and character set
 *    conversion.  Since this is done by the interpreter, we don't need to
 *    duplicate it here.
*/
int sm_Inhibit_identifier_check = 0;

const char TEXT_CONSTRAINT_PREFIX[] = "#text_";

/*
 *    This is the root of the currently active attribute/method descriptors.
 *    These are kept on a list so we can quickly invalidate them during
 *    significant but unusual events like schema changes.
 *    This list is generally short.  If it can be long, consider a
 *    doubly linked list for faster removal.
*/

SM_DESCRIPTOR *sm_Descriptors = NULL;

/* ROOT_CLASS GLOBALS */
/* Global root class structure */
ROOT_CLASS sm_Root_class;

/* Global MOP for the root class object.  Used by the locator */
MOP sm_Root_class_mop = NULL;

/* Name of the root class */
const char *sm_Root_class_name = ROOTCLASS_NAME;

/* Heap file identifier for the root class */
HFID *sm_Root_class_hfid = &sm_Root_class.header.heap;

static unsigned int schema_version_number = 0;

static int domain_search (MOP dclass_mop, MOP class_mop);
static int annotate_method_files (MOP classmop, SM_CLASS * class_);
static int alter_trigger_cache (SM_CLASS * class_, const char *attribute,
				int class_attribute,
				DB_OBJECT * trigger, int drop_it);
static int alter_trigger_hierarchy (DB_OBJECT * classop,
				    const char *attribute,
				    int class_attribute,
				    DB_OBJECT * target_class,
				    DB_OBJECT * trigger, int drop_it);
static const char *sm_get_class_name_internal (MOP op, bool return_null);
static int find_attribute_op (MOP op, const char *name,
			      SM_CLASS ** classp, SM_ATTRIBUTE ** attp);
static int lock_query_subclasses (DB_OBJLIST ** subclasses, MOP op,
				  DB_OBJLIST * exceptions, int update);
static void sm_gc_domain (TP_DOMAIN * domain, void (*gcmarker) (MOP));
static void sm_gc_attribute (SM_ATTRIBUTE * att, void (*gcmarker) (MOP));
static void sm_gc_method (SM_METHOD * meth, void (*gcmarker) (MOP));
static int fetch_descriptor_class (MOP op, SM_DESCRIPTOR * desc,
				   int for_update, SM_CLASS ** class_);

/*
 * sm_set_inhibit_identifier_check()
 *   return:
 *   inhibit(in):
 */

int
sm_set_inhibit_identifier_check (int inhibit)
{
  int current;

  current = sm_Inhibit_identifier_check;
  sm_Inhibit_identifier_check = inhibit;
  return current;
}

/*
 * sm_init() - Called during database restart.
 *    Setup the global variables that contain the root class OID and HFID.
 *    Also initialize the descriptor list
 *   return: none
 *   rootclass_oid(in): OID of root class
 *   rootclass_hfid(in): heap file of root class
 */

void
sm_init (OID * rootclass_oid, HFID * rootclass_hfid)
{

  sm_Root_class_mop = ws_mop (rootclass_oid, NULL);
  oid_Root_class_oid = ws_oid (sm_Root_class_mop);

  sm_Root_class.header.heap.vfid.volid = rootclass_hfid->vfid.volid;
  sm_Root_class.header.heap.vfid.fileid = rootclass_hfid->vfid.fileid;
  sm_Root_class.header.heap.hpgid = rootclass_hfid->hpgid;

  sm_Root_class_hfid = &sm_Root_class.header.heap;

  sm_Descriptors = NULL;
}

/*
 * sm_create_root() - Called when the database is first created.
 *    Sets up the root class globals, used later when the rootclass
 *    is flushed to disk
 *   return: none
 *   rootclass_oid(in): OID of root class
 *   rootclass_hfid(in): heap file of root class
 */

void
sm_create_root (OID * rootclass_oid, HFID * rootclass_hfid)
{
  sm_Root_class.header.obj_header.chn = 0;
  sm_Root_class.header.type = Meta_root;
  sm_Root_class.header.name = (char *) sm_Root_class_name;

  sm_Root_class.header.heap.vfid.volid = rootclass_hfid->vfid.volid;
  sm_Root_class.header.heap.vfid.fileid = rootclass_hfid->vfid.fileid;
  sm_Root_class.header.heap.hpgid = rootclass_hfid->hpgid;
  sm_Root_class_hfid = &sm_Root_class.header.heap;

  /* Sets up sm_Root_class_mop and Rootclass_oid */
  locator_add_root (rootclass_oid, (MOBJ) & sm_Root_class);
}


/*
 * sm_final() - Called during the shutdown sequence
 */

void
sm_final ()
{
  SM_DESCRIPTOR *d, *next;
  SM_CLASS *class_;
  DB_OBJLIST *cl;

#if defined(WINDOWS)
  /* unload any DLL's we may have opened for methods */
  sm_method_final ();
#endif /* WINDOWS */

  /* If there are any remaining descriptors it represents a memory leak
     in the application. Should be displaying warning messages here !
   */

  for (d = sm_Descriptors, next = NULL; d != NULL; d = next)
    {
      next = d->next;
      sm_free_descriptor (d);
    }

  /* go through the resident class list and free anything attached
     to the class that wasn't allocated in the workspace, this is
     only the virtual_query_cache at this time */
  for (cl = ws_Resident_classes; cl != NULL; cl = cl->next)
    {
      class_ = (SM_CLASS *) cl->op->object;
      if (class_ != NULL && class_->virtual_query_cache != NULL)
	{
	  mq_free_virtual_query_cache (class_->virtual_query_cache);
	  class_->virtual_query_cache = NULL;
	}
    }
}

/*
 * sm_transaction_boundary() - This is called by tm_commit() and tm_abort()
 *    to inform the schema manager that a transaction boundary has been crossed.
 *    If the commit-flag is non-zero it indicates that we've committed
 *    the transaction.
 *    We used to call sm_bump_schema_version directly from the tm_ functions.
 *    Now that we have more than one thing to do however, start
 *    encapsulating them in a module specific stransaction boundary handler
 *    so we don't have to keep modifying tmcl.c
 */

void
sm_transaction_boundary (void)
{
  /* reset any outstanding descriptor caches */
  sm_reset_descriptors (NULL);

  /* Could be resetting the transaction caches in each class too
     but the workspace is controlling that */
}

/* UTILITY FUNCTIONS */
/*
 * sm_check_name() - This is made void for ansi compatibility.
 *      It prevoudly insured that identifiers which were accepted could be
 *      parsed in the language interface.
 *
 *  	ANSI allows any character in an identifier. It also allows reserved
 *  	words. In order to parse identifiers with non-alpha characters
 *  	or that are reserved words, an escape syntax is defined with double
 *  	quotes, "FROM", for example
 *   return: non-zero if name is ok
 *   name(in): name to check
 */

int
sm_check_name (const char *name)
{
  if (name == NULL || name[0] == '\0')
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_NAME, 1,
	      name);
      return 0;
    }
  else
    return 1;
}

/*
 * sm_downcase_name() - This is a kludge to make sure that class names are
 *    always converted to lower case in the API.
 *    This conversion is already done by the parser so we must be consistent.
 *    This is necessarly largely becuase the eh_ module on the server does not
 *    offer a mode for case insensitive string comparison.
 *    Is there a system function that does this? I couldn't find one
 *   return: none
 *   name(in): class name
 *   buf(out): output buffer
 *   maxlen(in): maximum buffer length
 */

void
sm_downcase_name (const char *name, char *buf, int maxlen)
{
  intl_mbs_nlower (buf, name, maxlen);
}

/*
 * sm_resolution_space() -  This is used to convert a full component
 *    name_space to one of the more constrained resolution namespaces.
 *   return: resolution name_space
 *   name_space(in): component name_space
 */

SM_NAME_SPACE
sm_resolution_space (SM_NAME_SPACE name_space)
{
  SM_NAME_SPACE res_space = ID_INSTANCE;

  if (name_space == ID_CLASS_ATTRIBUTE || name_space == ID_CLASS_METHOD)
    res_space = ID_CLASS;

  return (res_space);
}

/* CLASS LOCATION FUNCTIONS */
/*
 * sm_get_class() - This is just a convenience function used to
 *    return a class MOP for any possible MOP.
 *    If this is a class mop return it, if this is  an object MOP,
 *    fetch the class and return its mop.
 *   return: class mop
 *   obj(in): object or class mop
 */

MOP
sm_get_class (MOP obj)
{
  MOP op = NULL;

  if (obj != NULL)
    {
      if (locator_is_class (obj, DB_FETCH_READ))
	op = obj;
      else
	{
	  if (obj->class_mop == NULL)
	    /* force class load through object load */
	    (void) au_fetch_class (obj, NULL, AU_FETCH_READ, AU_SELECT);
	  op = obj->class_mop;
	}
    }
  return (op);
}

/*
 * sm_fetch_all_classes() - Fetch all classes for a given purpose.
 *    Builds a list of all classes in the system.  Be careful to filter
 *    out the root class since it isn't really a class to the callers.
 *    The external_list flag is set when the object list is to be returned
 *    above the database interface layer (db_ functions) and must therefore
 *    be allocated in storage that will serve as roots to the garbage
 *    collector.
 *   return: object list of class MOPs
 *   external_list(in): non-zero if external list links are to be used
 *   purpose(in): Fetch purpose
 */
DB_OBJLIST *
sm_fetch_all_classes (int external_list, DB_FETCH_MODE purpose)
{
  LIST_MOPS *lmops;
  DB_OBJLIST *objects, *last, *new_;
  int i;

  objects = NULL;
  lmops = NULL;
  if (au_check_user () == NO_ERROR)
    {				/* make sure we have a user */
      last = NULL;
      lmops = locator_get_all_mops (sm_Root_class_mop, purpose);
      /* probably should make sure we push here because the list could be long */
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      /* is it necessary to have this check ? */
	      if (!WS_MARKED_DELETED (lmops->mops[i]) &&
		  lmops->mops[i] != sm_Root_class_mop)
		{
		  if (!external_list)
		    {
		      if (ml_append (&objects, lmops->mops[i], NULL))
			goto memory_error;
		    }
		  else
		    {
		      /* should have a ext_ append function */
		      new_ = ml_ext_alloc_link ();
		      if (new_ == NULL)
			goto memory_error;
		      new_->op = lmops->mops[i];
		      new_->next = NULL;
		      if (last != NULL)
			last->next = new_;
		      else
			objects = new_;
		      last = new_;
		    }
		}
	    }
	  locator_free_list_mops (lmops);
	}
    }
  return (objects);

memory_error:
  if (lmops != NULL)
    locator_free_list_mops (lmops);
  if (external_list)
    ml_ext_free (objects);
  else
    ml_free (objects);
  return NULL;
}

/*
 * sm_fetch_base_classes() - Fetch base classes for the given mode.
 *   Returns a list of classes that have no super classes.
 *   return: list of class MOPs
 *   external_list(in): non-zero to create external MOP list
 *   purpose(in): Fetch purpose
 */

DB_OBJLIST *
sm_fetch_all_base_classes (int external_list, DB_FETCH_MODE purpose)
{
  LIST_MOPS *lmops;
  DB_OBJLIST *objects, *last, *new_;
  int i;
  int error;
  SM_CLASS *class_;

  objects = NULL;
  lmops = NULL;
  if (au_check_user () == NO_ERROR)
    {				/* make sure we have a user */
      last = NULL;
      lmops = locator_get_all_mops (sm_Root_class_mop, purpose);
      /* probably should make sure we push here because the list could be long */
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      /* is it necessary to have this check ? */
	      if (!WS_MARKED_DELETED (lmops->mops[i]) &&
		  lmops->mops[i] != sm_Root_class_mop)
		{
		  error =
		    au_fetch_class_force (lmops->mops[i], &class_,
					  AU_FETCH_READ);
		  if (error != NO_ERROR)
		    {
		      /* problems accessing the class list, abort */
		      locator_free_list_mops (lmops);
		      ml_ext_free (objects);
		      return (NULL);
		    }
		  /* only put classes without supers on the list */
		  else if (class_->inheritance == NULL)
		    {
		      if (!external_list)
			{
			  if (ml_append (&objects, lmops->mops[i], NULL))
			    goto memory_error;
			}
		      else
			{
			  /* should have a ext_ append function */
			  new_ = ml_ext_alloc_link ();
			  if (new_ == NULL)
			    goto memory_error;
			  new_->op = lmops->mops[i];
			  new_->next = NULL;
			  if (last != NULL)
			    last->next = new_;
			  else
			    objects = new_;
			  last = new_;
			}
		    }
		}
	    }
	  locator_free_list_mops (lmops);
	}
    }
  return (objects);

memory_error:
  if (lmops != NULL)
    locator_free_list_mops (lmops);
  if (external_list)
    ml_ext_free (objects);
  else
    ml_free (objects);
  return NULL;
}


/*
 * sm_get_all_classes() -  Builds a list of all classes in the system.
 *    Be careful to filter out the root class since it isn't really a class
 *    to the callers. The external_list flag is set when the object list is
 *    to be returned above the database interface layer (db_ functions) and
 *    must therefore be allocated in storage that will serve as roots to
 *    the garbage collector.
 *    Authorization checking is not performed at this level so there may be
 *    MOPs in the list that you can't actually access.
 *   return: object list of class MOPs
 *   external_list(in): non-zero if external list links are to be used
 */

DB_OBJLIST *
sm_get_all_classes (int external_list)
{
  /* Lock all the classes in shared mode */
  return sm_fetch_all_classes (external_list, DB_FETCH_QUERY_READ);
}				/* sm_get_all_classes */

/*
 * sm_get_base_classes() - Returns a list of classes that have no super classes
 *   return: list of class MOPs
 *   external_list(in): non-zero to create external MOP list
*/
DB_OBJLIST *
sm_get_base_classes (int external_list)
{
  /* Lock all the classes in shared mode */
  return sm_fetch_all_base_classes (external_list, DB_FETCH_QUERY_READ);
}


/* OBJECT LOCATION */
/*
 * sm_get_all_objects() - Returns a list of all the instances that have
 *    been created for a class.
 *    This was used early on before query was available, it should not
 *    be heavily used now.  Be careful, this can potentially bring
 *    in lots of objects and overflow the workspace.
 *    This is used in the implementation of a db_ function so it must
 *    allocate an external mop list !
 *   return: list of objects
 *   op(in): class or instance object
 *   purpose(in): Fetch purpose
 */

DB_OBJLIST *
sm_fetch_all_objects (DB_OBJECT * op, DB_FETCH_MODE purpose)
{
  LIST_MOPS *lmops;
  SM_CLASS *class_;
  DB_OBJLIST *objects, *new_;
  MOP classmop;
  SM_CLASS_TYPE ct;
  int i;

  objects = NULL;
  classmop = NULL;
  lmops = NULL;
  if (op != NULL)
    {
      if (locator_is_class (op, purpose))
	classmop = op;
      else
	{
	  if (op->class_mop == NULL)
	    {
	      /* force load */
	      (void) au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
	    }
	  classmop = op->class_mop;
	}
      if (classmop != NULL)
	{
	  class_ = (SM_CLASS *) classmop->object;
	  if (!class_)
	    (void) au_fetch_class (classmop, &class_, AU_FETCH_READ,
				   AU_SELECT);
	  if (!class_)
	    return NULL;
	  ct = sm_get_class_type (class_);
	  if (ct == SM_CLASS_CT)
	    {
	      lmops = locator_get_all_mops (classmop, purpose);
	      if (lmops != NULL)
		{
		  for (i = 0; i < lmops->num; i++)
		    {
		      /* is it necessary to have this check ? */
		      if (!WS_MARKED_DELETED (lmops->mops[i]))
			{
			  new_ = ml_ext_alloc_link ();
			  if (new_ == NULL)
			    goto memory_error;
			  new_->op = lmops->mops[i];
			  new_->next = objects;
			  objects = new_;
			}
		    }
		  locator_free_list_mops (lmops);
		}
	    }
	  else
	    {
	      objects = vid_getall_mops (classmop, class_, purpose);
	    }
	}
    }
  return (objects);

memory_error:
  if (lmops != NULL)
    locator_free_list_mops (lmops);
  ml_ext_free (objects);
  return NULL;
}


/*
 * sm_get_all_objects() - Returns a list of all the instances that
 *    have been created for a class.
 *    This was used early on before query was available, it should not
 *    be heavily used now.  Be careful, this can potentially bring
 *    in lots of objects and overflow the workspace.
 *    This is used in the implementation of a db_ function so it must
 *    allocate an external mop list !
 *   return: list of objects
 *   op(in): class or instance object
 */

DB_OBJLIST *
sm_get_all_objects (DB_OBJECT * op)
{
  return sm_fetch_all_objects (op, DB_FETCH_QUERY_READ);
}

/* MISC SCHEMA OPERATIONS */
/*
 * sm_rename_class() - This is used to change the name of a class if possible.
 *    It is not part of the smt_ template layer because its a fairly
 *    fundamental change that must be checked early.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in/out): class mop
 *   new_name(in):
 */

int
sm_rename_class (MOP op, const char *new_name)
{
  int error;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  const char *current, *newname;
  char realname[SM_MAX_IDENTIFIER_LENGTH];
  int is_partition = 0, subren = 0;
/*  TR_STATE *trstate; */

  /* make sure this gets into the server table with no capitalization */
  sm_downcase_name (new_name, realname, SM_MAX_IDENTIFIER_LENGTH);

  if (sm_has_text_domain (db_get_attributes (op), 1))
    {
      /* prevent to rename class */
      ERROR1 (error, ER_REGU_NOT_IMPLEMENTED, rel_major_release_string ());
      return error;
    }

  error = do_is_partitioned_classobj (&is_partition, op, NULL, NULL);
  if (is_partition == 1)
    {
      if ((error = tran_savepoint (UNIQUE_PARTITION_SAVEPOINT_RENAME,
				   false)) != NO_ERROR)
	return error;
      if ((error = do_rename_partition (op, realname)) != NO_ERROR)
	{
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void)
		tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_RENAME);
	    }
	  return error;
	}
      subren = 1;
    }

  if (!sm_check_name (realname))
    error = er_errid ();

  else if ((error = au_fetch_class (op, &class_, AU_FETCH_UPDATE, AU_ALTER))
	   == NO_ERROR)
    {
      /*  We need to go ahead and copy the string since prepare_rename uses
       *  the address of the string in the hash table.
       */
      current = class_->header.name;
      newname = ws_copy_string (realname);
      if (newname == NULL)
	return er_errid ();
      if (locator_prepare_rename_class (op, current, newname) == NULL)
	{
	  ws_free_string (newname);
	  error = er_errid ();
	}
      else
	{
	  if (class_->class_type == SM_LDBVCLASS_CT)
	    {
	      /* do not update system catalog table, only update ldb_proxies.
	       */
	      ws_clean (op);
	      /* TODO : mtpi.c/db_rename_proxy() */
	      /* reserve to update system catalog table
	       */
	      ws_dirty (op);
	    }

	  /* rename related auto_increment serial obj name */
	  FOR_ATTRIBUTES (class_->attributes, att)
	  {
	    if (att->auto_increment != NULL)
	      {
		DB_VALUE name_val;
		char *class_name;

		if (db_get (att->auto_increment, "class_name", &name_val) !=
		    NO_ERROR)
		  break;

		class_name = DB_GET_STRING (&name_val);
		if (class_name != NULL &&
		    (strcmp (class_->header.name, class_name) == 0))
		  {
		    error =
		      do_update_auto_increment_serial_on_rename (att->
								 auto_increment,
								 newname,
								 att->
								 header.name);
		  }
		db_value_clear (&name_val);

		if (error != NO_ERROR)
		  break;
	      }
	  }

	  class_->header.name = newname;
	  ws_free_string (current);
/*      tr_after(trstate); */
	  if (error == NO_ERROR)
	    error = sm_flush_objects (op);
	}
    }
  if (subren && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    (void) tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_RENAME);
  return (error);
}

/*
 * sm_mark_system_classes() - Hack used to set the "system class" flag for
 *    all currently resident classes.
 *    This is only to make it more convenient to tell the
 *    difference between CUBRID and user defined classes.  This is intended
 *    to be called after the appropriate CUBRID class initialization function.
 *    Note that authorization is disabled here because these are normally
 *    called on the authorization classes.
 */

void
sm_mark_system_classes (void)
{
  LIST_MOPS *lmops;
  SM_CLASS *class_;
  int i;

  if (au_check_user () == NO_ERROR)
    {
      lmops = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_WRITE);
      if (lmops != NULL)
	{
	  for (i = 0; i < lmops->num; i++)
	    {
	      if (!WS_MARKED_DELETED (lmops->mops[i]) && lmops->mops[i]
		  != sm_Root_class_mop)
		{
		  if (au_fetch_class_force
		      (lmops->mops[i], &class_, AU_FETCH_UPDATE) == NO_ERROR)
		    {
		      class_->flags |= SM_CLASSFLAG_SYSTEM;
		    }
		}
	    }
	  locator_free_list_mops (lmops);
	}
    }
}

/*
 * sm_mark_system_class() - This turns on or off the system class flag.
 *   This flag is tested by the sm_is_system_class function.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop (in): class pointer
 *   on_or_off(in): state of the flag
 */

int
sm_mark_system_class (MOP classop, int on_or_off)
{
  SM_CLASS *class_;
  int error = NO_ERROR;

  if (classop != NULL)
    {
      if ((error = au_fetch_class_force (classop, &class_, AU_FETCH_UPDATE))
	  == NO_ERROR)
	{
	  if (on_or_off)
	    class_->flags |= SM_CLASSFLAG_SYSTEM;
	  else
	    class_->flags &= ~SM_CLASSFLAG_SYSTEM;
	}
    }
  return (error);
}

#ifdef SA_MODE
void
sm_mark_system_class_for_catalog (void)
{
  MOP classmop;
  SM_CLASS *class_;
  int i;

  const char *classes[] = {
    CT_CLASS_NAME, CT_ATTRIBUTE_NAME, CT_DOMAIN_NAME,
    CT_METHOD_NAME, CT_METHSIG_NAME, CT_METHARG_NAME,
    CT_METHFILE_NAME, CT_QUERYSPEC_NAME, CT_INDEX_NAME,
    CT_INDEXKEY_NAME, CT_CLASSAUTH_NAME, CT_DATATYPE_NAME,
    CT_STORED_PROC_NAME, CT_STORED_PROC_ARGS_NAME, CT_PARTITION_NAME,
    CTV_CLASS_NAME, CTV_SUPER_CLASS_NAME, CTV_VCLASS_NAME,
    CTV_ATTRIBUTE_NAME, CTV_ATTR_SD_NAME, CTV_METHOD_NAME,
    CTV_METHARG_NAME, CTV_METHARG_SD_NAME, CTV_METHFILE_NAME,
    CTV_INDEX_NAME, CTV_INDEXKEY_NAME, CTV_AUTH_NAME,
    CTV_TRIGGER_NAME, CTV_STORED_PROC_NAME, CTV_STORED_PROC_ARGS_NAME,
    CTV_PARTITION_NAME, NULL
  };

  for (i = 0; classes[i] != NULL; i++)
    {
      classmop = locator_find_class (classes[i]);
      if (au_fetch_class_force (classmop, &class_, AU_FETCH_UPDATE) ==
	  NO_ERROR)
	{
	  class_->flags |= SM_CLASSFLAG_SYSTEM;
	}
    }
}
#endif /* SA_MODE */

/*
 * sm_set_class_flag() - This turns on or off the given flag.
 *    The flag may be tested by the sm_get_class_flag function.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop (in): class pointer
 *   flag  (in): flag to set or clear
 *   on_or_off(in): 1 to set 0 to clear
 */

int
sm_set_class_flag (MOP classop, SM_CLASS_FLAG flag, int on_or_off)
{
  SM_CLASS *class_;
  int error = NO_ERROR;

  if (classop != NULL)
    {
      if ((error = au_fetch_class_force (classop, &class_, AU_FETCH_UPDATE))
	  == NO_ERROR)
	{
	  if (on_or_off)
	    class_->flags |= flag;
	  else
	    class_->flags &= ~flag;
	}
    }
  return (error);
}

/*
 * sm_is_system_class() - Tests the sytem class flag of a class object.
 *   return: non-zero if class is a system defined class
 *   op(in): class object
 */

int
sm_is_system_class (MOP op)
{
  SM_CLASS *class_;
  int is_system = false;

  if (op != NULL && locator_is_class (op, DB_FETCH_READ))
    {
      if (au_fetch_class_force (op, &class_, AU_FETCH_READ) == NO_ERROR)
	{
	  if (class_->flags & SM_CLASSFLAG_SYSTEM)
	    is_system = true;
	}
    }
  return (is_system);
}

/*
 * sm_get_class_flag() - Tests the class flag of a class object.
 *   return: non-zero if flag set
 *   op(in): class object
 *   flag(in): flag to test
 */

int
sm_get_class_flag (MOP op, SM_CLASS_FLAG flag)
{
  SM_CLASS *class_;
  int result = 0;

  if (op != NULL && locator_is_class (op, DB_FETCH_READ))
    {
      if (au_fetch_class_force (op, &class_, AU_FETCH_READ) == NO_ERROR)
	{
	  result = class_->flags & flag;
	}
    }
  return (result);
}



/*
 * sm_force_write_all_classes()
 *   return: NO_ERROR on success, non-zero for ERROR
 */

int
sm_force_write_all_classes (void)
{
  LIST_MOPS *lmops;
  int i;

  /* get all class objects */
  lmops = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_QUERY_WRITE);
  if (lmops != NULL)
    {

      for (i = 0; i < lmops->num; i++)
	ws_dirty (lmops->mops[i]);

      /* insert all class objects into the catalog classes */
      if (locator_flush_all_instances (sm_Root_class_mop, DONT_DECACHE) !=
	  NO_ERROR)
	return er_errid ();

      for (i = 0; i < lmops->num; i++)
	ws_dirty (lmops->mops[i]);

      /* update class hierarchy values for some class objects.
       * the hierarchy makes class/class mutual references
       * so some class objects were inserted with no hierarchy values.
       */
      if (locator_flush_all_instances (sm_Root_class_mop, DONT_DECACHE) !=
	  NO_ERROR)
	return er_errid ();

      locator_free_list_mops (lmops);
    }
  return NO_ERROR;
}

/*
 * sm_destroy_representations() - This is called by the compaction utility
 *    after it has swept through the instances of a class and converted them
 *    all the latest representation.
 *    Once this is done, the schema manager no longer needs to maintain
 *    the history of old representations.  In order for this to become
 *    persistent, the transaction must be committed.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): class object
 */

int
sm_destroy_representations (MOP op)
{
  int error = NO_ERROR;
  SM_CLASS *class_;

  error = au_fetch_class_force (op, &class_, AU_FETCH_UPDATE);
  if (error == NO_ERROR)
    {
      ws_list_free ((DB_LIST *) class_->representations,
		    (LFREEER) classobj_free_representation);
      class_->representations = NULL;
    }
  return (error);
}

/* DOMAIN MAINTENANCE FUNCTIONS */

/*
 * sm_filter_domain() - This removes any invalid domain references from a
 *    domain list.  See description of filter_domain_list for more details.
 *    If the domain list was changed, we could get a write lock on the owning
 *    class to ensure that the change is made persistent.
 *    Making the change persistent doesn't really improve much since we
 *    always have to do a filter pass when the class is fetched.
 *   return: non-zero if changes were made
 *   domain(in): domain list for attribute or method arg
 */

int
sm_filter_domain (TP_DOMAIN * domain)
{
  int changes = 0;

  if (domain != NULL)
    {
      changes = tp_domain_filter_list (domain);
      /* if changes, could get write lock on owning_class here */
    }

  return (changes);
}

/*
 * domain_search() - This recursively searches through the class hierarchy
 *    to see if the "class_mop" is equal to or a subclass of "dclass_mop"
 *    in which case it is within the domain of dlcass_mop.
 *    This is essentially the same as sm_is_superclass except that it
 *    doesn't check for authorization.
 *   return: non-zero if the class was valid
 *   dclass_mop(in): domain class
 *   class_mop(in): class in question
 */

static int
domain_search (MOP dclass_mop, MOP class_mop)
{
  DB_OBJLIST *cl;
  SM_CLASS *class_;
  int ok = 0;

  if (dclass_mop == class_mop)
    ok = 1;
  else
    {
      /* ignore authorization for the purposes of domain checking */
      if (au_fetch_class_force (class_mop, &class_, AU_FETCH_READ) ==
	  NO_ERROR)
	{
	  for (cl = class_->inheritance; cl != NULL && !ok; cl = cl->next)
	    ok = domain_search (dclass_mop, cl->op);
	}
    }
  return (ok);
}

/*
 * sm_check_object_domain() - This checks to see if an instance is valid for
 *    a given domain. It checks to see if the instance's class is equal to or
 *    a subclass of the class in the domain.  Also handles the various NULL
 *    conditions.
 *   return: non-zero if object is within the domain
 *   domain(in): domain to examine
 *   object(in): instance
 */

int
sm_check_object_domain (TP_DOMAIN * domain, MOP object)
{
  int ok;

  ok = 0;
  if (domain->type == tp_Type_object)
    {

      /* check for physical and logical NULLness of the MOP, treat it
         as if it were SQL NULL which is allowed in all domains */
      if (WS_MOP_IS_NULL (object))
	{
	  ok = 1;
	}
      /* check for the wildcard object domain */
      else if (domain->class_mop == NULL)
	{
	  ok = 1;
	}
      else
	{
	  /* fetch the class if it hasn't been cached, should this be a write
	     lock ?  don't need to pin, only forcing the class fetch
	   */
	  if (object->class_mop == NULL)
	    {
	      au_fetch_instance (object, NULL, AU_FETCH_READ, AU_SELECT);
	    }

	  /* if its still NULL, assume an authorization error and go on */
	  if (object->class_mop != NULL)
	    {
	      ok = domain_search (domain->class_mop, object->class_mop);
	    }
	}
    }

  return (ok);
}

/*
 * sm_coerce_object_domain() - This checks to see if an instance is valid for
 *    a given domain.
 *    It checks to see if the instance's class is equal to or a subclass
 *    of the class in the domain.  Also handles the various NULL
 *    conditions.
 *    If dest_object is not NULL and the object is a view on a real object,
 *    the real object will be returned.
 *   return: non-zero if object is within the domain
 *   domain(in): domain to examine
 *   object(in): instance
 *   dest_object(out): ptr to instance to coerce object to
 */

int
sm_coerce_object_domain (TP_DOMAIN * domain, MOP object, MOP * dest_object)
{
  int ok;
  SM_CLASS *class_;

  ok = 0;
  if (!dest_object)
    return 0;
  if (domain->type == tp_Type_object)
    {

      /* check for physical and logical NULLness of the MOP, treat it
         as if it were SQL NULL which is allowed in all domains */
      if (WS_MOP_IS_NULL (object))
	ok = 1;

      /* check for the wildcard object domain */
      else if (domain->class_mop == NULL)
	ok = 1;

      else
	{
	  /* fetch the class if it hasn't been cached, should this be a write lock ?
	     don't need to pin, only forcing the class fetch
	   */
	  if (object->class_mop == NULL)
	    au_fetch_instance (object, NULL, AU_FETCH_READ, AU_SELECT);

	  /* if its still NULL, assume an authorization error and go on */
	  if (object->class_mop != NULL)
	    {
	      if (domain->class_mop == object->class_mop)
		{
		  ok = 1;
		}
	      else
		{
		  if (au_fetch_class_force (object->class_mop, &class_,
					    AU_FETCH_READ) == NO_ERROR)
		    {
		      /* Coerce a view to a real class. */
		      if (class_->class_type == SM_VCLASS_CT)
			{
			  object = vid_get_referenced_mop (object);
			  if (object &&
			      (au_fetch_class_force (object->class_mop,
						     &class_,
						     AU_FETCH_READ) ==
			       NO_ERROR)
			      && (class_->class_type == SM_CLASS_CT))
			    {
			      ok = domain_search (domain->class_mop,
						  object->class_mop);
			    }
			}
		      else
			{
			  ok = domain_search (domain->class_mop,
					      object->class_mop);
			}
		    }
		}
	    }
	}
    }

  if (ok)
    {
      *dest_object = object;
    }

  return (ok);
}

/*
 * sm_check_class_domain() - see if a class is within the domain.
 *    It is similar to sm_check_object_domain except that we get
 *    a pointer directly to the class and we don't allow NULL conditions.
 *   return: non-zero if the class is within the domain
 *   domain(in): domain to examine
 *   class(in): class to look for
 */

int
sm_check_class_domain (TP_DOMAIN * domain, MOP class_)
{
  int ok = 0;

  if (domain->type == tp_Type_object && class_ != NULL)
    {

      /* check for domain class deletions and other delayed updates
         SINCE THIS IS CALLED FOR EVERY ATTRIBUTE UPDATE, WE MUST EITHER
         CACHE THIS INFORMATION OR PERFORM IT ONCE WHEN THE CLASS
         IS FETCHED */
      (void) sm_filter_domain (domain);

      /* wildcard case */
      if (domain->class_mop == NULL)
	ok = 1;
      else
	{
	  /* recursively check domains for class & super classes
	     for now assume only one possible base class */
	  ok = domain_search (domain->class_mop, class_);
	}
    }
  return (ok);
}


/*
 * sm_get_set_domain() - used only by the set support to get the domain list for
 *    the attribute that owns a set.  Need to be careful that the cached
 *    domain pointer is cleared if the class is ever swapped out.
 *   return: domain list
 *   classop(in): class mop
 *   att_id(in): attribute id
 */

TP_DOMAIN *
sm_get_set_domain (MOP classop, int att_id)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  TP_DOMAIN *domain;

  domain = NULL;
  if (au_fetch_class_force (classop, &class_, AU_FETCH_READ) == NO_ERROR)
    {
      att = NULL;

      /* search the attribute spaces, ids won't overlap */
      for (att = class_->attributes; att != NULL && att->id != att_id;
	   att = (SM_ATTRIBUTE *) att->header.next);
      if (att == NULL)
	{
	  for (att = class_->shared; att != NULL && att->id != att_id;
	       att = (SM_ATTRIBUTE *) att->header.next);
	  if (att == NULL)
	    {
	      for (att = class_->class_attributes;
		   att != NULL && att->id != att_id;
		   att = (SM_ATTRIBUTE *) att->header.next);
	    }
	}

      if (att != NULL)
	domain = att->domain;
    }
  return (domain);
}

/* POST-LOAD CLEANUP */
/*
 * This is a bit of a kludge to update the run-time class structures with
 * information that isn't stored in the disk representation.
 * This won't be called by the transformer directly after load because some
 * of the cleanup functions require class fetches and we don't in general
 * want to cause recursive fetches if we're already in the middle of one.
 * This doesn't hurt, its just best to avoid.  We also don't know
 * what the MOP of the class will be in the transformer so we have to delay
 * this cleanup until the class has been cached.
 *
 * When the transformer brings in a class, the post_load_cleanup bit
 * will be zero.  This flag will be checked at appropriate times and
 * the sm_clean_class function is called.
 *
 * NOTE: The things that can be defered in this way must only be things
 * required for schema modification as sm_clean_class is only called by
 * the class editing and browsing functions.  You cannot defer the setup
 * of run time data that is required for normal operations on instances
 * of the class.  In particular, the trigger cache must be handled
 * in a different way since it must be accessible immediately after
 * the class is loaded.
 *
 * NOTE: The cleanup routines will now check for references to deleted
 * domains.  This is a relatively expensive operation.
 */

/*
 * annotate_method_files() - This is a kludge to work around the fact that
 *    we don't store origin or source classes with method files.
 *    These have inheritance semantics like the other class components.
 *    They can't be deleted if they were not locally defined etc.
 *    The source class needs to be stored in the disk representation but
 *    since we can't change that until 2.0, we have to fake it and compute
 *    the source class after the class has been brought in from disk.
 *    Warning, since the transformer doesn't have the class MOP when it
 *    is building the class structures, it can't call this with a valid
 *    MOP for the actual class.  In this case, we let the class pointer
 *    remain NULL and assume that that "means" this is a local attribute.
 *    If we ever support the db_methfile_source() function, this will
 *    need to be fixed.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classmop(in):
 *   class(in): class being edited
 */

static int
annotate_method_files (MOP classmop, SM_CLASS * class_)
{
  DB_OBJLIST *cl;
  SM_CLASS *super;
  SM_METHOD_FILE *f;

  if (class_->method_files != NULL)
    {

      /* might want to have the class loop outside and make multiple passes over
         the method files ?  Probably doesn't matter much */

      for (f = class_->method_files; f != NULL; f = f->next)
	{
	  if (f->class_mop == NULL)
	    {
	      for (cl = class_->inheritance;
		   cl != NULL && f->class_mop == NULL; cl = cl->next)
		{
		  if (au_fetch_class_force (cl->op, &super, AU_FETCH_READ) !=
		      NO_ERROR)
		    return (er_errid ());
		  else
		    {
		      if (NLIST_FIND (super->method_files, f->name) != NULL)
			{
			  f->class_mop = cl->op;
			}
		    }
		}
	      /* if its still NULL, assume its defined locally */
	      if (f->class_mop == NULL)
		{
		  f->class_mop = classmop;
		}
	    }
	}
    }
  return (NO_ERROR);
}

/*
 * sm_clean_class() - used mainly before constructing a class template but
 *    it could be used in other places as well.  It will walk through the
 *    class structure and prune out any references to deleted objects
 *    in domain lists, etc. and do any other housekeeping tasks that it is
 *    convenient to delay until a major operation is performed.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classmop(in):
 *   class(in/out): class structure
 */

int
sm_clean_class (MOP classmop, SM_CLASS * class_)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;

  if (!class_->transaction_cache)
    {
      /* we only need to do this once because once we have read locks,
         the referenced classes can't be deleted */

      FOR_ATTRIBUTES (class_->attributes, att) sm_filter_domain (att->domain);
      FOR_ATTRIBUTES (class_->shared, att) sm_filter_domain (att->domain);
      FOR_ATTRIBUTES (class_->class_attributes, att)
	sm_filter_domain (att->domain);

      class_->transaction_cache = 1;
    }

  if (!class_->post_load_cleanup)
    {
      /* initialize things that weren't done by the transformer */

      error = annotate_method_files (classmop, class_);

      class_->post_load_cleanup = 1;
    }

  return (error);
}

/*
 * sm_reset_transaction_cache() - This is called by the workspace manager on
 *    transaction boundaries to clear any cached information int the class
 *    structure that cannot be maintained over the boundary.
 *   return: none
 *   classmop (in): class object pointer
 *   class(in/out): class structure
 */

void
sm_reset_transaction_cache (SM_CLASS * class_)
{
  if (class_ != NULL)
    {
      class_->transaction_cache = 0;
    }
}

/* CLASS STATISTICS FUNCTIONS */
/*
 * sm_get_class_with_statistics() - Fetches and returns the stastics information for a
 *    class from the system catalog on the server.
 *    Must make sure callers keep the class MOP visible to the garabge
 *    collector so the stat structures don't get reclaimed.
 *    Currently used only by the query optimizer.
 *   return: class object which contains statistics structure
 *   classop(in): class object
 */

SM_CLASS *
sm_get_class_with_statistics (MOP classop)
{
  SM_CLASS *class_ = NULL;

  /* only try to get statistics if we know the class has been flushed
     if it has a temporary oid, it isn't flushed and there are no statistics */

  if (classop != NULL &&
      locator_is_class (classop, DB_FETCH_QUERY_READ) &&
      !OID_ISTEMP (WS_OID (classop)))
    {
      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) ==
	  NO_ERROR)
	{
	  if (class_->stats == NULL)
	    {
	      /* it's first time to get the statistics of this class */
	      if (!OID_ISTEMP (WS_OID (classop)))
		{
		  /* make sure the class is flushed before asking for statistics,
		     this handles the case where an index has been added to the class
		     but the catalog & statistics do not reflect this fact until
		     the class is flushed.  We might want to flush instances
		     as well but that shouldn't affect the statistics ? */
		  if (locator_flush_class (classop) != NO_ERROR)
		    return (NULL);
		  class_->stats = stats_get_statistics (WS_OID (classop), 0);
		}
	    }
	  else
	    {
	      CLASS_STATS *stats;

	      /* to get the statistics to be updated, it send timestamp
	         as uninitialized value */
	      stats = stats_get_statistics (WS_OID (classop),
					    class_->stats->time_stamp);
	      /* if newly updated statistics are fetched, replace the old one */
	      if (stats != NULL)
		{
		  stats_free_statistics (class_->stats);
		  class_->stats = stats;
		}
	    }
	}
    }
  return (class_);
}

/*
 * sm_get_statistics_force()
 *   return: class statistics
 *   classop(in):
 */
CLASS_STATS *
sm_get_statistics_force (MOP classop)
{
  SM_CLASS *class_;
  CLASS_STATS *stats = NULL;

  if (classop != NULL &&
      locator_is_class (classop, DB_FETCH_QUERY_READ) &&
      !OID_ISTEMP (WS_OID (classop)))
    {
      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT)
	  == NO_ERROR)
	{
	  if (class_->stats)
	    stats_free_statistics (class_->stats);
	  stats = class_->stats = stats_get_statistics (WS_OID (classop), 0);
	}
    }

  return stats;
}

/*
 * sm_update_statistics() - Update class stastics on the server for a
 *    particular class. When finished, fetch the new stastics and
 *    cache them with the class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 */

int
sm_update_statistics (MOP classop)
{
  int error = NO_ERROR;
  SM_CLASS *class_;

  /* only try to get statistics if we know the class has been flushed
     if it has a temporary oid, it isn't flushed and there are no statistics */

  if (classop != NULL && !OID_ISTEMP (WS_OID (classop)) &&
      locator_is_class (classop, DB_FETCH_QUERY_READ))
    {

      /* make sure the workspace is flushed before calculating stats */
      if (locator_flush_all_instances (classop, false) != NO_ERROR)
	return er_errid ();

      error = stats_update_class_statistics (WS_OID (classop));
      if (error == NO_ERROR)
	{
	  /* only recache if the class itself is cached */
	  if (classop->object != NULL)
	    {			/* check cache */

	      /* why are we checking authorization here ? */
	      if ((error =
		   au_fetch_class_force (classop, &class_,
					 AU_FETCH_READ)) == NO_ERROR)
		{
		  if (class_->stats != NULL)
		    stats_free_statistics (class_->stats);
		  class_->stats = NULL;

		  /* make sure the class is flushed before aquiring stats,
		     see comnents above in sm_get_class_with_statistics */
		  if (locator_flush_class (classop) != NO_ERROR)
		    return (er_errid ());

		  /* get the new ones, should do this at the same time as the
		     update operation to avoid two server calls */
		  class_->stats = stats_get_statistics (WS_OID (classop), 0);
		}
	    }
	}
    }
  return (error);
}

/*
 * sm_update_all_stastics() - Update the statistics for all classes
 * 			      in the database.
 *   return: NO_ERROR on success, non-zero for ERROR
 */

int
sm_update_all_statistics ()
{
  int error = NO_ERROR;
  DB_OBJLIST *cl;
  SM_CLASS *class_;

  /* make sure the workspace is flushed before calculating stats */
  if (locator_all_flush () != NO_ERROR)
    return er_errid ();

  if ((error = stats_update_statistics ()) == NO_ERROR)
    {
      /* Need to reset the statistics cache for all resident classes */
      for (cl = ws_Resident_classes; cl != NULL; cl = cl->next)
	{
	  if (!WS_ISMARK_DELETED (cl->op))
	    {
	      /* uncache statistics only if object is cached - MOP trickery */
	      if (cl->op->object != NULL)
		{
		  class_ = (SM_CLASS *) cl->op->object;
		  if (class_->stats != NULL)
		    {
		      stats_free_statistics (class_->stats);
		      class_->stats = NULL;
		    }
		  /* make sure the class is flushed but quit if an error happens */
		  if (locator_flush_class (cl->op) != NO_ERROR)
		    return (er_errid ());
		  class_->stats = stats_get_statistics (WS_OID (cl->op), 0);
		}
	    }
	}
    }
  return (error);
}

/*
 * sm_update_all_catalog_statistics()
 *   return: NO_ERROR on success, non-zero for ERROR
 */

int
sm_update_all_catalog_statistics (void)
{
  int error = NO_ERROR;
  int i;

  const char *classes[] = {
    CT_CLASS_NAME, CT_ATTRIBUTE_NAME, CT_DOMAIN_NAME,
    CT_METHOD_NAME, CT_METHSIG_NAME, CT_METHARG_NAME,
    CT_METHFILE_NAME, CT_QUERYSPEC_NAME, CT_INDEX_NAME,
    CT_INDEXKEY_NAME, CT_CLASSAUTH_NAME, CT_DATATYPE_NAME, NULL
  };

  for (i = 0; classes[i] != NULL && error == NO_ERROR; i++)
    {
      error = sm_update_catalog_statistics (classes[i]);
    }
  return (error);
}

/*
 * sm_update_catalog_statistics()
 *   return: NO_ERROR on success, non-zero for ERROR
 */

int
sm_update_catalog_statistics (const char *class_name)
{
  int error = NO_ERROR;
  DB_OBJECT *obj;

  obj = db_find_class (class_name);
  if (obj != NULL)
    error = sm_update_statistics (obj);
  else
    error = er_errid ();
  return (error);
}

/* TRIGGER FUNCTIONS */
/*
 * sm_get_trigger_cache() - used to access a trigger cache within a class object.
 *    It is called by the trigger manager and object manager.
 *    The "attribute" argument may be NULL in which case the class
 *    level trigger cache is returned.  If the "attribute" argument
 *    is set, an attribute level trigger cache is returned.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   attribute(in): attribute name
 *   class_attribute(in): flag indicating class attribute name
 *   cache(out): cache pointer (returned)
 */

int
sm_get_trigger_cache (DB_OBJECT * classop,
		      const char *attribute, int class_attribute,
		      void **cache)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  *cache = NULL;
  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error == NO_ERROR)
    {
      if (attribute == NULL)
	*cache = class_->triggers;
      else
	{
	  att = classobj_find_attribute (class_, attribute, class_attribute);
	  if (att != NULL)
	    *cache = att->triggers;
	}
    }
  return (error);
}

/*
 * sm_update_trigger_cache() - This adds or modifies the trigger cache pointer
 *    in the schema.  The class is also marked as dirty so
 *    the updated cache can be stored with the class definition.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   attribute(in): attribute name
 *   class_attribute(in): flag indicating class attribute name
 *   cache(in/out): cache to update
 */

int
sm_update_trigger_cache (DB_OBJECT * classop,
			 const char *attribute, int class_attribute,
			 void *cache)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  error = au_fetch_class (classop, &class_, AU_FETCH_UPDATE, AU_ALTER);

  if (error == NO_ERROR)
    {
      if (attribute == NULL)
	class_->triggers = cache;
      else
	{
	  att = classobj_find_attribute (class_, attribute, class_attribute);
	  if (att != NULL)
	    att->triggers = cache;
	}

      /* turn off the cache validation bits so we have to recalculate them
         next time */
      class_->triggers_validated = 0;
    }
  return (error);
}

/*
 * sm_active_triggers() - Quick check to see if the class has active triggers.
 *    Returns <0 if errors were encountered.
 *   return: non-zero if the class has active triggers
 *   class(in/out): class structure
 */

int
sm_active_triggers (SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;
  int status;

  /* If trigger firing has been disabled we do not want to search for
   * active triggers.
   */
  if (tr_get_execution_state () != true)
    return (0);

  if (!class_->triggers_validated)
    {
      class_->has_active_triggers = 0;

      status = tr_active_schema_cache ((TR_SCHEMA_CACHE *) class_->triggers);
      if (status < 0)
	return status;

      if (status > 0)
	class_->has_active_triggers = 1;
      else
	{
	  /* no class level triggers, look for attribute level triggers */
	  for (att = class_->ordered_attributes;
	       att != NULL && !class_->has_active_triggers;
	       att = att->order_link)
	    {

	      status =
		tr_active_schema_cache ((TR_SCHEMA_CACHE *) att->triggers);
	      if (status < 0)
		return status;
	      else if (status)
		class_->has_active_triggers = 1;
	    }
	  if (!class_->has_active_triggers)
	    {
	      FOR_ATTRIBUTES (class_->class_attributes, att)
	      {
		status =
		  tr_active_schema_cache ((TR_SCHEMA_CACHE *) att->triggers);
		if (status < 0)
		  return status;
		else if (status)
		  class_->has_active_triggers = 1;
	      }
	    }
	}

      /* don't repeat this process again */
      class_->triggers_validated = 1;
    }

  return (class_->has_active_triggers);
}

/*
 * sm_class_has_triggers() - This function can be used to determine if
 *    there are any triggers defined for a particular class.
 *    This could be used to optimize execution paths for the case where
 *    we know there will be no trigger processing.
 *    It is important that the trigger support not slow down operations
 *    on classes that do not have triggers.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   status_ptr(in): return status (non-zero if triggers for the class)
 */

int
sm_class_has_triggers (DB_OBJECT * classop, int *status_ptr)
{
  int error;
  SM_CLASS *class_;
  int status;

  if ((error =
       au_fetch_class (classop, &class_, AU_FETCH_READ,
		       AU_SELECT)) == NO_ERROR)
    {
      status = sm_active_triggers (class_);
      if (status < 0)
	error = er_errid ();
      else
	*status_ptr = status;
    }
  return (error);
}

/*
 * sm_invalidate_trigger_cache() - This is called by the trigger manager
 *    when a trigger associated with this class has undergone a status change.
 *    When this happens, we need to recalculate the state of the
 *    has_active_triggers flag that is cached in the class structure.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 */

int
sm_invalidate_trigger_cache (DB_OBJECT * classop)
{
  int error;
  SM_CLASS *class_;

  if (!(error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT)))
    class_->triggers_validated = 0;

  return (error);
}

/*
 * alter_trigger_cache() - This function encapsulates the mechanics of updating
 *    the trigger caches on a class.  It calls out to tr_ functions to perform
 *    the actual modification of the caches.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class(in/out): class structure
 *   attribute(in): attribute name
 *   class_attribute(in): non-zero if class attribute name
 *   trigger(in/out): trigger object to drop/add
 *   drop_it(in): non-zero if we're dropping the trigger object
 */

static int
alter_trigger_cache (SM_CLASS * class_,
		     const char *attribute, int class_attribute,
		     DB_OBJECT * trigger, int drop_it)
{
  int error = NO_ERROR;
  void **location;
  TR_CACHE_TYPE ctype;
  SM_ATTRIBUTE *att;

  /* find the slot containing the appropriate schema cache */
  location = NULL;
  if (attribute == NULL)
    location = &class_->triggers;
  else
    {
      att = classobj_find_attribute (class_, attribute, class_attribute);
      if (att != NULL)
	location = &att->triggers;
    }

  if (location != NULL)
    {
      if (drop_it)
	{
	  if (*location != NULL)
	    error =
	      tr_drop_cache_trigger ((TR_SCHEMA_CACHE *) * location, trigger);
	}
      else
	{
	  /* we're adding it, create a cache if one doesn't exist */
	  if (*location == NULL)
	    {
	      ctype =
		(attribute == NULL) ? TR_CACHE_CLASS : TR_CACHE_ATTRIBUTE;
	      *location = tr_make_schema_cache (ctype, NULL);
	    }
	  if (*location == NULL)
	    error = er_errid ();	/* couldn't allocate one */
	  else
	    error =
	      tr_add_cache_trigger ((TR_SCHEMA_CACHE *) * location, trigger);
	}
    }

  /* Turn off the cache validation bits so we have to recalculate them
     next time.  This is VERY important. */
  class_->triggers_validated = 0;

  return error;
}

/*
 * alter_trigger_hierarchy() - This function walks a subclass hierarchy
 *    performing an alteration to the trigger caches.
 *    This can be called in two ways.  If the trigger attribute is NULL,
 *    it will walk the hierarchy obtaining the appropriate write locks
 *    on the subclasses but will not make any changes.
 *    This is used to make sure that we can in fact lock all the affected
 *    subclasses before we try to perform the operation.
 *    When the trigger argument is non-NULL, we walk the hierarchy
 *    in the same way but this time we actually modify the trigger caches.
 *    The recursion stops when we encounter a class that has a local
 *    "shadow" attribute of the given name.  For class triggers,
 *    no shadowing is possible so we go all the way to the bottom.
 *    I'm not sure if this makes sense, we may need to go all the way
 *    for attribute triggers too.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class MOP
 *   attribute(in): target attribute (optional)
 *   class_attribute(in): non-zero if class attribute
 *   target_class(in):
 *   trigger(in/out): trigger object (NULL if just locking the hierarchy)
 *   drop_it(in): non-zero if we're going to drop the trigger
 */

static int
alter_trigger_hierarchy (DB_OBJECT * classop,
			 const char *attribute,
			 int class_attribute,
			 DB_OBJECT * target_class,
			 DB_OBJECT * trigger, int drop_it)
{
  int error = NO_ERROR;
  AU_FETCHMODE mode;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  DB_OBJLIST *u;
  int dive;

  /* fetch the class */
  mode = (trigger == NULL) ? AU_FETCH_WRITE : AU_FETCH_UPDATE;
  error = au_fetch_class_force (classop, &class_, mode);
  if (error != NO_ERROR)
    {
      if (WS_ISMARK_DELETED (classop))
	error = NO_ERROR;	/* in this case, just ignore the error */
    }
  else
    {
      dive = 1;
      if (attribute != NULL)
	{
	  /* dive only if we don't have a shadow of this attribute */
	  if (classop != target_class)
	    {
	      att =
		classobj_find_attribute (class_, attribute, class_attribute);
	      if (att == NULL || att->class_mop != target_class)
		dive = 0;
	    }
	}
      if (dive)
	{
	  /* dive to the bottom */
	  for (u = class_->users; u != NULL && !error; u = u->next)
	    error =
	      alter_trigger_hierarchy (u->op, attribute, class_attribute,
				       target_class, trigger, drop_it);
	}

      /* if everything went ok, alter the cache */
      if (!error && trigger != NULL)
	error =
	  alter_trigger_cache (class_, attribute, class_attribute, trigger,
			       drop_it);
    }
  return (error);
}

/*
 * sm_add_trigger() - This is called by the trigger manager to associate
 *    a trigger object with a class.
 *    The trigger is added to the trigger list for this class and all of
 *    its subclasses.
 *    ALTER authorization is required for the topmost class, the subclasses
 *    are fetched without authorization because the trigger must be added
 *    to them to maintain the "is-a" relationship.
 *    The class and the affected subclasses are marked dirty so the new
 *    trigger will be stored.
 *    This function must create a trigger cache and add the trigger
 *    object by calling back to the trigger manager functions
 *    tr_make_schema_cache, and tr_add_schema_cache at the appropriate times.
 *    This lets the schema manager perform the class hierarchy walk,
 *    while the trigger manager still has control over how the caches
 *    are created and updated.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class on which to add a trigger
 *   attribute(in): attribute name (optional)
 *   class_attribute(in): non-zero if its a class attribute name
 *   trigger (in/out): trigger object to add
 */

int
sm_add_trigger (DB_OBJECT * classop,
		const char *attribute, int class_attribute,
		DB_OBJECT * trigger)
{
  int error = NO_ERROR;
  SM_CLASS *class_;

  /* first fetch with authorization on the outer class */
  if (!(error = au_fetch_class (classop, &class_, AU_FETCH_UPDATE, AU_ALTER)))
    {

      /* Make sure all the affected subclasses are accessible. */
      if (!
	  (error =
	   alter_trigger_hierarchy (classop, attribute, class_attribute,
				    classop, NULL, 0)))
	{

	  error =
	    alter_trigger_hierarchy (classop, attribute, class_attribute,
				     classop, trigger, 0);
	}
    }
  return error;
}

/*
 * sm_drop_trigger() - called by the trigger manager when a trigger is dropped.
 *    It will walk the class hierarchy and remove the trigger from
 *    the caches of this class and any subclasses that inherit the trigger.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class objct
 *   attribute(in): attribute name
 *   class_attribute(in): non-zero if class attribute
 *   trigger(in/out): trigger object to drop
 */

int
sm_drop_trigger (DB_OBJECT * classop,
		 const char *attribute, int class_attribute,
		 DB_OBJECT * trigger)
{
  int error = NO_ERROR;
  SM_CLASS *class_;

  /* first fetch with authorization on the outer class */
  error = au_fetch_class (classop, &class_, AU_FETCH_UPDATE, AU_ALTER);

  /* if the error is "deleted object", just ignore the request since
     the trigger will be marked invalid and the class can't possibly be
     pointing to it */
  if (error == ER_HEAP_UNKNOWN_OBJECT)
    error = NO_ERROR;
  else if (!error)
    {
      /* Make sure all the affected subclasses are accessible. */
      if (!
	  (error =
	   alter_trigger_hierarchy (classop, attribute, class_attribute,
				    classop, NULL, 1)))
	{

	  error =
	    alter_trigger_hierarchy (classop, attribute, class_attribute,
				     classop, trigger, 1);
	}
    }
  return error;
}

/* MISC INFORMATION FUNCTIONS */
/*
 * sm_class_name() - Returns the name of a class associated with an object.
 *    If the object is a class, its own class name is returned.
 *    If the object is an instance, the name of the instance's class
 *    is returned.
 *    Authorization is ignored for this one case.
 *   return: class name
 *   op(in): class or instance object
 */

const char *
sm_class_name (MOP op)
{
  SM_CLASS *class_;
  const char *name = NULL;

  if (op != NULL)
    {
      if (au_fetch_class_force (op, &class_, AU_FETCH_READ) == NO_ERROR)
	name = class_->header.name;
    }

  return (name);
}

/*
 * sm_get_class_name_internal()
 * sm_get_class_name()
 * sm_get_class_name_not_null() - Returns the name of a class associated with
 *    an object. If the object is a class, its own class name is returned.
 *    If the object is an instance, the name of the instance's class
 *    is returned.
 *    Authorization is ignored for this one case.
 *    This function is lighter than sm_class_name(), and returns not null.
 *   return: class name
 *   op(in): class or instance object
 *   return_null(in):
 */

static const char *
sm_get_class_name_internal (MOP op, bool return_null)
{
  SM_CLASS *class_ = NULL;
  const char *name = NULL;
  int save;

  if (op != NULL)
    {
      AU_DISABLE (save);
      if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	if (class_)
	  name = class_->header.name;
      AU_ENABLE (save);
    }

  return (name ? name : (return_null ? NULL : ""));
}

const char *
sm_get_class_name (MOP op)
{
  return sm_get_class_name_internal (op, true);
}

const char *
sm_get_class_name_not_null (MOP op)
{
  return sm_get_class_name_internal (op, false);
}

/*
 * sm_is_subclass() - Checks to see if one class is a subclass of another.
 *   return: non-zero if classmop is subclass of supermop
 *   classmop(in): possible sub class
 *   supermop(in): possible super class
 */

int
sm_is_subclass (MOP classmop, MOP supermop)
{
  DB_OBJLIST *s;
  SM_CLASS *class_;
  int found;

  found = 0;
  if (au_fetch_class (classmop, &class_, AU_FETCH_READ, AU_SELECT) ==
      NO_ERROR)
    {
      for (s = class_->inheritance; !found && s != NULL; s = s->next)
	{
	  if (s->op == supermop)
	    found = 1;
	}
      if (!found)
	{
	  for (s = class_->inheritance; !found && s != NULL; s = s->next)
	    found = sm_is_subclass (s->op, supermop);
	}
    }
  return (found);
}

/*
 * sm_object_size() - Walk through the instance or class and tally up
 *    the number of bytes used for storing the various object components.
 *    Information function only.  Not guarenteed acurate but should
 *    always be maintained as close as possible.
 *   return: memory byte size of object
 *   op(in): class or instance object
 */

int
sm_object_size (MOP op)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  MOBJ obj;
  int size, pin;

  size = 0;
  if (locator_is_class (op, DB_FETCH_READ))
    {
      if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	size = classobj_class_size (class_);
    }
  else
    {
      if (au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  if (au_fetch_instance (op, &obj, AU_FETCH_READ, AU_SELECT) ==
	      NO_ERROR)
	    {
	      /* wouldn't have to pin here since we don't allocate storage
	         but can't hurt to be safe */
	      pin = ws_pin (op, 1);
	      size = class_->object_size;
	      FOR_ATTRIBUTES (class_->attributes, att)
	      {
		if (att->type->variable_p)
		  size += pr_total_mem_size (att->type, obj + att->offset);
	      }
	      (void) ws_pin (op, pin);
	    }
	}
    }
  return (size);
}

/*
 * sm_object_size_quick() - Calculate the memory size of an instance.
 *    Called only by the workspace statistics functions.
 *    Like sm_object_size but doesn't do any fetches.
 *   return: byte size of instance
 *   class(in): class structure
 *   obj(in): pointer to instance memory
 */

int
sm_object_size_quick (SM_CLASS * class_, MOBJ obj)
{
  SM_ATTRIBUTE *att;
  int size = 0;

  if (class_ != NULL && obj != NULL)
    {
      size = class_->object_size;
      FOR_ATTRIBUTES (class_->attributes, att) if (att->type->variable_p)
	size += pr_total_mem_size (att->type, obj + att->offset);
    }

  return (size);
}

/*
 * sm_object_disk_size() - Calculates the disk size of an object.
 *    General information function that should be pretty accurate but
 *    not guarenteed to be absolutely accurrate.
 *   return: byte size of disk representation of object
 *   op(in): class or instance object
 */

int
sm_object_disk_size (MOP op)
{
  SM_CLASS *class_;
  MOBJ obj;
  int size, pin;

  size = 0;
  if (au_fetch_class (op->class_mop, &class_, AU_FETCH_READ, AU_SELECT) ==
      NO_ERROR)
    {
      obj = NULL;
      if (locator_is_class (op, DB_FETCH_READ))
	{
	  au_fetch_class (op, (SM_CLASS **) & obj, AU_FETCH_READ, AU_SELECT);
	  if (obj != NULL)
	    size = tf_object_size ((MOBJ) class_, obj);
	}
      else
	{
	  au_fetch_instance (op, &obj, AU_FETCH_READ, AU_SELECT);
	  if (obj != NULL)
	    {
	      /* probably woudn't have to pin here since we don't allocate */
	      pin = ws_pin (op, 1);
	      size = tf_object_size ((MOBJ) class_, obj);
	      (void) ws_pin (op, pin);
	    }
	}
    }

  return (size);
}

/*
 * sm_dump() - Debug function to dump internal information about class objects.
 *   return: none
 *   classmop(in): class object
 */

void
sm_print (MOP classmop)
{
  SM_CLASS *class_;

  if (au_fetch_class (classmop, &class_, AU_FETCH_READ, AU_SELECT) ==
      NO_ERROR)
    classobj_print (class_);
}

/* LOCATOR SUPPORT FUNCTIONS */
/*
 * sm_classobj_name() - Given a pointer to a class object in memory,
 *    return the name. Used by the transaction locator.
 *   return: class name
 *   classobj(in): class structure
 */

const char *
sm_classobj_name (MOBJ classobj)
{
  SM_CLASS_HEADER *class_;
  const char *name = NULL;

  if (classobj != NULL)
    {
      class_ = (SM_CLASS_HEADER *) classobj;
      name = class_->name;
    }

  return (name);
}

/*
 * sm_heap() - Support function for the transaction locator.
 *    This returns a pointer to the heap file identifier in a class.
 *    This will work for either classes or the root class.
 *   return: HFID of class
 *   clobj(in): pointer to class structure in memory
 */

HFID *
sm_heap (MOBJ clobj)
{
  SM_CLASS_HEADER *header;
  HFID *heap;

  header = (SM_CLASS_HEADER *) clobj;

  heap = &header->heap;

  return (heap);
}

/*
 * sm_get_heap() - Return the HFID of a class given a MOP.
 *    Like sm_heap but takes a MOP.
 *   return: hfid of class
 *   classmop(in): class object
 */

HFID *
sm_get_heap (MOP classmop)
{
  SM_CLASS *class_ = NULL;
  HFID *heap;

  heap = NULL;
  if (locator_is_class (classmop, DB_FETCH_READ))
    {
      if (au_fetch_class (classmop, &class_, AU_FETCH_READ, AU_SELECT) ==
	  NO_ERROR)
	heap = &class_->header.heap;
    }
  return (heap);
}

/*
 * sm_has_indexes() - This is used to determine if there are any indexes
 *    associated with a particular class.
 *    Currently, this is used only by the locator so
 *    that when deleted instances are flushed, we can set the appropriate
 *    flags so that the indexes on the server will be updated.  For updated
 *    objects, the "has indexes" flag is returned by tf_mem_to_disk().
 *    Since we don't transform deleted objects however, we need a different
 *    mechanism for determining whether indexes exist.  Probably we should
 *    be using this function for all cases and remove the flag from the
 *    tf_ interface.
 *    This will return an error code if the class could not be fetched for
 *    some reason.  Authorization is NOT checked here.
 *    All of the cosntraint information is also contained on the class
 *    property list as well as the class constraint cache.  The class
 *    constraint cache is probably easier and faster to search than
 *    scanning over each attribute.  Something that we might want to change
 *    later.
 *   return: Non-zero if there are indexes defined
 *   classmop(in): class pointer
 */

int
sm_has_indexes (MOBJ classobj)
{
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *con;
  int has_indexes = 0;

  class_ = (SM_CLASS *) classobj;
  for (con = class_->constraints; con != NULL; con = con->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY (con->type))
	{
	  has_indexes = 1;
	  break;
	}
    }

  return has_indexes;
}

/*
 * sm_has_constraint() - This is used to determine if a constraint is
 *    associated with a particular class.
 *   return: Non-zero if there are constraints defined
 *   classobj(in): class pointer
 *   constraint(in): the constraint to look for
 */

int
sm_has_constraint (MOBJ classobj, SM_ATTRIBUTE_FLAG constraint)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int has_constraint = 0;

  class_ = (SM_CLASS *) classobj;
  FOR_ATTRIBUTES (class_->attributes, att)
  {
    if (att->flags & constraint)
      {
	has_constraint = 1;
	break;
      }
  }
  return has_constraint;
}

/*
 * sm_class_constraints() - Return a pointer to the class constraint cache.
 *    A NULL pointer is returned is an error occurs.
 *   return: class constraint
 *   classop(in): class pointer
 */

SM_CLASS_CONSTRAINT *
sm_class_constraints (MOP classop)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *constraints = NULL;

  error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
  if (error == NO_ERROR)
    constraints = class_->constraints;

  return constraints;
}

/* INTERPRETER SUPPORT FUNCTIONS */
/*
 * sm_find_class() - Given a class name, return the class object.
 *    All this really does is call locator_find_class but it makes sure the
 *    search is case insensitive.
 *    Since the eh_ module does not support case insensitive string
 *    comparisons, we have to make sure that class names are always
 *    converted to lower case before passing them through to the lc_ layer.
 *   return: class object
 *   name(in): class name
 */

MOP
sm_find_class (const char *name)
{
  char realname[SM_MAX_IDENTIFIER_LENGTH];

  sm_downcase_name (name, realname, SM_MAX_IDENTIFIER_LENGTH);

  return (locator_find_class (realname));
}

/*
 * find_attribute_op() - Given the MOP of an object and an attribute name,
 *    return a pointer to the class structure and a pointer to the
 *    attribute structure with the given name.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): class or instance MOP
 *   name(in): attribute name
 *   classp(out): return pointer to class
 *   attp(out): return pointer to attribute
 */

static int
find_attribute_op (MOP op, const char *name,
		   SM_CLASS ** classp, SM_ATTRIBUTE ** attp)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  if (!sm_check_name (name))
    error = er_errid ();
  else
    {
      if ((error =
	   au_fetch_class (op, &class_, AU_FETCH_READ,
			   AU_SELECT)) == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att == NULL)
	    ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, name);
	  else
	    {
	      *classp = class_;
	      *attp = att;
	    }
	}
    }
  return (error);
}

/*
 * sm_get_att_domain() - Get the domain descriptor for an attribute.
 *    This should be replaced with sm_get_att_info.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): class object
 *   name(in): attribute name
 *   domain(out): returned pointer to domain
 */

int
sm_get_att_domain (MOP op, const char *name, TP_DOMAIN ** domain)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SM_CLASS *class_;

  if ((error = find_attribute_op (op, name, &class_, &att)) == NO_ERROR)
    {
      sm_filter_domain (att->domain);
      *domain = att->domain;
    }

  return (error);
}

/*
 * sm_get_att_name() - Get the name of an attribute with its id.
 *   return: attribute name
 *   classop(in): class object
 *   id(in): attribute ID
 */

const char *
sm_get_att_name (MOP classop, int id)
{
  const char *name = NULL;
  int error;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT))
      == NO_ERROR)
    {
      if ((att = classobj_find_attribute_id (class_, id, 0)) != NULL)
	name = att->header.name;
    }

  return name;
}				/* sm_get_att_name() */

/*
 * sm_att_id() - Returns the internal id number assigned to the attribute.
 *   return: attribute id number
 *   classop(in): class object
 *   name(in): attribute
 */

int
sm_att_id (MOP classop, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int id;

  id = -1;
  if (find_attribute_op (classop, name, &class_, &att) == NO_ERROR)
    {
      id = att->id;
    }
  return (id);
}

/*
 * sm_att_type_id() - Return the type constant for the basic
 * 		      type of an attribute.
 *   return: type identifier
 *   classop(in): class object
 *   name(in): attribute name
 */

DB_TYPE
sm_att_type_id (MOP classop, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  DB_TYPE type;

  type = DB_TYPE_NULL;
  if (find_attribute_op (classop, name, &class_, &att) == NO_ERROR)
    {
      type = att->type->id;
    }
  return (type);
}

/*
 * sm_type_name() - Accesses the primitive type name for a type identifier.
 *    Used by the interpreter for error messages during semantic checking.
 *   return: internal primitive type name
 *   id(in): type identifier
 */

const char *
sm_type_name (DB_TYPE id)
{
  PR_TYPE *type;

  type = PR_TYPE_FROM_ID (id);
  if (type != NULL)
    return type->name;

  return NULL;
}

/*
 * sm_att_class() - Returns the domain class of an attribute if its basic type
 *    is DB_TYPE_OBJECT.
 *   return: domain class of attribute
 *   classop(in): class object
 *   name(in): attribute name
 */

MOP
sm_att_class (MOP classop, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  MOP attclass;

  attclass = NULL;
  if (find_attribute_op (classop, name, &class_, &att) == NO_ERROR)
    {
      sm_filter_domain (att->domain);
      if (att->domain != NULL && att->domain->type == tp_Type_object)
	attclass = att->domain->class_mop;
    }
  return (attclass);
}

/*
 * sm_att_info() - Used by the interpreter and query compiler to gather
 *    misc information about an attribute.  Don't set errors
 *    if the attribute was not found, the compiler may use this to
 *    probe classes for information and will handle errors on its own.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   name(in): attribute name
 *   idp(out): returned attribute identifier
 *   domainp(out): returned domain structure
 *   sharedp(out): returned flag set if shared attribute
 *   class_attr(in): flag to indicate if you want att info for class attributes
 */

int
sm_att_info (MOP classop, const char *name, int *idp,
	     TP_DOMAIN ** domainp, int *sharedp, int class_attr)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  att = NULL;
  *sharedp = 0;
  if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT))
      == NO_ERROR)
    {
      if ((att = classobj_find_attribute (class_, name, class_attr)) == NULL)
	{
	  /* return error but don't call er_set */
	  error = ER_SM_ATTRIBUTE_NOT_FOUND;
	}

      if (error == NO_ERROR)
	{
	  if (att->header.name_space == ID_SHARED_ATTRIBUTE)
	    *sharedp = 1;
	  sm_filter_domain (att->domain);
	  *idp = att->id;
	  *domainp = att->domain;
	}
    }
  return (error);
}

/*
 * sm_find_index()
 *   return: Pointer to B-tree ID variable.
 *   classop(in): class object
 *   att_names(in):
 *   num_atts(in):
 *   unique_index_only(in):
 *   btid(out):
 */

BTID *
sm_find_index (MOP classop, char **att_names, int num_atts,
	       bool unique_index_only, BTID * btid)
{
  int error = NO_ERROR;
  int i;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *con;
  SM_ATTRIBUTE *att1, *att2;
  BTID *index = NULL;

  index = NULL;
  if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT))
      == NO_ERROR)
    {

      /* never use an unique index upon a class hierarchy */
      if (unique_index_only && (class_->inheritance || class_->users))
	return NULL;

      for (con = class_->constraints; con != NULL; con = con->next)
	{
	  if (SM_IS_CONSTRAINT_INDEX_FAMILY (con->type))
	    {
	      if (unique_index_only
		  && !SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type))
		continue;

	      if (num_atts > 0)
		{
		  for (i = 0; i < num_atts; i++)
		    {
		      att1 = con->attributes[i];
		      if (att1 == NULL)
			break;
		      att2 =
			classobj_find_attribute (class_, att_names[i], 0);
		      if (att1->id != att2->id)
			break;
		    }
		  if ((i == num_atts) && con->attributes[i] == NULL)
		    /* found it */
		    break;
		}
	      else
		break;
	    }
	}
    }
  if (con)
    {
      BTID_COPY (btid, &con->index);
      index = btid;
    }

  return (index);
}

/*
 * sm_att_constrained() - Returns whether the attribute is constained.
 *   return: whether the attribute is constrained.
 *   classop(in): class object
 *   name(in): attribute
 *   cons(in): constraint
 */

int
sm_att_constrained (MOP classop, const char *name, SM_ATTRIBUTE_FLAG cons)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int rc;

  rc = 0;
  if (find_attribute_op (classop, name, &class_, &att) == NO_ERROR)
    {
      if (SM_IS_ATTFLAG_INDEX_FAMILY (cons))
	{
	  rc = classobj_get_cached_constraint (att->constraints,
					       SM_MAP_INDEX_ATTFLAG_TO_CONSTRAINT
					       (cons), NULL);
	}
      else
	{
	  rc = att->flags & cons;
	}
    }
  return (rc);
}

/*
 * sm_is_att_fk_cache()
 *   return:
 *   classop(in): class object
 *   name(in):
 */

int
sm_is_att_fk_cache (MOP classop, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  if (find_attribute_op (classop, name, &class_, &att) == NO_ERROR)
    {
      return att->is_fk_cache_attr;
    }

  return false;
}

/*
 * sm_att_unique_constrained() - Returns whether the attribute is UNIQUE constained.
 *   return: whether the attribute is UNIQUE constrained.
 *   classop(in): class object
 *   name(in): attribute
 */

int
sm_att_unique_constrained (MOP classop, const char *name)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  int rc;

  rc = 0;
  if (find_attribute_op (classop, name, &class_, &att) == NO_ERROR)
    {
      rc = ATT_IS_UNIQUE (att);
    }
  return (rc);
}

/*
 * sm_class_check_uniques() - Returns NO_ERROR if there are no unique constraints
 *    or if none of the unique constraints are violated.
 *    Used by the interpreter to check batched unique constraints.
 *   return: whether class failed unique constraint tests.
 *   classop(in): class object
 */

int
sm_class_check_uniques (MOP classop)
{
  SM_CLASS *class_;
  int error = NO_ERROR;
  OR_ALIGNED_BUF (200) a_buffer;	/* should handle most of the cases */
  char *buffer;
  int buf_size, buf_len = 0, buf_malloced = 0, uniques = 0, btid_size;
  char *bufp, *buf_start;
  SM_CLASS_CONSTRAINT *con;

  buffer = OR_ALIGNED_BUF_START (a_buffer);
  bufp = buffer;
  buf_start = buffer;
  buf_size = 200;		/* could use OR_ALIGNED_BUF_SIZE */

  btid_size = or_align_length_for_btree (OR_BTID_SIZE);

  if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT))
      == NO_ERROR)
    {
      for (con = class_->constraints; con != NULL; con = con->next)
	{
	  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type))
	    {
	      uniques = 1;

	      /* check if we have space for one more btid */
	      if (buf_len + btid_size > buf_size)
		{
		  buf_size = buf_size * 2;
		  if (buf_malloced)
		    {
		      if ((buf_start = (char *)
			   realloc (buf_start, buf_size)) == NULL)
			{
			  buf_malloced = 0;
			  goto error_class_check_uniques;
			}
		    }
		  else
		    {
		      if ((buf_start = malloc (buf_size)) == NULL)
			goto error_class_check_uniques;
		      memcpy (buf_start, buffer, buf_len);
		    }
		  buf_malloced = 1;
		  bufp = buf_start + buf_len;
		}

	      bufp = or_pack_btid (bufp, &(con->index));
	      bufp = PTR_ALIGN (bufp, OR_INT_SIZE);
	      buf_len += btid_size;
	    }
	}

      if (uniques)
	{
	  error = btree_class_test_unique (buf_start, buf_len);
	}
    }

  if (buf_malloced)
    free_and_init (buf_start);

  return error;

error_class_check_uniques:
  if (buf_malloced)
    free_and_init (buf_start);
  return er_errid ();

}

/* QUERY PROCESSOR SUPPORT FUNCTIONS */
/*
 * sm_get_class_repid() - Used by the query compiler to tag compiled
 *    queries/views with the representation ids of the involved classes.
 *    This allows it to check for class modifications at a later date and
 *    invalidate the query/view.
 *   return: current representation id if class. Returns -1 if an error ocurred
 *   classop(in): class object
 */

int
sm_get_class_repid (MOP classop)
{
  SM_CLASS *class_;
  int id = -1;

  if (classop != NULL && locator_is_class (classop, DB_FETCH_READ))
    {
      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) ==
	  NO_ERROR)
	id = class_->repid;
    }
  return (id);
}

/*
 * lock_query_subclasses()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   subclasses(in):
 *   op(in): root class of query
 *   exceptions(in):  list of exception classes
 *   update(in): set if classes are to be locked for update
 */

static int
lock_query_subclasses (DB_OBJLIST ** subclasses, MOP op,
		       DB_OBJLIST * exceptions, int update)
{
  int error = NO_ERROR;
  DB_OBJLIST *l, *found, *new_, *u;
  SM_CLASS *class_;

  if (!ml_find (exceptions, op))
    {
      /* must be more effecient here */
      if (update)
	error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_UPDATE);
      else
	error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
      if (error == NO_ERROR)
	{
	  /* upgrade the lock, MUST change this to be part of the au call */
	  if (update)
	    class_ =
	      (SM_CLASS *) locator_fetch_class (op, DB_FETCH_QUERY_WRITE);
	  else
	    class_ =
	      (SM_CLASS *) locator_fetch_class (op, DB_FETCH_QUERY_READ);
	  if (class_ == NULL)
	    error = er_errid ();
	  else
	    {
	      /* dive to the bottom */
	      for (u = class_->users; u != NULL && error == NO_ERROR;
		   u = u->next)
		error =
		  lock_query_subclasses (subclasses, u->op, exceptions,
					 update);

	      /* push the class on the list */
	      for (l = *subclasses, found = NULL;
		   l != NULL && found == NULL; l = l->next)
		{
		  if (l->op == op)
		    found = l;
		}
	      if (found == NULL)
		{
		  new_ = (DB_OBJLIST *) db_ws_alloc (sizeof (DB_OBJLIST));
		  if (new_ == NULL)
		    return er_errid ();
		  new_->op = op;
		  new_->next = *subclasses;
		  *subclasses = new_;
		}
	    }
	}
    }
  return (error);
}

/*
 * sm_query_lock() - Lock a class hierarchy in preparation for a query.
 *   return: object list
 *   classop(in): root class of query
 *   exceptions(in): list of exception classes
 *   only(in): set if only top level class is locked
 *   update(in): set if classes are to be locked for update
 */

DB_OBJLIST *
sm_query_lock (MOP classop, DB_OBJLIST * exceptions, int only, int update)
{
  int error;
  DB_OBJLIST *classes, *u;
  SM_CLASS *class_;

  classes = NULL;
  if (classop != NULL)
    {
      if (update)
	error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_UPDATE);
      else
	error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT);
      if (error == NO_ERROR)
	{
	  /* upgrade the lock, MUST change this to be part of the au call */
	  if (update)
	    class_ =
	      (SM_CLASS *) locator_fetch_class (classop,
						DB_FETCH_QUERY_WRITE);
	  else
	    class_ =
	      (SM_CLASS *) locator_fetch_class (classop, DB_FETCH_QUERY_READ);
	  if (class_ == NULL)
	    {
	      ml_free (classes);
	      return (NULL);
	    }
	  if (!ml_find (exceptions, classop))
	    {
	      if (ml_add (&classes, classop, NULL))
		{
		  ml_free (classes);
		  return NULL;
		}
	    }

	  if (!only)
	    {
	      for (u = class_->users; u != NULL && error == NO_ERROR;
		   u = u->next)
		error =
		  lock_query_subclasses (&classes, u->op, exceptions, update);
	    }
	}
    }
  else if (!only)
    {
      /* KLUDGE, if the classop is NULL, assume that the domain is "object" and that
         all classes are available - shouldn't have to do this !!! */
      classes = sm_get_all_classes (0);
    }

  return (classes);
}

/*
 * sm_flush_objects() - Flush all the instances of a particular class
 *    to the server. Used by the query processor to ensure that all
 *    dirty objects of a class are flushed before attempting to
 *    execute the query.
 *    It is important that the class be flushed as well.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   obj(in): class or instance
 */

int
sm_flush_objects (MOP obj)
{
  return sm_flush_and_decache_objects (obj, false);
}

/*
 * sm_flush_and_decache_objects() - Flush all the instances of a particular
 *    class to the server. Optionally decache the instances of the class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   obj(in): class or instance
 *   decache(in): whether to decache the instances of the class.
 */

int
sm_flush_and_decache_objects (MOP obj, int decache)
{
  int error = NO_ERROR;
  MOBJ mem;
  SM_CLASS *class_;

  if (obj != NULL)
    {
      if (locator_is_class (obj, DB_FETCH_READ))
	{
	  /* always make sure the class is flushed as well */
	  if (locator_flush_class (obj) != NO_ERROR)
	    return (er_errid ());

	  if ((class_ =
	       (SM_CLASS *) locator_fetch_class (obj, DB_FETCH_READ)) == NULL)
	    ERROR (error, ER_WS_NO_CLASS_FOR_INSTANCE);
	  else
	    {
	      switch (class_->class_type)
		{
		case SM_CLASS_CT:
		  if (class_->flags & SM_CLASSFLAG_SYSTEM)
		    {
		      /* if system class, flush all dirty class */
		      if (locator_flush_all_instances
			  (sm_Root_class_mop, DONT_DECACHE) != NO_ERROR)
			{
			  error = er_errid ();
			  break;
			}
		    }

		  if (locator_flush_all_instances (obj, decache) != NO_ERROR)
		    error = er_errid ();
		  break;

		case SM_VCLASS_CT:
		case SM_LDBVCLASS_CT:
		  if (vid_flush_all_instances (obj, decache) != NO_ERROR)
		    error = er_errid ();
		  break;

		case SM_ADT_CT:
		  /* what to do here?? */
		  break;
		}
	    }
	}
      else
	{
	  if (obj->class_mop != NULL)
	    {
	      if (locator_flush_class (obj->class_mop) != NO_ERROR)
		return (er_errid ());

	      if ((class_ =
		   (SM_CLASS *) locator_fetch_class (obj,
						     DB_FETCH_READ)) == NULL)
		ERROR (error, ER_WS_NO_CLASS_FOR_INSTANCE);
	      else
		{
		  switch (class_->class_type)
		    {
		    case SM_CLASS_CT:
		      if (locator_flush_all_instances (obj->class_mop,
						       decache) != NO_ERROR)
			error = er_errid ();
		      break;

		    case SM_VCLASS_CT:
		    case SM_LDBVCLASS_CT:
		      if (vid_flush_all_instances (obj, decache) != NO_ERROR)
			error = er_errid ();
		      break;

		    case SM_ADT_CT:
		      /* what to do here?? */
		      break;
		    }
		}
	    }
	  else
	    {
	      if ((error =
		   au_fetch_instance (obj, &mem, AU_FETCH_READ,
				      AU_SELECT)) == NO_ERROR)
		{
		  /* don't need to pin here, we only wanted to check authorization */
		  if (obj->class_mop != NULL)
		    {
		      if (locator_flush_class (obj->class_mop) != NO_ERROR)
			return (er_errid ());

		      if ((class_ =
			   (SM_CLASS *) locator_fetch_class (obj,
							     DB_FETCH_READ))
			  == NULL)
			ERROR (error, ER_WS_NO_CLASS_FOR_INSTANCE);
		      else
			{
			  switch (class_->class_type)
			    {
			    case SM_CLASS_CT:
			      if (locator_flush_all_instances (obj->class_mop,
							       decache) !=
				  NO_ERROR)
				error = er_errid ();
			      break;

			    case SM_VCLASS_CT:
			    case SM_LDBVCLASS_CT:
			      if (vid_flush_all_instances (obj, decache) !=
				  NO_ERROR)
				error = er_errid ();
			      break;

			    case SM_ADT_CT:
			      /* what to do here?? */
			      break;
			    }
			}
		    }
		  else
		    ERROR (error, ER_WS_NO_CLASS_FOR_INSTANCE);
		}
	    }
	}
    }
  return (error);
}

/*
 * sm_flush_for_multi_update() - Flush all the dirty instances of a particular
 *    class to the server.
 *    It is invoked only in case that client updates multiple instances.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class_mop (in): class MOP
 */

int
sm_flush_for_multi_update (MOP class_mop)
{
  int success = NO_ERROR;

  if (WS_ISVID (class_mop))
    {
      /* This case must not occur. */
      /* goto error; */

      /* The second argument, decache, is false. */
      if (vid_flush_all_instances (class_mop, false) != NO_ERROR)
	goto error;

      success = sm_class_check_uniques (class_mop);
      return success;
    }
  if (locator_flush_for_multi_update (class_mop) != NO_ERROR)
    goto error;

  return success;

error:
  return er_errid ();

}

/* WORKSPACE/GARBAGE COLLECTION SUPPORT FUNCTIONS */
/*
 * sm_issystem() - This is called by the workspace manager to see
 *    if a class is a system class.  This avoids having the ws files know about
 *    the class structure and flags.
 *   return: non-zero if class is system class
 */

int
sm_issystem (SM_CLASS * class_)
{
  return (class_->flags & SM_CLASSFLAG_SYSTEM);
}

/*
 * sm_gc_domain()
 * sm_gc_attribute()
 * sm_gc_method() - GC sweep functions for structures internal to the
 * 		    class structure.
 *   return: none
 *   domain, att, meth(in): structure to examine
 *   gcmarker(in): callback function to mark referenced objects
 */

static void
sm_gc_domain (TP_DOMAIN * domain, void (*gcmarker) (MOP))
{
  TP_DOMAIN *d;

  for (d = domain; d != NULL; d = d->next)
    {
      if (d->class_mop != NULL)
	(*gcmarker) (d->class_mop);
      else if (d->setdomain != NULL)
	sm_gc_domain (d->setdomain, gcmarker);
    }
}

static void
sm_gc_attribute (SM_ATTRIBUTE * att, void (*gcmarker) (MOP))
{
  if (att->class_mop != NULL)
    (*gcmarker) (att->class_mop);

  sm_gc_domain (att->domain, gcmarker);

  pr_gc_value (&att->value, gcmarker);
  pr_gc_value (&att->original_value, gcmarker);

  if (att->properties != NULL)
    pr_gc_setref (att->properties, gcmarker);

  if (att->triggers != NULL)
    tr_gc_schema_cache ((TR_SCHEMA_CACHE *) att->triggers, gcmarker);

}

static void
sm_gc_method (SM_METHOD * meth, void (*gcmarker) (MOP))
{
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  if (meth->class_mop != NULL)
    (*gcmarker) (meth->class_mop);

  for (sig = meth->signatures; sig != NULL; sig = sig->next)
    {
      for (arg = sig->value; arg != NULL; arg = arg->next)
	sm_gc_domain (arg->domain, gcmarker);
      for (arg = sig->args; arg != NULL; arg = arg->next)
	sm_gc_domain (arg->domain, gcmarker);
    }

  if (meth->properties != NULL)
    pr_gc_setref (meth->properties, gcmarker);
}

/*
 * sm_gc_class() - Called by the workspace manager to perform a
 *    GC sweep on a class object.
 *   return: none
 *   mop(in): class object
 *   gcmarker(in): callback function for marking referenced objects
 */

void
sm_gc_class (MOP mop, void (*gcmarker) (MOP))
{
  SM_CLASS *class_;
  DB_OBJLIST *ml;
  SM_ATTRIBUTE *att;
  SM_METHOD *meth;
  SM_RESOLUTION *res;

  /* this is what should happen, this could be very dangerous for GC !! ?? */
  /* class = (SM_CLASS *) locator_fetch_class(mop, DB_FETCH_READ); */

  class_ = (SM_CLASS *) mop->object;
  if (class_ != NULL)
    {

      /* mops in the subclass list */
      for (ml = class_->users; ml != NULL; ml = ml->next)
	(*gcmarker) (ml->op);

      /* super classes */
      for (ml = class_->inheritance; ml != NULL; ml = ml->next)
	(*gcmarker) (ml->op);

      /* attributes */
      FOR_ATTRIBUTES (class_->attributes, att) sm_gc_attribute (att,
								gcmarker);
      FOR_ATTRIBUTES (class_->shared, att) sm_gc_attribute (att, gcmarker);
      FOR_ATTRIBUTES (class_->class_attributes, att)
	sm_gc_attribute (att, gcmarker);

      /* methods */
      FOR_METHODS (class_->methods, meth) sm_gc_method (meth, gcmarker);
      FOR_METHODS (class_->class_methods, meth) sm_gc_method (meth, gcmarker);

      /* resolutions */
      for (res = class_->resolutions; res != NULL; res = res->next)
	{
	  if (res->class_mop != NULL)
	    (*gcmarker) (res->class_mop);
	}

      /* owner */
      if (class_->owner != NULL)
	(*gcmarker) (class_->owner);

      /* properties */
      if (class_->properties != NULL)
	pr_gc_setref (class_->properties, gcmarker);

      /* trigger cache */
      if (class_->triggers != NULL)
	tr_gc_schema_cache ((TR_SCHEMA_CACHE *) class_->triggers, gcmarker);
    }
}

/*
 * sm_gc_object() - Called by the workspace manager to do a GC sweep on
 *    an instance. Might want to have the pr_gc level functions here too.
 *   return: none
 *   mop(in): instance object
 *   gcmarker(in): callback function to mark referenced objects
 */

void
sm_gc_object (MOP mop, void (*gcmarker) (MOP))
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  char *mem;

  /* this has to be accurate at this point - think about this very
     carefully */
  if (mop->class_mop != NULL)
    {
      class_ = (SM_CLASS *) mop->class_mop->object;
      if (class_ != NULL)
	{
	  FOR_ATTRIBUTES (class_->attributes, att)
	  {
	    mem = ((char *) mop->object) + att->offset;
	    pr_gc_type (att->type, mem, gcmarker);
	  }
	}
    }
}

/*
 * sm_schema_version()
 *   return: unsigned long indicating any change in schema as none
 */

unsigned int
sm_schema_version ()
{
  return schema_version_number;
}

/*
 * sm_bump_schema_version()
 *   return: indicates global schema version has changed none
 */

void
sm_bump_schema_version ()
{
  schema_version_number++;
}

/*
 * sm_virtual_queries() - Frees a session for a class.
 *   return: SM_CLASS pointer, with valid virtual query cache a class db_object
 *   class_object(in):
 */

struct parser_context *
sm_virtual_queries (DB_OBJECT * class_object)
{
  SM_CLASS *cl;
  unsigned int current_schema_id;
  PARSER_CONTEXT *cache = NULL, *tmp = NULL;

  if (au_fetch_class_force (class_object, &cl, AU_FETCH_READ) == NO_ERROR)
    {

      (void) ws_pin (class_object, 1);

      current_schema_id = sm_schema_version ();

      if ((cl->virtual_cache_schema_id != current_schema_id) &&
	  (cl->virtual_query_cache != NULL))
	{
	  mq_free_virtual_query_cache (cl->virtual_query_cache);
	  cl->virtual_query_cache = NULL;
	}

      if (cl->class_type != SM_CLASS_CT && cl->virtual_query_cache == NULL)
	{
	  /* Okay, this is a bit of a kludge:  If there happens to be a
	   * cyclic view definition, then the virtual_query_cache will be
	   * allocated during the call to mq_virtual_queries. So, we'll
	   * assign it to a temp pointer and check it again.  We need to
	   * keep the old one and free the new one because the parser
	   * assigned originally contains the error message.
	   */
	  tmp = mq_virtual_queries (class_object);
	  if (cl->virtual_query_cache)
	    mq_free_virtual_query_cache (tmp);
	  else
	    cl->virtual_query_cache = tmp;
	  cl->virtual_cache_schema_id = current_schema_id;
	}

      cache = cl->virtual_query_cache;
    }

  return cache;
}

/* DESCRIPTORS */
/*
 * This provides a mechanism for locating attributes & methods
 * based on a "descriptor" rather that through a name.
 *
 * The descriptors are maintained on a global list so that they
 * can be marked when important events happen like schema changes
 * or transaction boundaries.
 *
 * NOTE: Descriptors will try to avoid repeated lock & authorization
 * checks.  This means that once a descriptor has been established
 * and populated, changing the current user will not result
 * in flushing the descriptor cache.  This behavior is intended
 * primarily for the parser since we don't want to lose everything
 * just because we called a method that needed to set the authorization
 * context.  This may be a potential hole of the descriptors are
 * passed in to something that normally wouldn't have access to
 * the class.  Its not that severe since someone must have
 * had access to the descriptor in order to create it in the
 * first place.  Think about this, we may be able to make this
 * a more formal way to "pass in" authorization on a controlled
 * basis to methods.
 */

/*
 * sm_get_attribute_descriptor() - Find the named attribute structure
 *    in the class and return it. Lock the class with the appropriate intent.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): class or instance
 *   name(in): attribute name
 *   class_attribute(in): non-zero for locating class attributes
 *   for_update(in): non-zero if we're intending to update the attribute
 *   desc_ptr(out): returned attribute descriptor
 */

int
sm_get_attribute_descriptor (DB_OBJECT * op, const char *name,
			     int class_attribute,
			     int for_update, SM_DESCRIPTOR ** desc_ptr)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  SM_DESCRIPTOR *desc;
  MOP classmop;
  DB_FETCH_MODE class_purpose;

  att = NULL;

  if (class_attribute)
    {
      /* looking for class attribute */
      if (for_update)
	error = au_fetch_class (op, &class_, AU_FETCH_UPDATE, AU_ALTER);
      else
	error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_SELECT);
      if (error == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 1);
	  if (att == NULL)
	    ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
	}
    }
  else
    {
      /* looking for an instance attribute */
      if ((error = au_fetch_class (op, &class_, AU_FETCH_READ,
				   AU_SELECT)) == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att == NULL)
	    ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);

	  else if (att->header.name_space == ID_SHARED_ATTRIBUTE)
	    {
	      /* sigh, we didn't know that this was going to be a shared attribute
	         when we checked class authorization above, we must now upgrade
	         the lock and check for alter access.

	         Since this is logically in the name_space of the instance,
	         should we use simple AU_UPDATE authorization rather than AU_ALTER
	         even though we're technically modifying the class ?
	       */
	      if (for_update)
		error =
		  au_fetch_class (op, &class_, AU_FETCH_UPDATE, AU_ALTER);
	    }
	}
    }

  if (!error && att != NULL)
    {
      /* class must have been fetched at this point */
      class_purpose = ((for_update)
		       ? DB_FETCH_CLREAD_INSTWRITE
		       : DB_FETCH_CLREAD_INSTREAD);

      classmop = (locator_is_class (op, class_purpose)) ? op : op->class_mop;

      desc = classobj_make_descriptor (classmop, class_, (SM_COMPONENT *) att,
				       for_update);
      if (desc == NULL)
	error = er_errid ();
      else
	{
	  desc->next = sm_Descriptors;
	  sm_Descriptors = desc;
	  *desc_ptr = desc;
	}
    }
  return error;
}

/*
 * sm_get_method_desc() - This returns a method descriptor for the named method.
 *    The descriptor can then be used for faster access the method
 *    avoiding the name search.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op (in): class or instance
 *   name(in): method name
 *   class_method(in): class method name
 *   desc_ptr(out): returned method descirptor
 */

int
sm_get_method_descriptor (DB_OBJECT * op, const char *name,
			  int class_method, SM_DESCRIPTOR ** desc_ptr)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_METHOD *method = NULL;
  SM_DESCRIPTOR *desc;
  MOP classmop;

  if (!(error = au_fetch_class (op, &class_, AU_FETCH_READ, AU_EXECUTE)))
    {

      method = classobj_find_method (class_, name, class_method);
      if (method == NULL)
	ERROR1 (error, ER_OBJ_INVALID_METHOD, name);

      /* could do the link here too ? */
    }

  if (!error && method != NULL)
    {
      /* class must have been fetched at this point */
      classmop = (locator_is_class (op, DB_FETCH_READ)) ? op : op->class_mop;

      desc =
	classobj_make_descriptor (classmop, class_, (SM_COMPONENT *) method,
				  0);
      if (desc == NULL)
	error = er_errid ();
      else
	{
	  desc->next = sm_Descriptors;
	  sm_Descriptors = desc;
	  *desc_ptr = desc;
	}
    }
  return error;
}

/*
 * sm_free_descriptor() - Free an attribute or method descriptor.
 *    Remember to remove it from the global descriptor list.
 *   return: none
 *   desc(in): descriptor to free
 */

void
sm_free_descriptor (SM_DESCRIPTOR * desc)
{
  SM_DESCRIPTOR *d, *prev;

  for (d = sm_Descriptors, prev = NULL; d != desc; d = d->next)
    prev = d;

  /* if d == NULL, the descriptor wasn't on the global list and
     is probably a suspect pointer, ignore it */
  if (d != NULL)
    {
      if (prev == NULL)
	sm_Descriptors = d->next;
      else
	prev->next = d->next;

      classobj_free_descriptor (d);
    }
}

/*
 * sm_invalidate_descriptors() - This is called whenever a class is edited.
 *    Or when a transaction commits.
 *    We need to mark any descriptors that reference this class as
 *    being invalid since the attribute/method structure pointers contained
 *    in the descriptor are no longer valid.
 *   return: none
 *   class(in): class being modified
 */

void
sm_reset_descriptors (MOP class_)
{
  SM_DESCRIPTOR *d;
  SM_DESCRIPTOR_LIST *dl;

  if (class_ == NULL)
    {
      /* transaction boundary, unconditionally clear all outstanding
         descriptors */
      for (d = sm_Descriptors; d != NULL; d = d->next)
	{
	  classobj_free_desclist (d->map);
	  d->map = NULL;
	}
    }
  else
    {
      /* Schema change, clear any descriptors that reference the class.
         Note, the schema manager will call this for EVERY class in the
         hierarcy.
       */
      for (d = sm_Descriptors; d != NULL; d = d->next)
	{
	  for (dl = d->map; dl != NULL && dl->classobj != class_;
	       dl = dl->next);
	  if (dl != NULL)
	    {
	      /* found one, free the whole list */
	      classobj_free_desclist (d->map);
	      d->map = NULL;
	    }
	}
    }
}

/*
 * fetch_descriptor_class() - Work function for sm_get_descriptor_component.
 *    If the descriptor has been cleared or if we need to fetch the
 *    class and check authorization for some reason, this function obtains
 *    the appropriate locks and checks the necessary authorization.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): object
 *   desc(in): descriptor
 *   for_update(in): non-zero if we're intending to update the attribute
 *   class(out): returned class pointer
 */

static int
fetch_descriptor_class (MOP op, SM_DESCRIPTOR * desc, int for_update,
			SM_CLASS ** class_)
{
  int error = NO_ERROR;

  if (for_update)
    {
      if (desc->name_space == ID_CLASS_ATTRIBUTE
	  || desc->name_space == ID_SHARED_ATTRIBUTE)
	error = au_fetch_class (op, class_, AU_FETCH_UPDATE, AU_ALTER);
      else
	error = au_fetch_class (op, class_, AU_FETCH_READ, AU_UPDATE);
    }
  else
    {
      if (desc->name_space == ID_METHOD
	  || desc->name_space == ID_CLASS_METHOD)
	error = au_fetch_class (op, class_, AU_FETCH_READ, AU_EXECUTE);
      else
	error = au_fetch_class (op, class_, AU_FETCH_READ, AU_SELECT);
    }
  return error;
}

/*
 * sm_get_descriptor_component() - This locates an attribute structure
 *    associated with the class of the supplied object and identified
 *    by the descriptor.
 *    If the attribute has already been cached in the descriptor it is
 *    returned, otherwise, we search the class for the matching component
 *    and add it to the descriptor cache.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): object
 *   desc(in): descriptor
 *   for_update(in): non-zero if we're intending to update the attribute
 *   class_ptr(out):
 *   comp_ptr(out):
 */

int
sm_get_descriptor_component (MOP op, SM_DESCRIPTOR * desc,
			     int for_update,
			     SM_CLASS ** class_ptr, SM_COMPONENT ** comp_ptr)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_COMPONENT *comp;
  SM_DESCRIPTOR_LIST *d, *prev, *new_;
  MOP classmop;
  int class_component;

  /* handle common case quickly, allow either an instance MOP or
     class MOP to be used here */
  if (desc->map != NULL
      && (desc->map->classobj == op || desc->map->classobj == op->class_mop)
      && (!for_update || desc->map->write_access))
    {
      *comp_ptr = desc->map->comp;
      *class_ptr = desc->map->class_;
    }
  else
    {
      /* this is set when a fetch is performed, try to avoid if possible */
      class_ = NULL;

      /* get the class MOP for this thing, avoid fetching if possible */
      if (op->class_mop == NULL)
	{
	  if (fetch_descriptor_class (op, desc, for_update, &class_))
	    return er_errid ();
	}
      classmop = (IS_CLASS_MOP (op)) ? op : op->class_mop;

      /* search the descriptor map for this class */
      for (d = desc->map, prev = NULL; d != NULL && d->classobj != classmop;
	   d = d->next)
	prev = d;

      if (d != NULL)
	{
	  /* found an existing one, move it to the head of the list */
	  if (prev != NULL)
	    {
	      prev->next = d->next;
	      d->next = desc->map;
	      desc->map = d;
	    }
	  /* check update authorization if we haven't done it yet */
	  if (for_update && !d->write_access)
	    {
	      if (class_ == NULL)
		{
		  if (fetch_descriptor_class (op, desc, for_update, &class_))
		    return er_errid ();
		}
	      d->write_access = 1;
	    }
	  *comp_ptr = d->comp;
	  *class_ptr = d->class_;
	}
      else
	{
	  /* not on the list, fetch it if we haven't already done so */
	  if (class_ == NULL)
	    {
	      if (fetch_descriptor_class (op, desc, for_update, &class_))
		return er_errid ();
	    }

	  class_component = (desc->name_space == ID_CLASS_ATTRIBUTE
			     || desc->name_space == ID_CLASS_METHOD);
	  comp =
	    classobj_find_component (class_, desc->name, class_component);
	  if (comp == NULL)
	    {
	      if (desc->name_space == ID_METHOD
		  || desc->name_space == ID_CLASS_METHOD)
		ERROR1 (error, ER_OBJ_INVALID_METHOD, desc->name);
	      else
		ERROR1 (error, ER_OBJ_INVALID_ATTRIBUTE, desc->name);
	    }
	  else
	    {
	      /* make a new descriptor and add it to the head of the list */
	      new_ =
		classobj_make_desclist (classmop, class_, comp, for_update);
	      if (new_ == NULL)
		error = er_errid ();
	      else
		{
		  new_->next = desc->map;
		  desc->map = new_;
		  *comp_ptr = comp;
		  *class_ptr = class_;
		}
	    }
	}
    }

  return error;
}

/*
 * sm_has_text_domain() - Check if it is a TEXT typed attribute
 *   return: 1 if it has TEXT or 0
 *   attribute(in): attributes to check a domain
 *   check_all(in): scope to check a domain, 1 if all check, or 0
 */
int
sm_has_text_domain (DB_ATTRIBUTE * attributes, int check_all)
{
#if 0				/* to disable TEXT */
  DB_ATTRIBUTE *attr;
  DB_OBJLIST *supers;
  DB_OBJECT *domain;

  attr = attributes;
  while (attr)
    {
      if (db_attribute_type (attr) == DB_TYPE_OBJECT)
	{
	  domain = db_domain_class (db_attribute_domain (attr));
	  if (domain)
	    {
	      supers = db_get_superclasses (domain);
	      if (supers && supers->op &&
		  (intl_mbs_casecmp
		   (db_get_class_name (supers->op), "db_text") == 0))
		return true;
	    }
	}
      if (!check_all)
	break;
      attr = db_attribute_next (attr);
    }
#endif /* 0 */
  return false;
}
