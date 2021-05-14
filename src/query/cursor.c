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
 * esql_cursor.c - cursor manager
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "error_manager.h"
#include "storage_common.h"
#include "memory_alloc.h"
#include "object_primitive.h"
#include "object_representation.h"
#include "db.h"
#include "locator_cl.h"
#include "server_interface.h"
#include "work_space.h"
#include "set_object.h"
#include "cursor.h"
#include "parser_support.h"
#include "virtual_object.h"
#include "network_interface_cl.h"
#include "dbtype.h"

#define CURSOR_BUFFER_SIZE              DB_PAGESIZE
#define CURSOR_BUFFER_AREA_SIZE         IO_MAX_PAGE_SIZE

enum
{
  FIRST_TPL = -1,
  LAST_TPL = -2
};

static void cursor_initialize_current_tuple_value_position (CURSOR_ID * cursor_id_p);
static bool cursor_has_set_vobjs (DB_SET * set);
static int cursor_fixup_set_vobjs (DB_VALUE * value);
static int cursor_fixup_vobjs (DB_VALUE * val);
static int cursor_get_tuple_value_to_dbvalue (OR_BUF * buf, TP_DOMAIN * dom, QFILE_TUPLE_VALUE_FLAG val_flag,
					      DB_VALUE * db_value, bool copy);
static int cursor_get_tuple_value_from_list (CURSOR_ID * c_id, int index, DB_VALUE * value, char *tuple);
static int cursor_get_first_tuple_value (char *tuple, QFILE_TUPLE_VALUE_TYPE_LIST * type_list, DB_VALUE * value,
					 bool copy);
static char *cursor_peek_tuple (CURSOR_ID * cursor_id);
static int cursor_get_list_file_page (CURSOR_ID * cursor_id, VPID * vpid);
static OID *cursor_get_oid_from_vobj (OID * current_oid_p, int length);
static OID *cursor_get_oid_from_tuple (char *tuple_p, DB_TYPE type);
static int cursor_allocate_tuple_area (CURSOR_ID * cursor_id_p, int tuple_length);
static int cursor_construct_tuple_from_overflow_pages (CURSOR_ID * cursor_id_p, VPID * vpid_p);
static bool cursor_has_first_hidden_oid (CURSOR_ID * cursor_id_p);
static int cursor_fetch_oids (CURSOR_ID * cursor_id_p, int oid_index, DB_FETCH_MODE instant_fetch_mode,
			      DB_FETCH_MODE class_fetch_mode);
static int cursor_prefetch_first_hidden_oid (CURSOR_ID * cursor_id_p);
static int cursor_prefetch_column_oids (CURSOR_ID * cursor_id_p);
static int cursor_point_current_tuple (CURSOR_ID * cursor_id_p, int position, int offset);
static int cursor_buffer_last_page (CURSOR_ID * cursor_id_p, VPID * vpid_p);
static void cursor_allocate_oid_buffer (CURSOR_ID * cursor_id_p);

/*
 * List File routines
 */

static void
cursor_initialize_current_tuple_value_position (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return;
    }

  cursor_id_p->current_tuple_value_index = -1;
  cursor_id_p->current_tuple_value_p = NULL;
}

/*
 * cursor_copy_list_id () - Copy source list identifier into destination
 *                    list identifier
 *   return: true on ok, false otherwise
 *   dest_list_id(out): Destination list identifier
 *   src_list_id(in): Source list identifier
 */
int
cursor_copy_list_id (QFILE_LIST_ID * dest_list_id_p, const QFILE_LIST_ID * src_list_id_p)
{
  size_t size;
  QFILE_TUPLE_VALUE_TYPE_LIST *dest_type_list_p;
  const QFILE_TUPLE_VALUE_TYPE_LIST *src_type_list_p;

  memcpy (dest_list_id_p, src_list_id_p, DB_SIZEOF (QFILE_LIST_ID));

  src_type_list_p = &(src_list_id_p->type_list);
  dest_type_list_p = &(dest_list_id_p->type_list);

  dest_list_id_p->type_list.domp = NULL;
  if (src_list_id_p->type_list.type_cnt)
    {
      size = src_type_list_p->type_cnt * sizeof (TP_DOMAIN *);
      dest_type_list_p->domp = (TP_DOMAIN **) malloc (size);

      if (dest_type_list_p->domp == NULL)
	{
	  return ER_FAILED;
	}
      memcpy (dest_type_list_p->domp, src_type_list_p->domp, size);
    }

  dest_list_id_p->tpl_descr.f_valp = NULL;
  dest_list_id_p->tpl_descr.clear_f_val_at_clone_decache = NULL;
  dest_list_id_p->sort_list = NULL;	/* never use sort_list in crs_ level */

  if (src_list_id_p->last_pgptr)
    {
      dest_list_id_p->last_pgptr = (PAGE_PTR) malloc (CURSOR_BUFFER_SIZE);
      if (dest_list_id_p->last_pgptr == NULL)
	{
	  return ER_FAILED;
	}

      memcpy (dest_list_id_p->last_pgptr, src_list_id_p->last_pgptr, CURSOR_BUFFER_SIZE);
    }

  return NO_ERROR;
}

/*
 * cursor_has_set_vobjs () -
 *   return: nonzero iff set has some vobjs, zero otherwise
 *   seq(in): set/sequence db_value
 */
static bool
cursor_has_set_vobjs (DB_SET * set)
{
  int i, size;
  DB_VALUE element;

  size = db_set_size (set);

  for (i = 0; i < size; i++)
    {
      if (db_set_get (set, i, &element) != NO_ERROR)
	{
	  return false;
	}

      if (DB_VALUE_TYPE (&element) == DB_TYPE_VOBJ)
	{
	  pr_clear_value (&element);
	  return true;
	}

      pr_clear_value (&element);
    }

  return false;
}

/*
 * cursor_fixup_set_vobjs() - if val is a set/seq of vobjs then
 * 			    turn it into a set/seq of vmops
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   val(in/out): a db_value
 */
static int
cursor_fixup_set_vobjs (DB_VALUE * value_p)
{
  DB_TYPE type;
  int rc, i, size;
  DB_VALUE element;
  DB_SET *set, *new_set;

  type = DB_VALUE_TYPE (value_p);
  if (!pr_is_set_type (type))
    {
      return ER_FAILED;
    }

  set = db_get_set (value_p);
  size = db_set_size (set);

  if (cursor_has_set_vobjs (set) == false)
    {
      return set_convert_oids_to_objects (set);
    }

  switch (type)
    {
    case DB_TYPE_SET:
      new_set = db_set_create_basic (NULL, NULL);
      break;
    case DB_TYPE_MULTISET:
      new_set = db_set_create_multi (NULL, NULL);
      break;
    case DB_TYPE_SEQUENCE:
      new_set = db_seq_create (NULL, NULL, size);
      break;
    default:
      return ER_FAILED;
    }

  /* fixup element vobjs into vmops and add them to new */
  for (i = 0; i < size; i++)
    {
      if (db_set_get (set, i, &element) != NO_ERROR)
	{
	  db_set_free (new_set);
	  return ER_FAILED;
	}

      if (cursor_fixup_vobjs (&element) != NO_ERROR)
	{
	  db_set_free (new_set);
	  return ER_FAILED;
	}

      if (type == DB_TYPE_SEQUENCE)
	{
	  rc = db_seq_put (new_set, i, &element);
	}
      else
	{
	  rc = db_set_add (new_set, &element);
	}

      if (rc != NO_ERROR)
	{
	  db_set_free (new_set);
	  return ER_FAILED;
	}
    }

  pr_clear_value (value_p);

  switch (type)
    {
    case DB_TYPE_SET:
      db_make_set (value_p, new_set);
      break;
    case DB_TYPE_MULTISET:
      db_make_multiset (value_p, new_set);
      break;
    case DB_TYPE_SEQUENCE:
      db_make_sequence (value_p, new_set);
      break;
    default:
      db_set_free (new_set);
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * cursor_fixup_vobjs () -
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   value(in/out): a db_value
 * Note: if value is an OID then turn it into an OBJECT type value
 *       if value is a VOBJ then turn it into a vmop
 *       if value is a set/seq then do same fixups on its elements
 */
static int
cursor_fixup_vobjs (DB_VALUE * value_p)
{
  DB_OBJECT *obj;
  int rc;

  switch (DB_VALUE_DOMAIN_TYPE (value_p))
    {
    case DB_TYPE_OID:
      rc = vid_oid_to_object (value_p, &obj);
      db_make_object (value_p, obj);
      break;

    case DB_TYPE_VOBJ:
      if (DB_IS_NULL (value_p))
	{
	  db_value_clear (value_p);
	  db_value_domain_init (value_p, DB_TYPE_OBJECT, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
	  rc = NO_ERROR;
	}
      else
	{
	  rc = vid_vobj_to_object (value_p, &obj);
	  pr_clear_value (value_p);
	  db_make_object (value_p, obj);
	}
      break;

    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:
      /* fixup any set/seq of vobjs into a set/seq of vmops */
      rc = cursor_fixup_set_vobjs (value_p);
      value_p->need_clear = true;
      break;

    default:
      rc = NO_ERROR;
      break;
    }

  return rc;
}

/*
 * cursor_copy_vobj_to_dbvalue - The given tuple set value which is in disk
 *   representation form is copied to the db_value structure
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   buf(in)            : Pointer to set disk representation
 *   db_value(out)      : Set to the set value
 */
int
cursor_copy_vobj_to_dbvalue (struct or_buf *buffer_p, DB_VALUE * value_p)
{
  int rc;
  DB_VALUE vobj_dbval;
  DB_OBJECT *object_p;
  PR_TYPE *pr_type;

  pr_type = pr_type_from_id (DB_TYPE_VOBJ);
  if (pr_type == NULL)
    {
      return ER_FAILED;
    }

  if (db_value_domain_init (&vobj_dbval, pr_type->id, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (pr_type->data_readval (buffer_p, &vobj_dbval, NULL, -1, true, NULL, 0) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /* convert the vobj into a vmop */
  rc = vid_vobj_to_object (&vobj_dbval, &object_p);
  db_make_object (value_p, object_p);
  pr_clear_value (&vobj_dbval);

  return rc;
}

/*
 * cursor_get_tuple_value_to_dbvalue () - The given tuple value which is in disk
 *   representation form is copied/peeked to the db_value structure
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *    buf(in)          : Pointer to the tuple value
 *    dom(in)           : Domain for the tpl column
 *    val_flag(in)      : Flag to indicate if tuple value is bound
 *    db_value(out)     : Set to the tuple value
 *    copy(in)          : Indicator for copy/peek
 */
static int
cursor_get_tuple_value_to_dbvalue (OR_BUF * buffer_p, TP_DOMAIN * domain_p, QFILE_TUPLE_VALUE_FLAG value_flag,
				   DB_VALUE * value_p, bool is_copy)
{
  PR_TYPE *pr_type;
  DB_TYPE type;

  pr_type = domain_p->type;
  if (pr_type == NULL)
    {
      return ER_FAILED;
    }

  type = pr_type->id;
  if (value_flag == V_UNBOUND)
    {
      db_value_domain_init (value_p, type, domain_p->precision, domain_p->scale);
      return NO_ERROR;
    }

  /* VOBJs must be handled separately */
  if (type == DB_TYPE_VOBJ)
    {
      return cursor_copy_vobj_to_dbvalue (buffer_p, value_p);
    }

  /* for all other types, we can use the prim routines */
  if (pr_type->data_readval (buffer_p, value_p, domain_p, -1, is_copy, NULL, 0) != NO_ERROR)
    {
      return ER_FAILED;
    }

  /*
   * OIDs must be turned into objects.
   * VOBJs must be turned into vmops.
   */
  return cursor_fixup_vobjs (value_p);

}

/*
 * cursor_get_tuple_value_from_list () - The tuple value at the indicated position is
 *   extracted and mapped to given db_value
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   c_id(in)   : Cursor Identifier
 *   index(in)  : Tuple Value index
 *   value(out) : Set to the fetched tuple value
 *   tuple(in)  : List file tuple
 */
static int
cursor_get_tuple_value_from_list (CURSOR_ID * cursor_id_p, int index, DB_VALUE * value_p, char *tuple_p)
{
  QFILE_TUPLE_VALUE_TYPE_LIST *type_list_p;
  QFILE_TUPLE_VALUE_FLAG flag;
  OR_BUF buffer;
  int i;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  type_list_p = &cursor_id_p->list_id.type_list;

  assert (index >= 0 && index < type_list_p->type_cnt);

  or_init (&buffer, tuple_p, QFILE_GET_TUPLE_LENGTH (tuple_p));

  /* check for saved tplvalue position info */
  if (cursor_id_p->current_tuple_value_index >= 0 && cursor_id_p->current_tuple_value_index <= index
      && cursor_id_p->current_tuple_value_p != NULL)
    {
      i = cursor_id_p->current_tuple_value_index;
      tuple_p = cursor_id_p->current_tuple_value_p;
    }
  else
    {
      i = 0;
      tuple_p += QFILE_TUPLE_LENGTH_SIZE;
    }

  for (; i < index; i++)
    {
      tuple_p += (QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p));
    }

  /* save index-th tplvalue position info */
  cursor_id_p->current_tuple_value_index = i;
  cursor_id_p->current_tuple_value_p = tuple_p;

  flag = QFILE_GET_TUPLE_VALUE_FLAG (tuple_p);
  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
  buffer.ptr = tuple_p;

  return cursor_get_tuple_value_to_dbvalue (&buffer, type_list_p->domp[i], flag, value_p,
					    cursor_id_p->is_copy_tuple_value);
}

/*
 * cursor_get_first_tuple_value () - First tuple value is extracted and mapped to
 *                             given db_value
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   tuple(in): List file tuple
 *   type_list(in): Type List
 *   value(out): Set to the first tuple value
 *   copy(in): Indicator for copy/peek
 */
static int
cursor_get_first_tuple_value (char *tuple_p, QFILE_TUPLE_VALUE_TYPE_LIST * type_list_p, DB_VALUE * value_p,
			      bool is_copy)
{
  QFILE_TUPLE_VALUE_FLAG flag;
  OR_BUF buffer;

  or_init (&buffer, tuple_p, QFILE_GET_TUPLE_LENGTH (tuple_p));

  tuple_p = (char *) tuple_p + QFILE_TUPLE_LENGTH_SIZE;
  flag = QFILE_GET_TUPLE_VALUE_FLAG (tuple_p);
  tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE;
  buffer.ptr = tuple_p;

  return cursor_get_tuple_value_to_dbvalue (&buffer, type_list_p->domp[0], flag, value_p, is_copy);
}

/*
 * cursor_get_list_file_page () -
 *   return:
 *   cursor_id(in/out): Cursor identifier
 *   vpid(in):
 */
static int
cursor_get_list_file_page (CURSOR_ID * cursor_id_p, VPID * vpid_p)
{
  VPID in_vpid;
  int page_size;
  char *page_p;

  if (cursor_id_p == NULL || vpid_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  /* find page at buffer area */
  if (VPID_EQ (vpid_p, &cursor_id_p->current_vpid))
    {
      /*
       * current_vpid can indicate one of pages in buffer area,
       * so do not assign buffer as head of buffer area
       */
      ;
    }
  else
    {
      cursor_id_p->buffer = NULL;
      if (cursor_id_p->buffer_filled_size > 0)
	{
	  /* it received a page from server */
	  if (VPID_EQ (vpid_p, &cursor_id_p->header_vpid))
	    {
	      /* in case of header vpid in buffer area */
	      cursor_id_p->buffer = cursor_id_p->buffer_area;
	    }
	  else
	    {
	      page_p = cursor_id_p->buffer_area;
	      page_size = 0;

	      while (page_size < (cursor_id_p->buffer_filled_size - CURSOR_BUFFER_SIZE))
		{
		  if (QFILE_GET_OVERFLOW_PAGE_ID (page_p) == NULL_PAGEID)
		    {
		      QFILE_GET_NEXT_VPID (&in_vpid, page_p);
		    }
		  else
		    {
		      QFILE_GET_OVERFLOW_VPID (&in_vpid, page_p);
		    }

		  if (VPID_ISNULL (&in_vpid))
		    {
		      break;
		    }
		  else if (VPID_EQ (vpid_p, &in_vpid))
		    {
		      cursor_id_p->buffer = page_p + CURSOR_BUFFER_SIZE;
		      break;
		    }

		  page_p += CURSOR_BUFFER_SIZE;
		  page_size += CURSOR_BUFFER_SIZE;
		}
	    }
	}
    }

  /* if not found, get the page from server */
  if (cursor_id_p->buffer == NULL)
    {
      int ret_val;

      ret_val = qfile_get_list_file_page (cursor_id_p->query_id, vpid_p->volid, vpid_p->pageid,
					  cursor_id_p->buffer_area, &cursor_id_p->buffer_filled_size);
      if (ret_val != NO_ERROR)
	{
	  return ret_val;
	}

      cursor_id_p->buffer = cursor_id_p->buffer_area;
      QFILE_COPY_VPID (&cursor_id_p->header_vpid, vpid_p);
    }

  return NO_ERROR;
}

static OID *
cursor_get_oid_from_vobj (OID * current_oid_p, int length)
{
  char *vobject_p;
  OR_BUF buffer;
  DB_VALUE value;
  DB_OBJECT *object_p, *tmp_object_p;

  vobject_p = (char *) current_oid_p;
  current_oid_p = NULL;
  or_init (&buffer, vobject_p, length);
  db_make_null (&value);

  if (cursor_copy_vobj_to_dbvalue (&buffer, &value) == NO_ERROR)
    {
      tmp_object_p = db_get_object (&value);

      if (vid_is_updatable (tmp_object_p) == true)
	{
	  object_p = vid_base_instance (tmp_object_p);

	  if (object_p && !WS_ISVID (object_p))
	    {
	      current_oid_p = WS_OID (object_p);
	    }
	}
    }

  return current_oid_p;
}

static OID *
cursor_get_oid_from_tuple (char *tuple_p, DB_TYPE type)
{
  OID *current_oid_p;
  int length;

  length = QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p);
  current_oid_p = (OID *) ((char *) tuple_p + QFILE_TUPLE_VALUE_HEADER_SIZE);

  if (type == DB_TYPE_VOBJ)
    {
      return cursor_get_oid_from_vobj (current_oid_p, length);
    }

  return current_oid_p;
}

static int
cursor_allocate_tuple_area (CURSOR_ID * cursor_id_p, int tuple_length)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  if (cursor_id_p->tuple_record.size == 0)
    {
      cursor_id_p->tuple_record.tpl = (char *) malloc (tuple_length);
    }
  else
    {
      cursor_id_p->tuple_record.tpl = (char *) realloc (cursor_id_p->tuple_record.tpl, tuple_length);
    }

  if (cursor_id_p->tuple_record.tpl == NULL)
    {
      return ER_FAILED;
    }

  cursor_id_p->tuple_record.size = tuple_length;
  return NO_ERROR;
}

static int
cursor_construct_tuple_from_overflow_pages (CURSOR_ID * cursor_id_p, VPID * vpid_p)
{
  VPID overflow_vpid;
  char *buffer_p;
  char *tmp_tuple_p, *tuple_p;
  int tuple_length, offset, tuple_page_size;

  if (cursor_id_p == NULL || vpid_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  /* get tuple length and allocate space for the tuple */
  tmp_tuple_p = cursor_id_p->buffer + QFILE_PAGE_HEADER_SIZE;
  tuple_length = QFILE_GET_TUPLE_LENGTH (tmp_tuple_p);

  if (cursor_id_p->tuple_record.size < tuple_length)
    {
      if (cursor_allocate_tuple_area (cursor_id_p, tuple_length) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  tuple_p = cursor_id_p->tuple_record.tpl;
  offset = 0;

  do
    {
      buffer_p = cursor_id_p->buffer;

      QFILE_GET_OVERFLOW_VPID (&overflow_vpid, buffer_p);
      tuple_page_size = MIN (tuple_length - offset, QFILE_MAX_TUPLE_SIZE_IN_PAGE);
      memcpy (tuple_p, buffer_p + QFILE_PAGE_HEADER_SIZE, tuple_page_size);
      tuple_p += tuple_page_size;
      offset += tuple_page_size;

      if (overflow_vpid.pageid != NULL_PAGEID)
	{
	  if (cursor_get_list_file_page (cursor_id_p, &overflow_vpid) != NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  QFILE_COPY_VPID (&cursor_id_p->current_vpid, &overflow_vpid);
	}
    }
  while (overflow_vpid.pageid != NULL_PAGEID);

  /* reset buffer as a head page of overflow page */
  if (!VPID_EQ (vpid_p, &overflow_vpid) && cursor_get_list_file_page (cursor_id_p, vpid_p) != NO_ERROR)
    {
      return ER_FAILED;
    }

  cursor_id_p->current_tuple_p = cursor_id_p->tuple_record.tpl;

  return NO_ERROR;
}

static bool
cursor_has_first_hidden_oid (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return false;
    }

  return (cursor_id_p->is_oid_included && cursor_id_p->oid_ent_count > 0 && cursor_id_p->list_id.type_list.domp
	  && (TP_DOMAIN_TYPE (cursor_id_p->list_id.type_list.domp[0]) == DB_TYPE_OBJECT));
}

static int
cursor_fetch_oids (CURSOR_ID * cursor_id_p, int oid_index, DB_FETCH_MODE instant_fetch_mode,
		   DB_FETCH_MODE class_fetch_mode)
{
  int i;
  OID tmp_oid;
  MOBJ mobj;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  if (oid_index == 0)
    {
      /* nothing to fetch */
      return NO_ERROR;
    }

  /* form the MOP set from the existing oid_set */
  for (i = 0; i < oid_index; i++)
    {
      OR_GET_OID (&cursor_id_p->oid_set[i], &tmp_oid);
      cursor_id_p->mop_set[i] = ws_mop (&tmp_oid, (MOP) NULL);
    }


  if (oid_index == 1)
    {
      /* use mvcc fetch type to get the visible object again (accessible only using mvcc snapshot) */
      mobj = locator_fetch_object (cursor_id_p->mop_set[0], instant_fetch_mode, LC_FETCH_MVCC_VERSION);
    }
  else
    {
      mobj = locator_fetch_set (oid_index, cursor_id_p->mop_set, instant_fetch_mode, class_fetch_mode, false);
    }

  if (mobj == NULL && er_errid () != ER_HEAP_UNKNOWN_OBJECT)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
cursor_prefetch_first_hidden_oid (CURSOR_ID * cursor_id_p)
{
  char *tuple_p;
  OID *current_oid_p;
  QFILE_TUPLE current_tuple;
  int tupel_count, oid_index = 0, current_tuple_length, i;
  DB_TYPE type;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  /* set tuple count and point to the first tuple */
  tupel_count = QFILE_GET_TUPLE_COUNT (cursor_id_p->buffer);
  current_tuple = cursor_id_p->buffer + QFILE_PAGE_HEADER_SIZE;
  oid_index = 0;

  /*
   * search through the current buffer to store interesting OIDs
   * in the oid_set area, eliminating duplicates.
   */
  for (i = 0; i < tupel_count; i++)
    {
      current_tuple_length = QFILE_GET_TUPLE_LENGTH (current_tuple);

      /* fetch first OID */
      type = TP_DOMAIN_TYPE (cursor_id_p->list_id.type_list.domp[0]);
      tuple_p = (char *) current_tuple + QFILE_TUPLE_LENGTH_SIZE;

      if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) != V_BOUND)
	{
	  continue;
	}

      current_oid_p = cursor_get_oid_from_tuple (tuple_p, type);

      if (current_oid_p && oid_index < cursor_id_p->oid_ent_count)
	{
	  COPY_OID (&cursor_id_p->oid_set[oid_index], current_oid_p);
	  oid_index++;
	}

      /* move to next tuple */
      current_tuple = (char *) current_tuple + current_tuple_length;
    }

  return cursor_fetch_oids (cursor_id_p, oid_index, cursor_id_p->prefetch_lock_mode,
			    ((cursor_id_p->prefetch_lock_mode == DB_FETCH_WRITE)
			     ? DB_FETCH_QUERY_WRITE : DB_FETCH_QUERY_READ));
}

static int
cursor_prefetch_column_oids (CURSOR_ID * cursor_id_p)
{
  char *tuple_p;
  OID *current_oid_p;
  QFILE_TUPLE current_tuple;
  int tuple_count, oid_index = 0, current_tuple_length;
  int j, tuple_index, col_index, col_num;
  DB_TYPE type;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  /* set tuple count and point to the first tuple */
  tuple_count = QFILE_GET_TUPLE_COUNT (cursor_id_p->buffer);
  current_tuple = cursor_id_p->buffer + QFILE_PAGE_HEADER_SIZE;
  oid_index = 0;

  for (tuple_index = 0; tuple_index < tuple_count; tuple_index++)
    {
      current_tuple_length = QFILE_GET_TUPLE_LENGTH (current_tuple);

      for (col_index = 0; col_index < cursor_id_p->oid_col_no_cnt; col_index++)
	{
	  col_num = cursor_id_p->oid_col_no[col_index];
	  type = TP_DOMAIN_TYPE (cursor_id_p->list_id.type_list.domp[col_num]);

	  tuple_p = (char *) current_tuple + QFILE_TUPLE_LENGTH_SIZE;
	  for (j = col_num - 1; j >= 0; --j)
	    {
	      tuple_p += QFILE_TUPLE_VALUE_HEADER_SIZE + QFILE_GET_TUPLE_VALUE_LENGTH (tuple_p);
	    }

	  if (QFILE_GET_TUPLE_VALUE_FLAG (tuple_p) != V_BOUND)
	    {
	      continue;
	    }

	  if (type != DB_TYPE_OBJECT && type != DB_TYPE_VOBJ)
	    {
	      continue;
	    }

	  current_oid_p = cursor_get_oid_from_tuple (tuple_p, type);

	  if (current_oid_p && oid_index < cursor_id_p->oid_ent_count)
	    {
	      /* for little endian */
	      if (type == DB_TYPE_VOBJ)
		{
		  OR_PUT_OID (&cursor_id_p->oid_set[oid_index], current_oid_p);
		}
	      else
		{
		  COPY_OID (&cursor_id_p->oid_set[oid_index], current_oid_p);
		}

	      oid_index++;
	    }
	}

      current_tuple = (char *) current_tuple + current_tuple_length;
    }

  return cursor_fetch_oids (cursor_id_p, oid_index, DB_FETCH_READ, DB_FETCH_QUERY_READ);
}

static int
cursor_point_current_tuple (CURSOR_ID * cursor_id_p, int position, int offset)
{
  if (cursor_id_p == NULL || cursor_id_p->buffer == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  cursor_id_p->buffer_tuple_count = QFILE_GET_TUPLE_COUNT (cursor_id_p->buffer);
  cursor_id_p->current_tuple_length = QFILE_GET_TUPLE_LENGTH ((cursor_id_p->buffer + QFILE_PAGE_HEADER_SIZE));

  if (position == LAST_TPL)
    {
      cursor_id_p->current_tuple_no = cursor_id_p->buffer_tuple_count - 1;
      cursor_id_p->current_tuple_offset = QFILE_GET_LAST_TUPLE_OFFSET (cursor_id_p->buffer);
    }
  else if (position == FIRST_TPL)
    {
      cursor_id_p->current_tuple_no = 0;
      cursor_id_p->current_tuple_offset = QFILE_PAGE_HEADER_SIZE;
    }
  else if (position < cursor_id_p->buffer_tuple_count)
    {
      cursor_id_p->current_tuple_no = position;
      cursor_id_p->current_tuple_offset = offset;
    }
  else
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int
cursor_buffer_last_page (CURSOR_ID * cursor_id_p, VPID * vpid_p)
{
  if (cursor_id_p == NULL || vpid_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  if (cursor_id_p->list_id.last_pgptr && VPID_EQ (&(cursor_id_p->list_id.first_vpid), vpid_p))
    {
      cursor_id_p->buffer = cursor_id_p->list_id.last_pgptr;
    }
  else
    {
      if (cursor_get_list_file_page (cursor_id_p, vpid_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * cursor_fetch_page_having_tuple () - A request is made to the server side to
 *   bring the specified list file page and copy the page to the cursor buffer
 *   area
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   cursor_id(in): Cursor identifier
 *   vpid(in): List File Real Page Identifier
 *   position(in):
 *   offset(in):
 * Note: For performance reasons, this routine checks the cursor identifier
 *       and if the cursor LIST FILE has a hidden OID column (for update)
 *       or has preceding hidden OID columns, vector fetches those referred
 *       objects from the server.
 *
 *       It also positions the tuple pointer to the desired tuple position.
 *       If position = LAST_TPL, then the cursor is positioned to the LAST
 *       tuple on the page.  If position = FIRST_TPL, then the cursor is
 *       positioned to the FIRST tuple on the page.  Otherwise, position is
 *       the tuple position in the fetched page and offset is used as the
 *       byte offset to the tuple.  If positioning to the first or last tuple
 *       on the page, the offset is ignored.
 */
int
cursor_fetch_page_having_tuple (CURSOR_ID * cursor_id_p, VPID * vpid_p, int position, int offset)
{
  if (cursor_id_p == NULL || vpid_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  cursor_initialize_current_tuple_value_position (cursor_id_p);

  if (!VPID_EQ (&(cursor_id_p->current_vpid), vpid_p))
    {
      if (cursor_buffer_last_page (cursor_id_p, vpid_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (cursor_id_p->buffer == NULL)
    {
      return ER_FAILED;
    }

  if (cursor_point_current_tuple (cursor_id_p, position, offset) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (QFILE_GET_OVERFLOW_PAGE_ID (cursor_id_p->buffer) != NULL_PAGEID)
    {
      if (cursor_construct_tuple_from_overflow_pages (cursor_id_p, vpid_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }
  else
    {
      cursor_id_p->current_tuple_p = cursor_id_p->buffer + cursor_id_p->current_tuple_offset;
    }

  /* If there is only one tuple, don't prefetch objects because prefetching a small set of objects is slower than
   * fetching them individually. */
  if (cursor_id_p->buffer_tuple_count < 2)
    {
      return NO_ERROR;
    }

  /* vector fetched involved OIDs for performance reasons, if the fetched LIST FILE page contains a hidden OID column
   * or a set of hidden preceding OID columns. NOTE1: this process for oid-cols-included queries are disabled. NOTE2:
   * this process is done only for DB_TYPE_OBJECT colums not for any other colum types such as DB_TYPE_VOBJ. */
  if (cursor_has_first_hidden_oid (cursor_id_p))
    {
      return cursor_prefetch_first_hidden_oid (cursor_id_p);
    }
  else if (cursor_id_p->oid_col_no && cursor_id_p->oid_col_no_cnt)
    {
      return cursor_prefetch_column_oids (cursor_id_p);
    }

  return NO_ERROR;
}

#if defined(WINDOWS) || defined (CUBRID_DEBUG)
/*
 * cursor_print_list () - Dump the content of the list file to the standard output
 *   return:
 *   query_id(in):
 *   list_id(in): List File Identifier
 */
void
cursor_print_list (QUERY_ID query_id, QFILE_LIST_ID * list_id_p)
{
  CURSOR_ID cursor_id;
  DB_VALUE *value_list_p, *value_p;
  int count, i, status;

  if (list_id_p == NULL)
    {
      assert (0);
      return;
    }

  count = list_id_p->type_list.type_cnt;
  value_list_p = (DB_VALUE *) malloc (count * sizeof (DB_VALUE));
  if (value_list_p == NULL)
    {
      return;
    }

  fprintf (stdout, "\n=================   Q U E R Y   R E S U L T S   =================\n\n");

  if (cursor_open (&cursor_id, list_id_p, false, false) == false)
    {
      free_and_init (value_list_p);
      return;
    }

  cursor_id.query_id = query_id;

  while (true)
    {
      status = cursor_next_tuple (&cursor_id);
      if (status != DB_CURSOR_SUCCESS)
	{
	  break;
	}

      if (cursor_get_tuple_value_list (&cursor_id, count, value_list_p) != NO_ERROR)
	{
	  goto cleanup;
	}

      fprintf (stdout, "\n ");

      for (i = 0, value_p = value_list_p; i < count; i++, value_p++)
	{
	  fprintf (stdout, "  ");

	  if (TP_IS_SET_TYPE (DB_VALUE_TYPE (value_p)) || DB_VALUE_TYPE (value_p) == DB_TYPE_VOBJ)
	    {
	      db_set_print (db_get_set (value_p));
	    }
	  else
	    {
	      db_value_print (value_p);
	    }

	  db_value_clear (value_p);
	  fprintf (stdout, "  ");
	}
    }

  fprintf (stdout, "\n");

cleanup:

  cursor_close (&cursor_id);

  free_and_init (value_list_p);
  return;
}
#endif

/*
 * Cursor Management routines
 */

static void
cursor_allocate_oid_buffer (CURSOR_ID * cursor_id_p)
{
  size_t oids_size, mops_size;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return;
    }

  /*
   * NOTE: Currently assume a PAGESIZE. In fact, since we can
   * find average tuple count per page from the LIST FILE
   * identifier we can make a good estimate of oid entry count.
   */
  cursor_id_p->oid_ent_count = CEIL_PTVDIV (DB_PAGESIZE, sizeof (OID)) - 1;

  oids_size = cursor_id_p->oid_ent_count * sizeof (OID);
  cursor_id_p->oid_set = (OID *) malloc (oids_size);

  if (cursor_id_p->oid_set == NULL)
    {
      /* Ignore the failure, this is an optimization */
      cursor_id_p->oid_ent_count = 0;
    }

  mops_size = cursor_id_p->oid_ent_count * sizeof (MOP);
  cursor_id_p->mop_set = (MOP *) malloc (mops_size);

  if (cursor_id_p->mop_set == NULL)
    {
      /* Ignore the failure, this is an optimization */
      free_and_init (cursor_id_p->oid_set);
      cursor_id_p->oid_ent_count = 0;
    }
}

/*
 * cursor_open () -
 *   return: true on all ok, false otherwise
 *   cursor_id(out): Cursor identifier
 *   list_id: List file identifier
 *   updatable: Flag which indicates if cursor is updatable
 *   is_oid_included: Flag which indicates if first column of the list file
 *                 contains hidden object identifiers
 *
 * Note: A cursor is opened to scan through the tuples of the given
 *       list file. The cursor identifier is initialized and memory
 *       buffer for the cursor identifier is allocated. If is_oid_included
 *       flag is set to true, this indicates that the first column
 *       of list file tuples contains the object identifier to be used
 *       for cursor update/delete operations.
 */
bool
cursor_open (CURSOR_ID * cursor_id_p, QFILE_LIST_ID * list_id_p, bool updatable, bool is_oid_included)
{
  static QFILE_LIST_ID empty_list_id;	/* TODO: remove static empty_list_id */

  if (cursor_id_p == NULL)
    {
      assert (0);
      return false;
    }

  QFILE_CLEAR_LIST_ID (&empty_list_id);

  cursor_id_p->is_updatable = updatable;
  cursor_id_p->is_oid_included = is_oid_included;
  cursor_id_p->oid_ent_count = 0;
  cursor_id_p->oid_set = NULL;
  cursor_id_p->mop_set = NULL;
  cursor_id_p->position = C_BEFORE;
  cursor_id_p->tuple_no = -1;
  VPID_SET_NULL (&cursor_id_p->current_vpid);
  VPID_SET_NULL (&cursor_id_p->next_vpid);
  VPID_SET_NULL (&cursor_id_p->header_vpid);
  cursor_id_p->tuple_record.size = 0;
  cursor_id_p->tuple_record.tpl = NULL;
  cursor_id_p->on_overflow = false;
  cursor_id_p->buffer_tuple_count = 0;
  cursor_id_p->current_tuple_no = -1;
  cursor_id_p->current_tuple_offset = -1;
  cursor_id_p->current_tuple_p = NULL;
  cursor_id_p->current_tuple_length = -1;
  cursor_id_p->oid_col_no = NULL;
  cursor_id_p->oid_col_no_cnt = 0;
  cursor_id_p->buffer = NULL;
  cursor_id_p->buffer_area = NULL;
  cursor_id_p->buffer_filled_size = 0;
  cursor_id_p->list_id = empty_list_id;
  cursor_id_p->prefetch_lock_mode = DB_FETCH_READ;
  cursor_id_p->is_copy_tuple_value = true;	/* copy */
  cursor_initialize_current_tuple_value_position (cursor_id_p);

  if (cursor_copy_list_id (&cursor_id_p->list_id, list_id_p) != NO_ERROR)
    {
      return false;
    }

  cursor_id_p->query_id = list_id_p->query_id;

  if (cursor_id_p->list_id.type_list.type_cnt)
    {
      cursor_id_p->buffer_area = (char *) malloc (CURSOR_BUFFER_AREA_SIZE);
      cursor_id_p->buffer = cursor_id_p->buffer_area;

      if (cursor_id_p->buffer == NULL)
	{
	  return false;
	}

      if (is_oid_included)
	{
	  cursor_allocate_oid_buffer (cursor_id_p);
	}
    }

  return true;
}

/*
 * cursor_set_prefetch_lock_mode () - Record the lock mode for prefetched objects.
 *   return: It returns the previous lock mode.
 *   cursor_id(in/out): Cursor identifier
 *   mode(in):
 */
DB_FETCH_MODE
cursor_set_prefetch_lock_mode (CURSOR_ID * cursor_id_p, DB_FETCH_MODE mode)
{
  DB_FETCH_MODE old;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return mode;
    }

  old = cursor_id_p->prefetch_lock_mode;

  cursor_id_p->prefetch_lock_mode = mode;

  return old;
}

/*
 * cursor_set_copy_tuple_value () - Record the indicator for copy/peek tplvalue.
 *   return: It returns the previous indicator.
 *   cursor_id(in/out): Cursor identifier
 *   copy(in):
 */
bool
cursor_set_copy_tuple_value (CURSOR_ID * cursor_id_p, bool is_copy)
{
  bool old;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return false;
    }

  old = cursor_id_p->is_copy_tuple_value;

  cursor_id_p->is_copy_tuple_value = is_copy;

  return old;
}

/*
 * cursor_set_oid_columns () -
 *   return:
 *   cursor_id(in/out): Cursor identifier
 *   oid_col_no(in): Array of int
 *   oid_col_no_cnt(in): Size of oid_col_no
 *
 * returns/side-effects: int (true on success, false on failure)
 *
 * description: The caller indicates which columns of the list file
 *              contain OID's which are to be pre-fetched when a list file
 *              page is fetched.
 */
int
cursor_set_oid_columns (CURSOR_ID * cursor_id_p, int *oid_col_no_p, int oid_col_no_cnt)
{
  if (cursor_id_p == NULL || cursor_id_p->is_oid_included || cursor_id_p->is_updatable)
    {
      return ER_FAILED;
    }

  cursor_id_p->oid_col_no = oid_col_no_p;
  cursor_id_p->oid_col_no_cnt = oid_col_no_cnt;

  cursor_allocate_oid_buffer (cursor_id_p);
  return NO_ERROR;
}

/*
 * cursor_free () - Free the area allocated for the cursor identifier.
 *   return:
 *   cursor_id(in/out): Cursor Identifier
 */
void
cursor_free (CURSOR_ID * cursor_id_p)
{
  int i;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return;
    }

  cursor_free_list_id (&(cursor_id_p->list_id));

  if (cursor_id_p->buffer_area != NULL)
    {
      free_and_init (cursor_id_p->buffer_area);
      cursor_id_p->buffer_filled_size = 0;
      cursor_id_p->buffer = NULL;
    }

  free_and_init (cursor_id_p->tuple_record.tpl);
  free_and_init (cursor_id_p->oid_set);

  if (cursor_id_p->mop_set != NULL)
    {
      for (i = 0; i < cursor_id_p->oid_ent_count; i++)
	{
	  cursor_id_p->mop_set[i] = NULL;
	}
      free_and_init (cursor_id_p->mop_set);
    }
}

/*
 * cursor_close () - Free the area allocated for the cursor identifier and
 *                       invalidate the cursor identifier.
 *   return:
 *   cursor_id(in/out): Cursor Identifier
 */
void
cursor_close (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return;
    }

  /* free the cursor allocated area */
  cursor_free (cursor_id_p);

  /* invalidate the cursor_id */
  cursor_id_p->position = C_BEFORE;
  cursor_id_p->tuple_no = -1;
  cursor_id_p->is_updatable = false;
  cursor_id_p->oid_ent_count = 0;
  cursor_id_p->current_vpid.pageid = NULL_PAGEID;
  cursor_id_p->next_vpid.pageid = NULL_PAGEID;
  cursor_id_p->header_vpid.pageid = NULL_PAGEID;
  cursor_id_p->buffer_tuple_count = 0;
  cursor_id_p->current_tuple_no = -1;
  cursor_id_p->current_tuple_offset = -1;
  cursor_id_p->current_tuple_p = NULL;
  cursor_id_p->current_tuple_length = -1;
  cursor_id_p->oid_col_no = NULL;
  cursor_id_p->oid_col_no_cnt = 0;
  cursor_id_p->query_id = NULL_QUERY_ID;
  cursor_initialize_current_tuple_value_position (cursor_id_p);
}

/*
 * crs_peek_tuple () - peek the current cursor tuple
 *   return: NULL on error
 *   cursor_id(in): Cursor Identifier
 * Note: A pointer to the beginning of the current cursor tuple is
 *       returned. The pointer directly points to inside the cursor memory
 *       buffer.
 */
static char *
cursor_peek_tuple (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return NULL;
    }

  if (cursor_id_p->position != C_ON)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_CRSPOS, 0);
      return NULL;
    }

  /* tuple is contained in the cursor buffer */
  return cursor_id_p->current_tuple_p;
}

/*
 * cursor_get_current_oid () -
 *   return: DB_CURSOR_SUCCESS, DB_CURSOR_END, error_code
 *   cursor_id(in): Cursor Identifier
 *   db_value(out): Set to the object identifier
 * Note: The object identifier stored in the first column of the
 *       current cursor tuple is extracted and stored in the db_value
 *       parameter. If cursor list file does not have an object
 *       identifier column, an error code is set.
 */
int
cursor_get_current_oid (CURSOR_ID * cursor_id_p, DB_VALUE * value_p)
{
  char *tuple_p;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  assert (cursor_id_p->is_oid_included == true);

  tuple_p = cursor_peek_tuple (cursor_id_p);
  if (tuple_p == NULL)
    {
      return ER_FAILED;
    }

  return cursor_get_first_tuple_value (tuple_p, &cursor_id_p->list_id.type_list, value_p,
				       cursor_id_p->is_copy_tuple_value);
}

/*
 * cursor_next_tuple () -
 *   return: DB_CURSOR_SUCCESS, DB_CURSOR_END, error_code
 *   cursor_id(in/out): Cursor Identifier
 * Note: Makes the next tuple in the LIST FILE referred by the cursor
 *       identifier the current active tuple of the cursor and returns
 *       DB_CURSOR_SUCCESS.
 *
 * Note: if end_of_scan: DB_CURSOR_END, otherwise an error code is returned.
 */
int
cursor_next_tuple (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL || cursor_id_p->query_id == NULL_QUERY_ID)
    {
      assert (0);
      return DB_CURSOR_ERROR;
    }

  cursor_initialize_current_tuple_value_position (cursor_id_p);

  if (cursor_id_p->position == C_BEFORE)
    {
      if (VPID_ISNULL (&(cursor_id_p->list_id.first_vpid)))
	{
	  return DB_CURSOR_END;
	}

      if (cursor_fetch_page_having_tuple (cursor_id_p, &cursor_id_p->list_id.first_vpid, FIRST_TPL, 0) != NO_ERROR)
	{
	  return DB_CURSOR_ERROR;
	}

      QFILE_COPY_VPID (&cursor_id_p->current_vpid, &cursor_id_p->list_id.first_vpid);
      /*
       * Setup the cursor so that we can proceed through the next "if"
       * statement w/o code duplication.
       */
      cursor_id_p->position = C_ON;
      cursor_id_p->tuple_no = -1;
      cursor_id_p->current_tuple_no = -1;
      cursor_id_p->current_tuple_length = 0;
    }

  if (cursor_id_p->position == C_ON)
    {
      VPID next_vpid;

      if (cursor_id_p->current_tuple_no < cursor_id_p->buffer_tuple_count - 1)
	{
	  cursor_id_p->tuple_no++;
	  cursor_id_p->current_tuple_no++;
	  cursor_id_p->current_tuple_offset += cursor_id_p->current_tuple_length;
	  cursor_id_p->current_tuple_p += cursor_id_p->current_tuple_length;
	  cursor_id_p->current_tuple_length = QFILE_GET_TUPLE_LENGTH (cursor_id_p->current_tuple_p);
	}
      else if (QFILE_GET_NEXT_PAGE_ID (cursor_id_p->buffer) != NULL_PAGEID)
	{
	  QFILE_GET_NEXT_VPID (&next_vpid, cursor_id_p->buffer);
	  if (cursor_fetch_page_having_tuple (cursor_id_p, &next_vpid, FIRST_TPL, 0) != NO_ERROR)
	    {
	      return DB_CURSOR_ERROR;
	    }
	  QFILE_COPY_VPID (&cursor_id_p->current_vpid, &next_vpid);
	  cursor_id_p->tuple_no++;
	}
      else
	{
	  cursor_id_p->position = C_AFTER;
	  cursor_id_p->tuple_no = cursor_id_p->list_id.tuple_cnt;
	  return DB_CURSOR_END;
	}
    }
  else if (cursor_id_p->position == C_AFTER)
    {
      return DB_CURSOR_END;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return ER_QPROC_UNKNOWN_CRSPOS;
    }

  return DB_CURSOR_SUCCESS;
}

/*
 * cursor_prev_tuple () -
 *   return: DB_CURSOR_SUCCESS, DB_CURSOR_END, error_code
 *   cursor_id(in/out): Cursor Identifier
 * Note: Makes the previous tuple in the LIST FILE referred by cursor
 *       identifier the current active tuple of the cursor and returns
 *       DB_CURSOR_SUCCESS.
 *
 * Note: if end_of_scan: DB_CURSOR_END, otherwise an error code is returned.
 */
int
cursor_prev_tuple (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return DB_CURSOR_ERROR;
    }

  cursor_initialize_current_tuple_value_position (cursor_id_p);

  if (cursor_id_p->position == C_BEFORE)
    {
      return DB_CURSOR_END;
    }
  else if (cursor_id_p->position == C_ON)
    {
      VPID prev_vpid;

      if (cursor_id_p->current_tuple_no > 0)
	{
	  cursor_id_p->tuple_no--;
	  cursor_id_p->current_tuple_no--;
	  cursor_id_p->current_tuple_offset -= QFILE_GET_PREV_TUPLE_LENGTH (cursor_id_p->current_tuple_p);
	  cursor_id_p->current_tuple_p -= QFILE_GET_PREV_TUPLE_LENGTH (cursor_id_p->current_tuple_p);
	  cursor_id_p->current_tuple_length = QFILE_GET_TUPLE_LENGTH (cursor_id_p->current_tuple_p);
	}
      else if (QFILE_GET_PREV_PAGE_ID (cursor_id_p->buffer) != NULL_PAGEID)
	{
	  QFILE_GET_PREV_VPID (&prev_vpid, cursor_id_p->buffer);

	  if (cursor_fetch_page_having_tuple (cursor_id_p, &prev_vpid, LAST_TPL, 0) != NO_ERROR)
	    {
	      return DB_CURSOR_ERROR;
	    }

	  QFILE_COPY_VPID (&cursor_id_p->current_vpid, &prev_vpid);
	  cursor_id_p->tuple_no--;
	}
      else
	{
	  cursor_id_p->position = C_BEFORE;
	  cursor_id_p->tuple_no = -1;
	  return DB_CURSOR_END;
	}
    }
  else if (cursor_id_p->position == C_AFTER)
    {
      if (VPID_ISNULL (&(cursor_id_p->list_id.first_vpid)))
	{
	  return DB_CURSOR_END;
	}

      if (cursor_fetch_page_having_tuple (cursor_id_p, &cursor_id_p->list_id.last_vpid, LAST_TPL, 0) != NO_ERROR)
	{
	  return DB_CURSOR_ERROR;
	}

      QFILE_COPY_VPID (&cursor_id_p->current_vpid, &cursor_id_p->list_id.last_vpid);
      cursor_id_p->position = C_ON;
      cursor_id_p->tuple_no--;
    }
  else
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_UNKNOWN_CRSPOS, 0);
      return ER_QPROC_UNKNOWN_CRSPOS;
    }

  return DB_CURSOR_SUCCESS;
}

/*
 * cursor_first_tuple () -
 *
 * arguments:
 *   return: DB_CURSOR_SUCCESS, DB_CURSOR_END, error_code
 *   cursor_id(in/out): Cursor Identifier
 * Note: Makes the first tuple in the LIST FILE referred by the cursor
 *       identifier the current active tuple of the cursor and returns
 *       DB_CURSOR_SUCCESS. If there are no tuples in the list file,
 *       end_of_scan condition is reached.
 *
 * Note: if end_of_scan: DB_CURSOR_END, otherwise an error code is returned.
 */
int
cursor_first_tuple (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return DB_CURSOR_ERROR;
    }

  if (VPID_ISNULL (&(cursor_id_p->list_id.first_vpid)))
    {
      return DB_CURSOR_END;
    }

  if (cursor_fetch_page_having_tuple (cursor_id_p, &cursor_id_p->list_id.first_vpid, FIRST_TPL, 0) != NO_ERROR)
    {
      return DB_CURSOR_ERROR;
    }

  QFILE_COPY_VPID (&cursor_id_p->current_vpid, &cursor_id_p->list_id.first_vpid);
  cursor_id_p->position = C_ON;
  cursor_id_p->tuple_no = 0;

  if (cursor_id_p->buffer_tuple_count == 0)
    {
      cursor_id_p->position = C_AFTER;
      cursor_id_p->tuple_no = cursor_id_p->list_id.tuple_cnt;
      return DB_CURSOR_END;
    }

  return DB_CURSOR_SUCCESS;
}

/*
 * cursor_last_tuple () -
 *   return: DB_CURSOR_SUCCESS, DB_CURSOR_END, error_code
 *   cursor_id(in/out): Cursor Identifier
 * Note: Makes the last tuple in the LIST FILE referred by the cursor
 *       identifier the current active tuple of the cursor and returns
 *       DB_CURSOR_SUCCESS. If there are no tuples in the list file,
 *       end_of_scan condition is reached.
 *
 * Note: if end_of_scan: DB_CURSOR_END, otherwise an error code is returned.
 */
int
cursor_last_tuple (CURSOR_ID * cursor_id_p)
{
  if (cursor_id_p == NULL)
    {
      assert (0);
      return DB_CURSOR_ERROR;
    }

  if (VPID_ISNULL (&(cursor_id_p->list_id.first_vpid)))
    {
      return DB_CURSOR_END;
    }

  if (cursor_fetch_page_having_tuple (cursor_id_p, &cursor_id_p->list_id.last_vpid, LAST_TPL, 0) != NO_ERROR)
    {
      return DB_CURSOR_ERROR;
    }

  QFILE_COPY_VPID (&cursor_id_p->current_vpid, &cursor_id_p->list_id.last_vpid);
  cursor_id_p->position = C_ON;
  cursor_id_p->tuple_no = cursor_id_p->list_id.tuple_cnt - 1;

  return DB_CURSOR_SUCCESS;
}

/*
 * cursor_get_tuple_value () -
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   c_id(in): Cursor Identifier
 *   index(in):
 *   value(in/out):
 * Note: The data value of the current cursor tuple at the position
 *       pecified is fetched. If the position specified by index is
 *       not a valid position number, or if the cursor is not
 *       currently pointing to a tuple, then necessary error codes are
 *       returned.
 */
int
cursor_get_tuple_value (CURSOR_ID * cursor_id_p, int index, DB_VALUE * value_p)
{
  char *tuple_p;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  if (cursor_id_p->is_oid_included == true)
    {
      index++;
    }

  if (index < 0 || index >= cursor_id_p->list_id.type_list.type_cnt)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_TPLVAL_INDEX, 1, index);
      return ER_FAILED;
    }

  tuple_p = cursor_peek_tuple (cursor_id_p);
  if (tuple_p == NULL)
    {
      return ER_FAILED;
    }

  return cursor_get_tuple_value_from_list (cursor_id_p, index, value_p, tuple_p);
}

/*
 * cursor_get_tuple_value_list () -
 *   return: NO_ERROR on all ok, ER status( or ER_FAILED) otherwise
 *   cursor_id(in): Cursor Identifier
 *   size(in): Number of values in the value list
 *   value_list(in/out): Set to the values fetched from the current tuple
 * Note: The data values of the current cursor tuple are fetched
 *       and put the value_list in their originial order. The size
 *       parameter must be equal to the number of values in the tuple
 *       and the caller should allocate necessary space for the value
 *       list. If the cursor is not currently pointing to tuple, an
 *       error code is returned.
 */
int
cursor_get_tuple_value_list (CURSOR_ID * cursor_id_p, int size, DB_VALUE * value_list_p)
{
  DB_VALUE *value_p;
  int index;

  if (cursor_id_p == NULL)
    {
      assert (0);
      return ER_FAILED;
    }

  index = 0;
  value_p = value_list_p;

  while (index < size)
    {
      if (cursor_get_tuple_value (cursor_id_p, index, value_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      index++;
      value_p++;
    }

  return NO_ERROR;
}
