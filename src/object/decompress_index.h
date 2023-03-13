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
 * decompress_index.h - Definition for management of duplicate key indexes
 */

#ifndef _DECOMPRESS_INDEX_H_
#define _DECOMPRESS_INDEX_H_

#ident "$Id$"

#include "decompress_index_def.h"

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

#define IS_COMPRESS_INDEX_ATTR_ID(id)      (false)
#define IS_COMPRESS_INDEX_ATTR_NAME(name)  (false)

/* ******************************************************** */
#else /* #if !defined(SUPPORT_COMPRESS_MODE) */
/* ******************************************************** */

#define COMPRESS_INDEX_ATTR_ID_BASE    (0x8A8B8C90)	//  (-1970566000)
#define COMPRESS_INDEX_ATTR_NAME_PREFIX        "_compress_"
#define COMPRESS_INDEX_ATTR_NAME_LIKE_PATTERN  "'_compress_%%'"

#define COMPRESS_INDEX_ATTR_NAME_PREFIX_LEN  (10)	// strlen(COMPRESS_INDEX_ATTR_NAME_PREFIX)
#define RESERVED_INDEX_ATTR_NAME_BUF_SIZE    (255)

#define COUNT_OF_COMPRESS_INDEX_MOD_LEVEL (COMPRESS_INDEX_MOD_LEVEL_MAX + 1)

static const char *dk_reserved_compress_index_col_name[COUNT_OF_COMPRESS_INDEX_MOD_LEVEL] = {
/* *INDENT-OFF* */
COMPRESS_INDEX_ATTR_NAME_PREFIX  "00",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "01",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "02",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "03",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "04",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "05",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "06",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "07",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "08",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "09",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "10",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "11",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "12",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "13",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "14",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "15",
COMPRESS_INDEX_ATTR_NAME_PREFIX  "16"
/* *INDENT-ON* */
};

// *INDENT-OFF*
#define GET_COMPRESS_INDEX_ATTR_NAME(level)   (dk_reserved_compress_index_col_name[(level)])
#define MK_COMPRESS_INDEX_ATTR_ID(level)      (((int)COMPRESS_INDEX_ATTR_ID_BASE) - (level))
#define GET_COMPRESS_INDEX_ATTR_LEVEL(attid)  (((int)COMPRESS_INDEX_ATTR_ID_BASE) - (attid))

#define GET_COMPRESS_INDEX_ATTR_MODE_LEVEL_FROM_NAME(name, level)  do {                                         \
        char chx;                                                                                               \
        if(sscanf ((name) + COMPRESS_INDEX_ATTR_NAME_PREFIX_LEN, "%02d%c", &(level), &chx) != 1)                \
          {  assert(false); }                                                                                   \
        assert(((level) >= COMPRESS_INDEX_MOD_LEVEL_ZERO) && ((level) <= COMPRESS_INDEX_MOD_LEVEL_MAX));        \
 } while(0)
// *INDENT-ON*

#define MAX_COMPRESS_INDEX_ATTR_ID  MK_COMPRESS_INDEX_ATTR_ID(COMPRESS_INDEX_MOD_LEVEL_ZERO)
#define MIN_COMPRESS_INDEX_ATTR_ID  MK_COMPRESS_INDEX_ATTR_ID(COMPRESS_INDEX_MOD_LEVEL_MAX)

#define IS_COMPRESS_INDEX_ATTR_ID(id)      ((id) >= MIN_COMPRESS_INDEX_ATTR_ID &&  (id) <= MAX_COMPRESS_INDEX_ATTR_ID)
#define IS_COMPRESS_INDEX_ATTR_NAME(name)  \
        (((name)[0] == '_') && !memcmp ((name), COMPRESS_INDEX_ATTR_NAME_PREFIX, COMPRESS_INDEX_ATTR_NAME_PREFIX_LEN))


#if defined(SERVER_MODE) || defined(SA_MODE)
extern int dk_heap_midxkey_get_compress_index_value (int att_id, OID * rec_oid, DB_VALUE * value);

// The actual return type is OR_ATTRIBUTE*.
// But, here it is treated as void* due to collision with C++. (error: template with C linkage)
extern void *dk_find_or_compress_index_attribute (int att_id);
extern int dk_get_decompress_position (int n_attrs, int *attr_ids, int func_attr_index_start);
#endif

#if !defined(SERVER_MODE)
// SM_ATTRIBUTE and DB_ATTRIBUTE are the same thing
extern SM_ATTRIBUTE *dk_find_sm_compress_index_attribute (int att_id, const char *att_name);
extern void dk_create_index_level_remove_adjust (DB_CONSTRAINT_TYPE ctype, char **attnames, int *asc_desc,
						 int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info,
						 int compress_index_col_pos, int nnames);
extern void dk_create_index_level_adjust (const PT_INDEX_INFO * idx_info, char **attnames, int *asc_desc,
					  int *attrs_prefix_length, SM_FUNCTION_INFO * func_index_info, int nnames,
					  bool is_reverse);
extern char *dk_print_compress_index_info (char *buf, int buf_size, int compress_mode, int compress_level);
extern int dk_sm_decompress_position (int n_attrs, SM_ATTRIBUTE ** attrs, SM_FUNCTION_INFO * function_index);
#endif

extern void dk_compress_index_attribute_initialized ();
extern void dk_compress_index_attribute_finalized ();

/* ******************************************************** */
#endif /* #if !defined(SUPPORT_COMPRESS_MODE) */
/* ******************************************************** */


#endif /* _DECOMPRESS_INDEX_H_ */
