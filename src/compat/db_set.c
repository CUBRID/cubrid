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
 * db_set.c - API functions for manipulating sets & sequences.
 */

#include "db_set.h"
#include "db_set_function.h"

#if !defined (SERVER_MODE)
#include "authenticate.h"
#endif // not SERVER_MODE
#if !defined (SERVER_MODE)
#include "class_object.h"
#endif // SERVER_MODE
#include "db.h"
#include "dbtype.h"
#include "error_manager.h"
#include "object_primitive.h"
#include "set_object.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define ERROR_SET(error, code) \
  do {                     \
    error =  code;         \
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 0); \
  } while (0)

#define ERROR_SET1(error, code, arg1) \
  do {                            \
    error = code;                 \
    er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE, code, 1, arg1); \
  } while (0)

/*
 *  SET CREATION FUNCTIONS
 */

/*
 * db_set_create() - This function creates an empty set for an attribute.
 *    The type of set(set, multiset, or sequence) is determined by examining
 *    the attribute that is specified.
 * return : set descriptor
 * classop(in): class or instance pointer
 * name(in): attribute name
 *
 * note : The returned set is not immediately given to the attribute
 *   it must be given to an attribute later using a db_put() call
 */
DB_SET *
db_set_create (MOP classop, const char *name)
{
  DB_SET *set;
#if !defined(SERVER_MODE)
  int error = NO_ERROR;
#endif

  CHECK_CONNECT_NULL ();

  set = NULL;
  if (classop != NULL && name != NULL)
    {
#if !defined(SERVER_MODE)
      SM_CLASS *class_;
      SM_ATTRIBUTE *att;

      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att == NULL)
	    {
	      ERROR_SET1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
	    }
	  else
	    {
	      if (att->type->id == DB_TYPE_SET)
		{
		  set = set_create_basic ();
		}
	      else if (att->type->id == DB_TYPE_MULTISET)
		{
		  set = set_create_multi ();
		}
	      else if (att->type->id == DB_TYPE_SEQUENCE)
		{
		  set = set_create_sequence (0);
		}
	      else
		{
		  ERROR_SET1 (error, ER_OBJ_DOMAIN_CONFLICT, name);
		}
	    }
	}
#endif
    }

  return (set);
}

/*
 * db_set_create_basic() - This function creates a basic set for an attribute.
 *    The class and name arguments can be set to NULL. If values are supplied,
 *    a check will be made to make sure that the attribute was defined with the
 *    set domain.
 * return : set descriptor
 * classop(in): class or instance
 * name(in): attribute name
 *
 * note : The new set will not be attached to any object, so you must use the
 *   db_put() function to assign it as the value of an attribute.
 */
DB_SET *
db_set_create_basic (MOP classop, const char *name)
{
  DB_SET *set;
#if !defined(SERVER_MODE)
  int error = NO_ERROR;
#endif

  CHECK_CONNECT_NULL ();

  set = NULL;
  if (classop == NULL || name == NULL)
    {
      set = set_create_basic ();
    }
  else
    {
#if !defined(SERVER_MODE)
      SM_CLASS *class_;
      SM_ATTRIBUTE *att;

      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att == NULL)
	    {
	      ERROR_SET1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
	    }
	  else
	    {
	      if (att->type->id == DB_TYPE_SET)
		{
		  set = set_create_basic ();
		}
	      else
		{
		  ERROR_SET1 (error, ER_OBJ_DOMAIN_CONFLICT, name);
		}
	    }
	}
#endif
    }

  return (set);
}

/*
 * db_set_create_multi() - This function creates an empty multiset. The class
 *    and name arguments can be set to NULL. If values are supplied, a check
 *    will be made to make sure that the attribute was defined with the
 *    multiset domain.
 * return : set descriptor
 * classop(in): class or instance
 * name(in): attribute name
 *
 * note : The new set will not be attached to any object, so you must use the
 *    db_put() function to assign it as the value of an attribute.
 */
DB_SET *
db_set_create_multi (MOP classop, const char *name)
{
  DB_SET *set;
#if !defined(SERVER_MODE)
  int error = NO_ERROR;
#endif

  CHECK_CONNECT_NULL ();

  set = NULL;
  if (classop == NULL || name == NULL)
    {
      set = set_create_multi ();
    }
  else
    {
#if !defined(SERVER_MODE)
      SM_CLASS *class_;
      SM_ATTRIBUTE *att;

      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att == NULL)
	    {
	      ERROR_SET1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
	    }
	  else
	    {
	      if (att->type->id == DB_TYPE_MULTISET)
		{
		  set = set_create_multi ();
		}
	      else
		{
		  ERROR_SET1 (error, ER_OBJ_DOMAIN_CONFLICT, name);
		}
	    }
	}
#endif
    }

  return (set);
}

/*
 * db_seq_create() - This function creates an empty sequence. The class and
 *    name arguments can be set to NULL. If values are supplied, a check will
 *    be made to make sure that the attribute was defined with the sequence
 *    domain.
 * return : a set (sequence) descriptor
 * classop(in): class or instance
 * name(in): attribute name
 * size(in): initial size
 *
 * note : The new set will not be attached to any object, so you must use the
 *    db_put( ) function to assign it as the value of an attribute. If the size
 *    is not known, it is permissible to pass zero.
 */
DB_SET *
db_seq_create (MOP classop, const char *name, int size)
{
  DB_SET *set;
#if !defined(SERVER_MODE)
  int error = NO_ERROR;
#endif

  CHECK_CONNECT_NULL ();

  set = NULL;
  if (classop == NULL || name == NULL)
    {
      set = set_create_sequence (size);
    }
  else
    {
#if !defined(SERVER_MODE)
      SM_CLASS *class_;
      SM_ATTRIBUTE *att;

      if (au_fetch_class (classop, &class_, AU_FETCH_READ, AU_SELECT) == NO_ERROR)
	{
	  att = classobj_find_attribute (class_, name, 0);
	  if (att == NULL)
	    {
	      ERROR_SET1 (error, ER_OBJ_INVALID_ATTRIBUTE, name);
	    }
	  else
	    {
	      if (att->type->id == DB_TYPE_SEQUENCE)
		{
		  set = set_create_sequence (size);
		}
	      else
		{
		  ERROR_SET1 (error, ER_OBJ_DOMAIN_CONFLICT, name);
		}
	    }
	}
#endif
    }

  return (set);
}

/*
 * db_set_free() - This function frees a set handle. If the set is owned by an
 *    object, the contents of the set are not freed, only the set handle is
 *    freed. If the set is not owned by an object, the handle and all of the
 *    elements are freed.
 * return : error code
 * set(in): set descriptor
 */
int
db_set_free (DB_SET * set)
{
  /* don't check connection here, we always allow things to be freed */
  if (set != NULL)
    {
      set_free (set);
    }

  return (NO_ERROR);
}

/*
 * db_seq_free() -
 * return : error code
 * seq(in): set descriptor
 */
int
db_seq_free (DB_SEQ * seq)
{
  /* don't check connection here, we always allow things to be freed */
  if (seq != NULL)
    {
      set_free (seq);
    }

  return (NO_ERROR);
}

/*
 * db_set_filter() - This function causes set elements containing references
 *    to deleted objects to be completely removed from the designated set.
 *    The system does not automatically filter sets containing references to
 *    deleted objects; if you do not call this function, make sure that your
 *    application code is prepared to encounter deleted objects.
 * return : error code
 * set(in): set to filter
 */
int
db_set_filter (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  retval = (set_filter (set));

  return (retval);
}

/*
 * db_seq_filter() - This function causes set elements containing references
 *    to deleted objects to be completely removed from the designated set.
 *    The system does not automatically filter sets containing references to
 *    deleted objects; if you do not call this function, make sure that your
 *    application code is prepared to encounter deleted objects.
 * return : error code
 * set(in): set to filter
 */
int
db_seq_filter (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  retval = (set_filter (set));

  return (retval);
}

/*
 * db_set_copy() - This function makes a copy of the given set. The copied set
 *    will be a free set that is not owned by an object and will not be
 *    persistent in the database.
 * return : a new set
 * source(in): a set to copy
 *
 * note : To make the set persistent, you must assign it as the value of an
 *   object attribute. If you do not assign the set to an attribute, it must be
 *   freed manually with db_set_free function
 */
DB_SET *
db_set_copy (DB_SET * source)
{
  DB_SET *new_ = NULL;

  CHECK_CONNECT_NULL ();

  if (source != NULL)
    {
      new_ = set_copy (source);
    }

  return (new_);
}

/*
 * db_seq_copy() - This function makes a copy of the given source. The copied
 *    sequence will be a free set that is not owned by an object and will not
 *    be persistent in the database.
 * return : a new sequence
 * source(in): a sequence to copy
 *
 * note : To make the sequence persistent, you must assign it as the value of
 *   an object attribute. If you do not assign the sequence to an attribute,
 *   it must be freed manually with db_seq_free function
 */
DB_SEQ *
db_seq_copy (DB_SEQ * source)
{
  DB_SEQ *new_ = NULL;

  CHECK_CONNECT_NULL ();

  if (source != NULL)
    {
      new_ = set_copy (source);
    }

  return (new_);
}

/*
 *  SET/MULTI-SET FUNCTIONS
 */

/*
 * db_set_add() - This function adds an element to a set or multiset. If the
 *    set is a basic set and the value already exists in the set, a zero is
 *    returned, indicating that no error occurred. If the set is a multiset,
 *    the value will be added even if it already exists. If the set has been
 *    assigned as the value of an attribute, the domain of the value is first
 *    checked against the attribute domain. If they do not match, a zero is
 *    returned, indicating that no error occurred.
 * return : error code
 * set(in): set descriptor
 * value(in): value to add
 *
 * note : you may not make any assumptions about the position of the value
 *    within the set; it will be added wherever the system determines is most
 *    convenient. If you need to have sets with ordered elements, you must use
 *    sequences. Sets and multi-sets cannot contain NULL elements, if the value
 *    has a basic type of DB_TYPE_NULL, an error is returned.
 */
int
db_set_add (DB_SET * set, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  if ((value != NULL) && (DB_VALUE_TYPE (value) > DB_TYPE_LAST))
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = set_add_element (set, value);
    }

  return (error);
}

/*
 * db_set_get() - This function gets the value of an element of a set or
 *    multiset. This is the only set or multiset function that accepts an
 *    index. The first element of the set is accessed with an index of 0
 *    (zero). The index is used to sequentially retrieve elements and assumes
 *    that the set will not be modified for the duration of the set iteration
 *    loop. You cannot assume that the elements of the set remain in any
 *    particular order after a db_set_add statement.
 * return : error code
 * set(in): set descriptor
 * index(in) : element index
 * value(out): value container to be filled in with element value
 *
 * note : The supplied value container is filled in with a copy of the set
 *    element contents and must be freed with db_value_clear or db_value_free
 *    when the value is no longer required.
 */
int
db_set_get (DB_SET * set, int index, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (set, value);

  retval = (set_get_element (set, index, value));

  return (retval);
}

/*
 * db_set_drop() - This function removes the first matching element from a set
 *    or multiset. If no element matches the supplied value, a zero is
 *    returned, indicating no error occurred. If more than one element matches,
 *    only the first one is removed.
 * return : error code
 * set(in): set descriptor
 * value(in): value to drop from the set
 */
int
db_set_drop (DB_SET * set, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  retval = (set_drop_element (set, value, false));

  return (retval);
}

/*
 * db_set_size() - This function returns the total number of elements in a set,
 *    including elements that may have a NULL value. This function should
 *    always be used prior to using program loops to iterate over the set
 *    elements.
 * return : total number of elements in the set
 * set(in): set descriptor
 */
int
db_set_size (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (set);

  /* allow all types */
  retval = (set_size (set));

  return (retval);
}

/*
 * db_set_cardinality() - The cardinality of a set is defined as the number of
 *    non-null elements, and this function returns the number of non-null
 *    elements in the set.
 * return : number of non-null elements in the set
 * set(in): set descriptor
 */
int
db_set_cardinality (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (set);

  /* allow all types */
  retval = (set_cardinality (set));

  return (retval);
}

/*
 * db_set_ismember() - This function checks to see if a value is found in a set
 * return : non-zero if the value is in the set
 * set(in): set descriptor
 * value(in): value to test
 */
int
db_set_ismember (DB_SET * set, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_FALSE ();
  CHECK_1ARG_FALSE (set);

  /* allow all types */
  retval = (set_ismember (set, value)) ? 1 : 0;

  return (retval);
}

/*
 * db_set_isempty() - This function checks to see if a set is empty.  The set
 *    (or sequence) must have no elements at all in order for this to be true.
 *    If this is a sequence and there are only NULL elements, this function
 *    will return false since NULL elements are still considered valid elements
 *    for sequences.
 * return : non-zero if the set has no elements
 * set(in): set descriptor
 */
int
db_set_isempty (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_FALSE ();
  CHECK_1ARG_FALSE (set);

  /* allow all types */
  retval = (set_isempty (set)) ? 1 : 0;

  return (retval);
}

/*
 * db_set_has_null() - This function checks to see if a set is empty.  The set
 *    (or sequence) must have no elements at all in order for this to be true.
 *    If this is a sequence and there are only NULL elements, this function
 *    will return false since NULL elements are still considered valid elements
 *    for sequences.
 * return : non-zero if the set has no elements
 * set(in): set descriptor
 */
int
db_set_has_null (DB_COLLECTION * set)
{
  int retval;

  CHECK_CONNECT_FALSE ();
  CHECK_1ARG_FALSE (set);

  /* allow all types */
  retval = (set_has_null (set)) ? 1 : 0;

  return (retval);
}

/*
 * db_set_print() - This is a debugging function that prints a simple
 *    description of a set. This should be used for information purposes only.
 * return : error code
 * set(in): set descriptor
 */
int
db_set_print (DB_SET * set)
{
  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* allow all types */
  set_print (set);

  return (NO_ERROR);
}

/*
 * db_set_type() - This function returns the type identifier for a set. This
 *    can be used in places where it is not known if a set descriptor is for
 *    a set, multi-set or sequence.
 * return : set type identifier
 * set(in): set descriptor
 */
DB_TYPE
db_set_type (DB_SET * set)
{
  DB_TYPE type = DB_TYPE_NULL;

  if (set != NULL)
    {
      type = set_get_type (set);
    }

  return (type);
}

/*
 * SEQUENCE FUNCTIONS
 */

/*
 * db_seq_get() - This function retrieves the value of a sequence element.
 *    The first element of the sequence is accessed with an index of 0 (zero).
 * return : error code
 * set(in): sequence identifier
 * index(in): element index
 * value(out): value to be filled in with element contents
 *
 * note : The value will be copied from the sequence, so it must be released
 *    using either function db_value_clear() or db_value_free() when it is
 *    no longer needed.
 */
int
db_seq_get (DB_SET * set, int index, DB_VALUE * value)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (set, value);

  /* should make sure this is a sequence, probably introduce another set_ level function to do this rather than
   * checking the type here */
  retval = (set_get_element (set, index, value));

  return (retval);
}

/*
 * db_seq_put() - This function puts a value in a sequence at a fixed position.
 *    The value will always remain in the specified position so you can use
 *    db_seq_get() with the same index to retrieve it at a later time.
 *    This will overwrite the current value at that position if one exists. The
 *    value can be of type DB_TYPE_NULL, in which case the current element will
 *    be cleared and set to NULL.
 * return : error code
 * set(in): sequence descriptor
 * index(in): element index
 * value(in): value to put in the sequence
 *
 * note : The domain of the value must be compatible with the domain of the set
 *   (if the set has been assigned to an attribute). If the set does not have
 *   an element with the specified index, it will automatically grow to be as
 *   large as the given index. The empty elements (if any) between the former
 *   length of the set and the new index will be set to DB_TYPE_NULL.
 */
int
db_seq_put (DB_SET * set, int index, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  if ((value != NULL) && (DB_VALUE_TYPE (value) > DB_TYPE_LAST))
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = set_put_element (set, index, value);
    }

  return (error);
}

/*
 * db_seq_insert() - This function inserts a value into a sequence at the
 *    specified position. All elements starting from that position will be
 *    shifted down to make room for the new element. As with other sequence
 *    building functions, the domain of the value must be compatible with the
 *    domain of the sequence. The sequence will automatically grow to make room
 *    for the new value. Any existing slots that contain NULL will not be
 *    reused because NULL is a valid sequence element. If the index is beyond
 *    the length of the sequence, the sequence will grow and the value is
 *    appended.
 * return : error code
 * set(in): sequence descriptor
 * index(in): element index
 * value(in): value to insert
 */
int
db_seq_insert (DB_SET * set, int index, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  if ((value != NULL) && (DB_VALUE_TYPE (value) > DB_TYPE_LAST))
    {
      ERROR_SET (error, ER_OBJ_INVALID_ARGUMENTS);
    }
  else
    {
      error = set_insert_element (set, index, value);
    }

  return (error);
}

/*
 * db_seq_drop() - This function removes an element from a sequence. Any
 *    elements following the indexed element will be shifted up to take up
 *    the space. The length of the set will be decreased by one.
 * return : error code
 * set(in): sequence descriptor
 * index(in): element index
 *
 * note : If you want to clear an element without shifting subsequent elements
 *   up, use db_seq_put() with a value of DB_TYPE_NULL.
 */
int
db_seq_drop (DB_SET * set, int index)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  /* Check if modifications are disabled only if the set is owned */
  if (set->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  retval = (set_drop_seq_element (set, index));

  return (retval);
}

/*
 * db_seq_size() - This function returns the total number of slots allocated
 *    for a sequence.
 * return : total number of elements in the sequence
 * set(in): sequence descriptor
 */
int
db_seq_size (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (set);

  retval = (set_size (set));

  return (retval);
}

/*
 * db_seq_cardinality() - This function returns only the number of non-null
 *    elements in the sequence.
 * return : number of non-null elements in the sequence
 * set(in): sequence descriptor
 */
int
db_seq_cardinality (DB_SET * set)
{
  int retval;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (set);

  retval = (set_cardinality (set));

  return (retval);
}

/*
 * db_seq_print() - This is debug function to print out a simple description of
 *    a sequence.
 * return : error code
 * set(in): sequence descriptor
 */
int
db_seq_print (DB_SET * set)
{
  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (set);

  set_print (set);

  return (NO_ERROR);
}

/*
 * db_seq_find() - This function can be used to sequentially search for
 *    elements in a sequence that match a particular value. To search
 *    for duplicate elements in a sequence, you can call this function multiple
 *    times and set the index parameter to one greater than the number returned
 *    by the previous call to this function.
 * return: the index of the element or error code if not found
 * set(in): sequence descriptor
 * value(in): value to search for
 * index(in): starting index (zero if starting from the beginning)
 */
int
db_seq_find (DB_SET * set, DB_VALUE * value, int index)
{
  int retval;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (set, value);

  retval = (set_find_seq_element (set, value, index));

  return (retval);
}

/*
 *  GENERIC COLLECTION FUNCTIONS
 */

/*
 * The new DB_COLLECTION style of collection maintenance is preferred over
 * the old SET/MULTISET/SEQUENCE distinction.  Its easier to code against
 * and makes switching between collection types easier.
 *
 */

/*
 * db_col_create() - This is the primary function for constructing a new
 *    collection. Type should be DB_TYPE_SET, DB_TYPE_MULTISET, or
 *    DB_TYPE_SEQUENCE. Size should always be specified if known before
 *    hand as it will significantly improve performance when incrementally
 *    building collections. Domain is optional. If NULL, it is assumed that
 *    this is a "wildcard" collection and can contain elements of any domain.
 *    The elements already existing in the collection should be within the
 *    supplied domain.If a domain is supplied, the type argument is ignored.
 * return : collection (NULL if error)
 * type(in): one of the DB_TYPE_ collection types
 * size(in): initial size to preallocate (zero if unknown)
 * domain(in): fully specified domain (optional)
 *
 * note : The collection handle returned must be freed using the db_col_free()
 *    function when the collection handle is no longer necessary.
 */
DB_COLLECTION *
db_col_create (DB_TYPE type, int size, DB_DOMAIN * domain)
{
  DB_COLLECTION *col;

  CHECK_CONNECT_NULL ();

  if (domain != NULL)
    {
      col = set_create_with_domain (domain, size);
    }
  else
    {
      col = set_create (type, size);
    }

  return col;
}

/*
 * db_col_copy() - This function returns a copy of the given collection. The
 *    new collection has the identical domain and element values as the source
 *    collection.
 * return : new collection (NULL if error)
 * col(in): collection to copy
 */
DB_COLLECTION *
db_col_copy (DB_COLLECTION * col)
{
  DB_COLLECTION *new_ = NULL;

  CHECK_CONNECT_NULL ();

  if (col != NULL)
    {
      new_ = set_copy (col);
    }

  return new_;
}

/*
 * db_col_free() - This function frees storage for the collection. This
 *    function must be called for any collection created by the
 *    db_col_create() function as well as for any collection handle
 *    returned by any other API function.
 * return : error status
 * col(in): collection to free
 */
int
db_col_free (DB_COLLECTION * col)
{
  /* don't check connection here, we always allow things to be freed */

  if (col != NULL)
    {
      set_free (col);
    }

  return NO_ERROR;
}

/*
 * db_col_filter() - This function examines each element in the collection for
 *   references to objects that have been deleted. If any such elements are
 *   found, they are removed. If the collection type is DB_TYPE_SEQUENCE, then
 *   the elements containing deleted object references will be changed to have
 *   a DB_TYPE_NULL value. DB_TYPE_MULTISET or DB_TYPE_SET, elements containing
 *   deleted object references are first converted to elements containing a
 *   DB_TYPE_NULL value. After all deleted object references have been
 *   converted to null values, all but one of the null values are then removed
 *   from the collection.
 * return : error status
 * col(in): collection to filter
 */
int
db_col_filter (DB_COLLECTION * col)
{
  int error;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  error = set_filter (col);

  return error;
}

/*
 * db_col_add() -  This is used to add new elements to a collection.
 *
 * return : error status
 * col(in): collection to extend
 * value(in): value to add
 *
 * note : db_col_add is normally used only with collections of type DB_TYPE_SET
 *   and DB_TYPE_MULTISET.  It can be used with collections of type
 *   DB_TYPE_SEQUENCE but the new elements will always be appended to the end
 *   of the sequence.  If you need more control over the positioning of
 *   elements in a sequence, you may use the db_col_put or db_col_insert
 *   functions.
 */
int
db_col_add (DB_COLLECTION * col, DB_VALUE * value)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  error = set_add_element (col, value);

  return error;
}

/*
 * db_col_drop() - This function is used to remove a value from a collection.
 * return : error code
 * col(in): collection
 * value(in): value to drop
 * all(in): non-zero to drop all occurrences of the value
 *
 */
int
db_col_drop (DB_COLLECTION * col, DB_VALUE * value, int all)
{
  int error;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  error = set_drop_element (col, value, false);

  return error;
}

/*
 * db_col_drop_element() - The element with the given index will be removed
 *    from the collection and all subsequent elements in the collection will
 *    be moved up. The sequence size will be reduced by one. If the sequence
 *    has no elements at the given index, an error is returned.
 * return : error code
 * col(in): collection
 * element_index(in): index of element to drop
 */
int
db_col_drop_element (DB_COLLECTION * col, int element_index)
{
  int error;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  /* kludge, not preventing SET or MULTISET operations, might want to define some behavior here, even thouth the
   * resulting set order after the drop is undefined. */
  error = set_drop_seq_element (col, element_index);

  return error;
}

/*
 * db_col_drop_nulls() - This function is used to remove all NULL db_values
 *    from a collection.
 * return : error code
 * col(in): collection
 */
int
db_col_drop_nulls (DB_COLLECTION * col)
{
  int error;
  DB_VALUE value;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  db_make_null (&value);

  error = set_drop_element (col, &value, true);

  return error;
}

/*
 * db_col_size() - This function is used to obtain the number of elements found
 *    within the collection.
 * return : number of elements in the collection
 * col(in): collection
 */
int
db_col_size (DB_COLLECTION * col)
{
  int size;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (col);

  size = set_size (col);

  return size;
}

/*
 * db_col_cardinality() - This function returns the number of elements in the
 *    collection that have non-NULL values.
 * return: the cardinality of the collection
 * col(in): collection
 *
 * note : This is different than db_col_size which returns the total
 *    number of elements in the collection, including those with NULL
 *    values. Use of db_col_cardinality is discouraged since it is
 *    almost always the case that the API programmer really should be
 *    using db_col_size.
 */
int
db_col_cardinality (DB_COLLECTION * col)
{
  int card;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (col);

  card = set_cardinality (col);

  return card;
}

/*
 * db_col_get() - This function is the primary function for retrieving values
 *    out of a collection. It can be used for collections of all types.
 * return : error status
 * col(in): collection
 * element_index(in): index of element to access
 * value(in): container in which to store value
 *
 * note : The insertion order of elements in a collection of type DB_TYPE_SET
 *    or DB_TYPE_MULTISET is undefined, but the collection is guaranteed to
 *    retain its current order as long as no modifications are made to the
 *    collection. This makes it possible to iterate over the elements of a
 *    collection using an index. Iterations over a collection are normally
 *    performed by first obtaining the size of the collection with the
 *    db_col_size() function and then entering a loop whose index begins at 0
 *    and ends at the value of db_col_size() minus one.
 */
int
db_col_get (DB_COLLECTION * col, int element_index, DB_VALUE * value)
{
  int error;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (col, value);

  error = set_get_element (col, element_index, value);

  return error;
}

/*
 * db_col_put() - This function assigns the given value to the element of the
 *    collection with the given index. The previous contents of the element are
 *    freed. This function is normally used with collections of type
 *    DB_TYPE_SEQUENCE. This function is normally used to populate
 *    DB_TYPE_SEQUENCE collections since it allows more control over the
 *    positioning of the values within the collection. If the given collection
 *    is of type DB_TYPE_SET or DB_TYPE_MULTISET, the index argument is ignored
 *    and the function behaves identically to the db_col_add() function.
 * return : error code
 * col(in): collection
 * element_index(in): index of element to modify
 * value(in): value to assign
 */
int
db_col_put (DB_COLLECTION * col, int element_index, DB_VALUE * value)
{
  int error = NO_ERROR;
  DB_TYPE coltype;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  coltype = set_get_type (col);
  if (coltype == DB_TYPE_SEQUENCE)
    {
      error = set_put_element (col, element_index, value);
    }
  else
    {
      error = set_add_element (col, value);
    }

  return error;
}

/*
 * db_col_insert() - This function inserts a new element into the sequence at a
 *    position immediately before the indexed element. This function is
 *    normally used with collections of type DB_TYPE_SEQUENCE. If the index is
 *    0, the new element is added to the beginning of the sequence. All
 *    elements in the sequence are moved down to make room for the new element.
 *    The sequence increases in size by one element.
 * return : error status
 * col(in): collection
 * element_index(in): index of new element
 * value(in): value to insert
 */
int
db_col_insert (DB_COLLECTION * col, int element_index, DB_VALUE * value)
{
  int error = NO_ERROR;
  DB_TYPE coltype;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* Check if modifications are disabled only if the set is owned */
  if (col->owner != NULL)
    {
      CHECK_MODIFICATION_ERROR ();
    }

  coltype = set_get_type (col);
  if (coltype == DB_TYPE_SEQUENCE)
    {
      error = set_insert_element (col, element_index, value);
    }
  else
    {
      error = set_add_element (col, value);
    }

  return error;
}

/*
 * db_col_ismember() - This function can be used to determine if a particular
 *    value is found within a collection.
 * return :
 *      0 = the value was not found within the collection
 *     >0 = the value was found within the collection
 *     <0 = an error was detected.
 * col(in): collection
 * value(in): value to search for
 */
int
db_col_ismember (DB_COLLECTION * col, DB_VALUE * value)
{
  int member;

  CHECK_CONNECT_MINUSONE ();
  CHECK_1ARG_MINUSONE (col);

  member = set_ismember (col, value) ? 1 : 0;

  return member;
}

/*
 * db_col_find() - This function can be used to scan a collection looking for
 *    elements that whose values are equal to the given value. It is normally
 *    used with collections of DB_TYPE_SEQUENCE but can be used with other
 *    collection types.
 * return : index of the desired element
 * col(in): collection
 * value(in): value to search for
 * starting_index(in): starting index
 * found_index(out): returned index of element
 *
 * note : since this function uses element indexes, it can only be used
 *   reliably with collections of type DB_TYPE_SET and DB_TYPE_MULTISET when
 *   there are no modifications being made to the collection. The index
 *   returned by this function has the following properties. If the value is
 *   greater than or equal to 0, the number represents a valid element index.
 *   This index can be used with the db_col_get() or db_col_put()functions for
 *   example to directly access the element. If the index returned is -1, it
 *   indicates that the value as not found in the collection. When the element
 *   is not found, the error ER_SEQ_ELEMENT_NOT_FOUND is also set and returned.
 *   The index will also be -1 if any other error is detected.
 *   The starting_index parameter can be used to search for more than one
 *   occurrence of the value. To search for the first occurrence of the value,
 *   the starting_index should be 0. If an occurrence is found, to find the
 *   next occurrence, set the starting_index to the index value returned by
 *   the last db_col_find() call plus one.
 */
int
db_col_find (DB_COLLECTION * col, DB_VALUE * value, int starting_index, int *found_index)
{
  int error = NO_ERROR;
  int psn;

  CHECK_CONNECT_ERROR ();
  CHECK_2ARGS_ERROR (col, value);

  psn = set_find_seq_element (col, value, starting_index);

  if (psn < 0)
    {
      if (found_index != NULL)
	{
	  *found_index = -1;
	}
      error = (int) psn;
    }
  else
    {
      if (found_index != NULL)
	{
	  *found_index = psn;
	}
    }

  return error;
}

/*
 * db_col_type() - This function returns the base type for this collection.
 * return : one of DB_TYPE_SET, DB_TYPE_MULTISET, or DB_TYPE_SEQUENCE.
 * col(in): collection
 */
DB_TYPE
db_col_type (DB_COLLECTION * col)
{
  DB_TYPE type = DB_TYPE_NULL;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR_WITH_TYPE (col, DB_TYPE);

  type = set_get_type (col);

  return type;
}

/*
 * db_col_domain() - This function returns the fully specified domain
 *    for the collection.
 * return : domain of the collection
 * col(in): collection
 */
DB_DOMAIN *
db_col_domain (DB_COLLECTION * col)
{
  DB_DOMAIN *domain = NULL;

  CHECK_CONNECT_NULL ();
  CHECK_1ARG_NULL (col);

  domain = set_get_domain (col);

  return domain;
}

/*
 * db_col_fprint() - This is a debugging function that will emit a printed
 *    representation of the collection to the file.
 *    It is not intended to be used to emit a "readable" representation of
 *    the collection, in particular, it may truncate the output if the
 *    collection is very long.
 * return : error code
 * fp(in): file handle
 * col(in): collection
 */
int
db_col_fprint (FILE * fp, DB_COLLECTION * col)
{
  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  /* hack, so we don't have to know how the compiler does this */
  if (fp == NULL)
    {
      fp = stdout;
    }

  set_fprint (fp, col);

  return NO_ERROR;
}

/*
 * db_col_print() - Please refer to the db_col_fprintf() function
 * return: error code
 * col(in): collection
 */
int
db_col_print (DB_COLLECTION * col)
{
  return db_col_fprint (stdout, col);
}

/*
 * db_col_optimize() - This function makes the set is in its optimal order
 *    for performance(sort overhead) reason.
 * return : error status
 * col(in): collection
 */
int
db_col_optimize (DB_COLLECTION * col)
{
  int error = NO_ERROR;

  CHECK_CONNECT_ERROR ();
  CHECK_1ARG_ERROR (col);

  error = set_optimize (col);

  return error;
}
