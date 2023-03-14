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
 * decompress_index.c - Support duplicate key index
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dbtype.h"
#if defined(SERVER_MODE) || defined(SA_MODE)
#include "object_representation.h"
#include "object_primitive.h"
#include "object_representation_sr.h"
#endif

#include "decompress_index.h"

#if defined(SUPPORT_COMPRESS_MODE)

#if 0
// The higher the "level", the more compressed it should be. Close to mode "HIGH".
#define CALC_MOD_VALUE_FROM_LEVEL(lv)   (1 << ((COMPRESS_INDEX_MOD_LEVEL_MAX + 1) - (lv)))
#else
//The higher the "level" value, the weaker the compression.
// like pow(2, (lv));
#define CALC_MOD_VALUE_FROM_LEVEL(lv)   (1 << (lv))
#endif

static DB_DOMAIN *
get_reserved_index_attr_domain_type (int level)
{
  if (level == COMPRESS_INDEX_MOD_LEVEL_ZERO)
    {
      return &tp_Integer_domain;
    }

  return (CALC_MOD_VALUE_FROM_LEVEL (level) > SHRT_MAX) ? &tp_Integer_domain : &tp_Short_domain;
}

//=============================================================================
#if defined(SERVER_MODE) || defined(SA_MODE)
static OR_ATTRIBUTE st_or_atts[COUNT_OF_COMPRESS_INDEX_MOD_LEVEL];
static bool st_or_atts_init = false;

static void
dk_or_attribute_initialized ()
{
  int att_id;
  int level;

  for (level = COMPRESS_INDEX_MOD_LEVEL_ZERO; level <= COMPRESS_INDEX_MOD_LEVEL_MAX; level++)
    {
      att_id = MK_COMPRESS_INDEX_ATTR_ID (level);
      st_or_atts[level].id = att_id;
      st_or_atts[level].domain = get_reserved_index_attr_domain_type (level);
      st_or_atts[level].type = st_or_atts[level].domain->type->id;
    }

  st_or_atts_init = true;
}

void *
dk_find_or_compress_index_attribute (int att_id)
{
  int level = GET_COMPRESS_INDEX_ATTR_LEVEL (att_id);

  assert (IS_COMPRESS_INDEX_ATTR_ID (att_id));
  assert (level >= COMPRESS_INDEX_MOD_LEVEL_ZERO && level <= COMPRESS_INDEX_MOD_LEVEL_MAX);
  assert (st_or_atts_init == true);

  return (void *) &(st_or_atts[level]);
}

int
dk_get_decompress_position (int n_attrs, int *attr_ids, int func_attr_index_start)
{
  if (n_attrs > 1)
    {
      if (func_attr_index_start != -1)
	{
	  if ((func_attr_index_start > 0) && IS_COMPRESS_INDEX_ATTR_ID (attr_ids[func_attr_index_start - 1]))
	    {
	      return func_attr_index_start;
	    }
	}
      else if (IS_COMPRESS_INDEX_ATTR_ID (attr_ids[n_attrs - 1]))
	{
	  return n_attrs - 1;
	}
    }

  return -1;
}

int
dk_or_decompress_position (int n_attrs, OR_ATTRIBUTE ** attrs, OR_FUNCTION_INDEX * function_index)
{
  if (n_attrs > 1)
    {
      if (function_index)
	{
	  if ((function_index->attr_index_start > 0)
	      && IS_COMPRESS_INDEX_ATTR_ID (attrs[function_index->attr_index_start - 1]->id))
	    {
	      return function_index->attr_index_start;
	    }
	}
      else if (IS_COMPRESS_INDEX_ATTR_ID (attrs[n_attrs - 1]->id))
	{
	  return n_attrs - 1;
	}
    }

  return -1;
}

int
dk_get_compress_index_value (OID * rec_oid, int att_id, DB_VALUE * value)
{
  // The rec_oid may be NULL when the index of the UNIQUE attribute is an index. 
  // In that case, however, it cannot entered here.
  short level = GET_COMPRESS_INDEX_ATTR_LEVEL (att_id);

  assert_release (rec_oid != NULL);
  assert (IS_COMPRESS_INDEX_ATTR_ID (att_id));

#ifndef NDEBUG
#define OID_2_BIGINT(oidptr) (((oidptr)->volid << 48) | ((oidptr)->pageid << 16) | (oidptr)->slotid)

  if (prm_get_bool_value (PRM_ID_USE_COMPRESS_INDEX_MODE_OID_TEST))
    {
      if (level == COMPRESS_INDEX_MOD_LEVEL_ZERO)
	{
	  db_make_int (value, (int) (OID_2_BIGINT (rec_oid) % SHRT_MAX));
	}
      else
	{
	  int mod_val = CALC_MOD_VALUE_FROM_LEVEL (level);
	  if (mod_val > SHRT_MAX)
	    {
	      db_make_int (value, (int) (OID_PSEUDO_KEY (rec_oid) % mod_val));
	    }
	  else
	    {
	      db_make_short (value, (short) (OID_PSEUDO_KEY (rec_oid) % mod_val));
	    }
	}

      return NO_ERROR;
    }
#endif

  if (level == COMPRESS_INDEX_MOD_LEVEL_ZERO)
    {
      db_make_int (value, rec_oid->pageid);
    }
  else
    {
      int mod_val = CALC_MOD_VALUE_FROM_LEVEL (level);
      if (mod_val > SHRT_MAX)
	{
	  db_make_int (value, (int) (rec_oid->pageid % mod_val));
	}
      else
	{
	  db_make_short (value, (short) (rec_oid->pageid % mod_val));
	}
    }

  return NO_ERROR;
}

#endif // #if defined(SERVER_MODE) || defined(SA_MODE)
//=============================================================================

//=============================================================================
#if !defined(SERVER_MODE)

// SM_ATTRIBUTE and DB_ATTRIBUTE are the same thing.
static SM_ATTRIBUTE *st_sm_atts[COUNT_OF_COMPRESS_INDEX_MOD_LEVEL];
static bool st_sm_atts_init = false;

static void
dk_sm_attribute_finalized ()
{
  int level;

  if (st_sm_atts_init)
    {
      for (level = COMPRESS_INDEX_MOD_LEVEL_ZERO; level <= COMPRESS_INDEX_MOD_LEVEL_MAX; level++)
	{
	  if (st_sm_atts[level])
	    {
	      classobj_free_attribute (st_sm_atts[level]);
	      st_sm_atts[level] = NULL;
	    }
	}
      st_sm_atts_init = false;
    }
}

static void
dk_sm_attribute_initialized ()
{
  int error_code = NO_ERROR;
  int level;
  const char *reserved_name = NULL;
  SM_ATTRIBUTE *att;
  DB_DOMAIN *domain = NULL;

  for (level = COMPRESS_INDEX_MOD_LEVEL_ZERO; level <= COMPRESS_INDEX_MOD_LEVEL_MAX; level++)
    {
      reserved_name = GET_COMPRESS_INDEX_ATTR_NAME (level);
      domain = get_reserved_index_attr_domain_type (level);
      if (domain == NULL)
	{
	  assert (false);
	  ERROR0 (error_code, ER_FAILED);
	  goto error_exit;
	}

      att = classobj_make_attribute (reserved_name, domain->type, ID_ATTRIBUTE);
      if (att == NULL)
	{
	  assert (er_errid () != NO_ERROR);
	  error_code = er_errid ();
	  goto error_exit;
	}

      /* Flag this attribute as new so that we can initialize the original_value properly.  
       * Make sure this isn't saved on disk ! */
      att->flags |= SM_ATTFLAG_NEW;
      att->class_mop = NULL;
      att->domain = domain;
      att->auto_increment = NULL;
      att->id = MK_COMPRESS_INDEX_ATTR_ID (level);

      st_sm_atts[level] = att;
    }

  st_sm_atts_init = true;
  return;

error_exit:
  if (att)
    {
      classobj_free_attribute (att);
      assert_release (false);
    }
}

SM_ATTRIBUTE *
dk_find_sm_compress_index_attribute (int att_id, const char *att_name)
{
  int level, idx;

  assert ((att_id != -1) || (att_name && *att_name));
  if (att_id != -1)
    {
      assert (IS_COMPRESS_INDEX_ATTR_ID (att_id));
      level = GET_COMPRESS_INDEX_ATTR_LEVEL (att_id);
    }
  else
    {
      GET_COMPRESS_INDEX_ATTR_MODE_LEVEL_FROM_NAME (att_name, level);
    }

  assert (level >= COMPRESS_INDEX_MOD_LEVEL_ZERO && level <= COMPRESS_INDEX_MOD_LEVEL_MAX);
  assert (dk_reserved_compress_index_col_name[level] != NULL);
  assert (st_sm_atts_init == true);

  return st_sm_atts[level];
}

int
dk_sm_decompress_position (int n_attrs, SM_ATTRIBUTE ** attrs, SM_FUNCTION_INFO * function_index)
{
  if (n_attrs > 1)
    {
      if (function_index)
	{
	  if ((function_index->attr_index_start > 0)
	      && IS_COMPRESS_INDEX_ATTR_ID (attrs[function_index->attr_index_start - 1]->id))
	    {
	      return function_index->attr_index_start;
	    }
	}
      else if (IS_COMPRESS_INDEX_ATTR_ID (attrs[n_attrs - 1]->id))
	{
	  return (n_attrs - 1);
	}
    }

  return -1;
}

void
dk_create_index_level_remove_adjust (DB_CONSTRAINT_TYPE ctype, char **attnames, int *asc_desc,
				     int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info,
				     int compress_index_col_pos, int nnames)
{
  int func_no_args = 0;

  assert (!DB_IS_CONSTRAINT_UNIQUE_FAMILY (ctype));
  if (ctype != DB_CONSTRAINT_FOREIGN_KEY)
    {
      assert (asc_desc != NULL);
    }

  if (compress_index_col_pos != -1)
    {				// remove hidden column   
      attnames[compress_index_col_pos] = NULL;

      assert (!func_index_info || (func_index_info && func_index_info->attr_index_start > 0));
      if (func_index_info && func_index_info->attr_index_start > 0)
	{
	  func_no_args = nnames - compress_index_col_pos;
	  if (func_no_args > 0)
	    {
	      memmove (asc_desc + compress_index_col_pos, asc_desc + (compress_index_col_pos + 1),
		       (func_no_args * sizeof (asc_desc[0])));
	      if (attrs_prefix_length)
		{
		  memmove (attrs_prefix_length + compress_index_col_pos,
			   attrs_prefix_length + (compress_index_col_pos + 1),
			   (func_no_args * sizeof (attrs_prefix_length[0])));
		}
	      memmove (attnames + compress_index_col_pos, attnames + (compress_index_col_pos + 1),
		       (func_no_args * sizeof (attnames[0])));

	      attnames[nnames - 1] = NULL;
	      func_index_info->attr_index_start--;
	    }
	}
      compress_index_col_pos = -1;
    }
}

void
dk_create_index_level_adjust (const PT_INDEX_INFO * idx_info, char **attnames, int *asc_desc,
			      int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info, int nnames, bool is_reverse)
{
  int compress_index_col_pos;

  assert (asc_desc != NULL);
  assert (idx_info->compress_mode != COMPRESS_INDEX_MODE_NONE);
  assert (idx_info->compress_level >= COMPRESS_INDEX_MOD_LEVEL_ZERO
	  && idx_info->compress_level <= COMPRESS_INDEX_MOD_LEVEL_MAX);

  if (func_index_info)
    {
      if (idx_info->func_no_args > 0)
	{
	  memmove (asc_desc + (func_index_info->attr_index_start + 1),
		   asc_desc + func_index_info->attr_index_start, idx_info->func_no_args * sizeof (asc_desc[0]));
	  if (attrs_prefix_length)
	    {
	      memmove (attrs_prefix_length + (func_index_info->attr_index_start + 1),
		       attrs_prefix_length + func_index_info->attr_index_start,
		       idx_info->func_no_args * sizeof (attrs_prefix_length[0]));
	    }
	  memmove (attnames + (func_index_info->attr_index_start + 1),
		   attnames + func_index_info->attr_index_start, idx_info->func_no_args * sizeof (attnames[0]));
	}

      compress_index_col_pos = func_index_info->attr_index_start++;
    }
  else
    {
      compress_index_col_pos = nnames;
    }

  if (attrs_prefix_length)
    {
      attrs_prefix_length[compress_index_col_pos] = -1;
    }
  attnames[compress_index_col_pos] = (char *) GET_COMPRESS_INDEX_ATTR_NAME (idx_info->compress_level);
  asc_desc[compress_index_col_pos] = (is_reverse ? 1 : 0);

  attnames[nnames + 1] = NULL;
}

char *
dk_print_compress_index_info (char *buf, int buf_size, int compress_mode, int compress_level)
{
  int len = 0;

  buf[0] = '\0';
  if (compress_mode == COMPRESS_INDEX_MODE_NONE)
    {
      len = snprintf (buf, buf_size, "COMPRESS HIGH");
    }
  else if (compress_mode == COMPRESS_INDEX_MODE_SET)
    {
      if (compress_level == COMPRESS_INDEX_MOD_LEVEL_ZERO)
	{
	  len = snprintf (buf, buf_size, "COMPRESS LOW");
	}
      else
	{
	  len = snprintf (buf, buf_size, "COMPRESS MEDIUM(%d)", compress_level);
	}
    }

  assert (len < buf_size);
  return buf;
}

#endif // #if !defined(SERVER_MODE)
//=============================================================================


void
dk_compress_index_attribute_initialized ()
{
#if defined(SERVER_MODE) || defined(SA_MODE)
  dk_or_attribute_initialized ();
#endif

#if !defined(SERVER_MODE)
  dk_sm_attribute_initialized ();
#endif
}

void
dk_compress_index_attribute_finalized ()
{
#if !defined(SERVER_MODE)
  dk_sm_attribute_finalized ();
#endif
}

#endif // #if defined(SUPPORT_COMPRESS_MODE)
