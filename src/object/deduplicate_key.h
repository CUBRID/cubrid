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
 * deduplicate_key.h - Definition for management of duplicate key indexes
 */

#ifndef _DECOMPRESS_INDEX_H_
#define _DECOMPRESS_INDEX_H_

#ident "$Id$"

#if !defined(SERVER_MODE)
#include "parse_tree.h"
#endif

#define DEDUPLICATE_KEY_MODE_OFF     (0)
#define DEDUPLICATE_KEY_MODE_ON      (1)

typedef enum
{
  DEDUPLICATE_KEY_MODE_NONE = 0,
  DEDUPLICATE_KEY_MODE_SET
} EN_COMPRESS_INDEX_MODE;

#define DEDUPLICATE_KEY_LEVEL_MIN      (1)
#define DEDUPLICATE_KEY_LEVEL_MAX      (32)
#define DEDUPLICATE_KEY_LEVEL_NONE     (DEDUPLICATE_KEY_LEVEL_MAX)


/* ******************************************************** */
#if !defined(SUPPORT_DEDUPLICATE_KEY_MODE)
/* ******************************************************** */

#define IS_DEDUPLICATE_KEY_ATTR_ID(id)      (false)
#define IS_DEDUPLICATE_KEY_ATTR_NAME(name)  (false)

/* ******************************************************** */
#else /* #if !defined(SUPPORT_DEDUPLICATE_KEY_MODE) */
/* ******************************************************** */

#define DEDUPLICATE_KEY_ATTR_ID_BASE    (0x8A8B8C90)	//  (-1970566000)
#define DEDUPLICATE_KEY_ATTR_NAME_PREFIX        "_dedup_"
#define DEDUPLICATE_KEY_ATTR_NAME_LIKE_PATTERN  "'_dedup_%%'"

#define DEDUPLICATE_KEY_ATTR_NAME_PREFIX_LEN  (7)	// strlen(DEDUPLICATE_KEY_ATTR_NAME_PREFIX)
#define DEDUPLICATE_KEY_ATTR_NAME_LEN         (10)	// strlen(DEDUPLICATE_KEY_ATTR_NAME_PREFIX) + "xy" + '\0'
#define RESERVED_INDEX_ATTR_NAME_BUF_SIZE    (255)

#define COUNT_OF_DEDUPLICATE_KEY_LEVEL (DEDUPLICATE_KEY_LEVEL_MAX - DEDUPLICATE_KEY_LEVEL_MIN + 1)


// *INDENT-OFF*
#define LEVEL_2_IDX(lv)  ((lv) - DEDUPLICATE_KEY_LEVEL_MIN)
#define MK_DEDUPLICATE_KEY_ATTR_ID(level)      (((int)DEDUPLICATE_KEY_ATTR_ID_BASE) - (level))
#define GET_DEDUPLICATE_KEY_ATTR_LEVEL(attid)  (((int)DEDUPLICATE_KEY_ATTR_ID_BASE) - (attid))

#define GET_DEDUPLICATE_KEY_ATTR_MODE_LEVEL_FROM_NAME(name, level)  do {                             \
        char chx;                                                                                    \
        if(sscanf ((name) + DEDUPLICATE_KEY_ATTR_NAME_PREFIX_LEN, "%02d%c", &(level), &chx) != 1)    \
          {  assert(false); }                                                                        \
        assert(((level) >= DEDUPLICATE_KEY_LEVEL_MIN) && ((level) <= DEDUPLICATE_KEY_LEVEL_MAX));    \
 } while(0)
// *INDENT-ON*

#define MAX_DEDUPLICATE_KEY_ATTR_ID  MK_DEDUPLICATE_KEY_ATTR_ID(DEDUPLICATE_KEY_LEVEL_MIN)
#define MIN_DEDUPLICATE_KEY_ATTR_ID  MK_DEDUPLICATE_KEY_ATTR_ID(DEDUPLICATE_KEY_LEVEL_MAX)

#define IS_DEDUPLICATE_KEY_ATTR_ID(id)      ((id) >= MIN_DEDUPLICATE_KEY_ATTR_ID &&  (id) <= MAX_DEDUPLICATE_KEY_ATTR_ID)
#define IS_DEDUPLICATE_KEY_ATTR_NAME(name)  \
        (((name)[0] == '_') && !memcmp ((name), DEDUPLICATE_KEY_ATTR_NAME_PREFIX, DEDUPLICATE_KEY_ATTR_NAME_PREFIX_LEN))


#if defined(SERVER_MODE) || defined(SA_MODE)
extern int dk_get_deduplicate_key_value (OID * rec_oid, int att_id, DB_VALUE * value);

// The actual return type is OR_ATTRIBUTE*.
// But, here it is treated as void* due to collision with C++. (error: template with C linkage)
extern void *dk_find_or_deduplicate_key_attribute (int att_id);
extern int dk_get_deduplicate_key_position (int n_attrs, int *attr_ids, int func_attr_index_start);
#endif

#if !defined(SERVER_MODE)
// SM_ATTRIBUTE and DB_ATTRIBUTE are the same thing
extern SM_ATTRIBUTE *dk_find_sm_deduplicate_key_attribute (int att_id, const char *att_name);
extern void dk_create_index_level_remove_adjust (DB_CONSTRAINT_TYPE ctype, char **attnames, int *asc_desc,
						 int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info,
						 int deduplicate_key_col_pos, int nnames);
extern void dk_create_index_level_adjust (const PT_INDEX_INFO * idx_info, char **attnames, int *asc_desc,
					  int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info, int nnames,
					  bool is_reverse);
extern char *dk_print_deduplicate_key_info (char *buf, int buf_size, int dedup_key_mode, int dedup_key_level);
extern int dk_sm_deduplicate_key_position (int n_attrs, SM_ATTRIBUTE ** attrs, SM_FUNCTION_INFO * function_index);
#endif

extern char *dk_get_deduplicate_key_attr_name (int level);
extern void dk_deduplicate_key_attribute_initialized ();
extern void dk_deduplicate_key_attribute_finalized ();

/* ******************************************************** */
#endif /* #if !defined(SUPPORT_DEDUPLICATE_KEY_MODE) */
/* ******************************************************** */


#endif /* _DECOMPRESS_INDEX_H_ */
