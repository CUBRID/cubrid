/*
 *
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
 * deduplicate_key.h - Support duplicate key index
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

#include "deduplicate_key.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/* support for SUPPORT_DEDUPLICATE_KEY_MODE */

// The higher the level, the more severe the deduplicate.
#define CALC_MOD_VALUE_FROM_LEVEL(lv)   (((unsigned int)1) << (lv))

static char dk_reserved_deduplicate_key_attr_name[COUNT_OF_DEDUPLICATE_KEY_LEVEL][DEDUPLICATE_KEY_ATTR_NAME_LEN];

//=============================================================================
#if defined(SERVER_MODE) || defined(SA_MODE)
static OR_ATTRIBUTE st_or_atts[COUNT_OF_DEDUPLICATE_KEY_LEVEL];
static bool st_or_atts_init = false;

static void
dk_or_attribute_initialized ()
{
  int att_id;
  int level, idx;
  DB_DOMAIN *domain = &tp_Short_domain;

  for (level = DEDUPLICATE_KEY_LEVEL_MIN; level <= DEDUPLICATE_KEY_LEVEL_MAX; level++)
    {
      att_id = MK_DEDUPLICATE_KEY_ATTR_ID (level);
      idx = LEVEL_2_IDX (level);
      st_or_atts[idx].id = att_id;
      assert (CALC_MOD_VALUE_FROM_LEVEL (level) <= SHRT_MAX);
      st_or_atts[idx].domain = domain;
      st_or_atts[idx].type = st_or_atts[idx].domain->type->id;
    }

#ifndef NDEBUG
  /* Check whether the macro that creates or extracts the attribute name and ID for the deduplicate_key
   * for the specified level operates normally. */
  int lv1, lv2;
  char *attr_name;
  char *ptr;

  for (level = DEDUPLICATE_KEY_LEVEL_MIN; level <= DEDUPLICATE_KEY_LEVEL_MAX; level++)
    {
      att_id = MK_DEDUPLICATE_KEY_ATTR_ID (level);
      lv1 = GET_DEDUPLICATE_KEY_ATTR_LEVEL (att_id);
      attr_name = dk_get_deduplicate_key_attr_name (lv1);
      GET_DEDUPLICATE_KEY_ATTR_LEVEL_FROM_NAME (attr_name, lv2);
      if (!IS_DEDUPLICATE_KEY_ATTR_ID (att_id))
	{
	  assert (0);
	}
      if (!IS_DEDUPLICATE_KEY_ATTR_NAME (attr_name))
	{
	  assert (0);
	}
      if (lv1 != lv2 || lv1 != level)
	{
	  assert (0);
	}

      ptr = dk_get_deduplicate_key_attr_name (GET_DEDUPLICATE_KEY_ATTR_LEVEL (att_id));
      if (strcmp (attr_name, ptr))
	{
	  assert (0);
	}
    }
#endif

  st_or_atts_init = true;
}

void *
dk_find_or_deduplicate_key_attribute (int att_id)
{
  int level = GET_DEDUPLICATE_KEY_ATTR_LEVEL (att_id);

  assert (IS_DEDUPLICATE_KEY_ATTR_ID (att_id));
  assert (level >= DEDUPLICATE_KEY_LEVEL_MIN && level <= DEDUPLICATE_KEY_LEVEL_MAX);
  assert (st_or_atts_init == true);

  return (void *) &(st_or_atts[LEVEL_2_IDX (level)]);
}

int
dk_get_deduplicate_key_position (int n_attrs, int *attr_ids, int func_attr_index_start)
{
  if (n_attrs > 1)
    {
      if (func_attr_index_start != -1)
	{
	  if ((func_attr_index_start > 0) && IS_DEDUPLICATE_KEY_ATTR_ID (attr_ids[func_attr_index_start - 1]))
	    {
	      return func_attr_index_start;
	    }
	}
      else if (IS_DEDUPLICATE_KEY_ATTR_ID (attr_ids[n_attrs - 1]))
	{
	  return n_attrs - 1;
	}
    }

  return -1;
}

int
dk_get_deduplicate_key_value (OID * rec_oid, int att_id, DB_VALUE * value)
{
  // The rec_oid may be NULL when the index of the UNIQUE attribute is an index. 
  // In that case, however, it cannot entered here.
  short level = GET_DEDUPLICATE_KEY_ATTR_LEVEL (att_id);

  assert_release (rec_oid != NULL);
  assert (IS_DEDUPLICATE_KEY_ATTR_ID (att_id));

#ifndef NDEBUG
  /* This code is to be used only for internal developer testing.  */
  bool is_test_for_developer = false;
  if (is_test_for_developer)
    {
#define OID_2_BIGINT(oidptr) (((oidptr)->volid << 48) | ((oidptr)->pageid << 16) | (oidptr)->slotid)
      assert (CALC_MOD_VALUE_FROM_LEVEL (level) <= SHRT_MAX);
      db_make_short (value, (short) (OID_2_BIGINT (rec_oid) % CALC_MOD_VALUE_FROM_LEVEL (level)));

      return NO_ERROR;
    }
#endif

  assert (CALC_MOD_VALUE_FROM_LEVEL (level) <= SHRT_MAX);
  db_make_short (value, (short) (rec_oid->pageid % CALC_MOD_VALUE_FROM_LEVEL (level)));

  return NO_ERROR;
}

#endif // #if defined(SERVER_MODE) || defined(SA_MODE)
//=============================================================================

//=============================================================================
#if !defined(SERVER_MODE)

// SM_ATTRIBUTE and DB_ATTRIBUTE are the same thing.
static SM_ATTRIBUTE *st_sm_atts[COUNT_OF_DEDUPLICATE_KEY_LEVEL];
static bool st_sm_atts_init = false;

static void
dk_sm_attribute_finalized ()
{
  int level, idx;

  if (st_sm_atts_init)
    {
      for (level = DEDUPLICATE_KEY_LEVEL_MIN; level <= DEDUPLICATE_KEY_LEVEL_MAX; level++)
	{
	  idx = LEVEL_2_IDX (level);
	  if (st_sm_atts[idx])
	    {
	      classobj_free_attribute (st_sm_atts[idx]);
	      st_sm_atts[idx] = NULL;
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
  DB_DOMAIN *domain = &tp_Short_domain;

  for (level = DEDUPLICATE_KEY_LEVEL_MIN; level <= DEDUPLICATE_KEY_LEVEL_MAX; level++)
    {
      assert (CALC_MOD_VALUE_FROM_LEVEL (level) <= SHRT_MAX);
      reserved_name = dk_get_deduplicate_key_attr_name (level);

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
      att->id = MK_DEDUPLICATE_KEY_ATTR_ID (level);

      st_sm_atts[LEVEL_2_IDX (level)] = att;
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
dk_find_sm_deduplicate_key_attribute (int att_id, const char *att_name)
{
  int level;

  assert ((att_id != -1) || (att_name && *att_name));
  if (att_id != -1)
    {
      assert (IS_DEDUPLICATE_KEY_ATTR_ID (att_id));
      level = GET_DEDUPLICATE_KEY_ATTR_LEVEL (att_id);
    }
  else
    {
      GET_DEDUPLICATE_KEY_ATTR_LEVEL_FROM_NAME (att_name, level);
    }

  assert (level >= DEDUPLICATE_KEY_LEVEL_MIN && level <= DEDUPLICATE_KEY_LEVEL_MAX);
  assert (dk_reserved_deduplicate_key_attr_name[level] != NULL);
  assert (st_sm_atts_init == true);

  return st_sm_atts[LEVEL_2_IDX (level)];
}

int
dk_sm_deduplicate_key_position (int n_attrs, SM_ATTRIBUTE ** attrs, SM_FUNCTION_INFO * function_index)
{
  if (n_attrs > 1)
    {
      if (function_index)
	{
	  if ((function_index->attr_index_start > 0)
	      && IS_DEDUPLICATE_KEY_ATTR_ID (attrs[function_index->attr_index_start - 1]->id))
	    {
	      return function_index->attr_index_start;
	    }
	}
      else if (IS_DEDUPLICATE_KEY_ATTR_ID (attrs[n_attrs - 1]->id))
	{
	  return (n_attrs - 1);
	}
    }

  return -1;
}

void
dk_create_index_level_adjust (const PT_INDEX_INFO * idx_info, char **attnames, int *asc_desc,
			      int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info, int nnames, bool is_reverse)
{
  int deduplicate_key_col_pos;

  assert (nnames > 0);
  assert (asc_desc != NULL);
  assert (idx_info->deduplicate_level != DEDUPLICATE_KEY_LEVEL_OFF);
  assert (idx_info->deduplicate_level >= DEDUPLICATE_KEY_LEVEL_MIN
	  && idx_info->deduplicate_level <= DEDUPLICATE_KEY_LEVEL_MAX);

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

      deduplicate_key_col_pos = func_index_info->attr_index_start++;
    }
  else
    {
      deduplicate_key_col_pos = nnames;
    }

  if (attrs_prefix_length)
    {
      attrs_prefix_length[deduplicate_key_col_pos] = -1;
    }
  attnames[deduplicate_key_col_pos] = dk_get_deduplicate_key_attr_name (idx_info->deduplicate_level);
  asc_desc[deduplicate_key_col_pos] = (is_reverse ? 1 : 0);

  attnames[nnames + 1] = NULL;
}

char *
dk_print_deduplicate_key_info (char *buf, int buf_size, int deduplicate_level)
{
  int len = 0;

  assert ((deduplicate_level == DEDUPLICATE_KEY_LEVEL_OFF)
	  || (deduplicate_level >= DEDUPLICATE_KEY_LEVEL_MIN && deduplicate_level <= DEDUPLICATE_KEY_LEVEL_MAX));

  if (prm_get_bool_value (PRM_ID_PRINT_INDEX_DETAIL))
    {
      len = snprintf (buf, buf_size, "DEDUPLICATE=%d", deduplicate_level);
    }
  else
    {
      buf[0] = '\0';
    }

  assert (len < buf_size);
  return buf;
}
#endif // #if !defined(SERVER_MODE)
//=============================================================================
char *
dk_get_deduplicate_key_attr_name (int level)
{
  char *p = dk_reserved_deduplicate_key_attr_name[LEVEL_2_IDX ((level))];
  return p;
}

void
dk_deduplicate_key_attribute_initialized ()
{
  for (int level = DEDUPLICATE_KEY_LEVEL_MIN; level <= DEDUPLICATE_KEY_LEVEL_MAX; level++)
    {
      sprintf (dk_reserved_deduplicate_key_attr_name[LEVEL_2_IDX (level)], "%s%02d",
	       DEDUPLICATE_KEY_ATTR_NAME_PREFIX, level);
    }

#if defined(SERVER_MODE) || defined(SA_MODE)
  dk_or_attribute_initialized ();
#endif

#if !defined(SERVER_MODE)
  dk_sm_attribute_initialized ();
#endif
}

void
dk_deduplicate_key_attribute_finalized ()
{
#if !defined(SERVER_MODE)
  dk_sm_attribute_finalized ();
#endif
}
