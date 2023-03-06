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
 * dup_key.h - Definition for management of duplicate key indexes
 */

#ifndef _DUP_KEY_H_
#define _DUP_KEY_H_

#ident "$Id$"

#include "dup_key_def.h"

#if !defined(SERVER_MODE)
#include "parse_tree.h"
#endif

#define COMPRESS_INDEX_MODE_HIGH     (0)
#define COMPRESS_INDEX_MODE_MEDIUM   (1)
#define COMPRESS_INDEX_MODE_LOW      (2)

typedef enum
{
  COMPRESS_INDEX_MODE_NONE = 0,
  COMPRESS_INDEX_MODE_SET
} EN_DUP_MODE;

#define COMPRESS_INDEX_MOD_LEVEL_ZERO     (0)
#define COMPRESS_INDEX_MOD_LEVEL_MIN      (1)
#define COMPRESS_INDEX_MOD_LEVEL_MAX      (16)
#define COMPRESS_INDEX_MOD_LEVEL_DEFAULT  (10)


/* ******************************************************** */
#if !defined(SUPPORT_COMPRESS_MODE)
/* ******************************************************** */

#define IS_RESERVED_INDEX_ATTR_ID(id)      (false)
#define IS_RESERVED_INDEX_ATTR_NAME(name)  (false)

/* ******************************************************** */
#else /* #if !defined(SUPPORT_COMPRESS_MODE) */
/* ******************************************************** */

#define RESERVED_INDEX_ATTR_ID_BASE    (-28480000)
#define RESERVED_INDEX_ATTR_NAME_PREFIX        "_cub_idx_col_"
#define RESERVED_INDEX_ATTR_NAME_LIKE_PATTERN  "'_cub_idx_col_%%'"

#define RESERVED_INDEX_ATTR_NAME_PREFIX_LEN  (13)	// strlen(RESERVED_INDEX_ATTR_NAME_PREFIX)
#define RESERVED_INDEX_ATTR_NAME_BUF_SIZE    (255)

#define RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID  "p_"

#define COUNT_OF_DUP_LEVEL (COMPRESS_INDEX_MOD_LEVEL_MAX + 1)

static const char *st_reserved_index_col_name[COUNT_OF_DUP_LEVEL] = {
/* *INDENT-OFF* */
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "00",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "01",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "02",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "03",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "04",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "05",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "06",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "07",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "08",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "09",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "10",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "11",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "12",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "13",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "14",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "15",
RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID "16"
/* *INDENT-ON* */
};

// *INDENT-OFF*
#define GET_RESERVED_INDEX_ATTR_NAME(level)   (st_reserved_index_col_name[(level)])
#define MK_RESERVED_INDEX_ATTR_ID(level)      (RESERVED_INDEX_ATTR_ID_BASE - ((level) << 4))
#define GET_RESERVED_INDEX_ATTR_LEVEL(attid)  ((RESERVED_INDEX_ATTR_ID_BASE - (attid)) >> 4)

#define GET_RESERVED_INDEX_ATTR_MODE_LEVEL_FROM_NAME(name, level)  do {                                         \
        char chx, ch_mode;                                                                                      \
        if(sscanf ((name) + RESERVED_INDEX_ATTR_NAME_PREFIX_LEN, "%c_%02d%c", &(ch_mode), &(level), &chx) != 2) \
          {  assert(false); }                                                                                   \
        assert(((level) >= COMPRESS_INDEX_MOD_LEVEL_ZERO) && ((level) <= COMPRESS_INDEX_MOD_LEVEL_MAX));        \
        switch(ch_mode)                                                                                         \
        {                                                                                                       \
           case RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID[0]:  break;                                              \
           default: assert(false);                                                                              \
        }                                                                                                       \
 } while(0)
// *INDENT-ON*

#define MAX_RESERVED_INDEX_ATTR_ID  MK_RESERVED_INDEX_ATTR_ID(COMPRESS_INDEX_MOD_LEVEL_ZERO)
#define MIN_RESERVED_INDEX_ATTR_ID  MK_RESERVED_INDEX_ATTR_ID(COMPRESS_INDEX_MOD_LEVEL_MAX)

#define IS_RESERVED_INDEX_ATTR_ID(id)      ((id) >= MIN_RESERVED_INDEX_ATTR_ID &&  (id) <= MAX_RESERVED_INDEX_ATTR_ID)
#define IS_RESERVED_INDEX_ATTR_NAME(name)  \
        (((name)[0] == '_') && !memcmp ((name), RESERVED_INDEX_ATTR_NAME_PREFIX, RESERVED_INDEX_ATTR_NAME_PREFIX_LEN))


#if defined(SERVER_MODE) || defined(SA_MODE)
extern int dk_heap_midxkey_get_reserved_index_value (int att_id, OID * rec_oid, DB_VALUE * value);

// The actual return type is OR_ATTRIBUTE*.
// But, here it is treated as void* due to collision with C++. (error: template with C linkage)
extern void *dk_find_or_reserved_index_attribute (int att_id);
extern int dk_get_decompress_position (int n_attrs, int *attr_ids, int func_attr_index_start);
#endif

#if !defined(SERVER_MODE)
extern SM_ATTRIBUTE *dk_find_sm_reserved_index_attribute (int att_id, const char *att_name);


extern void dk_create_index_level_remove_adjust (DB_CONSTRAINT_TYPE ctype, char **attnames, int *asc_desc,
						 int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info,
						 int reserved_index_col_pos, int nnames);
extern void dk_create_index_level_adjust (const PT_INDEX_INFO * idx_info, char **attnames, int *asc_desc,
					  int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info, int nnames,
					  bool is_reverse);

extern char *dk_print_reserved_index_info (char *buf, int buf_size, int dupkey_mode, int dupkey_hash_level);
extern int dk_sm_decompress_position (int n_attrs, SM_ATTRIBUTE ** attrs, SM_FUNCTION_INFO * function_index);
#endif

extern char *dk_get_compress_index_attr_name (int level, char **ppbuf);

extern void dk_reserved_index_attribute_initialized ();
extern void dk_reserved_index_attribute_finalized ();

/* ******************************************************** */
#endif /* #if !defined(SUPPORT_COMPRESS_MODE) */
/* ******************************************************** */

#endif /* _DUP_KEY_H_ */
