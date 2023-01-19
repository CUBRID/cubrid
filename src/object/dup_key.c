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
 * dup_key.c - Support duplicate key index
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

#include "dup_key.h"

#if defined(SUPPORT_KEY_DUP_LEVEL)

static DB_DOMAIN *
get_reserved_index_attr_domain_type (int mode, int level)
{
  DB_DOMAIN *domain = (DB_DOMAIN *) 0;

  int hash_mod_val = 1 << level;	// like pow(2, level);
  switch (mode)
    {
    case DUP_MODE_OID:
      if (level == 0)
	{
	  domain = &tp_Bigint_domain;	// tp_domain_construct (DB_TYPE_BIGINT, NULL, DB_BIGINT_PRECISION, 0, NULL); 
	}
      else if (hash_mod_val > SHRT_MAX)
	{
	  domain = &tp_Integer_domain;	//tp_domain_construct (DB_TYPE_INTEGER, NULL, DB_INTEGER_PRECISION, 0, NULL);
	}
      else
	{
	  domain = &tp_Short_domain;	//tp_domain_construct (DB_TYPE_SHORT, NULL, DB_SHORT_PRECISION, 0, NULL);
	}
      break;

    case DUP_MODE_PAGEID:
      if (level == 0 || hash_mod_val > SHRT_MAX)
	{
	  domain = &tp_Integer_domain;	//tp_domain_construct (DB_TYPE_INTEGER, NULL, DB_INTEGER_PRECISION, 0, NULL);
	}
      else
	{
	  domain = &tp_Short_domain;	//tp_domain_construct (DB_TYPE_SHORT, NULL, DB_SHORT_PRECISION, 0, NULL);         
	}
      break;

    case DUP_MODE_SLOTID:
    case DUP_MODE_VOLID:
      domain = &tp_Short_domain;	//tp_domain_construct (DB_TYPE_SHORT, NULL, DB_SHORT_PRECISION, 0, NULL);
      break;

    default:
      assert (false);
      break;
    }

  return domain;
}

#if defined(SERVER_MODE) || defined(SA_MODE)
static OR_ATTRIBUTE st_or_atts[COUNT_OF_DUP_MODE][COUNT_OF_DUP_LEVEL];
static bool st_or_atts_init = false;

static void
dk_or_attribute_initialized ()
{
  int att_id;
  int mode, level;

#ifndef NDEBUG
  OR_ATTRIBUTE *att;
  int mode2, level2;
  const char *reserved_name;
  OID rec_oid = OID_INITIALIZER;
  DB_VALUE v;
  DB_DOMAIN *domain = NULL;
  int cnt = 0;
#endif

  for (mode = DUP_MODE_OID; mode < DUP_MODE_LAST; mode++)
    {
      for (level = OVFL_LEVEL_MIN; level <= OVFL_LEVEL_MAX; level++)
	{
	  att_id = MK_RESERVED_INDEX_ATTR_ID (mode, level);
	  st_or_atts[mode - 1][level].id = att_id;
	  st_or_atts[mode - 1][level].domain = get_reserved_index_attr_domain_type (mode, level);
	  st_or_atts[mode - 1][level].type = st_or_atts[mode - 1][level].domain->type->id;

#ifndef NDEBUG
	  cnt++;
	  reserved_name = GET_RESERVED_INDEX_ATTR_NAME (mode, level);
	  domain = get_reserved_index_attr_domain_type (mode, level);
	  dk_heap_midxkey_get_reserved_index_value (att_id, &rec_oid, &v);

	  mode2 = GET_RESERVED_INDEX_ATTR_MODE (att_id);
	  level2 = GET_RESERVED_INDEX_ATTR_LEVEL (att_id);

	  assert (mode == mode2);
	  assert (level == level2);

	  assert (IS_RESERVED_INDEX_ATTR_ID (att_id) == true);
	  assert (IS_RESERVED_INDEX_ATTR_NAME (reserved_name) == true);

	  GET_RESERVED_INDEX_ATTR_MODE_LEVEL_FROM_NAME (reserved_name, mode2, level2);
	  assert (mode == mode2);
	  assert (level == level2);
#endif // #ifndef NDEBUG
	}
    }

#ifndef NDEBUG
  int max = sizeof (st_reserved_index_col_name) / sizeof (st_reserved_index_col_name[0][0]);
  assert (max == cnt);
#endif

  st_or_atts_init = true;
}

void *
dk_find_or_reserved_index_attribute (int att_id)
{
  int mode = GET_RESERVED_INDEX_ATTR_MODE (att_id);
  int level = GET_RESERVED_INDEX_ATTR_LEVEL (att_id);

  assert (mode > DUP_MODE_NONE && mode < DUP_MODE_LAST);
  assert (level >= OVFL_LEVEL_MIN && level <= OVFL_LEVEL_MAX);

  assert (st_or_atts_init == true);

  return (void *) &(st_or_atts[mode - 1][level]);
}

int
dk_heap_midxkey_get_reserved_index_value (int att_id, OID * rec_oid, DB_VALUE * value)
{
  // The rec_oid may be NULL when the index of the UNIQUE attribute is an index. 
  // In that case, however, it cannot enter here.
  assert_release (rec_oid != NULL);

  int hash_mod_val;
  short level = GET_RESERVED_INDEX_ATTR_LEVEL (att_id);
  short mode = GET_RESERVED_INDEX_ATTR_MODE (att_id);

  hash_mod_val = 1 << level;	// like pow(2, level);

  switch (mode)
    {
    case DUP_MODE_OID:
      if (level == 0)
	{
	  db_make_bigint (value, *((DB_BIGINT *) rec_oid));
	}
      else if (hash_mod_val > SHRT_MAX)
	{
	  db_make_int (value, (int) (OID_PSEUDO_KEY (rec_oid) % hash_mod_val));
	}
      else
	{
	  db_make_short (value, (short) (OID_PSEUDO_KEY (rec_oid) % hash_mod_val));
	}
      break;

    case DUP_MODE_PAGEID:
      if (level == 0)
	{
	  db_make_int (value, rec_oid->pageid);
	}
      else if (hash_mod_val > SHRT_MAX)
	{
	  db_make_int (value, (int) (rec_oid->pageid % hash_mod_val));
	}
      else
	{
	  db_make_short (value, (short) (rec_oid->pageid % hash_mod_val));
	}
      break;

    case DUP_MODE_SLOTID:
      db_make_short (value, (level == 0) ? rec_oid->slotid : (rec_oid->slotid % hash_mod_val));
      break;

    case DUP_MODE_VOLID:
      db_make_short (value, (level == 0) ? rec_oid->volid : (rec_oid->volid % hash_mod_val));
      break;
    default:
      assert (false);
      break;
    }

  return NO_ERROR;
}
#endif // #if defined(SERVER_MODE) || defined(SA_MODE)

#if !defined(SERVER_MODE)

static SM_ATTRIBUTE *st_sm_atts[COUNT_OF_DUP_MODE][COUNT_OF_DUP_LEVEL];
static bool st_sm_atts_init = false;

static void
dk_sm_attribute_finalized ()
{
  int mode, level;

  if (st_sm_atts_init)
    {
      for (mode = DUP_MODE_OID; mode <= DUP_MODE_VOLID; mode++)
	{
	  for (level = OVFL_LEVEL_MIN; level <= OVFL_LEVEL_MAX; level++)
	    {
	      if (st_sm_atts[mode - 1][level])
		{
		  classobj_free_attribute (st_sm_atts[mode - 1][level]);
		  st_sm_atts[mode - 1][level] = NULL;
		}
	    }
	}

      st_sm_atts_init = false;
    }
}

static void
dk_sm_attribute_initialized ()
{
  int error_code = NO_ERROR;
  int mode, level;
  const char *reserved_name = NULL;
  SM_ATTRIBUTE *att;
  DB_DOMAIN *domain = NULL;

  assert (prm_get_integer_value (PRM_ID_AUTO_DEDUP_MODE) >= DUP_MODE_NONE
	  && prm_get_integer_value (PRM_ID_AUTO_DEDUP_MODE) < DUP_MODE_LAST);
  assert (prm_get_integer_value (PRM_ID_AUTO_DEDUP_LEVEL) >= OVFL_LEVEL_MIN
	  && prm_get_integer_value (PRM_ID_AUTO_DEDUP_LEVEL) <= OVFL_LEVEL_MAX);

  for (mode = DUP_MODE_OID; mode <= DUP_MODE_VOLID; mode++)
    {
      for (level = OVFL_LEVEL_MIN; level <= OVFL_LEVEL_MAX; level++)
	{
	  reserved_name = GET_RESERVED_INDEX_ATTR_NAME (mode, level);
	  domain = get_reserved_index_attr_domain_type (mode, level);
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
	  att->id = MK_RESERVED_INDEX_ATTR_ID (mode, level);

	  st_sm_atts[mode - 1][level] = att;
	}
    }

  st_sm_atts_init = true;
  return;

error_exit:
  if (att)
    {
      classobj_free_attribute (att);
    }
}

SM_ATTRIBUTE *
dk_find_sm_reserved_index_attribute (int att_id, const char *att_name)
{
  int mode, level, idx;

  assert ((att_id != -1) || (att_name && *att_name));

  if (att_id != -1)
    {
      mode = GET_RESERVED_INDEX_ATTR_MODE (att_id);
      level = GET_RESERVED_INDEX_ATTR_LEVEL (att_id);
    }
  else
    {
      GET_RESERVED_INDEX_ATTR_MODE_LEVEL_FROM_NAME (att_name, mode, level);
    }
  assert (mode > DUP_MODE_NONE && mode < DUP_MODE_LAST);
  assert (level >= OVFL_LEVEL_MIN && level <= OVFL_LEVEL_MAX);
  assert (st_reserved_index_col_name[mode - 1][level] != NULL);
  assert (st_sm_atts_init == true);

  return st_sm_atts[mode - 1][level];
}

int
dk_alter_rebuild_index_level_adjust (DB_CONSTRAINT_TYPE ctype, const PT_INDEX_INFO * idx_info, char **attnames,
				     int *asc_desc, int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info,
				     int *reserved_index_col_pos, int nnames, bool is_reverse)
{
  int func_no_args = 0;

  assert (!DB_IS_CONSTRAINT_UNIQUE_FAMILY (ctype) && ctype != DB_CONSTRAINT_FOREIGN_KEY);
  assert (asc_desc != NULL);
  assert (reserved_index_col_pos != NULL);

  if (idx_info->dupkey_mode <= DUP_MODE_OVFL_LEVEL_NOT_SET)
    {
      /* no action */
      return NO_ERROR;
    }

  if (idx_info->dupkey_mode == DUP_MODE_NONE)
    {
      if (*reserved_index_col_pos != -1)
	{			// remove hidden column   
	  // If memory release is actually required, it is performed in do_alter_index_rebuild().                
	  attnames[*reserved_index_col_pos] = NULL;

	  assert (!func_index_info || (func_index_info && func_index_info->attr_index_start > 0));
	  if (func_index_info && func_index_info->attr_index_start > 0)
	    {
	      func_no_args = nnames - *reserved_index_col_pos;
	      if (func_no_args > 0)
		{
		  memmove (asc_desc + *reserved_index_col_pos, asc_desc + (*reserved_index_col_pos + 1),
			   (func_no_args * sizeof (asc_desc[0])));
		  if (attrs_prefix_length)
		    {
		      memmove (attrs_prefix_length + *reserved_index_col_pos,
			       attrs_prefix_length + (*reserved_index_col_pos + 1),
			       (func_no_args * sizeof (attrs_prefix_length[0])));
		    }
		  memmove (attnames + *reserved_index_col_pos, attnames + (*reserved_index_col_pos + 1),
			   (func_no_args * sizeof (attnames[0])));

		  attnames[nnames - 1] = NULL;
		  func_index_info->attr_index_start--;
		}
	    }
	  *reserved_index_col_pos = -1;
	}

      return NO_ERROR;
    }

  assert (idx_info->dupkey_mode != DUP_MODE_NONE && idx_info->dupkey_mode != DUP_MODE_OVFL_LEVEL_NOT_SET);
  assert (idx_info->dupkey_hash_level >= OVFL_LEVEL_MIN && idx_info->dupkey_hash_level <= OVFL_LEVEL_MAX);

  if (*reserved_index_col_pos != -1)
    {				// reset level       
      if (attrs_prefix_length)
	{
	  attrs_prefix_length[*reserved_index_col_pos] = -1;
	}

      asc_desc[*reserved_index_col_pos] = (is_reverse ? 1 : 0);
      strcpy (attnames[*reserved_index_col_pos],
	      (char *) GET_RESERVED_INDEX_ATTR_NAME (idx_info->dupkey_mode, idx_info->dupkey_hash_level));
    }
  else
    {				// append hidden column
      if (func_index_info && func_index_info->attr_index_start >= 0)
	{
	  func_no_args = nnames - func_index_info->attr_index_start;
	  if (func_no_args > 0)
	    {
	      memmove (asc_desc + (func_index_info->attr_index_start + 1),
		       asc_desc + func_index_info->attr_index_start, func_no_args * sizeof (asc_desc[0]));
	      if (attrs_prefix_length)
		{
		  memmove (attrs_prefix_length + (func_index_info->attr_index_start + 1),
			   attrs_prefix_length + func_index_info->attr_index_start,
			   func_no_args * sizeof (attrs_prefix_length[0]));
		}
	      memmove (attnames + (func_index_info->attr_index_start + 1),
		       attnames + func_index_info->attr_index_start, func_no_args * sizeof (attnames[0]));
	    }

	  *reserved_index_col_pos = func_index_info->attr_index_start++;
	}
      else
	{
	  *reserved_index_col_pos = nnames;
	}

      if (attrs_prefix_length)
	{
	  attrs_prefix_length[*reserved_index_col_pos] = -1;
	}

      asc_desc[*reserved_index_col_pos] = (is_reverse ? 1 : 0);
      /* do_alter_index_rebuild() is using strdup(). The same treatment method shall be followed. */
      attnames[*reserved_index_col_pos] =
	strdup ((char *) GET_RESERVED_INDEX_ATTR_NAME (idx_info->dupkey_mode, idx_info->dupkey_hash_level));
      attnames[nnames + 1] = NULL;

      if (attnames[*reserved_index_col_pos] == NULL)
	{
	  char *str = (char *) GET_RESERVED_INDEX_ATTR_NAME (idx_info->dupkey_mode, idx_info->dupkey_hash_level);
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (strlen (str) + 1) * sizeof (char));
	  return ER_OUT_OF_VIRTUAL_MEMORY;
	}
    }

  return NO_ERROR;
}

void
dk_create_index_level_adjust (const PT_INDEX_INFO * idx_info, char **attnames, int *asc_desc,
			      int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info, int nnames, bool is_reverse)
{
  int reserved_index_col_pos;

  assert (asc_desc != NULL);

  assert (idx_info->dupkey_mode != DUP_MODE_NONE && idx_info->dupkey_mode != DUP_MODE_OVFL_LEVEL_NOT_SET);
  assert (idx_info->dupkey_hash_level >= OVFL_LEVEL_MIN && idx_info->dupkey_hash_level <= OVFL_LEVEL_MAX);

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

      reserved_index_col_pos = func_index_info->attr_index_start++;
    }
  else
    {
      reserved_index_col_pos = nnames;
    }

  if (attrs_prefix_length)
    {
      attrs_prefix_length[reserved_index_col_pos] = -1;
    }
  attnames[reserved_index_col_pos] =
    (char *) GET_RESERVED_INDEX_ATTR_NAME (idx_info->dupkey_mode, idx_info->dupkey_hash_level);
  asc_desc[reserved_index_col_pos] = (is_reverse ? 1 : 0);

  attnames[nnames + 1] = NULL;
}

char *
dk_print_reserved_index_info (char *buf, int buf_size, int dupkey_mode, int dupkey_hash_level)
{
  int len = 0;
  const char *pzstr_mode[DUP_MODE_LAST] = {
    "",
    "OID",
    "PAGEID",
    "SLOTID",
    "VOLID"
  };

  assert ((dupkey_mode >= DUP_MODE_OVFL_LEVEL_NOT_SET) && (dupkey_mode < DUP_MODE_LAST));

  buf[0] = '\0';
  if (dupkey_mode <= DUP_MODE_NONE)
    {
      /* case DUP_MODE_OVFL_LEVEL_NOT_SET: 
       *     It entered to output an error message due to a parsing error. */
      assert ((dupkey_mode = DUP_MODE_NONE) || (dupkey_mode == DUP_MODE_OVFL_LEVEL_NOT_SET && dupkey_hash_level == 0));
      return buf;
    }

  if (dupkey_hash_level == OVFL_LEVEL_MIN)
    {
      len = snprintf (buf, buf_size, " deduplicate with %s", (char *) pzstr_mode[dupkey_mode]);
    }
  else
    {
      len =
	snprintf (buf, buf_size, " deduplicate with %s level %d", (char *) pzstr_mode[dupkey_mode], dupkey_hash_level);
    }

  assert (len < buf_size);
  return buf;
}
#endif // #if !defined(SERVER_MODE)

void
dk_reserved_index_attribute_initialized ()
{
#if defined(SERVER_MODE) || defined(SA_MODE)
  dk_or_attribute_initialized ();
#endif

#if !defined(SERVER_MODE)
  dk_sm_attribute_initialized ();
#endif
}

void
dk_reserved_index_attribute_finalized ()
{
#if !defined(SERVER_MODE)
  dk_sm_attribute_finalized ();
#endif
}

#endif // #if defined(SUPPORT_KEY_DUP_LEVEL)
