/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * smu.c - Schema manager update operations
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "error_manager.h"
#include "db.h"
#include "work_space.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "object_accessor.h"
#include "class_object.h"
#include "schema_manager_3.h"
#include "locator_cl.h"
#include "authenticate.h"
#include "boot_cl.h"
#include "network_interface_sky.h"
#include "virtual_object_1.h"
#include "set_object_1.h"
#include "execute_schema_8.h"
#include "schema_manager_3.h"
#include "release_string.h"

/* Shorthand error macros */
#define WARN(error, code) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); } while (0)

#define ERROR(error, code) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); } while (0)

#define ERROR1(error, code, arg1) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 1, arg1); } while (0)

#define ERROR2(error, code, arg1, arg2) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 2, arg1, arg2); } \
       while (0)

#define ERROR3(error, code, arg1, arg2, arg3) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 3, arg1, arg2, arg3); \
       } while (0)

#define ERROR4(error, code, arg1, arg2, arg3, arg4) \
  do { error = code; \
       er_set(ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 4, \
	   arg1, arg2, arg3, arg4); } while (0)

#define UNIQUE_SAVEPOINT_NAME "aDDuNIQUEcONSTRAINT"
#define UNIQUE_SAVEPOINT_NAME2 "dELETEcLASSmOP"

/* various states of a domain comparison. */
typedef enum
{

  DC_INCOMPATIBLE,
  DC_EQUAL,
  DC_LESS_SPECIFIC,
  DC_MORE_SPECIFIC
} DOMAIN_COMP;

/*
 *    Structure used internally during the flattening of a class
 *    hierarchy.  This information could be folded in whith the
 *    class and method structure definitions but its only used once
 *    and makes it less confusing for the flattener.
 *    For each attribute and method in a class hierarchy, a candidate
 *    structure will be built during flattening.
 */
typedef struct sm_candidate SM_CANDIDATE;

struct sm_candidate
{
  struct sm_candidate *next;

  const char *name;
  const char *alias;
  SM_NAME_SPACE name_space;
  MOP origin;
  MOP source;
  int order;

  unsigned int is_alias:1;	/* expanded alias candidates */

  unsigned int is_requested:1;	/* requrested in a resolution specifier */

  SM_COMPONENT *obj;		/* actual component structure */
};

static int check_resolution_target (SM_TEMPLATE * template_,
				    SM_RESOLUTION * res, int *valid_ptr);
static const char *template_classname (SM_TEMPLATE * template_);
static const char *candidate_source_name (SM_TEMPLATE * template_,
					  SM_CANDIDATE * candidate);
static int find_superclass (DB_OBJECT * classop, SM_TEMPLATE * temp,
			    DB_OBJECT * super);
static DOMAIN_COMP compare_domains (TP_DOMAIN * d1, TP_DOMAIN * d2);
static SM_METHOD_ARGUMENT *find_argument (SM_METHOD_SIGNATURE * sig,
					  int argnum);
static DOMAIN_COMP compare_argument_domains (SM_METHOD * m1, SM_METHOD * m2);
static DOMAIN_COMP compare_component_domains (SM_COMPONENT * c1,
					      SM_COMPONENT * c2);
static SM_CANDIDATE *make_candidate_from_component (SM_COMPONENT * comp,
						    MOP source);
static void free_candidates (SM_CANDIDATE * candidates);
static SM_CANDIDATE *prune_candidate (SM_CANDIDATE ** clist_pointer);
static void add_candidate (SM_CANDIDATE ** candlist, SM_COMPONENT * comp,
			   int order, MOP source,
			   SM_RESOLUTION * resolutions);
static SM_COMPONENT *make_component_from_candidate (MOP classop,
						    SM_CANDIDATE * cand);
static SM_CANDIDATE *get_candidates (SM_TEMPLATE * def, SM_TEMPLATE * flat,
				     SM_NAME_SPACE name_space);
static int check_attribute_method_overlap (SM_TEMPLATE * template_,
					   SM_CANDIDATE * candidates);
static int check_alias_conflict (SM_TEMPLATE * template_,
				 SM_CANDIDATE * candidates);
static int check_alias_domains (SM_TEMPLATE * template_,
				SM_CANDIDATE * candidates,
				SM_CANDIDATE ** most_specific);
static void auto_resolve_conflict (SM_CANDIDATE * candidate,
				   SM_RESOLUTION ** resolutions,
				   SM_NAME_SPACE resspace);
static int resolve_candidates (SM_TEMPLATE * template_,
			       SM_CANDIDATE * candidates,
			       int auto_resolve,
			       SM_CANDIDATE ** winner_return);
static void insert_attribute (SM_ATTRIBUTE ** attlist, SM_ATTRIBUTE * att);
static void insert_method (SM_METHOD ** methlist, SM_METHOD * method);
static int flatten_components (SM_TEMPLATE * def, SM_TEMPLATE * flat,
			       SM_NAME_SPACE name_space, int auto_res);
static int flatten_method_files (SM_TEMPLATE * def, SM_TEMPLATE * flat);
static int flatten_query_spec_lists (SM_TEMPLATE * def, SM_TEMPLATE * flat);
static void filter_component_resolutions (SM_TEMPLATE * template_,
					  const char *name,
					  SM_NAME_SPACE resspace);
static void remove_shadowed_resolutions (SM_TEMPLATE * original,
					 SM_TEMPLATE * flat);
static void filter_reslist (SM_RESOLUTION ** reslist, MOP deleted_class);
static int check_invalid_resolutions (SM_TEMPLATE * template_,
				      SM_RESOLUTION ** resolutions,
				      SM_RESOLUTION * original_list);
static int filter_resolutions (SM_TEMPLATE * def, SM_TEMPLATE * flat,
			       MOP deleted_class);
static SM_ATTRIBUTE *find_matching_att (SM_ATTRIBUTE * list,
					SM_ATTRIBUTE * att, int idmatch);
static void retain_former_ids (SM_TEMPLATE * flat);
static int flatten_trigger_cache (SM_TEMPLATE * def, SM_TEMPLATE * flat);
static int flatten_properties (SM_TEMPLATE * def, SM_TEMPLATE * flat);
static int flatten_template (SM_TEMPLATE * def, MOP deleted_class,
			     SM_TEMPLATE ** flatp, int auto_res);
static void assign_attribute_id (SM_CLASS * class_, SM_ATTRIBUTE * att,
				 int class_attribute);
static void assign_method_id (SM_CLASS * class_, SM_METHOD * method,
			      bool class_method);
static SM_ATTRIBUTE *order_atts_by_alignment (SM_ATTRIBUTE * atts);
static int build_storage_order (SM_CLASS * class_, SM_TEMPLATE * flat);
static void fixup_component_classes (MOP classop, SM_TEMPLATE * flat);
static void fixup_self_domain (TP_DOMAIN * domain, MOP self);
static void fixup_method_self_domains (SM_METHOD * meth, MOP self);
static void fixup_attribute_self_domain (SM_ATTRIBUTE * att, MOP self);
static void fixup_self_reference_domains (MOP classop, SM_TEMPLATE * flat);
static TP_DOMAIN *construct_index_key_domain (int n_atts,
					      SM_ATTRIBUTE ** atts,
					      const int *asc_desc);
static int collect_hier_class_info (MOP classop, DB_OBJLIST * subclasses,
				    const char *constraint_name,
				    int reverse,
				    int *n_classes,
				    int n_attrs, OID * oids,
				    int *attr_ids, HFID * hfids);
static int allocate_index (MOP classop, SM_CLASS * class_,
			   DB_OBJLIST * subclasses,
			   SM_ATTRIBUTE ** attrs, const int *asc_desc,
			   int unique, int reverse,
			   const char *constraint_name, BTID * index,
			   OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
			   int cache_attr_id, const char *fk_name);
static int deallocate_index (SM_CLASS_CONSTRAINT * cons, BTID * index);
static int rem_class_from_index (OID * oid, BTID * index, HFID * heap);
static int build_fk_obj_cache (MOP classop, SM_CLASS * class_,
			       SM_ATTRIBUTE ** key_attrs,
			       const int *asc_desc, OID * pk_cls_oid,
			       BTID * pk_btid, int cache_attr_id,
			       char *fk_name);
static int update_foreign_key_ref (MOP ref_clsop,
				   SM_FOREIGN_KEY_INFO * fk_info);
static int allocate_unique_constraint (MOP classop, SM_CLASS * class_,
				       SM_CLASS_CONSTRAINT * con,
				       DB_OBJLIST * subclasses);
static int allocate_foreign_key (MOP classop, SM_CLASS * class_,
				 SM_CLASS_CONSTRAINT * con,
				 bool * recache_cls_cons);
static int allocate_disk_structure_helper (MOP classop, SM_CLASS * class_,
					   SM_CLASS_CONSTRAINT * con,
					   DB_OBJLIST * subclasses,
					   bool * recache_cls_cons);
static int allocate_disk_structures (MOP classop, SM_CLASS * class_,
				     DB_OBJLIST * subclasses);
static int drop_foreign_key_ref (MOP classop,
				 SM_CLASS_CONSTRAINT * flat_cons,
				 SM_CLASS_CONSTRAINT * cons);
static int transfer_disk_structures (MOP classop, SM_CLASS * class_,
				     SM_TEMPLATE * flat);
static void save_previous_value (SM_ATTRIBUTE * old, SM_ATTRIBUTE * new_);
static void check_inherited_attributes (MOP classmop, SM_CLASS * class_,
					SM_TEMPLATE * flat);
static void invalidate_unused_triggers (MOP class_mop, SM_CLASS * class_,
					SM_TEMPLATE * flat);
static int install_new_representation (MOP classop, SM_CLASS * class_,
				       SM_TEMPLATE * flat);
static int lock_supers (SM_TEMPLATE * def, DB_OBJLIST * current,
			DB_OBJLIST ** oldlist, DB_OBJLIST ** newlist);
static int update_supers (MOP classop, DB_OBJLIST * oldsupers,
			  DB_OBJLIST * newsupers);
static int lock_supers_drop (DB_OBJLIST * supers);
static int update_supers_drop (MOP classop, DB_OBJLIST * supers);
static int lock_subclasses_internal (SM_TEMPLATE * def, MOP op,
				     DB_OBJLIST * newsupers,
				     DB_OBJLIST ** newsubs);
static int lock_subclasses (SM_TEMPLATE * def, DB_OBJLIST * newsupers,
			    DB_OBJLIST * cursubs, DB_OBJLIST ** newsubs);
static int check_catalog_space (MOP classmop, SM_CLASS * class_);
static int flatten_subclasses (DB_OBJLIST * subclasses, MOP deleted_class);
static void abort_subclasses (DB_OBJLIST * subclasses);
static int update_subclasses (DB_OBJLIST * subclasses);
static int verify_object_id (SM_TEMPLATE * template_);
static int lockhint_subclasses (SM_TEMPLATE * temp, SM_CLASS * class_);
static int update_class (SM_TEMPLATE * template_, MOP * classmop,
			 int auto_res, int verify_oid);
static void remove_class_triggers (MOP classop, SM_CLASS * class_);
static int sm_exist_index (MOP classop, const char *idxname);
static char *sm_default_constraint_name (const char *class_name,
					 DB_CONSTRAINT_TYPE type,
					 const char **att_names,
					 const int *asc_desc);

/* NAME SEARCHERS */
/*
 * template_classname() - Shorthand function for calls to er_set.
 *    Get the class name for the class associated with a template.
 *   return: class name
 *   template(in): schema template
 */

static const char *
template_classname (SM_TEMPLATE * template_)
{
  const char *name;

  name = template_->name;
  if (name == NULL && template_->op != NULL)
    name = sm_class_name (template_->op);

  return (name);
}

/*
 * candidate_source_name() - Shorthand function to determine the class name
 *    that is the source of the given candidate.
 *   return: class name
 *   template(in): template for class being edited
 *   candidate(in): candidate of interest
 */

static const char *
candidate_source_name (SM_TEMPLATE * template_, SM_CANDIDATE * candidate)
{
  const char *name = NULL;

  if (candidate->source != NULL)
    name = sm_class_name (candidate->source);
  else
    {
      if (template_->name != NULL)
	name = template_->name;
      else if (template_->op != NULL)
	name = sm_class_name (template_->op);
    }
  return (name);
}

/* DOMAIN COMPARISON */

/*
 * find_superclass() - searches up the class hierarchy looking for a superclass.
 *    If a superclass has a template pending, we use the superclass list
 *    of the template rather than the real superclass list so that we can
 *    recognize domain compatibility in a schema operation that has not
 *    yet been fully applied.
 *   return: non-zero if the superclass was found
 *   classop(in): class object (if passed, temp should be NULL)
 *   temp(in): template (if passed, classop should be NULL)
 *   super(in): super class we're looking for
 */

static int
find_superclass (DB_OBJECT * classop, SM_TEMPLATE * temp, DB_OBJECT * super)
{
  DB_OBJLIST *super_list, *el;
  SM_CLASS *class_;
  int status = 0;

  super_list = NULL;

  if (classop != NULL)
    {
      /* fetch the class and check for a pending template */
      if (au_fetch_class_force (classop, &class_, AU_FETCH_READ) != NO_ERROR)
	return 0;
      if (class_->new_ != NULL)
	/* its got a template, use the pending inheritance list */
	super_list = class_->new_->inheritance;
      else
	/* no template, use the real inheritance list */
	super_list = class_->inheritance;
    }
  else if (temp != NULL)
    /* use the inheritance list of the supplied template */
    super_list = temp->inheritance;

  /* search immediate superclasses first */
  for (el = super_list; el != NULL && !status; el = el->next)
    {
      if (el->op == super)
	status = 1;
    }
  if (!status)
    {
      /* Look all the way up the hierarchy, could be doing this in the
         previous loop but lets try to make the detection of immediate
         superclasses fast as it is likely to be the most common.
         Recurse so we recognize pending templates on the way up.
       */
      for (el = super_list; el != NULL && !status; el = el->next)
	status = find_superclass (el->op, NULL, super);
    }

  return status;
}

/*
 * compare_domains() - Compare two domains and calculate the appropriate
 *    comparison code.
 *    The result indicates the state of d1 relative to d2, that is, if the
 *    result is DC_MORE_SPECIFIC it indicates that d1 is more specific
 *    than d2.
 *    If a domain comes in whose type is tp_Type_null, the domain is actually
 *    a tp_Type_object domain for a class that has not yet been created.
 *    In this case, the "class" field of the TP_DOMAIN structure will point
 *    to the template for the new class.
 *   return: domain comparison code
 *   d1(in): domain structure
 *   d2(in): domain structure
 */

static DOMAIN_COMP
compare_domains (TP_DOMAIN * d1, TP_DOMAIN * d2)
{
  DOMAIN_COMP status = DC_INCOMPATIBLE;

  if (d1->type == tp_Type_null || d2->type == tp_Type_null)
    {
      /* domain comparison involving classes that haven't been created yet */
      if (d1->type == d2->type)
	{
	  if (d1->class_mop == d2->class_mop)
	    {
	      status = DC_EQUAL;
	    }
	  /* else, you can't create two different classes in the same template
	     so this can never happen */
	}
      else if (d1->type == tp_Type_null)
	{
	  if (d2->type != tp_Type_object)
	    {
	      status = DC_INCOMPATIBLE;
	    }
	  else if (d2->class_mop == NULL)
	    {
	      status = DC_MORE_SPECIFIC;
	    }
	  else
	    {
	      /* If d2->class is accessible by scanning upwards from
	         the inheritance list of the template, then d1 is in the
	         process of becoming a subtype of d2 and is therefore more
	         specific.
	       */
	      if (find_superclass (NULL, (SM_TEMPLATE *) (d1->class_mop),
				   d2->class_mop))
		{
		  status = DC_MORE_SPECIFIC;
		}
	    }
	}
      else
	{
	  /* same as previous clause except the polarity is reversed */
	  if (d1->type != tp_Type_object)
	    {
	      status = DC_INCOMPATIBLE;
	    }
	  else if (d1->class_mop == NULL)
	    {
	      status = DC_LESS_SPECIFIC;
	    }
	  else
	    {
	      if (find_superclass (NULL, (SM_TEMPLATE *) (d2->class_mop),
				   d1->class_mop))
		status = DC_LESS_SPECIFIC;
	    }
	}
    }
  else if (d1->type == d2->type)
    {
      if (d1->type == tp_Type_object)
	{
	  if (d1->class_mop == d2->class_mop)
	    status = DC_EQUAL;
	  else if (d1->class_mop == NULL)
	    status = DC_LESS_SPECIFIC;
	  else if (d2->class_mop == NULL)
	    status = DC_MORE_SPECIFIC;
	  else if (find_superclass (d1->class_mop, NULL, d2->class_mop))
	    status = DC_MORE_SPECIFIC;
	  else if (find_superclass (d2->class_mop, NULL, d1->class_mop))
	    status = DC_LESS_SPECIFIC;
	  else
	    status = DC_INCOMPATIBLE;
	}
      else if (pr_is_set_type (d1->type->id))
	{
	  /* set element domains must be compatible */
	  status = DC_EQUAL;
	}
      else
	{
	  status = DC_EQUAL;
	}
    }

  return (status);
}

/*
 * find_argument() - Helper function for compare_argument_domains.
 *    Locate an argument by number.  These need to be stored in
 *    an array for easier lookup.
 *   return: argument structure
 *   sig(in): method signature
 *   argnum(in): argument index
 */

static SM_METHOD_ARGUMENT *
find_argument (SM_METHOD_SIGNATURE * sig, int argnum)
{
  SM_METHOD_ARGUMENT *arg;

  for (arg = sig->args; arg != NULL && arg->index != argnum; arg = arg->next);
  return arg;
}

/*
 * compare_argument_domains() - This compares the argument lists of two methods
 *    to see if they are compatible.
 *    Currently this is defined so that the arguments must match
 *    exactly.  Eventually this could support the notion of "contravariance"
 *    in the signature.
 *   return: domain comparison code
 *   m1(in): method 1
 *   m2(in): method 2
 */

static DOMAIN_COMP
compare_argument_domains (SM_METHOD * m1, SM_METHOD * m2)
{
  DOMAIN_COMP status, arg_status;
  SM_METHOD_SIGNATURE *sig1, *sig2;
  SM_METHOD_ARGUMENT *arg1, *arg2;
  int i;

  status = DC_EQUAL;

  sig1 = m1->signatures;
  sig2 = m2->signatures;

  /* If both signatures are NULL, assume its ok, this is largely
     for backward compatibility.   */
  if (sig1 == NULL || sig2 == NULL)
    {
      if (sig1 != sig2)
	status = DC_INCOMPATIBLE;
    }
  else if (sig1->num_args == sig2->num_args)
    {
      /* Since the arguments aren't set stored in an array, lookup
         is harder than it should be.  Recall that arg indexes start with 1 */
      for (i = 1; i <= sig1->num_args && status == DC_EQUAL; i++)
	{
	  arg1 = find_argument (sig1, i);
	  arg2 = find_argument (sig2, i);

	  /* if either arg is missing, could assume its a "void" and allow it */
	  if (arg1 == NULL || arg2 == NULL)
	    {
	      if (arg1 != arg2)
		status = DC_INCOMPATIBLE;
	    }
	  else
	    {
	      arg_status = compare_domains (arg1->domain, arg2->domain);
	      if (arg_status != DC_EQUAL)
		status = DC_INCOMPATIBLE;
	    }
	}
    }
  return status;
}

/*
 * compare_component_domains() - Compare the domains of two components and
 *    return an appropriate comparison code.
 *    The result of this function indicates the state of c1 relative to
 *    c2, that is, if the result is ER_DOMAIN_LESS_SPECIFIC it means that
 *    c1 is less specific than c2.
 *   return: domain comparison code
 *   c1(in): component
 *   c2(in): component
 */

static DOMAIN_COMP
compare_component_domains (SM_COMPONENT * c1, SM_COMPONENT * c2)
{
  DOMAIN_COMP arg_status, status = DC_INCOMPATIBLE;
  SM_ATTRIBUTE *a1, *a2;
  SM_METHOD *m1, *m2;
  TP_DOMAIN *d1, *d2;

  if (c1->name_space == ID_METHOD || c1->name_space == ID_CLASS_METHOD)
    {
      if (c2->name_space == c1->name_space)
	{
	  /* compare return argument domains, should do full argument
	     signatures as well !
	     be careful here because methods don't always have domains specified */
	  m1 = (SM_METHOD *) c1;
	  m2 = (SM_METHOD *) c2;
	  d1 = NULL;
	  d2 = NULL;
	  if (m1->signatures != NULL && m1->signatures->value != NULL)
	    d1 = m1->signatures->value->domain;
	  if (m2->signatures != NULL && m2->signatures->value != NULL)
	    d2 = m2->signatures->value->domain;

	  if (d1 != NULL && d2 != NULL)
	    status = compare_domains (d1, d2);
	  else if (d1 == NULL && d2 == NULL)
	    /* neither specified, assume the same */
	    status = DC_EQUAL;
	  else
	    /* for now, if either method has no domain, assume its ok.  this
	       happens a lot with the multimedia classes and will happen when
	       using db_add_method before the argument domains are fully specified */
	    status = DC_EQUAL;

	  if (status != DC_INCOMPATIBLE)
	    {
	      arg_status = compare_argument_domains (m1, m2);
	      if (arg_status != DC_EQUAL)
		status = DC_INCOMPATIBLE;
	    }
	}
    }
  else
    {
      /* allow combination of instance/shared but not instance/class */
      if (c1->name_space == c2->name_space ||
	  (c1->name_space != ID_CLASS_ATTRIBUTE
	   && c2->name_space != ID_CLASS_ATTRIBUTE))
	{
	  /* regular, shared, or class attribute, these must have domains */
	  a1 = (SM_ATTRIBUTE *) c1;
	  a2 = (SM_ATTRIBUTE *) c2;
	  status = compare_domains (a1->domain, a2->domain);
	}
    }

  return (status);
}

/* CANDIDATE STRUCTURE MAINTENANCE */

/*
 * make_candidate_from_component() - Construct a candidate structure from
 * 				     a class component.
 *   return: candidate structure
 *   comp(in): component (attribute or method)
 *   source(in): MOP of source class (immediate super class)
 */

static SM_CANDIDATE *
make_candidate_from_component (SM_COMPONENT * comp, MOP source)
{
  SM_CANDIDATE *candidate;

  candidate = (SM_CANDIDATE *) db_ws_alloc (sizeof (SM_CANDIDATE));
  if (candidate != NULL)
    {
      candidate->next = NULL;
      candidate->name = comp->name;
      candidate->alias = NULL;
      candidate->name_space = comp->name_space;
      candidate->source = source;
      candidate->obj = comp;
      candidate->is_alias = 0;
      candidate->is_requested = 0;
      candidate->order = 0;

      if (comp->name_space == ID_METHOD
	  || comp->name_space == ID_CLASS_METHOD)
	{
	  candidate->origin = ((SM_METHOD *) comp)->class_mop;
	}
      else
	{
	  candidate->origin = ((SM_ATTRIBUTE *) comp)->class_mop;
	}
    }

  return (candidate);
}

/*
 * free_candidates() - Free a list of candidiates structures
 * 		       when done with schema flattening.
 *   return: none
 *   candidates(in): candidates list
 */

static void
free_candidates (SM_CANDIDATE * candidates)
{
  SM_CANDIDATE *c, *next;

  for (c = candidates, next = NULL; c != NULL; c = next)
    {
      next = c->next;
      db_ws_free (c);
    }
}

/*
 * prune_candidate() - This will remove the first candidiate in the list AND
 *    all other candidates in the list that have the same name.  The list of
 *    candidates with the same name as the first candidate as returned.
 *    The source list is destructively modified to remove the pruned
 *    candidates.
 *   return: pruned candidates
 *   clist_pointer (in): source candidates list
 */

static SM_CANDIDATE *
prune_candidate (SM_CANDIDATE ** clist_pointer)
{
  SM_CANDIDATE *candidates, *head;

  candidates = NULL;
  head = *clist_pointer;
  if (head != NULL)
    candidates =
      (SM_CANDIDATE *) nlist_filter ((DB_NAMELIST **) clist_pointer,
				     head->name,
				     (NLSEARCHER) SM_COMPARE_NAMES);

  return (candidates);
}

/*
 * add_candidate() - This adds a candidate structure for the component to
 *    the candidates list.
 *    If the component has an alias resolution in the resolution list,
 *    the candidate is marked as being aliased and an additional candidate
 *    is added to the list with the alias name.
 *   return: none
 *   candlist(in/out): pointer to candidate list head
 *   comp(in): component to build a candidate for
 *   order(in): the definition order of this candidate
 *   source(in): the source class of the candidate
 *   resolutions(in): resolution list in effect
 */

static void
add_candidate (SM_CANDIDATE ** candlist, SM_COMPONENT * comp, int order,
	       MOP source, SM_RESOLUTION * resolutions)
{
  SM_CANDIDATE *new_;
  SM_RESOLUTION *res;

  new_ = make_candidate_from_component (comp, source);
  new_->order = order;
  new_->next = *candlist;
  *candlist = new_;

  /* check the resolution list to see if there are any aliases for this
     component */
  res = classobj_find_resolution (resolutions, source, comp->name, ID_NULL);
  if (res != NULL)
    {
      if (res->alias == NULL)
	/* mark the component as being specifically requested */
	new_->is_requested = 1;
      else
	{
	  /* mark the candidiate as having an alias */
	  new_->alias = res->alias;
	  /* make an entry in the candidates list for the alias */
	  new_ = make_candidate_from_component (comp, source);
	  new_->name = res->alias;
	  new_->is_alias = 1;
	  new_->order = order;
	  new_->next = *candlist;
	  *candlist = new_;
	}
    }
}

/*
 * make_component_from_candidate() - Called after candidate flattening
 *    to construct an actual class component for a flattened candidate.
 *   return: class component
 *   classop(in): class being defined
 *   cand(in): candidate structure
 */

static SM_COMPONENT *
make_component_from_candidate (MOP classop, SM_CANDIDATE * cand)
{
  SM_COMPONENT *new_;
  SM_ATTRIBUTE *att = NULL;
  SM_METHOD *method = NULL;
  SM_NAME_SPACE space;

  new_ = NULL;

  space = cand->obj->name_space;
  if (space == ID_METHOD || space == ID_CLASS_METHOD)
    {
      method = classobj_copy_method ((SM_METHOD *) cand->obj, NULL);
      if (method == NULL)
	return (NULL);
      new_ = (SM_COMPONENT *) method;
      method->order = cand->order;

      /* if this is an inherited component, clear out certain fields that
         don't get inherited automatically */
      if (cand->source != NULL && cand->source != classop)
	method->id = -1;
    }
  else
    {
      att = classobj_copy_attribute ((SM_ATTRIBUTE *) cand->obj, NULL);
      if (att == NULL)
	return (NULL);
      new_ = (SM_COMPONENT *) att;
      att->order = cand->order;

      /* !! ALWAYS CLEAR THIS, ITS A RUN TIME ONLY FLAG AND CAN'T
         MAKE IT TO DISK */
      att->flags &= ~SM_ATTFLAG_NEW;

      /* if this is an inherited component, clear out certain fields that
         don't get inherited automatically.
         We now allow the UNIQUE constraint to be inherited but not INDEX */

      if (cand->source != NULL && cand->source != classop)
	{
	  att->id = -1;		/* must reassign this */
	}
    }

  /* if this is an alias candidate, change the name */
  if (cand->is_alias)
    {
      ws_free_string (new_->name);
      new_->name = ws_copy_string (cand->name);
      if (new_->name == NULL)
	{
	  if (method)
	    classobj_free_method (method);
	  if (att)
	    classobj_free_attribute (att);
	  new_ = NULL;
	}
    }

  return (new_);
}

/* CANDIDATE GATHERING */
/*
 * get_candidates() - This builds a candidates list for either the instance
 *    or class name_space.  The candidates list is the raw flattened list of all
 *    the attribute and method definitions in the name_space.
 *    Each candidate is tagged with an order counter so that the definition
 *    order can be preserved in the resulting class.  Although attributes
 *    and methods are included on the same candidates list, they are ordered
 *    seperately.
 *   return: candidates list
 *   def(in): original template
 *   flag(in): flattened template (in progress)
 *   namespace(in): ID_CLASS or ID_INSTANCE
 */

static SM_CANDIDATE *
get_candidates (SM_TEMPLATE * def, SM_TEMPLATE * flat,
		SM_NAME_SPACE name_space)
{
  SM_COMPONENT *complist, *comp;
  SM_RESOLUTION *reslist;
  SM_ATTRIBUTE *att;
  SM_CANDIDATE *candlist;
  DB_OBJLIST *super;
  SM_CLASS *sclass;
  int att_order, meth_order;

  candlist = NULL;
  /* get appropriate resolution list from the flattened template */
  if (name_space == ID_CLASS)
    reslist = flat->class_resolutions;
  else
    reslist = flat->resolutions;

  /* initialize the component order counters */
  att_order = 0;
  meth_order = 0;

  /* go left to right through the supers adding the components in order */
  for (super = def->inheritance; super != NULL; super = super->next)
    {
      if (au_fetch_class_force (super->op, &sclass, AU_FETCH_READ) ==
	  NO_ERROR)
	{

	  if (name_space == ID_CLASS)
	    {
	      /* add the class attributes */
	      complist = (SM_COMPONENT *) ((sclass->new_ == NULL) ?
					   sclass->class_attributes :
					   sclass->new_->class_attributes);
	      for (comp = complist; comp != NULL;
		   comp = comp->next, att_order++)
		add_candidate (&candlist, comp, att_order, super->op,
			       reslist);

	      /* add the class methods */
	      complist = (SM_COMPONENT *) ((sclass->new_ == NULL) ?
					   sclass->class_methods :
					   sclass->new_->class_methods);
	      for (comp = complist; comp != NULL;
		   comp = comp->next, meth_order++)
		add_candidate (&candlist, comp, meth_order, super->op,
			       reslist);
	    }
	  else
	    {
	      /* add the instance and shared attributes, the template is ordered */
	      if (sclass->new_ != NULL)
		{
		  for (att = sclass->new_->attributes; att != NULL;
		       att = (SM_ATTRIBUTE *) att->header.next, att_order++)
		    add_candidate (&candlist, (SM_COMPONENT *) att, att_order,
				   super->op, reslist);
		}
	      else
		{
		  /* get these from the ordered list ! */
		  for (att = sclass->ordered_attributes; att != NULL;
		       att = att->order_link, att_order++)
		    add_candidate (&candlist, (SM_COMPONENT *) att, att_order,
				   super->op, reslist);
		}
	      /* add the instance methods */
	      complist = (SM_COMPONENT *) ((sclass->new_ == NULL) ?
					   sclass->methods :
					   sclass->new_->methods);
	      for (comp = complist; comp != NULL;
		   comp = comp->next, meth_order++)
		add_candidate (&candlist, comp, meth_order, super->op,
			       reslist);
	    }
	}
    }

  /* get local definition component list */
  if (name_space == ID_CLASS)
    {
      /* add local class attributes */
      complist = (SM_COMPONENT *) def->class_attributes;
      for (comp = complist; comp != NULL; comp = comp->next, att_order++)
	add_candidate (&candlist, comp, att_order, def->op, NULL);

      /* add local class methods */
      complist = (SM_COMPONENT *) def->class_methods;
      for (comp = complist; comp != NULL; comp = comp->next, meth_order++)
	add_candidate (&candlist, comp, meth_order, def->op, NULL);
    }
  else
    {
      /* add local attibutes */
      complist = (SM_COMPONENT *) def->attributes;
      for (comp = complist; comp != NULL; comp = comp->next, att_order++)
	add_candidate (&candlist, comp, att_order, def->op, NULL);

      /* add local methods */
      complist = (SM_COMPONENT *) def->methods;
      for (comp = complist; comp != NULL; comp = comp->next, meth_order++)
	add_candidate (&candlist, comp, meth_order, def->op, NULL);
    }

  return (candlist);
}

/*
 * CANDIDATE LIST RULES
 * These functions map over a pruned candidates list checking for various
 * rules of inheritance.  Some rules are checked in the more complex
 * function resolve_candidates(), so that better error messages can
 * be produced.
 */

/*
 * check_attribute_method_overlap() - This checks the candidates in the list
 *    to see if there are any attributes and methods with the same name.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): template of class being edited (used for class name only)
 *   candidates(in): candidate list
 */

static int
check_attribute_method_overlap (SM_TEMPLATE * template_,
				SM_CANDIDATE * candidates)
{
  int error = NO_ERROR;
  SM_CANDIDATE *att_cand, *meth_cand, *c;

  att_cand = meth_cand = NULL;

  for (c = candidates; c != NULL && error == NO_ERROR; c = c->next)
    {
      if (c->name_space == ID_METHOD || c->name_space == ID_CLASS_METHOD)
	{
	  meth_cand = c;
	  if (att_cand != NULL)
	    {
	      ERROR3 (error, ER_SM_INCOMPATIBLE_COMPONENTS, c->name,
		      candidate_source_name (template_, att_cand),
		      candidate_source_name (template_, c));
	    }
	}
      else
	{
	  att_cand = c;
	  if (meth_cand != NULL)
	    {
	      ERROR3 (error, ER_SM_INCOMPATIBLE_COMPONENTS, c->name,
		      candidate_source_name (template_, c),
		      candidate_source_name (template_, meth_cand));
	    }
	}
    }
  return (error);
}

/*
 * check_alias_conflict() - This checks for candidates that were produced
 *    by aliasing an inherited component. If an alias is defined, there can be
 *    only one component with that name.  Two inherited components cannot
 *    have the same alias and an alias cannot conflict with a "real"
 *    component.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class template (used for name only)
 *   candidates(in): candidates list
 */

static int
check_alias_conflict (SM_TEMPLATE * template_, SM_CANDIDATE * candidates)
{
  int error = NO_ERROR;
  SM_CANDIDATE *c, *normal, *alias;

  normal = alias = NULL;

  for (c = candidates; c != NULL && error == NO_ERROR; c = c->next)
    {
      /* ignore candidates that have been aliased to something else */
      if (c->alias == NULL)
	{
	  /* is this candidate using an alias name ? */
	  if (c->is_alias == 0)
	    /* could be smarter and recognize most specific domains
	       and shadowing in case we get one of the error conditions below */
	    normal = c;
	  else
	    {
	      if (alias != NULL)
		{
		  /* Alias name `%1s' is used more than once. */
		  ERROR1 (error, ER_SM_MULTIPLE_ALIAS, alias->name);
		}
	      else
		alias = c;
	    }
	}
    }

  if (error == NO_ERROR && alias != NULL && normal != NULL)
    {

      if (normal->source == NULL || normal->source == template_->op)
	{
	  /* Can't use "alias" as an alias for inherited component "name", there
	     is already a locally defined component with that name */
	  ERROR2 (error, ER_SM_ALIAS_COMPONENT_EXISTS,
		  alias->name, alias->obj->name);
	}
      else
	{
	  /* Can't use `%1$s' as an alias for `%2$s' of `%3$s'.
	     A component with that name is already inherited from `%4s'.
	   */
	  ERROR4 (error, ER_SM_ALIAS_COMPONENT_INHERITED,
		  alias->name, alias->obj->name,
		  candidate_source_name (template_, alias),
		  candidate_source_name (template_, normal));
	}
    }
  return (error);
}

/*
 * check_alias_domains() - This checks the domains of all candidates in the
 *    list that have been given aliases.
 *    Candidates with aliases will be ignored during resolution.
 *    The rule is however that if a candidate is aliased,
 *    there must be an appropriate substitute candidate from another class.
 *    This function first checks to make sure that all of the aliased
 *    candidates have compatible domains.
 *    While this check is being done, the alias with the most specific domain
 *    is found.  The domain of the alias substitute must be at least
 *    as specific as the domains of all the candidates that were aliased.
 *    This last test is performed at the end of resolve_candidates().
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class template (used for name only)
 *   candidates(in): candidates list
 *   most_specific(out): return pointer to most specific aliased candidate
 */

static int
check_alias_domains (SM_TEMPLATE * template_,
		     SM_CANDIDATE * candidates, SM_CANDIDATE ** most_specific)
{
  int error = NO_ERROR;
  SM_CANDIDATE *c, *most;
  DOMAIN_COMP dstate;

  most = NULL;
  for (c = candidates; c != NULL && error == NO_ERROR; c = c->next)
    {
      if (c->alias != NULL)
	{			/* only look at candidates that are aliased */
	  if (most == NULL)
	    most = c;
	  else if (c->origin != most->origin)
	    {
	      dstate = compare_component_domains (c->obj, most->obj);
	      switch (dstate)
		{
		case DC_INCOMPATIBLE:
		  ERROR4 (error, ER_SM_INCOMPATIBLE_DOMAINS, c->name,
			  candidate_source_name (template_, most),
			  candidate_source_name (template_, c),
			  template_classname (template_));
		  break;

		case DC_MORE_SPECIFIC:
		  most = c;
		  break;

		case DC_EQUAL:
		case DC_LESS_SPECIFIC:
		  /* ignore it */
		  break;
		}
	    }
	}
    }
  *most_specific = most;
  return (error);
}

/* CANDIDATE RESOLUTION */
/*
 * auto_resolve_conflict() - Add (or modify an existing) resolution for
 *    the candidate to a resolution list.
 *   return: none
 *   candiate(in): candidate needing resolution
 *   resolutions(in/out): pointer to resolution list
 *   resspace(in): resolution space (class or instance)
 */

static void
auto_resolve_conflict (SM_CANDIDATE * candidate, SM_RESOLUTION ** resolutions,
		       SM_NAME_SPACE resspace)
{
  SM_RESOLUTION *res, *found;

  found = NULL;
  for (res = *resolutions; res != NULL && found == NULL; res = res->next)
    {
      if ((res->name_space == resspace) &&
	  (SM_COMPARE_NAMES (res->name, candidate->name) == 0))
	{
	  if (res->alias == NULL)
	    found = res;
	}
    }
  if (found != NULL)
    {
      /* adjust the existing resolution to point at the new class */
      found->class_mop = candidate->source;
    }
  else
    {
      /* generate a new resolution */
      res =
	classobj_make_resolution (candidate->source, candidate->name, NULL,
				  resspace);
      if (res)
	res->next = *resolutions;
      *resolutions = res;
    }
}

/*
 * resolve_candidates() - This is the main function for checking component
 *    combination rules. Given a list of candidates, all of the rules for
 *    compatibility are checked and a winner is determined if there is
 *    more than one possible candidate.
 *    If any of the rules fail, an error code is returned.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   candidates(in): candidates list
 *   auto_resolve(in): non-zero to enable auto resolution of conflicts
 *   winner_return(out): returned pointer to winning candidates
 */

static int
resolve_candidates (SM_TEMPLATE * template_,
		    SM_CANDIDATE * candidates,
		    int auto_resolve, SM_CANDIDATE ** winner_return)
{
  int error = NO_ERROR;
  SM_CANDIDATE *winner, *c, *requested, *conflict, *local, *alias;
  SM_NAME_SPACE resspace;
  DOMAIN_COMP dstate;

  winner = NULL;
  alias = NULL;
  requested = NULL;
  local = NULL;
  conflict = NULL;

  /* first check some simple rules */
  if ((error = check_attribute_method_overlap (template_, candidates)))
    return (error);

  if ((error = check_alias_conflict (template_, candidates)))
    return (error);

  if ((error = check_alias_domains (template_, candidates, &alias)))
    return (error);

  /* look for a local & requested component */
  for (c = candidates; c != NULL; c = c->next)
    {
      if (c->source == NULL || c->source == template_->op)
	/* if local is not NULL here, its technically an error */
	local = c;
      if (c->is_requested)
	/* if local is not NULL here, its technically an error */
	requested = c;
    }

  /* establish an initial winner if possible */
  if (local == NULL)
    winner = requested;
  else
    {
      winner = local;
      /* this means that we found a resolution for an inherited attribute
         but we also have a local definition, in this case the resolution
         has no effect and is deleted */
      /* remove_invalid_resolution(template, requested); */
      requested = NULL;
    }

  /* loop through the non-aliases candidates looking for a winner
     should detect aliases that are ignored because of a local definition
     and remove them from the resolution list ! - try to handle
     this during template building
   */

  for (c = candidates; c != NULL && error == NO_ERROR; c = c->next)
    {
      if (c->alias == NULL)
	{
	  if (winner == NULL)
	    winner = c;
	  else if (c != winner && c->origin != winner->origin)
	    {
	      dstate = compare_component_domains (c->obj, winner->obj);
	      switch (dstate)
		{
		case DC_INCOMPATIBLE:
		  if (local == NULL)
		    /* incompatibility between two inherited things */
		    ERROR4 (error, ER_SM_INCOMPATIBLE_DOMAINS, winner->name,
			    candidate_source_name (template_, winner),
			    candidate_source_name (template_, c),
			    template_classname (template_));
		  else
		    {
		      /* incompatiblity between inherited thing and a locally
		         defined thing */
		      ERROR3 (error, ER_SM_INCOMPATIBLE_SHADOW, winner->name,
			      candidate_source_name (template_, c),
			      template_classname (template_));
		    }
		  break;
		case DC_MORE_SPECIFIC:
		  if (local != NULL)
		    {
		      /* trying to shadow an inherited attribute with a more specific
		         domain */
		      ERROR3 (error, ER_SM_INCOMPATIBLE_SHADOW, winner->name,
			      candidate_source_name (template_, c),
			      template_classname (template_));
		    }
		  else
		    {
		      /* must override requested resolution or issue error */
		      if (winner != requested || auto_resolve)
			{
			  winner = c;
			  /* reset conflict when upgrading the domain of the winner */
			  conflict = NULL;
			}
		      else
			{
			  /* can't override resolution on <attname> of <classname> with
			     required attribute from <classname2> */
			  ERROR4 (error, ER_SM_RESOLUTION_OVERRIDE,
				  winner->name,
				  candidate_source_name (template_, winner),
				  candidate_source_name (template_, c),
				  template_classname (template_));
			}
		    }
		  break;
		case DC_EQUAL:
		  /* remember the conflict for later, it may be ignored if there
		     is another candidate with a more specific domain */
		  if (local == NULL && winner != requested)
		    conflict = c;
		  break;
		case DC_LESS_SPECIFIC:
		  /* ignore it */
		  break;
		}
	    }
	}
    }

  /* check for compatibility with any aliased components */
  if (error == NO_ERROR && alias != NULL)
    {
      if (winner == NULL)
	{
	  ERROR3 (error, ER_SM_MISSING_ALIAS_SUBSTITUTE, alias->name,
		  candidate_source_name (template_, alias),
		  template_classname (template_));
	}
      else
	{
	  dstate = compare_component_domains (winner->obj, alias->obj);
	  if (dstate == DC_INCOMPATIBLE)
	    {
	      /* we need to differentiate between a local reference
	       * conflicting with an alias so that we can give a
	       * better error message.
	       */
	      if (local == winner)
		{
		  ERROR3 (error, ER_SM_INCOMPATIBLE_ALIAS_LOCAL_SUB,
			  winner->name, candidate_source_name (template_,
							       alias),
			  template_classname (template_));
		}
	      else
		{
		  ERROR4 (error, ER_SM_INCOMPATIBLE_ALIAS_SUBSTITUTE,
			  winner->name, candidate_source_name (template_,
							       winner),
			  candidate_source_name (template_, alias),
			  template_classname (template_));
		}
	    }
	  else if (dstate == DC_LESS_SPECIFIC)
	    {
	      ERROR4 (error, ER_SM_LESS_SPECIFIC_ALIAS_SUBSTITUTE,
		      winner->name, candidate_source_name (template_, alias),
		      candidate_source_name (template_, winner),
		      template_classname (template_));
	    }
	}
    }

  /* check for conflicts between two classes of the most specific domains */
  if (error == NO_ERROR && conflict != NULL)
    {
      if (auto_resolve)
	{
	  resspace = sm_resolution_space (winner->name_space);
	  auto_resolve_conflict (winner, &template_->resolutions, resspace);
	}
      else
	{
	  ERROR3 (error, ER_SM_ATTRIBUTE_NAME_CONFLICT, winner->name,
		  candidate_source_name (template_, winner),
		  candidate_source_name (template_, conflict));
	}
    }

  if (error == NO_ERROR)
    *winner_return = winner;
  else
    *winner_return = NULL;
  return (error);
}

/* COMPONENT FLATTENING */
/*
 * insert_attribute()
 * insert_method() - This inserts an attribute into a list positioned according
 *    to the "order" field.  Should add this to class.c someday.
 *    This is intended to be used for the ordering of the flattened attribute
 *    list.  As such, we don't use the order_link field here we just use
 *    the regular next field.
 *    Unfortunately we need a seperate method version of this since the
 *    order field isn't part of the common header.
 *   return: none
 *   attlist(in/out): pointer to attribte list
 *   att(in): attribute to insert
 */

static void
insert_attribute (SM_ATTRIBUTE ** attlist, SM_ATTRIBUTE * att)
{
  SM_ATTRIBUTE *a, *prev;

  prev = NULL;
  for (a = *attlist; a != NULL && a->order < att->order;
       a = (SM_ATTRIBUTE *) a->header.next)
    prev = a;

  att->header.next = (SM_COMPONENT *) a;
  if (prev == NULL)
    *attlist = att;
  else
    prev->header.next = (SM_COMPONENT *) att;
}

static void
insert_method (SM_METHOD ** methlist, SM_METHOD * method)
{
  SM_METHOD *m, *prev;

  prev = NULL;
  for (m = *methlist; m != NULL && m->order < method->order;
       m = (SM_METHOD *) m->header.next)
    prev = m;

  method->header.next = (SM_COMPONENT *) m;
  if (prev == NULL)
    *methlist = method;
  else
    prev->header.next = (SM_COMPONENT *) method;
}

/*
 * flatten_components() - This is used to flatten the components of a template.
 *    The components are first converted into a list of candidates.
 *    The candidates list is then checked for the rules of compatibility
 *    and conflicts are resolved.  The winning candidate for each name
 *    is then converted back to a component and added to the template
 *    on the appropriate list.
 *    NOTE: Formerly we assumed that the candidates would be pruned and
 *    resolved in order.  Although the "order" field in each candidate
 *    will be set correctly we can't assume that the resulting list we
 *    produce is also ordered.  This is important mainly because this
 *    template will be stored on the class and used in the flattening
 *    of any subclasses.  get_candidates assumes that the template
 *    lists of the super classes are ordered.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   flat(out): flattened tamplate
 *   namespace(in): component name_space
 *   auto_res(in): non-zero to enable auto resolution of conflicts
 */

static int
flatten_components (SM_TEMPLATE * def, SM_TEMPLATE * flat,
		    SM_NAME_SPACE name_space, int auto_res)
{
  int error = NO_ERROR;
  SM_CANDIDATE *candlist, *candidates, *winner;
  SM_COMPONENT *comp;

  /* get all of the possible candidates for this name_space (class or instance) */
  candlist = get_candidates (def, flat, name_space);

  /* prune the like named candates of the list one at a time, check
     for consistency and resolve any conflicts */

  while ((error == NO_ERROR) &&
	 ((candidates = prune_candidate (&candlist)) != NULL))
    {

      error = resolve_candidates (flat, candidates, auto_res, &winner);

      if (error == NO_ERROR)
	{
	  if (winner != NULL)
	    {
	      /* convert the candidate back to a component */
	      comp = make_component_from_candidate (def->op, winner);
	      if (comp == NULL)
		{
		  error = er_errid ();
		  free_candidates (candidates);
		  break;
		}

	      /* add it to the appropriate list */
	      switch (comp->name_space)
		{
		case ID_ATTRIBUTE:
		case ID_SHARED_ATTRIBUTE:
		  insert_attribute (&flat->attributes, (SM_ATTRIBUTE *) comp);
		  break;
		case ID_CLASS_ATTRIBUTE:
		  insert_attribute (&flat->class_attributes,
				    (SM_ATTRIBUTE *) comp);
		  break;
		case ID_METHOD:
		  insert_method (&flat->methods, (SM_METHOD *) comp);
		  break;
		case ID_CLASS_METHOD:
		  insert_method (&flat->class_methods, (SM_METHOD *) comp);
		  break;
		default:
		  break;
		}
	    }
	}
      free_candidates (candidates);
    }
  /*  If an error occurs, the remaining candidates in candlist should be freed
   */

  if (candlist)
    free_candidates (candlist);


  return (error);
}

/*
 * flatten_method_files() - Flatten the method file lists from the template
 *    into the flattened template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   flat(out): flattened template
 */

static int
flatten_method_files (SM_TEMPLATE * def, SM_TEMPLATE * flat)
{
  DB_OBJLIST *super;
  SM_CLASS *class_;
  SM_METHOD_FILE *mfile, *new_mfile;

  /* start by copying the local files to the template */
  if (classobj_copy_methfiles (def->method_files, NULL, &flat->method_files))
    goto memory_error;

  /* collect files from the super classes if we don't already have them */

  for (super = flat->inheritance; super != NULL; super = super->next)
    {

      /* better not be any fetch errors at this point */
      if (au_fetch_class_force (super->op, &class_, AU_FETCH_READ))
	goto memory_error;	/* may be a deadlock abort !, don't overwrite the error */

      /* if the class is being edited, be sure and get its pending file list */
      if (class_->new_ != NULL)
	mfile = class_->new_->method_files;
      else
	mfile = class_->method_files;

      for (; mfile != NULL; mfile = mfile->next)
	{
	  if (!NLIST_FIND (flat->method_files, mfile->name))
	    {
	      new_mfile = classobj_make_method_file (mfile->name);
	      if (new_mfile == NULL)
		goto memory_error;
	      new_mfile->class_mop = mfile->class_mop;
	      WS_LIST_APPEND (&flat->method_files, new_mfile);
	    }
	}
    }
  return NO_ERROR;

memory_error:
  return er_errid ();
}

/*
 * flatten_query_spec_lists() - Flatten the query_spec lists.
 *    Note that query_spec lists aren't flattened, we just use the one
 *    currently in the template.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   flat(out): flattened template
 */

static int
flatten_query_spec_lists (SM_TEMPLATE * def, SM_TEMPLATE * flat)
{
  /* start by copying the local definitions to the template */
  if (def->query_spec == NULL)
    flat->query_spec = NULL;
  else
    {
      flat->query_spec = classobj_copy_query_spec_list (def->query_spec);
      if (flat->query_spec == NULL)
	return er_errid ();
    }
  /* no need to flatten the query_spec lists */
  return NO_ERROR;
}

/*
 * filter_resolutions() - Work function for check_shadowed_resolutions.
 *    This will search the resolution list for entries that use a particular
 *    name and remove them.  This is used to remove resolution entries
 *    that are invalid because there is a local definition for a class
 *    component (attribute or method) that must use that name.
 *   return: none
 *   template(in/out): class definition template
 *   name(in): component name
 *   resspace(in): component name_space
 */

static void
filter_component_resolutions (SM_TEMPLATE * template_,
			      const char *name, SM_NAME_SPACE resspace)
{
  SM_RESOLUTION **reslist, *res, *prev, *next;
  const char *rname;

  reslist = (resspace == ID_INSTANCE) ?
    &(template_->resolutions) : &(template_->class_resolutions);

  prev = next = NULL;
  for (res = *reslist; res != NULL; res = next)
    {
      next = res->next;
      if (res->name_space != resspace)
	prev = res;
      else
	{
	  rname = (res->alias == NULL) ? res->name : res->alias;
	  if (SM_COMPARE_NAMES (rname, name) != 0)
	    prev = res;
	  else
	    {
	      if (prev == NULL)
		*reslist = next;
	      else
		prev->next = next;
	      res->next = NULL;
	      classobj_free_resolution (res);
	    }
	}
    }
}

/*
 * remove_shadowed_resolutions() - This will make sure that there are
 *    no resolutions in the flattened template that conflict with the names
 *    of any locally defined components.
 *    Since the local components will always take precidence over the
 *    inherited components, resolutions for these inherited components would
 *    make no sense.
 *    Note that since the flattened template hasn't been populated with
 *    components yet, we get the local component list from the original
 *    template but we modify the resolution list on the flattened
 *    template.
 *   return: none
 *   original(in):
 *   flat(in/out):
 */

static void
remove_shadowed_resolutions (SM_TEMPLATE * original, SM_TEMPLATE * flat)
{
  SM_COMPONENT *comp;

  FOR_COMPONENTS (original->attributes, comp)
    filter_component_resolutions (flat, comp->name, ID_INSTANCE);

  FOR_COMPONENTS (original->methods, comp)
    filter_component_resolutions (flat, comp->name, ID_INSTANCE);

  FOR_COMPONENTS (original->class_attributes, comp)
    filter_component_resolutions (flat, comp->name, ID_CLASS);

  FOR_COMPONENTS (original->class_methods, comp)
    filter_component_resolutions (flat, comp->name, ID_CLASS);
}

/*
 * filter_reslist() - This removes any resolutions in the list that
 *    reference the deleted class.
 *   return: none
 *   reslist(in/out): resolution list filter
 *   deleted_class(in): class to remove
 */

static void
filter_reslist (SM_RESOLUTION ** reslist, MOP deleted_class)
{
  SM_RESOLUTION *res, *next, *prev;

  /* filter out any resolutions for the deleted class */
  if (deleted_class != NULL)
    {
      for (res = *reslist, prev = NULL, next = NULL; res != NULL; res = next)
	{
	  next = res->next;
	  if (res->class_mop != deleted_class)
	    {
	      prev = res;
	    }
	  else
	    {
	      if (prev == NULL)
		*reslist = next;
	      else
		prev->next = next;
	      classobj_free_resolution (res);
	    }
	}
    }
}

/*
 * check_resolution_target() - This checks to see if a particular resolution
 *    makes sense for a template.  This means that the class specified in the
 *    resolution must be on the inheritance list of the template and that the
 *    component name in the resolution must be a valid component of the
 *    class.
 *    This may be more easilly done if we keep track of the resolutions
 *    that were actually used during flattening and then prune the ones
 *    that weren't used.  Think about doing this when we rewrite the
 *    flattening algorighm.
 *    Determination of which list to look on to match the resolution is
 *    kind of brute force, when the flattening structures are redesigned,
 *    Try to maintain them in such a way that this sort of operation is
 *    easier.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class template
 *   res(in): resolution to check
 *   valid_ptr(out): set if resolution is valid (returned)
 */

static int
check_resolution_target (SM_TEMPLATE * template_, SM_RESOLUTION * res,
			 int *valid_ptr)
{
  int error = NO_ERROR;
  SM_CLASS *super;
  int valid;

  valid = 0;
  if (ml_find (template_->inheritance, res->class_mop))
    {
      /* the class exists, must check to see if the attribute still
         exists in the class. Note that since we may be in a subclass of
         the edited class, we have to look for templates on the superclass. */
      if ((error =
	   au_fetch_class_force (res->class_mop, &super,
				 AU_FETCH_READ)) == NO_ERROR)
	{
	  if (super->new_ != NULL)
	    {
	      /* its got a template */
	      if (res->name_space == ID_INSTANCE)
		{
		  if (SM_FIND_NAME_IN_COMPONENT_LIST
		      (super->new_->attributes, res->name) != NULL
		      || SM_FIND_NAME_IN_COMPONENT_LIST (super->new_->methods,
							 res->name) != NULL)
		    valid = 1;
		}
	      else
		{
		  if (SM_FIND_NAME_IN_COMPONENT_LIST
		      (super->new_->class_attributes, res->name) != NULL
		      || SM_FIND_NAME_IN_COMPONENT_LIST (super->new_->
							 class_methods,
							 res->name) != NULL)
		    valid = 1;
		}
	    }
	  else
	    {
	      /* no template, look directly at the class */
	      if (res->name_space == ID_INSTANCE)
		{
		  if (classobj_find_component (super, res->name, 0) != NULL)
		    valid = 1;
		}
	      else
		{
		  if (classobj_find_component (super, res->name, 1))
		    valid = 1;
		}
	    }
	}
    }
  *valid_ptr = valid;
  return (error);
}

/*
 * check_invalid_resolutions() - This checks a new resolution list for
 *    resolutions that don't make any sense. If an invalid resolution appears
 *    in the current definition of a class, it has atrophied as a side affect
 *    of some operation and will be removed silently.
 *    If an invalid resolution does not appear in the current definition,
 *    it was placed in the template by the user in an invalid state and
 *    an error will be generated.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): class template
 *   resolutions(in): the resolution list to examine
 *   original_list(in): the resoltion list in the current class definition
 */

static int
check_invalid_resolutions (SM_TEMPLATE * template_,
			   SM_RESOLUTION ** resolutions,
			   SM_RESOLUTION * original_list)
{
  int error = NO_ERROR;
  SM_RESOLUTION *res, *prev, *next, *original;
  int valid;

  for (res = *resolutions, prev = NULL, next = NULL;
       res != NULL && error == NO_ERROR; res = next)
    {
      next = res->next;
      if ((error =
	   check_resolution_target (template_, res, &valid)) == NO_ERROR)
	{
	  if (valid)
	    prev = res;
	  else
	    {
	      /* looks bogus try to find it in the original list */
	      original =
		classobj_find_resolution (original_list, res->class_mop,
					  res->name, res->name_space);
	      if (original != NULL)
		{
		  /* see if the aliases are the same */
		  if (res->alias != original->alias)
		    {
		      if (res->alias != NULL && original->alias != NULL)
			{
			  if (SM_COMPARE_NAMES (res->alias, original->alias)
			      != 0)
			    original = NULL;	/* aliases different */
			}
		      else
			original = NULL;	/* aliaes different */
		    }
		}
	      if (original != NULL)
		{
		  /* an old resolution that is no longer valid, remove it */
		  if (prev == NULL)
		    *resolutions = next;
		  else
		    prev->next = next;
		  classobj_free_resolution (res);
		}
	      else
		{
		  /* a new resolution that is not valid, signal an error */
		  ERROR3 (error, ER_SM_INVALID_RESOLUTION,
			  template_classname (template_),
			  res->name, sm_class_name (res->class_mop));
		}
	    }
	}
    }
  return (error);
}

/*
 * flatten_resolutions() - Flatten the resolutions for a template.
 *    This doesn't really flatten, it just cleans up the resolution
 *    lists.
 *    If a class was deleted, remove any references to the deleted class.
 *    Remove resolutions for inherited components that are now shadowed
 *    by local components.
 *    Remove resolutions for non-existent super classes.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   flat(in/out): flattened template
 *   deleted_class(in): deleted class object (optional, can be NULL)
 */

static int
filter_resolutions (SM_TEMPLATE * def, SM_TEMPLATE * flat, MOP deleted_class)
{
  int error = NO_ERROR;
  SM_RESOLUTION *original;

  /* no flattening, just get the locally defined resolutions */
  if (classobj_copy_reslist
      (def->resolutions, ID_INSTANCE, &flat->resolutions))
    return er_errid ();

  if (classobj_copy_reslist
      (def->class_resolutions, ID_CLASS, &flat->class_resolutions))
    return er_errid ();

  /* filter resolutions that are shadowed by local definitions,
     might consider these error conditions ? */
  remove_shadowed_resolutions (def, flat);

  /* remove all references to the deleted class if any */
  filter_reslist (&flat->resolutions, deleted_class);
  filter_reslist (&flat->class_resolutions, deleted_class);

  /* look for newly added bogus resolutions */
  original = (def->current == NULL) ? NULL : def->current->resolutions;
  error = check_invalid_resolutions (flat, &flat->resolutions, original);
  if (error == NO_ERROR)
    error =
      check_invalid_resolutions (flat, &flat->class_resolutions, original);

  return (error);
}

/*
 * find_matching_att() - This is a work function for retain_attribute_ids and
 *    others. It performs a very common attribute loopup operation.
 *    An attribute is said to match if the name, source class, and type
 *    are the same.
 *    If idmatch is selected the match is based on the id nubmers only.
 *   return: matching attribute
 *   list(in): attribute list to search
 *   att(in): attribute to look for
 *   idmatch(in): flag to cause search based on id rather than name
 */

static SM_ATTRIBUTE *
find_matching_att (SM_ATTRIBUTE * list, SM_ATTRIBUTE * att, int idmatch)
{
  SM_ATTRIBUTE *a, *found;

  found = NULL;
  for (a = list; a != NULL && found == NULL;
       a = (SM_ATTRIBUTE *) a->header.next)
    {
      if (idmatch)
	{
	  if (a->header.name_space == att->header.name_space &&
	      a->id == att->id)
	    found = a;
	}
      else
	{
	  if (a->header.name_space == att->header.name_space &&
	      SM_COMPARE_NAMES (a->header.name, att->header.name) == 0 &&
	      a->class_mop == att->class_mop && a->type == att->type)
	    found = a;
	}
    }
  return (found);
}


/*
 * retain_former_ids() - This is a bit of a kludge because we lost the ids of
 *    the inherited attributes when the template was created.
 *    This is a problem for inherited attributes that have been renamed
 *    in the super class.  Since they won't match based on name and the
 *    attibute id is -1, build_storage_order will think the inherited
 *    attribute was dropped and replaced with one of a different name.
 *    Immediately after flattening, we call this to fix the attribute
 *    id assignments for things that are the same.
 *    I think this would be a good place to copy the values of shared
 *    and class attributes as well.  We will have the same problem  of
 *    name matching.
 *    When shadowing a inherited attribute, we used to think that we should
 *    retain the former attribute ID so that we don't lose access to data
 *    previously stored for that attribute.  We now think that this is not
 *    the correct behavior.  A shadowed attribute is a "new" attribute and
 *    it should shadow the inherited attribute along with it's previously
 *    stored values.
 *   return: none
 *   flat(in): template
 */

static void
retain_former_ids (SM_TEMPLATE * flat)
{
  SM_ATTRIBUTE *new_att, *found, *super_new, *super_old;
  SM_CLASS *sclass;

  /* Does this class have a previous representation ? */
  if (flat->current != NULL)
    {

      /* Check each new inherited attribute.  These attribute will not have
         an assigned id and their class MOPs will not match */
      FOR_ATTRIBUTES (flat->attributes, new_att)
      {
	/* is this a new attribute ? */
	if (new_att->id == -1)
	  {

	    /* is it inherited ? */
	    if (new_att->class_mop != NULL && new_att->class_mop != flat->op)
	      {
		/* look for a matching attribute in the existing representation */
		found =
		  find_matching_att (flat->current->attributes, new_att, 0);
		if (found != NULL)
		  /* re-use this attribute */
		  new_att->id = found->id;
		else
		  {
		    /* couldn't find it, it may have been renamed in the super
		       class though */
		    if (au_fetch_class_force (new_att->class_mop, &sclass,
					      AU_FETCH_READ) == NO_ERROR)
		      {
			/* search the super class' pending attribute list for
			   this name */
			if (sclass->new_ != NULL)
			  {
			    super_new =
			      find_matching_att (sclass->new_->attributes,
						 new_att, 0);
			    if (super_new != NULL)
			      {
				/*
				 * search the supers original attribute list
				 * based on the id of the new one
				 */
				super_old =
				  find_matching_att (sclass->attributes,
						     super_new, 1);
				if (super_old != NULL)
				  {
				    if (SM_COMPARE_NAMES
					(super_old->header.name,
					 new_att->header.name) != 0)
				      {
					/* search our old list with the old name */
					found =
					  find_matching_att (flat->current->
							     attributes,
							     super_old, 0);
					if (found != NULL)
					  /* found the renamed attribute, reuse id */
					  new_att->id = found->id;
				      }
				  }
			      }
			  }
		      }
		  }
	      }

/* As mentioned in the description above, we no longer think that
   it is a good idea to retain the old attribute ID when shadowing
   an inherited attribute.  Since we had thought differently before
   and might think differently again I would rather keep this part
   of the code in here as a reminder.  JB (3/6/96) */
#if 0
	    else
	      {
		/* Its a new local attribute.  If we're shadowing a previously
		   inherited attribute, reuse the old id so we don't lose the
		   previous value.  This is new (12/7/94), does it cause
		   unexpected problems ? */
		/* look for one in the existing representation */
		found =
		  classobj_find_attribute_list (flat->current->attributes,
						new->header.name, -1);
		/* was it inherited ? */
		if (found != NULL && found->class != new->class)
		  {
		    /* reuse the attribute id, don't have to worry about type
		       compatibility because that must have been checked during
		       flattening. */
		    new->id = found->id;
		  }
		/* else couldn't find it, do we need to deal with the case where
		   the inherited attribute from the super class has been renamed
		   as is done above ? */
	      }
#endif /* 0 */
	  }
      }
    }
}


/*
 * flatten_trigger_cache() - This re-flattens the trigger cache for triggers
 *    directly on this class (not associated with an attribute).
 *    The attirbute caches are maintained directly on the attributes.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   flat(out): flattened template
 */

static int
flatten_trigger_cache (SM_TEMPLATE * def, SM_TEMPLATE * flat)
{
  int error = NO_ERROR;
  void *flat_triggers, *super_triggers;
  DB_OBJLIST *super;
  SM_CLASS *class_;

  /* trigger list in def has been filtered to contain only those
     triggers defined directly on the class, combine these with
     those on the current super classes */

  flat_triggers = NULL;
  if (def->triggers != NULL)
    flat_triggers =
      tr_copy_schema_cache ((TR_SCHEMA_CACHE *) def->triggers, NULL);
  else
    flat_triggers = tr_make_schema_cache (TR_CACHE_CLASS, NULL);

  if (flat_triggers == NULL)
    error = er_errid ();

  for (super = flat->inheritance;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {

      /* better not be any fetch errors at this point */
      if (!(error = au_fetch_class_force (super->op, &class_, AU_FETCH_READ)))
	{

	  /* if the class is being edited, be sure and get its updated trigger cache */
	  if (class_->new_ != NULL)
	    super_triggers = class_->new_->triggers;
	  else
	    super_triggers = class_->triggers;

	  if (super_triggers != NULL)
	    error =
	      tr_merge_schema_cache ((TR_SCHEMA_CACHE *) flat_triggers,
				     (TR_SCHEMA_CACHE *) super_triggers);
	}
    }

  if (error)
    {
      if (flat_triggers != NULL)
	tr_free_schema_cache ((TR_SCHEMA_CACHE *) flat_triggers);
    }
  else
    {
      if (tr_empty_schema_cache ((TR_SCHEMA_CACHE *) flat_triggers))
	tr_free_schema_cache ((TR_SCHEMA_CACHE *) flat_triggers);
      else
	flat->triggers = flat_triggers;
    }

  return error;
}

/*
 * flatten_properties() - This combines the interesting properties from the
 *    superclasses into the template property list. This is used mainly for
 *    UNIQUE constraint properties which must be inherited uniformly by
 *    the subclasses.
 *    NOTE: Things will get a lot more complicated here when we start having
 *    to deal with constraints over multiple attributes.
 *    Note that for NEW classes or constraints, the BTID will not have been
 *    allocated at this time, it is allocated in
 *    allocate_disk_structures() call after flattening has finished.
 *    This means that unique constraint info that we inherit may have a NULL
 *    BTID (fields are all -1).  That's ok for now, it will look as if it
 *    was one of our own local unique constraints.  When we get around
 *    to calling allocate_disk_structures() we must always check to see
 *    if the associated attributes were inherited and if so, go back
 *    to the super class to get its real BTID.  It is assumred that the
 *    super class will have the real BTID by this time because the call
 *    to allocate_disk_structures() has been moved to preceed the call
 *    to update_subclasses().
 *    It would be nice if we could allocate the indexes DURING flattening
 *    rather than deferring it until the end.  This would make the whole
 *    think cleaner and less prone to error.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): original class template
 *   flat(out): flattened template being built
 */

static int
flatten_properties (SM_TEMPLATE * def, SM_TEMPLATE * flat)
{
  DB_OBJLIST *super;
  SM_CLASS *class_;
  DB_SET *props;
  SM_CLASS_CONSTRAINT *constraints, *c;
  SM_ATTRIBUTE *atts, *att;
  int error = NO_ERROR;

  constraints = NULL;

  /* start by copying over any locally defined properties */
  if (def->properties != NULL)
    {
      if (classobj_copy_props (def->properties, NULL, &flat->properties) !=
	  NO_ERROR)
	goto structure_error;	/* should be a memory error */
    }

  /* map over each super class */
  for (super = flat->inheritance; super != NULL; super = super->next)
    {

      /* better not be any fetch errors at this point */
      if (au_fetch_class_force (super->op, &class_, AU_FETCH_READ))
	goto structure_error;

      /* If the class is being edited, be sure and get its updated property &
       * attribute list. This is going to get really annoying if we have to deal
       * with non-instance attributes.
       */
      if (class_->new_ != NULL)
	{
	  props = class_->new_->properties;
	  atts = class_->new_->attributes;
	}
      else
	{
	  props = class_->properties;
	  atts = class_->attributes;
	}

      /* For right now, the only thing we're interested in is unique
       * constraint information.  As other inheritable things make
       * their way onto the property list, this function will become
       * more complicated.  Since its so much easier to walk over the
       * SM_CLASS_CONSTRAINT list than the property list, built a
       * transient constraint list.
       */
      if (classobj_make_class_constraints (props, atts, &constraints))
	{
	  goto structure_error;
	}

      for (c = constraints; c != NULL; c = c->next)
	{
	  /* ignore non-unique for now */
	  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (c->type) ||
	      c->type == SM_CONSTRAINT_FOREIGN_KEY)
	    {
	      SM_ATTRIBUTE **attrs;
	      int found_match;
	      int i;

	      attrs = c->attributes;
	      if (attrs[0] != NULL)
		{

		  /* Loop over each attribute in the constraint */
		  found_match = 1;
		  for (i = 0; ((attrs[i] != NULL) && found_match); i++)
		    {

		      /*
		       * Try to find a corresponding attribute in the flattened template
		       */
		      for (att = flat->attributes;
			   att != NULL;
			   att = (SM_ATTRIBUTE *) att->header.next)
			{
			  if (SM_COMPARE_NAMES
			      (attrs[i]->header.name, att->header.name) == 0)
			    break;
			}

		      /*
		       * If we found an attribute with a matching name but from a
		       * different source class, it still isn't a match since it was
		       * inherited from somewhere else.
		       */
		      if ((att == NULL)
			  || (att->class_mop != attrs[i]->class_mop))
			{
			  found_match = 0;
			}
		    }

		  if (found_match)
		    {
		      DB_VALUE cnstr_val;
		      int cnstr_exists = 0;

		      /* Does the constraint exist in the subclass ? */
		      DB_MAKE_NULL (&cnstr_val);
		      cnstr_exists =
			classobj_find_prop_constraint (flat->properties,
						       classobj_map_constraint_to_property
						       (c->type), c->name,
						       &cnstr_val);
		      /* If the constraint already exists then either;
		         1. The subclass has a constraint with the same name,
		         2. The inherited constraint was previously inherited
		         via another route through multiple inheritence.

		         In the first case, we will need to raise an error.  In the
		         second case, we can skip past this section since the
		         constraint has already been added.  We can test for the
		         second case by comparing the BTID's which will be equal if
		         the inherited constraint is identical to the local
		         constraint */
		      if (cnstr_exists)
			{
			  DB_SEQ *local_property;
			  DB_VALUE btid_val;
			  BTID btid;

			  /* Get the BTID from the local constraint */
			  DB_MAKE_NULL (&btid_val);
			  local_property = DB_GET_SEQ (&cnstr_val);
			  if (set_get_element (local_property, 0, &btid_val))
			    {
			      pr_clear_value (&cnstr_val);
			      goto structure_error;
			    }
			  if (classobj_btid_from_property_value
			      (&btid_val, &btid, NULL))
			    {
			      pr_clear_value (&btid_val);
			      pr_clear_value (&cnstr_val);
			      goto structure_error;
			    }
			  pr_clear_value (&btid_val);

			  /* Raise an error if the B-trees are not equal */
			  if (!BTID_IS_EQUAL (&btid, &c->index))
			    ERROR1 (error, ER_SM_CONSTRAINT_EXISTS, c->name);
			}
		      else
			{
			  if (classobj_put_index
			      (&(flat->properties), c->type, c->name, attrs,
			       c->asc_desc, &c->index, c->fk_info,
			       NULL) != NO_ERROR)
			    {
			      pr_clear_value (&cnstr_val);
			      goto structure_error;
			    }
			}

		      pr_clear_value (&cnstr_val);
		    }
		}
	    }
	}

      /* make sure we free the transient constraint list */
      classobj_free_class_constraints (constraints);
    }

  return error;

structure_error:

  classobj_free_class_constraints (constraints);
  /* should have a more appropriate error for this */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_INVALID_PROPERTY, 0);
  return er_errid ();
}

/*
 * flatten_template() - Flatten a template, checking for all of the various
 *    schema rules.  Returns a flattened template that forms the basis
 *    for a new class representation if all went well.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   deleted_class(in): MOP of deleted class (optional, can be NULL)
 *   flatp(out): returned pointer to flattened template
 *   auto_res(in): non-zero to enable auto resolution of conflicts
 */

static int
flatten_template (SM_TEMPLATE * def, MOP deleted_class,
		  SM_TEMPLATE ** flatp, int auto_res)
{
  int error = NO_ERROR;
  SM_TEMPLATE *flat;

  /* start with an empty template */
  flat = classobj_make_template (def->name, def->op, NULL);
  if (flat == NULL)
    goto memory_error;

  /* is this necessary ? */
  flat->class_type = def->class_type;

  /* remember this, CAN'T PASS THIS AS AN ARGUMENT to classobj_make_template */
  flat->current = def->current;
  flat->partition_of = def->partition_of;

  /* copy the super class list filtering out the deleted class if any */
  if (deleted_class != NULL)
    ml_remove (&def->inheritance, deleted_class);
  if (def->inheritance != NULL)
    {
      flat->inheritance = ml_copy (def->inheritance);
      if (flat->inheritance == NULL)
	goto memory_error;
    }

  /* merge the method file lists */
  if (flatten_method_files (def, flat))
    goto memory_error;

  /* merge query_spec lists */
  if (flatten_query_spec_lists (def, flat))
    goto memory_error;

  /* merge trigger caches */
  if (flatten_trigger_cache (def, flat))
    goto memory_error;

  /* copy the loader commands, we should be flattening these as well ? */
  if (def->loader_commands != NULL)
    {
      flat->loader_commands = ws_copy_string (def->loader_commands);
      if (flat->loader_commands == NULL)
	goto memory_error;
    }

  /* filter out any useless resolutions */
  error = filter_resolutions (def, flat, deleted_class);
  if (error == NO_ERROR)
    {
      /* flatten each component list */
      error = flatten_components (def, flat, ID_INSTANCE, auto_res);
      if (error == NO_ERROR)
	error = flatten_components (def, flat, ID_CLASS, auto_res);
    }

  /* Flatten the properties (primarily for constraints).
   * Do this after the components have been flattened so we can see use this
   * information for selecting constraint properties.
   */
  if (flatten_properties (def, flat))
    goto memory_error;

  /* if errors, throw away the template and abort */
  if (error != NO_ERROR)
    {
      classobj_free_template (flat);
      flat = NULL;;
    }
  else
    /* make sure these get kept */
    retain_former_ids (flat);

  *flatp = flat;
  return (error);

memory_error:
  if (flat != NULL)
    classobj_free_template (flat);
  return er_errid ();
}

/* PREPARATION FOR NEW REPRESENTATIONS */
/*
 * assign_attibute_id() - Generate an id for a shared or class attibute.
 *    Instance attribute id's are assigned during order_attibutes.
 *    Must use an existing attribute id if one exists.
 *    Note that the attribute id counter for all attributes is the same
 *    so the ids can be assumed unique across all attribute
 *    namespaces.
 *   return: none
 *   class(in/out): class structure
 *   att(in/out): attribute needing id
 *   class_attribute(in): non-zero for class name_space
 */

static void
assign_attribute_id (SM_CLASS * class_, SM_ATTRIBUTE * attribute,
		     int class_attribute)
{
  SM_ATTRIBUTE *attr;

  /* if it already has one, just leave it alone */
  if (attribute->id == -1)
    {

      if (class_attribute)
	{
	  attr = class_->class_attributes;
	}
      else
	{
	  attr = class_->shared;
	}

      for (; attr != NULL && attribute->id == -1;
	   attr = (SM_ATTRIBUTE *) attr->header.next)
	{
	  if ((SM_COMPARE_NAMES (attr->header.name, attribute->header.name) ==
	       0) && (attr->class_mop == attribute->class_mop)
	      && (attr->type == attribute->type))
	    {
	      /* reuse old id */
	      attribute->id = attr->id;
	    }
	}
      if (attribute->id)
	{
	  /* couldn't find an existing one, generate a new one */
	  attribute->id = class_->att_ids++;
	}
    }
}

/*
 * assign_method_id() - Generate an id for an instance or class method.
 *    Use the existing id if already present in the class.
 *   return: none
 *   class(in/out): class structure
 *   method(in/out): method needing an id
 *   class_method(in): non-zero for class name_space
 */

static void
assign_method_id (SM_CLASS * class_, SM_METHOD * method, bool class_method)
{
  SM_METHOD *m;

  if (method->id == -1)
    {
      if (class_method)
	m = class_->class_methods;
      else
	m = class_->methods;

      for (; m != NULL && method->id == -1; m = (SM_METHOD *) m->header.next)
	{
	  /* need to check return domains here and reassign id ? */
	  if ((SM_COMPARE_NAMES (m->header.name, method->header.name) == 0) &&
	      (m->class_mop == method->class_mop))
	    method->id = m->id;
	}
      if (method->id == -1)
	method->id = class_->method_ids++;
    }
}

/*
 * order_atts_by_alignment() - Order the attributes by descending order of
 *    alignment needs.  Within the same alignment group, order the attributes
 *    by ascending order of disk size (this is mainly for the char types).
 *    In this way, if the object is too large to fit on one page, we can try to
 *    keep the smaller char types on the same page as the OID and thereby
 *    we might be able to read the attributes we need without reading
 *    the overflow page.
 *    This algorithm is simplistic but these lists are not long.
 *   return: ordered attributes.
 *   atts(in/out): attributes to be ordered
 */
static SM_ATTRIBUTE *
order_atts_by_alignment (SM_ATTRIBUTE * atts)
{
  SM_ATTRIBUTE *newatts, *found, *attr;

  newatts = NULL;

  while (atts != NULL)
    {
      for (found = atts, attr = atts; attr != NULL;
	   attr = (SM_ATTRIBUTE *) attr->header.next)
	{

	  /* the new attr becomes the found attr if it has larger alignment
	   * requirements or if it has the same alignment needs but has
	   * smaller disk size.
	   */
	  if ((attr->type->alignment > found->type->alignment) ||
	      ((attr->type->alignment == found->type->alignment) &&
	       (tp_domain_disk_size (attr->domain) <
		tp_domain_disk_size (found->domain))))
	    {
	      found = attr;
	    }
	}
      /* move the one we found to the new list */
      WS_LIST_REMOVE (&atts, found);
      found->header.next = NULL;
      WS_LIST_APPEND (&newatts, found);
    }

  return newatts;
}

/*
 * build_storage_order() - Here we take a flattened template and reorder
 *    the attributes to be close to the ordering the class had before editing.
 *    In the process, we assign attribute ids. If the current and new attribute
 *    lists turn out to be the same, we can avoid the generation of a
 *    new representation since the disk structure of the objects will
 *    be the same.  If the two attribute lists differ, non-zero is
 *    returned indicating that a new representation must be generated.
 *    At the start, the template has a combined list of instance
 *    and shared attributes in the attributes list.  When this completes,
 *    the attributes list will be NULL and the attributes will have
 *    been split into two lists, ordered_attributes and shared_attributes.
 *    Formerly this function tried to retain ids of attributes that
 *    hadn't changed.  This is now done in retain_attribute_ids above.
 *    When we get here, this is the state of attribute id's in th e
 *    flattened template:
 *      id != -1  : this is a local attribute (possibly renamed) that needs
 *                  to keep its former attribute id.
 *      id == -1  : this is an inherited attribute or new local attribute
 *                  if its new, assign a new id, if its inherited, look
 *                  in the old attribute list for one that matches and reuse
 *                  the old id.  Searching the old list for matching
 *                  components could be done by make_component_from_candidate?
 *   return: non-zero if new representation is needed
 *   class(in): class being modified
 *   flat(out): new flattened template
 */

static int
build_storage_order (SM_CLASS * class_, SM_TEMPLATE * flat)
{
  SM_ATTRIBUTE *fixed, *variable, *current, *new_att, *found, *next, *newatts;
  int newrep;

  fixed = variable = NULL;
  newrep = 0;

  newatts = (SM_ATTRIBUTE *)
    classobj_filter_components ((SM_COMPONENT **) & flat->attributes,
				ID_ATTRIBUTE);

  FOR_ATTRIBUTES (class_->attributes, current)
  {
    found = NULL;
    for (new_att = newatts; new_att != NULL && found == NULL;
	 new_att = (SM_ATTRIBUTE *) new_att->header.next)
      {

	/* if the id's are the same, use it without looking at the name,
	   this is how rename works */
	if (new_att->id != -1)
	  {
	    if (new_att->id == current->id)
	      found = new_att;
	  }

	/* this shoudn't be necessary now that we assume id's have been
	   assigned where there was one before */

	else
	  if ((SM_COMPARE_NAMES (current->header.name, new_att->header.name)
	       == 0) && (current->class_mop == new_att->class_mop)
	      && (current->type == new_att->type))
	  {
	    /* fprintf(stdout, "Shouldn't be here \n"); */
	    found = new_att;
	  }
      }

    if (found == NULL)
      newrep = 1;		/* attribute was deleted */
    else
      {
	/* there was a match, either in name or id */
	if (found->id == -1)
	  /* name match, reuse the old id */
	  found->id = current->id;

	(void) WS_LIST_REMOVE (&newatts, found);
	found->header.next = NULL;
	if (found->type->variable_p)
	  WS_LIST_APPEND (&variable, found);
	else
	  WS_LIST_APPEND (&fixed, found);
      }
  }

  /* check for new attributes */
  if (newatts != NULL)
    {
      newrep = 1;
      for (new_att = newatts, next = NULL; new_att != NULL; new_att = next)
	{
	  next = (SM_ATTRIBUTE *) new_att->header.next;
	  new_att->header.next = NULL;
	  new_att->id = class_->att_ids++;
	  if (new_att->type->variable_p)
	    WS_LIST_APPEND (&variable, new_att);
	  else
	    WS_LIST_APPEND (&fixed, new_att);
	}
    }

  /* order the fixed attributes in descending order by alignment needs */
  if (fixed != NULL)
    {
      fixed = order_atts_by_alignment (fixed);
    }

  /* join the two lists */
  if (fixed == NULL)
    newatts = variable;
  else
    {
      newatts = fixed;
      for (new_att = fixed; new_att != NULL && new_att->header.next != NULL;
	   new_att = (SM_ATTRIBUTE *) new_att->header.next);
      new_att->header.next = (SM_COMPONENT *) variable;
    }

  if (flat->partition_parent_atts != NULL)
    {
      /* if partition subclass is created,
         the class must have the same attributes order and id with its parent class
       */
      SM_ATTRIBUTE *supatt, *reorder = NULL, *a, *prev;

      FOR_ATTRIBUTES (flat->partition_parent_atts, supatt)
      {
	prev = found = NULL;
	for (a = newatts; a != NULL; a = (SM_ATTRIBUTE *) a->header.next)
	  {
	    if (SM_COMPARE_NAMES (a->header.name, supatt->header.name) == 0)
	      {
		found = a;
		found->id = supatt->id;
		if (prev == NULL)
		  newatts = (SM_ATTRIBUTE *) newatts->header.next;
		else
		  prev->header.next = found->header.next;
		found->header.next = NULL;
		WS_LIST_APPEND (&reorder, found);
		break;
	      }
	    prev = a;
	  }
      }
      WS_LIST_APPEND (&reorder, newatts);
      newatts = reorder;
    }
  /* now change the template to reflect the divided instance and shared attribute
     lists */
  flat->instance_attributes = newatts;
  flat->shared_attributes = flat->attributes;
  flat->attributes = NULL;

  return (newrep);
}

/*
 * fixup_component_classes() - Work function for install_new_representation.
 *    Now that we're certain that the template can be applied
 *    and we have a MOP for the class being edited, go through and stamp
 *    the attibutes and methods of the class with the classmop.  This
 *    makes it easier later for the browsing functions to get the origin
 *    class of attributes.  This is only a problem when the class is
 *    defined for the first time.
 *   return: none
 *   classop(in): class object
 *   flat(out): flattened template
 */

static void
fixup_component_classes (MOP classop, SM_TEMPLATE * flat)
{
  SM_ATTRIBUTE *a;
  SM_METHOD *m;
  SM_METHOD_FILE *f;

  FOR_ATTRIBUTES (flat->attributes, a)
  {
    if (a->class_mop == NULL)
      a->class_mop = classop;
  }
  FOR_ATTRIBUTES (flat->class_attributes, a)
  {
    if (a->class_mop == NULL)
      a->class_mop = classop;
  }
  FOR_METHODS (flat->methods, m)
  {
    if (m->class_mop == NULL)
      m->class_mop = classop;
  }
  FOR_METHODS (flat->class_methods, m)
  {
    if (m->class_mop == NULL)
      m->class_mop = classop;
  }
  for (f = flat->method_files; f != NULL; f = f->next)
    {
      if (f->class_mop == NULL)
	f->class_mop = classop;
    }
}


/*
 * fixup_self_domain()
 * fixup_method_self_domains()
 * fixup_attribute_self_domain()
 * fixup_self_reference_domains() - Domains that were build for new classes
 *    that need to reference the class being build were constructed in a
 *    special way since the MOP of the class was not available at the time the
 *    domain structure was created.  Once semantic checking has been performed
 *    and the class is created, we not must go through and modify the
 *    temporary domain structures to look like real self-referencing
 *    domains.  See documentation on the get_domain() function in the
 *    file smt.c
 *    We now have a number of last minute fixup functions.  Try to bundle
 *    these into a single function sometime to avoid repeated passes
 *    over the class structures.  Not really that performance critical but
 *    nicer if this isn't spread out all over.
 *   return: none
 *   classop(in): class object
 *   flag(in/out): flattened template
 */

static void
fixup_self_domain (TP_DOMAIN * domain, MOP self)
{
  TP_DOMAIN *d;

  for (d = domain; d != NULL; d = d->next)
    {
      if (d->type == tp_Type_null)
	{
	  d->type = tp_Type_object;
	  d->class_mop = self;
	}
      fixup_self_domain (d->setdomain, self);
    }
}

static void
fixup_method_self_domains (SM_METHOD * meth, MOP self)
{
  SM_METHOD_SIGNATURE *sig;
  SM_METHOD_ARGUMENT *arg;

  for (sig = meth->signatures; sig != NULL; sig = sig->next)
    {
      for (arg = sig->value; arg != NULL; arg = arg->next)
	{
	  fixup_self_domain (arg->domain, self);
	  arg->domain = tp_domain_cache (arg->domain);
	}
      for (arg = sig->args; arg != NULL; arg = arg->next)
	{
	  fixup_self_domain (arg->domain, self);
	  arg->domain = tp_domain_cache (arg->domain);
	}
    }
}

static void
fixup_attribute_self_domain (SM_ATTRIBUTE * att, MOP self)
{
  /*
     Remember that attributes have a type pointer cache as well as a full
     domain.  BOTH of these need to be updated.  This is unfortunate, I
     think its time to remove the type pointer and rely on the domain
     structure only. */

  fixup_self_domain (att->domain, self);
  att->domain = tp_domain_cache (att->domain);

  /* get the type cache as well */
  if (att->type == tp_Type_null)
    att->type = tp_Type_object;
}

static void
fixup_self_reference_domains (MOP classop, SM_TEMPLATE * flat)
{
  SM_ATTRIBUTE *a;
  SM_METHOD *m;

  /* should only bother with this if the class is new, can we somehow
     determine this here ? */

  FOR_ATTRIBUTES (flat->attributes, a)
    fixup_attribute_self_domain (a, classop);

  FOR_ATTRIBUTES (flat->class_attributes, a)
    fixup_attribute_self_domain (a, classop);

  FOR_ATTRIBUTES (flat->shared_attributes, a)
    fixup_attribute_self_domain (a, classop);

  FOR_METHODS (flat->methods, m) fixup_method_self_domains (m, classop);

  FOR_METHODS (flat->class_methods, m) fixup_method_self_domains (m, classop);
}

/* DISK STRUCTURE ALLOCATION */
/*
 * construct_index_key_domain()
 *   return:
 *   n_atts(in):
 *   atts(in):
 *   asc_desc(in):
 */

static TP_DOMAIN *
construct_index_key_domain (int n_atts, SM_ATTRIBUTE ** atts,
			    const int *asc_desc)
{
  int i;
  TP_DOMAIN *head = NULL;
  TP_DOMAIN *current = NULL;
  TP_DOMAIN *set_domain = NULL;
  TP_DOMAIN *new_domain = NULL;
  TP_DOMAIN *cached_domain = NULL;

  if (n_atts == 1)
    {
      if (asc_desc && asc_desc[0] == 1)
	{			/* is reverse index */
	  new_domain = tp_domain_copy (atts[0]->domain, false);
	  if (new_domain == NULL)
	    {
	      goto mem_error;
	    }

	  new_domain->is_desc = true;

	  cached_domain = tp_domain_cache (new_domain);
	}
      else
	{
	  cached_domain = atts[0]->domain;
	}
    }
  else if (n_atts > 1)
    {
      for (i = 0; i < n_atts; i++)
	{
	  new_domain = tp_domain_new (DB_TYPE_NULL);
	  if (new_domain == NULL)
	    {
	      goto mem_error;
	    }

	  new_domain->type = atts[i]->domain->type;
	  new_domain->precision = atts[i]->domain->precision;
	  new_domain->scale = atts[i]->domain->scale;
	  new_domain->codeset = atts[i]->domain->codeset;
	  new_domain->is_parameterized = atts[i]->domain->is_parameterized;
	  if (asc_desc && asc_desc[i] == 1)
	    {			/* is descending order */
	      new_domain->is_desc = true;
	    }
	  else
	    {
	      new_domain->is_desc = false;
	    }

	  if (head == NULL)
	    {
	      head = new_domain;
	      current = new_domain;
	    }
	  else
	    {
	      current->next = new_domain;
	      current = new_domain;
	    }
	}

      set_domain = tp_domain_construct (DB_TYPE_MIDXKEY, NULL, 0, 0, head);
      if (set_domain == NULL)
	{
	  goto mem_error;
	}

      cached_domain = tp_domain_cache (set_domain);
    }

  return cached_domain;

mem_error:

  if (head != NULL)
    {
      TP_DOMAIN *td, *next;
      for (td = head, next = NULL; td != NULL; td = next)
	{
	  next = td->next;
	  tp_domain_free (td);
	}
    }
  return (NULL);
}

/*
 * collect_hier_class_info() - calling this function in which case *n_classes
 *   			       will equal to 1 upon entry.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): Class MOP of the base class.
 *   subclasses(in): List of subclasses.
 *   constraint_name(in): Name of UNIQUE constraint to search for.
 *   reverse(in):
 *   n_classes(out): Number of subclasses which inherit the constraint.
 *   n_attrs(in): Number of attributes in constraint.
 *   oids(out): Array of class OID's which inherit the constraint.
 *   attr_ids(out): Array of attribute ID's for each class
 *   hfids(out): Array of heaps for classes whicTraverse the subclasses list
 *               looking for matching constraints.
 */

static int
collect_hier_class_info (MOP classop, DB_OBJLIST * subclasses,
			 const char *constraint_name, int reverse,
			 int *n_classes, int n_attrs,
			 OID * oids, int *attr_ids, HFID * hfids)
{
  DB_OBJLIST *sub;
  SM_CLASS *class_;
  int error = NO_ERROR;

  for (sub = subclasses; ((sub != NULL) && (error == NO_ERROR));
       sub = sub->next)
    {
      error = au_fetch_class_force (sub->op, &class_, AU_FETCH_READ);
      if (error == NO_ERROR)
	{
	  SM_TEMPLATE *flat;
	  SM_CLASS_CONSTRAINT *constraints, *found;
	  int *attr_ptr;

	  /* Get flattened template */
	  flat = class_->new_;

	  /* Make transient constraint cache from property list.  At this point
	     in the process, the property list should be current and include
	     inherited constraints */
	  error =
	    classobj_make_class_constraints (flat->properties,
					     flat->attributes, &constraints);
	  if (error == NO_ERROR)
	    {
	      /* Does this class contain the constraint that we're looking for?
	         Note that we're only interested in UNIQUE constraints at this
	         time. */
	      if (reverse)
		{
		  found = classobj_find_class_constraint (constraints,
							  SM_CONSTRAINT_REVERSE_UNIQUE,
							  constraint_name);
		}
	      else
		{
		  found = classobj_find_class_constraint (constraints,
							  SM_CONSTRAINT_UNIQUE,
							  constraint_name);
		  if (!found)
		    {
		      found = classobj_find_class_constraint (constraints,
							      SM_CONSTRAINT_PRIMARY_KEY,
							      constraint_name);
		    }
		}

	      /* If we found a constraint with a matching name, we also need to
	         make sure that the constraint originated in the class that we're
	         interested in.  If so, then save the class OID, attribute ID's
	         and HFID.  We attempt to maintain unique constraint names, but
	         it is possible for different constraint to have the same name.
	         This might happen if a subclass shadows and attribute which
	         invalidates the constraint and then adds a constraint of the same
	         name.  This might also be possible if a class inherits from
	         multiple parent which each have constraints of the same name. */
	      if (found && (found->attributes[0]->class_mop == classop))
		{
		  int i;

		  /* Make sure that we have a permanent OID for the class.  This
		     function only processes the subclasses.  We're assuming that
		     the base class has already been processed. */
		  if (OID_ISTEMP (ws_oid (sub->op)))
		    locator_assign_permanent_oid (sub->op);

		  COPY_OID (&oids[*n_classes], WS_OID (sub->op));

		  attr_ptr = &attr_ids[(*n_classes) * n_attrs];
		  for (i = 0; i < n_attrs; i++)
		    attr_ptr[i] = found->attributes[i]->id;

		  HFID_COPY (&hfids[*n_classes], &class_->header.heap);
		  (*n_classes)++;
		}

	      classobj_free_class_constraints (constraints);
	    }
	}

    }
  return error;
}

/*
   This done as a post processing pass of sm_update_class to make sure
   that all attributes that were declared to have indexes or unique btids
   tables have the necessary disk structures allocated.

   Logically this should be done before the class is created so if any
   errors occur, we can abort the operation.  Unfortunately, doing this
   accurately requires attribute id's being assigned so it would have
   to go in install_new_representation.  After beta, restructure the
   sequence of operations in sm_update_class and install_new_representation
   (and proably the flattener as well) so we have all the information necessary
   to generate the disk structures before the call to
   install_new_representation and before the class is created.

   allocate_index is also called directly by sm_add_index which for now
   will be the only official way to add an index.
*/

/*
 * allocate_index() - Allocates an index on disk for an attribute of a class.
 *    Note, if this is called from allocate_disk_structures it probably
 *    won't work because of the timing of instance/class flushes with
 *    btree loading.  This isn't a problem currently because indexes
 *    are only allocated via sm_add_index().  See comments there for
 *    more information.
 *    NOTE: the call to btree_add_index & btree_load_index require a class OID and
 *    an attribute ID.  I'm not sure why this is necessary, it doesn't
 *    have much meaning if this index is used over several classes as the
 *    UNIQUE indexes are.  It would be nice if we could be allocating the
 *    indexes earlier in the update_class() process, preferably during
 *    template flattening.  In that case its possible that the class OID
 *    and attribute ID won't be known yet so we'd have to pass NULL here.
 *    I don't think that should hurt anything but we should check to see
 *    what the bt_ & fl_ modules are doing with this information.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   subclasses(in): List of subclasses
 *   attrs(in): attribute getting the index
 *   asc_desc(in): asc/desc info list
 *   unique(in): True if were allocating a UNIQUE index.  False otherwise.
 *   reverse(in):
 *   constraint_name(in): Name of constraint.
 *   index(out): The BTID of the returned index.
 *   fk_refcls_oid(in):
 *   fk_refcls_pk_btid(in):
 *   cache_attr_id(in):
 *   fk_name(in):
 */

static int
allocate_index (MOP classop, SM_CLASS * class_, DB_OBJLIST * subclasses,
		SM_ATTRIBUTE ** attrs, const int *asc_desc, int unique,
		int reverse, const char *constraint_name, BTID * index,
		OID * fk_refcls_oid, BTID * fk_refcls_pk_btid,
		int cache_attr_id, const char *fk_name)
{
  int error = NO_ERROR;
  DB_TYPE type;
  int i, n_attrs;
  int *attr_ids = NULL;
  OID *oids = NULL;
  HFID *hfids = NULL;

  /* Count the attributes */
  for (i = 0, n_attrs = 0; attrs[i] != NULL; i++, n_attrs++)
    {
      type = attrs[i]->type->id;
      if (!tp_valid_indextype (type))
	{
	  error = ER_SM_INVALID_INDEX_TYPE;
	  er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, error, 1,
		  pr_type_name (type));
	}
    }

  if (error == NO_ERROR)
    {
      TP_DOMAIN *domain = NULL;

      domain = construct_index_key_domain (n_attrs, attrs, asc_desc);
      if (domain == NULL)
	{
	  error = er_errid ();
	}
      else
	{
	  int max_classes, n_classes, has_instances;
	  DB_OBJLIST *sub;

	  /* need to have macros for this !! */
	  index->vfid.volid = boot_User_volid;

	  /* Count maximum possible subclasses */
	  max_classes = 1;	/* Start with 1 for the current class */
	  for (sub = subclasses; sub != NULL; sub = sub->next)
	    max_classes++;

	  /* Allocate arrays to hold subclass information */
	  if ((attr_ids = (int *) malloc (max_classes * n_attrs *
					  sizeof (int))) == NULL)
	    goto mem_error;

	  if ((oids = (OID *) malloc (max_classes * sizeof (OID))) == NULL)
	    goto mem_error;

	  if ((hfids = (HFID *) malloc (max_classes * sizeof (HFID))) == NULL)
	    goto mem_error;

	  /* Enter the base class information into the arrays */
	  n_classes = 0;
	  COPY_OID (&oids[n_classes], WS_OID (classop));
	  for (i = 0; i < n_attrs; i++)
	    attr_ids[i] = attrs[i]->id;
	  HFID_COPY (&hfids[n_classes], &class_->header.heap);
	  n_classes++;

	  /* If we're creating a UNIQUE B-tree, we need to collect information
	     from subclasses which inherit the UNIQUE constraint */
	  if (unique)
	    {
	      error =
		collect_hier_class_info (classop, subclasses, constraint_name,
					 reverse, &n_classes, n_attrs, oids,
					 attr_ids, hfids);
	      if (error != NO_ERROR)
		goto gen_error;
	    }

	  /* Are there any populated classes for this index ? */
	  has_instances = 0;
	  for (i = 0; i < n_classes; i++)
	    {
	      if (!HFID_IS_NULL (&hfids[i])
		  && heap_has_instance (&hfids[i], &oids[i]))
		{
		  /* in case of error and instances exist */
		  has_instances = 1;
		  break;
		}
	    }

	  /* If there are no instances, then call btree_add_index() to create an
	     empty index, otherwise call btree_load_index () to load all of the
	     instances (including applicable subclasses) into a new B-tree */
	  if (!has_instances)
	    error = btree_add_index (index, domain, WS_OID (classop),
				     attrs[0]->id, unique, reverse);

	  /* If there are instances, load all of them (including applicable
	     subclasses) into the new B-tree */
	  else
	    error = btree_load_index (index, domain, oids, n_classes, n_attrs,
				      attr_ids, hfids, unique, reverse,
				      fk_refcls_oid, fk_refcls_pk_btid,
				      cache_attr_id, fk_name);

	  free_and_init (attr_ids);
	  free_and_init (oids);
	  free_and_init (hfids);
	}
    }

  return error;

mem_error:
  error = er_errid ();

gen_error:
  if (attr_ids != NULL)
    free_and_init (attr_ids);
  if (oids != NULL)
    free_and_init (oids);
  if (hfids != NULL)
    free_and_init (hfids);

  return error;
}

/*
 * deallocate_index() - Deallocate an index that was previously created for
 * 			an attribute.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   cons(in):
 *   index(in/out): index disk identifier
 */

static int
deallocate_index (SM_CLASS_CONSTRAINT * cons, BTID * index)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *con;
  int ref_count = 0;

  for (con = cons; con != NULL; con = con->next)
    {
      if (BTID_IS_EQUAL (index, &con->index))
	{
	  ref_count++;
	}
    }

  if (ref_count == 1 && !btree_delete_index (index))
    error = er_errid ();

  return (error);
}


/*
 * remove_class_from_index() - Remove the class from the B-tree.
 *    This is used when it's necessary to delete instances from a particular
 *    class out of a B-tree while leaving other class instances intact.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   oid(in): Class OID
 *   index(in/out): B-tree index
 *   heap(in/out): Class heap
 */

static int
rem_class_from_index (OID * oid, BTID * index, HFID * heap)
{
  int error = NO_ERROR;

  /* If there is no heap, then there cannot be instances to remove. */
  if (HFID_IS_NULL (heap))
    {
      return (error);
    }

  if (!locator_remove_class_from_index (oid, index, heap))
    error = er_errid ();

  return (error);
}

/*
 * build_fk_obj_cache()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   key_attrs(in): attribute getting the index
 *   asc_desc(in): asc/desc info list
 *   pk_cls_oid(in):
 *   pk_btid(in):
 *   cache_attr_id(in):
 *   fk_name(in):
 */

static int
build_fk_obj_cache (MOP classop, SM_CLASS * class_, SM_ATTRIBUTE ** key_attrs,
		    const int *asc_desc, OID * pk_cls_oid, BTID * pk_btid,
		    int cache_attr_id, char *fk_name)
{
  int error = NO_ERROR;
  int i, n_attrs;
  int *attr_ids = NULL;
  TP_DOMAIN *domain = NULL;
  OID *cls_oid;
  HFID *hfid;

  cls_oid = ws_oid (classop);
  hfid = &class_->header.heap;

  if (!HFID_IS_NULL (hfid) && heap_has_instance (hfid, cls_oid))
    {
      for (i = 0, n_attrs = 0; key_attrs[i] != NULL; i++, n_attrs++);

      domain = construct_index_key_domain (n_attrs, key_attrs, asc_desc);
      if (domain == NULL)
	{
	  return er_errid ();
	}

      attr_ids = (int *) malloc (n_attrs * sizeof (int));
      if (attr_ids == NULL)
	{
	  return er_errid ();
	}

      for (i = 0; i < n_attrs; i++)
	{
	  attr_ids[i] = key_attrs[i]->id;
	}

      error = locator_build_fk_obj_cache (cls_oid, hfid, domain, n_attrs,
					  attr_ids, pk_cls_oid, pk_btid,
					  cache_attr_id, fk_name);

      free_and_init (attr_ids);
    }

  return error;
}

/*
 * update_foreign_key_ref() - Update PK referenced by FK
 *   return: NO_ERROR on success, non-zero for ERROR
 *   ref_clsop(in): referenced class by FK 
 *   fk_info(in): foreign key info
 */
static int
update_foreign_key_ref (MOP ref_clsop, SM_FOREIGN_KEY_INFO * fk_info)
{
  SM_TEMPLATE *template_;
  int save, error;

  AU_DISABLE (save);

  template_ = dbt_edit_class (ref_clsop);
  if (template_ == NULL)
    {
      AU_ENABLE (save);
      return er_errid ();
    }

  error = classobj_put_foreign_key_ref (&(template_->properties), fk_info);
  if (error != NO_ERROR)
    {
      dbt_abort_class (template_);
      AU_ENABLE (save);
      return error;
    }

  ref_clsop = dbt_finish_class (template_);
  if (ref_clsop == NULL)
    {
      dbt_abort_class (template_);
      AU_ENABLE (save);
      return er_errid ();
    }

  AU_ENABLE (save);
  return NO_ERROR;
}

/*
 * allocate_unique_constraint() - Allocate index for unique constraints
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   con(in):constraint info
 *   subclasses(in): sub class list
 *   asc_desc(in): asc/desc info list
 */
static int
allocate_unique_constraint (MOP classop, SM_CLASS * class_,
			    SM_CLASS_CONSTRAINT * con,
			    DB_OBJLIST * subclasses)
{
  int unique, reverse;
  SM_CLASS *super_class;
  SM_CLASS_CONSTRAINT *super_con, *shared_con;
  const int *asc_desc;

  if (con->attributes[0]->class_mop == classop)
    {
      /* its local, allocate our very own index */
      unique = BTREE_CONSTRAINT_UNIQUE;
      if (con->type == SM_CONSTRAINT_PRIMARY_KEY)
	{
	  unique |= BTREE_CONSTRAINT_PRIMARY_KEY;
	}

      if (con->shared_cons_name)
	{
	  shared_con = classobj_find_cons_index (class_->constraints,
						 con->shared_cons_name);
	  con->index = shared_con->index;
	}
      else
	{
	  if (con->type == SM_CONSTRAINT_UNIQUE
	      || con->type == SM_CONSTRAINT_REVERSE_UNIQUE)
	    {
	      asc_desc = con->asc_desc;
	    }
	  else
	    {
	      asc_desc = NULL;
	    }

	  reverse = SM_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (con->type);

	  if (allocate_index (classop, class_, subclasses, con->attributes,
			      asc_desc, unique, reverse, con->name,
			      &con->index, NULL, NULL, -1, NULL))
	    {
	      return er_errid ();
	    }
	}
    }
  else
    {
      /* its inherited, go get the btid from the super class */
      if (au_fetch_class_force (con->attributes[0]->class_mop,
				&super_class, AU_FETCH_READ))
	{
	  return er_errid ();
	}

      super_con = classobj_find_class_constraint (super_class->constraints,
						  con->type, con->name);
      if (super_con != NULL)
	{
	  con->index = super_con->index;
	}
      else
	{
	  /* not supposed to happen, need a better error */
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_SM_INVALID_PROPERTY, 0);
	  return ER_SM_INVALID_PROPERTY;
	}
    }

  return NO_ERROR;
}

/*
 * allocate_foreign_key() - Allocate index for foreign key
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   con(in):constraint info
 *   recache_cls_cons(out): 
 */
static int
allocate_foreign_key (MOP classop, SM_CLASS * class_,
		      SM_CLASS_CONSTRAINT * con, bool * recache_cls_cons)
{
  SM_CLASS_CONSTRAINT *pk, *shared_con;
  MOP ref_clsop;
  SM_ATTRIBUTE *cache_attr;

  if (OID_ISNULL (&con->fk_info->ref_class_oid))
    {
      con->fk_info->ref_class_oid = *(ws_oid (classop));

      pk = classobj_find_cons_primary_key (class_->constraints);
      if (pk == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		  ER_FK_REF_CLASS_HAS_NOT_PK, 1, class_->header.name);
	  return ER_FK_REF_CLASS_HAS_NOT_PK;
	}
      con->fk_info->ref_class_pk_btid = pk->index;
    }

  if (con->fk_info->cache_attr && con->fk_info->cache_attr_id < 0)
    {
      cache_attr = classobj_find_attribute (class_, con->fk_info->cache_attr,
					    0);
      con->fk_info->cache_attr_id = cache_attr->id;
      cache_attr->is_fk_cache_attr = true;
    }

  if (con->shared_cons_name)
    {
      shared_con = classobj_find_cons_index (class_->constraints,
					     con->shared_cons_name);
      con->index = shared_con->index;

      if (con->fk_info->cache_attr_id > 0 &&
	  build_fk_obj_cache (classop, class_, con->attributes, con->asc_desc,
			      &(con->fk_info->ref_class_oid),
			      &(con->fk_info->
				ref_class_pk_btid),
			      con->fk_info->cache_attr_id,
			      (char *) con->fk_info->name) != NO_ERROR)
	{
	  return er_errid ();
	}
    }
  else
    {
      if (allocate_index (classop, class_, NULL, con->attributes, NULL, false,
			  false, con->name, &con->index,
			  &(con->fk_info->ref_class_oid),
			  &(con->fk_info->ref_class_pk_btid),
			  con->fk_info->cache_attr_id, con->fk_info->name))
	{
	  return er_errid ();
	}
    }

  con->fk_info->self_oid = *(ws_oid (classop));
  con->fk_info->self_btid = con->index;

  ref_clsop = ws_mop (&(con->fk_info->ref_class_oid), NULL);

  if (classop == ref_clsop)
    {
      if (classobj_put_foreign_key_ref (&(class_->properties),
					con->fk_info) != NO_ERROR)
	{
	  return er_errid ();
	}
      *recache_cls_cons = true;
    }
  else if (!classobj_is_exist_foreign_key_ref (ref_clsop, con->fk_info))
    {
      if (update_foreign_key_ref (ref_clsop, con->fk_info) != NO_ERROR)
	{
	  return er_errid ();
	}
    }

  return NO_ERROR;
}

/*
 * allocate_disk_structure_helper() - Helper for index allocation
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   con(in):constraint info
 *   subclasses(in): sub class list
 *   recache_cls_cons(out): 
 */
static int
allocate_disk_structure_helper (MOP classop, SM_CLASS * class_,
				SM_CLASS_CONSTRAINT * con,
				DB_OBJLIST * subclasses,
				bool * recache_cls_cons)
{
  int error = NO_ERROR;
  int reverse;

  if (BTID_IS_NULL (&con->index))
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type))
	{
	  error =
	    allocate_unique_constraint (classop, class_, con, subclasses);
	}
      else if (con->type == SM_CONSTRAINT_INDEX ||
	       con->type == SM_CONSTRAINT_REVERSE_INDEX)
	{
	  reverse = (con->type == SM_CONSTRAINT_INDEX) ? false : true;
	  error = allocate_index (classop, class_, NULL, con->attributes,
				  con->asc_desc, false, reverse, con->name,
				  &con->index, NULL, NULL, -1, NULL);
	}
      else if (con->type == SM_CONSTRAINT_FOREIGN_KEY)
	{
	  error = allocate_foreign_key (classop, class_, con,
					recache_cls_cons);
	}

      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  /* Whether we allocated a BTID or not, always write the contraint info
   * back out to the property list.  This is where the promotion of
   * attribute name references to ids references happens.
   */
  if (classobj_put_index_id (&(class_->properties), con->type,
			     con->name, con->attributes,
			     con->asc_desc,
			     &(con->index), con->fk_info, NULL) != NO_ERROR)
    {
      return er_errid ();
    }

  return NO_ERROR;
}

/*
 * allocate_disk_structures() - Allocate the necessary disk structures for
 *    a new or modified class. For constraints, be careful to recognize
 *    a place holder for a BTID that hasn't been allocated yet but whose
 *    definition was actually inherited from a super class. When we find these,
 *    go to the super class and use the BTID that will have by now been
 *    allocated in there rather than allocating a new one of our own.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   subclasses(in):
 */
static int
allocate_disk_structures (MOP classop, SM_CLASS * class_,
			  DB_OBJLIST * subclasses)
{
  SM_CLASS_CONSTRAINT *con;
  bool recache_cls_cons = false;

  /* Formerly, we mapped over the attributes looking for SM_ATTFLAG_UNIQUE,
   * now we map over the class constraint list looking for fully specified
   * unique properties.
   * Note that since its so very much easier to map over the constraint
   * structures than it is the property list, we go ahead and cache the
   * properties at this point.  If we have to allocate the BTID's, we're
   * careful to put them back on the property list where they belong.
   * Note that in order to convert named attribute references to ID references,
   * we always write the constraint info back to the property list, even
   * if we don't have to allocate a BTID.
   */
  if (classobj_cache_class_constraints (class_))
    {
      goto structure_error;
    }

  if (OID_ISTEMP (ws_oid (classop)))
    {
      locator_assign_permanent_oid (classop);
    }

  for (con = class_->constraints; con != NULL; con = con->next)
    {
      if (con->shared_cons_name == NULL && con->attributes[0] != NULL)
	{
	  if (allocate_disk_structure_helper (classop, class_, con,
					      subclasses,
					      &recache_cls_cons) != NO_ERROR)
	    {
	      goto structure_error;
	    }
	}
    }

  for (con = class_->constraints; con != NULL; con = con->next)
    {
      if (con->shared_cons_name != NULL && con->attributes[0] != NULL)
	{
	  if (allocate_disk_structure_helper (classop, class_, con,
					      subclasses,
					      &recache_cls_cons) != NO_ERROR)
	    {
	      goto structure_error;
	    }
	}
    }

  if (classobj_snapshot_representation (class_))
    {
      goto structure_error;
    }

  /* recache class constraint for foreign key */
  if (recache_cls_cons && classobj_cache_class_constraints (class_))
    {
      goto structure_error;
    }

  /* when we're done, make sure that each attribute's cache is also updated */
  if (!classobj_cache_constraints (class_))
    {
      goto structure_error;
    }

  if (locator_update_class (classop) == NULL)
    {
      goto structure_error;
    }

  if (locator_flush_class (classop) != NO_ERROR)
    {
      goto structure_error;
    }

  if (sm_update_statistics (classop))
    {
      goto structure_error;
    }

  return NO_ERROR;

structure_error:
  /* the workspace has already been damaged by this point, the caller will
   * have to recognize the error and abort the transaction.
   */
  return er_errid ();
}

/*
 * drop_foreign_key_ref()
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in):
 *   flat_cons(in):
 *   cons(in):
 */

static int
drop_foreign_key_ref (MOP classop,
		      SM_CLASS_CONSTRAINT * flat_cons,
		      SM_CLASS_CONSTRAINT * cons)
{
  int err = NO_ERROR;
  MOP ref_clsop;
  SM_TEMPLATE *refcls_template;
  int save;
  SM_CLASS_CONSTRAINT *con;
  SM_FOREIGN_KEY_INFO *fk;

  AU_DISABLE (save);

  ref_clsop = ws_mop (&cons->fk_info->ref_class_oid, NULL);

  if (classop == ref_clsop)
    {
      for (con = flat_cons; con != NULL; con = con->next)
	{
	  if (con->type == SM_CONSTRAINT_PRIMARY_KEY)
	    {
	      for (fk = con->fk_info; fk != NULL; fk = fk->next)
		{
		  if (BTID_IS_EQUAL (&fk->self_btid, &cons->index))
		    {
		      fk->is_dropped = true;
		      break;
		    }
		}
	      break;
	    }
	}
    }
  else
    {
      if ((refcls_template = dbt_edit_class (ref_clsop)) == NULL)
	{
	  AU_ENABLE (save);
	  return er_errid ();
	}

      if ((err =
	   classobj_drop_foreign_key_ref (&(refcls_template->properties),
					  &cons->index)) != NO_ERROR)
	goto error;

      if ((ref_clsop = dbt_finish_class (refcls_template)) == NULL)
	{
	  err = er_errid ();
	  goto error;
	}
    }

  AU_ENABLE (save);
  return NO_ERROR;

error:
  dbt_abort_class (refcls_template);
  AU_ENABLE (save);

  return err;
}

/*
 * transfer_disk_structures() - Work function for install_new_representation.
 *    Here we look for any attributes that are being dropped from the
 *    class and remove their associated disk structures (if any).
 *    This also moves the index ids from the existing attribute structures
 *    into the new ones.  It must do this because copying the index
 *    field is not part of the usual copying done by the cl_ functions.
 *    This is because indexes are not inherited and we
 *    must be very careful that they stay only with the class on which
 *    they were defined.
 *    This can also be called for sm_delete_class with a template of
 *    NULL in which case we just free all disk structures we find.
 *    We DO NOT allocate new index structures here, see
 *    allocate_disk_structures to see how that is done.
 *    This is where BTID's for unique & indexes get inherited.
 *    Think about moving the functionality of allocate_disk_structures
 *    in here, it should be possible to do that and would simplify things.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   flat(out): new flattened template
 */

/* NOTE:
   Kludge for disk structures associated with inherited attributes.
   The test for retention of the index and unique has tables now not
   only checks to see if the flag is still active but also checks to
   see if the attribute was inherited.  If the attribute was inherited,
   the flag will always be off since add_attribute_internal builds
   a fresh list from the super classes.  smt_add_index won't recognize
   inherited attributes (they aren't in the unflattened list) so there
   is no way really using templates to maintain indexes on inherited attributes.
   Since we can't have changed the state of an inherited attribute index
   using a template, if the old representation had an index on the inherited
   attribute, the new one must also so use it.

   This is not a problem now because all index addition/removal is done
   through sm_add_index which doesn't use a template.  This kind of
   "override" information on inherited attributes needs to be formalized
   and included in the template so it can be dealt with in a cleaner way.
   Currently the only information that is maintained in this way is the
   index and the UNIQUE integrity constraint.

   A simpler solution to this would be to enforce the inheritance of
   indexes and constraints.  I think this makes more sense anyway since
   changing constraints is sort of a violation of the "is-a" relationship.
   Not inheriting indexes makes indexes useless for queries on
   class hierarchies (ALL queries).
   */

/* NOTE2: Since this is called after build_ordered_attributes, we have
   to use the "instance_attributes" list in the template rather than
   the "attributes" list.  Consider moving this up so we don't have
   to make this distinction yet.
   */

/* NOTE3: The previous two comments are largely obsolete.  When the
 * introduction of the SM_CLASS_CONSTRAINT concept, we now have
 * a place to hold the constraint & possibly index information apart
 * from the attributes themselves.
 */

static int
transfer_disk_structures (MOP classop, SM_CLASS * class_, SM_TEMPLATE * flat)
{
  int error = NO_ERROR;
  SM_CLASS_CONSTRAINT *flat_constraints, *con, *new_con, *prev, *next;

  /* Get the cached constraint info for the flattened template.
   * Sigh, convert the template property list to a transient constraint
   * cache so we have a prayer of dealing with it.
   */
  if (flat == NULL)
    flat_constraints = NULL;
  else
    error = classobj_make_class_constraints (flat->properties,
					     flat->instance_attributes,
					     &flat_constraints);

  /* loop over each old constraint */
  for (con = class_->constraints;
       ((con != NULL) && (error == NO_ERROR)); con = con->next)
    {
      if (SM_IS_CONSTRAINT_INDEX_FAMILY (con->type))
	{
	  new_con =
	    classobj_find_class_constraint (flat_constraints, con->type,
					    con->name);
	  if (new_con == NULL)
	    {
	      /* Constraint does not exist in the template */
	      if (con->attributes[0] != NULL)
		{
		  /* destroy the old index but only if we're the owner of it ! */
		  if (con->attributes[0]->class_mop == classop)
		    {
		      if (con->type == SM_CONSTRAINT_FOREIGN_KEY)
			error =
			  drop_foreign_key_ref (classop, flat_constraints,
						con);

		      deallocate_index (class_->constraints, &con->index);
		      BTID_SET_NULL (&con->index);
		    }
		  /* If we're not the owner of it, then only remove this class
		     from the B-tree (the B-tree will still exist) */
		  else
		    {
		      if (con->type == SM_CONSTRAINT_FOREIGN_KEY)
			error =
			  drop_foreign_key_ref (classop, flat_constraints,
						con);

		      rem_class_from_index (WS_OID (classop), &con->index,
					    &class_->header.heap);
		      BTID_SET_NULL (&con->index);
		    }
		}
	    }
	  else if (!BTID_IS_EQUAL (&con->index, &new_con->index))
	    {
	      if (BTID_IS_NULL (&(new_con->index)))
		{
		  /* Template index isn't set, transfer the old one
		   * Can this happen, it should have been transfered by now.
		   */
		  new_con->index = con->index;
		}
	      else
		{
		  /* The index in the new template is not the same, I'm not entirely
		   * sure what this means or how we can get here.
		   * Possibly if we drop the unique but add it again with the same
		   * name but over different attributes.
		   */
		  if (con->attributes[0] != NULL &&
		      con->attributes[0]->class_mop == classop)
		    {
		      if (con->type == SM_CONSTRAINT_FOREIGN_KEY)
			error =
			  drop_foreign_key_ref (classop, flat_constraints,
						con);

		      deallocate_index (class_->constraints, &con->index);
		      BTID_SET_NULL (&con->index);
		    }
		}
	    }
	}
    }

  /* Filter out any constraints that don't have associated attriubtes,
   * this is normally only the case for old constraints whose attributes
   * have been deleted.
   */
  for (con = flat_constraints, prev = NULL, next = NULL;
       con != NULL; con = next)
    {
      next = con->next;
      if (con->attributes[0] != NULL)
	prev = con;
      else
	{
	  if (prev == NULL)
	    flat_constraints = con->next;
	  else
	    prev->next = con->next;

	  con->next = NULL;
	  if (!BTID_IS_NULL (&con->index))
	    {
	      if (con->type == SM_CONSTRAINT_FOREIGN_KEY)
		error = drop_foreign_key_ref (classop, flat_constraints, con);

	      deallocate_index (class_->constraints, &con->index);
	      BTID_SET_NULL (&con->index);
	    }
	  classobj_free_class_constraints (con);
	}
    }

  /* Loop over each new constraint, if we find any without indexes,
   * this must be inherited, go get the real index from the super class.
   * If this is local constraint without an allocated index,
   * we could allocate one here rather than maintaining
   * separate logic in allocate_disk_structures!  Think about this.
   *
   * UNIQUE constraints are inheritable but INDEX'es are not.
   */

  for (con = flat_constraints;
       ((con != NULL) && (error == NO_ERROR)); con = con->next)
    {
      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type) ||
	  con->type == SM_CONSTRAINT_FOREIGN_KEY)
	{
	  if (BTID_IS_NULL (&(con->index)))
	    {
	      SM_ATTRIBUTE *att;
	      SM_CLASS *super_class;
	      SM_CLASS_CONSTRAINT *super_con;

	      att = con->attributes[0];
	      if (att != NULL && att->class_mop != classop)
		{
		  /* its inherited, go get the btid from the super class */
		  if ((error =
		       au_fetch_class_force (att->class_mop, &super_class,
					     AU_FETCH_READ)) == NO_ERROR)
		    {
		      super_con =
			classobj_find_class_constraint (super_class->
							constraints,
							con->type, con->name);
		      if (super_con == NULL)
			{
			  /* not supposed to happen, need a better error */
			  error = ER_SM_INVALID_PROPERTY;
			  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
			}
		      else
			{
			  /* copy the index */
			  con->index = super_con->index;
			}
		    }
		}
	    }
	}
    }

  /* rebuild the unique property list entry based on the modified
   * constraint list
   */
  if (flat != NULL)
    {
      classobj_drop_prop (flat->properties, SM_PROPERTY_UNIQUE);
      classobj_drop_prop (flat->properties, SM_PROPERTY_INDEX);
      classobj_drop_prop (flat->properties, SM_PROPERTY_REVERSE_UNIQUE);
      classobj_drop_prop (flat->properties, SM_PROPERTY_REVERSE_INDEX);
      classobj_drop_prop (flat->properties, SM_PROPERTY_PRIMARY_KEY);
      classobj_drop_prop (flat->properties, SM_PROPERTY_FOREIGN_KEY);

      for (con = flat_constraints;
	   ((con != NULL) && (error == NO_ERROR)); con = con->next)
	{
	  if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type) ||
	      con->type == SM_CONSTRAINT_FOREIGN_KEY)
	    {
	      if (classobj_put_index_id (&(flat->properties), con->type,
					 con->name, con->attributes,
					 con->asc_desc, &(con->index),
					 con->fk_info, con->shared_cons_name)
		  != NO_ERROR)
		{
		  error = ER_SM_INVALID_PROPERTY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		}
	    }
	  else if (con->type == SM_CONSTRAINT_INDEX ||
		   con->type == SM_CONSTRAINT_REVERSE_INDEX)
	    {
	      if (classobj_put_index_id (&(flat->properties), con->type,
					 con->name, con->attributes,
					 con->asc_desc,
					 &(con->index), NULL, NULL)
		  != NO_ERROR)
		{
		  error = ER_SM_INVALID_PROPERTY;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		}
	    }
	}
    }

  /* This was used only for convenience here, be sure to free it.
   * Eventually, we'll just maintain these directly on the template.
   */
  classobj_free_class_constraints (flat_constraints);
  return error;
}

/*
 * save_previous_value() - Transfer the value in the old attribute definition
 *    to the new attribute definition.
 *    Work function for check_inherited_attributes.
 *   return: none
 *   old(in): old attribute definition
 *   new(out): new attribute definition
 */

static void
save_previous_value (SM_ATTRIBUTE * old, SM_ATTRIBUTE * new_)
{
  pr_clear_value (&new_->value);
  pr_clone_value (&old->value, &new_->value);

  pr_clear_value (&new_->original_value);

  /* Transfer the current value to the copied definition.
   * Note that older code copied old->value into new->original_value, I don't
   * think thats, right, I changed it to copy the old->original_value
   */
  pr_clone_value (&old->original_value, &new_->original_value);
}

/*
 * check_inherited_attributes() - We maintain a seperate copy of the values
 *    for inherited class attributes and shared attributes for each class.
 *    That is, the value of a class/shared attribute is not inherited, only the
 *    definition.  When we have finished re-flattening a class, we must
 *    remember to use the value of the class/shared attribute that is
 *    currently stored in the class NOT the value that came from the
 *    inherited definitions.
 *   return: none
 *   classmop(in): class object
 *   class(in): class structure
 *   flat(in): flattened template
 */

static void
check_inherited_attributes (MOP classmop, SM_CLASS * class_,
			    SM_TEMPLATE * flat)
{
  SM_ATTRIBUTE *old, *att, *new_attr;

  if (flat != NULL)
    {
      FOR_ATTRIBUTES (class_->shared, old)
      {
	new_attr = NULL;
	for (att = flat->attributes; att != NULL && new_attr == NULL;
	     att = (SM_ATTRIBUTE *) att->header.next)
	  {
	    if (att->header.name_space == ID_SHARED_ATTRIBUTE
		&& SM_COMPARE_NAMES (att->header.name, old->header.name) == 0
		&& att->class_mop != classmop
		&& att->class_mop == old->class_mop)
	      {
		/* inherited attribute */
		new_attr = att;
	      }
	  }
	if (new_attr != NULL)
	  save_previous_value (old, new_attr);
      }
      FOR_ATTRIBUTES (class_->class_attributes, old)
      {
	new_attr = NULL;
	for (att = flat->class_attributes; att != NULL && new_attr == NULL;
	     att = (SM_ATTRIBUTE *) att->header.next)
	  {
	    if (SM_COMPARE_NAMES (att->header.name, old->header.name) == 0
		&& att->class_mop != classmop
		&& att->class_mop == old->class_mop)
	      {
		/* inherited attribute */
		new_attr = att;
	      }
	  }
	if (new_attr != NULL)
	  save_previous_value (old, new_attr);
      }
    }
}

/*
 * invalidate_unused_triggers() - This will invalidate any triggers that are
 *    associated with  attributes that have been deleted.  This is performed
 *    by the function tr_delete_schema_cache which frees the schema cache
 *    but also marks the triggers contained in the cache as invalid.
 *    Note that since a trigger can be referenced by caches throughout
 *    the hierarchy, we only invalidate the trigger if the attribute
 *    being removed was defined directly on this class and not inherited.
 *    We can't invalidate triggers on inherited attributes because the
 *    attribute may still exist in the super class.  tr_delete_schema_cache
 *    must be passed in the MOP of the current class, it will only
 *    invalidate triggers whose target class is the same as this
 *    class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   class_mop(in): class object
 *   class(in): class structure
 *   flat(in): flattened template
 */

static void
invalidate_unused_triggers (MOP class_mop,
			    SM_CLASS * class_, SM_TEMPLATE * flat)
{
  SM_ATTRIBUTE *old, *new_;

  /* instance level attributes */
  for (old = class_->ordered_attributes; old != NULL; old = old->order_link)
    {
      new_ = NULL;
      if (flat != NULL)
	{
	  for (new_ = flat->instance_attributes;
	       new_ != NULL && new_->id != old->id;
	       new_ = (SM_ATTRIBUTE *) new_->header.next);
	}
      if (new_ == NULL)
	{
	  if (old->triggers != NULL)
	    {
	      tr_delete_schema_cache ((TR_SCHEMA_CACHE *) old->triggers,
				      class_mop);
	      old->triggers = NULL;
	    }
	}
    }

  /* class attributes */
  FOR_ATTRIBUTES (class_->class_attributes, old)
  {
    new_ = NULL;
    if (flat != NULL)
      {
	for (new_ = flat->class_attributes;
	     new_ != NULL && new_->id != old->id;
	     new_ = (SM_ATTRIBUTE *) new_->header.next);
      }
    if (new_ == NULL)
      {
	if (old->triggers != NULL)
	  {
	    tr_delete_schema_cache ((TR_SCHEMA_CACHE *) old->triggers,
				    class_mop);
	    old->triggers = NULL;
	  }
      }
  }
}

/*
 * install_new_representation() - Final installation of a class template.
 *    It is necessary to guarantee that this is an atomic operation and
 *    the workspace will not change while this executes.
 *    Garbage collection should be disabled while this happens
 *    although we keep MOP cached in structures everywhere so it won't
 *    make a difference.
 *    This is essentially the "commit" operation of a schema modification,
 *    be VERY sure you know what you're doing if you change this
 *    code.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   class(in): class structure
 *   flat(out): flattened template
 */

static int
install_new_representation (MOP classop, SM_CLASS * class_,
			    SM_TEMPLATE * flat)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *a;
  SM_METHOD *m;
  int needrep, newrep;

  /* now that we're ready, make sure attribute/methods are stamped with the
     proper class mop */
  fixup_component_classes (classop, flat);

  /* go through and replace kludged "self referencing" domain with a proper
     domain containing the new class MOP */
  fixup_self_reference_domains (classop, flat);

  /* check for inherited class and shared attributes and make sure we maintain
     the current value */
  check_inherited_attributes (classop, class_, flat);

  /* assign attribute ids and check for structural representation changes */
  needrep = build_storage_order (class_, flat);

  /* assign identifiers for the shared and class attributes */
  FOR_ATTRIBUTES (flat->shared_attributes, a)
    assign_attribute_id (class_, a, 0);

  FOR_ATTRIBUTES (flat->class_attributes, a)
    assign_attribute_id (class_, a, 1);

  /* methods don't currently have ids stored persistently but go ahead and
     assign them anyway in the hopes that someday they'll be stored */
  FOR_METHODS (flat->methods, m) assign_method_id (class_, m, 0);
  FOR_METHODS (flat->class_methods, m) assign_method_id (class_, m, 1);

  /* if the representation changed but there have been no objects
     created with the previous representation, don't create a new one,
     otherwise, flush all resident instances */
  newrep = 0;
  if (needrep)
    {

      /* NEW: 12/5/92, check for error on each of the locator functions, an error
         can happen if we run out of space during flushing. */

      if (!classop->no_objects)
	{
	  switch (class_->class_type)
	    {
	    case SM_CLASS_CT:
	      if (locator_flush_all_instances (classop, true) != NO_ERROR)
		return (er_errid ());
	      break;

	    case SM_VCLASS_CT:
	    case SM_LDBVCLASS_CT:
	      if (vid_flush_all_instances (classop, true) != NO_ERROR)
		return (er_errid ());
	      break;

	    default:
	      break;
	    }

	  /* note that the previous operation will flush the current class representation
	     along with the instances and clear the dirty bit, this is unnecessary if
	     the class was only marked dirty in preparation for the new representation.
	     Because the dirty bit is clear however, we must turn it back on after
	     the new representation is installed so it will be properly flushed,
	     the next time a transaction commits or locator_flush_all_instances is called
	   */
	  if (locator_update_class (classop) == NULL)
	    return (er_errid ());

	  /* !!! I've seen some cases where objects are left cached while this
	     flag is on which is illegal.  Not sure how this happens but leave
	     this trap so we can track it down.  Shouldn't be necessary */
	  if (ws_class_has_cached_objects (classop))
	    {
	      ERROR (error, ER_SM_CORRUPTED);
	      return (error);
	    }

	  newrep = 1;

	  /* Set the no_objects flag so we know that if no object dependencies are
	     introduced on this representation, we don't have to generate another
	     one the next time the class is updated. */
	  /* this used to be outside, think about why */
	  WS_SET_NO_OBJECTS (classop);

	}
      else
	{
	  /* if we don't have any new instances, we still must increment the repid in
	     the class (but we don't have to flush resident instances) - what can
	     happen is if the last representation got flushed to the catalog,
	     when this new one gets flushed, it will have the same representation
	     id as the one currently in the catalog - the catalog manager assumes
	     they are the same and doesn't update the catalog.  Since we want
	     to avoid catalog updates when non-destructive changes are made to
	     the class this check needs to be maintained.  So, we now always
	     increment the representation id but we don't try to flush anything,
	     this should at least keep the class from being flushed after every
	     change which is what we want.
	   */
	  newrep = 1;
	}
    }

  /* Check for any attribute index removal, not sure what we can do if there
     is an error here, probably should abort the entire class edit.
     NOTE: Formerly, this call was made prior to the flush operation above.
     This caused problems for indexed attributes that had been updated
     during this transaction.  Because transfer_disk_structures, modifies the
     original class by moving the index pointers into the template, the
     flush operation didn't think the class had any indexes and the index
     wasn't updated for the new attribute values.  Make sure that all destructive
     operations on the class structure are made AFTER the class and the instances
     have been flushed.
   */
  error = transfer_disk_structures (classop, class_, flat);

  /* Delete the trigger caches associated with attributes that
     are no longer part of the class.  This will also mark the
     triggers as invalid since their associated attribute has gone
     away. */
  invalidate_unused_triggers (classop, class_, flat);

  /* clear any attribute or method descriptor caches that reference this
     class. */
  sm_reset_descriptors (classop);

  /* install the template, the dirty bit must be on at this point */
  if ((error = classobj_install_template (class_, flat, newrep)) != NO_ERROR)
    return (error);

  /* make absolutely sure this gets marked dirty after the insllation,
     this is usually redundant but the class could get flushed
     during memory panics so we always must make sure it gets flushed again */
  if (locator_update_class (classop) == NULL)
    return er_errid ();

  /* If the representation was incremented, invalidate any existing
     statistics cache.  The next time statistics are requested, we'll
     go to the server and get them based on the new catalog information.
     This probably isn't necessary in all cases but lets be safe and
     waste it unconditionally.
   */
  if (newrep && class_->stats != NULL)
    {
      stats_free_statistics (class_->stats);
      class_->stats = NULL;
    }

  /* formerly had classop->no_objects = 1 here, why ? */

  /* now that we don't always load methods immediately after editing,
     must make sure that the methods_loaded flag is cleared so they
     will be loaded the next time a mesage is sent */
  class_->methods_loaded = 0;

  return (error);
}

/* CLASS DEPENDENCY LOCKING */

/*
 * lock_supers() - Get write locks on any super classes that will need to
 *    have their subclass list updated because of changes in the inheritance
 *    of the class being edited.
 *    As a side effect, this constructs the "oldsupers" and "newsupers" list
 *    by comparing the new super class list with the old definition.
 *    These lists will be used later by update_supers.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   current(in): list of current super classes
 *   oldlist(out): returned list of supers being dropped
 *   newlist(out): returned list of supers being added
 */

static int
lock_supers (SM_TEMPLATE * def, DB_OBJLIST * current,
	     DB_OBJLIST ** oldlist, DB_OBJLIST ** newlist)
{
  int error = NO_ERROR;
  DB_OBJLIST *super;
  SM_CLASS *class_;

  /* first check for removals */
  for (super = current;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {
      if (!ml_find (def->inheritance, super->op))
	{
	  if ((error =
	       au_fetch_class (super->op, &class_, AU_FETCH_WRITE,
			       AU_SELECT)) == NO_ERROR)
	    error = ml_append (oldlist, super->op, NULL);
	}
    }
  /* now check for new supers */
  for (super = def->inheritance;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {
      if (!ml_find (current, super->op))
	{
	  if ((error =
	       au_fetch_class (super->op, &class_, AU_FETCH_WRITE,
			       AU_SELECT)) == NO_ERROR)
	    error = ml_append (newlist, super->op, NULL);
	}
    }
  return (error);
}

/*
 * update_supers() - This updates the subclass list on all super classes that
 *    were affected by a class edit.  It uses the lists built by the
 *    lock_supers function.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class being edited
 *   oldsupers(in): supers no longer connected
 *   newsupers(out): supers being added
 */

static int
update_supers (MOP classop, DB_OBJLIST * oldsupers, DB_OBJLIST * newsupers)
{
  int error = NO_ERROR;
  DB_OBJLIST *super;
  SM_CLASS *class_;

  /* removals */
  for (super = oldsupers;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {
      if ((error = au_fetch_class_force (super->op, &class_, AU_FETCH_UPDATE))
	  == NO_ERROR)
	ml_remove (&class_->users, classop);
    }
  /* additions */
  for (super = newsupers;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {
      if ((error = au_fetch_class_force (super->op, &class_, AU_FETCH_UPDATE))
	  == NO_ERROR)
	error = ml_append (&class_->users, classop, NULL);
    }
  return (error);
}

/*
 * lock_supers_drop() - Lock the super classes in preparation for a drop
 *    operation. All supers in the list will have to be locked.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   supers(in): list of super classes
 */

static int
lock_supers_drop (DB_OBJLIST * supers)
{
  int error = NO_ERROR;
  DB_OBJLIST *super;
  SM_CLASS *class_;

  for (super = supers;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {
      error = au_fetch_class (super->op, &class_, AU_FETCH_WRITE, AU_SELECT);
    }
  return (error);
}

/*
 * update_supers_drop() - This updates the subclass list on super classes
 *    after a class has been deleted.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classmop(in): class object being dropped
 *   supers(in): super class list to update
 */

static int
update_supers_drop (MOP classop, DB_OBJLIST * supers)
{
  int error = NO_ERROR;
  DB_OBJLIST *super;
  SM_CLASS *class_;

  for (super = supers;
       ((super != NULL) && (error == NO_ERROR)); super = super->next)
    {
      if ((error = au_fetch_class_force (super->op, &class_, AU_FETCH_UPDATE))
	  == NO_ERROR)
	ml_remove (&class_->users, classop);
    }
  return (error);
}

/*
 * lock_subclasses_internal()
 * lock_subclasses() - Recursively get write locks on all subclasses that
 *    inherit directly or indirectly from the class being edited. Returns zero
 *    if all classes were successfully locked.
 *    NOTE: The order of the list produced here is very important.
 *    We must make sure that the classes are updated BEFORE any other
 *    classes that use them.
 *    As a side effect, a flattened list of all effected subclasses
 *    is build for later use by update_users.
 *    We're also checking for cycles in the class hierarchy here.
 *    If any of the encountered subclasses are in the immediate super class
 *    list of the class being edited, we must abort.  This is the reason
 *    we pass in the immediate super class list.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   def(in): schema template
 *   op(in): MOP of class being edited
 *   newsupers(in): new super class list
 *   newsubs(out): retured list of flattened subclasses
 */

static int
lock_subclasses_internal (SM_TEMPLATE * def, MOP op,
			  DB_OBJLIST * newsupers, DB_OBJLIST ** newsubs)
{
  int error = NO_ERROR;
  DB_OBJLIST *l, *found, *new_, *u;
  SM_CLASS *class_;

  if (ml_find (newsupers, op))
    ERROR2 (error, ER_SM_CYCLE_DETECTED, sm_class_name (op), def->name);

  else
    {
      error = au_fetch_class_force (op, &class_, AU_FETCH_WRITE);
      if (error != NO_ERROR)
	{
	  if (WS_ISMARK_DELETED (op))
	    /* in this case, just ignore the error */
	    error = NO_ERROR;
	}
      else
	{
	  /* dive to the bottom */
	  for (u = class_->users;
	       ((u != NULL) && (error == NO_ERROR)); u = u->next)
	    error = lock_subclasses_internal (def, u->op, newsupers, newsubs);

	  if (error == NO_ERROR)
	    {
	      /* push the class on the list */
	      for (l = *newsubs, found = NULL; l != NULL && found == NULL;
		   l = l->next)
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
		  new_->next = *newsubs;
		  *newsubs = new_;
		}
	    }
	}
    }
  return (error);
}

static int
lock_subclasses (SM_TEMPLATE * def, DB_OBJLIST * newsupers,
		 DB_OBJLIST * cursubs, DB_OBJLIST ** newsubs)
{
  int error = NO_ERROR;
  DB_OBJLIST *sub;

  for (sub = cursubs; ((sub != NULL) && (error == NO_ERROR)); sub = sub->next)
    error = lock_subclasses_internal (def, sub->op, newsupers, newsubs);

  return (error);
}

/*
 * check_catalog_space() - Checks to see if the catalog manager is able to
 *    handle another representation for this class.  There is a fixed limit
 *    on the number of representations that can be stored in the catalog
 *    for each class.  If this limit is reached, the schema operation
 *    cannot be performed until the database is compacted.
 *    Note that this needs only be called when a schema operation
 *    will actually result in the generation of a new catalog entry.
 *    Since this won't be a problem very often, its also ok just to check
 *    it up front even if the operation may not result in the generation
 *    of a new representation.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classmop(in): class pointer
 *   class(in): class structure
 */

static int
check_catalog_space (MOP classmop, SM_CLASS * class_)
{
  int error = NO_ERROR;
  int status, can_accept;

  /* if the OID is temporary, then we haven't flushed the class yet
     and it isn't necessary to check since there will be no
     existing entries in the catalog */

  if (!OID_ISTEMP (WS_OID (classmop)))
    {

      /* if the oid is permanent, we still may not have flushed the class
         because the OID could have been assigned during the transformation
         of another object that referenced this class.
         In this case, the catalog manager will return ER_CT_UNKNOWN_CLASSID
         because it will have no entries for this class oid.
       */

      status =
	catalog_is_acceptable_new_representation (WS_OID (classmop),
						  &class_->header.heap,
						  &can_accept);

      if (status != NO_ERROR)
	{
	  error = er_errid ();
	  /* ignore if if the class hasn't been flushed yet */
	  if (error == ER_CT_UNKNOWN_CLASSID)
	    /* if dirty bit isn't on in this MOP, its probably an internal error */
	    error = NO_ERROR;
	}
      else if (!can_accept)
	ERROR1 (error, ER_SM_CATALOG_SPACE, class_->header.name);
    }

  return (error);
}

/*
 * flatten_subclasses() - Construct a flattened template for every subclass
 *    affected by a class edit (or deletion).  If flattening fails for any
 *    of the subclasses, the entire class edit must be aborted.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   subclasses(in): list of subclasses needing flattening
 *   deleted_class(in): MOP of delete_class (if any, can be NULL)
 */

static int
flatten_subclasses (DB_OBJLIST * subclasses, MOP deleted_class)
{
  int error = NO_ERROR;
  DB_OBJLIST *sub;
  SM_CLASS *class_;
  SM_TEMPLATE *utemplate, *flat;

  for (sub = subclasses;
       ((sub != NULL) && (error == NO_ERROR)); sub = sub->next)
    {
      if ((error =
	   au_fetch_class_force (sub->op, &class_,
				 AU_FETCH_UPDATE)) == NO_ERROR)
	{

	  /* make sure the catalog manager can handle another modification */
	  if ((error = check_catalog_space (sub->op, class_)) == NO_ERROR)
	    {

	      /* make sure the run-time stuff is cached before editing, this
	         is particularly important for the method file source class kludge */
	      if ((error = sm_clean_class (sub->op, class_)) == NO_ERROR)
		{

		  /* create a template */
		  if ((utemplate =
		       classobj_make_template (class_->header.name, sub->op,
					       class_)) == NULL)
		    error = er_errid ();
		  else
		    {
		      /* reflatten it without any local changes (will inherit changes) */
		      error =
			flatten_template (utemplate, deleted_class, &flat, 1);

		      if (error == NO_ERROR)
			class_->new_ = flat;

		      /* free the definition template */
		      classobj_free_template (utemplate);
		    }
		}
	    }
	}
    }
  return (error);
}

/*
 * abort_subclasses() - If subclass flattening failed for some reason, must go
 *    through the list and free the temporary templates for those subclasses
 *    that were sucessfully flattened.
 *   return: none
 *   subclasses(in): subclass list
 */

static void
abort_subclasses (DB_OBJLIST * subclasses)
{
  DB_OBJLIST *sub;
  SM_CLASS *class_;

  /* don't stop the loop if we get fetch errors, we're just trying
   * to clean up the templates that are attached to the classes here.
   */
  for (sub = subclasses; sub != NULL; sub = sub->next)
    {
      if (au_fetch_class_force (sub->op, &class_, AU_FETCH_WRITE) == NO_ERROR)
	{
	  if (class_->new_ != NULL)
	    {
	      classobj_free_template (class_->new_);
	      class_->new_ = NULL;
	    }
	}
    }
}

/*
 * update_subclasses() - At this point, all subclasses have been successfully
 *    flattened and it is ok to install new representations for each.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   subclasses(in): list of subclasses
 */

static int
update_subclasses (DB_OBJLIST * subclasses)
{
  int error = NO_ERROR;
  DB_OBJLIST *sub;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *con;
  SM_ATTRIBUTE **atts;
  bool found_inherited_index;

  for (sub = subclasses; sub != NULL && error == NO_ERROR; sub = sub->next)
    {
      if (au_fetch_class_force (sub->op, &class_, AU_FETCH_UPDATE) ==
	  NO_ERROR)
	{
	  if (class_->new_ == NULL)
	    {
	      ERROR (error, ER_SM_CORRUPTED);
	    }
	  else
	    {
	      error = install_new_representation (sub->op, class_,
						  class_->new_);
	      if (error == NO_ERROR)
		{
		  /*
		   * currently, install_new_representation, allocate_disk_structures
		   * both increment repr_id.
		   *   NEED MORE CONSIDERATION
		   *   someday later, consider the following:
		   *   modify install_new_representation and
		   *   remove allocated_disk_structures
		   */
		  found_inherited_index = false;
		  for (con = class_->constraints; con != NULL;
		       con = con->next)
		    {
		      if (SM_IS_CONSTRAINT_UNIQUE_FAMILY (con->type) ||
			  con->type == SM_CONSTRAINT_FOREIGN_KEY)
			{
			  atts = con->attributes;
			  if (atts[0] != NULL)
			    {
			      if (atts[0]->class_mop != sub->op)
				{
				  /* there is inherited B+tree index */
				  found_inherited_index = true;
				  break;
				}
			    }
			}
		    }

		  if (found_inherited_index == true)
		    {
		      error =
			allocate_disk_structures (sub->op, class_, NULL);
		      if (error != NO_ERROR)
			{
			  return error;
			}
		    }
		  classobj_free_template (class_->new_);
		  class_->new_ = NULL;
		}
	    }
	}
    }
  return (error);
}

/*
 * verify_object_id() - Verify the object_id for the class.  If none has been
 *              set, indicate that all attributes form the object_id
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema templat
 */

static int
verify_object_id (SM_TEMPLATE * template_)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  int no_keys;
  DB_VALUE val;

  if ((template_) && (template_->class_type == SM_LDBVCLASS_CT))
    {
      no_keys = 0;
      FOR_ATTRIBUTES (template_->attributes, att)
      {
	if (vid_att_in_obj_id (att))
	  ++no_keys;
      }

      if (no_keys > 0)
	return NO_ERROR;

      if (classobj_get_prop
	  (template_->properties, SM_PROPERTY_LDB_INTRINSIC_OID, &val) > 0)
	{
	  if (DB_GET_INTEGER (&val))
	    {
	      return NO_ERROR;
	    }
	}

      no_keys = 0;
      FOR_ATTRIBUTES (template_->attributes, att)
      {
	if ((error = vid_set_att_obj_id (template_->name, att, no_keys))
	    != NO_ERROR)
	  return error;
	++no_keys;
      }
    }
  return NO_ERROR;

}


/*
 * lockhint_subclasses() - This is called early during the processing of
 *    sm_update_class. It will use the new subclass lattice locking function
 *    to try to get all the locks we need before we proceed.  This will
 *    be better for deadlock avoidance.
 *    This is done as a "hint" only, if we don't lock everything,
 *    we'll hit them again later and suspend.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   temp(in):
 *   class(in): class structure
 */

static int
lockhint_subclasses (SM_TEMPLATE * temp, SM_CLASS * class_)
{
  int error = NO_ERROR;
  const char *names[1];
  LOCK locks[1];
  int subs[1];

  if (class_ != NULL)
    {
      names[0] = class_->header.name;
      locks[0] = locator_fetch_mode_to_lock (DB_FETCH_WRITE, LC_CLASS);
      subs[0] = 1;
      if (locator_lockhint_classes (1, names, locks, subs, 1) ==
	  LC_CLASSNAME_ERROR)
	error = er_errid ();
    }
  else if (temp != NULL)
    {
      names[0] = temp->name;
      locks[0] = locator_fetch_mode_to_lock (DB_FETCH_WRITE, LC_CLASS);
      subs[0] = 1;
      if (locator_lockhint_classes (1, names, locks, subs, 1) ==
	  LC_CLASSNAME_ERROR)
	error = er_errid ();
    }
  return error;
}

/*
 * update_class() - Apply a schema template for a new or existing class.
 *    If there is an error in the local class or any affected subclass
 *    because of a change in the template, none of the changes in the
 *    template will be applied.
 *    Even if there were  no errors detected during the building of the
 *    template, there still may be some outstanding errors detected
 *    during actual flattening that will cause application of the template
 *    to fail.
 *    Locking affected superclasses and subclasses has also been defered
 *    till now so if locks cannot be obtained, the template cannot be
 *    applied.
 *    If the returned error status is zero, the template application
 *    was successful and the template was freed and can no longer be used.
 *    If the returned error status indicates a problem locking an affected
 *    object, you either abort the template or wait and try again later.
 *    If there is another error in the template, you can either abort
 *    the template or alter the template and try again.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   classmop(in): MOP of existing class (NULL if new class)
 *   auto_res(in): non-zero to enable auto-resolution of conflicts
 *   verify_oid(in): whether to verify object id
 */

/* NOTE: There were some problems when the transaction was unilaterally
 * aborted in the middle of a schema change operation.  What happens is
 * that during flattening, each class structure is given a pointer to
 * the flattened template.  If the transaction is aborted, all of the
 * dirty objects in the workspace would be flushed.  classobj_free_class()
 * would always free an associated template if one was present.  When
 * that happened, we would get back from some function, find and error,
 * but not realize that our template had been freed out from under us.
 *
 * The simplest solution to this problem is to prevent classobj_free_class()
 * from freeing templates.  This is ok in the normal case but in the
 * event of a unilateral abort, we may end up with some memory leaks as
 * the templates that had been attached to the classes will be lost.
 *
 * Fixing this to avoid this leak will be complicated and the likelyhood
 * of this problem is very remote.
 */

static int
update_class (SM_TEMPLATE * template_, MOP * classmop,
	      int auto_res, int verify_oid)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  DB_OBJLIST *cursupers, *oldsupers, *newsupers, *cursubs, *newsubs;
  SM_TEMPLATE *flat;

  sm_bump_schema_version ();
  class_ = NULL;
  cursupers = NULL;
  oldsupers = NULL;
  newsupers = NULL;
  cursubs = NULL;
  newsubs = NULL;

  /*
   *  Set a savepoint in the event that we are adding a unique constraint
   *  to a class with instances and the constraint is violated.  In this
   *  situation, we do not want to abort the entire transaction.
   */
  error = tran_savepoint (UNIQUE_SAVEPOINT_NAME, false);

  if ((error == NO_ERROR) && (template_->op != NULL))
    {
      /* existing class, fetch it */
      error =
	au_fetch_class (template_->op, &class_, AU_FETCH_UPDATE, AU_ALTER);

      /* make sure the catalog manager can deal with another representation */
      if (error == NO_ERROR)
	error = check_catalog_space (template_->op, class_);
    }

  /* pre-lock subclass lattice to the extent possible */

  if (error == NO_ERROR &&
      (error = lockhint_subclasses (template_, class_)) == NO_ERROR)
    {

      /* get write locks on all super clases */
      if (class_ != NULL)
	cursupers = class_->inheritance;

      error = lock_supers (template_, cursupers, &oldsupers, &newsupers);
      if (error == NO_ERROR)
	{
	  /* flatten template, store the pending template in the "new" field
	     of the class in case we need it to make domain comparisons */
	  if (class_ != NULL)
	    class_->new_ = template_;

	  error = flatten_template (template_, NULL, &flat, auto_res);
	  if (error != NO_ERROR)
	    {
	      /* If we aborted the operation (error == ER_LK_UNILATERALLY_ABORTED)
	         then the class may no longer be in the workspace.  So make sure
	         that the class exists before using it.  */
	      if (class_ != NULL && error != ER_LK_UNILATERALLY_ABORTED)
		class_->new_ = NULL;
	    }
	  else
	    {
	      /* get write locks on all subclasses */
	      if (class_ != NULL)
		cursubs = class_->users;

	      error =
		lock_subclasses (template_, newsupers, cursubs, &newsubs);
	      if (error != NO_ERROR)
		{
		  classobj_free_template (flat);
		  /* don't touch this class if we aborted ! */
		  if (class_ != NULL && error != ER_LK_UNILATERALLY_ABORTED)
		    {
		      class_->new_ = NULL;
		    }
		}
	      else
		{
		  /* put the flattened definition in the class for use during subclass
		     flattening */
		  if (class_ != NULL)
		    {
		      class_->new_ = flat;
		    }

		  /* flatten all subclasses */
		  error = flatten_subclasses (newsubs, NULL);
		  if (error != NO_ERROR)
		    {
		      abort_subclasses (newsubs);
		      classobj_free_template (flat);
		      /* don't touch this class if we aborted ! */
		      if (class_ != NULL
			  && error != ER_LK_UNILATERALLY_ABORTED)
			{
			  class_->new_ = NULL;
			}
		    }
		  else
		    {
		      /* now we can assume that every class we need to touch has a write
		         lock - proceed with the installation of the changes */

		      /* are we creating a new class ? */
		      if (class_ == NULL)
			{
			  class_ = classobj_make_class (template_->name);
			  if (class_ == NULL)
			    error = er_errid ();
			  else
			    {
			      class_->owner = Au_user;	/* remember the owner id */

			      /* NOTE: Garbage collection can occur in the following function
			         as a result of the allocation of the class MOP.  We must
			         ensure that there are no object handles in the SM_CLASS structure
			         at this point that don't have roots elsewhere.  Currently, this
			         is the case since we are simply caching a newly created empty
			         class structure which will later be populated with
			         install_new_representation.  The template that holds
			         the new class contents IS already a GC root.
			       */
			      template_->op =
				locator_add_class ((MOBJ) class_,
						   (char *) class_->header.
						   name);

			      if (template_->op == NULL)
				{
				  /* return locator error code */
				  error = er_errid ();
				  abort_subclasses (newsubs);
				  classobj_free_template (flat);
				  classobj_free_class (class_);
				}
			    }
			}

		      if (error == NO_ERROR)
			{
			  /* Verify that a VID object_id is correct. */
			  if (verify_oid)
			    error = verify_object_id (flat);

			  /* the next sequence of operations is extremely critical,
			     if any errors are detected, we'll have to abort the current
			     transaction or the database will be left in an inconsistent
			     state */

			  if (error == NO_ERROR)
			    {
			      flat->partition_parent_atts =
				template_->partition_parent_atts;
			      error =
				install_new_representation (template_->op,
							    class_, flat);
			      if (error == NO_ERROR)
				{

				  /* This used to be done toward the end but since the
				   * unique btid has to be inherited, the disk structures
				   * have to be created before we update the subclasses.
				   */
				  error =
				    allocate_disk_structures (template_->op,
							      class_,
							      newsubs);
				  if (error == NO_ERROR)
				    {
				      error = update_supers (template_->op,
							     oldsupers,
							     newsupers);
				      if (error == NO_ERROR)
					{

					  error = update_subclasses (newsubs);
					  if (error == NO_ERROR)
					    {
					      if (classmop != NULL)
						{
						  *classmop = template_->op;
						}
					      /* we're done */
					      class_->new_ = NULL;

					      classobj_free_template (flat);
					      classobj_free_template
						(template_);
					    }
					}
				    }
				}
			    }
			  /* if (error != NO_ERROR) we got an error during the installation
			   * phase, the structures in the workspace are in an undefined
			   * state and the transaction must be aborted.  The transaction
			   * may already have been aborted by another level, we may want to
			   * check this.
			   *
			   * An exception in if the error occured while we were attempting
			   * to add a unique constraint to a populated class and the
			   * constraint was violated.  In this case, we want to abort
			   * back to the savepoint set above.  We still need to free
			   * templates since we missed that part above.
			   */
			  if (error != NO_ERROR)
			    {
			      classobj_free_template (flat);
			      abort_subclasses (newsubs);
			      if (error == ER_BTREE_UNIQUE_FAILED
				  || error == ER_FK_INVALID)
				{
				  (void)
				    tran_abort_upto_savepoint
				    (UNIQUE_SAVEPOINT_NAME);
				}
			      else
				{
				  (void) tran_unilaterally_abort ();
				}
			    }
			}
		    }
		}
	    }
	}
    }

  ml_free (oldsupers);
  ml_free (newsupers);
  ml_free (newsubs);

  return (error);
}

/*
 * sm_finish_class() - this is called to finish a dbt template,
 *                  dont perform auto resolutions
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   classmop(in): MOP of existing class (NULL if new class)
 */

int
sm_finish_class (SM_TEMPLATE * template_, MOP * classmop)
{
  return (update_class (template_, classmop, 0, 1));
}

/*
 * sm_update_class() - this is what the interpreter calls,
 *                     dont perform auto resolutions
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   classmop(in): MOP of existing class (NULL if new class)
 */

int
sm_update_class (SM_TEMPLATE * template_, MOP * classmop)
{
  return (update_class (template_, classmop, 0, 0));
}

/*
 * sm_update_class_auto() - this is called by the db_ layer,
 *                          perform auto resolution
 *   return: NO_ERROR on success, non-zero for ERROR
 *   template(in): schema template
 *   classmop(in): MOP of existing class (NULL if new class)
 */

int
sm_update_class_auto (SM_TEMPLATE * template_, MOP * classmop)
{
  return (update_class (template_, classmop, 1, 0));
}

/*
 * remove_class_triggers() - Work function for sm_delete_class_mop.
 *    Inform the trigger manager that the class is going away so
 *    it can update the triggers defined for this class.
 *    Need a better strategy for handling errors here.
 *   return: none
 *   classop(in):
 *   class(in): class structure
 */

static void
remove_class_triggers (MOP classop, SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;

  for (att = class_->ordered_attributes; att != NULL; att = att->order_link)
    {
      (void) tr_delete_schema_cache ((TR_SCHEMA_CACHE *) att->triggers,
				     classop);
      att->triggers = NULL;
    }

  for (att = class_->class_attributes; att != NULL;
       att = (SM_ATTRIBUTE *) att->header.next)
    {
      (void) tr_delete_schema_cache ((TR_SCHEMA_CACHE *) att->triggers,
				     classop);
      att->triggers = NULL;
    }

  (void) tr_delete_schema_cache ((TR_SCHEMA_CACHE *) class_->triggers,
				 classop);
  class_->triggers = NULL;
}

/*
 * sm_delete_class() - This will delete a class from the schema and
 *    delete all instances of the class from the database.  All classes that
 *    inherit from this class will be updated so that inhrited components
 *    are removed.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   op(in): class object
 */

int
sm_delete_class_mop (MOP op)
{
  int error = NO_ERROR;
  DB_OBJLIST *oldsupers, *oldsubs;
  SM_CLASS *class_;
  SM_TEMPLATE *template_;
  SM_ATTRIBUTE *att;
  int is_partition = 0, subdel = 0;
  SM_CLASS_CONSTRAINT *pk;

  error = do_is_partitioned_classobj (&is_partition, op, NULL, NULL);
  if (error != NO_ERROR)
    return error;

  if (is_partition == 1)
    {
      error = tran_savepoint (UNIQUE_PARTITION_SAVEPOINT_DROP, false);
      if (error != NO_ERROR)
	return error;

      if ((error = do_drop_partition (op, 1)) != NO_ERROR)
	{
	  if (error != ER_LK_UNILATERALLY_ABORTED)
	    {
	      (void)
		tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_DROP);
	    }
	  return error;
	}
      subdel = 1;
    }

  /* if the delete fails, we'll need to rollback to savepoint */
  error = tran_savepoint (UNIQUE_SAVEPOINT_NAME2, false);
  if (error < 0)
    goto fail_end;

  oldsubs = NULL;
  oldsupers = NULL;
  sm_bump_schema_version ();

  if (op != NULL)
    {
      /* op should be a class */
      if (!locator_is_class (op, DB_FETCH_WRITE))
	ERROR (error, ER_OBJ_NOT_A_CLASS);

      /* Authorization + pre-lock subclass lattice to the extent possible */
      else
	if ((error =
	     au_fetch_class (op, &class_, AU_FETCH_WRITE,
			     AU_ALTER)) == NO_ERROR
	    && (error = lockhint_subclasses (NULL, class_)) == NO_ERROR)
	{

	  pk = classobj_find_cons_primary_key (class_->constraints);
	  if (pk && pk->fk_info
	      && classobj_is_pk_refer_other (op, pk->fk_info))
	    {
	      ERROR (error, ER_FK_CANT_DROP_PK_REFERRED);
	      goto fail_end;
	    }

	  /* remove auto_increment serial object if exist */
	  if (!db_Replication_agent_mode)
	    {
	      for (att = class_->ordered_attributes;
		   att; att = att->order_link)
		{
		  if (att->auto_increment != NULL)
		    {
		      DB_VALUE name_val;
		      char *class_name;

		      error =
			db_get (att->auto_increment, "class_name", &name_val);
		      if (error == NO_ERROR)
			{
			  class_name = DB_GET_STRING (&name_val);
			  if (class_name != NULL &&
			      (strcmp (class_->header.name, class_name) == 0))
			    {
			      error = obj_delete (att->auto_increment);
			    }
			  db_value_clear (&name_val);
			}

		      if (error != NO_ERROR)
			goto fail_end;
		    }
		}
	    }

	  /* we don't really need this but some of the support routines use it */
	  template_ = classobj_make_template (NULL, op, class_);
	  if (template_ == NULL)
	    {
	      error = er_errid ();
	      goto fail_end;
	    }

	  if (class_->inheritance != NULL)
	    {
	      oldsupers = ml_copy (class_->inheritance);
	      if (oldsupers == NULL)
		{
		  error = er_errid ();
		  goto fail_end;
		}
	    }

	  if ((error = lock_supers_drop (oldsupers)) != NO_ERROR)
	    {
	      classobj_free_template (template_);
	    }
	  else
	    {
	      /* get write locks on all subclasses */
	      if ((error =
		   lock_subclasses (template_, NULL, class_->users,
				    &oldsubs)) != NO_ERROR)
		{
		  classobj_free_template (template_);
		}
	      else
		{
		  /* now we can assume that every class we need to touch has a write
		     lock - attempt to flatten subclasses to reflect the deletion */
		  error = flatten_subclasses (oldsubs, op);
		  if (error != NO_ERROR)
		    abort_subclasses (oldsubs);
		  else
		    {
		      /* flush all instances of this class */
		      switch (class_->class_type)
			{
			case SM_CLASS_CT:
			  if (locator_flush_all_instances (op, true) !=
			      NO_ERROR)
			    error = er_errid ();
			  break;

			case SM_VCLASS_CT:
			  if (vid_flush_all_instances (op, true) != NO_ERROR)
			    error = er_errid ();
			  break;

			default:
			  break;
			}

		      if (error != NO_ERROR)
			{
			  /* we had problems flushing, this may be due to an out of
			     space condition, probably the transaction should
			     be aborted as well */
			  abort_subclasses (oldsubs);
			}
		      else
			{
			  /* this section is critical, if any errors happen here,
			     the workspace will be in an inconsistent state and the
			     transaction will have to be aborted */

			  /* now update the supers and users */
			  if ((error =
			       update_supers_drop (op,
						   oldsupers)) == NO_ERROR
			      && (error =
				  update_subclasses (oldsubs)) == NO_ERROR)
			    {

			      /* OLD CODE, here we removed the class from the resident
			       * class list, this causes bad problems for GC since the
			       * class will be GC'd before instances have been decached.
			       * This operation has been moved below with
			       * ws_remove_resident_class().  Not sure if this is position
			       * dependent.  If it doesn't cause any problems remove this
			       * comment.
			       */
			      /* ml_remove(&ws_Resident_classes, op); */

			      /* free any indexes, unique btids, or other associated
			       * disk structures
			       */
			      transfer_disk_structures (op, class_, NULL);

			      /* notify all associated triggers that the class is gone */
			      remove_class_triggers (op, class_);

			      /* This to be maintained as long as the class
			       * is cached in the workspace, dirty or not.  When the
			       * deleted class is flushed, the name is removed.
			       * Assuming this doesn't cause problems, remove this comment
			       */
			      /* ws_drop_classname((MOBJ) class); */

			      /* inform the locator - this will mark the class MOP as
			       * deleted so all operations that require the current class
			       * object must be done before calling this function */

			      if (locator_remove_class (op) == NO_ERROR)
				{

				  /* mark all instance MOPs as deleted, should the locator
				   * be doing this ? */

				  ws_mark_instances_deleted (op);

				  /* make sure this is removed from the resident class list,
				   * this will also make the class mop subject to garbage
				   * collection.
				   * This function will expect that all of the instances of
				   * the class have been decached by this point ! */

				  ws_remove_resident_class (op);

				  classobj_free_template (template_);
				}
			      else
				{
				  /* an error occured - we need to abort */
				  error = er_errid ();
				  if (error !=
				      ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED
				      && error != ER_LK_UNILATERALLY_ABORTED)
				    /* Not already aborted, so abort to savepoint */
				    tran_abort_upto_savepoint
				      (UNIQUE_SAVEPOINT_NAME2);
				}
			    }
			}
		    }
		}
	    }
	}
    }

  ml_free (oldsupers);
  ml_free (oldsubs);
fail_end:
  if (subdel && error != NO_ERROR && error != ER_LK_UNILATERALLY_ABORTED)
    {
      (void) tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_DROP);
    }
  return (error);
}

/*
 * sm_delete_class() - Delete a class by name.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   name(in): class name
 */

int
sm_delete_class (const char *name)
{
  int error = NO_ERROR;
  MOP classop;

  classop = sm_find_class (name);
  if (classop == NULL)
    error = er_errid ();
  else
    error = sm_delete_class_mop (classop);

  return (error);
}

/* INDEX FUNCTIONS */
/*
 * These are in here bacause they share some of the internal
 * allocation/deallocation for indexes.
 * They also play games with the representation id so the
 * catalog gets updated correctly to include the new index.
*/
/*
 * sm_exist_index() - Checks to see if an index exist
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   idxname(in): index name
 */
static int
sm_exist_index (MOP classop, const char *idxname)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *cons;

  if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT))
      == NO_ERROR)
    {
      if ((cons = classobj_find_class_index (class_, idxname)))
	{
	  return NO_ERROR;
	}
    }
  return ER_FAILED;
}

/*
 * sm_add_index() - Adds an index to an attribute.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   attname(in): attribute name
 *   asc_desc(in): asc/desc info list
 *   constraint_name(in): Name of constraint.
 *   reverse_index(in): true for reverse index.
 */

int
sm_add_index (MOP classop, const char **attnames,
	      const int *asc_desc,
	      const char *constraint_name, int reverse_index)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  BTID index;
  int i, n_attrs, is_partition = 0, savepoint_index = 0;
  MOP *sub_partitions = NULL;
  SM_ATTRIBUTE **attrs = NULL;
  const char *class_name;

  error = do_is_partitioned_classobj (&is_partition, classop, NULL,
				      &sub_partitions);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (is_partition == 1)
    {
      error = tran_savepoint (UNIQUE_PARTITION_SAVEPOINT_INDEX, false);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}

      savepoint_index = 1;
      for (i = 0; sub_partitions[i]; i++)
	{
	  if (sm_exist_index (sub_partitions[i], constraint_name) == NO_ERROR)
	    {
	      class_name = sm_class_name (sub_partitions[i]);
	      if (class_name)
		{
		  error = ER_SM_INDEX_EXISTS;
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 2,
			  class_name, constraint_name);
		}
	      else
		{
		  error = er_errid ();
		}
	      break;
	    }
	  error = sm_add_index (sub_partitions[i], attnames, asc_desc,
				constraint_name, reverse_index);
	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
      free_and_init (sub_partitions);
      if (error != NO_ERROR)
	{
	  goto fail_end;
	}
    }

  error = au_fetch_class (classop, &class_, AU_FETCH_UPDATE, AU_INDEX);
  if (error == NO_ERROR)
    {

      /* should be had checked before if this index already exist */

      /* make sure the catalog can handle another representation */
      error = check_catalog_space (classop, class_);
      if (error)
	{
	  goto general_error;
	}

      /* Count the number of attributes */
      n_attrs = 0;
      for (i = 0; attnames[i] != NULL; i++)
	{
	  n_attrs++;
	}

      /* Allocate memory for the attribute array */
      attrs =
	(SM_ATTRIBUTE **) malloc (sizeof (SM_ATTRIBUTE *) * (n_attrs + 1));
      if (attrs == NULL)
	{
	  error = er_errid ();
	  goto general_error;
	}

      /* Retrieve all of the attributes */
      for (i = 0; i < n_attrs; i++)
	{
	  attrs[i] = classobj_find_attribute (class_, attnames[i], 0);
	  if (attrs[i] == NULL || attrs[i]->header.name_space != ID_ATTRIBUTE)
	    {
	      ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, attnames[i]);
	      goto general_error;
	    }
	  if (sm_has_text_domain (attrs[i], 0))
	    {
	      if (strstr (constraint_name, TEXT_CONSTRAINT_PREFIX))
		{
		  /* prevent to create index on TEXT attribute */
		  ERROR1 (error, ER_REGU_NOT_IMPLEMENTED,
			  rel_major_release_string ());
		  goto general_error;
		}
	    }
	}
      attrs[n_attrs] = NULL;

      /* Make sure both the class and the instances are flushed before
         creating the index.  NOTE THAT THIS WILL REMOVE THE DIRTY
         BIT FROM THE CLASS OBJECT BEFORE THE INDEX HAS ACTUALLY BEEN
         ATTACHED !  WE NEED TO MAKE SURE THE CLASS IS MARKED DIRTY
         AGAIN AFTER THE INDEX LOAD.
       */

      if (locator_flush_class (classop) != NO_ERROR ||
	  locator_flush_all_instances (classop, true) != NO_ERROR)
	return er_errid ();

      /* allocate the index - this will result in a btree load if there
         are existing instances */
      BTID_SET_NULL (&index);
      error =
	allocate_index (classop, class_, NULL, attrs, asc_desc, false,
			reverse_index, constraint_name, &index, NULL, NULL,
			-1, NULL);

      if (error == NO_ERROR)
	{
	  /* must bump the representation in order to get the index into
	     the catalog unfortunate but not worth changing now, this is
	     alot simpler than install_new_representation because there
	     are no structural changes made to the instances
	     - this must be an atomic operation.
	     If this fails, the transaction must be aborted.
	   */
	  if (classobj_snapshot_representation (class_))
	    goto severe_error;

	  /* modify the class to point at the new index */
	  if (classobj_put_index_id (&(class_->properties),
				     (!reverse_index) ? SM_CONSTRAINT_INDEX :
				     SM_CONSTRAINT_REVERSE_INDEX,
				     constraint_name, attrs, asc_desc, &index,
				     NULL, NULL) != NO_ERROR)
	    {
	      error = er_errid ();
	      goto general_error;
	    }
	  error = classobj_cache_class_constraints (class_);
	  if (error != NO_ERROR)
	    {
	      goto general_error;
	    }
	  if (!classobj_cache_constraints (class_))
	    {
	      error = er_errid ();
	      goto general_error;
	    }

	  /* now that the index is physically attached to the class, we must
	     mark it as dirty and flush it again to make sure the catalog
	     is updated correctly.  This is necessary because the allocation
	     and loading of the instance are done at the same time.  We need
	     to be able to allocate the index and flush the class BEFORE
	     the loading to avoid this extra step. */

	  /* If either of these operations fail, the transaction should
	     be aborted */
	  if (locator_update_class (classop) == NULL)
	    goto severe_error;

	  if (locator_flush_class (classop) != NO_ERROR)
	    goto severe_error;

	  /* since we almost always want to use the index after
	     it has been created, cause the statistics for this
	     class to be updated so that the optimizer is able
	     to make use of the new index.  Recall that the optimizer
	     looks at the statistics structures, not the schema structures.
	   */
	  if (sm_update_statistics (classop))
	    goto severe_error;
	}

      free_and_init (attrs);
    }

fail_end:
  if (savepoint_index && error != NO_ERROR &&
      error != ER_LK_UNILATERALLY_ABORTED)
    (void) tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_INDEX);
  if (sub_partitions)
    free_and_init (sub_partitions);

  return (error);

general_error:
  if (attrs != NULL)
    free_and_init (attrs);
  return error;

severe_error:
  /* Something happened at a bad time, the database is in an inconsistent
     state.  Must abort the transaction.
     Save the error that caused the problem.
     We should try to disable error overwriting when we
     abort so the caller can find out what happened.
   */
  if (attrs != NULL)
    free_and_init (attrs);
  error = er_errid ();
  (void) tran_unilaterally_abort ();
  return error;
}


/*
 * sm_drop_index() - Removes an index for an attribute.
 *    Take care to remove the class property list entry for this
 *    index if one has been created.  !! This works now because
 *    sm_drop_index is the only way that we can remove indexes.  If
 *    index add/drop can ever be done during template processing, we'll
 *    have to make that code more aware of this.  I suspect that this
 *    will all get cleaned up during the migration to multi-column
 *    indexes.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   constraint_name(in): constraint name
 */

int
sm_drop_index (MOP classop, const char *constraint_name)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *found;
  SM_CONSTRAINT_TYPE ctype;
  int i, is_partition = 0, savepoint_index = 0;
  MOP *sub_partitions = NULL;

  error = do_is_partitioned_classobj (&is_partition, classop, NULL,
				      &sub_partitions);
  if (error != NO_ERROR)
    return error;

  if (is_partition == 1)
    {
      error = tran_savepoint (UNIQUE_PARTITION_SAVEPOINT_INDEX, false);
      if (error != NO_ERROR)
	goto fail_end;

      savepoint_index = 1;
      for (i = 0; sub_partitions[i]; i++)
	{
	  if (sm_exist_index (sub_partitions[i], constraint_name) != NO_ERROR)
	    {
	      continue;
	    }
	  error = sm_drop_index (sub_partitions[i], constraint_name);
	  if (error != NO_ERROR)
	    break;
	}
      free_and_init (sub_partitions);
      if (error != NO_ERROR)
	goto fail_end;
    }

  if ((error = au_fetch_class (classop, &class_, AU_FETCH_UPDATE, AU_INDEX))
      == NO_ERROR)
    {

      /* Verify that this constraint does exist */
      ctype = SM_CONSTRAINT_INDEX;
      found = classobj_find_class_constraint (class_->constraints, ctype,
					      constraint_name);

      if (found == NULL)
	{
	  ctype = SM_CONSTRAINT_REVERSE_INDEX;
	  found = classobj_find_class_constraint (class_->constraints, ctype,
						  constraint_name);
	}
      if (found == NULL)
	ERROR1 (error, ER_SM_NO_INDEX, constraint_name);
      else
	{

	  /* make sure the catalog can handle another representation */
	  error = check_catalog_space (classop, class_);
	  if (error)
	    return error;

	  /* must bump the representation in order to get the catalog updated,
	     unfortunate but not worth changing now, this is alot simpler
	     than install_new_representation because there are no structural
	     changes made to the instances - this must be an atomic operation */
	  if (classobj_snapshot_representation (class_))
	    goto severe_error;

	  /*
	   *  Remove the index from the class.  We do this is an akward
	   *  way.  First we remove it from the class constraint cache and
	   *  then we back propogate the changes to the class property list.
	   *  We do this backwards because it's easier, go figure.
	   */
	  if (deallocate_index (class_->constraints, &found->index))
	    goto severe_error;

	  BTID_SET_NULL (&found->index);
	  classobj_remove_class_constraint_node (&class_->constraints, found);
	  classobj_free_class_constraints (found);

	  error = classobj_populate_class_properties (&class_->properties,
						      class_->constraints,
						      ctype);

	  if (classobj_cache_class_constraints (class_) != NO_ERROR)
	    goto severe_error;
	  if (!classobj_cache_constraints (class_))
	    goto severe_error;

	  /* Make sure the class is now marked dirty and flushed so that
	     the catalog is updated.  Also update statistics so that
	     the optimizer will know that the index no longer exists.
	   */
	  if (locator_update_class (classop) == NULL)
	    goto severe_error;

	  if (locator_flush_class (classop) != NO_ERROR)
	    goto severe_error;

	  if (sm_update_statistics (classop))
	    goto severe_error;
	}
    }

fail_end:
  if (savepoint_index && error != NO_ERROR &&
      error != ER_LK_UNILATERALLY_ABORTED)
    (void) tran_abort_upto_savepoint (UNIQUE_PARTITION_SAVEPOINT_INDEX);
  if (sub_partitions)
    free_and_init (sub_partitions);
  return (error);

severe_error:
  /* Something happened at a bad time, the database is in an inconsistent
     state.  Must abort the transaction.
     Save the error that caused the problem.
     We should try to disable error overwriting when we
     abort so the caller can find out what happened.
   */
  error = er_errid ();
  (void) tran_unilaterally_abort ();
  return error;
}

/*
 * sm_get_index() - Checks to see if an attribute has an index and if so,
 *    returns the BTID of the index.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class object
 *   attname(in): attribute name
 *   index(out): returned pointer to index
 */

int
sm_get_index (MOP classop, const char *attname, BTID * index)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;

  /* what happens if we formerly indexed the attribute, revoked index
     authorization and now want to remove it ? */

  if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT))
      == NO_ERROR)
    {
      att = classobj_find_attribute (class_, attname, 0);
      if (att == NULL || att->header.name_space != ID_ATTRIBUTE)
	ERROR1 (error, ER_SM_ATTRIBUTE_NOT_FOUND, attname);
      else
	{
	  SM_CONSTRAINT *con;
	  int found = 0;

	  /*  First look for the index in the attribute constraint cache */
	  for (con = att->constraints; ((con != NULL) && !found);
	       con = con->next)
	    {
	      if (con->type == SM_CONSTRAINT_INDEX ||
		  con->type == SM_CONSTRAINT_REVERSE_INDEX)
		{
		  *index = con->index;
		  found = 1;
		}
	    }
	}
    }
  return (error);
}


/*
 * sm_default_constraint_name() - Constructs a constraint name based upon
 *    the class and attribute names and names's asc/desc info.
 *    Returns the constraint name or NULL is an error occured.  The string
 *    should be deallocated with free_and_init() when no longer needed.
 *    The class name is normally obtained from the Class Object.  This is
 *    not always possible, though (for instance at class creation time,
 *    there is no class object and <classop> will be NULL).  Under this
 *    condition, the class name will be taken from the Class Template
 *    <ctmpl>.
 *    The format of the default name is;
 *        X_<class>_att1_att2_... or
 *        X_<class>_att1_d_att2_...  --> _d implies that att1 order is 'desc'
 *    where X indicates the constraint type;
 *          i=INDEX,            u=UNIQUE,       pk=PRIMARY KEY,
 *          fk=FOREIGN KEY,     n=NOT NULL,     ru=REVERSE UNIQUE,
 *          ri=REVERSE INDEX
 *          <class> is the class name
 *          attn is the attribute name
 *    (ex)  If we are generating a default name for
 *              create index on foo (a, b);
 *          It would look like i_foo_a_b
 *    (ex)  If we are generating a default name for
 *              create index on foo (a desc, b);
 *          It would look like i_foo_a_d_b --> use '_d' for 'desc'
 *    (ex)  If we are generating a default name for
 *              create reverse index on foo (a desc, b);
 *          It would look like ri_foo_a_b --> not use '_d' for reverse type
 *   return: constraint name
 *   class_name(in): class name
 *   type(in): Constraint Type
 *   att_names(in): Attribute Names
 *   asc_desc(in): asc/desc info list
 */

static char *
sm_default_constraint_name (const char *class_name,
			    DB_CONSTRAINT_TYPE type,
			    const char **att_names, const int *asc_desc)
{
  const char **ptr;
  char *name = NULL;
  int name_length = 0;
  bool do_desc;
  int error = NO_ERROR;

  /*
   *  Construct the constraint name
   */
  if ((class_name == NULL) || (att_names == NULL))
    {
      ERROR (error, ER_SM_INVALID_DEF_CONSTRAINT_NAME_PARAMS);
    }
  else
    {
      const char *prefix;
      int i;

      /* Constraint Type */
      prefix = (type == DB_CONSTRAINT_INDEX) ? "i_" :
	(type == DB_CONSTRAINT_UNIQUE) ? "u_" :
	(type == DB_CONSTRAINT_PRIMARY_KEY) ? "pk_" :
	(type == DB_CONSTRAINT_FOREIGN_KEY) ? "fk_" :
	(type == DB_CONSTRAINT_NOT_NULL) ? "n_" :
	(type == DB_CONSTRAINT_REVERSE_UNIQUE) ? "ru_" :
	(type == DB_CONSTRAINT_REVERSE_INDEX) ? "ri_" :
	/*          UNKNOWN TYPE            */ "x_";

      /*
       *  Count the number of characters that we'll need for the name
       */
      name_length = sizeof (prefix);
      name_length += strlen (class_name);	/* class name */

      i = 0;
      for (ptr = att_names; *ptr != NULL; ptr++, i++)
	{
	  do_desc = false;	/* init */
	  if (asc_desc)
	    {
	      if (!DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (type))
		{
		  /* attr is marked as 'desc' in the non-reverse index */
		  if (asc_desc[i] == 1)
		    do_desc = true;
		}
	    }

	  name_length += (1 + strlen (*ptr));	/* seprator and attr name */
	  if (do_desc)
	    name_length += 2;	/* '_d' for 'desc' */
	}			/* for (ptr = ...) */

      /*
       *  Allocate space for the name and construct it
       */
      name = (char *) malloc (name_length + 1);	/* Remember terminating NULL */
      if (name != NULL)
	{
	  /* Constraint Type */
	  strcpy (name, prefix);

	  /* Class name */
	  strcat (name, class_name);

	  /* seperated list of attribute names */
	  i = 0;
	  for (ptr = att_names; *ptr != NULL; ptr++, i++)
	    {
	      do_desc = false;	/* init */
	      if (asc_desc)
		{
		  if (!DB_IS_CONSTRAINT_REVERSE_INDEX_FAMILY (type))
		    {
		      /* attr is marked as 'desc' in the non-reverse index */
		      if (asc_desc[i] == 1)
			do_desc = true;
		    }
		}

	      strcat (name, "_");

	      intl_mbs_lower (*ptr, &name[strlen (name)]);

	      /* attr is marked as 'desc' */
	      if (do_desc)
		strcat (name, "_d");
	    }			/* for (ptr = ...) */

	  /* now, strcat already appended terminating NULL character */
	}
    }

  return name;
}


/*
 * sm_produce_constraint_name() - Generate a normalized constraint name.
 *    If a constraint name is given <given_name> != NULL, then this name is
 *    downcased and returned. In this case, the constraint type and attribute
 *    names are not needed.
 *    If a given name is not provided <given_name> == NULL, then a
 *    normalized name is generated using the constraint type and attribute
 *    names.
 *    In either case, the returned name is generated its own memory area
 *    and should be deallocated with by calling sm_free_constraint_name()
 *    when it is no longer needed.
 *    This function differs from sm_produce_constraint_name_mop() in that
 *    the class name is supplied as a parameters and therefore, does not
 *    need to be derived.
 *   return: constraint name
 *   class_name(in): Class Name
 *   constraint_type(in): Constraint Type
 *   att_names(in): Attribute Names
 *   asc_desc(in): asc/desc info list
 *   given_name(in): Optional constraint name.
 */

char *
sm_produce_constraint_name (const char *class_name,
			    DB_CONSTRAINT_TYPE constraint_type,
			    const char **att_names,
			    const int *asc_desc, const char *given_name)
{
  char *name = NULL;

  if (given_name == NULL)
    {
      name = sm_default_constraint_name (class_name, constraint_type,
					 att_names, asc_desc);
    }
  else
    {
      name = (char *) malloc ((strlen (given_name) + 1) * sizeof (char));
      if (name)
	intl_mbs_lower (given_name, name);
    }

  return name;
}


/*
 * sm_produce_constraint_name_mop() - This function serves the same
 *    functionality as sm_produce_constraint_name() except that it accepts
 *    a class MOP instead of a class name.
 *   return: constraint name
 *   classop(in): Class Object
 *   constraint_type(in): Constraint Type
 *   att_names(in): Attribute Names
 *   given_name(in): Optional constraint name.
 */

char *
sm_produce_constraint_name_mop (MOP classop,
				DB_CONSTRAINT_TYPE constraint_type,
				const char **att_names,
				const char *given_name)
{

  return sm_produce_constraint_name (sm_class_name (classop), constraint_type, att_names, NULL,	/* may NEED FUTURE WORK - DO NOT DELETE ME */
				     given_name);
}


/*
 * sm_produce_constraint_name_tmpl() - This function serves the same
 *    functionality as sm_produce_constraint_name() except that it accepts
 *    a class template instead of a class name.
 *   return: constraint name
 *   tmpl(in): Class Template
 *   constraint_type(in): Constraint Type
 *   att_names(in): Attribute Names
 *   given_name(in): Optional constraint name.
 */

char *
sm_produce_constraint_name_tmpl (SM_TEMPLATE * tmpl,
				 DB_CONSTRAINT_TYPE constraint_type,
				 const char **att_names,
				 const char *given_name)
{

  return sm_produce_constraint_name (template_classname (tmpl), constraint_type, att_names, NULL,	/* may NEED FUTURE WORK - DO NOT DELETE ME */
				     given_name);
}


/*
 * sm_free_constraint_name() - Deallocate the system generated constraint name.
 *   return: none
 *   constraint_name(in): Constraint name
 */

void
sm_free_constraint_name (char *constraint_name)
{
  if (constraint_name != NULL)
    free_and_init (constraint_name);
}


/*
 * sm_add_constraint() - Add a constraint to the class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class (or instance) pointer
 *   constraint_type(in): Type of constraint to add (UNIQUE, NOT NULL or INDEX)
 *   constraint_name(in): What to call the new constraint
 *   att_names(in): Names of attributes to be constrained
 *   asc_desc(in): asc/desc info list
 *   class_attributes(in): Flag.  A true value indicates that the names refer to
 *     		class attributes. A false value indicates that the names
 *     		refer to instance attributes.
 */

int
sm_add_constraint (MOP classop,
		   DB_CONSTRAINT_TYPE constraint_type,
		   const char *constraint_name,
		   const char **att_names,
		   const int *asc_desc, int class_attributes)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_CLASS_CONSTRAINT *cons;
  SM_CLASS *smclass;
  char *shared_cons_name = NULL;

  if (DB_IS_CONSTRAINT_INDEX_FAMILY (constraint_type))
    {
      /* check index name uniqueness */
      if ((error = au_fetch_class (classop, &class_, AU_FETCH_READ, AU_INDEX))
	  != NO_ERROR)
	return error;
      if ((cons = classobj_find_class_index (class_, constraint_name)))
	{
	  ERROR2 (error, ER_SM_INDEX_EXISTS, class_->header.name, cons->name);
	  return error;
	}
      smclass = (asc_desc) ? sm_get_class_with_statistics (classop) : NULL;
      cons = classobj_find_class_index2 (class_,
					 (smclass ? smclass->stats : NULL),
					 constraint_type, att_names,
					 asc_desc);
      if (cons)
	{
	  if (cons->name && strstr (cons->name, TEXT_CONSTRAINT_PREFIX))
	    {
	      ERROR1 (error, ER_REGU_NOT_IMPLEMENTED,
		      rel_major_release_string ());
	      return error;
	    }

	  if (DB_IS_CONSTRAINT_UNIQUE_FAMILY (constraint_type) &&
	      SM_IS_CONSTRAINT_UNIQUE_FAMILY (cons->type))
	    {
	      shared_cons_name = (char *) cons->name;
	    }
	  else
	    {
	      ERROR2 (error, ER_SM_INDEX_EXISTS, class_->header.name,
		      cons->name);
	      return error;
	    }
	}
      if (constraint_type == DB_CONSTRAINT_PRIMARY_KEY)
	{
	  cons = classobj_find_class_primary_key (class_);
	  if (cons)
	    {
	      ERROR2 (error, ER_SM_PRIMARY_KEY_EXISTS, class_->header.name,
		      cons->name);
	      return error;
	    }
	}
    }

  switch (constraint_type)
    {
    case DB_CONSTRAINT_INDEX:
    case DB_CONSTRAINT_REVERSE_INDEX:
      error = sm_add_index (classop, att_names, asc_desc, constraint_name,
			    (constraint_type ==
			     DB_CONSTRAINT_INDEX) ? false : true);
      break;

    case DB_CONSTRAINT_UNIQUE:
    case DB_CONSTRAINT_REVERSE_UNIQUE:
    case DB_CONSTRAINT_PRIMARY_KEY:
      {
	SM_TEMPLATE *def;

	def = smt_edit_class_mop (classop);
	if (def == NULL)
	  {
	    error = er_errid ();
	  }
	else
	  {
	    error =
	      smt_constrain (def, att_names, asc_desc, constraint_name,
			     class_attributes,
			     SM_MAP_CONSTRAINT_TO_ATTFLAG (constraint_type),
			     true, NULL, shared_cons_name);
	    if (error)
	      {
		smt_quit (def);
	      }
	    else
	      {
		error = sm_update_class (def, NULL);
		if (error)
		  {
		    smt_quit (def);
		  }
	      }
	  }
      }
      break;

    case DB_CONSTRAINT_NOT_NULL:
      {
	SM_TEMPLATE *def;

	def = smt_edit_class_mop (classop);
	if (def == NULL)
	  {
	    error = er_errid ();
	  }
	else
	  {
	    error = smt_constrain (def, att_names, asc_desc, constraint_name,
				   class_attributes, SM_ATTFLAG_NON_NULL,
				   true, NULL, NULL);
	    if (error)
	      {
		smt_quit (def);
	      }
	    else
	      {
		error = sm_update_class (def, NULL);
		if (error)
		  smt_quit (def);
	      }
	  }
      }
      break;

    default:
      break;
    }

  return error;
}



/*
 * sm_drop_constraint() - Drop a constraint from the class.
 *   return: NO_ERROR on success, non-zero for ERROR
 *   classop(in): class (or instance) pointer
 *   constraint_type(in): Type of constraint to add (UNIQUE, NOT NULL or INDEX)
 *   constraint_name(in): What to call the new constraint
 *   att_names(in): Names of attributes to be constrained
 *   class_attributes(in): Flag.  A true value indicates that the names refer to
 *   		class attributes. A false value indicates that
 *   		the names refer to instance attributes.
 */

int
sm_drop_constraint (MOP classop,
		    DB_CONSTRAINT_TYPE constraint_type,
		    const char *constraint_name,
		    const char **att_names, int class_attributes)
{
  int error = NO_ERROR;

  switch (constraint_type)
    {
    case DB_CONSTRAINT_INDEX:
    case DB_CONSTRAINT_REVERSE_INDEX:
      error = sm_drop_index (classop, constraint_name);
      break;

    case DB_CONSTRAINT_UNIQUE:
    case DB_CONSTRAINT_REVERSE_UNIQUE:
    case DB_CONSTRAINT_PRIMARY_KEY:
      {
	SM_TEMPLATE *def;

	def = smt_edit_class_mop (classop);
	if (def == NULL)
	  {
	    error = er_errid ();
	  }
	else
	  {
	    error =
	      smt_constrain (def, att_names, NULL, constraint_name,
			     class_attributes,
			     SM_MAP_CONSTRAINT_TO_ATTFLAG (constraint_type),
			     false, NULL, NULL);
	    if (error)
	      {
		smt_quit (def);
	      }
	    else
	      {
		error = sm_update_class (def, NULL);
		if (error)
		  smt_quit (def);
	      }
	  }
      }
      break;

    case DB_CONSTRAINT_NOT_NULL:
      {
	SM_TEMPLATE *def;

	def = smt_edit_class_mop (classop);
	if (def == NULL)
	  {
	    error = er_errid ();
	  }
	else
	  {
	    error = smt_constrain (def, att_names, NULL, constraint_name,
				   class_attributes, SM_ATTFLAG_NON_NULL,
				   false, NULL, NULL);
	    if (error)
	      {
		smt_quit (def);
	      }
	    else
	      {
		error = sm_update_class (def, NULL);
		if (error)
		  smt_quit (def);
	      }
	  }
      }
      break;

    default:
      break;
    }

  return error;
}
