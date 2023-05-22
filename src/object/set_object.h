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
 * set_object.h - Set management
 */

#ifndef _SET_OBJECT_H_
#define _SET_OBJECT_H_

#ident "$Id$"

#include "config.h"

#include "dbtype_def.h"
#include "error_manager.h"
#include "object_domain.h"	/* for TP_DOMAIN */
#include "locator.h"		/* for LC_OIDSET */

#if !defined (SERVER_MODE)
#include "parser.h"		/* for PT_OP_TYPE */
#endif /* CS_MODE */

#define SET_DUPLICATE_VALUE (1)
#define IMPLICIT (1)

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
#define VALUETOP(col) ((col->topblock * COL_BLOCK_SIZE) + col->topblockcount)
#define COLBLOCKSIZE(n) (sizeof(COL_BLOCK) + (n * sizeof(DB_VALUE)))
#define BLOCK_START(block)      ((COL_BLOCK *) \
            ((char *)block - offsetof(struct collect_block, val)))

/* hack, need to check for a NULL OID pointer (a virtual object) before using
 * OID_ISTEMP.
 */
#define OBJECT_HAS_TEMP_OID(obj) \
  ((WS_OID(obj) == NULL) ? 0 : OID_ISTEMP(WS_OID(obj)))

typedef struct setobj SETOBJ;
struct setobj
{

  DB_TYPE coltype;
  int size;			/* valid indexes from 0 to size -1 aka the number of represented values in the
				 * collection */
  int lastinsert;		/* the last value insertion point 0 to size. */
  int topblock;			/* maximum index of an allocated block. This is the maximum non-NULL db_value pointer
				 * index of array. array[topblock] should be non-NULL. array[topblock+1] will be a NULL
				 * pointer for future expansion. */
  int arraytop;			/* maximum indexable pointer in array the valid indexes for array are 0 to arraytop
				 * inclusive Generally this may be greater than topblock */
  int topblockcount;		/* This is the max index of the top block Since it may be shorter than a standard sized
				 * block for space efficiency. */
  DB_VALUE **array;

  /* not stored on disk, attached at run time by the schema */
  struct tp_domain *domain;

  /* external reference list */
  DB_COLLECTION *references;

  /* clear if we can't guarantee sort order, always on for sequences */
  unsigned sorted:1;

  /* set if we can't guarantee that there are no temporary OID's in here */
  unsigned may_have_temporary_oids:1;
};
typedef SETOBJ COL;

typedef struct set_iterator
{
  struct set_iterator *next;	/* chain of iterators on this set */

  DB_SET *ref;			/* set we're iterating over */
  SETOBJ *set;			/* set object */
  DB_VALUE_LIST *element;	/* current list element */
  DB_VALUE *value;		/* current element pointer */
  int position;			/* current element index */
} SET_ITERATOR;



extern DB_COLLECTION *set_create (DB_TYPE type, int initial_size);
extern int set_area_init (void);
extern void set_area_final (void);
extern void set_area_reset ();
extern DB_COLLECTION *set_create_basic (void);
extern DB_COLLECTION *set_create_multi (void);
extern DB_COLLECTION *set_create_sequence (int size);
extern DB_COLLECTION *set_create_with_domain (TP_DOMAIN * domain, int initial_size);
extern DB_COLLECTION *set_make_reference (void);
extern DB_COLLECTION *set_copy (DB_COLLECTION * set);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int set_clear (DB_COLLECTION * set);
#endif
extern void set_free (DB_COLLECTION * set);
extern DB_COLLECTION *set_coerce (DB_COLLECTION * set, TP_DOMAIN * domain, bool implicit_coercion);
extern int set_filter (DB_COLLECTION * set);

/* Element access */

extern int set_get_element (DB_COLLECTION * set, int index, DB_VALUE * value);
extern int set_get_element_nocopy (DB_COLLECTION * set, int index, DB_VALUE * value);
extern int set_add_element (DB_COLLECTION * set, DB_VALUE * value);
#if defined (ENABLE_UNUSED_FUNCTION)
extern int set_add_element_quick (DB_COLLECTION * set, DB_VALUE * value);
#endif
extern int set_put_element (DB_COLLECTION * set, int index, DB_VALUE * value);
extern int set_insert_element (DB_COLLECTION * set, int index, DB_VALUE * value);
extern int set_drop_element (DB_COLLECTION * set, DB_VALUE * value, bool match_nulls);
extern int set_drop_seq_element (DB_COLLECTION * set, int index);
extern int set_find_seq_element (DB_COLLECTION * set, DB_VALUE * value, int index);

/* set operations */

extern int set_difference (DB_COLLECTION * collection1, DB_COLLECTION * collection2, DB_COLLECTION ** result,
			   DB_DOMAIN * domain);
extern int set_union (DB_COLLECTION * collection1, DB_COLLECTION * collection2, DB_COLLECTION ** result,
		      DB_DOMAIN * domain);
extern int set_intersection (DB_COLLECTION * collection1, DB_COLLECTION * collection2, DB_COLLECTION ** result,
			     DB_DOMAIN * domain);
extern void set_make_collection (DB_VALUE * value, DB_COLLECTION * col);

/* Information */

extern int set_cardinality (DB_COLLECTION * set);
extern int set_size (DB_COLLECTION * set);
extern bool set_isempty (DB_COLLECTION * set);
#if defined(ENABLE_UNUSED_FUNCTION)
extern bool set_is_all_null (DB_COLLECTION * set);
#endif
extern bool set_has_null (DB_COLLECTION * set);
extern bool set_ismember (DB_COLLECTION * set, DB_VALUE * value);
#if !defined (SERVER_MODE)
extern int set_issome (DB_VALUE * value, DB_COLLECTION * set, PT_OP_TYPE op, int do_coercion);
#endif /* !defined (SERVER_MODE) */
extern int set_convert_oids_to_objects (DB_COLLECTION * set);
extern DB_TYPE set_get_type (DB_COLLECTION * set);
extern DB_VALUE_COMPARE_RESULT set_compare_order (DB_COLLECTION * set1, DB_COLLECTION * set2, int do_coercion,
						  int total_order);
extern DB_VALUE_COMPARE_RESULT set_compare (DB_COLLECTION * set1, DB_COLLECTION * set2, int do_coercion);
extern DB_VALUE_COMPARE_RESULT set_seq_compare (DB_COLLECTION * set1, DB_COLLECTION * set2, int do_coercion,
						int total_order);
extern DB_VALUE_COMPARE_RESULT vobj_compare (DB_COLLECTION * set1, DB_COLLECTION * set2, int do_coercion,
					     int total_order);
extern TP_DOMAIN_STATUS set_check_domain (DB_COLLECTION * set, TP_DOMAIN * domain);
extern TP_DOMAIN *set_get_domain (DB_COLLECTION * set);

/* Debugging functions */
#ifdef __cplusplus
extern "C"
{
#endif
  extern void set_fprint (FILE * fp, DB_COLLECTION * set);
  extern void set_print (DB_COLLECTION * set);
#ifdef __cplusplus
}
#endif
/* shut down */

extern void set_final (void);

/* hack for post-sorting collections with temporary OIDs */

extern int set_optimize (DB_COLLECTION * ref);

/* These are lower level functions intended only for the transformer */

extern DB_VALUE_COMPARE_RESULT setvobj_compare (COL * set1, COL * set2, int do_coercion, int total_order);

/* intended for use only by the object manager (obj) */

extern int set_connect (DB_COLLECTION * ref, MOP owner, int attid, TP_DOMAIN * domain);
extern int set_disconnect (DB_COLLECTION * ref);
extern DB_COLLECTION *set_change_owner (DB_COLLECTION * ref, MOP owner, int attid, TP_DOMAIN * domain);
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
#if defined(ENABLE_UNUSED_FUNCTION)
extern int set_iterator_finished (SET_ITERATOR * it);

#if !defined(SERVER_MODE)
extern int setobj_find_temporary_oids (SETOBJ * col, LC_OIDSET * oidset);
#endif /* SERVER_MODE */
#endif /* ENABLE_UNUSED_FUNCTION */

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
extern bool setobj_ismember (COL * col, DB_VALUE * proposed_value, int check_null);
extern DB_VALUE_COMPARE_RESULT setobj_compare (COL * set1, COL * set2, int do_coercion);
extern DB_VALUE_COMPARE_RESULT setobj_compare_order (COL * set1, COL * set2, int do_coercion, int total_order);
extern int setobj_difference (COL * set1, COL * set2, COL * result);
extern int setobj_union (COL * set1, COL * set2, COL * result);
extern int setobj_intersection (COL * set1, COL * set2, COL * result);
#if !defined (SERVER_MODE)
extern int setobj_issome (DB_VALUE * value, COL * set, PT_OP_TYPE op, int do_coercion);
#endif /* defined (CS_MODE) */
extern int setobj_convert_oids_to_objects (COL * col);
extern int setobj_get_element_ptr (COL * col, int index, DB_VALUE ** result);
extern int setobj_get_element (COL * set, int index, DB_VALUE * value);
extern int setobj_add_element (COL * col, DB_VALUE * value);
extern int setobj_put_element (COL * col, int index, DB_VALUE * value);
extern int setobj_insert_element (COL * col, int index, DB_VALUE * value);
extern int setobj_drop_element (COL * col, DB_VALUE * value, bool match_nulls);
extern int setobj_drop_seq_element (COL * col, int index);
extern int setobj_find_seq_element (COL * col, DB_VALUE * value, int index);
extern COL *setobj_coerce (COL * col, TP_DOMAIN * domain, bool implicit_coercion);
extern void setobj_print (FILE * fp, COL * col);
extern TP_DOMAIN *setobj_domain (COL * set);
extern void setobj_put_domain (COL * set, TP_DOMAIN * domain);
extern int setobj_put_value (COL * col, int index, DB_VALUE * value);
extern DB_COLLECTION *setobj_get_reference (COL * set);
extern int setobj_release (COL * set);
extern int setobj_build_domain_from_col (COL * col, TP_DOMAIN ** set_domain);

#endif /* _SET_OBJECT_H_ */
