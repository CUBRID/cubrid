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
 * catalog_class.c - catalog class
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "storage_common.h"
#include "error_manager.h"
#include "system_catalog.h"
#include "heap_file.h"
#include "btree.h"
#include "oid.h"
#include "object_representation.h"
#include "object_representation_sr.h"
#include "transform.h"
#include "set_object.h"
#include "locator_sr.h"
#include "memory_hash.h"
#include "system_parameter.h"
#include "class_object.h"
#include "critical_section.h"
#include "xserver_interface.h"
#include "memory_alloc.h"
#include "language_support.h"
#include "numeric_opfunc.h"
#include "string_opfunc.h"
#include "dbtype.h"
#include "db_date.h"
#include "mvcc.h"

/* this must be the last header file included!!! */
#include "dbval.h"

#define IS_SUBSET(value)        (value).sub.count >= 0

#define EXCHANGE_OR_VALUE(a,b) \
  do { \
    OR_VALUE t; \
    t = a; \
    a = b; \
    b = t; \
  } while (0)

#define CATCLS_INDEX_NAME "i__db_class_class_name"
#define CATCLS_INDEX_KEY   10

#define CATCLS_OID_TABLE_SIZE   1024

typedef struct or_value OR_VALUE;
typedef struct catcls_entry CATCLS_ENTRY;
typedef struct catcls_property CATCLS_PROPERTY;
typedef int (*CREADER) (THREAD_ENTRY * thread_p, OR_BUF * buf,
			OR_VALUE * value_p);

struct or_value
{
  union or_id
  {
    OID classoid;
    ATTR_ID attrid;
  } id;
  DB_VALUE value;
  struct or_sub
  {
    struct or_value *value;
    int count;
  } sub;
};

struct catcls_entry
{
  OID class_oid;
  OID oid;
  CATCLS_ENTRY *next;
};

struct catcls_property
{
  const char *name;
  DB_SEQ *seq;
  int size;
  int is_unique;
  int is_reverse;
  int is_primary_key;
  int is_foreign_key;
};

/* TODO: add to ct_class.h */
bool catcls_Enable = false;

static BTID catcls_Btid;
static CATCLS_ENTRY *catcls_Free_entry_list = NULL;
static MHT_TABLE *catcls_Class_oid_to_oid_hash_table = NULL;

/* TODO: move to ct_class.h */
extern int catcls_compile_catalog_classes (THREAD_ENTRY * thread_p);
extern int catcls_insert_catalog_classes (THREAD_ENTRY * thread_p,
					  RECDES * record);
extern int catcls_delete_catalog_classes (THREAD_ENTRY * thread_p,
					  const char *name, OID * class_oid);
extern int catcls_update_catalog_classes (THREAD_ENTRY * thread_p,
					  const char *name, RECDES * record,
					  OID * class_oid_p,
					  bool force_in_place);
extern int catcls_finalize_class_oid_to_oid_hash_table (void);
extern int catcls_remove_entry (OID * class_oid);
extern int catcls_get_server_lang_charset (THREAD_ENTRY * thread_p,
					   int *charset_id_p, char *lang_buf,
					   const int lang_buf_size);
extern int catcls_get_db_collation (THREAD_ENTRY * thread_p,
				    LANG_COLL_COMPAT ** db_collations,
				    int *coll_cnt);
extern int catcls_get_apply_info_log_record_time (THREAD_ENTRY * thread_p,
						  time_t * log_record_time);
extern int catcls_find_and_set_serial_class_oid (THREAD_ENTRY * thread_p);
extern int catcls_find_and_set_partition_class_oid (THREAD_ENTRY * thread_p);

static int catcls_initialize_class_oid_to_oid_hash_table (int num_entry);
static int catcls_get_or_value_from_class (THREAD_ENTRY * thread_p,
					   OR_BUF * buf_p,
					   OR_VALUE * value_p);
static int catcls_get_or_value_from_attribute (THREAD_ENTRY * thread_p,
					       OR_BUF * buf_p,
					       OR_VALUE * value_p);
static int catcls_get_or_value_from_attrid (THREAD_ENTRY * thread_p,
					    OR_BUF * buf_p,
					    OR_VALUE * value_p);
static int catcls_get_or_value_from_domain (THREAD_ENTRY * thread_p,
					    OR_BUF * buf_p,
					    OR_VALUE * value_p);
static int catcls_get_or_value_from_method (THREAD_ENTRY * thread_p,
					    OR_BUF * buf_p,
					    OR_VALUE * value_p);
static int catcls_get_or_value_from_method_signiture (THREAD_ENTRY * thread_p,
						      OR_BUF * buf_p,
						      OR_VALUE * value_p);
static int catcls_get_or_value_from_method_argument (THREAD_ENTRY * thread_p,
						     OR_BUF * buf_p,
						     OR_VALUE * value_p);
static int catcls_get_or_value_from_method_file (THREAD_ENTRY * thread_p,
						 OR_BUF * buf_p,
						 OR_VALUE * value_p);
static int catcls_get_or_value_from_resolution (THREAD_ENTRY * thread_p,
						OR_BUF * buf_p,
						OR_VALUE * value_p);
static int catcls_get_or_value_from_query_spec (THREAD_ENTRY * thread_p,
						OR_BUF * buf_p,
						OR_VALUE * value_p);

static int catcls_get_or_value_from_indexes (DB_SEQ * seq,
					     OR_VALUE * subset,
					     int is_unique,
					     int is_reverse,
					     int is_primary_key,
					     int is_foreign_key);
static int catcls_get_subset (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
			      int expected_size, OR_VALUE * value_p,
			      CREADER reader);
static int catcls_get_object_set (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				  int expected_size, OR_VALUE * value);
static int catcls_get_property_set (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				    int expected_size, OR_VALUE * value_p);
static int catcls_reorder_attributes_by_repr (THREAD_ENTRY * thread_p,
					      OR_VALUE * value_p);
static int catcls_expand_or_value_by_repr (OR_VALUE * value_p,
					   OID * class_oid, DISK_REPR * rep);
static void catcls_expand_or_value_by_subset (THREAD_ENTRY * thread_p,
					      OR_VALUE * value_p);

static int catcls_get_or_value_from_buffer (THREAD_ENTRY * thread_p,
					    OR_BUF * buf_p,
					    OR_VALUE * value_p,
					    DISK_REPR * rep);
static int catcls_put_or_value_into_buffer (OR_VALUE * value_p, int chn,
					    OR_BUF * buf_p, OID * class_oid,
					    DISK_REPR * rep);

static OR_VALUE *catcls_get_or_value_from_class_record (THREAD_ENTRY *
							thread_p,
							RECDES * record);
static OR_VALUE *catcls_get_or_value_from_record (THREAD_ENTRY * thread_p,
						  RECDES * record,
						  OID * class_oid);
static int catcls_put_or_value_into_record (THREAD_ENTRY * thread_p,
					    OR_VALUE * value_p, int chn,
					    RECDES * record, OID * class_oid);

static int catcls_insert_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
				 OID * root_oid);
static int catcls_delete_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p);
static int catcls_update_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
				 OR_VALUE * old_value, int *uflag);
static int catcls_mvcc_update_subset (THREAD_ENTRY * thread_p,
				      OR_VALUE * value_p,
				      OR_VALUE * old_value_p, OID * root_oid);
static int catcls_insert_instance (THREAD_ENTRY * thread_p,
				   OR_VALUE * value_p, OID * oid,
				   OID * root_oid, OID * class_oid,
				   HFID * hfid, HEAP_SCANCACHE * scan);
static int catcls_delete_instance (THREAD_ENTRY * thread_p, OID * oid,
				   OID * class_oid, HFID * hfid,
				   HEAP_SCANCACHE * scan);
static int catcls_update_instance (THREAD_ENTRY * thread_p,
				   OR_VALUE * value_p, OID * oid,
				   OID * class_oid, HFID * hfid,
				   HEAP_SCANCACHE * scan);
static int catcls_mvcc_update_instance (THREAD_ENTRY * thread_p,
					OR_VALUE * value_p, OID * oid_p,
					OID * new_oid,
					OID * root_oid_p, OID * class_oid_p,
					HFID * hfid_p,
					HEAP_SCANCACHE * scan_p);
static OID *catcls_find_oid (OID * class_oid);
static int catcls_put_entry (CATCLS_ENTRY * entry);
static char *catcls_unpack_allocator (int size);
static OR_VALUE *catcls_allocate_or_value (int size);
static void catcls_free_sub_value (OR_VALUE * values, int count);
static void catcls_free_or_value (OR_VALUE * value);
static int catcls_expand_or_value_by_def (OR_VALUE * value_p, CT_CLASS * def);
static int catcls_guess_record_length (OR_VALUE * value_p);
static int catcls_find_class_oid_by_class_name (THREAD_ENTRY * thread_p,
						const char *name,
						OID * class_oid);
static int catcls_find_btid_of_class_name (THREAD_ENTRY * thread_p,
					   BTID * btid);
static int catcls_find_oid_by_class_name (THREAD_ENTRY * thread_p,
					  const char *name, OID * oid);
static int catcls_convert_class_oid_to_oid (THREAD_ENTRY * thread_p,
					    DB_VALUE * oid_val);
static int catcls_convert_attr_id_to_name (THREAD_ENTRY * thread_p,
					   OR_BUF * orbuf_p,
					   OR_VALUE * value_p);
static void catcls_apply_component_type (OR_VALUE * value_p, int type);
static int catcls_resolution_space (int name_space);
static void catcls_apply_resolutions (OR_VALUE * value_p,
				      OR_VALUE * resolution_p);
static int catcls_is_mvcc_update_needed (THREAD_ENTRY * thread_p, OID * oid,
					 bool * need_mvcc_update);

/*
 * catcls_allocate_entry () -
 *   return:
 *   void(in):
 */
static CATCLS_ENTRY *
catcls_allocate_entry (void)
{
  CATCLS_ENTRY *entry_p;
  if (catcls_Free_entry_list != NULL)
    {
      entry_p = catcls_Free_entry_list;
      catcls_Free_entry_list = catcls_Free_entry_list->next;
    }
  else
    {
      entry_p = (CATCLS_ENTRY *) malloc (sizeof (CATCLS_ENTRY));
      if (entry_p == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY,
		  1, sizeof (CATCLS_ENTRY));
	  return NULL;
	}
    }

  entry_p->next = NULL;
  return entry_p;
}

/*
 * catcls_free_entry () -
 *   return:
 *   key(in):
 *   data(in):
 *   args(in):
 */
static int
catcls_free_entry (const void *key, void *data, void *args)
{
  CATCLS_ENTRY *entry_p = (CATCLS_ENTRY *) data;
  entry_p->next = catcls_Free_entry_list;
  catcls_Free_entry_list = entry_p;

  return NO_ERROR;
}

/*
 * catcls_initialize_class_oid_to_oid_hash_table () -
 *   return:
 *   num_entry(in):
 */
static int
catcls_initialize_class_oid_to_oid_hash_table (int num_entry)
{
  catcls_Class_oid_to_oid_hash_table =
    mht_create ("Class OID to OID", num_entry, oid_hash, oid_compare_equals);

  if (catcls_Class_oid_to_oid_hash_table == NULL)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * catcls_finalize_class_oid_to_oid_hash_table () -
 *   return:
 *   void(in):
 */
int
catcls_finalize_class_oid_to_oid_hash_table (void)
{
  CATCLS_ENTRY *entry_p, *next_p;

  if (catcls_Class_oid_to_oid_hash_table)
    {
      mht_map (catcls_Class_oid_to_oid_hash_table, catcls_free_entry, NULL);
      mht_destroy (catcls_Class_oid_to_oid_hash_table);
    }

  for (entry_p = catcls_Free_entry_list; entry_p; entry_p = next_p)
    {
      next_p = entry_p->next;
      free_and_init (entry_p);
    }
  catcls_Free_entry_list = NULL;

  catcls_Class_oid_to_oid_hash_table = NULL;
  return NO_ERROR;
}

/*
 * catcls_find_oid () -
 *   return:
 *   class_oid(in):
 */
static OID *
catcls_find_oid (OID * class_oid_p)
{
  CATCLS_ENTRY *entry_p;

  if (catcls_Class_oid_to_oid_hash_table)
    {
      entry_p =
	(CATCLS_ENTRY *) mht_get (catcls_Class_oid_to_oid_hash_table,
				  (void *) class_oid_p);
      if (entry_p != NULL)
	{
	  return &entry_p->oid;
	}
      else
	{
	  return NULL;
	}
    }

  return NULL;
}

/*
 * catcls_put_entry () -
 *   return:
 *   entry(in):
 */
static int
catcls_put_entry (CATCLS_ENTRY * entry_p)
{
  if (catcls_Class_oid_to_oid_hash_table)
    {
      if (mht_put
	  (catcls_Class_oid_to_oid_hash_table, &entry_p->class_oid,
	   entry_p) == NULL)
	{
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/*
 * catcls_remove_entry () -
 *   return:
 *   class_oid(in):
 */
int
catcls_remove_entry (OID * class_oid_p)
{
  if (catcls_Class_oid_to_oid_hash_table)
    {
      mht_rem (catcls_Class_oid_to_oid_hash_table, class_oid_p,
	       catcls_free_entry, NULL);
    }

  return NO_ERROR;
}

/*
 * catcls_unpack_allocator () -
 *   return:
 *   size(in):
 */
static char *
catcls_unpack_allocator (int size)
{
  return ((char *) malloc (size));
}

/*
 * catcls_allocate_or_value () -
 *   return:
 *   size(in):
 */
static OR_VALUE *
catcls_allocate_or_value (int size)
{
  OR_VALUE *value_p;
  int msize, i;

  msize = size * sizeof (OR_VALUE);
  value_p = (OR_VALUE *) malloc (msize);
  if (value_p == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
	      msize);
    }
  else
    {
      for (i = 0; i < size; i++)
	{
	  db_value_put_null (&value_p[i].value);
	  value_p[i].sub.value = NULL;
	  value_p[i].sub.count = -1;
	}
    }

  return (value_p);
}

/*
 * cr_free_sub_value () -
 *   return:
 *   values(in):
 *   count(in):
 */
static void
catcls_free_sub_value (OR_VALUE * values, int count)
{
  int i;

  if (values != NULL)
    {
      for (i = 0; i < count; i++)
	{
	  pr_clear_value (&values[i].value);
	  catcls_free_sub_value (values[i].sub.value, values[i].sub.count);
	}
      free_and_init (values);
    }
}

/*
 * catcls_free_or_value () -
 *   return:
 *   value(in):
 */
static void
catcls_free_or_value (OR_VALUE * value_p)
{
  if (value_p != NULL)
    {
      pr_clear_value (&value_p->value);
      catcls_free_sub_value (value_p->sub.value, value_p->sub.count);
      free_and_init (value_p);
    }
}

/*
 * catcls_expand_or_value_by_def () -
 *   return:
 *   value(in):
 *   def(in):
 */
static int
catcls_expand_or_value_by_def (OR_VALUE * value_p, CT_CLASS * def_p)
{
  OR_VALUE *attrs_p;
  int n_attrs;
  CT_ATTR *att_def_p;
  int i;
  int error;

  if (value_p != NULL)
    {
      /* index_of */
      COPY_OID (&value_p->id.classoid, &def_p->classoid);

      n_attrs = def_p->n_atts;
      attrs_p = catcls_allocate_or_value (n_attrs);
      if (attrs_p == NULL)
	{
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}

      value_p->sub.value = attrs_p;
      value_p->sub.count = n_attrs;

      att_def_p = def_p->atts;
      for (i = 0; i < n_attrs; i++)
	{
	  attrs_p[i].id.attrid = att_def_p[i].id;
	  error = db_value_domain_init (&attrs_p[i].value, att_def_p[i].type,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
	  if (error != NO_ERROR)
	    {
	      return error;
	    }
	}
    }

  return NO_ERROR;
}

/*
 * catcls_guess_record_length () -
 *   return:
 *   value(in):
 */
static int
catcls_guess_record_length (OR_VALUE * value_p)
{
  int length;
  DB_TYPE data_type;
  PR_TYPE *map_p;
  OR_VALUE *attrs_p;
  int n_attrs, i;

  attrs_p = value_p->sub.value;
  n_attrs = value_p->sub.count;

  length =
    ((mvcc_Enabled == true) ?
     OR_MVCC_MAX_HEADER_SIZE : OR_NON_MVCC_HEADER_SIZE) +
    OR_VAR_TABLE_SIZE (n_attrs) + OR_BOUND_BIT_BYTES (n_attrs);

  for (i = 0; i < n_attrs; i++)
    {
      data_type = DB_VALUE_DOMAIN_TYPE (&attrs_p[i].value);
      map_p = tp_Type_id_map[data_type];

      if (map_p->data_lengthval != NULL)
	{
	  length += (*(map_p->data_lengthval)) (&attrs_p[i].value, 1);
	}
      else if (map_p->disksize)
	{
	  length += map_p->disksize;
	}
      /* else : is null-type */
    }

  return (length);
}

/*
 * catcls_find_class_oid_by_class_name () -
 *   return:
 *   name(in):
 *   class_oid(in):
 */
static int
catcls_find_class_oid_by_class_name (THREAD_ENTRY * thread_p,
				     const char *name_p, OID * class_oid_p)
{
  LC_FIND_CLASSNAME status;

  status = xlocator_find_class_oid (thread_p, name_p, class_oid_p, NULL_LOCK);

  if (status == LC_CLASSNAME_ERROR)
    {
      er_set (ER_WARNING_SEVERITY, ARG_FILE_LINE,
	      ER_LC_UNKNOWN_CLASSNAME, 1, name_p);
      return ER_FAILED;
    }
  else if (status == LC_CLASSNAME_DELETED)
    {
      /* not found the class */
      OID_SET_NULL (class_oid_p);
    }

  return NO_ERROR;
}

/*
 * catcls_find_btid_of_class_name () -
 *   return:
 *   btid(in):
 */
static int
catcls_find_btid_of_class_name (THREAD_ENTRY * thread_p, BTID * btid_p)
{
  DISK_REPR *repr_p = NULL;
  DISK_ATTR *att_repr_p;
  REPR_ID repr_id;
  OID *index_class_p;
  ATTR_ID index_key;
  int error = NO_ERROR;

  index_class_p = &ct_Class.classoid;
  index_key = (ct_Class.atts)[CATCLS_INDEX_KEY].id;

  error =
    catalog_get_last_representation_id (thread_p, index_class_p, &repr_id);
  if (error != NO_ERROR)
    {
      goto error;
    }
  else
    {
      repr_p = catalog_get_representation (thread_p, index_class_p, repr_id);
      if (repr_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error;
	}
    }

  for (att_repr_p = repr_p->variable; att_repr_p->id != index_key;
       att_repr_p++)
    {
      ;
    }

  if (att_repr_p->bt_stats == NULL)
    {
      error = ER_SM_NO_INDEX;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, CATCLS_INDEX_NAME);
      goto error;

    }
  else
    {
      BTID_COPY (btid_p, &(att_repr_p->bt_stats->btid));
    }

  catalog_free_representation (repr_p);
  return NO_ERROR;

error:

  if (repr_p)
    {
      catalog_free_representation (repr_p);
    }

  return error;
}

/*
 * catcls_find_oid_by_class_name () - Get an instance oid in the ct_Class using the
 *                               index for classname
 *   return:
 *   name(in):
 *   oid(in):
 */
static int
catcls_find_oid_by_class_name (THREAD_ENTRY * thread_p, const char *name_p,
			       OID * oid_p)
{
  DB_VALUE key_val;
  int error = NO_ERROR;

  error = db_make_varchar (&key_val, DB_MAX_IDENTIFIER_LENGTH,
			   (char *) name_p, strlen (name_p),
			   LANG_SYS_CODESET, LANG_SYS_COLLATION);
  if (error != NO_ERROR)
    {
      return error;
    }

  error =
    xbtree_find_unique (thread_p, &catcls_Btid, S_SELECT, &key_val,
			&ct_Class.classoid, oid_p, false);
  if (error == BTREE_ERROR_OCCURRED)
    {
      pr_clear_value (&key_val);
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      return ((error == NO_ERROR) ? ER_FAILED : error);
    }
  else if (error == BTREE_KEY_NOTFOUND)
    {
      OID_SET_NULL (oid_p);
    }

  pr_clear_value (&key_val);
  return NO_ERROR;
}

/*
 * catcls_convert_class_oid_to_oid () -
 *   return:
 *   oid_val(in):
 */
static int
catcls_convert_class_oid_to_oid (THREAD_ENTRY * thread_p,
				 DB_VALUE * oid_val_p)
{
  char *name_p = NULL;
  OID oid_buf;
  OID *class_oid_p, *oid_p;
  CATCLS_ENTRY *entry_p;

  if (DB_IS_NULL (oid_val_p))
    {
      return NO_ERROR;
    }

  class_oid_p = DB_PULL_OID (oid_val_p);

  if (csect_enter_as_reader (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  oid_p = catcls_find_oid (class_oid_p);

  csect_exit (thread_p, CSECT_CT_OID_TABLE);

  if (oid_p == NULL)
    {
      oid_p = &oid_buf;
      name_p = heap_get_class_name (thread_p, class_oid_p);
      if (name_p == NULL)
	{
	  return NO_ERROR;
	}

      if (catcls_find_oid_by_class_name (thread_p, name_p, oid_p) != NO_ERROR)
	{
	  free_and_init (name_p);

	  assert (er_errid () != NO_ERROR);
	  return er_errid ();
	}

      if (!OID_ISNULL (oid_p) && (entry_p = catcls_allocate_entry ()) != NULL)
	{
	  COPY_OID (&entry_p->class_oid, class_oid_p);
	  COPY_OID (&entry_p->oid, oid_p);
	  if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) !=
	      NO_ERROR)
	    {
	      return ER_FAILED;
	    }
	  catcls_put_entry (entry_p);
	  csect_exit (thread_p, CSECT_CT_OID_TABLE);
	}
    }

  db_push_oid (oid_val_p, oid_p);

  if (name_p)
    {
      free_and_init (name_p);
    }

  return NO_ERROR;
}

/*
 * catcls_convert_attr_id_to_name () -
 *   return:
 *   obuf(in):
 *   value(in):
 */
static int
catcls_convert_attr_id_to_name (THREAD_ENTRY * thread_p, OR_BUF * orbuf_p,
				OR_VALUE * value_p)
{
  OR_BUF *buf_p, orep;
  OR_VALUE *indexes, *keys;
  OR_VALUE *index_atts, *key_atts;
  OR_VALUE *id_val_p = NULL, *id_atts;
  OR_VALUE *ids;
  OR_VARINFO *vars = NULL;
  int id;
  int size;
  int i, j, k;
  int error = NO_ERROR;

  buf_p = &orep;
  or_init (buf_p, orbuf_p->buffer, (int) (orbuf_p->endptr - orbuf_p->buffer));

  or_advance (buf_p, OR_NON_MVCC_HEADER_SIZE);

  size = tf_Metaclass_class.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);

      return error;
    }

  /* jump to the 'attributes' and extract its id/name.
   * there are no indexes for shared or class attributes,
   * so we need only id/name for 'attributes'.
   * the offsets are relative to end of the class record header
   */
  or_seek (buf_p,
	   vars[ORC_ATTRIBUTES_INDEX].offset + OR_NON_MVCC_HEADER_SIZE);

  id_val_p = catcls_allocate_or_value (1);
  if (id_val_p == NULL)
    {
      free_and_init (vars);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  error = catcls_get_subset (thread_p, buf_p,
			     vars[ORC_ATTRIBUTES_INDEX].length,
			     id_val_p, catcls_get_or_value_from_attrid);
  if (error != NO_ERROR)
    {
      free_and_init (vars);
      free_and_init (id_val_p);
      return error;
    }

  /* replace id with name for each key attribute */
  for (indexes = value_p->sub.value, i = 0; i < value_p->sub.count; i++)
    {
      index_atts = indexes[i].sub.value;

      for (keys = (index_atts[4]).sub.value,
	   j = 0; j < (index_atts[4]).sub.count; j++)
	{
	  key_atts = keys[j].sub.value;

	  if (!DB_IS_NULL (&key_atts[1].value))
	    {
	      id = DB_GET_INT (&key_atts[1].value);

	      for (ids = id_val_p->sub.value, k = 0; k < id_val_p->sub.count;
		   k++)
		{
		  id_atts = ids[k].sub.value;
		  if (!DB_IS_NULL (&id_atts[0].value)
		      && id == DB_GET_INT (&id_atts[0].value))
		    {
		      pr_clear_value (&key_atts[1].value);
		      pr_clone_value (&id_atts[1].value, &key_atts[1].value);
		    }
		}
	    }
	}
    }

  catcls_free_or_value (id_val_p);
  free_and_init (vars);

  return NO_ERROR;
}

/*
 * catcls_apply_component_type () -
 *   return:
 *   value(in):
 *   type(in):
 */
static void
catcls_apply_component_type (OR_VALUE * value_p, int type)
{
  OR_VALUE *subset_p, *attrs;
  int i;

  for (subset_p = value_p->sub.value, i = 0; i < value_p->sub.count; i++)
    {
      attrs = subset_p[i].sub.value;
      /* assume that the attribute values of xxx are ordered by
         { class_of, xxx_name, xxx_type, from_xxx_name, ... } */
      db_make_int (&attrs[2].value, type);
    }
}

/*
 * catcls_resolution_space () - modified of sm_resolution_space()
 *   return:
 *   name_space(in):
 *
 * TODO: need to integrate
 */
static int
catcls_resolution_space (int name_space)
{
  int res_space = 5;		/* ID_INSTANCE */

  /* TODO: is ID_CLASS_ATTRIBUTE corret?? */
  if (name_space == 1)		/* ID_SHARED_ATTRIBUTE */
    {
      res_space = 6;		/* ID_CLASS */
    }

  return res_space;
}

/*
 * catcls_apply_resolutions () -
 *   return:
 *   value(in):
 *   res(in):
 */
static void
catcls_apply_resolutions (OR_VALUE * value_p, OR_VALUE * resolution_p)
{
  OR_VALUE *subset_p, *resolution_subset_p;
  OR_VALUE *attrs, *res_attrs;
  int i, j;
  int attr_name_space;

  for (resolution_subset_p = resolution_p->sub.value, i = 0;
       i < resolution_p->sub.count; i++)
    {
      res_attrs = resolution_subset_p[i].sub.value;

      for (subset_p = value_p->sub.value, j = 0; j < value_p->sub.count; j++)
	{
	  attrs = subset_p[j].sub.value;

	  /* assume that the attribute values of xxx are ordered by
	     { class_of, xxx_name, xxx_type, from_xxx_name, ... } */

	  /* compare component name & name space */
	  if (tp_value_compare (&attrs[1].value, &res_attrs[1].value, 1, 0)
	      == DB_EQ)
	    {
	      attr_name_space =
		catcls_resolution_space (DB_GET_INT (&attrs[2].value));
	      if (attr_name_space == DB_GET_INT (&res_attrs[2].value))
		{
		  /* set the value as 'from_xxx_name' */
		  pr_clear_value (&attrs[3].value);
		  pr_clone_value (&res_attrs[3].value, &attrs[3].value);
		}
	    }
	}
    }
}

/*
 * catcls_get_or_value_from_class () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_class (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  OR_VALUE *resolution_p = NULL;
  OID class_oid;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Class);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_class.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* fixed */

  or_advance (buf_p, ORC_ATT_COUNT_OFFSET);

  /* attribute_count */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[1].value, NULL, -1, true, NULL,
				0);

  /* object_size */
  or_advance (buf_p, OR_INT_SIZE);

  /* shared_count */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[2].value, NULL, -1, true, NULL,
				0);

  /* method_count */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[3].value, NULL, -1, true, NULL,
				0);

  /* class_method_count */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[4].value, NULL, -1, true, NULL,
				0);

  /* class_att_count */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[5].value, NULL, -1, true, NULL,
				0);

  /* flags */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[6].value, NULL, -1, true, NULL,
				0);

  /* class_type */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[7].value, NULL, -1, true, NULL,
				0);

  /* owner */
  (*(tp_Object.data_readval)) (buf_p, &attrs[8].value, NULL, -1, true, NULL,
			       0);

  /* collation_id */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[9].value, NULL, -1, true, NULL,
				0);

  /* variable */

  /* name */
  attr_val_p = &attrs[10].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_NAME_INDEX].length, true, NULL, 0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  /* (class_of) */
  if (catcls_find_class_oid_by_class_name (thread_p,
					   DB_GET_STRING (&attrs[10].value),
					   &class_oid) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }
  db_push_oid (&attrs[0].value, &class_oid);

  /* loader_commands */
  or_advance (buf_p, vars[ORC_LOADER_COMMANDS_INDEX].length);

  /* representations */
  or_advance (buf_p, vars[ORC_REPRESENTATIONS_INDEX].length);

  /* sub_classes */
  error =
    catcls_get_object_set (thread_p, buf_p, vars[ORC_SUBCLASSES_INDEX].length,
			   &attrs[11]);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* super_classes */
  error =
    catcls_get_object_set (thread_p, buf_p,
			   vars[ORC_SUPERCLASSES_INDEX].length, &attrs[12]);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* attributes */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_ATTRIBUTES_INDEX].length,
		       &attrs[13], catcls_get_or_value_from_attribute);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* shared_attributes */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_SHARED_ATTRS_INDEX].length,
		       &attrs[14], catcls_get_or_value_from_attribute);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* class_attributes */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_CLASS_ATTRS_INDEX].length,
		       &attrs[15], catcls_get_or_value_from_attribute);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* methods */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_METHODS_INDEX].length,
		       &attrs[16], catcls_get_or_value_from_method);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* class_methods */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_CLASS_METHODS_INDEX].length,
		       &attrs[17], catcls_get_or_value_from_method);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* (apply attribute & method type) */
  catcls_apply_component_type (&attrs[13], 0);
  catcls_apply_component_type (&attrs[14], 2);
  catcls_apply_component_type (&attrs[15], 1);
  catcls_apply_component_type (&attrs[16], 0);
  catcls_apply_component_type (&attrs[17], 1);

  /* method_files */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_METHOD_FILES_INDEX].length,
		       &attrs[18], catcls_get_or_value_from_method_file);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* (resolutions) */
  if (vars[ORC_RESOLUTIONS_INDEX].length > 0)
    {
      resolution_p = catcls_allocate_or_value (1);
      if (resolution_p == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      error =
	catcls_get_subset (thread_p, buf_p,
			   vars[ORC_RESOLUTIONS_INDEX].length, resolution_p,
			   catcls_get_or_value_from_resolution);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      catcls_apply_resolutions (&attrs[13], resolution_p);
      catcls_apply_resolutions (&attrs[14], resolution_p);
      catcls_apply_resolutions (&attrs[15], resolution_p);
      catcls_apply_resolutions (&attrs[16], resolution_p);
      catcls_apply_resolutions (&attrs[17], resolution_p);
      catcls_free_or_value (resolution_p);
      resolution_p = NULL;
    }

  /* query_spec */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_QUERY_SPEC_INDEX].length,
		       &attrs[19], catcls_get_or_value_from_query_spec);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* triggers */
  or_advance (buf_p, vars[ORC_TRIGGERS_INDEX].length);

  /* properties */
  error =
    catcls_get_property_set (thread_p, buf_p,
			     vars[ORC_PROPERTIES_INDEX].length, &attrs[20]);
  if (error != NO_ERROR)
    {
      goto error;
    }

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  if (resolution_p)
    {
      catcls_free_or_value (resolution_p);
    }

  return error;
}

/*
 * catcls_get_or_value_from_attribute () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_attribute (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				    OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  DB_VALUE default_expr, val;
  DB_SEQ *att_props;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Attribute);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* The order of attrs[] is same with that of ct_attribute_atts[]. */
  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_attribute.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* id */
  or_advance (buf_p, OR_INT_SIZE);

  /* type */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[4].value, NULL, -1, true, NULL,
				0);

  /* offset */
  or_advance (buf_p, OR_INT_SIZE);

  /* order */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[5].value, NULL, -1, true, NULL,
				0);

  /* class */
  attr_val_p = &attrs[6].value;
  (*(tp_Object.data_readval)) (buf_p, attr_val_p, NULL, -1, true, NULL, 0);
  error = catcls_convert_class_oid_to_oid (thread_p, attr_val_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* flag */
  attr_val_p = &attrs[7].value;
  (*(tp_Integer.data_readval)) (buf_p, attr_val_p, NULL, -1, true, NULL, 0);

  /* for 'is_nullable', reverse NON_NULL flag */
  db_make_int (attr_val_p,
	       (DB_GET_INT (attr_val_p) & SM_ATTFLAG_NON_NULL) ? false :
	       true);

  /* index_file_id */
  or_advance (buf_p, OR_INT_SIZE);

  /* index_root_pageid */
  or_advance (buf_p, OR_INT_SIZE);

  /* index_volid_key */
  or_advance (buf_p, OR_INT_SIZE);

  /** variable **/

  /* name */
  attr_val_p = &attrs[1].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_ATT_NAME_INDEX].length, true, NULL,
			       0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  /* default value */
  attr_val_p = &attrs[8].value;
  or_get_value (buf_p, attr_val_p, NULL,
		vars[ORC_ATT_CURRENT_VALUE_INDEX].length, true);

  /* original value - advance only */
  or_advance (buf_p, vars[ORC_ATT_ORIGINAL_VALUE_INDEX].length);

  /* domain */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_ATT_DOMAIN_INDEX].length,
		       &attrs[9], catcls_get_or_value_from_domain);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* Complete default value of enumeration type. We need to access domain
   * structure of the attribute in order to get access to the elements of
   * enumeration type. */
  if (DB_VALUE_TYPE (attr_val_p) == DB_TYPE_ENUMERATION
      && attrs[9].sub.count > 0)
    {
      OR_VALUE *or_val = attrs[9].sub.value;

      if (or_val == NULL || or_val->sub.count < 8)
	{
	  goto error;
	}
      or_val = or_val[0].sub.value;
      if (or_val == NULL)
	{
	  goto error;
	}
      or_val = &or_val[7];
      if (!TP_IS_SET_TYPE (DB_VALUE_TYPE (&or_val->value)) ||
	  DB_GET_ENUM_SHORT (attr_val_p) > or_val->value.data.set->set->size)
	{
	  goto error;
	}
      if (set_get_element (DB_GET_SET (&or_val->value),
			   DB_GET_ENUM_SHORT (attr_val_p) - 1, &val)
	  != NO_ERROR)
	{
	  goto error;
	}

      val.need_clear = false;
      DB_MAKE_ENUMERATION (attr_val_p, DB_GET_ENUM_SHORT (attr_val_p),
			   DB_GET_STRING (&val), DB_GET_STRING_SIZE (&val),
			   DB_GET_ENUM_CODESET (attr_val_p),
			   DB_GET_ENUM_COLLATION (attr_val_p));
      attr_val_p->need_clear = true;
    }
  /* triggers - advance only */
  or_advance (buf_p, vars[ORC_ATT_TRIGGER_INDEX].length);

  /* properties */
  or_get_value (buf_p, &val, tp_domain_resolve_default (DB_TYPE_SEQUENCE),
		vars[ORC_ATT_PROPERTIES_INDEX].length, true);
  att_props = DB_GET_SEQUENCE (&val);
  attr_val_p = &attrs[8].value;
  if (att_props != NULL &&
      classobj_get_prop (att_props, "default_expr", &default_expr) > 0)
    {
      char *str_val;

      str_val =
	(char *) db_private_alloc (thread_p, strlen ("UNIX_TIMESTAMP") + 1);

      if (str_val == NULL)
	{
	  pr_clear_value (&default_expr);
	  pr_clear_value (&val);
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      switch (DB_GET_INT (&default_expr))
	{
	case DB_DEFAULT_SYSDATE:
	  strcpy (str_val, "SYS_DATE");
	  break;

	case DB_DEFAULT_SYSDATETIME:
	  strcpy (str_val, "SYS_DATETIME");
	  break;

	case DB_DEFAULT_SYSTIMESTAMP:
	  strcpy (str_val, "SYS_TIMESTAMP");
	  break;

	case DB_DEFAULT_UNIX_TIMESTAMP:
	  strcpy (str_val, "UNIX_TIMESTAMP");
	  break;

	case DB_DEFAULT_USER:
	  strcpy (str_val, "USER");
	  break;

	case DB_DEFAULT_CURR_USER:
	  strcpy (str_val, "CURRENT_USER");
	  break;

	default:
	  pr_clear_value (&default_expr);
	  pr_clear_value (&val);
	  assert (false);
	  error = ER_GENERIC_ERROR;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  db_private_free_and_init (thread_p, str_val);
	  goto error;
	  break;
	}

      pr_clear_value (attr_val_p);	/*clean old default value */
      DB_MAKE_STRING (attr_val_p, str_val);
    }
  else
    {
      valcnv_convert_value_to_string (attr_val_p);
    }
  pr_clear_value (&val);
  attr_val_p->need_clear = true;
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_attrid () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_attrid (THREAD_ENTRY * thread_p, OR_BUF * buf,
				 OR_VALUE * value)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val;
  OR_VARINFO *vars = NULL;
  int size;
  char *start_ptr;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value, &ct_Attrid);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value->sub.value;

  /* variable offset */
  start_ptr = buf->ptr;

  size = tf_Metaclass_attribute.n_variable;
  vars = or_get_var_table (buf, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* id */
  (*(tp_Integer.data_readval)) (buf, &attrs[0].value, NULL, -1, true, NULL,
				0);

  or_advance (buf,
	      (int) (start_ptr - buf->ptr) + vars[ORC_ATT_NAME_INDEX].offset);

  /* name */
  attr_val = &attrs[1].value;
  (*(tp_String.data_readval)) (buf, attr_val, NULL,
			       vars[ORC_ATT_NAME_INDEX].length, true, NULL,
			       0);
  db_string_truncate (attr_val, DB_MAX_IDENTIFIER_LENGTH);

  /* go to the end */
  or_advance (buf, (int) (start_ptr - buf->ptr)
	      + (vars[(size - 1)].offset + vars[(size - 1)].length));

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_domain () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_domain (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				 OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Domain);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_domain.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* type */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[1].value, NULL, -1, true, NULL,
				0);

  /* precision */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[2].value, NULL, -1, true, NULL,
				0);

  /* scale */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[3].value, NULL, -1, true, NULL,
				0);

  /* codeset */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[4].value, NULL, -1, true, NULL,
				0);

  /* collation id */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[5].value, NULL, -1, true, NULL,
				0);

  /* class */
  attr_val_p = &attrs[6].value;
  (*(tp_Object.data_readval)) (buf_p, attr_val_p, NULL, -1, true, NULL, 0);

  if (!DB_IS_NULL (attr_val_p))
    {
      error = catcls_convert_class_oid_to_oid (thread_p, attr_val_p);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      if (DB_IS_NULL (attr_val_p))
	{
	  /* if self reference for example, "class x (a x)"
	     set an invalid data type, and fill its value later */
	  error = db_value_domain_init (attr_val_p, DB_TYPE_VARIABLE,
					DB_DEFAULT_PRECISION,
					DB_DEFAULT_SCALE);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* enumeration */
  if (vars[ORC_DOMAIN_ENUMERATION_INDEX].length)
    {
      /* enumerations are stored as a collection of strings */
      TP_DOMAIN *string_dom = tp_domain_resolve_default (DB_TYPE_STRING);
      PR_TYPE *seq_type = PR_TYPE_FROM_ID (DB_TYPE_SEQUENCE);

      TP_DOMAIN *domain =
	tp_domain_construct (DB_TYPE_SEQUENCE, NULL, 0, 0, string_dom);
      if (domain == NULL)
	{
	  goto error;
	}
      domain = tp_domain_cache (domain);

      (*(seq_type->data_readval)) (buf_p, &attrs[7].value, domain, -1, true,
				   NULL, 0);
    }

  /* set_domain */
  error =
    catcls_get_subset (thread_p, buf_p,
		       vars[ORC_DOMAIN_SETDOMAIN_INDEX].length, &attrs[8],
		       catcls_get_or_value_from_domain);
  if (error != NO_ERROR)
    {
      goto error;
    }

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_method () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_method (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				 OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Method);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_method.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* class */
  attr_val_p = &attrs[4].value;
  (*(tp_Object.data_readval)) (buf_p, attr_val_p, NULL, -1, true, NULL, 0);
  error = catcls_convert_class_oid_to_oid (thread_p, attr_val_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* id */
  or_advance (buf_p, OR_INT_SIZE);

  /* name */
  attr_val_p = &attrs[1].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_METHOD_NAME_INDEX].length, true, NULL,
			       0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  /* signatures */
  error =
    catcls_get_subset (thread_p, buf_p,
		       vars[ORC_METHOD_SIGNATURE_INDEX].length, &attrs[5],
		       catcls_get_or_value_from_method_signiture);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* properties */
  or_advance (buf_p, vars[ORC_METHOD_PROPERTIES_INDEX].length);

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_method_signiture () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_method_signiture (THREAD_ENTRY * thread_p,
					   OR_BUF * buf_p, OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Methsig);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /* variable offset */
  size = tf_Metaclass_methsig.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* arg_count */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[1].value, NULL, -1, true, NULL,
				0);

  /* function_name */
  attr_val_p = &attrs[2].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_METHSIG_FUNCTION_NAME_INDEX].length,
			       true, NULL, 0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  /* string_def */
  or_advance (buf_p, vars[ORC_METHSIG_SQL_DEF_INDEX].length);

  /* return_value */
  error =
    catcls_get_subset (thread_p, buf_p,
		       vars[ORC_METHSIG_RETURN_VALUE_INDEX].length, &attrs[3],
		       catcls_get_or_value_from_method_argument);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* arguments */
  error =
    catcls_get_subset (thread_p, buf_p,
		       vars[ORC_METHSIG_ARGUMENTS_INDEX].length, &attrs[4],
		       catcls_get_or_value_from_method_argument);
  if (error != NO_ERROR)
    {
      goto error;
    }

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_method_argument () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_method_argument (THREAD_ENTRY * thread_p,
					  OR_BUF * buf_p, OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  OR_VARINFO *vars = NULL;
  int size;
  int error;

  error = catcls_expand_or_value_by_def (value_p, &ct_Metharg);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_metharg.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* type */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[1].value, NULL, -1, true, NULL,
				0);

  /* index */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[2].value, NULL, -1, true, NULL,
				0);

  /* domain */
  error =
    catcls_get_subset (thread_p, buf_p, vars[ORC_METHARG_DOMAIN_INDEX].length,
		       &attrs[3], catcls_get_or_value_from_domain);
  if (error != NO_ERROR)
    {
      goto error;
    }

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_method_file () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_method_file (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				      OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Methfile);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_methfile.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* class */
  attr_val_p = &attrs[1].value;
  (*(tp_Object.data_readval)) (buf_p, attr_val_p, NULL, -1, true, NULL, 0);
  error = catcls_convert_class_oid_to_oid (thread_p, attr_val_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* name */
  attr_val_p = &attrs[2].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_METHFILE_NAME_INDEX].length, true,
			       NULL, 0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  /* properties */
  or_advance (buf_p, vars[ORC_METHFILE_PROPERTIES_INDEX].length);

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_resolution () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_resolution (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				     OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Resolution);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_resolution.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* class */
  attr_val_p = &attrs[0].value;
  (*(tp_Object.data_readval)) (buf_p, attr_val_p, NULL, -1, true, NULL, 0);
  error = catcls_convert_class_oid_to_oid (thread_p, attr_val_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* type */
  (*(tp_Integer.data_readval)) (buf_p, &attrs[2].value, NULL, -1, true, NULL,
				0);

  /* name */
  attr_val_p = &attrs[3].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_RES_NAME_INDEX].length, true, NULL,
			       0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  /* alias */
  attr_val_p = &attrs[1].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_RES_ALIAS_INDEX].length, true, NULL,
			       0);
  db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_query_spec () -
 *   return:
 *   buf(in):
 *   value(in):
 */
static int
catcls_get_or_value_from_query_spec (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				     OR_VALUE * value_p)
{
  OR_VALUE *attrs;
  DB_VALUE *attr_val_p;
  OR_VARINFO *vars = NULL;
  int size;
  int error = NO_ERROR;

  error = catcls_expand_or_value_by_def (value_p, &ct_Queryspec);
  if (error != NO_ERROR)
    {
      goto error;
    }

  attrs = value_p->sub.value;

  /** variable offset **/
  size = tf_Metaclass_query_spec.n_variable;
  vars = or_get_var_table (buf_p, size, catcls_unpack_allocator);
  if (vars == NULL)
    {
      int msize = size * sizeof (OR_VARINFO);

      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, msize);
      goto error;
    }

  /* specification */
  attr_val_p = &attrs[1].value;
  (*(tp_String.data_readval)) (buf_p, attr_val_p, NULL,
			       vars[ORC_QUERY_SPEC_SPEC_INDEX].length, true,
			       NULL, 0);
  db_string_truncate (attr_val_p, DB_MAX_SPEC_LENGTH);

  if (vars)
    {
      free_and_init (vars);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  return error;
}

/*
 * catcls_get_or_value_from_indexes () -
 *   return:
 *   seq(in):
 *   values(in):
 *   is_unique(in):
 *   is_reverse(in):
 *   is_primary_key(in):
 *   is_foreign_key(in):
 */
static int
catcls_get_or_value_from_indexes (DB_SEQ * seq_p, OR_VALUE * values,
				  int is_unique, int is_reverse,
				  int is_primary_key, int is_foreign_key)
{
  int seq_size;
  DB_VALUE keys, svalue, val, avalue, *pvalue = NULL;
  DB_SEQ *key_seq_p = NULL, *seq = NULL, *pred_seq = NULL, *prefix_seq = NULL;
  int key_size, att_cnt;
  OR_VALUE *attrs, *key_attrs;
  DB_VALUE *attr_val_p;
  OR_VALUE *subset_p = NULL;
  int e, i, j, k;
  int has_function_index = 0;
  int error = NO_ERROR;

  db_value_put_null (&keys);
  db_value_put_null (&svalue);
  db_value_put_null (&val);
  db_value_put_null (&avalue);
  seq_size = set_size (seq_p);
  for (i = 0, j = 0; i < seq_size; i += 2, j++)
    {
      has_function_index = 0;
      prefix_seq = NULL;
      error = catcls_expand_or_value_by_def (&values[j], &ct_Index);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      attrs = values[j].sub.value;

      /* index_name */
      attr_val_p = &attrs[1].value;
      error = set_get_element (seq_p, i, attr_val_p);
      if (error != NO_ERROR)
	{
	  goto error;
	}
      db_string_truncate (attr_val_p, DB_MAX_IDENTIFIER_LENGTH);

      /* (is_unique) */
      db_make_int (&attrs[2].value, is_unique);

      error = set_get_element (seq_p, i + 1, &keys);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      if (DB_VALUE_TYPE (&keys) == DB_TYPE_SEQUENCE)
	{
	  key_seq_p = DB_GET_SEQUENCE (&keys);
	}
      else
	{
	  assert (0);
	  error = ER_SM_INVALID_PROPERTY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	  goto error;
	}

      /* the sequence of keys also includes the B+tree ID and the filter
       * predicate expression in the first two positions (0 and 1) */
      key_size = set_size (key_seq_p);
      att_cnt = (key_size - 1) / 2;

      if (!is_primary_key && !is_foreign_key)
	{
	  /* prefix_length or filter index */
	  error = set_get_element (key_seq_p, key_size - 1, &svalue);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  if (DB_VALUE_TYPE (&svalue) != DB_TYPE_SEQUENCE)
	    {
	      error = ER_SM_INVALID_PROPERTY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto error;
	    }

	  seq = DB_GET_SEQUENCE (&svalue);
	  error = set_get_element (seq, 0, &val);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  if (DB_VALUE_TYPE (&val) == DB_TYPE_INTEGER)
	    {
	      /* have prefix length */
	      prefix_seq = seq;
	    }
	  else if (DB_VALUE_TYPE (&val) == DB_TYPE_SEQUENCE)
	    {
	      DB_SET *child_seq = DB_GET_SEQUENCE (&val);
	      int seq_size = set_size (seq);
	      int flag, l = 0;
	      DB_VALUE temp;
	      int col_id, att_index_start;
	      char *buffer, *ptr;
	      TP_DOMAIN *fi_domain = NULL;

	      /* have filter or function index */
	      while (true)
		{
		  flag = 0;
		  error = set_get_element (child_seq, 0, &avalue);
		  if (error != NO_ERROR)
		    {
		      goto error;
		    }

		  if (DB_IS_NULL (&avalue) ||
		      DB_VALUE_TYPE (&avalue) != DB_TYPE_STRING)
		    {
		      error = ER_SM_INVALID_PROPERTY;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		      goto error;
		    }

		  if (!intl_identifier_casecmp (DB_GET_STRING (&avalue),
						SM_FILTER_INDEX_ID))
		    {
		      flag = 0x01;
		    }
		  else if (!intl_identifier_casecmp (DB_GET_STRING (&avalue),
						     SM_FUNCTION_INDEX_ID))
		    {
		      flag = 0x02;
		    }
		  else if (!intl_identifier_casecmp
			   (DB_GET_STRING (&avalue), SM_PREFIX_INDEX_ID))
		    {
		      flag = 0x03;
		    }

		  pr_clear_value (&avalue);

		  error = set_get_element (child_seq, 1, &avalue);
		  if (error != NO_ERROR)
		    {
		      goto error;
		    }

		  if (DB_VALUE_TYPE (&avalue) != DB_TYPE_SEQUENCE)
		    {
		      error = ER_SM_INVALID_PROPERTY;
		      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
		      goto error;
		    }

		  switch (flag)
		    {
		    case 0x01:
		      pred_seq = DB_GET_SEQUENCE (&avalue);
		      attr_val_p = &attrs[8].value;
		      error = set_get_element (pred_seq, 0, attr_val_p);
		      if (error != NO_ERROR)
			{
			  goto error;
			}
		      break;

		    case 0x02:
		      has_function_index = 1;
		      pred_seq = DB_GET_SEQUENCE (&avalue);

		      error = set_get_element_nocopy (pred_seq, 2, &temp);
		      if (error != NO_ERROR)
			{
			  goto error;
			}
		      col_id = db_get_int (&temp);

		      error = set_get_element_nocopy (pred_seq, 3, &temp);
		      if (error != NO_ERROR)
			{
			  goto error;
			}
		      att_index_start = db_get_int (&temp);
		      assert (col_id <= att_index_start);

		      att_cnt = att_index_start + 1;

		      /* key_count */
		      db_make_int (&attrs[3].value, att_cnt);

		      subset_p = catcls_allocate_or_value (att_cnt);
		      if (subset_p == NULL)
			{
			  error = ER_OUT_OF_VIRTUAL_MEMORY;
			  goto error;
			}

		      attrs[4].sub.value = subset_p;
		      attrs[4].sub.count = att_cnt;

		      /* key_attrs */
		      e = 1;
		      for (k = 0; k < att_cnt; k++)	/* for each [attrID, asc_desc]+ */
			{
			  error =
			    catcls_expand_or_value_by_def (&subset_p[k],
							   &ct_Indexkey);
			  if (error != NO_ERROR)
			    {
			      goto error;
			    }

			  key_attrs = subset_p[k].sub.value;

			  if (k != col_id)
			    {
			      /* key_attr_id */
			      attr_val_p = &key_attrs[1].value;
			      error = set_get_element (key_seq_p, e++,
						       attr_val_p);
			      if (error != NO_ERROR)
				{
				  goto error;
				}

			      /* key_order */
			      db_make_int (&key_attrs[2].value, k);

			      /* asc_desc */
			      attr_val_p = &key_attrs[3].value;
			      error = set_get_element (key_seq_p, e++,
						       attr_val_p);
			      if (error != NO_ERROR)
				{
				  goto error;
				}

			      /* function name */
			      db_make_null (&key_attrs[5].value);
			    }
			  else
			    {
			      /* key_attr_id */
			      db_make_null (&key_attrs[1].value);

			      /* key_order */
			      db_make_int (&key_attrs[2].value, k);

			      /* asc_desc */
			      error = set_get_element_nocopy (pred_seq, 4,
							      &temp);
			      if (error != NO_ERROR)
				{
				  goto error;
				}

			      buffer = DB_GET_STRING (&temp);
			      ptr = buffer;
			      ptr = or_unpack_domain (ptr, &fi_domain, NULL);

			      db_make_int (&key_attrs[3].value,
					   fi_domain->is_desc);
			      tp_domain_free (fi_domain);

			      /* function name */
			      attr_val_p = &key_attrs[5].value;
			      error = set_get_element (pred_seq, 0,
						       attr_val_p);
			      if (error != NO_ERROR)
				{
				  goto error;
				}
			    }

			  /* prefix_length */
			  db_make_int (&key_attrs[4].value, -1);
			}

		      break;

		    case 0x03:
		      pvalue = db_value_copy (&avalue);
		      prefix_seq = DB_GET_SEQUENCE (pvalue);
		      break;

		    default:
		      break;
		    }

		  pr_clear_value (&avalue);

		  l++;
		  if (l >= seq_size)
		    {
		      break;
		    }

		  pr_clear_value (&val);
		  if (set_get_element (seq, l, &val) != NO_ERROR)
		    {
		      goto error;
		    }
		  if (DB_VALUE_TYPE (&val) != DB_TYPE_SEQUENCE)
		    {
		      goto error;
		    }

		  child_seq = DB_GET_SEQUENCE (&val);
		}
	    }
	  else
	    {
	      error = ER_SM_INVALID_PROPERTY;
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
	      goto error;
	    }
	  pr_clear_value (&val);
	}

      /* key_count */
      if (has_function_index == 0)
	{
	  db_make_int (&attrs[3].value, att_cnt);

	  subset_p = catcls_allocate_or_value (att_cnt);
	  if (subset_p == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto error;
	    }

	  attrs[4].sub.value = subset_p;
	  attrs[4].sub.count = att_cnt;

	  /* key_attrs */
	  e = 1;
	  for (k = 0; k < att_cnt; k++)	/* for each [attrID, asc_desc]+ */
	    {
	      error = catcls_expand_or_value_by_def (&subset_p[k],
						     &ct_Indexkey);
	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      key_attrs = subset_p[k].sub.value;

	      /* key_attr_id */
	      attr_val_p = &key_attrs[1].value;
	      error = set_get_element (key_seq_p, e++, attr_val_p);
	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      /* key_order */
	      db_make_int (&key_attrs[2].value, k);

	      /* asc_desc */
	      attr_val_p = &key_attrs[3].value;
	      error = set_get_element (key_seq_p, e++, attr_val_p);
	      if (error != NO_ERROR)
		{
		  goto error;
		}

	      /* prefix_length */
	      db_make_int (&key_attrs[4].value, -1);

	      /* function name */
	      db_make_null (&key_attrs[5].value);
	    }
	}

      if (prefix_seq)
	{
	  for (k = 0; k < att_cnt; k++)
	    {
	      key_attrs = subset_p[k].sub.value;
	      attr_val_p = &key_attrs[4].value;

	      error = set_get_element (prefix_seq, k, attr_val_p);
	      if (error != NO_ERROR)
		{
		  goto error;
		}
	    }
	}

      pr_clear_value (&svalue);
      pr_clear_value (&keys);
      if (pvalue)
	{
	  db_value_free (pvalue);
	  pvalue = NULL;
	}

      /* is_reverse */
      db_make_int (&attrs[5].value, is_reverse);

      /* is_primary_key */
      db_make_int (&attrs[6].value, is_primary_key);

      /* is_foreign_key */
      db_make_int (&attrs[7].value, is_foreign_key);

      /* have_function */
      db_make_int (&attrs[9].value, has_function_index);
    }

  return NO_ERROR;

error:

  pr_clear_value (&keys);
  pr_clear_value (&svalue);
  pr_clear_value (&val);
  pr_clear_value (&avalue);
  if (pvalue)
    {
      db_value_free (pvalue);
    }
  return error;
}

/*
 * catcls_get_subset () -
 *   return:
 *   buf(in):
 *   expected_size(in):
 *   value(in):
 *   reader(in):
 */
static int
catcls_get_subset (THREAD_ENTRY * thread_p, OR_BUF * buf_p, int expected_size,
		   OR_VALUE * value_p, CREADER reader)
{
  OR_VALUE *subset_p;
  int count, i;
  int error = NO_ERROR;

  if (expected_size == 0)
    {
      value_p->sub.count = 0;
      return NO_ERROR;
    }

  count = or_skip_set_header (buf_p);
  subset_p = catcls_allocate_or_value (count);
  if (subset_p == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  value_p->sub.value = subset_p;
  value_p->sub.count = count;

  for (i = 0; i < count; i++)
    {
      error = (*reader) (thread_p, buf_p, &subset_p[i]);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * catcls_get_object_set () -
 *   return:
 *   buf(in):
 *   expected_size(in):
 *   value(in):
 */
static int
catcls_get_object_set (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
		       int expected_size, OR_VALUE * value_p)
{
  DB_SET *oid_set_p = NULL;
  DB_VALUE oid_val;
  int count, i;
  int error = NO_ERROR;

  if (expected_size == 0)
    {
      return NO_ERROR;
    }

  count = or_skip_set_header (buf_p);
  oid_set_p = set_create_sequence (count);
  if (oid_set_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  for (i = 0; i < count; i++)
    {
      (*(tp_Object.data_readval)) (buf_p, &oid_val, NULL, -1, true, NULL, 0);

      error = catcls_convert_class_oid_to_oid (thread_p, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      error = set_put_element (oid_set_p, i, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  db_make_sequence (&value_p->value, oid_set_p);
  return NO_ERROR;

error:

  if (oid_set_p)
    {
      set_free (oid_set_p);
    }

  return error;
}

/*
 * catcls_get_property_set () -
 *   return:
 *   buf(in):
 *   expected_size(in):
 *   value(in):
 */
static int
catcls_get_property_set (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
			 int expected_size, OR_VALUE * value_p)
{
  DB_VALUE prop_val;
  DB_SEQ *prop_seq_p = NULL;
  int n_size = 0;
  CATCLS_PROPERTY property_vars[SM_PROPERTY_NUM_INDEX_FAMILY] = {
    {SM_PROPERTY_PRIMARY_KEY, NULL, 0, true, false, true, false},
    {SM_PROPERTY_UNIQUE, NULL, 0, true, false, false, false},
    {SM_PROPERTY_REVERSE_UNIQUE, NULL, 0, true, true, false, false},
    {SM_PROPERTY_INDEX, NULL, 0, false, false, false, false},
    {SM_PROPERTY_REVERSE_INDEX, NULL, 0, false, true, false, false},
    {SM_PROPERTY_FOREIGN_KEY, NULL, 0, false, false, false, true},
  };

  DB_VALUE vals[SM_PROPERTY_NUM_INDEX_FAMILY];
  OR_VALUE *subset_p = NULL;
  int error = NO_ERROR;
  int i, idx;

  if (expected_size == 0)
    {
      value_p->sub.count = 0;
      return NO_ERROR;
    }

  db_value_put_null (&prop_val);
  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
    {
      db_value_put_null (&vals[i]);
    }

  (*(tp_Sequence.data_readval)) (buf_p, &prop_val, NULL, expected_size, true,
				 NULL, 0);
  prop_seq_p = DB_GET_SEQUENCE (&prop_val);

  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
    {
      if (prop_seq_p != NULL &&
	  (classobj_get_prop (prop_seq_p, property_vars[i].name, &vals[i]) >
	   0))
	{

	  if (DB_VALUE_TYPE (&vals[i]) == DB_TYPE_SEQUENCE)
	    {
	      property_vars[i].seq = DB_GET_SEQUENCE (&vals[i]);
	    }

	  if (property_vars[i].seq != NULL)
	    {
	      property_vars[i].size = set_size (property_vars[i].seq) / 2;
	      n_size += property_vars[i].size;
	    }
	}
    }

  if (n_size > 0)
    {
      subset_p = catcls_allocate_or_value (n_size);
      if (subset_p == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  goto error;
	}

      value_p->sub.value = subset_p;
      value_p->sub.count = n_size;
    }
  else
    {
      value_p->sub.value = NULL;
      value_p->sub.count = 0;
    }

  idx = 0;
  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
    {
      if (property_vars[i].seq != NULL)
	{
	  error = catcls_get_or_value_from_indexes (property_vars[i].seq,
						    &subset_p[idx],
						    property_vars
						    [i].is_unique,
						    property_vars
						    [i].is_reverse,
						    property_vars
						    [i].is_primary_key,
						    property_vars
						    [i].is_foreign_key);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}

      idx += property_vars[i].size;
    }

  error = catcls_convert_attr_id_to_name (thread_p, buf_p, value_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  pr_clear_value (&prop_val);
  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
    {
      pr_clear_value (&vals[i]);
    }

  return NO_ERROR;

error:
  pr_clear_value (&prop_val);
  for (i = 0; i < SM_PROPERTY_NUM_INDEX_FAMILY; i++)
    {
      pr_clear_value (&vals[i]);
    }

  return error;
}

/*
 * catcls_reorder_attributes_by_repr () -
 *   return:
 *   value(in):
 */
static int
catcls_reorder_attributes_by_repr (THREAD_ENTRY * thread_p,
				   OR_VALUE * value_p)
{
  OR_VALUE *attrs, *var_attrs;
  int n_attrs;
  DISK_ATTR *fixed_p, *variable_p;
  int n_fixed, n_variable;
  DISK_REPR *repr_p = NULL;
  REPR_ID repr_id;
  OID *class_oid_p;
  int i, j;
  int error = NO_ERROR;

  class_oid_p = &value_p->id.classoid;
  error =
    catalog_get_last_representation_id (thread_p, class_oid_p, &repr_id);
  if (error != NO_ERROR)
    {
      goto error;
    }
  else
    {
      repr_p = catalog_get_representation (thread_p, class_oid_p, repr_id);
      if (repr_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error;
	}
    }

  fixed_p = repr_p->fixed;
  n_fixed = repr_p->n_fixed;
  variable_p = repr_p->variable;
  n_variable = repr_p->n_variable;

  attrs = value_p->sub.value;
  n_attrs = n_fixed + n_variable;

  for (i = 0; i < n_fixed; i++)
    {
      for (j = i; j < n_attrs; j++)
	{
	  if (fixed_p[i].id == attrs[j].id.attrid)
	    {
	      if (i != j)
		{		/* need to exchange */
		  EXCHANGE_OR_VALUE (attrs[i], attrs[j]);
		}
	      break;
	    }
	}
    }

  var_attrs = &attrs[n_fixed];
  for (i = 0; i < n_variable; i++)
    {
      for (j = i; j < n_variable; j++)
	{
	  if (variable_p[i].id == var_attrs[j].id.attrid)
	    {
	      if (i != j)
		{		/* need to exchange */
		  EXCHANGE_OR_VALUE (var_attrs[i], var_attrs[j]);
		}
	      break;
	    }
	}
    }
  catalog_free_representation (repr_p);

  return NO_ERROR;

error:

  if (repr_p)
    {
      catalog_free_representation (repr_p);
    }

  return error;
}

/*
 * catcls_expand_or_value_by_repr () -
 *   return:
 *   value(in):
 *   class_oid(in):
 *   rep(in):
 */
static int
catcls_expand_or_value_by_repr (OR_VALUE * value_p, OID * class_oid_p,
				DISK_REPR * repr_p)
{
  OR_VALUE *attrs, *var_attrs;
  int n_attrs;
  DISK_ATTR *fixed_p, *variable_p;
  int n_fixed, n_variable;
  int i;
  int error = NO_ERROR;

  fixed_p = repr_p->fixed;
  n_fixed = repr_p->n_fixed;
  variable_p = repr_p->variable;
  n_variable = repr_p->n_variable;

  n_attrs = n_fixed + n_variable;
  attrs = catcls_allocate_or_value (n_attrs);
  if (attrs == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  value_p->sub.value = attrs;
  value_p->sub.count = n_attrs;

  COPY_OID (&value_p->id.classoid, class_oid_p);

  for (i = 0; i < n_fixed; i++)
    {
      attrs[i].id.attrid = fixed_p[i].id;
      error = db_value_domain_init (&attrs[i].value, fixed_p[i].type,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  var_attrs = &attrs[n_fixed];
  for (i = 0; i < n_variable; i++)
    {
      var_attrs[i].id.attrid = variable_p[i].id;
      error = db_value_domain_init (&var_attrs[i].value, variable_p[i].type,
				    DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  return NO_ERROR;
}

/*
 * catcls_expand_or_value_by_subset () -
 *   return:
 *   value(in):
 */
static void
catcls_expand_or_value_by_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p)
{
  DB_SET *set_p;
  int size, i;
  DB_VALUE element;
  OID *oid_p, class_oid;
  OR_VALUE *subset_p;

  if (pr_is_set_type (DB_VALUE_TYPE (&value_p->value)))
    {
      set_p = DB_PULL_SET (&value_p->value);
      size = set_size (set_p);
      if (size > 0)
	{
	  set_get_element_nocopy (set_p, 0, &element);
	  if (DB_VALUE_TYPE (&element) == DB_TYPE_OID)
	    {
	      oid_p = DB_PULL_OID (&element);
	      (void) heap_get_class_oid (thread_p, &class_oid, oid_p,
					 NEED_SNAPSHOT);

	      if (!OID_EQ (&class_oid, &ct_Class.classoid))
		{
		  subset_p = catcls_allocate_or_value (size);
		  if (subset_p != NULL)
		    {
		      value_p->sub.value = subset_p;
		      value_p->sub.count = size;

		      for (i = 0; i < size; i++)
			{
			  COPY_OID (&((subset_p[i]).id.classoid), &class_oid);
			}
		    }
		}
	    }
	}
    }
}

/*
 * catcls_put_or_value_into_buffer () -
 *   return:
 *   value(in):
 *   chn(in):
 *   buf(in):
 *   class_oid(in):
 *   rep(in):
 */
static int
catcls_put_or_value_into_buffer (OR_VALUE * value_p, int chn, OR_BUF * buf_p,
				 OID * class_oid_p, DISK_REPR * repr_p)
{
  OR_VALUE *attrs, *var_attrs;
  int n_attrs;
  DISK_ATTR *fixed_p, *variable_p;
  int n_fixed, n_variable;
  DB_TYPE data_type;
  unsigned int repr_id_bits;
  char *bound_bits = NULL;
  int bound_size;
  char *offset_p, *start_p;
  int i, pad, offset, header_size;
  int error = NO_ERROR;

  fixed_p = repr_p->fixed;
  n_fixed = repr_p->n_fixed;
  variable_p = repr_p->variable;
  n_variable = repr_p->n_variable;

  attrs = value_p->sub.value;
  n_attrs = n_fixed + n_variable;

  bound_size = OR_BOUND_BIT_BYTES (n_fixed);
  bound_bits = (char *) malloc (bound_size);
  if (bound_bits == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, bound_size);
      goto error;
    }
  memset (bound_bits, 0, bound_size);

  /* header */

  repr_id_bits = repr_p->id;
  if (n_fixed)
    {
      repr_id_bits |= OR_BOUND_BIT_FLAG;
    }

  OR_SET_VAR_OFFSET_SIZE (repr_id_bits, BIG_VAR_OFFSET_SIZE);	/* 4byte */

  if (prm_get_bool_value (PRM_ID_MVCC_ENABLED))
    {
      repr_id_bits |= (OR_MVCC_FLAG_VALID_INSID << OR_MVCC_FLAG_SHIFT_BITS);
      or_put_int (buf_p, repr_id_bits);
      or_put_bigint (buf_p, MVCCID_NULL);	/* MVCC insert id */
      or_put_int (buf_p, chn);	/* CHN */
      header_size = OR_MVCC_INSERT_HEADER_SIZE;
    }
  else
    {
      or_put_int (buf_p, repr_id_bits);
      or_put_int (buf_p, chn);
      header_size = OR_NON_MVCC_HEADER_SIZE;
    }

  /* offset table */
  offset_p = buf_p->ptr;
  or_advance (buf_p, OR_VAR_TABLE_SIZE (n_variable));

  /* fixed */
  start_p = buf_p->ptr;
  for (i = 0; i < n_fixed; i++)
    {
      data_type = fixed_p[i].type;

      if (DB_IS_NULL (&attrs[i].value))
	{
	  or_advance (buf_p, (*tp_Type_id_map[data_type]).disksize);
	  OR_CLEAR_BOUND_BIT (bound_bits, i);
	}
      else
	{
	  (*((*tp_Type_id_map[data_type]).data_writeval)) (buf_p,
							   &attrs[i].value);
	  OR_ENABLE_BOUND_BIT (bound_bits, i);
	}
    }

  pad = (int) (buf_p->ptr - start_p);
  if (pad < repr_p->fixed_length)
    {
      or_pad (buf_p, repr_p->fixed_length - pad);
    }
  else if (pad > repr_p->fixed_length)
    {
      error = ER_SM_CORRUPTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto error;
    }

  /* bound bits */
  if (n_fixed)
    {
      or_put_data (buf_p, bound_bits, bound_size);
    }

  /* variable */
  var_attrs = &attrs[n_fixed];
  for (i = 0; i < n_variable; i++)
    {
      /* the variable offsets are relative to end of the class record header */
      offset = (int) (buf_p->ptr - buf_p->buffer - header_size);

      data_type = variable_p[i].type;
      (*((*tp_Type_id_map[data_type]).data_writeval)) (buf_p,
						       &var_attrs[i].value);

      OR_PUT_OFFSET (offset_p, offset);
      offset_p += BIG_VAR_OFFSET_SIZE;
    }

  /* put last offset */
  offset = (int) (buf_p->ptr - buf_p->buffer - header_size);
  OR_PUT_OFFSET (offset_p, offset);

  if (bound_bits)
    {
      free_and_init (bound_bits);
    }

  return NO_ERROR;

error:

  if (bound_bits)
    {
      free_and_init (bound_bits);
    }

  return error;
}

/*
 * catcls_get_or_value_from_buffer () -
 *   return:
 *   buf(in):
 *   value(in):
 *   rep(in):
 */
static int
catcls_get_or_value_from_buffer (THREAD_ENTRY * thread_p, OR_BUF * buf_p,
				 OR_VALUE * value_p, DISK_REPR * repr_p)
{
  OR_VALUE *attrs, *var_attrs;
  int n_attrs;
  DISK_ATTR *fixed_p, *variable_p;
  int n_fixed, n_variable;
  DB_TYPE data_type;
  OR_VARINFO *vars = NULL;
  unsigned int repr_id_bits;
  char *bound_bits = NULL;
  int bound_bits_flag = false;
  char *start_p;
  int i, pad, size, rc;
  int error = NO_ERROR;

  fixed_p = repr_p->fixed;
  n_fixed = repr_p->n_fixed;
  variable_p = repr_p->variable;
  n_variable = repr_p->n_variable;

  attrs = value_p->sub.value;
  n_attrs = n_fixed + n_variable;

  /* header */
  assert (OR_GET_OFFSET_SIZE (buf_p->ptr) == BIG_VAR_OFFSET_SIZE);

  if (prm_get_bool_value (PRM_ID_MVCC_ENABLED))
    {
      char mvcc_flags;
      repr_id_bits = or_mvcc_get_repid_and_flags (buf_p, &rc);
      /* get bound_bits_flag and skip other MVCC header fields */
      bound_bits_flag = repr_id_bits & OR_BOUND_BIT_FLAG;
      mvcc_flags =
	(char) ((repr_id_bits >> OR_MVCC_FLAG_SHIFT_BITS) &
		OR_MVCC_FLAG_MASK);
      repr_id_bits = repr_id_bits & OR_MVCC_REPID_MASK;
      /* check that only OR_MVCC_FLAG_VALID_INSID is set */

      if (mvcc_flags & OR_MVCC_FLAG_VALID_INSID)
	{
	  or_advance (buf_p, OR_INT64_SIZE);	/* skip INS_ID */
	}

      if (mvcc_flags &
	  (OR_MVCC_FLAG_VALID_DELID | OR_MVCC_FLAG_VALID_LONG_CHN))
	{
	  or_advance (buf_p, OR_INT64_SIZE);	/* skip DEL_ID / long CHN */
	}
      else
	{
	  or_advance (buf_p, OR_INT_SIZE);	/* skip short CHN */
	}

      if (mvcc_flags & OR_MVCC_FLAG_VALID_NEXT_VERSION)
	{
	  or_advance (buf_p, OR_OID_SIZE);	/* skip short CHN */
	}
    }
  else
    {
      repr_id_bits = or_get_int (buf_p, &rc);	/* repid */
      bound_bits_flag = repr_id_bits & OR_BOUND_BIT_FLAG;
      or_advance (buf_p, OR_INT_SIZE);	/* chn */
    }

  if (bound_bits_flag)
    {
      size = OR_BOUND_BIT_BYTES (n_fixed);
      bound_bits = (char *) malloc (size);
      if (bound_bits == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, size);
	  goto error;
	}
      memset (bound_bits, 0, size);
    }

  /* get the offsets relative to the end of the header (beginning
   * of variable table)
   */
  vars = or_get_var_table (buf_p, n_variable, catcls_unpack_allocator);
  if (vars == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1,
	      n_variable * sizeof (OR_VARINFO));
      goto error;
    }

  /* fixed */
  start_p = buf_p->ptr;

  /* read bound bits before accessing fixed attributes */
  buf_p->ptr += repr_p->fixed_length;
  if (bound_bits_flag)
    {
      or_get_data (buf_p, bound_bits, OR_BOUND_BIT_BYTES (n_fixed));
    }

  buf_p->ptr = start_p;
  for (i = 0; i < n_fixed; i++)
    {
      data_type = fixed_p[i].type;

      if (bound_bits_flag && OR_GET_BOUND_BIT (bound_bits, i))
	{
	  (*((*tp_Type_id_map[data_type]).data_readval)) (buf_p,
							  &attrs[i].value,
							  NULL, -1, true,
							  NULL, 0);
	}
      else
	{
	  db_value_put_null (&attrs[i].value);
	  or_advance (buf_p, (*tp_Type_id_map[data_type]).disksize);
	}
    }

  pad = (int) (buf_p->ptr - start_p);
  if (pad < repr_p->fixed_length)
    {
      or_advance (buf_p, repr_p->fixed_length - pad);
    }
  else if (pad > repr_p->fixed_length)
    {
      error = ER_SM_CORRUPTED;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 0);
      goto error;
    }

  /* bound bits */
  if (bound_bits_flag)
    {
      or_advance (buf_p, OR_BOUND_BIT_BYTES (n_fixed));
    }

  /* variable */
  var_attrs = &attrs[n_fixed];
  for (i = 0; i < n_variable; i++)
    {
      data_type = variable_p[i].type;
      (*((*tp_Type_id_map[data_type]).data_readval)) (buf_p,
						      &var_attrs[i].value,
						      NULL, vars[i].length,
						      true, NULL, 0);
      catcls_expand_or_value_by_subset (thread_p, &var_attrs[i]);
    }

  if (vars)
    {
      free_and_init (vars);
    }

  if (bound_bits)
    {
      free_and_init (bound_bits);
    }

  return NO_ERROR;

error:

  if (vars)
    {
      free_and_init (vars);
    }

  if (bound_bits)
    {
      free_and_init (bound_bits);
    }

  return error;
}

/*
 * catcls_put_or_value_into_record () -
 *   return:
 *   value(in):
 *   chn(in):
 *   record(in):
 *   class_oid(in):
 */
static int
catcls_put_or_value_into_record (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
				 int chn, RECDES * record_p,
				 OID * class_oid_p)
{
  OR_BUF *buf_p, repr_buffer;
  DISK_REPR *repr_p = NULL;
  REPR_ID repr_id;
  int error = NO_ERROR;

  error = catalog_get_last_representation_id (thread_p, class_oid_p,
					      &repr_id);
  if (error != NO_ERROR)
    {
      return error;
    }
  else
    {
      repr_p = catalog_get_representation (thread_p, class_oid_p, repr_id);
      if (repr_p == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  return error;
	}
    }

  buf_p = &repr_buffer;
  or_init (buf_p, record_p->data, record_p->length);

  error = catcls_put_or_value_into_buffer (value_p, chn, buf_p,
					   class_oid_p, repr_p);
  if (error != NO_ERROR)
    {
      catalog_free_representation (repr_p);
      return error;
    }

  record_p->length = (int) (buf_p->ptr - buf_p->buffer);
  catalog_free_representation (repr_p);

  return NO_ERROR;
}

/*
 * catcls_get_or_value_from_class_record () -
 *   return:
 *   record(in):
 */
static OR_VALUE *
catcls_get_or_value_from_class_record (THREAD_ENTRY * thread_p,
				       RECDES * record_p)
{
  OR_VALUE *value_p = NULL;
  OR_BUF *buf_p, repr_buffer;

  value_p = catcls_allocate_or_value (1);
  if (value_p == NULL)
    {
      return NULL;
    }

  assert (OR_GET_OFFSET_SIZE (record_p->data) == BIG_VAR_OFFSET_SIZE);

  buf_p = &repr_buffer;
  or_init (buf_p, record_p->data, record_p->length);

  /* class record header does not contain MVCC info */
  or_advance (buf_p, OR_NON_MVCC_HEADER_SIZE);
  if (catcls_get_or_value_from_class (thread_p, buf_p, value_p) != NO_ERROR)
    {
      catcls_free_or_value (value_p);
      return NULL;
    }

  return value_p;
}

/*
 * catcls_get_or_value_from_record () -
 *   return:
 *   record(in):
 *   class_oid(in):
 */
static OR_VALUE *
catcls_get_or_value_from_record (THREAD_ENTRY * thread_p, RECDES * record_p,
				 OID * class_oid_p)
{
  OR_VALUE *value_p = NULL;
  OR_BUF *buf_p, repr_buffer;
  REPR_ID repr_id;
  DISK_REPR *repr_p = NULL;
  int error;

  error =
    catalog_get_last_representation_id (thread_p, class_oid_p, &repr_id);
  if (error != NO_ERROR)
    {
      goto error;
    }

  repr_p = catalog_get_representation (thread_p, class_oid_p, repr_id);
  if (repr_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  value_p = catcls_allocate_or_value (1);
  if (value_p == NULL)
    {
      goto error;
    }

  if (catcls_expand_or_value_by_repr (value_p, class_oid_p, repr_p) !=
      NO_ERROR)
    {
      goto error;
    }

  buf_p = &repr_buffer;
  or_init (buf_p, record_p->data, record_p->length);

  if (catcls_get_or_value_from_buffer (thread_p, buf_p, value_p, repr_p) !=
      NO_ERROR)
    {
      goto error;
    }

  catalog_free_representation (repr_p);
  return value_p;

error:

  if (value_p)
    {
      catcls_free_or_value (value_p);
    }

  if (repr_p)
    {
      catalog_free_representation (repr_p);
    }

  return NULL;
}

/*
 * catcls_insert_subset () -
 *   return:
 *   value(in):
 *   root_oid(in):
 */
static int
catcls_insert_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
		      OID * root_oid_p)
{
  OR_VALUE *subset_p;
  int n_subset;
  OID *class_oid_p, oid;
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p;
  DB_SET *oid_set_p = NULL;
  DB_VALUE oid_val;
  int i;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;
  int error = NO_ERROR;

  subset_p = value_p->sub.value;
  n_subset = value_p->sub.count;

  if (n_subset == 0)
    {
      return NO_ERROR;
    }

  oid_set_p = set_create_sequence (n_subset);
  if (oid_set_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  class_oid_p = &subset_p[0].id.classoid;
  cls_info_p = catalog_get_class_info (thread_p, class_oid_p);
  if (cls_info_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  /* need to update assigned oid of each instance */
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, class_oid_p,
				   MULTI_ROW_UPDATE, NULL) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  is_scan_inited = true;

  for (i = 0; i < n_subset; i++)
    {
      error = catcls_insert_instance (thread_p, &subset_p[i], &oid,
				      root_oid_p, class_oid_p, hfid_p, &scan);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      db_push_oid (&oid_val, &oid);
      error = set_put_element (oid_set_p, i, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  db_make_sequence (&value_p->value, oid_set_p);

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);

  return NO_ERROR;

error:

  if (oid_set_p)
    {
      set_free (oid_set_p);
    }

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return error;
}

/*
 * catcls_delete_subset () -
 *   return:
 *   value(in):
 */
static int
catcls_delete_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p)
{
  OR_VALUE *subset_p;
  int n_subset;
  OID *class_oid_p, *oid_p;
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p;
  DB_SET *oid_set_p;
  DB_VALUE oid_val;
  int i;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;
  int error = NO_ERROR;

  subset_p = value_p->sub.value;
  n_subset = value_p->sub.count;

  if (n_subset == 0)
    {
      return NO_ERROR;
    }

  oid_set_p = DB_GET_SET (&value_p->value);
  class_oid_p = &subset_p[0].id.classoid;

  cls_info_p = catalog_get_class_info (thread_p, class_oid_p);
  if (cls_info_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, class_oid_p,
				   MULTI_ROW_DELETE, NULL) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  is_scan_inited = true;

  for (i = 0; i < n_subset; i++)
    {
      error = set_get_element (oid_set_p, i, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      oid_p = DB_GET_OID (&oid_val);
      error =
	catcls_delete_instance (thread_p, oid_p, class_oid_p, hfid_p, &scan);
      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);

  return NO_ERROR;

error:

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return error;
}

/*
 * catcls_update_subset () -
 *   return:
 *   value(in):
 *   old_value(in):
 *   uflag(in):
 */
static int
catcls_update_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
		      OR_VALUE * old_value_p, int *update_flag_p)
{
  OR_VALUE *subset_p, *old_subset_p;
  int n_subset, n_old_subset;
  int n_min_subset;
  OID *class_oid_p;
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p;
  OID *oid_p, tmp_oid;
  DB_SET *old_oid_set_p = NULL;
  DB_VALUE oid_val;
  int i;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;
  int error = NO_ERROR;

  if ((value_p->sub.count > 0) &&
      ((old_value_p->sub.count < 0) || DB_IS_NULL (&old_value_p->value)))
    {
      old_oid_set_p = set_create_sequence (0);
      db_make_sequence (&old_value_p->value, old_oid_set_p);
      old_value_p->sub.count = 0;
    }

  subset_p = value_p->sub.value;
  n_subset = value_p->sub.count;

  old_subset_p = old_value_p->sub.value;
  n_old_subset = old_value_p->sub.count;

  if (subset_p != NULL)
    {
      class_oid_p = &subset_p[0].id.classoid;
    }
  else if (old_subset_p != NULL)
    {
      class_oid_p = &old_subset_p[0].id.classoid;
    }
  else
    {
      return NO_ERROR;
    }

  old_oid_set_p = DB_PULL_SET (&old_value_p->value);
  cls_info_p = catalog_get_class_info (thread_p, class_oid_p);
  if (cls_info_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, class_oid_p,
				   MULTI_ROW_UPDATE, NULL) != NO_ERROR)
    {
      goto error;
    }

  is_scan_inited = true;

  n_min_subset = (n_subset > n_old_subset) ? n_old_subset : n_subset;
  /* update components */
  for (i = 0; i < n_min_subset; i++)
    {
      error = set_get_element (old_oid_set_p, i, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      if (DB_VALUE_TYPE (&oid_val) != DB_TYPE_OID)
	{
	  goto error;
	}

      oid_p = DB_PULL_OID (&oid_val);
      error = catcls_update_instance (thread_p, &subset_p[i], oid_p,
				      class_oid_p, hfid_p, &scan);
      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  /* drop components */
  if (n_old_subset > n_subset)
    {
      for (i = n_old_subset - 1; i >= n_min_subset; i--)
	{
	  error = set_get_element (old_oid_set_p, i, &oid_val);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  if (DB_VALUE_TYPE (&oid_val) != DB_TYPE_OID)
	    {
	      goto error;
	    }

	  oid_p = DB_PULL_OID (&oid_val);
	  error = catcls_delete_instance (thread_p, oid_p, class_oid_p,
					  hfid_p, &scan);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  error = set_drop_seq_element (old_oid_set_p, i);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}

      if (set_size (old_oid_set_p) == 0)
	{
	  pr_clear_value (&old_value_p->value);
	}

      *update_flag_p = true;
    }
  /* add components */
  else if (n_old_subset < n_subset)
    {
      OID root_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
      for (i = n_min_subset, oid_p = &tmp_oid; i < n_subset; i++)
	{
	  error = catcls_insert_instance (thread_p, &subset_p[i], oid_p,
					  &root_oid, class_oid_p, hfid_p,
					  &scan);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  db_push_oid (&oid_val, oid_p);
	  error = set_add_element (old_oid_set_p, &oid_val);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
      *update_flag_p = true;
    }

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);

  return NO_ERROR;

error:

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return error;
}

/*
 * catcls_insert_instance () -
 *   return:
 *   value(in):
 *   oid(in):
 *   root_oid(in):
 *   class_oid(in):
 *   hfid(in):
 *   scan(in):
 */
static int
catcls_insert_instance (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
			OID * oid_p, OID * root_oid_p, OID * class_oid_p,
			HFID * hfid_p, HEAP_SCANCACHE * scan_p)
{
  RECDES record;
  OR_VALUE *attrs;
  OR_VALUE *subset_p, *attr_p;
  bool old;
  int i, j, k;
  bool is_lock_inited = false;
  int error = NO_ERROR;

  record.data = NULL;

  if (heap_assign_address_with_class_oid (thread_p, hfid_p, class_oid_p,
					  oid_p, 0) != NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  is_lock_inited = true;

  if (OID_ISNULL (root_oid_p))
    {
      COPY_OID (root_oid_p, oid_p);
    }

  for (attrs = value_p->sub.value, i = 0; i < value_p->sub.count; i++)
    {
      if (IS_SUBSET (attrs[i]))
	{
	  /* set backward oid */
	  for (subset_p = attrs[i].sub.value,
	       j = 0; j < attrs[i].sub.count; j++)
	    {
	      /* assume that the attribute values of xxx are ordered by
	         { class_of, xxx_name, xxx_type, from_xxx_name, ... } */

	      attr_p = subset_p[j].sub.value;
	      db_push_oid (&attr_p[0].value, oid_p);

	      if (OID_EQ (class_oid_p, &ct_Class.classoid))
		{
		  /* if root node, eliminate self references */
		  for (k = 1; k < subset_p[j].sub.count; k++)
		    {
		      if (DB_VALUE_TYPE (&attr_p[k].value) == DB_TYPE_OID)
			{
			  if (OID_EQ (oid_p, DB_PULL_OID (&attr_p[k].value)))
			    {
			      db_value_put_null (&attr_p[k].value);
			    }
			}
		    }
		}
	    }

	  error = catcls_insert_subset (thread_p, &attrs[i], root_oid_p);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else if (DB_VALUE_DOMAIN_TYPE (&attrs[i].value) == DB_TYPE_VARIABLE)
	{
	  /* set a self referenced oid */
	  db_push_oid (&attrs[i].value, root_oid_p);
	}
    }

  error = catcls_reorder_attributes_by_repr (thread_p, value_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  record.length = catcls_guess_record_length (value_p);
  record.area_size = record.length;
  record.type = REC_HOME;
  record.data = (char *) malloc (record.length);

  if (record.data == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, record.length);
      goto error;
    }

  error = catcls_put_or_value_into_record (thread_p, value_p, 0, &record,
					   class_oid_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* for replication */
  if (locator_add_or_remove_index (thread_p, &record, oid_p, class_oid_p,
				   NULL, false, true, SINGLE_ROW_INSERT,
				   scan_p, false, false, hfid_p, NULL) !=
      NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  if (heap_update (thread_p, hfid_p, class_oid_p, oid_p, &record, NULL, &old,
		   scan_p, HEAP_UPDATE_IN_PLACE) == NULL)
    {
      error = er_errid ();
      assert (error != NO_ERROR);
      goto error;
    }

#if defined(SERVER_MODE)
  lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
#endif /* SERVER_MODE */
  free_and_init (record.data);

  return NO_ERROR;

error:

#if defined(SERVER_MODE)
  if (is_lock_inited)
    {
      lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
    }
#endif /* SERVER_MODE */

  if (record.data)
    {
      free_and_init (record.data);
    }

  return error;
}

/*
 * catcls_delete_instance () -
 *   return:
 *   oid(in):
 *   class_oid(in):
 *   hfid(in):
 *   scan(in):
 */
static int
catcls_delete_instance (THREAD_ENTRY * thread_p, OID * oid_p,
			OID * class_oid_p, HFID * hfid_p,
			HEAP_SCANCACHE * scan_p)
{
  RECDES record;
  OR_VALUE *value_p = NULL;
  OR_VALUE *attrs;
  int i;
#if defined(SERVER_MODE)
  bool is_lock_inited = false;
#endif /* SERVER_MODE */
  int error = NO_ERROR;

  assert (oid_p != NULL && class_oid_p != NULL && hfid_p != NULL
	  && scan_p != NULL);
  record.data = NULL;

#if defined(SERVER_MODE)
  if (lock_object (thread_p, oid_p, class_oid_p, X_LOCK, LK_UNCOND_LOCK) !=
      LK_GRANTED)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }
  is_lock_inited = true;
#endif /* SERVER_MODE */

  if (heap_get (thread_p, oid_p, &record, scan_p, COPY, NULL_CHN) !=
      S_SUCCESS)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  value_p = catcls_get_or_value_from_record (thread_p, &record, class_oid_p);
  if (value_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  for (attrs = value_p->sub.value, i = 0; i < value_p->sub.count; i++)
    {
      if (IS_SUBSET (attrs[i]))
	{
	  error = catcls_delete_subset (thread_p, &attrs[i]);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  /* for replication */
  if (locator_add_or_remove_index (thread_p, &record, oid_p, class_oid_p,
				   NULL, false, false, SINGLE_ROW_DELETE,
				   scan_p, false, false, hfid_p, NULL) !=
      NO_ERROR)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  if (heap_delete (thread_p, hfid_p, class_oid_p, oid_p, scan_p) == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

#if defined(SERVER_MODE)
  lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
#endif /* SERVER_MODE */
  catcls_free_or_value (value_p);

  return NO_ERROR;

error:

#if defined(SERVER_MODE)
  if (is_lock_inited)
    {
      lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
    }
#endif /* SERVER_MODE */

  if (value_p)
    {
      catcls_free_or_value (value_p);
    }

  return error;
}

/*
 * catcls_update_instance () -
 *   return:
 *   value(in):
 *   oid(in):
 *   class_oid(in):
 *   hfid(in):
 *   scan(in):
 */
static int
catcls_update_instance (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
			OID * oid_p, OID * class_oid_p, HFID * hfid_p,
			HEAP_SCANCACHE * scan_p)
{
  RECDES record, old_record;
  OR_VALUE *old_value_p = NULL;
  OR_VALUE *attrs, *old_attrs;
  OR_VALUE *subset_p, *attr_p;
  int old_chn;
  int uflag = false;
  bool old;
  int i, j, k;
#if defined(SERVER_MODE)
  bool is_lock_inited = false;
#endif /* SERVER_MODE */
  int error = NO_ERROR;

  record.data = NULL;
  old_record.data = NULL;
#if defined(SERVER_MODE)
  /* in MVCC the lock has been already acquired */
  if (mvcc_Enabled == false)
    {
      if (lock_object (thread_p, oid_p, class_oid_p, X_LOCK, LK_UNCOND_LOCK)
	  != LK_GRANTED)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error;
	}
      is_lock_inited = true;
    }
#endif /* SERVER_MODE */

  if (heap_get (thread_p, oid_p, &old_record, scan_p, COPY, NULL_CHN) !=
      S_SUCCESS)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  old_chn = or_chn (&old_record);
  old_value_p =
    catcls_get_or_value_from_record (thread_p, &old_record, class_oid_p);
  if (old_value_p == NULL)
    {
      assert (er_errid () != NO_ERROR);
      error = er_errid ();
      goto error;
    }

  error = catcls_reorder_attributes_by_repr (thread_p, value_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* update old_value */
  for (attrs = value_p->sub.value, old_attrs = old_value_p->sub.value,
       i = 0; i < value_p->sub.count; i++)
    {
      if (IS_SUBSET (attrs[i]))
	{
	  /* set backward oid */
	  for (subset_p = attrs[i].sub.value,
	       j = 0; j < attrs[i].sub.count; j++)
	    {
	      /* assume that the attribute values of xxx are ordered by
	         { class_of, xxx_name, xxx_type, from_xxx_name, ... } */
	      attr_p = subset_p[j].sub.value;
	      db_push_oid (&attr_p[0].value, oid_p);

	      if (OID_EQ (class_oid_p, &ct_Class.classoid))
		{
		  /* if root node, eliminate self references */
		  for (k = 1; k < subset_p[j].sub.count; k++)
		    {
		      if (DB_VALUE_TYPE (&attr_p[k].value) == DB_TYPE_OID)
			{
			  if (OID_EQ (oid_p, DB_PULL_OID (&attr_p[k].value)))
			    {
			      db_value_put_null (&attr_p[k].value);
			    }
			}
		    }
		}
	    }

	  error = catcls_update_subset (thread_p, &attrs[i], &old_attrs[i],
					&uflag);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else
	{
	  if (tp_value_compare (&old_attrs[i].value, &attrs[i].value, 1, 1) !=
	      DB_EQ)
	    {
	      pr_clear_value (&old_attrs[i].value);
	      pr_clone_value (&attrs[i].value, &old_attrs[i].value);
	      uflag = true;
	    }
	}
    }

  if (uflag == true)
    {
      record.length = catcls_guess_record_length (old_value_p);
      record.area_size = record.length;
      record.type = REC_HOME;
      record.data = (char *) malloc (record.length);

      if (record.data == NULL)
	{
	  error = ER_OUT_OF_VIRTUAL_MEMORY;
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, record.length);
	  goto error;
	}

      error = catcls_put_or_value_into_record (thread_p, old_value_p,
					       old_chn + 1, &record,
					       class_oid_p);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      /* give up setting updated attr info */
      if (locator_update_index (thread_p, &record, &old_record, NULL, 0,
				oid_p, oid_p, class_oid_p, NULL, false,
				SINGLE_ROW_UPDATE, scan_p, false,
				REPL_INFO_TYPE_STMT_NORMAL) != NO_ERROR)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error;
	}

      /* update in place */
      if (heap_update (thread_p, hfid_p, class_oid_p, oid_p, &record, NULL,
		       &old, scan_p, HEAP_UPDATE_IN_PLACE) == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error = er_errid ();
	  goto error;
	}

      free_and_init (record.data);
    }

#if defined(SERVER_MODE)
  lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
#endif /* SERVER_MODE */
  catcls_free_or_value (old_value_p);

  return NO_ERROR;

error:

#if defined(SERVER_MODE)
  if (is_lock_inited)
    {
      lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
    }
#endif /* SERVER_MODE */

  if (record.data)
    {
      free_and_init (record.data);
    }

  if (old_value_p)
    {
      catcls_free_or_value (old_value_p);
    }

  return error;
}

/*
 * catcls_insert_catalog_classes () -
 *   return:
 *   record(in):
 */
int
catcls_insert_catalog_classes (THREAD_ENTRY * thread_p, RECDES * record_p)
{
  OR_VALUE *value_p = NULL;
  OID oid, *class_oid_p;
  OID root_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;

  value_p = catcls_get_or_value_from_class_record (thread_p, record_p);
  if (value_p == NULL)
    {
      goto error;
    }

  class_oid_p = &ct_Class.classoid;
  cls_info_p = catalog_get_class_info (thread_p, class_oid_p);
  if (cls_info_p == NULL)
    {
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, class_oid_p,
				   SINGLE_ROW_UPDATE, NULL) != NO_ERROR)
    {
      goto error;
    }

  is_scan_inited = true;

  if (catcls_insert_instance (thread_p, value_p, &oid, &root_oid, class_oid_p,
			      hfid_p, &scan) != NO_ERROR)
    {
      goto error;
    }

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);
  catcls_free_or_value (value_p);

  return NO_ERROR;

error:

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  if (value_p)
    {
      catcls_free_or_value (value_p);
    }

  return ER_FAILED;
}

/*
 * catcls_delete_catalog_classes () -
 *   return:
 *   name(in):
 *   class_oid(in):
 */
int
catcls_delete_catalog_classes (THREAD_ENTRY * thread_p, const char *name_p,
			       OID * class_oid_p)
{
  OID oid, *ct_class_oid_p;
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;

  if (catcls_find_oid_by_class_name (thread_p, name_p, &oid) != NO_ERROR)
    {
      goto error;
    }

  if (OID_ISNULL (&oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME,
	      1, name_p);
      goto error;
    }

  ct_class_oid_p = &ct_Class.classoid;
  cls_info_p = catalog_get_class_info (thread_p, ct_class_oid_p);
  if (cls_info_p == NULL)
    {
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  /* in MVCC, do not physically remove the row */
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p,
				   ct_class_oid_p,
				   SINGLE_ROW_DELETE, NULL) != NO_ERROR)
    {
      goto error;
    }

  is_scan_inited = true;

  if (catcls_delete_instance (thread_p, &oid, ct_class_oid_p, hfid_p, &scan)
      != NO_ERROR)
    {
      goto error;
    }

  if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) != NO_ERROR)
    {
      goto error;
    }

  if (catcls_remove_entry (class_oid_p) != NO_ERROR)
    {
      csect_exit (thread_p, CSECT_CT_OID_TABLE);
      goto error;
    }

  csect_exit (thread_p, CSECT_CT_OID_TABLE);

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);

  return NO_ERROR;

error:

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return ER_FAILED;
}

/*
 * catcls_is_mvcc_update_needed () - check whether mvcc update is needed
 *   return: error code
 *   thread_p(in): thread entry
 *   oid(in): OID
 *   need_mvcc_update(in/out): true, if mvcc update is needed
 */
int
catcls_is_mvcc_update_needed (THREAD_ENTRY * thread_p, OID * oid,
			      bool * need_mvcc_update)
{
  PAGE_PTR pgptr = NULL, forward_pgptr = NULL;
  bool ignore_record = false;
  MVCC_REC_HEADER mvcc_rec_header;
  int error = NO_ERROR;

  assert (oid != NULL && need_mvcc_update != NULL);
  if (heap_get_pages_for_mvcc_chain_read (thread_p, oid, &pgptr,
					  &forward_pgptr, NULL,
					  &ignore_record) != NO_ERROR)
    {
      goto error;
    }

  if (ignore_record)
    {
      /* Is this possible? */
      assert (0);
      goto error;
    }

  if (heap_get_mvcc_data (thread_p, oid, &mvcc_rec_header, pgptr,
			  forward_pgptr, NULL) != S_SUCCESS)
    {
      goto error;
    }

  if (mvcc_rec_header.mvcc_ins_id == logtb_get_current_mvccid (thread_p))
    {
      /* The record is inserted by current transaction, update in-place
       * can be used instead of duplicating record
       */
      *need_mvcc_update = false;
    }
  else
    {
      /* MVCC update */
      *need_mvcc_update = true;
    }

  if (pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, pgptr);
    }

  if (forward_pgptr != NULL)
    {
      pgbuf_unfix_and_init (thread_p, forward_pgptr);
    }

  return NO_ERROR;

error:
  if (pgptr != NULL)
    {
      pgbuf_unfix (thread_p, pgptr);
    }

  if (forward_pgptr != NULL)
    {
      pgbuf_unfix (thread_p, forward_pgptr);
    }

  error = er_errid ();
  return ((error == NO_ERROR) ? ER_FAILED : error);
}

/*
 * catcls_update_catalog_classes () -
 *   return:
 *   name(in):
 *   record(in):
 *   class_oid_p(in): class OID
 *   force_in_place(in): in MVCC the update of the instances will be made in
 *			 place. Otherwise the decision will be made in this
 *			 function. Doesn't matter in non-MVCC.
 */
int
catcls_update_catalog_classes (THREAD_ENTRY * thread_p, const char *name_p,
			       RECDES * record_p, OID * class_oid_p,
			       bool force_in_place)
{
  OR_VALUE *value_p = NULL;
  OID oid, *catalog_class_oid_p;
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;
  bool need_mvcc_update;

  if (catcls_find_oid_by_class_name (thread_p, name_p, &oid) != NO_ERROR)
    {
      goto error;
    }

  if (OID_ISNULL (&oid))
    {
      return (catcls_insert_catalog_classes (thread_p, record_p));
    }

  /* check whether mvcc update or update in place is needed */
  if (force_in_place)
    {
      need_mvcc_update = false;
    }
  else
    {
#if !defined (SERVER_MODE)
      need_mvcc_update = false;
#else /* SERVER_MODE */
      if (catcls_is_mvcc_update_needed (thread_p, &oid, &need_mvcc_update)
	  != NO_ERROR)
	{
	  goto error;
	}
#endif /* SERVER_MODE */
    }

  value_p = catcls_get_or_value_from_class_record (thread_p, record_p);
  if (value_p == NULL)
    {
      goto error;
    }

  catalog_class_oid_p = &ct_Class.classoid;
  cls_info_p = catalog_get_class_info (thread_p, catalog_class_oid_p);
  if (cls_info_p == NULL)
    {
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p,
				   catalog_class_oid_p,
				   SINGLE_ROW_UPDATE, NULL) != NO_ERROR)
    {
      goto error;
    }

  is_scan_inited = true;

  /* update catalog classes in MVCC */

  /* The oid is visible for current transaction, so it can't be
   * removed by vacuum. More, can't be other transaction that
   * concurrently update oid - can't be two concurrent transactions that 
   * alter the same table.
   */
  if (need_mvcc_update == false)
    {
      /* already inserted by the current transaction, need to replace
       * the old version
       */
      if (catcls_update_instance (thread_p, value_p, &oid,
				  catalog_class_oid_p, hfid_p,
				  &scan) != NO_ERROR)
	{
	  goto error;
	}
    }
  else
    {
      /* not inserted by the current transaction - update to new version */
      OID root_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
      OID new_oid;
      if (catcls_mvcc_update_instance (thread_p, value_p, &oid, &new_oid,
				       &root_oid, catalog_class_oid_p, hfid_p,
				       &scan) != NO_ERROR)
	{
	  goto error;
	}
    }

  if (need_mvcc_update == true)
    {
      /* since new OID version will be created, remove the old version */
      if (csect_enter (thread_p, CSECT_CT_OID_TABLE, INF_WAIT) != NO_ERROR)
	{
	  goto error;
	}

      if (catcls_remove_entry (class_oid_p) != NO_ERROR)
	{
	  csect_exit (thread_p, CSECT_CT_OID_TABLE);
	  goto error;
	}

      csect_exit (thread_p, CSECT_CT_OID_TABLE);
    }

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);
  catcls_free_or_value (value_p);

  return NO_ERROR;

error:

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  if (value_p)
    {
      catcls_free_or_value (value_p);
    }

  return ER_FAILED;
}

/*
 * catcls_compile_catalog_classes () -
 *   return:
 *   void(in):
 */
int
catcls_compile_catalog_classes (THREAD_ENTRY * thread_p)
{
  RECDES class_record;
  OID *class_oid_p, tmp_oid;
  const char *class_name_p;
  const char *attr_name_p;
  CT_ATTR *atts;
  int n_atts;
  int c, a, i;
  HEAP_SCANCACHE scan;

  /* check if an old version database */
  if (catcls_find_class_oid_by_class_name (thread_p, CT_CLASS_NAME, &tmp_oid)
      != NO_ERROR)
    {
      return ER_FAILED;
    }
  else if (OID_ISNULL (&tmp_oid))
    {
      /* no catalog classes */
      return NO_ERROR;
    }

  /* fill classoid and attribute ids for each meta catalog classes */
  for (c = 0; ct_Classes[c] != NULL; c++)
    {
      class_name_p = ct_Classes[c]->name;
      class_oid_p = &ct_Classes[c]->classoid;

      if (catcls_find_class_oid_by_class_name (thread_p, class_name_p,
					       class_oid_p) != NO_ERROR)
	{
	  return ER_FAILED;
	}

      atts = ct_Classes[c]->atts;
      n_atts = ct_Classes[c]->n_atts;

      if (heap_scancache_quick_start (&scan) != NO_ERROR)
	{
	  return ER_FAILED;
	}
      if (heap_get (thread_p, class_oid_p, &class_record, &scan, PEEK,
		    NULL_CHN) != S_SUCCESS)
	{
	  (void) heap_scancache_end (thread_p, &scan);
	  return ER_FAILED;
	}

      for (i = 0; i < n_atts; i++)
	{
	  attr_name_p = or_get_attrname (&class_record, i);
	  if (attr_name_p == NULL)
	    {
	      (void) heap_scancache_end (thread_p, &scan);
	      return ER_FAILED;
	    }

	  for (a = 0; a < n_atts; a++)
	    {
	      if (strcmp (atts[a].name, attr_name_p) == 0)
		{
		  atts[a].id = i;
		  break;
		}
	    }
	}
      if (heap_scancache_end (thread_p, &scan) != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  catcls_Enable = true;

  if (catcls_find_btid_of_class_name (thread_p, &catcls_Btid) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (catcls_initialize_class_oid_to_oid_hash_table (CATCLS_OID_TABLE_SIZE) !=
      NO_ERROR)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

/*
 * catcls_get_server_lang_charset () - get the language id and the charset id
 *				       stored in the "db_root" system table
 *   return: NO_ERROR, or error code
 *   thread_p(in)  : thread context
 *   charset_id_p(out):
 *   lang_buf(in/out): buffer language string
 *   lang_buf_size(in): size of buffer language string
 *
 *  Note : This function is called during server initialization, for this
 *	   reason, no locks are required on the class.
 */
int
catcls_get_server_lang_charset (THREAD_ENTRY * thread_p, int *charset_id_p,
				char *lang_buf, const int lang_buf_size)
{
  OID class_oid;
  OID inst_oid;
  HFID hfid;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_SCANCACHE scan_cache;
  RECDES recdes;
  const char *class_name = "db_root";
  int charset_att_id = -1, lang_att_id = -1;
  int i;
  int error = NO_ERROR;
  bool scan_cache_inited = false;
  bool attr_info_inited = false;

  assert (charset_id_p != NULL);
  assert (lang_buf != NULL);

  OID_SET_NULL (&class_oid);
  OID_SET_NULL (&inst_oid);

  error = catcls_find_class_oid_by_class_name (thread_p, class_name,
					       &class_oid);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (OID_ISNULL (&class_oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME,
	      1, class_name);
      error = ER_LC_UNKNOWN_CLASSNAME;
      goto exit;
    }

  error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  attr_info_inited = true;

  (void) heap_scancache_quick_start (&scan_cache);
  scan_cache_inited = true;

  if (heap_get (thread_p, &class_oid, &recdes, &scan_cache, PEEK,
		NULL_CHN) != S_SUCCESS)
    {
      error = ER_FAILED;
      goto exit;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      const char *rec_attr_name_p = or_get_attrname (&recdes, i);
      if (rec_attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      if (strcmp ("charset", rec_attr_name_p) == 0)
	{
	  charset_att_id = i;
	  if (lang_att_id != -1)
	    {
	      break;
	    }
	}

      if (strcmp ("lang", rec_attr_name_p) == 0)
	{
	  lang_att_id = i;
	  if (charset_att_id != -1)
	    {
	      break;
	    }
	}
    }

  if (charset_att_id == -1 || lang_att_id == -1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_FAILED;
      goto exit;
    }

  (void) heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  /* read values of the single record in heap */
  error = heap_get_hfid_from_class_oid (thread_p, &class_oid, &hfid);
  if (error != NO_ERROR || HFID_IS_NULL (&hfid))
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_scancache_start (thread_p, &scan_cache, &hfid, NULL, true,
				false, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  scan_cache_inited = true;

  while (heap_next (thread_p, &hfid, NULL, &inst_oid, &recdes,
		    &scan_cache, PEEK) == S_SUCCESS)
    {
      HEAP_ATTRVALUE *heap_value = NULL;

      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes,
				       NULL, &attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      for (i = 0, heap_value = attr_info.values;
	   i < attr_info.num_values; i++, heap_value++)
	{
	  if (heap_value->attrid == charset_att_id)
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (&(heap_value->dbvalue))
		      == DB_TYPE_INTEGER);

	      *charset_id_p = DB_GET_INTEGER (&heap_value->dbvalue);
	    }
	  else if (heap_value->attrid == lang_att_id)
	    {
	      char *lang_str = NULL;
	      int lang_str_len;

	      assert (DB_VALUE_DOMAIN_TYPE (&(heap_value->dbvalue))
		      == DB_TYPE_STRING);

	      lang_str = DB_GET_STRING (&heap_value->dbvalue);
	      lang_str_len = (lang_str != NULL) ? strlen (lang_str) : 0;

	      assert (lang_str_len < lang_buf_size);
	      strncpy (lang_buf, lang_str, MIN (lang_str_len, lang_buf_size));
	      lang_buf[MIN (lang_str_len, lang_buf_size)] = '\0';
	    }
	}
    }

exit:
  if (scan_cache_inited == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      scan_cache_inited = false;
    }
  if (attr_info_inited == true)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      attr_info_inited = false;
    }

  return error;
}

/*
 * catcls_mvcc_update_subset () - MVCC update catalog class subset
 *   return:
 *   thread_p(in): thred entry
 *   value(in): new values
 *   old_value_p(in): old values 
 */
static int
catcls_mvcc_update_subset (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
			   OR_VALUE * old_value_p, OID * root_oid)
{
  OR_VALUE *subset_p = NULL, *old_subset_p = NULL;
  DB_SET *oid_set_p = NULL;
  int n_subset, n_old_subset;
  int n_min_subset;
  OID *class_oid_p = NULL;
  CLS_INFO *cls_info_p = NULL;
  HFID *hfid_p = NULL;
  OID *oid_p = NULL, tmp_oid, new_oid;
  DB_SET *old_oid_set_p = NULL;
  DB_VALUE oid_val;
  int i;
  HEAP_SCANCACHE scan;
  bool is_scan_inited = false;
  int error = NO_ERROR;

  old_subset_p = old_value_p->sub.value;
  n_old_subset = old_value_p->sub.count;
  if (n_old_subset < 0)
    {
      n_old_subset = 0;
    }

  subset_p = value_p->sub.value;
  n_subset = value_p->sub.count;
  if (n_subset < 0)
    {
      n_subset = 0;
    }

  if (subset_p != NULL)
    {
      class_oid_p = &subset_p[0].id.classoid;
    }
  else if (old_subset_p != NULL)
    {
      class_oid_p = &old_subset_p[0].id.classoid;
    }
  else
    {
      return NO_ERROR;
    }

  if (n_subset > 0)
    {
      /* get the OIDs set */
      if (DB_IS_NULL (&value_p->value))
	{
	  oid_set_p = set_create_sequence (n_subset);
	  if (oid_set_p == NULL)
	    {
	      error = er_errid ();
	      goto error;
	    }
	}
      else
	{
	  oid_set_p = DB_PULL_SET (&value_p->value);
	}
    }

  if (n_old_subset > 0)
    {
      old_oid_set_p = DB_PULL_SET (&old_value_p->value);
    }

  cls_info_p = catalog_get_class_info (thread_p, class_oid_p);
  if (cls_info_p == NULL)
    {
      error = er_errid ();
      goto error;
    }

  hfid_p = &cls_info_p->hfid;
  if (heap_scancache_start_modify (thread_p, &scan, hfid_p, class_oid_p,
				   MULTI_ROW_UPDATE, NULL) != NO_ERROR)
    {
      goto error;
    }

  is_scan_inited = true;

  n_min_subset = (n_subset > n_old_subset) ? n_old_subset : n_subset;
  /* update components */
  for (i = 0; i < n_min_subset; i++)
    {
      error = set_get_element (old_oid_set_p, i, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      if (DB_VALUE_TYPE (&oid_val) != DB_TYPE_OID)
	{
	  goto error;
	}

      oid_p = DB_PULL_OID (&oid_val);
      error =
	catcls_mvcc_update_instance (thread_p, &subset_p[i], oid_p, &new_oid,
				     root_oid, class_oid_p, hfid_p, &scan);
      if (error != NO_ERROR)
	{
	  goto error;
	}

      db_push_oid (&oid_val, &new_oid);
      error = set_put_element (oid_set_p, i, &oid_val);
      if (error != NO_ERROR)
	{
	  goto error;
	}
    }

  /* drop components */
  if (n_old_subset > n_subset)
    {
      for (i = n_old_subset - 1; i >= n_min_subset; i--)
	{
	  error = set_get_element (old_oid_set_p, i, &oid_val);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  if (DB_VALUE_TYPE (&oid_val) != DB_TYPE_OID)
	    {
	      goto error;
	    }

	  /* logical deletion - keep OID in sequence */
	  oid_p = DB_PULL_OID (&oid_val);
	  error = catcls_delete_instance (thread_p, oid_p, class_oid_p,
					  hfid_p, &scan);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }
  /* add components */
  else if (n_old_subset < n_subset)
    {
      for (i = n_min_subset, oid_p = &tmp_oid; i < n_subset; i++)
	{
	  error = catcls_insert_instance (thread_p, &subset_p[i], oid_p,
					  root_oid, class_oid_p, hfid_p,
					  &scan);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }

	  db_push_oid (&oid_val, oid_p);
	  error = set_put_element (oid_set_p, i, &oid_val);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
    }

  if (DB_IS_NULL (&value_p->value))
    {
      db_make_sequence (&value_p->value, oid_set_p);
    }

  heap_scancache_end_modify (thread_p, &scan);
  catalog_free_class_info (cls_info_p);

  return NO_ERROR;

error:
  if (oid_set_p && DB_IS_NULL (&value_p->value))
    {
      set_free (oid_set_p);
    }

  if (is_scan_inited)
    {
      heap_scancache_end_modify (thread_p, &scan);
    }

  if (cls_info_p)
    {
      catalog_free_class_info (cls_info_p);
    }

  return error;
}

/*
 * catcls_insert_instance () - MVCC update catalog class instance
 *   return: error code
 *   thread_p(in): thread entry
 *   value_p(in/out): the values to insert into catalog classes
 *   oid_p(in): old instance oid
 *   root_oid_p(in):  root oid
 *   class_oid_p(in): class oid
 *   hfid_p(in): class hfid
 *   scan_p(in): scan cache
 */
static int
catcls_mvcc_update_instance (THREAD_ENTRY * thread_p, OR_VALUE * value_p,
			     OID * oid_p, OID * new_oid, OID * root_oid_p,
			     OID * class_oid_p, HFID * hfid_p,
			     HEAP_SCANCACHE * scan_p)
{
  RECDES record, old_record;
  OR_VALUE *old_value_p = NULL;
  OR_VALUE *attrs, *old_attrs;
  OR_VALUE *subset_p, *attr_p;
  bool old;
  int i, j, k;
  bool is_lock_inited = false;
  int uflag = false;
  int error = NO_ERROR;

  record.data = NULL;
  old_record.data = NULL;

#if defined(SERVER_MODE)
  /* lock the OID for delete purpose */
  if (lock_object (thread_p, oid_p, class_oid_p, X_LOCK, LK_UNCOND_LOCK) !=
      LK_GRANTED)
    {
      error = er_errid ();
      goto error;
    }
  is_lock_inited = true;
#endif /* SERVER_MODE */

  if (heap_get (thread_p, oid_p, &old_record, scan_p, COPY, NULL_CHN) !=
      S_SUCCESS)
    {
      error = er_errid ();
      goto error;
    }

  old_value_p =
    catcls_get_or_value_from_record (thread_p, &old_record, class_oid_p);
  if (old_value_p == NULL)
    {
      error = er_errid ();
      goto error;
    }

  error = catcls_reorder_attributes_by_repr (thread_p, value_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  if (heap_assign_address_with_class_oid (thread_p, hfid_p, class_oid_p,
					  new_oid, 0) != NO_ERROR)
    {
      error = er_errid ();
      goto error;
    }

  if (OID_ISNULL (root_oid_p))
    {
      COPY_OID (root_oid_p, new_oid);
    }

  for (attrs = value_p->sub.value, old_attrs = old_value_p->sub.value,
       i = 0; i < value_p->sub.count; i++)
    {
      if (IS_SUBSET (attrs[i]))
	{
	  /* set backward oid */
	  for (subset_p = attrs[i].sub.value,
	       j = 0; j < attrs[i].sub.count; j++)
	    {
	      /* assume that the attribute values of xxx are ordered by
	         { class_of, xxx_name, xxx_type, from_xxx_name, ... } */

	      attr_p = subset_p[j].sub.value;
	      db_push_oid (&attr_p[0].value, new_oid);

	      if (OID_EQ (class_oid_p, &ct_Class.classoid))
		{
		  /* if root node, eliminate self references */
		  for (k = 1; k < subset_p[j].sub.count; k++)
		    {
		      if (DB_VALUE_TYPE (&attr_p[k].value) == DB_TYPE_OID)
			{
			  if (OID_EQ (oid_p, DB_PULL_OID (&attr_p[k].value)))
			    {
			      db_value_put_null (&attr_p[k].value);
			    }
			}
		    }
		}
	    }

	  error =
	    catcls_mvcc_update_subset (thread_p, &attrs[i], &old_attrs[i],
				       root_oid_p);
	  if (error != NO_ERROR)
	    {
	      goto error;
	    }
	}
      else if (DB_VALUE_DOMAIN_TYPE (&attrs[i].value) == DB_TYPE_VARIABLE)
	{
	  /* set a self referenced oid */
	  db_push_oid (&attrs[i].value, root_oid_p);
	}
    }

  record.length = catcls_guess_record_length (value_p);
  record.area_size = record.length;
  record.type = REC_HOME;
  record.data = (char *) malloc (record.length);

  if (record.data == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, error, 1, record.length);
      goto error;
    }

  error = catcls_put_or_value_into_record (thread_p, value_p, 0, &record,
					   class_oid_p);
  if (error != NO_ERROR)
    {
      goto error;
    }

  /* heap update new object */
  if (heap_update (thread_p, hfid_p, class_oid_p, new_oid, &record, NULL,
		   &old, scan_p, HEAP_UPDATE_IN_PLACE) == NULL)
    {
      error = er_errid ();
      goto error;
    }

  /* give up setting updated attr info */
  if (locator_update_index (thread_p, &record, &old_record, NULL, 0,
			    oid_p, new_oid, class_oid_p, NULL, false,
			    SINGLE_ROW_UPDATE, scan_p, false,
			    REPL_INFO_TYPE_STMT_NORMAL) != NO_ERROR)
    {
      error = er_errid ();
      goto error;
    }

  /* link the old version by new version */
  if (heap_mvcc_update_to_row_version (thread_p, hfid_p, class_oid_p, oid_p,
				       new_oid, scan_p) == NULL)
    {
      error = er_errid ();
      goto error;
    }

#if defined(SERVER_MODE)
  lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
#endif /* SERVER_MODE */

  free_and_init (record.data);
  catcls_free_or_value (old_value_p);

  return NO_ERROR;

error:

#if defined(SERVER_MODE)
  if (is_lock_inited)
    {
      lock_unlock_object (thread_p, oid_p, class_oid_p, X_LOCK, false);
    }
#endif /* SERVER_MODE */

  if (record.data)
    {
      free_and_init (record.data);
    }

  if (old_value_p)
    {
      catcls_free_or_value (old_value_p);
    }

  return error;
}

/*
 * catcls_get_db_collation () - get infomation on all collation in DB
 *				stored in the "_db_collation" system table
 *
 *   return: NO_ERROR, or error code
 *   thread_p(in)  : thread context
 *   db_collations(out): array of collation info
 *   coll_cnt(out): number of collations found in DB
 *
 *  Note : This function is called during server initialization, for this
 *	   reason, no locks are required on the class.
 */
int
catcls_get_db_collation (THREAD_ENTRY * thread_p,
			 LANG_COLL_COMPAT ** db_collations, int *coll_cnt)
{
  OID class_oid;
  OID inst_oid;
  HFID hfid;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_SCANCACHE scan_cache;
  RECDES recdes;
  const char *class_name = "_db_collation";
  int i;
  int error = NO_ERROR;
  int att_id_cnt = 0;
  int max_coll_cnt;
  int coll_id_att_id = -1, coll_name_att_id = -1, charset_id_att_id = -1,
    checksum_att_id = -1;
  int alloc_size;
  bool attr_info_inited = false;
  bool scan_cache_inited = false;

  assert (db_collations != NULL);
  assert (coll_cnt != NULL);

  OID_SET_NULL (&class_oid);
  OID_SET_NULL (&inst_oid);

  error = catcls_find_class_oid_by_class_name (thread_p, class_name,
					       &class_oid);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (OID_ISNULL (&class_oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME,
	      1, class_name);
      error = ER_LC_UNKNOWN_CLASSNAME;
      goto exit;
    }

  error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  attr_info_inited = true;

  (void) heap_scancache_quick_start (&scan_cache);
  scan_cache_inited = true;

  if (heap_get (thread_p, &class_oid, &recdes, &scan_cache, PEEK,
		NULL_CHN) != S_SUCCESS)
    {
      error = ER_FAILED;
      goto exit;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      const char *rec_attr_name_p = or_get_attrname (&recdes, i);
      if (rec_attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      if (strcmp (CT_DBCOLL_COLL_ID_COLUMN, rec_attr_name_p) == 0)
	{
	  coll_id_att_id = i;
	  att_id_cnt++;
	}
      else if (strcmp (CT_DBCOLL_COLL_NAME_COLUMN, rec_attr_name_p) == 0)
	{
	  coll_name_att_id = i;
	  att_id_cnt++;
	}
      else if (strcmp (CT_DBCOLL_CHARSET_ID_COLUMN, rec_attr_name_p) == 0)
	{
	  charset_id_att_id = i;
	  att_id_cnt++;
	}
      else if (strcmp (CT_DBCOLL_CHECKSUM_COLUMN, rec_attr_name_p) == 0)
	{
	  checksum_att_id = i;
	  att_id_cnt++;
	}
      if (att_id_cnt >= 4)
	{
	  break;
	}
    }

  if (att_id_cnt != 4)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_FAILED;
      goto exit;
    }

  (void) heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  /* read values of all records in heap */
  error = heap_get_hfid_from_class_oid (thread_p, &class_oid, &hfid);
  if (error != NO_ERROR || HFID_IS_NULL (&hfid))
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_scancache_start (thread_p, &scan_cache, &hfid, NULL, true,
				false, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  scan_cache_inited = true;

  max_coll_cnt = LANG_MAX_COLLATIONS;
  alloc_size = max_coll_cnt * sizeof (LANG_COLL_COMPAT);
  *db_collations = (LANG_COLL_COMPAT *) db_private_alloc (thread_p,
							  alloc_size);
  if (*db_collations == NULL)
    {
      error = ER_OUT_OF_VIRTUAL_MEMORY;
      goto exit;
    }

  *coll_cnt = 0;
  while (heap_next (thread_p, &hfid, NULL, &inst_oid, &recdes,
		    &scan_cache, PEEK) == S_SUCCESS)
    {
      HEAP_ATTRVALUE *heap_value = NULL;
      LANG_COLL_COMPAT *curr_coll;

      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes,
				       NULL, &attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      if (*coll_cnt >= max_coll_cnt)
	{
	  max_coll_cnt = max_coll_cnt * 2;
	  alloc_size = max_coll_cnt * sizeof (LANG_COLL_COMPAT);
	  *db_collations =
	    (LANG_COLL_COMPAT *) db_private_realloc (thread_p, *db_collations,
						     alloc_size);
	  if (db_collations == NULL)
	    {
	      error = ER_OUT_OF_VIRTUAL_MEMORY;
	      goto exit;
	    }
	}

      curr_coll = &((*db_collations)[(*coll_cnt)++]);
      memset (curr_coll, 0, sizeof (LANG_COLL_COMPAT));

      for (i = 0, heap_value = attr_info.values;
	   i < attr_info.num_values; i++, heap_value++)
	{
	  if (heap_value->attrid == coll_id_att_id)
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (&(heap_value->dbvalue))
		      == DB_TYPE_INTEGER);

	      curr_coll->coll_id = DB_GET_INTEGER (&heap_value->dbvalue);
	    }
	  else if (heap_value->attrid == coll_name_att_id)
	    {
	      char *lang_str = NULL;
	      int lang_str_len;

	      assert (DB_VALUE_DOMAIN_TYPE (&(heap_value->dbvalue))
		      == DB_TYPE_STRING);

	      lang_str = DB_GET_STRING (&heap_value->dbvalue);
	      lang_str_len = (lang_str != NULL) ? strlen (lang_str) : 0;
	      lang_str_len = MIN (lang_str_len,
				  (int) sizeof (curr_coll->coll_name));

	      strncpy (curr_coll->coll_name, lang_str, lang_str_len);
	      curr_coll->coll_name[lang_str_len] = '\0';
	    }
	  else if (heap_value->attrid == charset_id_att_id)
	    {
	      assert (DB_VALUE_DOMAIN_TYPE (&(heap_value->dbvalue))
		      == DB_TYPE_INTEGER);

	      curr_coll->codeset =
		(INTL_CODESET) DB_GET_INTEGER (&heap_value->dbvalue);
	    }
	  else if (heap_value->attrid == checksum_att_id)
	    {
	      char *checksum_str = NULL;
	      int str_len;

	      assert (DB_VALUE_DOMAIN_TYPE (&(heap_value->dbvalue))
		      == DB_TYPE_STRING);

	      checksum_str = DB_GET_STRING (&heap_value->dbvalue);
	      str_len = (checksum_str != NULL) ? strlen (checksum_str) : 0;

	      assert (str_len == 32);

	      strncpy (curr_coll->checksum, checksum_str, str_len);
	      curr_coll->checksum[str_len] = '\0';
	    }
	}
    }

exit:
  if (scan_cache_inited == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      scan_cache_inited = false;
    }

  if (attr_info_inited == true)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      attr_info_inited = false;
    }

  return error;
}

/*
 * catcls_get_apply_info_log_record_time () - get max log_record_time
 *                                            in db_ha_apply_info
 *
 *   return: NO_ERROR, or error code
 *   thread_p(in)  : thread context
 *   log_record_time(out): log_record_time
 *
 */
int
catcls_get_apply_info_log_record_time (THREAD_ENTRY * thread_p,
				       time_t * log_record_time)
{
  OID class_oid;
  OID inst_oid;
  HFID hfid;
  HEAP_CACHE_ATTRINFO attr_info;
  HEAP_SCANCACHE scan_cache;
  RECDES recdes;
  DB_DATETIME *tmp_datetime;
  time_t tmp_log_record_time = 0;
  int log_record_time_att_id = -1;
  int error = NO_ERROR;
  int i;
  bool attr_info_inited = false;
  bool scan_cache_inited = false;


  assert (log_record_time != NULL);
  *log_record_time = 0;

  OID_SET_NULL (&class_oid);
  OID_SET_NULL (&inst_oid);

  error =
    catcls_find_class_oid_by_class_name (thread_p, CT_HA_APPLY_INFO_NAME,
					 &class_oid);
  if (error != NO_ERROR)
    {
      goto exit;
    }

  if (OID_ISNULL (&class_oid))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_LC_UNKNOWN_CLASSNAME, 1,
	      CT_HA_APPLY_INFO_NAME);
      error = ER_LC_UNKNOWN_CLASSNAME;
      goto exit;
    }

  error = heap_attrinfo_start (thread_p, &class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  attr_info_inited = true;

  heap_scancache_quick_start (&scan_cache);
  scan_cache_inited = true;

  if (heap_get (thread_p, &class_oid, &recdes, &scan_cache, PEEK, NULL_CHN) !=
      S_SUCCESS)
    {
      error = ER_FAILED;
      goto exit;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      const char *rec_attr_name_p = or_get_attrname (&recdes, i);
      if (rec_attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      if (strcmp ("log_record_time", rec_attr_name_p) == 0)
	{
	  log_record_time_att_id = i;
	  break;
	}
    }

  if (log_record_time_att_id == -1)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      error = ER_FAILED;
      goto exit;
    }

  heap_scancache_end (thread_p, &scan_cache);
  scan_cache_inited = false;

  error = heap_get_hfid_from_class_oid (thread_p, &class_oid, &hfid);
  if (error != NO_ERROR || HFID_IS_NULL (&hfid))
    {
      error = ER_FAILED;
      goto exit;
    }

  error = heap_scancache_start (thread_p, &scan_cache, &hfid, NULL, true,
				false, NULL);
  if (error != NO_ERROR)
    {
      goto exit;
    }
  scan_cache_inited = true;

  while (heap_next
	 (thread_p, &hfid, NULL, &inst_oid, &recdes, &scan_cache,
	  PEEK) == S_SUCCESS)
    {
      HEAP_ATTRVALUE *heap_value = NULL;

      if (heap_attrinfo_read_dbvalues (thread_p, &inst_oid, &recdes, NULL,
				       &attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  goto exit;
	}

      for (i = 0, heap_value = attr_info.values; i < attr_info.num_values;
	   i++, heap_value++)
	{
	  if (heap_value->attrid == log_record_time_att_id)
	    {
	      tmp_datetime = DB_GET_DATETIME (&heap_value->dbvalue);
	      tmp_datetime->time /= 1000;

	      tmp_log_record_time =
		db_mktime (&tmp_datetime->date, &tmp_datetime->time);

	      break;
	    }
	}

      if (tmp_log_record_time > *log_record_time)
	{
	  *log_record_time = tmp_log_record_time;
	}
    }

exit:
  if (scan_cache_inited == true)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      scan_cache_inited = false;
    }

  if (attr_info_inited == true)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      attr_info_inited = false;
    }

  return error;
}

/*
 * catcls_find_and_set_serial_class_oid () - Used to find OID for serial class
 *					  and set the global variable for
 *					  serial class OID.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: This was required for MVCC. It is probably a good idea to extend
 *	 cached OID's for all system classes, to avoid looking for them every
 *	 time they're needed.
 */
int
catcls_find_and_set_serial_class_oid (THREAD_ENTRY * thread_p)
{
  OID serial_class_oid;
  LC_FIND_CLASSNAME status;

  status = xlocator_find_class_oid (thread_p, CT_SERIAL_NAME,
				    &serial_class_oid, NULL_LOCK);
  if (status == LC_CLASSNAME_ERROR)
    {
      return ER_FAILED;
    }

  oid_set_serial (&serial_class_oid);

  return NO_ERROR;
}

/*
 * catcls_find_and_set_partition_class_oid () - Used to find OID for partition
 *						class and set the global
 *						variable for partition class
 *						OID.
 *
 * return	 : Error code.
 * thread_p (in) : Thread entry.
 *
 * NOTE: This was required for MVCC. It is probably a good idea to extend
 *	 cached OID's for all system classes, to avoid looking for them every
 *	 time they're needed.
 */
int
catcls_find_and_set_partition_class_oid (THREAD_ENTRY * thread_p)
{
  OID partition_class_oid;
  LC_FIND_CLASSNAME status;

  status = xlocator_find_class_oid (thread_p, CT_PARTITION_NAME,
				    &partition_class_oid, NULL_LOCK);
  if (status == LC_CLASSNAME_ERROR)
    {
      return ER_FAILED;
    }

  oid_set_partition (&partition_class_oid);

  return NO_ERROR;
}
