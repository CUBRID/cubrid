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
 * transform_cl.c: Functions related to the storage of instances and classes.
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "porting.h"
#include "memory_alloc.h"
#include "error_manager.h"
#include "work_space.h"
#include "oid.h"
#include "object_representation.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "class_object.h"
#include "set_object.h"
#include "transform.h"
#include "transform_cl.h"
#include "schema_manager.h"
#include "locator_cl.h"
#include "object_accessor.h"
#include "trigger_manager.h"
#include "locator.h"
#include "server_interface.h"
#include "execute_statement.h"
#include "db_json.hpp"

#include "dbtype.h"

#if defined (SUPPRESS_STRLEN_WARNING)
#define strlen(s1)  ((int) strlen(s1))
#endif /* defined (SUPPRESS_STRLEN_WARNING) */

/*
 * tf_Allow_fixups
 *
 * Integration kludge.
 * The changes for initialization of the OR_BUF were rather widespread
 * and could not all be done at the same time.
 * This will be OFF until we're sure all the OR_BUF usages are initialized.
 *
 * Note, after this gets checked in, and is shown to work well enough, this
 * variable can be removed, it is only retained to allow for a quick disable
 * in the event that something ugly happens.
 */
int tf_Allow_fixups = 1;


/*
 * OR_TEMPOID
 *    Information about a temporary OID encountered during a transformation.
 *    These will get stuck onto the LC_OIDMAP structures as they are created
 *    for the LC_OIDSET being maintained inside the OR_FIXUP structure, whew.
 *    LC_OIDSET is a structure that gets sent accross the client/server
 *    boundary so it contains only server safe things.  Unfortuantely, we
 *    need to map entries in the LC_OIDSET universe back into the DB_OBJECT*
 *    and transformation buffer locations that need to get fixed once the
 *    permanent OID is known.  Rather than keep parallel structures for this,
 *    we're allowed to stick a client/side structure on each LC_OIDMAP
 *    entry, this is it.
 *    When the LC_OIDMAP is returned by the server with permanent OIDs, it
 *    will be unpacked INTO an EXISTING LC_OIDMAP structure rather than
 *    creating a new one.  This existing structure will still have pointers
 *    to this structure so once the server has filled in the permanent OIDs
 *    we can simply walk over the LC_OIDMAP, get into the OR_TEMPOID and make
 *    the appropriate modifications.
 */
typedef struct or_tempoid
{

  int refsize;			/* number of elements in array */
  int refcount;			/* our fill pointer */
  void **references;		/* references, NULL terminated */

} OR_TEMPOID;

/*
 * OR_FIXUP
 *    State structure maintained in an OR_BUF that encapuslates a history
 *    of temporary OIDs that we encounter during transformation.
 *    Now that the LC_OIDSET is sufficiently complex, we could just replace
 *    OR_FIXUP with that, but lets keep the encapsulation for awhile in case
 *    we need to add somethign else here.
 */
typedef struct or_fixup
{
  LC_OIDSET *oidset;
} OR_FIXUP;

/*
 * Set functions
 *    These are shorthand macros for functions that are required as
 *    arguments to the variable set functions below.
 *
 */
typedef int (*LSIZER) (void *);
typedef void (*LWRITER) (void *, void *);
typedef DB_LIST *(*LREADER) (void *);
typedef void (*VREADER) (void *, void *);

/*
 * Forward declaration of local functions
 */
static int optimize_sets (SM_CLASS * class_, MOBJ volatile obj);
static OR_FIXUP *tf_make_fixup (void);
static void tf_free_fixup (OR_FIXUP * fix);
static void fixup_callback (LC_OIDMAP * oidmap);
static int tf_do_fixup (OR_FIXUP * fix);
static int put_varinfo (OR_BUF * buf, char *obj, SM_CLASS * class_, int offset_size);
static int object_size (SM_CLASS * class_, MOBJ obj, int *offset_size_ptr);
static int put_attributes (OR_BUF * buf, char *obj, SM_CLASS * class_);
static char *get_current (OR_BUF * buf, SM_CLASS * class_, MOBJ * obj_ptr, int bound_bit_flag, int offset_size);
static SM_ATTRIBUTE *find_current_attribute (SM_CLASS * class_, int id);
static void clear_new_unbound (char *obj, SM_CLASS * class_, SM_REPRESENTATION * oldrep);
static char *get_old (OR_BUF * buf, SM_CLASS * class_, MOBJ * obj_ptr, int repid, int bound_bit_flag, int offset_size);
static char *unpack_allocator (int size);
static OR_VARINFO *read_var_table (OR_BUF * buf, int nvars);
static OR_VARINFO *read_var_table_internal (OR_BUF * buf, int nvars, int offset_size);
static void free_var_table (OR_VARINFO * vars);
static int string_disk_size (const char *string);
static char *get_string (OR_BUF * buf, int length);
static void put_string (OR_BUF * buf, const char *string);
static int object_set_size (DB_OBJLIST * list);
static int put_object_set (OR_BUF * buf, DB_OBJLIST * list);
static DB_OBJLIST *get_object_set (OR_BUF * buf, int expected);
static int substructure_set_size (DB_LIST * list, LSIZER function);
static void put_substructure_set (OR_BUF * buf, DB_LIST * list, LWRITER writer, OID * class_, int repid);
static DB_LIST *get_substructure_set (OR_BUF * buf, LREADER reader, int expected);
static void install_substructure_set (OR_BUF * buf, DB_LIST * list, VREADER reader, int expected);
static int property_list_size (DB_SEQ * properties);
static void put_property_list (OR_BUF * buf, DB_SEQ * properties);
static DB_SEQ *get_property_list (OR_BUF * buf, int expected_size);
static int domain_size (TP_DOMAIN * domain);
static void domain_to_disk (OR_BUF * buf, TP_DOMAIN * domain);
static TP_DOMAIN *disk_to_domain2 (OR_BUF * buf);
static TP_DOMAIN *disk_to_domain (OR_BUF * buf);
static void metharg_to_disk (OR_BUF * buf, SM_METHOD_ARGUMENT * arg);
static int metharg_size (SM_METHOD_ARGUMENT * arg);
static SM_METHOD_ARGUMENT *disk_to_metharg (OR_BUF * buf);
static int methsig_to_disk (OR_BUF * buf, SM_METHOD_SIGNATURE * sig);
static inline void methsig_to_disk_lwriter (void *buf, void *sig);
static int methsig_size (SM_METHOD_SIGNATURE * sig);
static SM_METHOD_SIGNATURE *disk_to_methsig (OR_BUF * buf);
static int method_to_disk (OR_BUF * buf, SM_METHOD * method);
static inline void method_to_disk_lwriter (void *buf, void *method);
static int method_size (SM_METHOD * method);
static void disk_to_method (OR_BUF * buf, SM_METHOD * method);
static int methfile_to_disk (OR_BUF * buf, SM_METHOD_FILE * file);
static inline void methfile_to_disk_lwriter (void *buf, void *file);
static int methfile_size (SM_METHOD_FILE * file);
static SM_METHOD_FILE *disk_to_methfile (OR_BUF * buf);
static int query_spec_to_disk (OR_BUF * buf, SM_QUERY_SPEC * query_spec);
static inline void query_spec_to_disk_lwriter (void *buf, void *query_spec);
static int query_spec_size (SM_QUERY_SPEC * statement);
static SM_QUERY_SPEC *disk_to_query_spec (OR_BUF * buf);
static int attribute_to_disk (OR_BUF * buf, SM_ATTRIBUTE * att);
static inline void attribute_to_disk_lwriter (void *buf, void *att);
static int attribute_size (SM_ATTRIBUTE * att);
static void disk_to_attribute (OR_BUF * buf, SM_ATTRIBUTE * att);
static int resolution_to_disk (OR_BUF * buf, SM_RESOLUTION * res);
static inline void resolution_to_disk_lwriter (void *buf, void *res);
static int resolution_size (SM_RESOLUTION * res);
static SM_RESOLUTION *disk_to_resolution (OR_BUF * buf);
static int repattribute_to_disk (OR_BUF * buf, SM_REPR_ATTRIBUTE * rat);
static inline void repattribute_to_disk_lwriter (void *buf, void *rat);
static int repattribute_size (SM_REPR_ATTRIBUTE * rat);
static SM_REPR_ATTRIBUTE *disk_to_repattribute (OR_BUF * buf);
static int representation_size (SM_REPRESENTATION * rep);
static int representation_to_disk (OR_BUF * buf, SM_REPRESENTATION * rep);
static inline void representation_to_disk_lwriter (void *buf, void *rep);
static SM_REPRESENTATION *disk_to_representation (OR_BUF * buf);
static int check_class_structure (SM_CLASS * class_);
static int put_class_varinfo (OR_BUF * buf, SM_CLASS * class_);
static void put_class_attributes (OR_BUF * buf, SM_CLASS * class_);
static void class_to_disk (OR_BUF * buf, SM_CLASS * class_);
static void tag_component_namespace (SM_COMPONENT * components, SM_NAME_SPACE name_space);
static SM_CLASS *disk_to_class (OR_BUF * buf, SM_CLASS ** class_ptr);
static void root_to_disk (OR_BUF * buf, ROOT_CLASS * root);
static int root_size (MOBJ rootobj);
static int tf_class_size (MOBJ classobj);
static ROOT_CLASS *disk_to_root (OR_BUF * buf);

static int enumeration_size (const DB_ENUMERATION * enumeration);
static void put_enumeration (OR_BUF * buf, const DB_ENUMERATION * e);
static int get_enumeration (OR_BUF * buf, DB_ENUMERATION * enumeration, int expected);
static int tf_attribute_default_expr_to_property (SM_ATTRIBUTE * attr_list);

static int partition_info_to_disk (OR_BUF * buf, SM_PARTITION * partition_info);
static inline void partition_info_to_disk_lwriter (void *buf, void *partition_info);
static SM_PARTITION *disk_to_partition_info (OR_BUF * buf);
static int partition_info_size (SM_PARTITION * partition_info);
static void or_pack_mop (OR_BUF * buf, MOP mop);

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tf_find_temporary_oids - walks over the memory representation of an
 * instance and finds all the temporary OIDs within it and adds them to the
 * oidset.
 *    return: NO_ERROR if successful, error code otherwise
 *    oidset(out): oidset to populate
 *    classobj(in): object to examine
 */
int
tf_find_temporary_oids (LC_OIDSET * oidset, MOBJ classobj, MOBJ obj)
{
  int error = NO_ERROR;
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  DB_TYPE type;
  SETOBJ *col;

  /*
   * Do this only for instance objects.  This means that class objects
   * with temporary oids in, say, class variables won't get flushed at
   * this time, but that's probably ok.  They'll get flushed as part of
   * the ordinary class object packing business, and since they ought to
   * be few in number, that shouldn't have much of an impact.
   */
  if ((classobj != (MOBJ) (&sm_Root_class)) && (obj != NULL))
    {

      class_ = (SM_CLASS *) classobj;

      for (att = class_->attributes; att != NULL && error == NO_ERROR; att = (SM_ATTRIBUTE *) att->header.next)
	{

	  type = TP_DOMAIN_TYPE (att->domain);
	  if (type == DB_TYPE_OBJECT)
	    {
	      WS_MEMOID *mem;
	      OID *oid;
	      mem = (WS_MEMOID *) (obj + att->offset);
	      oid = &mem->oid;

	      if (OID_ISTEMP (oid) && mem->pointer != NULL && !WS_IS_DELETED (mem->pointer))
		{

		  /* Make sure the ws_memoid mop is temporary. */
		  if (OID_ISTEMP (WS_OID (mem->pointer)))
		    {

		      /* its an undeleted temporary object, promote */
		      if (locator_add_oidset_object (oidset, mem->pointer) == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			  error = er_errid ();
			}
		    }
		}
	    }
	  else if (TP_IS_SET_TYPE (type))
	    {
	      col = *(SETOBJ **) (obj + att->offset);

	      error = setobj_find_temporary_oids (col, oidset);
	    }
	}
    }

  return error;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * optimize_sets - Called before the transformation begins to ensure that
 * any sets this object contains are sorted before sending them to disk.
 *    return: NO_ERROR if successful, error code otherwise
 *    class(in): class structure
 *    obj(in/out): object attributes
 * Note:
 *    In practice, sets will be unsorted only if they contained temporary
 *    OIDs at one time in their lives.  This will have the side effect
 *    of doing a bulk OID assignment for each set that is unsorted and then
 *    sorting the set.  This may add some time to the flush process but
 *    the sort will not allocate very much memory so it should be safe in low
 *    memory situations.
 *
 *    This would be more effecient if we split this into two opeations,
 *    the first would walk over the entire object upgrading all temporary
 *    OIDs to permanent ones.  The second sorted the sets.
 *    This would result in one batch OID assignment calls rather than
 *    one for each set.  Unfortuantely, that's a little more complicated as
 *    it requires some new traversal code.  Someday . . .
 */
static int
optimize_sets (SM_CLASS * class_, MOBJ volatile obj)
{
  int error = NO_ERROR;
  SM_ATTRIBUTE *att;
  SETOBJ *col;

  for (att = class_->attributes; att != NULL && error == NO_ERROR; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (TP_IS_SET_TYPE (TP_DOMAIN_TYPE (att->domain)))
	{
	  col = *((SETOBJ **) (obj + att->offset));

	  if (col)
	    {
	      /*
	       * col now has the collection pointer.
	       * sort it before we flush. The sort operation will produce
	       * batched permanent oid's on demand if needed.
	       */
	      error = setobj_sort (col);
	    }
	}
    }
  return error;
}


/*
 * tf_make_fixup - This initializes a new transformer fixup structure for use.
 *    return: fixup structure
 * Note:
 *    It doesn't actually allocate the LC_OIDSET until we find that
 *    we have references.
 */
static OR_FIXUP *
tf_make_fixup (void)
{
  OR_FIXUP *fix;

  fix = (OR_FIXUP *) malloc (sizeof (OR_FIXUP));
  if (fix != NULL)
    {
      fix->oidset = NULL;
    }
  return fix;
}


/*
 * tf_free_fixup - frees a fixup structure create by tf_make_fixup.
 *    return: none
 *    fix(out): fixup structure
 * Note:
 *    This now consists of a single LC_OIDSET structure with
 *    OR_TEMPOID structure embedded within it.
 *    Since locator_free_oid_set doesn't know anything about our appendages,
 *    we have to walk the LC_OIDSET and free them first.
 *
 */
static void
tf_free_fixup (OR_FIXUP * fix)
{
  LC_CLASS_OIDSET *class_;
  LC_OIDMAP *oid;
  OR_TEMPOID *temp;

  if (fix == NULL)
    {
      return;
    }

  if (fix->oidset == NULL)
    {
      free_and_init (fix);
      return;
    }
  /* free our OR_TEMPOID warts */
  for (class_ = fix->oidset->classes; class_ != NULL; class_ = class_->next)
    {
      for (oid = class_->oids; oid != NULL; oid = oid->next)
	{
	  if (oid->client_data)
	    {
	      temp = (OR_TEMPOID *) oid->client_data;
	      free_and_init (temp->references);
	      free_and_init (temp);
	      oid->client_data = NULL;
	    }
	}
    }

  /* now free the LC_OIDSET hierarchy */
  locator_free_oid_set (NULL, fix->oidset);
  free_and_init (fix);
}



#define TF_FIXUP_REFERENCE_QUANT 32
/*
 * tf_add_fixup - This adds a fixup "relocation" entry for a temporary OID
 * that's been encountered.
 *    return: error
 *    fix(in/out): fixup state
 *    obj(in): object with temporary OID
 *    ref(in): address of packed reference
 */
static int
tf_add_fixup (OR_FIXUP * fix, DB_OBJECT * obj, void *ref)
{
  LC_OIDMAP *o;
  OR_TEMPOID *t;
  size_t buf_size;

  /* get the oidset we're building */
  if (fix->oidset == NULL)
    {
      fix->oidset = locator_make_oid_set ();
      if (fix->oidset == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}
    }

  o = locator_add_oidset_object (fix->oidset, obj);
  if (o == NULL)
    {
      assert (er_errid () != NO_ERROR);
      return er_errid ();
    }

  /* check our references list */
  t = (OR_TEMPOID *) o->client_data;
  if (t == NULL)
    {
      /* this is a new OID, add our reference wart */
      t = (OR_TEMPOID *) malloc (sizeof (OR_TEMPOID));
      if (t == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (OR_TEMPOID));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      buf_size = sizeof (void *) * TF_FIXUP_REFERENCE_QUANT;
      t->references = (void **) malloc (buf_size);
      if (t->references == NULL)
	{
	  free_and_init (t);

	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
      t->refsize = TF_FIXUP_REFERENCE_QUANT;
      t->references[0] = ref;
      t->refcount = 1;
      o->client_data = (void *) t;
    }
  else
    {
      /* we've already had at least one reference, add another one */
      if (t->refcount >= t->refsize)
	{
	  /* its time to grow */
	  t->refsize = t->refcount + TF_FIXUP_REFERENCE_QUANT;
	  buf_size = t->refsize * sizeof (void *);
	  t->references = (void **) realloc (t->references, buf_size);
	  if (t->references == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, buf_size);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }
	}
      t->references[t->refcount] = ref;
      t->refcount++;
    }

  return NO_ERROR;
}


/*
 * tf_need_permanent_oid - called when a temporary OID is encountered during
 * various packing places to ensure permanent oid assignment
 *    return: oid to store
 *    buf(in/out): packing buffer
 *    obj(in): object to examine
 * Note:
 *    This is called by the various packing functions when they encounter
 *    a temporary OID that needs to be packed.  If this is an INSTANCE OID,
 *    and we're maintaining a deferred "fixup" buffer, then we return a NULL
 *    oid and add an entry to the fixup state.
 *
 *    If this is a CLASS OID, or if we're not maintaining a fixup buffer,
 *    then we simply call locator_assign_permanent_oid to immeidately get a
 *    permanent OID.
 *
 *    The oid returned by this function does not need to be freed but it
 *    must be used or copied immediately.
 *
 *    NULL is returned on error.
 */
OID *
tf_need_permanent_oid (or_buf * buf, DB_OBJECT * obj)
{
  OID *oidp;

  oidp = NULL;

  /*
   * if we have a fixup buffer, and this is NOT a class object, then make
   * an entry for it.
   */
  if (tf_Allow_fixups && buf->fixups != NULL && obj->class_mop != NULL && obj->class_mop != sm_Root_class_mop)
    {

      if (tf_add_fixup (buf->fixups, obj, buf->ptr) != NO_ERROR)
	{
	  or_abort (buf);
	}
      else
	{
	  /* leave the NULL oid in the buffer until we can fix it */
	  oidp = (OID *) (&oid_Null_oid);
	}
    }
  else
    {
      /*
       * couldn't make a fixup buffer entry, go to the server and assign
       * it in the usual way.
       */
      if (locator_assign_permanent_oid (obj) == NULL)
	{
	  if (er_errid () == NO_ERROR)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_CANT_ASSIGN_OID, 0);
	    }

	  /* this is serious */
	  or_abort (buf);
	}
      else
	{
	  oidp = WS_OID (obj);
	}
    }
  return oidp;
}


/*
 * fixup_callback - LC_OIDMAP_CALLBACK function passed to locator_assign_oidset
 * by tf_do_fixup function.
 *    return: none
 *    oidmap(in): oidmap being processed
 * Note:
 *    This function gets called for each oid that has been made permanent,
 *    here we get our client appendage out of the client_data field, then
 *    whip through the references list so that they can be properly updated.
 */
static void
fixup_callback (LC_OIDMAP * oidmap)
{
  OR_TEMPOID *t;
  int i;

  t = (OR_TEMPOID *) oidmap->client_data;
  if (t != NULL)
    {
      /* fix the packed references */
      for (i = 0; i < t->refcount; i++)
	{
	  OR_PUT_OID (t->references[i], &(oidmap->oid));
	}
    }
}


/*
 * tf_do_fixup - Process the fixup operations left over after a transformation.
 *    return: error code
 *    fix(): fixup state
 */
static int
tf_do_fixup (OR_FIXUP * fix)
{
  int error = NO_ERROR;

  if (fix != NULL && fix->oidset != NULL)
    {
      error = locator_assign_oidset (fix->oidset, fixup_callback);
    }

  return error;
}


/*
 * put_varinfo - Write the variable attribute offset table for an instance.
 *    return:  NO_ERROR
 *    buf(in/out): translation buffer
 *    obj(in): instance memory
 *    class(in): class of instance
 */
static int
put_varinfo (OR_BUF * buf, char *obj, SM_CLASS * class_, int offset_size)
{
  SM_ATTRIBUTE *att;
  char *mem;
  int a, offset, len;
  int rc = NO_ERROR;

  if (class_->variable_count)
    {
      /* compute the variable offsets relative to the end of the header (beginning of variable table) */
      offset =
	OR_VAR_TABLE_SIZE_INTERNAL (class_->variable_count,
				    offset_size) + class_->fixed_size + OR_BOUND_BIT_BYTES (class_->fixed_count);

      for (a = class_->fixed_count; a < class_->att_count; a++)
	{
	  att = &class_->attributes[a];
	  mem = obj + att->offset;

	  len = att->domain->type->get_disk_size_of_mem (mem, att->domain);

	  or_put_offset_internal (buf, offset, offset_size);
	  offset += len;
	}

      or_put_offset_internal (buf, offset, offset_size);
      buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
    }
  return rc;
}


/*
 * object_size - Calculates the amount of disk storage required for an instance.
 *    return: bytes of disk storage required
 *    class(in): class structure
 *    obj(in): instance to examine
 */
static int
object_size (SM_CLASS * class_, MOBJ obj, int *offset_size_ptr)
{
  SM_ATTRIBUTE *att;
  char *mem;
  int a, size;

  *offset_size_ptr = OR_BYTE_SIZE;

re_check:

  size = OR_MVCC_INSERT_HEADER_SIZE + class_->fixed_size + OR_BOUND_BIT_BYTES (class_->fixed_count);

  if (class_->variable_count)
    {
      size += OR_VAR_TABLE_SIZE_INTERNAL (class_->variable_count, *offset_size_ptr);
      for (a = class_->fixed_count; a < class_->att_count; a++)
	{
	  att = &class_->attributes[a];
	  mem = obj + att->offset;

	  size += att->domain->type->get_disk_size_of_mem (mem, att->domain);
	}
    }

  if (*offset_size_ptr == OR_BYTE_SIZE && size > OR_MAX_BYTE)
    {
      *offset_size_ptr = OR_SHORT_SIZE;	/* 2byte */
      goto re_check;
    }
  if (*offset_size_ptr == OR_SHORT_SIZE && size > OR_MAX_SHORT)
    {
      *offset_size_ptr = BIG_VAR_OFFSET_SIZE;	/* 4byte */
      goto re_check;
    }
  return (size);
}


/*
 * put_attributes - Write the fixed and variable attribute values.
 *    return: on overflow, or_overflow will call longjmp and jump to the
 *    outer caller
 *    buf(in/out): translation buffer
 *    obj(in): instance memory
 *    class(in): class structure
 * Note:
 *    The object  header and offset table will already have been written.
 *    Assumes that the schema manager is smart enough to keep the
 *    attributes in their appropriate disk ordering.
 */
static int
put_attributes (OR_BUF * buf, char *obj, SM_CLASS * class_)
{
  SM_ATTRIBUTE *att;
  char *start;
  int pad;

  /*
   * write fixed attribute values, if unbound, leave zero or garbage
   * it doesn't really matter.
   */
  start = buf->ptr;
  for (att = class_->attributes; att != NULL && !att->type->variable_p; att = (SM_ATTRIBUTE *) att->header.next)
    {
      att->type->data_writemem (buf, obj + att->offset, att->domain);
    }

  /* bring the end of the fixed width block up to proper alignment */
  pad = (int) (buf->ptr - start);
  if (pad < class_->fixed_size)
    {
      or_pad (buf, class_->fixed_size - pad);
    }
  else if (pad > class_->fixed_size)
    {				/* mismatched fixed block calculations */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SM_CORRUPTED, 0);
      or_abort (buf);
    }

  /* write the bound bits */
  if (class_->fixed_count)
    {
      or_put_data (buf, obj + OBJ_HEADER_BOUND_BITS_OFFSET, OR_BOUND_BIT_BYTES (class_->fixed_count));
    }
  /* write the variable attributes */

  for (; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      att->type->data_writemem (buf, obj + att->offset, att->domain);
    }
  return NO_ERROR;
}


/*
 * tf_mem_to_disk - Translate an instance from its memory format into the
 *                  disk format.
 *    return: zero on success, non-zero on error
 *    classmop(in): class of this instance
 *    classobj(in): class structure
 *    obj(in/out): instance memory to translate
 *    record(out): destination disk record
 *    index_flag(out): set if there are indexes on any attributes
 * Note:
 *    If there was an overflow error on the given record, we determine
 *    the required size and return this size as a negative number, otherwise
 *    a zero is returned to indicate successful translation.
 *    Note that volatile is used here to prevent the compiler from
 *    optimizing variables into registers that aren't preserved by
 *    setjmp/longjmp.
 */
TF_STATUS
tf_mem_to_disk (MOP classmop, MOBJ classobj, MOBJ volatile obj, RECDES * record, bool * index_flag)
{
  OR_BUF orep, *buf;
  SM_CLASS *volatile class_;	/* prevent register optimization */
  int chn;
  bool has_index = false;
  unsigned int repid_bits;
  TF_STATUS status;
  volatile int expected_size;
  volatile int offset_size;

  buf = &orep;
  or_init (buf, record->data, record->area_size);
  buf->error_abort = 1;

  if (tf_Allow_fixups)
    {
      buf->fixups = tf_make_fixup ();
      if (buf->fixups == NULL)
	{
	  return TF_ERROR;
	}
    }

  class_ = (SM_CLASS *) classobj;

  if (optimize_sets (class_, obj) != NO_ERROR)
    {
      if (tf_Allow_fixups)
	{
	  tf_free_fixup (buf->fixups);
	}
      return TF_ERROR;
    }

  expected_size = object_size (class_, obj, (int *) &offset_size);
  if ((expected_size + OR_MVCC_MAX_HEADER_SIZE - OR_MVCC_INSERT_HEADER_SIZE) > record->area_size)
    {
      record->length = -expected_size;

      /* make sure we free this */
      if (tf_Allow_fixups)
	{
	  tf_free_fixup (buf->fixups);
	}

      *index_flag = false;
      return (TF_OUT_OF_SPACE);
    }

  switch (_setjmp (buf->env))
    {
    case 0:
      status = TF_SUCCESS;

      if (OID_ISTEMP (WS_OID (classmop)))
	{
	  /*
	   * since this isn't a mem_oid, can't rely on write_oid to do this,
	   * don't bother making this part of the deferred fixup stuff yet.
	   */
	  if (locator_assign_permanent_oid (classmop) == NULL)
	    {
	      if (er_errid () == NO_ERROR)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_CANT_ASSIGN_OID, 0);
		}

	      or_abort (buf);
	    }
	}

      /* header */

      repid_bits = class_->repid;
      if (class_->fixed_count)
	{
	  repid_bits |= OR_BOUND_BIT_FLAG;
	}
      /* offset size */
      OR_SET_VAR_OFFSET_SIZE (repid_bits, offset_size);

      chn = WS_CHN (obj) + 1;

      /* in most of the cases, we expect the MVCC header of a new object to have OR_MVCC_FLAG_VALID_INSID flag,
       * repid_bits, MVCC insert id and chn. This header may be changed later, at insert/update. So, we must be sure
       * that the record has enough free space. */

      repid_bits |= (OR_MVCC_FLAG_VALID_INSID << OR_MVCC_FLAG_SHIFT_BITS);
      or_put_int (buf, repid_bits);
      or_put_int (buf, chn);	/* CHN, short size */
      or_put_bigint (buf, MVCCID_NULL);	/* MVCC insert id */

      /* variable info block */
      put_varinfo (buf, obj, class_, offset_size);

      /* attributes, fixed followed by variable */
      put_attributes (buf, obj, class_);

      record->length = (int) (buf->ptr - buf->buffer);

      /* see if there are any indexes */
      has_index = classobj_class_has_indexes (class_);

      /*
       * assign permanent OID's and make the necessary adjustments in
       * the packed buffer.
       */
      if (tf_do_fixup (buf->fixups) != NO_ERROR)
	{
	  status = TF_ERROR;
	}

      /*
       * Now that we know the object has been packed up safely, it's safe
       * to update the coherency number of the in-memory image.
       */
      WS_CHN (obj) = chn;

      break;

      /* if the longjmp status was anything other than ER_TF_BUFFER_OVERFLOW, it represents an error condition and
       * er_set will have been called */
    case ER_TF_BUFFER_OVERFLOW:
      status = TF_OUT_OF_SPACE;
      record->length = -expected_size;
      has_index = false;
      break;

    default:
      status = TF_ERROR;
      has_index = false;
      break;
    }

  /* make sure we free this */
  if (tf_Allow_fixups)
    {
      tf_free_fixup (buf->fixups);
    }

  *index_flag = has_index;

  buf->error_abort = 0;
  return (status);
}


/*
 * get_current - Loads an instance from disk using the latest class
 * representation.
 *    return: new instance memory
 *    buf(in/out): translation buffer
 *    class(): class of this instance
 *    obj_ptr(out): loaded object (same as return value)
 *    bound_bit_flag(in): initial status of bound bits
 */
static char *
get_current (OR_BUF * buf, SM_CLASS * class_, MOBJ * obj_ptr, int bound_bit_flag, int offset_size)
{
  SM_ATTRIBUTE *att;
  char *obj, *mem, *start;
  int *vars, rc = NO_ERROR;
  int i, j, offset, offset2, pad;

  obj = NULL;
  /* need nicer way to store these */
  vars = NULL;
  if (class_->variable_count)
    {
      vars = (int *) db_ws_alloc (sizeof (int) * class_->variable_count);
      if (vars == NULL)
	{
	  or_abort (buf);
	}
      else
	{
	  /* get the offsets relative to the end of the header (beginning of variable table) */
	  offset = or_get_offset_internal (buf, &rc, offset_size);
	  for (i = 0; i < class_->variable_count; i++)
	    {
	      offset2 = or_get_offset_internal (buf, &rc, offset_size);
	      vars[i] = offset2 - offset;
	      offset = offset2;
	    }
	  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
	}
    }

  /*
   * if there are no bound bits on disk, allocate the instance block
   * with all the bits turned on, we have to assume that the values
   * are non-null
   */
  obj = *obj_ptr = obj_alloc (class_, (bound_bit_flag) ? 0 : 1);

  if (obj == NULL)
    {
      if (vars != NULL)
	{
	  db_ws_free (vars);
	}
      or_abort (buf);
    }
  else
    {
      /* fixed */
      start = buf->ptr;
      for (i = 0; i < class_->fixed_count; i++)
	{
	  att = &(class_->attributes[i]);
	  mem = obj + att->offset;
	  att->type->data_readmem (buf, mem, att->domain, -1);
	}

      /* round up to a to the end of the fixed block */
      pad = (int) (buf->ptr - start);
      if (pad < class_->fixed_size)
	{
	  or_advance (buf, class_->fixed_size - pad);
	}
      /* bound bits, if not present, we have to assume everything is bound */
      if (bound_bit_flag && class_->fixed_count)
	{
	  or_get_data (buf, obj + OBJ_HEADER_BOUND_BITS_OFFSET, OR_BOUND_BIT_BYTES (class_->fixed_count));
	}

      /* variable */
      if (vars != NULL)
	{
	  for (i = class_->fixed_count, j = 0; i < class_->att_count && j < class_->variable_count; i++, j++)
	    {
	      att = &(class_->attributes[i]);
	      mem = obj + att->offset;
	      att->type->data_readmem (buf, mem, att->domain, vars[j]);
	    }
	}
    }

  if (vars != NULL)
    {
      db_ws_free (vars);
    }

  return (obj);
}


/*
 * find_current_attribute - Given an instance attribute id, find the
 * corresponding attribute in the latest representation.
 *    return: pointer to current attribute. NULL if not found
 *    class(in): class structure
 *    id(in): attribute id
 */
static SM_ATTRIBUTE *
find_current_attribute (SM_CLASS * class_, int id)
{
  SM_ATTRIBUTE *found, *att;

  found = NULL;
  for (att = class_->attributes; att != NULL && found == NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      if (att->id == id)
	{
	  found = att;
	}
    }
  return (found);
}


/*
 * clear_new_unbound - This initializes the space created for instance
 * attributes that had no saved values on disk.
 *    return: void
 *    obj(in/out): instance memory
 *    class(in): class of this instance
 *    oldrep(in): old representation (of instance on disk)
 * Note:
 *    This happens when an object is converted to a new representation
 *    that had one or more attributes added.
 *    It might be more efficient to do this for all fields when the instance
 *    memory is first allocated ?
 *    The original_value is used if it has a value.
 */
static void
clear_new_unbound (char *obj, SM_CLASS * class_, SM_REPRESENTATION * oldrep)
{
  SM_ATTRIBUTE *att;
  SM_REPR_ATTRIBUTE *rat, *found;
  char *mem;

  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      found = NULL;
      for (rat = oldrep->attributes; rat != NULL && found == NULL; rat = rat->next)
	{
	  if (rat->attid == att->id)
	    {
	      found = rat;
	    }
	}
      if (found == NULL)
	{
	  mem = obj + att->offset;
	  /* initialize in case there isn't an initial value */
	  att->type->initmem (mem, att->domain);
	  if (!DB_IS_NULL (&att->default_value.original_value))
	    {
	      /* assign the initial value, should check for non-existance ? */
	      att->type->setmem (mem, att->domain, &att->default_value.original_value);
	      if (!att->type->variable_p)
		{
		  OBJ_SET_BOUND_BIT (obj, att->storage_order);
		}
	    }
	}
    }
}


/*
 * get_old - This creates an instance from an obsolete disk representation.
 *    return: new instance memory
 *    buf(in/out): translation buffer
 *    class(in): class of this instance
 *    obj_ptr(out): return value
 *    repid(in): old representation id
 *    bound_bit_flag(in): initial status of bound bits
 * Note:
 *    Any new attributes that had no values on disk are given a starting
 *    value from the "original_value" field in the class.
 *    Values for deleted attributes are discarded.
 */
static char *
get_old (OR_BUF * buf, SM_CLASS * class_, MOBJ * obj_ptr, int repid, int bound_bit_flag, int offset_size)
{
  SM_REPRESENTATION *oldrep;
  SM_REPR_ATTRIBUTE *rat;
  SM_ATTRIBUTE **attmap;
  PR_TYPE *type;
  char *obj, *bits, *start;
  int *vars = NULL, rc = NO_ERROR;
  int i, total, offset, offset2, bytes, att_index, padded_size, fixed_size;

  obj = NULL;
  oldrep = classobj_find_representation (class_, repid);

  if (oldrep == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_INVALID_REPRESENTATION, 1, sm_ch_name ((MOBJ) class_));
    }
  else
    {
      /*
       * if there are no bound bits on disk, allocate the instance block
       * with all the bits turned on, we have to assume that the values
       * are non-null
       */
      obj = *obj_ptr = obj_alloc (class_, (bound_bit_flag) ? 0 : 1);

      if (obj == NULL)
	{
	  or_abort (buf);
	}
      else
	{
	  /*
	   * read the variable offset table, can't we just leave this on
	   * disk ?
	   */
	  vars = NULL;
	  if (oldrep->variable_count)
	    {
	      vars = (int *) db_ws_alloc (sizeof (int) * oldrep->variable_count);
	      if (vars == NULL)
		{
		  or_abort (buf);
		  return NULL;
		}
	      else
		{
		  /* get the offsets relative to the end of the header (beginning of variable table) */
		  offset = or_get_offset_internal (buf, &rc, offset_size);
		  for (i = 0; i < oldrep->variable_count; i++)
		    {
		      offset2 = or_get_offset_internal (buf, &rc, offset_size);
		      vars[i] = offset2 - offset;
		      offset = offset2;
		    }
		  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
		}
	    }

	  /* calculate an attribute map */
	  total = oldrep->fixed_count + oldrep->variable_count;
	  attmap = NULL;
	  if (total > 0)
	    {
	      attmap = (SM_ATTRIBUTE **) db_ws_alloc (sizeof (SM_ATTRIBUTE *) * total);
	      if (attmap == NULL)
		{
		  if (vars != NULL)
		    {
		      db_ws_free (vars);
		    }
		  or_abort (buf);
		  return NULL;
		}
	      else
		{
		  for (rat = oldrep->attributes, i = 0; rat != NULL; rat = rat->next, i++)
		    {
		      attmap[i] = find_current_attribute (class_, rat->attid);
		    }
		}
	    }

	  /* read the fixed attributes */
	  rat = oldrep->attributes;
	  start = buf->ptr;
	  for (i = 0; i < oldrep->fixed_count && rat != NULL && attmap != NULL; i++, rat = rat->next)
	    {
	      type = pr_type_from_id (rat->typeid_);
	      if (type == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_INVALID_REPRESENTATION, 1,
			  sm_ch_name ((MOBJ) class_));

		  db_ws_free (attmap);
		  if (vars != NULL)
		    {
		      db_ws_free (vars);
		    }

		  or_abort (buf);
		  return NULL;
		}

	      if (attmap[i] == NULL)
		{
		  type->data_readmem (buf, NULL, rat->domain, -1);
		}
	      else
		{
		  type->data_readmem (buf, obj + attmap[i]->offset, rat->domain, -1);
		}
	    }

	  fixed_size = (int) (buf->ptr - start);
	  padded_size = DB_ATT_ALIGN (fixed_size);
	  or_advance (buf, (padded_size - fixed_size));


	  /*
	   * sigh, we now have to process the bound bits in much the same way
	   * as the attributes above, it would be nice if these could be done
	   * in parallel but we don't have the fixed size of the old
	   * representation so we can't easily sneak the buffer pointer
	   * forward, work on this someday
	   */

	  if (bound_bit_flag && oldrep->fixed_count)
	    {
	      bits = buf->ptr;
	      bytes = OR_BOUND_BIT_BYTES (oldrep->fixed_count);
	      if ((buf->ptr + bytes) > buf->endptr)
		{
		  or_overflow (buf);
		}

	      rat = oldrep->attributes;
	      for (i = 0; i < oldrep->fixed_count && rat != NULL && attmap != NULL; i++, rat = rat->next)
		{
		  if (attmap[i] != NULL)
		    {
		      if (OR_GET_BOUND_BIT (bits, i))
			{
			  OBJ_SET_BOUND_BIT (obj, attmap[i]->storage_order);
			}
		    }
		}
	      or_advance (buf, bytes);
	    }

	  /* variable */
	  if (vars != NULL)
	    {
	      for (i = 0; i < oldrep->variable_count && rat != NULL && attmap != NULL; i++, rat = rat->next)
		{
		  type = pr_type_from_id (rat->typeid_);
		  if (type == NULL)
		    {
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_INVALID_REPRESENTATION, 1,
			      sm_ch_name ((MOBJ) class_));

		      db_ws_free (attmap);
		      db_ws_free (vars);

		      or_abort (buf);
		      return NULL;
		    }

		  att_index = oldrep->fixed_count + i;
		  if (attmap[att_index] == NULL)
		    {
		      type->data_readmem (buf, NULL, rat->domain, vars[i]);
		    }
		  else
		    {
		      type->data_readmem (buf, obj + attmap[att_index]->offset, rat->domain, vars[i]);
		    }
		}
	    }

	  /* should be optimizing this operation ! */
	  clear_new_unbound (obj, class_, oldrep);

	  if (attmap != NULL)
	    {
	      db_ws_free (attmap);
	    }
	  if (vars != NULL)
	    {
	      db_ws_free (vars);
	    }
	}
    }

  return (obj);
}


/*
 * tf_disk_to_mem - Interface function for transforming the disk
 * representation of an instance into the memory representation.
 *    return: MOBJ
 *    classobj(in): class structure
 *    record(in): disk record
 *    convertp(in): set to non-zero if the object had to be converted from
 *                  an obsolete disk representation.
 * Note:
 *    It is imperitive that garbage collection be disabled during this
 *    operation.
 *    This is because the structure being built may contain pointers
 *    to MOPs but it has not itself been attached to its own MOP yet so it
 *    doesn't serve as a gc root.   Make sure the caller
 *    calls ws_cache() immediately after we return so the new object
 *    gets attached as soon as possible.  We must also have already
 *    allocated the MOP in the caller so that we don't get one of the
 *    periodic gc's when we attempt to allocate a new MOP.
 *    This dependency should be isolated in a ws_ function called by
 *    the locator.
 */
MOBJ
tf_disk_to_mem (MOBJ classobj, RECDES * record, int *convertp)
{
  OR_BUF orep, *buf;
  SM_CLASS *class_ = NULL;
  char *obj = NULL;
  unsigned int repid_bits;
  int repid, convert, chn, bound_bit_flag;
  int rc = NO_ERROR;
  int offset_size;
  char mvcc_flags;

  buf = &orep;
  or_init (buf, record->data, record->length);
  buf->error_abort = 1;

  switch (_setjmp (buf->env))
    {
    case 0:
      class_ = (SM_CLASS *) classobj;
      obj = NULL;
      convert = 0;
      /* offset size */
      offset_size = OR_GET_OFFSET_SIZE (buf->ptr);

      /* in case of MVCC, repid_bits contains MVCC flags */
      repid_bits = or_mvcc_get_repid_and_flags (buf, &rc);
      repid = repid_bits & OR_MVCC_REPID_MASK;

      mvcc_flags = (char) ((repid_bits >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK);

      chn = or_get_int (buf, &rc);

      if (mvcc_flags & OR_MVCC_FLAG_VALID_INSID)
	{
	  /* skip insert id */
	  or_advance (buf, OR_MVCCID_SIZE);
	}

      if (mvcc_flags & OR_MVCC_FLAG_VALID_DELID)
	{
	  /* skip delete id */
	  or_advance (buf, OR_MVCCID_SIZE);
	}

      if (mvcc_flags & OR_MVCC_FLAG_VALID_PREV_VERSION)
	{
	  /* skip prev version lsa */
	  or_advance (buf, OR_MVCC_PREV_VERSION_LSA_SIZE);
	}

      bound_bit_flag = repid_bits & OR_BOUND_BIT_FLAG;

      if (repid == class_->repid)
	{
	  obj = get_current (buf, class_, &obj, bound_bit_flag, offset_size);
	}
      else
	{
	  obj = get_old (buf, class_, &obj, repid, bound_bit_flag, offset_size);
	  convert = 1;
	}

      /* set the chn of the object */
      if (obj != NULL)
	{
	  WS_CHN (obj) = chn;
	}

      *convertp = convert;
      break;

    default:
      /* make sure to clear the object that was being created, an appropriate error will have been set */
      if (obj != NULL)
	{
	  obj_free_memory (class_, obj);
	  obj = NULL;
	}
      break;
    }

  buf->error_abort = 0;
  return (obj);
}

/*
 * unpack_allocator - memory allocator
 *    return: memory allocated
 *    size(in): size to allocate
 * Note:
 *    Used in cases where an allocatino function needs to be passed
 *    to one of the transformation routines.
 */
static char *
unpack_allocator (int size)
{
  return ((char *) malloc (size));
}


/*
 * read_var_table -  extracts the variable offset table from the disk
 * representation and build a more convenient memory representation.
 *    return: array of OR_VARINFO structures
 *    buf(in/out): translation buffr
 *    nvars(in): expected number of variables
 */
static OR_VARINFO *
read_var_table (OR_BUF * buf, int nvars)
{
  return (read_var_table_internal (buf, nvars, BIG_VAR_OFFSET_SIZE));
}

/*
 * read_var_table_internal -  extracts the variable offset table from the disk
 * representation and build a more convenient memory representation.
 *    return: array of OR_VARINFO structures
 *    buf(in/out): translation buffr
 *    nvars(in): expected number of variables
 */
static OR_VARINFO *
read_var_table_internal (OR_BUF * buf, int nvars, int offset_size)
{
  return (or_get_var_table_internal (buf, nvars, unpack_allocator, offset_size));
}


/*
 * free_var_table - frees a table allocated by read_var_table
 *    return: void
 *    vars(out): variable table
 */
static void
free_var_table (OR_VARINFO * vars)
{
  if (vars != NULL)
    {
      free_and_init (vars);
    }
}


/*
 * string_disk_size - calculate the disk size of a NULL terminated ASCII
 * string that is supposed to be stored as a VARNCHAR attribute in one of
 * the various class objects.
 *    return: byte size for packed "varchar" string
 *    string(in):
 */
static int
string_disk_size (const char *string)
{
  DB_VALUE value;
  int str_length = 0;
  int length = 0;

  if (string)
    {
      str_length = strlen (string);
    }
  else
    {
      str_length = 0;
    }

  db_make_varnchar (&value, TP_FLOATING_PRECISION_VALUE, string, str_length, LANG_SYS_CODESET, LANG_SYS_COLLATION);
  length = tp_VarNChar.get_disk_size_of_value (&value);

  /* Clear the compressed_string of DB_VALUE */
  pr_clear_compressed_string (&value);

  return length;
}


/*
 * get_string - read a string out of the OR_BUF and return it as a NULL
 * terminated ASCII string
 *    return: string from value or NULL
 *    buf(in/out): translation buffer
 *    length(in): length of string or -1 if read variable length
 * Note:
 *    Shorthand macro to read a string out of the OR_BUF and return it
 *    as a NULL terminated ASCII string which everything stored as part
 *    of the class definition is.
 *
 *    A jmp_buf has previously been established.
 */
static char *
get_string (OR_BUF * buf, int length)
{
  DB_VALUE value;
  DB_DOMAIN my_domain;
  char *string = NULL;

  /*
   * Make sure this starts off initialized so "readval" won't try to free
   * any existing contents.
   */
  db_make_null (&value);

  /*
   * The domain here is always a server side VARNCHAR.  Set a temporary
   * domain to reflect this.
   */
  my_domain.precision = DB_MAX_VARNCHAR_PRECISION;

  my_domain.codeset = lang_charset ();
  my_domain.collation_id = LANG_SYS_COLLATION;
  my_domain.collation_flag = TP_DOMAIN_COLL_NORMAL;

  tp_VarNChar.data_readval (buf, &value, &my_domain, length, false, NULL, 0);

  if (DB_VALUE_TYPE (&value) == DB_TYPE_VARNCHAR)
    {
      string = ws_copy_string (db_get_string (&value));
    }

  db_value_clear (&value);

  return string;
}


/*
 * put_string - Put a NULL terminated ASCII string into the disk buffer in the
 * usual way.
 *    return: void
 *    buf(in/out): translation buffer
 *    string(in): string to store
 * Note:
 *    See get_string & string_disk_size for the related functions.
 */
static void
put_string (OR_BUF * buf, const char *string)
{
  DB_VALUE value;
  int str_length = 0;

  if (string)
    {
      str_length = strlen (string);
    }
  else
    {
      str_length = 0;
    }

  db_make_varnchar (&value, TP_FLOATING_PRECISION_VALUE, string, str_length, LANG_SYS_CODESET, LANG_SYS_COLLATION);
  tp_VarNChar.data_writeval (buf, &value);
  pr_clear_value (&value);
}

/*
 * SET SUPPORT FUNCTIONS
 *
 * These make it easier to read and write object and substructure sets
 * which are used often in a class's disk representation.
 *
 * SUBSTRUCTURE SET RULES
 *
 * The set header containing type and count is always present.
 * The variable offset table is present only if there are elements.
 * The substructure tag is present only if there are elements.
 *
 */

/*
 * object_set_size - Calculates the disk storage required for a set of object
 * pointers.
 *    return: byte size for a set of OIDs
 *    list(in): object list
 */
static int
object_set_size (DB_OBJLIST * list)
{
  DB_OBJLIST *l;
  int count, size;

  /* store NULL for empty set */
  if (list == NULL)
    {
      return 0;
    }

  size = 0;
  for (count = 0, l = list; l != NULL; l = l->next)
    {
      if (WS_IS_DELETED (l->op))
	{
	  continue;
	}
      count++;
    }

  if (count == 0)
    {
      return 0;
    }

  size += OR_SET_HEADER_SIZE;
  size += OR_INT_SIZE;		/* size of domain word */
  size += OR_INT_SIZE;		/* size of the built-in object domain */
  size += (count * tp_Object.disksize);

  return (size);
}

/*
 * or_pack_mop - write an OID to a disk representation buffer given a MOP
 * instead of a WS_MEMOID.
 *    return:
 *    buf(): transformer buffer
 *    mop(): mop to transform
 * Note:
 *    mr_write_object can't be used because it takes a WS_MEMOID as is the
 *    case for object references in instances.
 *    This must stay in sync with mr_write_object above !
 */
static void
or_pack_mop (OR_BUF * buf, MOP mop)
{
  DB_VALUE value;

  tp_Object.initval (&value, 0, 0);
  db_make_object (&value, mop);
  tp_Object.data_writeval (buf, &value);
  tp_Object.setval (&value, NULL, false);
}

/*
 * put_object_set - Translates a list objects into the disk representation of a
 * sequence of objects
 *    return: on overflow, or_overflow will call longjmp and jump to the outer
 *            caller
 *    buf(in/out): translation buffer
 *    list(in): object list
 */
static int
put_object_set (OR_BUF * buf, DB_OBJLIST * list)
{
  DB_OBJLIST *l;
  int count;

  /* store NULL for empty set */
  if (list == NULL)
    return ER_FAILED;

  for (count = 0, l = list; l != NULL; l = l->next)
    {
      if (WS_IS_DELETED (l->op))
	{
	  continue;
	}
      count++;
    }

  if (count == 0)
    {
      return NO_ERROR;
    }

  /* w/ domain, no bound bits, no offsets, no tags, no substructure header */
  or_put_set_header (buf, DB_TYPE_SEQUENCE, count, 1, 0, 0, 0, 0);

  /* use the generic "object" domain */
  or_put_int (buf, OR_INT_SIZE);	/* size of the domain */
  or_put_domain (buf, &tp_Object_domain, 0, 0);	/* actual domain */

  /* should be using something other than or_pack_mop here ! */
  for (l = list; l != NULL; l = l->next)
    {
      if (WS_IS_DELETED (l->op))
	{
	  continue;
	}
      or_pack_mop (buf, l->op);
    }

  return NO_ERROR;
}

/*
 * get_object_set - Extracts a sequence of objects in a disk representation
 * and converts it into an object list in memory.
 *    return: object list  NOTE: a jmp_buf has previously been established.
 *    buf(in/out): translation buffer
 *    expected(in): expected length
 */
static DB_OBJLIST *
get_object_set (OR_BUF * buf, int expected)
{
  DB_OBJLIST *list;
  MOP op;
  DB_VALUE value;
  int count, i;

  /* handle NULL case, variable width will be zero */
  if (expected == 0)
    {
      return NULL;
    }

  list = NULL;
  count = or_skip_set_header (buf);
  for (i = 0; i < count; i++)
    {
      /*
       * Get a MOP, could assume classes mops here and make sure the resulting
       * MOP is stamped with the sm_Root_class_mop class ?
       */
      tp_Object.data_readval (buf, &value, NULL, -1, true, NULL, 0);
      op = db_get_object (&value);
      if (op != NULL)
	{
	  if (ml_append (&list, op, NULL))
	    {
	      /* memory error */
	      or_abort (buf);
	    }
	}
    }

  return (list);
}

/*
 * substructure_set_size - Calculates the disk storage for a set created from
 * a list of memory elements.
 *    return: byte of disk storage required
 *    list(in): list of elements
 *    function(in): function to calculate element sizes
 * Note:
 *    The supplied function is used to find the size of the list elements.
 *    Even if the list is empty, there will always be at least a set header
 *    written to disk.
 */
static int
substructure_set_size (DB_LIST * list, LSIZER function)
{
  DB_LIST *l;
  int size, count;

  /* store NULL for empty list */
  if (list == NULL)
    {
      return 0;
    }

  /* header + domain length word + domain */
  size = OR_SET_HEADER_SIZE + OR_INT_SIZE + OR_SUB_DOMAIN_SIZE;

  count = 0;
  for (l = list; l != NULL; l = l->next, count++)
    {
      size += (*function) (l);
    }

  if (count)
    {
      /*
       * we have elements to store, in that case we need to add the
       * common substructure header at the front and an offset table
       */
      size += OR_VAR_TABLE_SIZE (count);
      size += OR_SUB_HEADER_SIZE;
    }

  return (size);
}

/*
 * put_substructure_set - Write the disk representation of a substructure
 * set from a linked list of structures.
 *    return:  void
 *    buf(in/out): translation buffer
 *    list(in): substructure list
 *    writer(in): translation function
 *    class(in): OID of meta class for this substructure
 *    repid(in): repid for the meta class
 * Note:
 *    The supplied functions calculate the size and write the elements.
 *    The OID/repid for the metaclass will be one of the meta classes
 *    defined in the catalog.
 */
static void
put_substructure_set (OR_BUF * buf, DB_LIST * list, LWRITER writer, OID * class_, int repid)
{
  DB_LIST *l;
  char *start;
  int count;
  char *offset_ptr;

  /* store NULL for empty list */
  if (list == NULL)
    {
      return;
    }

  count = 0;
  for (l = list; l != NULL; l = l->next, count++);

  /* with domain, no bound bits, with offsets, no tags, common sub header */
  start = buf->ptr;
  or_put_set_header (buf, DB_TYPE_SEQUENCE, count, 1, 0, 1, 0, 1);

  if (!count)
    {
      /* we must at least store the domain even though there will be no elements */
      or_put_int (buf, OR_SUB_DOMAIN_SIZE);
      or_put_sub_domain (buf);
    }
  else
    {
      /* begin an offset table */
      offset_ptr = buf->ptr;
      or_advance (buf, OR_VAR_TABLE_SIZE (count));

      /* write the domain */
      or_put_int (buf, OR_SUB_DOMAIN_SIZE);
      or_put_sub_domain (buf);

      /* write the common substructure header */
      or_put_oid (buf, class_);
      or_put_int (buf, repid);
      or_put_int (buf, 0);	/* flags */

      /* write each substructure */
      for (l = list; l != NULL; l = l->next)
	{
	  /* determine the offset to the this element and put it in the table */
	  OR_PUT_OFFSET (offset_ptr, (buf->ptr - start));
	  offset_ptr += BIG_VAR_OFFSET_SIZE;
	  /* write the substructure */
	  (*writer) (buf, l);
	}
      /* write the final offset */
      OR_PUT_OFFSET (offset_ptr, (buf->ptr - start));
    }
}

/*
 * get_substructure_set - extracts a substructure set on disk and create a
 * list of memory structures.
 *    return: list of structures
 *    buf(in/out): translation buffer
 *    reader(in): function to read the elements
 *    expected(in): expected size
 * Note:
 *    It is important that the elements be APPENDED here.
 */
static DB_LIST *
get_substructure_set (OR_BUF * buf, LREADER reader, int expected)
{
  DB_LIST *list, *obj;
  int count, i;

  /* handle NULL case, variable width will be zero */
  if (expected == 0)
    {
      return NULL;
    }

  list = NULL;
  count = or_skip_set_header (buf);
  for (i = 0; i < count; i++)
    {
      obj = (*reader) (buf);
      if (obj != NULL)
	{
	  WS_LIST_APPEND (&list, obj);
	}
      else
	{
	  or_abort (buf);
	}
    }
  return (list);
}

/*
 * install_substructure_set - loads a substructure set from disk into a list
 * of memory structures.
 *    return: void
 *    buf(in/out): translation buffer
 *    list(in): threaded array of structures
 *    reader(in): function to read elements
 *    expected(in): expected size;
 * Note:
 *    The difference is this function does not allocate storage for the
 *    elements, these have been previously allocated and are simply filled in
 *    by the reader.
 */
static void
install_substructure_set (OR_BUF * buf, DB_LIST * list, VREADER reader, int expected)
{
  DB_LIST *p;
  int count, i;

  if (expected)
    {
      count = or_skip_set_header (buf);
      for (i = 0, p = list; i < count && p != NULL; i++, p = p->next)
	{
	  (*reader) (buf, p);
	}
    }
}

/*
 * property_list_size - Calculate the disk storage requirements for a
 * property list.
 *    return: byte size of property list
 *    properties(in): property list
 */
static int
property_list_size (DB_SEQ * properties)
{
  DB_VALUE value;
  int size, max;

  size = 0;
  if (properties != NULL)
    {
      /* collapse empty property sequences to NULL values on disk */
      max = set_size (properties);
      if (max)
	{
	  db_make_sequence (&value, properties);
	  size = tp_Sequence.get_disk_size_of_value (&value);
	}
    }
  return size;
}

/*
 * put_property_list - Write the disk representation of a property list.
 *    return: void
 *    buf(in/out): translation buffer
 *    properties(in): property list
 */
static void
put_property_list (OR_BUF * buf, DB_SEQ * properties)
{
  DB_VALUE value;
  int max;

  if (properties == NULL)
    {
      return;
    }
  /* collapse empty property sequences to NULL values on disk */
  max = set_size (properties);
  if (max)
    {
      db_make_sequence (&value, properties);
      tp_Sequence.data_writeval (buf, &value);
    }
}

/*
 * get_property_list - This reads a property list from disk.
 *    return: a new property list
 *    buf(in/out): translation buffer
 *    expected_size(in): number of bytes on disk
 * Note:
 *    This reads a property list from disk.
 *    If either the expected size is zero, NULL is returned.
 *    If a sequence was stored on disk but it had no elements, NULL is
 *    returned and the empty sequence is freed.  This is to handle the
 *    case where old style class objects had an empty substructure set
 *    stored at this position.  Since these will be converted to property
 *    lists, we can just ignore them.
 *
 *    A jmp_buf has previously been established.
 */
static DB_SEQ *
get_property_list (OR_BUF * buf, int expected_size)
{
  DB_VALUE value;
  DB_SEQ *properties;
  int max;

  properties = NULL;
  if (expected_size)
    {
      tp_Sequence.data_readval (buf, &value, NULL, expected_size, true, NULL, 0);
      properties = db_get_set (&value);
      if (properties == NULL)
	or_abort (buf);		/* trouble allocating a handle */
      else
	{
	  max = set_size (properties);
	  if (!max)
	    {
	      /*
	       * there is an empty sequence here, get rid of it so we don't
	       * have to carry it around
	       */
	      set_free (properties);
	      properties = NULL;
	    }
	}
    }
  return (properties);
}

/*
 * domain_size - Calculates the number of bytes required to store a domain
 * list on disk.
 *    return: disk size of domain
 *    domain(in): domain list
 */
static int
domain_size (TP_DOMAIN * domain)
{
  int size;

  size = tf_Metaclass_domain.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_domain.mc_n_variable);

  size += enumeration_size (&DOM_GET_ENUMERATION (domain));

  size += substructure_set_size ((DB_LIST *) domain->setdomain, (LSIZER) domain_size);

  size += (domain->json_validator == NULL
	   ? 0 : string_disk_size (db_json_get_schema_raw_from_validator (domain->json_validator)));

  return (size);
}


/*
 * domain_to_disk - Translates a domain list to its disk representation.
 *    return: void
 *    buf(in/out): translation buffer
 *    domain(in): domain list
 * Note:
 * Translates a domain list to its disk representation.
 */
static void
domain_to_disk (OR_BUF * buf, TP_DOMAIN * domain)
{
  char *start;
  int offset;
  DB_VALUE schema_value;

  /* safe-guard : domain collation flags should only be used for execution */
  if (TP_DOMAIN_COLLATION_FLAG (domain) != TP_DOMAIN_COLL_NORMAL)
    {
      assert (false);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  /* VARIABLE OFFSET TABLE */
  start = buf->ptr;
  offset = tf_Metaclass_domain.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_domain.mc_n_variable);

  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) domain->setdomain, (LSIZER) domain_size);

  or_put_offset (buf, offset);
  offset += enumeration_size (&DOM_GET_ENUMERATION (domain));

  or_put_offset (buf, offset);
  offset += (domain->json_validator == NULL
	     ? 0 : string_disk_size (db_json_get_schema_raw_from_validator (domain->json_validator)));

  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  or_put_int (buf, (int) TP_DOMAIN_TYPE (domain));
  or_put_int (buf, domain->precision);
  or_put_int (buf, domain->scale);
  or_put_int (buf, domain->codeset);
  or_put_int (buf, domain->collation_id);
  or_pack_mop (buf, domain->class_mop);

  put_substructure_set (buf, (DB_LIST *) domain->setdomain, (LWRITER) domain_to_disk, &tf_Metaclass_domain.mc_classoid,
			tf_Metaclass_domain.mc_repid);

  put_enumeration (buf, &DOM_GET_ENUMERATION (domain));

  if (domain->json_validator)
    {
      db_make_string (&schema_value, db_json_get_schema_raw_from_validator (domain->json_validator));
      tp_String.data_writeval (buf, &schema_value);
      pr_clear_value (&schema_value);
    }
  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }
}

/*
 * disk_to_domain2 - Create the memory representation for a domain list from
 * the disk representation.
 *    return: domain structure
 *    buf(in/out): translation buffer
 * Note:
 *    This builds a transient domain, it is called by disk_to_domain which
 *    just turns around and caches it.
 *
 *    A jmp_buf has previously been established.
 */
static TP_DOMAIN *
disk_to_domain2 (OR_BUF * buf)
{
  OR_VARINFO *vars;
  TP_DOMAIN *domain;
  DB_TYPE typeid_;
  OID oid;
  int rc = NO_ERROR;

  vars = read_var_table (buf, tf_Metaclass_domain.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
      return NULL;
    }

  typeid_ = (DB_TYPE) or_get_int (buf, &rc);

  domain = tp_domain_new (typeid_);
  if (domain == NULL)
    {
      free_var_table (vars);
      or_abort (buf);
      return NULL;
    }

  domain->precision = or_get_int (buf, &rc);
  domain->scale = or_get_int (buf, &rc);
  domain->codeset = or_get_int (buf, &rc);
  domain->collation_id = or_get_int (buf, &rc);
  if (typeid_ == DB_TYPE_ENUMERATION && domain->codeset == 0)
    {
      assert (domain->collation_id == LANG_COLL_ISO_BINARY);
      domain->codeset = INTL_CODESET_ISO88591;
    }
  /*
   * Read the domain class OID without promoting it to a MOP.
   * Could use readval, and extract the OID out of the already swizzled
   * MOP too.
   */
  tp_Oid.data_readmem (buf, &oid, NULL, -1);
  domain->class_oid = oid;

  /* swizzle the pointer, we know we're on the client here */
  if (!OID_ISNULL (&domain->class_oid))
    {
      domain->class_mop = ws_mop (&domain->class_oid, NULL);
      if (domain->class_mop == NULL)
	{
	  free_var_table (vars);
	  or_abort (buf);
	}
    }
  domain->setdomain = (TP_DOMAIN *) get_substructure_set (buf, (LREADER) disk_to_domain2,
							  vars[ORC_DOMAIN_SETDOMAIN_INDEX].length);
  domain->enumeration.collation_id = domain->collation_id;

  if (get_enumeration (buf, &DOM_GET_ENUMERATION (domain), vars[ORC_DOMAIN_ENUMERATION_INDEX].length) != NO_ERROR)
    {
      free_var_table (vars);
      tp_domain_free (domain);
      or_abort (buf);
      return NULL;
    }

  if (vars[ORC_DOMAIN_SCHEMA_JSON_OFFSET].length > 0)
    {
      char *schema_raw;
      int error_code;

      or_get_json_schema (buf, schema_raw);
      if (schema_raw == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  vars[ORC_DOMAIN_SCHEMA_JSON_OFFSET].length + 1);
	  tp_domain_free (domain);
	  free_var_table (vars);
	  return NULL;
	}

      error_code = db_json_load_validator (schema_raw, domain->json_validator);
      if (error_code != NO_ERROR)
	{
	  assert_release (false);
	  tp_domain_free (domain);
	  free_var_table (vars);
	  db_private_free_and_init (NULL, schema_raw);
	  return NULL;
	}
      db_private_free_and_init (NULL, schema_raw);
    }

  free_var_table (vars);

  return (domain);
}

/*
 * disk_to_domain - Create the memory representation for a domain list from
 * the disk representation.
 *    return: domain structure
 *    buf(in/out): translation buffer
 * Note:
 *    Calls disk_to_domain2 which builds the transient domain which we
 *    then cache when we're done.
 *    We need to separate the two operations because disk_to_domain2 is
 *    called recursively for nested domains and we don't want to
 *    cache each of those.
 */
static TP_DOMAIN *
disk_to_domain (OR_BUF * buf)
{
  TP_DOMAIN *domain;

  domain = disk_to_domain2 (buf);

  if (domain != NULL)
    {
      domain = tp_domain_cache (domain);
    }
  return domain;
}


/*
 * metharg_to_disk - Write the memory representation of a method argument to
 * disk.
 *    return: void
 *    buf(in/out): translation buffer
 *    arg(in): method argument
 * Note:
 *    Write the memory representation of a method argument to disk.
 *    On overflow, or_overflow will call longjmp and jump to the outer caller
 */
static void
metharg_to_disk (OR_BUF * buf, SM_METHOD_ARGUMENT * arg)
{
  char *start;
  int offset;

  /* VARIABLE OFFSET TABLE */
  start = buf->ptr;
  offset = tf_Metaclass_metharg.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_metharg.mc_n_variable);
  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) arg->domain, (LSIZER) domain_size);
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  /* note that arguments can be empty, in which case they are untyped */
  if (arg->type == NULL)
    {
      or_put_int (buf, (int) DB_TYPE_NULL);
    }
  else
    {
      or_put_int (buf, (int) arg->type->id);
    }

  or_put_int (buf, arg->index);

  put_substructure_set (buf, (DB_LIST *) arg->domain, (LWRITER) domain_to_disk, &tf_Metaclass_domain.mc_classoid,
			tf_Metaclass_domain.mc_repid);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }
}

/*
 * metharg_size - Calculate the byte size for the disk representation of a
 * method arg.
 *    return: disk size of method argument
 *    arg(in): argument
 */
static int
metharg_size (SM_METHOD_ARGUMENT * arg)
{
  int size;

  size = tf_Metaclass_metharg.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_metharg.mc_n_variable);
  size += substructure_set_size ((DB_LIST *) arg->domain, (LSIZER) domain_size);

  return (size);
}

/*
 * disk_to_metharg - Read the disk represenation of a method argument and
 * build the memory representation.
 *    return: new method argument
 *    buf(in/out): translation buffer
 */
static SM_METHOD_ARGUMENT *
disk_to_metharg (OR_BUF * buf)
{
  OR_VARINFO *vars;
  SM_METHOD_ARGUMENT *arg;
  DB_TYPE argtype;
  int rc = NO_ERROR;

  vars = read_var_table (buf, tf_Metaclass_metharg.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
      return NULL;
    }

  arg = classobj_make_method_arg (0);
  if (arg == NULL)
    {
      or_abort (buf);
    }
  else
    {
      argtype = (DB_TYPE) or_get_int (buf, &rc);
      if (argtype == DB_TYPE_NULL)
	{
	  arg->type = NULL;
	}
      else
	{
	  arg->type = pr_type_from_id (argtype);
	}
      arg->index = or_get_int (buf, &rc);
      arg->domain =
	(TP_DOMAIN *) get_substructure_set (buf, (LREADER) disk_to_domain, vars[ORC_METHARG_DOMAIN_INDEX].length);
    }
  free_var_table (vars);

  return (arg);
}

/*
 * methsig_to_disk - Write the disk representation of a method signature.
 *    return: void
 *    buf(in/out): translation buffer
 *    sig(in): signature
 * Note:
 * On overflow, or_overflow will call longjmp and jump to the outer caller
 *
 */
static int
methsig_to_disk (OR_BUF * buf, SM_METHOD_SIGNATURE * sig)
{
  char *start;
  int offset;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  offset = tf_Metaclass_methsig.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_methsig.mc_n_variable);

  or_put_offset (buf, offset);
  offset += string_disk_size (sig->function_name);

  or_put_offset (buf, offset);
  offset += string_disk_size (sig->sql_definition);

  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) sig->value, (LSIZER) metharg_size);

  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) sig->args, (LSIZER) metharg_size);

  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);


  /* ATTRIBUTES */
  or_put_int (buf, sig->num_args);
  put_string (buf, sig->function_name);
  put_string (buf, sig->sql_definition);

  put_substructure_set (buf, (DB_LIST *) sig->value, (LWRITER) metharg_to_disk, &tf_Metaclass_metharg.mc_classoid,
			tf_Metaclass_metharg.mc_repid);

  put_substructure_set (buf, (DB_LIST *) sig->args, (LWRITER) metharg_to_disk, &tf_Metaclass_metharg.mc_classoid,
			tf_Metaclass_metharg.mc_repid);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
methsig_to_disk_lwriter (void *buf, void *sig)
{
  (void) methsig_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_METHOD_SIGNATURE *, sig));
}

/*
 * methsig_size - Calculate the disk size of a method signature.
 *    return: disk size of signature
 *    sig(in): signature
 */
static int
methsig_size (SM_METHOD_SIGNATURE * sig)
{
  int size;

  size = tf_Metaclass_methsig.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_methsig.mc_n_variable);
  size += string_disk_size (sig->function_name);
  size += string_disk_size (sig->sql_definition);
  size += substructure_set_size ((DB_LIST *) sig->value, (LSIZER) metharg_size);
  size += substructure_set_size ((DB_LIST *) sig->args, (LSIZER) metharg_size);

  return (size);
}

/*
 * disk_to_methsig - Read the disk represenation of a method signature and
 * create the memory represenatation.
 *    return: new method signature
 *    buf(in/out): translation buffer
 */
static SM_METHOD_SIGNATURE *
disk_to_methsig (OR_BUF * buf)
{
  OR_VARINFO *vars;
  SM_METHOD_SIGNATURE *sig;
  int nargs, rc = NO_ERROR;
  const char *fname, *fix;

  sig = NULL;
  vars = read_var_table (buf, tf_Metaclass_methsig.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      nargs = or_get_int (buf, &rc);
      sig = classobj_make_method_signature (NULL);
      if (sig == NULL)
	{
	  or_abort (buf);
	}
      else
	{
	  fname = get_string (buf, vars[ORC_METHSIG_FUNCTION_NAME_INDEX].length);
	  sig->sql_definition = get_string (buf, vars[ORC_METHSIG_SQL_DEF_INDEX].length);

	  /*
	   * KLUDGE: older databases have the function name string stored with
	   * a prepended '_' character for the sun.  Now, since we don't do this
	   * until we actually have to dynamic link the function, we have to
	   * strip it off if it was stored in the old form
	   */
	  if (fname != NULL && fname[0] == '_')
	    {
	      fix = ws_copy_string (fname + 1);
	      if (fix == NULL)
		{
		  or_abort (buf);
		}
	      else
		{
		  ws_free_string (fname);
		  fname = fix;
		}
	    }
	  sig->function_name = fname;

	  sig->num_args = nargs;
	  sig->value =
	    (SM_METHOD_ARGUMENT *) get_substructure_set (buf, (LREADER) disk_to_metharg,
							 vars[ORC_METHSIG_RETURN_VALUE_INDEX].length);
	  sig->args =
	    (SM_METHOD_ARGUMENT *) get_substructure_set (buf, (LREADER) disk_to_metharg,
							 vars[ORC_METHSIG_ARGUMENTS_INDEX].length);
	}
      free_var_table (vars);
    }

  return (sig);
}

/*
 * method_to_disk - Write the disk representation of a method.
 *    return:
 *    buf(in/out): translation buffer
 *    method(in): method structure
 * Note:
 *    On overflow, or_overflow will call longjmp and jump to the outer caller
 */
static int
method_to_disk (OR_BUF * buf, SM_METHOD * method)
{
  char *start;
  int offset;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  /* name */
  offset = tf_Metaclass_method.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_method.mc_n_variable);

  or_put_offset (buf, offset);
  offset += string_disk_size (method->header.name);

  /* signature set */
  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) method->signatures, (LSIZER) methsig_size);

  /* property list */

  or_put_offset (buf, offset);
  offset += property_list_size (method->properties);

  /* end of variables */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  /* source class oid */
  or_pack_mop (buf, method->class_mop);
  or_put_int (buf, method->id);

  /* name */
  put_string (buf, method->header.name);

  /* signatures */
  put_substructure_set (buf, (DB_LIST *) method->signatures, methsig_to_disk_lwriter,
			&tf_Metaclass_methsig.mc_classoid, tf_Metaclass_methsig.mc_repid);

  put_property_list (buf, method->properties);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
method_to_disk_lwriter (void *buf, void *method)
{
  (void) method_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_METHOD *, method));
}

/*
 * method_size - Calculates the disk size of a method.
 *    return: disk size of method
 *    method(in): method structure
 */
static int
method_size (SM_METHOD * method)
{
  int size;

  size = tf_Metaclass_method.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_method.mc_n_variable);
  size += string_disk_size (method->header.name);
  size += substructure_set_size ((DB_LIST *) method->signatures, (LSIZER) methsig_size);
  size += property_list_size (method->properties);

  return (size);
}

/*
 * disk_to_method - Reads the disk representation of a method and fills in
 * the supplied method structure.
 *    return: void
 *    buf(in/out): translation buffer
 *    method(out): method structure
 * Note:
 * A jmp_buf has previously been established.
 */
static void
disk_to_method (OR_BUF * buf, SM_METHOD * method)
{
  OR_VARINFO *vars;
  DB_VALUE value;
  int rc = NO_ERROR;

  vars = read_var_table (buf, tf_Metaclass_method.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      /* this must be set later */
      method->header.name_space = ID_NULL;

      /* CLASS */
      tp_Object.data_readval (buf, &value, NULL, -1, true, NULL, 0);
      method->class_mop = db_get_object (&value);
      method->id = or_get_int (buf, &rc);
      method->function = NULL;

      /* variable attrubute 0 : name */
      method->header.name = get_string (buf, vars[ORC_METHOD_NAME_INDEX].length);

      /* variable attribute 1 : signatures */
      method->signatures =
	(SM_METHOD_SIGNATURE *) get_substructure_set (buf, (LREADER) disk_to_methsig,
						      vars[ORC_METHOD_SIGNATURE_INDEX].length);

      /* variable attribute 2 : property list */
      method->properties = get_property_list (buf, vars[ORC_METHOD_PROPERTIES_INDEX].length);

      free_var_table (vars);
    }
}

/*
 * methfile_to_disk - Write the disk representation of a method file.
 *    return: NO_ERROR, or error code
 *    buf(in/out): translation buffer
 *    file(in): method file
 * Note:
 *    on overflow, or_overflow will call longjmp and jump to the outer caller
 */
static int
methfile_to_disk (OR_BUF * buf, SM_METHOD_FILE * file)
{
  char *start;
  int offset;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  offset = tf_Metaclass_methfile.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_methfile.mc_n_variable);

  /* name */
  or_put_offset (buf, offset);
  offset += string_disk_size (file->name);

  /* property list */
  or_put_offset (buf, offset);
  /* currently no properties */
  offset += property_list_size (NULL);

  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */

  /* class */
  or_pack_mop (buf, file->class_mop);

  /* name */
  put_string (buf, file->name);

  /* property list */
  put_property_list (buf, NULL);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
methfile_to_disk_lwriter (void *buf, void *file)
{
  (void) methfile_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_METHOD_FILE *, file));
}

/*
 * methfile_size - Calculate the disk size of a method file.
 *    return: disk size of the method file
 *    file(in): method file
 */
static int
methfile_size (SM_METHOD_FILE * file)
{
  int size;

  size = tf_Metaclass_methfile.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_methfile.mc_n_variable);
  size += string_disk_size (file->name);
  size += property_list_size (NULL);

  return (size);
}

/*
 * disk_to_methfile - Read the disk representation of a method file and
 * create a new method file structure.
 *    return: new method file structure
 *    buf(in/out): translation buffer
 * Note:
 *    A jmp_buf has previously been established.
 */
static SM_METHOD_FILE *
disk_to_methfile (OR_BUF * buf)
{
  SM_METHOD_FILE *file;
  OR_VARINFO *vars;
  DB_SET *props;
  DB_VALUE value;

  file = NULL;
  vars = read_var_table (buf, tf_Metaclass_methfile.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      file = classobj_make_method_file (NULL);
      if (file == NULL)
	{
	  or_abort (buf);
	}
      else
	{
	  /* class */
	  tp_Object.data_readval (buf, &value, NULL, -1, true, NULL, 0);
	  file->class_mop = db_get_object (&value);

	  /* name */
	  file->name = get_string (buf, vars[ORC_METHFILE_NAME_INDEX].length);

	  /* properties */
	  props = get_property_list (buf, vars[ORC_METHFILE_PROPERTIES_INDEX].length);
	  /* shouldn't have any of these yet */
	  if (props != NULL)
	    {
	      set_free (props);
	    }
	}

      free_var_table (vars);
    }
  return (file);
}

/*
 * query_spec_to_disk - Write the disk representation of a virtual class
 * query_spec statement.
 *    return: NO_ERROR or error code
 *    buf(in/out): translation buffer
 *    statement(in): query_spec statement
 */
static int
query_spec_to_disk (OR_BUF * buf, SM_QUERY_SPEC * query_spec)
{
  char *start;
  int offset;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  offset = tf_Metaclass_query_spec.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_query_spec.mc_n_variable);

  or_put_offset (buf, offset);
  offset += string_disk_size (query_spec->specification);

  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  put_string (buf, query_spec->specification);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
query_spec_to_disk_lwriter (void *buf, void *query_spec)
{
  (void) query_spec_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_QUERY_SPEC *, query_spec));
}

/*
 * query_spec_size - Calculates the disk size of a query_spec statement.
 *    return: disk size of query_spec statement
 *    statement(in): query_spec statement
 */
static int
query_spec_size (SM_QUERY_SPEC * statement)
{
  int size;

  size = tf_Metaclass_query_spec.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_query_spec.mc_n_variable);
  size += string_disk_size (statement->specification);

  return (size);
}


/*
 * disk_to_query_spec - Reads the disk representation of a query_spec
 * statement and creates the memory representation.
 *    return: new query_spec structure
 *    buf(in/out): translation buffer
 */
static SM_QUERY_SPEC *
disk_to_query_spec (OR_BUF * buf)
{
  SM_QUERY_SPEC *statement;
  OR_VARINFO *vars;

  statement = NULL;
  vars = read_var_table (buf, tf_Metaclass_query_spec.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      statement = classobj_make_query_spec (NULL);
      if (statement == NULL)
	{
	  or_abort (buf);
	}
      else
	{
	  statement->specification = get_string (buf, vars[ORC_QUERY_SPEC_SPEC_INDEX].length);
	}

      free_var_table (vars);
    }
  return (statement);
}

/*
 * attribute_to_disk - Write the disk representation of an attribute.
 *    return: on overflow, or_overflow will call longjmp and                        jump to the outer caller
 *    buf(in/out): translation buffer
 *    att(in): attribute
 * Note:
 *    On overflow, or_overflow will call longjmp and jump to the outer caller
 */
static int
attribute_to_disk (OR_BUF * buf, SM_ATTRIBUTE * att)
{
  char *start;
  int offset;
  DB_OBJLIST *triggers;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  /* name */
  offset = tf_Metaclass_attribute.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_attribute.mc_n_variable);
  or_put_offset (buf, offset);

  offset += string_disk_size (att->header.name);

  /* initial value variable */
  or_put_offset (buf, offset);
  /* could avoid domain tag here ? */
  offset += or_packed_value_size (&att->default_value.value, 1, 1, 0);

  /* original value */
  or_put_offset (buf, offset);
  /* could avoid domain tag here ? */
  offset += or_packed_value_size (&att->default_value.original_value, 1, 1, 0);

  /* domain list */
  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) att->domain, (LSIZER) domain_size);

  /* trigger list */
  or_put_offset (buf, offset);
  (void) tr_get_cache_objects (att->triggers, &triggers);
  offset += object_set_size (triggers);

  /* property list */
  or_put_offset (buf, offset);
  offset += property_list_size (att->properties);

  /* comment */
  or_put_offset (buf, offset);
  offset += string_disk_size (att->comment);

  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  or_put_int (buf, att->id);
  or_put_int (buf, (int) att->type->id);
  or_put_int (buf, 0);		/* memory offsets are now calculated after loading */
  or_put_int (buf, att->order);
  or_pack_mop (buf, att->class_mop);
  or_put_int (buf, att->flags);

  /* index BTID */
  /*
   * The index member of the attribute structure has been removed.  Indexes
   * are now stored on the class property list.  We still need to store
   * something out to disk since the disk representation has not changed.
   * For now, store a NULL BTID on disk.  - JB
   */
  or_put_int (buf, NULL_FILEID);
  or_put_int (buf, NULL_PAGEID);
  or_put_int (buf, 0);

  /* name */
  put_string (buf, att->header.name);

  /* value/original value, make sure the flags match the calls to or_packed_value_size above ! */
  or_put_value (buf, &att->default_value.value, 1, 1, 0);
  or_put_value (buf, &att->default_value.original_value, 1, 1, 0);

  /* domain */
  put_substructure_set (buf, (DB_LIST *) att->domain, (LWRITER) domain_to_disk, &tf_Metaclass_domain.mc_classoid,
			tf_Metaclass_domain.mc_repid);

  /* triggers */
  put_object_set (buf, triggers);

  /* formerly att_extension_to_disk(buf, att) */
  put_property_list (buf, att->properties);

  /* comment */
  put_string (buf, att->comment);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
attribute_to_disk_lwriter (void *buf, void *att)
{
  (void) attribute_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_ATTRIBUTE *, att));
}

/*
 * attribute_size - Calculates the disk size of an attribute.
 *    return: disk size of attribute
 *    att(in): attribute
 */
static int
attribute_size (SM_ATTRIBUTE * att)
{
  DB_OBJLIST *triggers;
  int size;

  size = tf_Metaclass_attribute.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_attribute.mc_n_variable);

  size += string_disk_size (att->header.name);
  size += or_packed_value_size (&att->default_value.value, 1, 1, 0);
  size += or_packed_value_size (&att->default_value.original_value, 1, 1, 0);
  size += substructure_set_size ((DB_LIST *) att->domain, (LSIZER) domain_size);

  (void) tr_get_cache_objects (att->triggers, &triggers);
  size += object_set_size (triggers);

  /* size += att_extension_size(att); */
  size += property_list_size (att->properties);

  size += string_disk_size (att->comment);

  return (size);
}


/*
 * disk_to_attribute - Reads the disk representation of an attribute and
 * fills in the supplied attribute structure.
 *    return: void
 *    buf(in/out): translation buffer
 *    att(out): attribute structure
 * Note:
 *    A jmp_buf has previously been established.
 */
static void
disk_to_attribute (OR_BUF * buf, SM_ATTRIBUTE * att)
{
  OR_VARINFO *vars;
  int fileid;
  DB_OBJLIST *triggers;
  DB_VALUE value;
  int rc = NO_ERROR;
  DB_TYPE dbval_type;

  vars = read_var_table (buf, tf_Metaclass_attribute.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      /*
       * must be sure to initialize these, the function classobj_make_attribute
       * should be split into creation & initialization functions so we can
       * have a single function that initializes the various fields.  As it
       * stands now, any change made to the attribute structure has to be
       * initialized in classobj_make_attribute and here as well
       */
      att->header.name_space = ID_NULL;	/* must set this later ! */
      att->header.name = NULL;
      att->domain = NULL;
      att->constraints = NULL;
      att->properties = NULL;
      att->order_link = NULL;
      att->triggers = NULL;
      att->auto_increment = NULL;

      att->id = or_get_int (buf, &rc);
      dbval_type = (DB_TYPE) or_get_int (buf, &rc);
      att->type = pr_type_from_id (dbval_type);
      att->offset = or_get_int (buf, &rc);
      att->offset = 0;		/* calculated later */
      att->order = or_get_int (buf, &rc);

      tp_Object.data_readval (buf, &value, NULL, -1, true, NULL, 0);
      att->class_mop = db_get_object (&value);
      /* prevents clear on next readval call */
      db_value_put_null (&value);

      att->flags = or_get_int (buf, &rc);

      fileid = or_get_int (buf, &rc);

      /* index BTID */
      /*
       * Read the NULL BTID from disk.  There is no place to put this so
       * ignore it.  - JB
       */
      (void) or_get_int (buf, &rc);
      (void) or_get_int (buf, &rc);

      /* variable attribute 0 : name */
      att->header.name = get_string (buf, vars[ORC_ATT_NAME_INDEX].length);

      /* variable attribute 1 : value */
      or_get_value (buf, &att->default_value.value, NULL, vars[ORC_ATT_CURRENT_VALUE_INDEX].length, true);

      /* variable attribute 2 : original value */
      or_get_value (buf, &att->default_value.original_value, NULL, vars[ORC_ATT_ORIGINAL_VALUE_INDEX].length, true);

      /* variable attribute 3 : domain */
      att->domain =
	(TP_DOMAIN *) get_substructure_set (buf, (LREADER) disk_to_domain, vars[ORC_ATT_DOMAIN_INDEX].length);

      if (att->domain != NULL && att->domain->type->id == DB_TYPE_ENUMERATION)
	{
	  /* Fill the default values with missing data */
	  pr_complete_enum_value (&att->default_value.value, att->domain);
	  pr_complete_enum_value (&att->default_value.original_value, att->domain);
	}

      /* variable attribute 4: trigger list */
      triggers = get_object_set (buf, vars[ORC_ATT_TRIGGER_INDEX].length);
      if (triggers != NULL)
	{
	  att->triggers = tr_make_schema_cache (TR_CACHE_ATTRIBUTE, triggers);
	}
      /* variable attribute 5: property list */
      /* formerly disk_to_att_extension(buf, att, vars[5].length); */
      att->properties = get_property_list (buf, vars[ORC_ATT_PROPERTIES_INDEX].length);

      classobj_initialize_default_expr (&att->default_value.default_expr);
      att->on_update_default_expr = DB_DEFAULT_NONE;
      if (att->properties)
	{
	  if (classobj_get_prop (att->properties, "update_default", &value) > 0)
	    {
	      att->on_update_default_expr = (DB_DEFAULT_EXPR_TYPE) db_get_int (&value);
	    }

	  if (classobj_get_prop (att->properties, "default_expr", &value) > 0)
	    {
	      /* We have two cases: simple and complex expressions. */
	      if (DB_VALUE_TYPE (&value) == DB_TYPE_SEQUENCE)
		{
		  DB_SEQ *def_expr_seq;
		  DB_VALUE def_expr_op, def_expr_type, def_expr_format;
		  const char *def_expr_format_str;

		  assert (set_size (db_get_set (&value)) == 3);

		  def_expr_seq = db_get_set (&value);

		  /* get default expression operator (op of expr) */
		  if (set_get_element_nocopy (def_expr_seq, 0, &def_expr_op) != NO_ERROR)
		    {
		      assert (false);
		    }
		  assert (DB_VALUE_TYPE (&def_expr_op) == DB_TYPE_INTEGER
			  && db_get_int (&def_expr_op) == (int) T_TO_CHAR);
		  att->default_value.default_expr.default_expr_op = db_get_int (&def_expr_op);

		  /* get default expression type (arg1 of expr) */
		  if (set_get_element_nocopy (def_expr_seq, 1, &def_expr_type) != NO_ERROR)
		    {
		      assert (false);
		    }
		  assert (DB_VALUE_TYPE (&def_expr_type) == DB_TYPE_INTEGER);
		  att->default_value.default_expr.default_expr_type =
		    (DB_DEFAULT_EXPR_TYPE) db_get_int (&def_expr_type);

		  /* get default expression format (arg2 of expr) */
		  if (set_get_element_nocopy (def_expr_seq, 2, &def_expr_format) != NO_ERROR)
		    {
		      assert (false);
		    }

#if !defined (NDEBUG)
		  {
		    DB_TYPE db_value_type_local = db_value_type (&def_expr_format);
		    assert (db_value_type_local == DB_TYPE_NULL || TP_IS_CHAR_TYPE (db_value_type_local));
		  }
#endif
		  if (!db_value_is_null (&def_expr_format))
		    {
		      def_expr_format_str = db_get_string (&def_expr_format);
		      att->default_value.default_expr.default_expr_format = ws_copy_string (def_expr_format_str);
		      if (att->default_value.default_expr.default_expr_format == NULL)
			{
			  assert (er_errid () != NO_ERROR);
			}
		    }
		}
	      else
		{
		  att->default_value.default_expr.default_expr_type = (DB_DEFAULT_EXPR_TYPE) db_get_int (&value);
		}

	      pr_clear_value (&value);
	    }
	}

      /* variable attribute 6: comment */
      att->comment = get_string (buf, vars[ORC_ATT_COMMENT_INDEX].length);

      /* THIS SHOULD BE INITIALIZING THE header.name_space field !! */

      free_var_table (vars);
    }
}


/*
 * resolution_to_disk - Writes the disk representation of a resolution.
 *    return:
 *    buf(in/out): translation buffer
 *    res(in): resolution
 */
static int
resolution_to_disk (OR_BUF * buf, SM_RESOLUTION * res)
{
  char *start;
  int offset, name_space;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  /* name */
  offset = tf_Metaclass_resolution.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_resolution.mc_n_variable);
  or_put_offset (buf, offset);
  offset += string_disk_size (res->name);

  /* new name */
  or_put_offset (buf, offset);
  offset += string_disk_size (res->alias);

  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  or_pack_mop (buf, res->class_mop);
  name_space = (int) res->name_space;	/* kludge for ansi */
  or_put_int (buf, name_space);
  put_string (buf, res->name);
  put_string (buf, res->alias);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
resolution_to_disk_lwriter (void *buf, void *res)
{
  (void) resolution_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_RESOLUTION *, res));
}

/*
 * resolution_size - Calculates the disk size of a resolution.
 *    return: disk size of resolution
 *    res(in): resolution
 */
static int
resolution_size (SM_RESOLUTION * res)
{
  int size;

  size = tf_Metaclass_resolution.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_resolution.mc_n_variable);
  size += string_disk_size (res->name);
  size += string_disk_size (res->alias);

  return (size);
}


/*
 * disk_to_resolution - Reads the disk representation of a resolution and
 * creates a new structure in memory.
 *    return: new resolution structure
 *    buf(in/out): translation buffer
 * Note:
 *    Handle the case where the resolution for a deleted class was
 *    accidentally saved.  When this happens, the stored OID will be the NULL
 *    oid and ws_mop will return NULL.
 *    Shouldn't really happen but its easier to check for it here
 *    than having the sizer and writer functions check.  Seeing this
 *    probably indicates a bug in the subclass cleanup code after
 *    class deletion.
 *    This paranoia may no longer be necessary but it was put here
 *    for a reason and we should be careful about ripping it out until
 *    all the tests run without it.
 *
 *    A jmp_buf has previously been established.
 */
static SM_RESOLUTION *
disk_to_resolution (OR_BUF * buf)
{
  SM_RESOLUTION *res;
  SM_NAME_SPACE name_space;
  OR_VARINFO *vars;
  MOP class_;
  DB_VALUE value;
  int rc;

  res = NULL;
  vars = read_var_table (buf, tf_Metaclass_resolution.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      tp_Object.data_readval (buf, &value, NULL, -1, true, NULL, 0);
      class_ = db_get_object (&value);
      if (class_ == NULL)
	{
	  (void) or_get_int (buf, &rc);
	  tp_VarNChar.data_readval (buf, NULL, NULL, vars[ORC_RES_NAME_INDEX].length, true, NULL, 0);
	  tp_VarNChar.data_readval (buf, NULL, NULL, vars[ORC_RES_ALIAS_INDEX].length, true, NULL, 0);
	}
      else
	{
	  name_space = (SM_NAME_SPACE) or_get_int (buf, &rc);
	  res = classobj_make_resolution (NULL, NULL, NULL, name_space);
	  if (res == NULL)
	    {
	      or_abort (buf);
	    }
	  else
	    {
	      res->class_mop = class_;
	      res->name = get_string (buf, vars[ORC_RES_NAME_INDEX].length);
	      res->alias = get_string (buf, vars[ORC_RES_ALIAS_INDEX].length);
	    }
	}

      free_var_table (vars);
    }
  return (res);
}

/*
 * repattribute_to_disk - Writes the disk representation of a representation
 * attribute.
 *    return: NO_ERROR or error code
 *    buf(in): translation buffer
 *    rat(in): attribute
 * Note:
 *    On overflow, or_overflow will call longjmp and jump to the outer caller
 */
static int
repattribute_to_disk (OR_BUF * buf, SM_REPR_ATTRIBUTE * rat)
{
  char *start;
  int offset;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  offset = tf_Metaclass_repattribute.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_repattribute.mc_n_variable);

  /* domain list */
  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) rat->domain, (LSIZER) domain_size);

  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);
  /* fixed width attributes */
  or_put_int (buf, rat->attid);
  or_put_int (buf, (int) rat->typeid_);

  /* domain */
  put_substructure_set (buf, (DB_LIST *) rat->domain, (LWRITER) domain_to_disk, &tf_Metaclass_domain.mc_classoid,
			tf_Metaclass_domain.mc_repid);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
repattribute_to_disk_lwriter (void *buf, void *rat)
{
  (void) repattribute_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_REPR_ATTRIBUTE *, rat));
}

/*
 * repattribute_size - Calculates the disk size for the REPATTRIBUTE.
 *    return: disk size of repattribute
 *    rat(in): memory attribute
 */
static int
repattribute_size (SM_REPR_ATTRIBUTE * rat)
{
  int size;

  size = tf_Metaclass_repattribute.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_repattribute.mc_n_variable);

  size += substructure_set_size ((DB_LIST *) rat->domain, (LSIZER) domain_size);

  return (size);
}

/*
 * disk_to_repattribute - Reads the disk representation of a representation
 * attribute.
 *    return: new repattribute
 *    buf(in/out): translation buffer
 */
static SM_REPR_ATTRIBUTE *
disk_to_repattribute (OR_BUF * buf)
{
  SM_REPR_ATTRIBUTE *rat;
  OR_VARINFO *vars;
  int id, tid;
  int rc = NO_ERROR;

  vars = read_var_table (buf, tf_Metaclass_repattribute.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
      return NULL;
    }

  id = or_get_int (buf, &rc);
  tid = or_get_int (buf, &rc);
  rat = classobj_make_repattribute (id, (DB_TYPE) tid, NULL);
  if (rat == NULL)
    {
      free_var_table (vars);
      or_abort (buf);
      return NULL;
    }

  rat->domain =
    (TP_DOMAIN *) get_substructure_set (buf, (LREADER) disk_to_domain, vars[ORC_REPATT_DOMAIN_INDEX].length);

  free_var_table (vars);

  return rat;
}


/*
 * representation_size - Calculate the disk size for a representation.
 *    return: byte size of the representation
 *    rep(in): representation
 */
static int
representation_size (SM_REPRESENTATION * rep)
{
  int size;

  size = tf_Metaclass_representation.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_representation.mc_n_variable);

  size += substructure_set_size ((DB_LIST *) rep->attributes, (LSIZER) repattribute_size);

  size += property_list_size (NULL);

  return (size);
}

/*
 * representation_to_disk - Write the disk representation of an
 * SM_REPRESENTATION.
 *    return: NO_ERROR
 *    buf(in/out): translation buffer
 *    rep(in): representation
 * Note:
 *    On overflow, or_overflow will call longjmp and jump to the outer caller
 */
static int
representation_to_disk (OR_BUF * buf, SM_REPRESENTATION * rep)
{
  char *start;
  int offset;

  start = buf->ptr;
  offset = tf_Metaclass_representation.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_representation.mc_n_variable);

  or_put_offset (buf, offset);
  offset += substructure_set_size ((DB_LIST *) rep->attributes, (LSIZER) repattribute_size);

  or_put_offset (buf, offset);
  offset += property_list_size (NULL);
  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  or_put_int (buf, rep->id);
  or_put_int (buf, rep->fixed_count);
  or_put_int (buf, rep->variable_count);

  /* no longer have the fixed_size field, leave it for future expansion */
  or_put_int (buf, 0);

  put_substructure_set (buf, (DB_LIST *) rep->attributes, repattribute_to_disk_lwriter,
			&tf_Metaclass_repattribute.mc_classoid, tf_Metaclass_repattribute.mc_repid);

  put_property_list (buf, NULL);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
representation_to_disk_lwriter (void *buf, void *rep)
{
  (void) representation_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_REPRESENTATION *, rep));
}

/*
 * disk_to_representation - Read the disk representation for an
 * SM_REPRESENTATION structure and build the memory structure.
 *    return: new representation structure
 *    buf(in/out): translation buffer
 */
static SM_REPRESENTATION *
disk_to_representation (OR_BUF * buf)
{
  SM_REPRESENTATION *rep;
  OR_VARINFO *vars;
  DB_SEQ *props;
  int rc = NO_ERROR;

  vars = read_var_table (buf, tf_Metaclass_representation.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
      return NULL;
    }

  rep = classobj_make_representation ();

  if (rep == NULL)
    {
      free_var_table (vars);
      or_abort (buf);
      return NULL;
    }
  else
    {
      rep->id = or_get_int (buf, &rc);
      rep->fixed_count = or_get_int (buf, &rc);
      rep->variable_count = or_get_int (buf, &rc);

      /* we no longer use this field, formerly fixed_size */
      (void) or_get_int (buf, &rc);

      /* variable 0 : attributes */
      rep->attributes =
	(SM_REPR_ATTRIBUTE *) get_substructure_set (buf, (LREADER) disk_to_repattribute,
						    vars[ORC_REP_ATTRIBUTES_INDEX].length);

      /* variable 1 : properties */
      props = get_property_list (buf, vars[ORC_REP_PROPERTIES_INDEX].length);
      /* shouldn't be any, we have no place for them */
      if (props != NULL)
	{
	  set_free (props);
	}
    }

  free_var_table (vars);
  return (rep);
}

/*
 * check_class_structure - maps over the class prior to storage to make sure
 * that everything looks ok.
 *    return: error code
 *    class(in): class strucutre
 * Note:
 *    The point is to get errors detected early on so that the lower level
 *    translation functions don't have to worry about them.
 *    It is MANDATORY that this be called before any size or other walk of
 *    the class
 */
static int
check_class_structure (SM_CLASS * class_)
{
  int ok = 1;

  /* Currently, there is nothing to decache.  I expect have to populate the class property list here someting in the
   * near future so I'll leave the function in place.  - JB */

/*  decache_attribute_properties(class); */
  return (ok);
}

/*
 * put_class_varinfo - Writes the variable offset table for a class object.
 *    return: ending offset
 *    buf(in/out): translation buffer
 *    class(in): class structure
 * Note:
 *    This is the only meta object that includes OR_MVCC_HEADER_SIZE
 *    as part of the offset calculations.  This is because the other
 *    substructures are all stored in sets inside the class object.
 *    Returns the offset within the buffer after the offset table
 *    (offset to first fixed attribute).
 */
static int
put_class_varinfo (OR_BUF * buf, SM_CLASS * class_)
{
  DB_OBJLIST *triggers;
  int offset;

  /* compute the variable offsets relative to the end of the header (beginning of variable table) */
  offset = tf_Metaclass_class.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_class.mc_n_variable);

  /* unique_name */
  or_put_offset (buf, offset);

  offset += string_disk_size (sm_ch_name ((MOBJ) class_));

  /* class_name */
  or_put_offset (buf, offset);

  offset += string_disk_size (sm_remove_qualifier_name (sm_ch_name ((MOBJ) class_)));

  or_put_offset (buf, offset);

  offset += string_disk_size (class_->loader_commands);

  /* representation set */
  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->representations, (LSIZER) representation_size);
  or_put_offset (buf, offset);

  offset += object_set_size (class_->users);
  or_put_offset (buf, offset);

  offset += object_set_size (class_->inheritance);
  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->attributes, (LSIZER) attribute_size);
  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->shared, (LSIZER) attribute_size);
  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->class_attributes, (LSIZER) attribute_size);

  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->methods, (LSIZER) method_size);

  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->class_methods, (LSIZER) method_size);

  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->method_files, (LSIZER) methfile_size);

  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->resolutions, (LSIZER) resolution_size);

  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->query_spec, (LSIZER) query_spec_size);

  or_put_offset (buf, offset);

  (void) tr_get_cache_objects (class_->triggers, &triggers);
  offset += object_set_size (triggers);

  /* property list */
  or_put_offset (buf, offset);

  offset += property_list_size (class_->properties);

  or_put_offset (buf, offset);

  offset += string_disk_size (class_->comment);

  or_put_offset (buf, offset);

  offset += substructure_set_size ((DB_LIST *) class_->partition, (LSIZER) partition_info_size);

  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  return (offset);
}

/*
 * put_class_attributes - Writes the fixed and variable attributes of a class.
 *    return: void
 *    buf(in/out): translation buffer
 *    class(in): class structure
 */
static void
put_class_attributes (OR_BUF * buf, SM_CLASS * class_)
{
  DB_OBJLIST *triggers;

#if !defined(NDEBUG)
  if (!HFID_IS_NULL (sm_ch_heap ((MOBJ) class_)))
    {
      assert (!OID_ISNULL (sm_ch_rep_dir ((MOBJ) class_)));
    }
#endif

  /* attribute id counters */

  /* doesn't exist yet */
  or_put_int (buf, class_->att_ids);
  or_put_int (buf, class_->method_ids);

  or_put_oid (buf, &(class_->header.ch_rep_dir));

  or_put_int (buf, class_->header.ch_heap.vfid.fileid);
  or_put_int (buf, class_->header.ch_heap.vfid.volid);
  or_put_int (buf, class_->header.ch_heap.hpgid);

  or_put_int (buf, class_->fixed_count);
  or_put_int (buf, class_->variable_count);
  or_put_int (buf, class_->fixed_size);
  or_put_int (buf, class_->att_count);
  /* object size is now calculated after loading */
  or_put_int (buf, 0);
  or_put_int (buf, class_->shared_count);
  or_put_int (buf, class_->method_count);
  or_put_int (buf, class_->class_method_count);
  or_put_int (buf, class_->class_attribute_count);
  or_put_int (buf, class_->flags);
  or_put_int (buf, (int) class_->class_type);

  /* owner object */
  or_pack_mop (buf, class_->owner);
  or_put_int (buf, (int) class_->collation_id);

  or_put_int (buf, class_->tde_algorithm);


  /* 0: NAME */
  put_string (buf, sm_ch_name ((MOBJ) class_));
  put_string (buf, sm_remove_qualifier_name (sm_ch_name ((MOBJ) class_)));
  put_string (buf, class_->loader_commands);

  put_substructure_set (buf, (DB_LIST *) class_->representations, representation_to_disk_lwriter,
			&tf_Metaclass_representation.mc_classoid, tf_Metaclass_representation.mc_repid);

  put_object_set (buf, class_->users);

  put_object_set (buf, class_->inheritance);

  put_substructure_set (buf, (DB_LIST *) class_->attributes, attribute_to_disk_lwriter,
			&tf_Metaclass_attribute.mc_classoid, tf_Metaclass_attribute.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->shared, attribute_to_disk_lwriter,
			&tf_Metaclass_attribute.mc_classoid, tf_Metaclass_attribute.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->class_attributes, attribute_to_disk_lwriter,
			&tf_Metaclass_attribute.mc_classoid, tf_Metaclass_attribute.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->methods, method_to_disk_lwriter, &tf_Metaclass_method.mc_classoid,
			tf_Metaclass_method.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->class_methods, method_to_disk_lwriter,
			&tf_Metaclass_method.mc_classoid, tf_Metaclass_method.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->method_files, methfile_to_disk_lwriter,
			&tf_Metaclass_methfile.mc_classoid, tf_Metaclass_methfile.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->resolutions, resolution_to_disk_lwriter,
			&tf_Metaclass_resolution.mc_classoid, tf_Metaclass_resolution.mc_repid);

  put_substructure_set (buf, (DB_LIST *) class_->query_spec, query_spec_to_disk_lwriter,
			&tf_Metaclass_query_spec.mc_classoid, tf_Metaclass_query_spec.mc_repid);

  /*
   * triggers - for simplicity, convert the cache into a flattened
   * list of object id's
   */
  (void) tr_get_cache_objects (class_->triggers, &triggers);
  put_object_set (buf, triggers);

  put_property_list (buf, class_->properties);

  put_string (buf, class_->comment);

  put_substructure_set (buf, (DB_LIST *) class_->partition, partition_info_to_disk_lwriter,
			&tf_Metaclass_partition.mc_classoid, tf_Metaclass_partition.mc_repid);
}

/*
 * class_to_disk - Write the disk representation of a class.
 *    return: void
 *    buf(in/out): translation buffer
 *    class(in): class structure
 * Note:
 *    The writing of the offset table and the attributes was split into
 *    two pieces because it was getting too long.
 *    Caller must have a setup a jmpbuf (called setjmp) to handle any
 *    errors
 */
static void
class_to_disk (OR_BUF * buf, SM_CLASS * class_)
{
  char *start;
  int offset;

  /* kludge, we may have to do some last minute adj of the class structures before saving.  In particular, some of the
   * attribute fields need to be placed in the attribute property list because there are no corresponding fields in the
   * disk representation.  This may result in storage allocation which because we don't have modern computers may fail.
   * This function does all of the various checking up front so we don't have to detect it later in the substructure
   * conversion routines */
  if (!check_class_structure (class_))
    {
      or_abort (buf);
    }
  else
    {
      start = buf->ptr - OR_NON_MVCC_HEADER_SIZE;
      /* header already written */
      offset = put_class_varinfo (buf, class_);
      put_class_attributes (buf, class_);

      if (start + offset + OR_NON_MVCC_HEADER_SIZE != buf->ptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
	  or_abort (buf);
	}
    }
}


/*
 * tf_class_size - Calculates the disk size of a class.
 *    return: disk size of class
 *    classobj(in): pointer to class structure
 */
static int
tf_class_size (MOBJ classobj)
{
  SM_CLASS *class_;
  DB_OBJLIST *triggers;
  int size;

  class_ = (SM_CLASS *) classobj;

  /* make sure properties are decached */
  if (!check_class_structure (class_))
    {
      return (-1);
    }

  size = OR_NON_MVCC_HEADER_SIZE;

  size += tf_Metaclass_class.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_class.mc_n_variable);

  size += string_disk_size (sm_ch_name ((MOBJ) class_));
  size += string_disk_size (sm_remove_qualifier_name (sm_ch_name ((MOBJ) class_)));
  size += string_disk_size (class_->loader_commands);

  size += substructure_set_size ((DB_LIST *) class_->representations, (LSIZER) representation_size);

  size += object_set_size (class_->users);
  size += object_set_size (class_->inheritance);

  size += substructure_set_size ((DB_LIST *) class_->attributes, (LSIZER) attribute_size);
  size += substructure_set_size ((DB_LIST *) class_->shared, (LSIZER) attribute_size);
  size += substructure_set_size ((DB_LIST *) class_->class_attributes, (LSIZER) attribute_size);

  size += substructure_set_size ((DB_LIST *) class_->methods, (LSIZER) method_size);
  size += substructure_set_size ((DB_LIST *) class_->class_methods, (LSIZER) method_size);

  size += substructure_set_size ((DB_LIST *) class_->method_files, (LSIZER) methfile_size);

  size += substructure_set_size ((DB_LIST *) class_->resolutions, (LSIZER) resolution_size);

  size += substructure_set_size ((DB_LIST *) class_->query_spec, (LSIZER) query_spec_size);

  (void) tr_get_cache_objects (class_->triggers, &triggers);
  size += object_set_size (triggers);

  size += property_list_size (class_->properties);

  size += string_disk_size (class_->comment);

  size += substructure_set_size ((DB_LIST *) class_->partition, (LSIZER) partition_info_size);

  return (size);
}

#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tf_dump_class_size - Debugging function to display disk size information
 * for a class.
 *    return:  void
 *    classobj(in): pointer to class structure
 */
void
tf_dump_class_size (MOBJ classobj)
{
  SM_CLASS *class_;
  DB_OBJLIST *triggers;
  int size, s;

  class_ = (SM_CLASS *) classobj;

  /* make sure properties are decached */
  if (!check_class_structure (class_))
    {
      return;
    }

  size = OR_NON_MVCC_HEADER_SIZE;	/* ? */
  size += tf_Metaclass_class.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_class.mc_n_variable);
  fprintf (stdout, "Fixed size %d\n", size);

  s = string_disk_size (sm_ch_name ((MOBJ) class_));
  fprintf (stdout, "Header name %d\n", s);
  size += s;

  s = string_disk_size (class_->loader_commands);
  fprintf (stdout, "Loader commands %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->representations, (LSIZER) representation_size);
  fprintf (stdout, "Representations %d\n", s);
  size += s;

  s = object_set_size (class_->users);
  fprintf (stdout, "Users %d\n", s);
  size += s;

  s = object_set_size (class_->inheritance);
  fprintf (stdout, "Inheritance %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->attributes, (LSIZER) attribute_size);
  fprintf (stdout, "Attributes %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->shared, (LSIZER) attribute_size);
  fprintf (stdout, "Shared attributes %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->class_attributes, (LSIZER) attribute_size);
  fprintf (stdout, "Class attributes %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->methods, (LSIZER) method_size);
  fprintf (stdout, "Methods %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->class_methods, (LSIZER) method_size);
  fprintf (stdout, "Class methods %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->method_files, (LSIZER) methfile_size);
  fprintf (stdout, "Method files %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->resolutions, (LSIZER) resolution_size);
  fprintf (stdout, "Resolutions %d\n", s);
  size += s;

  s = substructure_set_size ((DB_LIST *) class_->query_spec, (LSIZER) query_spec_size);
  fprintf (stdout, "Query_Spec statements %d\n", s);
  size += s;

  (void) tr_get_cache_objects (class_->triggers, &triggers);
  s = object_set_size (triggers);
  fprintf (stdout, "Triggers %d\n", s);
  size += s;

  s = property_list_size (class_->properties);
  fprintf (stdout, "Properties %d\n", s);
  size += s;

  s = string_disk_size (class_->comment);
  fprintf (stdout, "Comment %d\n", s);
  size += s;

  fprintf (stdout, "TOTAL: %d\n", size);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * tag_component_namespace - restore the appropriate name_space tags for
 * class components.
 *    return: void
 *    components(in/out): component list
 *    namespace(in):
 * Note:
 *    Since the component tag is not stored with the attributes & methods
 *    it must be restored after they have been read.  We know the name_space
 *    because they have been separated into different lists in the
 *    class structure.
 */
static void
tag_component_namespace (SM_COMPONENT * components, SM_NAME_SPACE name_space)
{
  SM_COMPONENT *c;

  for (c = components; c != NULL; c = c->next)
    {
      c->name_space = name_space;
    }
}

/*
 * disk_to_class - Reads the disk representation of a class and creates the
 * memory structure.
 *    return: class structure
 *    buf(in/out): translation buffer
 *    class_ptr(out): return pointer
 * Note:
      A jmp_buf has previously been established.
 */
static SM_CLASS *
disk_to_class (OR_BUF * buf, SM_CLASS ** class_ptr)
{
  SM_CLASS *class_;
  SM_ATTRIBUTE *att;
  OR_VARINFO *vars;
  DB_OBJLIST *triggers;
  DB_VALUE value;
  int rc = NO_ERROR;
  char auto_increment_name[AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH];
  MOP serial_class_mop = NULL, serial_mop;
  DB_IDENTIFIER serial_obj_id;

  class_ = NULL;
  /* get the variable length and offsets. The offsets are relative to the end of the header (beginning of variable
   * table). */
  vars = read_var_table (buf, tf_Metaclass_class.mc_n_variable);
  if (vars == NULL)
    {
      goto on_error;
    }

  class_ = *class_ptr = classobj_make_class (NULL);
  if (class_ == NULL)
    {
      goto on_error;
    }

  class_->att_ids = or_get_int (buf, &rc);
  class_->method_ids = or_get_int (buf, &rc);

  or_get_oid (buf, &(class_->header.ch_rep_dir));

  class_->header.ch_heap.vfid.fileid = or_get_int (buf, &rc);
  class_->header.ch_heap.vfid.volid = or_get_int (buf, &rc);
  class_->header.ch_heap.hpgid = or_get_int (buf, &rc);

#if !defined(NDEBUG)
  if (!HFID_IS_NULL (sm_ch_heap ((MOBJ) class_)))
    {
      assert (!OID_ISNULL (sm_ch_rep_dir ((MOBJ) class_)));
    }
#endif

  class_->fixed_count = or_get_int (buf, &rc);
  class_->variable_count = or_get_int (buf, &rc);
  class_->fixed_size = or_get_int (buf, &rc);
  class_->att_count = or_get_int (buf, &rc);
  class_->object_size = or_get_int (buf, &rc);
  class_->object_size = 0;	/* calculated later */
  class_->shared_count = or_get_int (buf, &rc);
  class_->method_count = or_get_int (buf, &rc);
  class_->class_method_count = or_get_int (buf, &rc);
  class_->class_attribute_count = or_get_int (buf, &rc);
  class_->flags = or_get_int (buf, &rc);
  class_->class_type = (SM_CLASS_TYPE) or_get_int (buf, &rc);

  /* owner object */
  tp_Object.data_readval (buf, &value, NULL, -1, true, NULL, 0);
  class_->owner = db_get_object (&value);
  class_->collation_id = or_get_int (buf, &rc);

  class_->tde_algorithm = or_get_int (buf, &rc);

  /* variable 0 */
  class_->header.ch_name = get_string (buf, vars[ORC_UNIQUE_NAME_INDEX].length);

  /* variable 1 */
  /* Since unique_name includes class_name, only unique_name is stored in SM_CLASS.
   * The code below is needed to move the buf location. */
  get_string (buf, vars[ORC_NAME_INDEX].length);

  /* variable 2 */
  class_->loader_commands = get_string (buf, vars[ORC_LOADER_COMMANDS_INDEX].length);

  /* REPRESENTATIONS */
  /* variable 3 */
  class_->representations =
    (SM_REPRESENTATION *) get_substructure_set (buf, (LREADER) disk_to_representation,
						vars[ORC_REPRESENTATIONS_INDEX].length);

  /* variable 4 */
  class_->users = get_object_set (buf, vars[ORC_SUBCLASSES_INDEX].length);
  /* variable 5 */
  class_->inheritance = get_object_set (buf, vars[ORC_SUPERCLASSES_INDEX].length);

  class_->attributes = (SM_ATTRIBUTE *) classobj_alloc_threaded_array (sizeof (SM_ATTRIBUTE), class_->att_count);
  if (class_->att_count && class_->attributes == NULL)
    {
      goto on_error;
    }
  classobj_initialize_attributes (class_->attributes);

  class_->shared = (SM_ATTRIBUTE *) classobj_alloc_threaded_array (sizeof (SM_ATTRIBUTE), class_->shared_count);
  if (class_->shared_count && class_->shared == NULL)
    {
      goto on_error;
    }
  classobj_initialize_attributes (class_->shared);

  class_->class_attributes =
    (SM_ATTRIBUTE *) classobj_alloc_threaded_array (sizeof (SM_ATTRIBUTE), class_->class_attribute_count);
  if (class_->class_attribute_count && class_->class_attributes == NULL)
    {
      goto on_error;
    }
  classobj_initialize_attributes (class_->class_attributes);

  class_->methods = (SM_METHOD *) classobj_alloc_threaded_array (sizeof (SM_METHOD), class_->method_count);
  if (class_->method_count && class_->methods == NULL)
    {
      goto on_error;
    }
  classobj_initialize_methods (class_->methods);

  class_->class_methods = (SM_METHOD *) classobj_alloc_threaded_array (sizeof (SM_METHOD), class_->class_method_count);
  if (class_->class_method_count && class_->class_methods == NULL)
    {
      goto on_error;
    }
  classobj_initialize_methods (class_->class_methods);

  /* variable 6 */
  install_substructure_set (buf, (DB_LIST *) class_->attributes, (VREADER) disk_to_attribute,
			    vars[ORC_ATTRIBUTES_INDEX].length);
  /* variable 7 */
  install_substructure_set (buf, (DB_LIST *) class_->shared, (VREADER) disk_to_attribute,
			    vars[ORC_SHARED_ATTRS_INDEX].length);
  /* variable 8 */
  install_substructure_set (buf, (DB_LIST *) class_->class_attributes, (VREADER) disk_to_attribute,
			    vars[ORC_CLASS_ATTRS_INDEX].length);

  /* variable 9 */
  install_substructure_set (buf, (DB_LIST *) class_->methods, (VREADER) disk_to_method, vars[ORC_METHODS_INDEX].length);
  /* variable 10 */
  install_substructure_set (buf, (DB_LIST *) class_->class_methods, (VREADER) disk_to_method,
			    vars[ORC_CLASS_METHODS_INDEX].length);

  /*
   * fix up the name_space tags, could do this later but easier just
   * to assume that they're set up correctly
   */
  tag_component_namespace ((SM_COMPONENT *) class_->attributes, ID_ATTRIBUTE);
  tag_component_namespace ((SM_COMPONENT *) class_->shared, ID_SHARED_ATTRIBUTE);
  tag_component_namespace ((SM_COMPONENT *) class_->class_attributes, ID_CLASS_ATTRIBUTE);
  tag_component_namespace ((SM_COMPONENT *) class_->methods, ID_METHOD);
  tag_component_namespace ((SM_COMPONENT *) class_->class_methods, ID_CLASS_METHOD);

  /* variable 11 */
  class_->method_files =
    (SM_METHOD_FILE *) get_substructure_set (buf, (LREADER) disk_to_methfile, vars[ORC_METHOD_FILES_INDEX].length);

  /* variable 12 */
  class_->resolutions =
    (SM_RESOLUTION *) get_substructure_set (buf, (LREADER) disk_to_resolution, vars[ORC_RESOLUTIONS_INDEX].length);

  /* variable 13 */
  class_->query_spec =
    (SM_QUERY_SPEC *) get_substructure_set (buf, (LREADER) disk_to_query_spec, vars[ORC_QUERY_SPEC_INDEX].length);

  /* variable 14 */
  triggers = get_object_set (buf, vars[ORC_TRIGGERS_INDEX].length);
  if (triggers != NULL)
    {
      class_->triggers = tr_make_schema_cache (TR_CACHE_CLASS, triggers);
    }

  /* variable 15 */
  class_->properties = get_property_list (buf, vars[ORC_PROPERTIES_INDEX].length);

  /* variable 16 */
  class_->comment = get_string (buf, vars[ORC_COMMENT_INDEX].length);

  /* variable 17 */
  class_->partition =
    (SM_PARTITION *) get_substructure_set (buf, (LREADER) disk_to_partition_info, vars[ORC_PARTITION_INDEX].length);

  /* build the ordered instance/shared instance list */
  classobj_fixup_loaded_class (class_);

  /* set attribute's auto_increment object if any */
  for (att = class_->attributes; att != NULL; att = (SM_ATTRIBUTE *) att->header.next)
    {
      att->auto_increment = NULL;
      if (att->flags & SM_ATTFLAG_AUTO_INCREMENT)
	{
	  if (serial_class_mop == NULL)
	    {
	      serial_class_mop = sm_find_class (CT_SERIAL_NAME);
	      if (serial_class_mop == NULL)
		{
		  ASSERT_ERROR ();
		  goto on_error;
		}
	    }

	  SET_AUTO_INCREMENT_SERIAL_NAME (auto_increment_name, sm_ch_name ((MOBJ) class_), att->header.name);
	  serial_mop = do_get_serial_obj_id (&serial_obj_id, serial_class_mop, auto_increment_name);

	  /* If this att is inherited from a super class, serial_mop can be NULL. In this case, att->auto_increment
	   * will be set later. */
#if 0
	  if (serial_mop == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ATTRIBUTE, 1, auto_increment_name);
	      db_ws_free (class_);
	      free_var_table (vars);
	      or_abort (buf);
	      return NULL;
	    }
#endif
	  att->auto_increment = serial_mop;
	}
    }

  free_var_table (vars);

  return (class_);

on_error:

  if (vars != NULL)
    {
      free_var_table (vars);
    }

  if (class_ != NULL)
    {
      classobj_free_class (class_);
    }

  *class_ptr = NULL;

  return NULL;
}


/*
 * root_to_disk - Write the disk representation of the root class.
 *    return: void
 *    buf(in/out): translation buffer
 *    root(in): root class object
 *    header_size(in): the size of header - variable in MVCC
 * Note:
 *    Caller must have a setup a jmpbuf (called setjmp) to handle any errors.
 *    Only the header part of the 'root' object is serialized. Please see comment on ROOT_CLASS definition.
 */
static void
root_to_disk (OR_BUF * buf, ROOT_CLASS * root)
{
  char *start;
  int offset;

  start = buf->ptr - OR_NON_MVCC_HEADER_SIZE;	/* header already written */

  /* compute the variable offsets relative to the end of the header (beginning of variable table) */
  offset = tf_Metaclass_root.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_root.mc_n_variable);

  /* name */
  or_put_offset (buf, offset);
  offset += string_disk_size (sm_ch_name ((MOBJ) root));

  /* end of object */
  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  assert (OID_ISNULL (sm_ch_rep_dir ((MOBJ) root)));	/* is dummy */

  /* heap file id - see assumptions in comment above */
  or_put_int (buf, (int) root->header.ch_heap.vfid.fileid);
  or_put_int (buf, (int) root->header.ch_heap.vfid.volid);
  or_put_int (buf, (int) root->header.ch_heap.hpgid);

  put_string (buf, sm_ch_name ((MOBJ) root));

  if (start + OR_NON_MVCC_HEADER_SIZE + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }
}


/*
 * root_size - Calculates the disk size of the root class.
 *    return: disk size of root class
 *    rootobj(in): root class object
 *
 *  Note:
 *    Only the header part of the 'root' object is serialized. Please see comment on ROOT_CLASS definition.
 */
static int
root_size (MOBJ rootobj)
{
  ROOT_CLASS *root;
  int size;

  root = (ROOT_CLASS *) rootobj;

  size = OR_NON_MVCC_HEADER_SIZE;
  size += tf_Metaclass_root.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_root.mc_n_variable);

  /* name */
  size += string_disk_size (sm_ch_name ((MOBJ) root));

  return (size);
}


/*
 * disk_to_root - Reads the disk representation of the root class and builds
 * the memory rootclass.
 *    return: root class object
 *    buf(in/out): translation buffer
 * Note:
 *    We know there is only one static structure for the root class so we use it rather than allocating a structure.
 *    Only the header part of the 'root' object is serialized. Please see comment on ROOT_CLASS definition.
 */
static ROOT_CLASS *
disk_to_root (OR_BUF * buf)
{
  char *start;
  OR_VARINFO *vars;
  int rc = NO_ERROR;

  start = buf->ptr - OR_NON_MVCC_HEADER_SIZE;	/* header already read */

  /* get the variable length and offsets. The offsets are relative to the end of the header (beginning of variable
   * table). */
  vars = read_var_table (buf, tf_Metaclass_root.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
    }
  else
    {
      assert (OID_ISNULL (sm_ch_rep_dir ((MOBJ) & sm_Root_class)));	/* is dummy */

      sm_Root_class.header.ch_heap.vfid.fileid = (FILEID) or_get_int (buf, &rc);
      sm_Root_class.header.ch_heap.vfid.volid = (VOLID) or_get_int (buf, &rc);
      sm_Root_class.header.ch_heap.hpgid = (PAGEID) or_get_int (buf, &rc);

      /* name - could make sure its the same as sm_Root_class_name */
      or_advance (buf, vars[0].length);

      if (start + OR_NON_MVCC_HEADER_SIZE + vars[0].offset + vars[0].length != buf->ptr)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
	}

      free_var_table (vars);
    }
  return (&sm_Root_class);
}


/*
 * tf_disk_to_class - transforming the disk representation of a class.
 *    return: class structure
 *    oid(in):
 *    record(in): disk record
 * Note:
 *    It is imperitive that garbage collection be disabled during this
 *    operation.
 *    This is because the structure being built may contain pointers
 *    to MOPs but it has not itself been attached to its own MOP yet so it
 *    doesn't serve as a gc root.   Make sure the caller
 *    calls ws_cache() immediately after we return so the new object
 *    gets attached as soon as possible.  We must also have already
 *    allocated the MOP in the caller so that we don't get one of the
 *    periodic gc's when we attempt to allocate a new MOP.
 *    This dependency should be isolated in a ws_ function called by
 *    the locator.
 */
MOBJ
tf_disk_to_class (OID * oid, RECDES * record)
{
  OR_BUF orep, *buf;
  unsigned int repid, chn;
  /* declare volatile to prevent reg optimization */
  MOBJ volatile class_;
  int rc = NO_ERROR;

  assert (oid != NULL && !OID_ISNULL (oid));

  /* should we assume this ? */
  if (!tf_Metaclass_class.mc_n_variable)
    {
      tf_compile_meta_classes ();
    }

  class_ = NULL;

  buf = &orep;
  or_init (buf, record->data, record->length);
  buf->error_abort = 1;

  switch (_setjmp (buf->env))
    {
    case 0:
      /* offset size */
      assert (OR_GET_OFFSET_SIZE (buf->ptr) == BIG_VAR_OFFSET_SIZE);

      repid = or_get_int (buf, &rc);
      repid = repid & ~OR_OFFSET_SIZE_FLAG;
      assert (((char) (repid >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK) == 0);
      chn = or_get_int (buf, &rc);

      if (oid_is_root (oid))
	{
	  class_ = (MOBJ) disk_to_root (buf);
	}
      else
	{
	  class_ = (MOBJ) disk_to_class (buf, (SM_CLASS **) (&class_));
	  if (class_ != NULL)
	    {
	      ((SM_CLASS *) class_)->repid = repid;
	    }
	}

      if (class_ != NULL)
	{
	  ((SM_CLASS *) class_)->header.ch_obj_header.chn = chn;
	}
      break;

    default:
      /*
       * make sure to clear the class that was being created,
       * an appropriate error will have been set
       */
      if (class_ != NULL)
	{
	  classobj_free_class ((SM_CLASS *) class_);
	  class_ = NULL;
	}
      break;
    }

  sm_bump_global_schema_version ();

  buf->error_abort = 0;
  return (class_);
}


/*
 * tf_class_to_disk - creates the disk representation of a class.
 *    return: zero for success, non-zero if errors
 *    classobj(in): pointer to class structure
 *    record(out): disk record
 * Note:
 *    If the record was not large enough for the class, resulting in
 *    the or_overflow error, this function returns the required size of the
 *    record as a negative number.
 */
TF_STATUS
tf_class_to_disk (MOBJ classobj, RECDES * record)
{
  OR_BUF orep, *buf;
  /* prevent reg optimization which hoses longmp */
  SM_CLASS *volatile class_;
  volatile int expected_size;
  SM_CLASS_HEADER *header;
  int chn;
  TF_STATUS status;
  int rc = 0;
  volatile int prop_free = 0;
  int repid;

  /* should we assume this ? */
  if (!tf_Metaclass_class.mc_n_variable)
    {
      tf_compile_meta_classes ();
    }

  /*
   * don't worry about deferred fixup for classes, we don't usually have
   * many temporary OIDs in classes.
   */
  buf = &orep;
  or_init (buf, record->data, record->area_size);
  buf->error_abort = 1;

  class_ = (SM_CLASS *) classobj;

  header = (SM_CLASS_HEADER *) classobj;
  if (header->ch_type != SM_META_ROOT)
    {
      /* put all default_expr values in attribute properties */
      rc = tf_attribute_default_expr_to_property (class_->attributes);
    }

  /*
   * test - this isn't necessary but we've been having a class size related
   * bug that I want to try to catch - take this out when we're sure
   */
  if (class_ == (SM_CLASS *) (&sm_Root_class))
    {
      expected_size = root_size (classobj);
    }
  else
    {
      expected_size = tf_class_size (classobj);
    }

  /* if anything failed this far, no need to save stack context, return code will be handled in the switch below */
  if (rc == NO_ERROR)
    {
      rc = _setjmp (buf->env);
    }

  switch (rc)
    {
    case 0:
      status = TF_SUCCESS;

      /* representation id, offset size */
      repid = class_->repid;
      assert (((char) (repid >> OR_MVCC_FLAG_SHIFT_BITS) & OR_MVCC_FLAG_MASK) == 0);
      OR_SET_VAR_OFFSET_SIZE (repid, BIG_VAR_OFFSET_SIZE);	/* 4byte */

      chn = class_->header.ch_obj_header.chn + 1;
      class_->header.ch_obj_header.chn = chn;

      /* The header size of a class record in the same like non-MVCC case. */
      or_put_int (buf, repid);
      or_put_int (buf, chn);

      if (header->ch_type == SM_META_ROOT)
	{
	  root_to_disk (buf, (ROOT_CLASS *) class_);
	}
      else
	{
	  assert (class_->repid == (repid & ~OR_OFFSET_SIZE_FLAG));
	  class_to_disk (buf, (SM_CLASS *) class_);
	}

      record->length = CAST_BUFLEN (buf->ptr - buf->buffer);

      /* sanity test, this sets an error only so we can see it if it happens */
      if (record->length != expected_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_SIZE_MISMATCH, 2, expected_size, record->length);
	  or_abort (buf);
	}

      /* fprintf(stdout, "Saved class in %d bytes\n", record->length); */
      break;

      /*
       * if the longjmp status was anything other than ER_TF_BUFFER_OVERFLOW,
       * it represents an error condition and er_set will have been called
       */

    case ER_TF_BUFFER_OVERFLOW:
      status = TF_OUT_OF_SPACE;
      record->length = -expected_size;
      break;

    default:
      status = TF_ERROR;
      break;

    }

  /* restore properties */
  if (((SM_CLASS_HEADER *) classobj)->ch_type != SM_META_ROOT && class_->partition)
    {
      if (prop_free)
	{
	  classobj_free_prop (class_->properties);
	  class_->properties = NULL;
	}
    }

  buf->error_abort = 0;
  return (status);
}


/*
 * tf_object_size - Determines the number of byte required to store an object
 * on disk.
 *    return: byte size of object on disk
 *    classobj(in): class of instance
 *    obj(in): instance to examine
 * Note:
 *    This will work for any object; classes, instances, or the rootclass.
 */
int
tf_object_size (MOBJ classobj, MOBJ obj)
{
  int size = 0;

  if (classobj != (MOBJ) (&sm_Root_class))
    {
      int dummy;
      size = object_size ((SM_CLASS *) classobj, obj, &dummy);
    }
  else if (obj == (MOBJ) (&sm_Root_class))
    {
      size = root_size (obj);
    }
  else
    {
      size = tf_class_size (obj);
    }

  return size;
}

/*
 * enumeration_size () - calculates size of enumeration
 * return : size of enumeration
 * enumeration (in) : enumeration
 */
static int
enumeration_size (const DB_ENUMERATION * enumeration)
{
  return or_packed_enumeration_size (enumeration);
}

/*
 * put_enumeration () - pack an enumeration
 * return: error code or NO_ERROR
 * enumeration (in): enumeration
 */
static void
put_enumeration (OR_BUF * buf, const DB_ENUMERATION * enumeration)
{
  (void) or_put_enumeration (buf, enumeration);
}

/*
 * get_enumeration - read enumeration from input buffer
 * return: NO_ERROR or error code
 * buf(in): input buffer
 * enumeration(in/out): pointer to enumeration holder
 * expected(in): expected length
 */
static int
get_enumeration (OR_BUF * buf, DB_ENUMERATION * enumeration, int expected)
{
  if (expected == 0)
    {
      enumeration->count = 0;
      enumeration->elements = NULL;
      return NO_ERROR;
    }
  return or_get_enumeration (buf, enumeration);
}


/*
 * tf_attribute_default_expr_to_property - transfer default_expr flag to a
 *                                         disk stored property
 *  returns: error code or NO_ERROR
 *  attr_list(in): attribute list to process
 */
static int
tf_attribute_default_expr_to_property (SM_ATTRIBUTE * attr_list)
{
  SM_ATTRIBUTE *attr = NULL;
  DB_DEFAULT_EXPR *default_expr;
  DB_VALUE default_expr_value;

  if (attr_list == NULL)
    {
      /* nothing to do */
      return NO_ERROR;
    }

  for (attr = attr_list; attr; attr = (SM_ATTRIBUTE *) attr->header.next)
    {
      default_expr = &attr->default_value.default_expr;
      if (default_expr->default_expr_type != DB_DEFAULT_NONE)
	{
	  /* attr has default expression as default value */
	  if (attr->properties == NULL)
	    {
	      /* allocate new property sequence */
	      attr->properties = classobj_make_prop ();

	      if (attr->properties == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, sizeof (DB_SEQ));
		  return er_errid ();
		}
	    }

	  if (default_expr->default_expr_op != NULL_DEFAULT_EXPRESSION_OPERATOR)
	    {
	      DB_SEQ *default_expr_sequence = NULL;
	      DB_VALUE value;

	      default_expr_sequence = set_create_sequence (3);
	      if (default_expr_sequence == NULL)
		{
		  if (attr->properties)
		    {
		      classobj_free_prop (attr->properties);
		      attr->properties = NULL;
		    }
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, sizeof (DB_SEQ));
		  return er_errid ();
		}

	      /* currently, only T_TO_CHAR operator is allowed  */
	      assert (default_expr->default_expr_op == T_TO_CHAR);
	      db_make_int (&value, (int) T_TO_CHAR);
	      set_put_element (default_expr_sequence, 0, &value);

	      /* default expression type */
	      db_make_int (&value, default_expr->default_expr_type);
	      set_put_element (default_expr_sequence, 1, &value);

	      /* default expression format */
	      if (default_expr->default_expr_format)
		{
		  db_make_string (&value, default_expr->default_expr_format);
		}
	      else
		{
		  db_make_null (&value);
		}

	      set_put_element (default_expr_sequence, 2, &value);

	      /* create and put sequence */
	      db_make_sequence (&default_expr_value, default_expr_sequence);
	      default_expr_sequence = NULL;
	      classobj_put_prop (attr->properties, "default_expr", &default_expr_value);
	      pr_clear_value (&default_expr_value);

	    }
	  else
	    {
	      /* add default_expr property to sequence */
	      db_make_int (&default_expr_value, default_expr->default_expr_type);
	      classobj_put_prop (attr->properties, "default_expr", &default_expr_value);
	    }
	}
      else if (attr->properties != NULL)
	{
	  /* make sure property is unset for existing attributes */
	  classobj_drop_prop (attr->properties, "default_expr");
	}

      DB_DEFAULT_EXPR_TYPE update_default = attr->on_update_default_expr;
      if (update_default != DB_DEFAULT_NONE)
	{
	  if (attr->properties == NULL)
	    {
	      attr->properties = classobj_make_prop ();

	      if (attr->properties == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, sizeof (DB_SEQ));
		  return er_errid ();
		}
	    }

	  db_make_int (&default_expr_value, update_default);
	  classobj_put_prop (attr->properties, "update_default", &default_expr_value);
	}
      else if (attr->properties != NULL)
	{
	  /* make sure property is unset for existing attributes */
	  classobj_drop_prop (attr->properties, "update_default");
	}
    }

  /* all ok */
  return NO_ERROR;
}


#if defined(ENABLE_UNUSED_FUNCTION)
/*
 * tf_set_size - calculates the number of bytes that are required to store
 * the disk representation of a set.
 *    return: size in bytes (-1 if error)
 *    set(in): set to examine
 * Note:
 *    If -1 is returned, an error was detected while calculating the
 *    size and er_errid() must be consulted to determine the cause.
 *    In practice, errors should not happen since we're simply
 *    mapping over the elements without fetching any objects.
 */
int
tf_set_size (DB_SET * set)
{
  DB_VALUE value;
  int size;

  /* won't handle references to "attached" sets that have been swapped out */
  if (set == NULL || set->set == NULL)
    {
      return 0;
    }

  /*
   * Doesn't matter which set function we call, they all use the
   * Don't have to synthesize a domain, we can get the one out
   * of the set itself.
   */
  db_make_set (&value, set);
  size = (*(tp_Set.lengthval)) (&value, 1);

  return size;
}


/*
 * tf_pack_set - transforms a DB_SET into its disk representation.
 *    return: error code
 *    set(in): set to pack
 *    buffer(out): destination buffer
 *    buffer_size(in): maximum length of buffer
 *    actual_bytes(out): number of bytes used or required
 * Note:
 *    If the set will fit within the buffer_size, no error is returned and
 *    actual_bytes is set to the number of bytes used.
 *    If the set will not fit in the buffer, ER_TF_BUFFER_OVERFLOW is returned
 *    and actual_bytes is set to the number of bytes that are required
 *    to pack this set.
 *    Other errors may occur as the set is examined.
 */
int
tf_pack_set (DB_SET * set, char *buffer, int buffer_size, int *actual_bytes)
{
  OR_BUF orep, *buf;
  int error;
  DB_VALUE value;

  if (set == NULL || set->set == NULL)
    {
      if (actual_bytes != NULL)
	{
	  *actual_bytes = 0;
	}
      return NO_ERROR;
    }

  /* set up a transformation buffer, may want to do deferred fixup here ? */
  buf = &orep;
  or_init (buf, buffer, buffer_size);
  buf->error_abort = 1;

  switch (_setjmp (buf->env))
    {
    case 0:
      error = NO_ERROR;

      /*
       * Doesn't matter which set function we pick, all the types are the same.
       * Don't have to pass in domain either since the type will be
       * self-describing.
       */
      db_make_set (&value, set);
      (*(tp_Set.writeval)) (buf, &value);

      if (actual_bytes != NULL)
	{
	  *actual_bytes = (int) (buf->ptr - buf->buffer);
	}
      break;

      /*
       * something happened, if it was ER_TF_BUFFER_OVERFLOW, return
       * the desired size as a negative number
       */

    case ER_TF_BUFFER_OVERFLOW:
      error = ER_TF_BUFFER_OVERFLOW;
      if (actual_bytes != NULL)
	{
	  DB_VALUE value;
	  db_make_set (&value, set);
	  *actual_bytes = (*(tp_Set.lengthval)) (&value, 1);
	}
      break;

    default:
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      break;
    }
  buf->error_abort = 0;

  return (error);
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * partition_to_disk - Write the disk representation of partition information
 *    return: NO_ERROR or error code
 *    buf(in/out): translation buffer
 *    partition_info(in):
 */
static int
partition_info_to_disk (OR_BUF * buf, SM_PARTITION * partition_info)
{
  char *start;
  int offset;
  DB_VALUE val;

  start = buf->ptr;
  /* VARIABLE OFFSET TABLE */
  offset = tf_Metaclass_partition.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_partition.mc_n_variable);

  db_make_sequence (&val, partition_info->values);

  or_put_offset (buf, offset);
  offset += string_disk_size (partition_info->pname);

  or_put_offset (buf, offset);
  offset += string_disk_size (partition_info->expr);

  or_put_offset (buf, offset);
  offset += or_packed_value_size (&val, 1, 1, 0);

  or_put_offset (buf, offset);
  offset += string_disk_size (partition_info->comment);

  or_put_offset (buf, offset);
  buf->ptr = PTR_ALIGN (buf->ptr, INT_ALIGNMENT);

  /* ATTRIBUTES */
  or_put_int (buf, partition_info->partition_type);

  put_string (buf, partition_info->pname);
  put_string (buf, partition_info->expr);

  or_put_value (buf, &val, 1, 1, 0);

  put_string (buf, partition_info->comment);

  if (start + offset != buf->ptr)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_TF_OUT_OF_SYNC, 0);
    }

  return NO_ERROR;
}

static inline void
partition_info_to_disk_lwriter (void *buf, void *partition_info)
{
  (void) partition_info_to_disk (STATIC_CAST (OR_BUF *, buf), STATIC_CAST (SM_PARTITION *, partition_info));
}

/*
 * partition_info_size - Calculates the disk size of a sm_partition structure.
 *    return: disk size
 *    partition_info(in):
 */
static int
partition_info_size (SM_PARTITION * partition_info)
{
  int size;
  DB_VALUE val;

  size = tf_Metaclass_partition.mc_fixed_size + OR_VAR_TABLE_SIZE (tf_Metaclass_partition.mc_n_variable);
  size += string_disk_size (partition_info->pname);
  size += string_disk_size (partition_info->expr);
  size += string_disk_size (partition_info->comment);

  db_make_sequence (&val, partition_info->values);
  size += or_packed_value_size (&val, 1, 1, 0);

  return (size);
}


/*
 * disk_to_partition_info - Reads the disk representation of partition
 * information and creates the memory representation.
 *    return: new sm_partition structure
 *    buf(in/out): translation buffer
 */
static SM_PARTITION *
disk_to_partition_info (OR_BUF * buf)
{
  SM_PARTITION *partition_info = NULL;
  OR_VARINFO *vars;
  DB_VALUE val;
  int error = NO_ERROR;

  vars = read_var_table (buf, tf_Metaclass_partition.mc_n_variable);
  if (vars == NULL)
    {
      or_abort (buf);
      return NULL;
    }

  assert (vars != NULL);

  partition_info = classobj_make_partition_info ();
  if (partition_info == NULL)
    {
      or_abort (buf);
    }
  else
    {
      partition_info->partition_type = or_get_int (buf, &error);
      if (error != NO_ERROR)
	{
	  free_var_table (vars);
	  classobj_free_partition_info (partition_info);
	  return NULL;
	}

      partition_info->pname = get_string (buf, vars[ORC_PARTITION_NAME_INDEX].length);
      partition_info->expr = get_string (buf, vars[ORC_PARTITION_EXPR_INDEX].length);

      error = or_get_value (buf, &val, NULL, vars[ORC_PARTITION_VALUES_INDEX].length, true);
      if (error != NO_ERROR)
	{
	  free_var_table (vars);
	  classobj_free_partition_info (partition_info);
	  return NULL;
	}
      partition_info->values = db_seq_copy (db_get_set (&val));
      if (partition_info->values == NULL)
	{
	  free_var_table (vars);
	  pr_clear_value (&val);
	  classobj_free_partition_info (partition_info);
	  return NULL;
	}

      partition_info->comment = get_string (buf, vars[ORC_PARTITION_COMMENT_INDEX].length);
    }

  pr_clear_value (&val);
  free_var_table (vars);
  return (partition_info);
}
