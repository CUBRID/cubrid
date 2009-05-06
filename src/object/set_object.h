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
 * set_object.h - Set management
 */

#ifndef _SET_OBJECT_H_
#define _SET_OBJECT_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "object_representation.h"
#include "object_domain.h"	/* for TP_DOMAIN */
#include "parser.h"		/* for PT_OP_TYPE */
#include "locator.h"		/* for LC_OIDSET */
#include "area_alloc.h"

#define SET_DUPLICATE_VALUE (1)
#define IMPLICIT (1)

/* Quick set argument check */

#define CHECKNULL(thing) \
  if ((thing) == NULL) { \
    er_set(ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0); \
    return(ER_OBJ_INVALID_ARGUMENTS); \
    }

/* this needs to go into the pr_ level */

#define SET_FIX_VALUE(value) \
  if (((DB_VALUE_TYPE(value) == DB_TYPE_STRING) && \
       (DB_GET_STRING(value) == NULL)) || \
      (((DB_VALUE_TYPE(value) == DB_TYPE_SET) || \
        (DB_VALUE_TYPE(value) == DB_TYPE_MULTI_SET) || \
        (DB_VALUE_TYPE(value) == DB_TYPE_SEQUENCE)) && \
       DB_GET_SET(value) == NULL) || \
      ((DB_VALUE_TYPE(value) == DB_TYPE_OBJECT) && \
       (DB_GET_OBJECT(value) == NULL)) || \
      ((DB_VALUE_TYPE(value) == DB_TYPE_ELO) && \
       (DB_GET_ELO(value) == NULL)) ) \
    DB_MAKE_NULL(value);

#define COL_BLOCK_SIZE (64)

/* Is there some compelling reason to keep the value array larger than necessary?
 * this will suck proportionally with the size of the collection
 * Leaving it as is for now, not sure if the code relies on this being 1 larger
 * than the necessary size.
 */
#define EXPAND(blockindex) ((int) (((blockindex)*1.1) + 1))

#define BLOCK(collection_index) ((int) ((collection_index)/COL_BLOCK_SIZE))
#define OFFSET(collection_index) ((int) ((collection_index)%COL_BLOCK_SIZE))
#define INDEX(collection,index) (&(collection->array[BLOCK(index)][OFFSET(index)]))
#define BLOCKING_LESS1 (COL_BLOCK_SIZE -1)
#define VALUETOP(col) ((col->topblock*COL_BLOCK_SIZE)+col->topblockcount)
#define COLBLOCKSIZE(n) (sizeof(COL_BLOCK) + (n * sizeof(DB_VALUE)))
#define BLOCK_START(block)      ((COL_BLOCK *) \
            ((char *)block - offsetof(struct collect_block, val)))

/* hack, need to check for a NULL OID pointer (a virtual object) before using
 * OID_ISTEMP.
 */
#define OBJECT_HAS_TEMP_OID(obj) \
  ((WS_OID(obj) == NULL) ? 0 : OID_ISTEMP(WS_OID(obj)))


typedef struct set_iterator
{
  struct set_iterator *next;	/* chain of iterators on this set */

  DB_SET *ref;			/* set we're iterating over */
  SETOBJ *set;			/* set object */
  DB_VALUE_LIST *element;	/* current list element */
  DB_VALUE *value;		/* current element pointer */
  int position;			/* current element index */
} SET_ITERATOR;

/*
 * struct setobj
 * The internal structure of a setobj data struct is private to this module.
 * all access to this structure should be encapsulated via function calls.
 */
typedef SETOBJ COL;

struct setobj
{

  DB_TYPE coltype;
  int size;			/* valid indexes from 0 to size -1
				 * aka the number of represented values in
				 * the collection */
  int lastinsert;		/* the last value insertion point
				 * 0 to size. */
  int topblock;			/* maximum index of an allocated block.
				 * This is the maximum non-NULL db_value
				 * pointer index of array. array[topblock]
				 * should be non-NULL. array[topblock+1] will
				 * be a NULL pointer for future expansion.
				 */
  int arraytop;			/* maximum indexable pointer in array the valid
				 * indexes for array are 0 to arraytop
				 * inclusive Generally this may be greater
				 * than topblock */
  int topblockcount;		/* This is the max index of the top block
				 * Since it may be shorter than a standard
				 * sized block for space efficicency.
				 */
  DB_VALUE **array;

  /* not stored on disk, attached at run time by the schema */
  struct tp_domain *domain;

  /* external reference list */
  DB_COLLECTION *references;

  /* to prevent objects in disconnected sets from being gc'd */
  struct db_object *gc_kludge;

  /* clear if we can't guarentee sort order, always on for sequences */
  unsigned sorted:1;

  /* set if we can't guarentee that there are no temporary OID's in here */
  unsigned may_have_temporary_oids:1;
};

/* Creation */
extern AREA *Set_Ref_Area;	/* Area for allocation of set reference structures */
extern AREA *Set_Obj_Area;	/* Area for allocation of set object structures */

extern DB_COLLECTION *set_create (DB_TYPE type, int initial_size);
extern void set_area_init (void);
extern DB_COLLECTION *set_create_basic (void);
extern DB_COLLECTION *set_create_multi (void);
extern DB_COLLECTION *set_create_sequence (int size);
extern DB_COLLECTION *set_create_with_domain (TP_DOMAIN * domain,
					      int initial_size);
extern DB_COLLECTION *set_make_reference (void);
extern DB_COLLECTION *set_copy (DB_COLLECTION * set);
extern int set_clear (DB_COLLECTION * set);
extern void set_free (DB_COLLECTION * set);
extern DB_COLLECTION *set_coerce (DB_COLLECTION * set, TP_DOMAIN * domain,
				  bool implicit_coercion);
extern int set_filter (DB_COLLECTION * set);

/* Element access */

extern int set_get_element (DB_COLLECTION * set, int index, DB_VALUE * value);
extern int set_get_element_nocopy (DB_COLLECTION * set, int index,
				   DB_VALUE * value);
extern int set_midxkey_get_element_nocopy (const DB_MIDXKEY * midxkey,
					   int index, DB_VALUE * value,
					   int *prev_indexp,
					   char **prev_ptrp);
extern int set_midxkey_add_elements (DB_VALUE * keyval, DB_VALUE * dbvals,
				     int num_dbvals,
				     TP_DOMAIN * dbvals_domain_list,
				     TP_DOMAIN * domain);
extern int set_add_element (DB_COLLECTION * set, DB_VALUE * value);
extern int set_add_element_quick (DB_COLLECTION * set, DB_VALUE * value);
extern int set_put_element (DB_COLLECTION * set, int index, DB_VALUE * value);
extern int set_insert_element (DB_COLLECTION * set, int index,
			       DB_VALUE * value);
extern int set_drop_element (DB_COLLECTION * set, DB_VALUE * value,
			     bool match_nulls);
extern int set_drop_seq_element (DB_COLLECTION * set, int index);
extern int set_find_seq_element (DB_COLLECTION * set, DB_VALUE * value,
				 int index);

/* set operations */

extern int set_difference (DB_COLLECTION * collection1,
			   DB_COLLECTION * collection2,
			   DB_COLLECTION ** result, DB_DOMAIN * domain);
extern int set_union (DB_COLLECTION * collection1,
		      DB_COLLECTION * collection2,
		      DB_COLLECTION ** result, DB_DOMAIN * domain);
extern int set_intersection (DB_COLLECTION * collection1,
			     DB_COLLECTION * collection2,
			     DB_COLLECTION ** result, DB_DOMAIN * domain);
extern void set_make_collection (DB_VALUE * value, DB_COLLECTION * col);

/* Information */

extern int set_cardinality (DB_COLLECTION * set);
extern int set_size (DB_COLLECTION * set);
extern bool set_isempty (DB_COLLECTION * set);
extern bool set_is_all_null (DB_COLLECTION * set);
extern bool set_has_null (DB_COLLECTION * set);
extern bool set_ismember (DB_COLLECTION * set, DB_VALUE * value);
extern int set_issome (DB_VALUE * value, DB_COLLECTION * set,
		       PT_OP_TYPE op, int do_coercion);
extern int set_convert_oids_to_objects (DB_COLLECTION * set);
extern DB_TYPE set_get_type (DB_COLLECTION * set);
extern int set_compare_order (DB_COLLECTION * set1, DB_COLLECTION * set2,
			      int do_coercion, int total_order);
extern int set_compare (DB_COLLECTION * set1, DB_COLLECTION * set2,
			int do_coercion);
extern int set_seq_compare (DB_COLLECTION * set1, DB_COLLECTION * set2,
			    int do_coercion, int total_order);
extern int vobj_compare (DB_COLLECTION * set1, DB_COLLECTION * set2,
			 int do_coercion, int total_order);
extern TP_DOMAIN_STATUS set_check_domain (DB_COLLECTION * set,
					  TP_DOMAIN * domain);
extern TP_DOMAIN *set_get_domain (DB_COLLECTION * set);

/* Debugging functions */

extern void set_fprint (FILE * fp, DB_COLLECTION * set);
extern void set_print (DB_COLLECTION * set);

/* shut down */

extern void set_final (void);

/* hack for post-sorting collections with temporary OIDs */

extern int set_optimize (DB_COLLECTION * ref);

/* These are lower level functions intended only for the transformer */

extern int setvobj_compare (COL * set1, COL * set2, int do_coercion,
			    int total_order);

/* intended for use only by the object manager (obj) */

extern int set_connect (DB_COLLECTION * ref, MOP owner, int attid,
			TP_DOMAIN * domain);
extern int set_disconnect (DB_COLLECTION * ref);
extern DB_COLLECTION *set_change_owner (DB_COLLECTION * ref, MOP owner,
					int attid, TP_DOMAIN * domain);
extern int set_get_setobj (DB_COLLECTION * ref, COL ** setptr, int for_write);
/* hacks for loaddb */

extern DB_VALUE *set_new_element (DB_COLLECTION * ref);

extern SET_ITERATOR *set_iterate (DB_COLLECTION * set);
extern void set_iterator_free (SET_ITERATOR * it);
extern DB_VALUE *set_iterator_value (SET_ITERATOR * it);
extern int set_iterator_next (SET_ITERATOR * it);
extern void set_free_block (DB_VALUE * in_block);

extern int col_value_compare (DB_VALUE * a, DB_VALUE * b);

extern int col_sort (COL * col);
extern int col_expand (COL * col, long i);
extern COL *col_new (long size, int settype);
extern int col_has_null (COL * col);

extern long col_find (COL * col, long *found, DB_VALUE * val, int do_coerce);
extern int col_put (COL * col, long colindex, DB_VALUE * val);

extern int col_insert (COL * col, long colindex, DB_VALUE * val);
extern int col_delete (COL * col, long colindex);
extern int col_add (COL * col, DB_VALUE * val);
extern int col_drop (COL * col, DB_VALUE * val);
extern int col_drop_nulls (COL * col);
extern int col_permanent_oids (COL * col);

extern int set_tform_disk_set (DB_COLLECTION * ref, COL ** setptr);
extern int set_iterator_finished (SET_ITERATOR * it);

#if !defined(SERVER_MODE)
extern int setobj_find_temporary_oids (SETOBJ * col, LC_OIDSET * oidset);
extern void setobj_gc (SETOBJ * set, void (*gcmarker) (MOP));
extern void set_gc (SETREF * ref, void (*gcmarker) (MOP));
#endif

extern COL *setobj_create_with_domain (TP_DOMAIN * domain, int initial_size);
extern COL *setobj_create (DB_TYPE collection_type, int size);
extern void setobj_clear (COL * col);
extern void setobj_free (COL * col);
extern COL *setobj_copy (COL * col);
extern int setobj_sort (COL * col);
extern TP_DOMAIN_STATUS setobj_check_domain (COL * col, TP_DOMAIN * domain);
extern TP_DOMAIN *setobj_get_domain (COL * set);
extern void swizzle_value (DB_VALUE * val, int input);
extern int setobj_filter (COL * col, int filter, int *cardptr);
extern int setobj_size (COL * col);
extern int setobj_cardinality (COL * col);
extern bool setobj_isempty (COL * col);
extern bool setobj_ismember (COL * col, DB_VALUE * proposed_value,
			     int check_null);
extern int setobj_compare (COL * set1, COL * set2, int do_coercion);
extern int setobj_compare_order (COL * set1, COL * set2,
				 int do_coercion, int total_order);
extern int setobj_difference (COL * set1, COL * set2, COL * result);
extern int setobj_union (COL * set1, COL * set2, COL * result);
extern int setobj_intersection (COL * set1, COL * set2, COL * result);
extern int setobj_issome (DB_VALUE * value, COL * set, PT_OP_TYPE op,
			  int do_coercion);
extern int setobj_convert_oids_to_objects (COL * col);
extern int setobj_get_element_ptr (COL * col, int index, DB_VALUE ** result);
extern int setobj_get_element (COL * set, int index, DB_VALUE * value);
extern int setobj_add_element (COL * col, DB_VALUE * value);
extern int setobj_put_element (COL * col, int index, DB_VALUE * value);
extern int setobj_insert_element (COL * col, int index, DB_VALUE * value);
extern int setobj_drop_element (COL * col, DB_VALUE * value,
				bool match_nulls);
extern int setobj_drop_seq_element (COL * col, int index);
extern int setobj_find_seq_element (COL * col, DB_VALUE * value, int index);
extern COL *setobj_coerce (COL * col, TP_DOMAIN * domain,
			   bool implicit_coercion);
extern void setobj_print (FILE * fp, COL * col);
extern DB_TYPE setobj_type (COL * set);
extern TP_DOMAIN *setobj_domain (COL * set);
extern void setobj_put_domain (COL * set, TP_DOMAIN * domain);
extern int setobj_put_value (COL * col, int index, DB_VALUE * value);
extern DB_COLLECTION *setobj_get_reference (COL * set);
extern int setobj_release (COL * set);
extern void setobj_assigned (COL * col);
extern int setobj_midxkey_get_element (const DB_MIDXKEY * midxkey,
				       int index, DB_VALUE * value,
				       bool copy,
				       int *prev_indexp, char **prev_ptrp);

#endif /* _SET_OBJECT_H_ */
