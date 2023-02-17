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

typedef enum
{
  DUP_MODE_NONE = 0,
  DUP_MODE_OID,
  DUP_MODE_PAGEID,
  DUP_MODE_LAST
} EN_DUP_MODE;

#define OVFL_LEVEL_MIN       (0)
#define OVFL_LEVEL_MAX       (16)

#define DUP_MODE_OVFL_LEVEL_NOT_SET   (-1)

/* ******************************************************** */
#if !defined(SUPPORT_KEY_DUP_LEVEL)
/* ******************************************************** */

#define IS_RESERVED_INDEX_ATTR_ID(id)      (false)
#define IS_RESERVED_INDEX_ATTR_NAME(name)  (false)

/* ******************************************************** */
#else /* #if !defined(SUPPORT_KEY_DUP_LEVEL) */
/* ******************************************************** */

#define RESERVED_INDEX_ATTR_ID_BASE    (-28480000)
#define RESERVED_INDEX_ATTR_NAME_PREFIX        "_cub_idx_col_"
#define RESERVED_INDEX_ATTR_NAME_LIKE_PATTERN  "'_cub_idx_col_%%'"
#define RESERVED_INDEX_ATTR_NAME_REG_PATTERN   "'^_cub_idx_col_(o|p)_([0][0-9]|[1][0-6])$'"

#define RESERVED_INDEX_ATTR_NAME_PREFIX_LEN  (13)	// strlen(RESERVED_INDEX_ATTR_NAME_PREFIX)
#define RESERVED_INDEX_ATTR_NAME_BUF_SIZE    (255)

#define RESERVED_INDEX_ATTR_NAME_PREFIX_OID     "o_"
#define RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID  "p_"

#define COUNT_OF_DUP_MODE  (DUP_MODE_LAST - 1)
#define COUNT_OF_DUP_LEVEL (OVFL_LEVEL_MAX + 1)

static const char *st_reserved_index_col_name[COUNT_OF_DUP_MODE][COUNT_OF_DUP_LEVEL] = {
/* *INDENT-OFF* */
  {
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "00",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "01",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "02",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "03",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "04",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "05",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "06",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "07",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "08",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "09",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "10",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "11",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "12",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "13",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "14",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "15",
     RESERVED_INDEX_ATTR_NAME_PREFIX  RESERVED_INDEX_ATTR_NAME_PREFIX_OID "16"
  }, 
  {
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
  }  
/* *INDENT-ON* */
};

// *INDENT-OFF*
#define GET_RESERVED_INDEX_ATTR_NAME(mode, level)   (st_reserved_index_col_name[(mode)-1][(level)])
#define MK_RESERVED_INDEX_ATTR_ID(mode, level)      (RESERVED_INDEX_ATTR_ID_BASE - ((mode) | ((level) << 4)))
#define GET_RESERVED_INDEX_ATTR_MODE(attid)         ((RESERVED_INDEX_ATTR_ID_BASE - (attid)) & 0x0000000F)
#define GET_RESERVED_INDEX_ATTR_LEVEL(attid)        ((RESERVED_INDEX_ATTR_ID_BASE - (attid)) >> 4)

#define GET_RESERVED_INDEX_ATTR_MODE_LEVEL_FROM_NAME(name, mode, level)  do {                                   \
        char chx, ch_mode;                                                                                      \
        if(sscanf ((name) + RESERVED_INDEX_ATTR_NAME_PREFIX_LEN, "%c_%02d%c", &(ch_mode), &(level), &chx) != 2) \
          {  assert(false); }                                                                                   \
        assert(((level) >= OVFL_LEVEL_MIN) && ((level) <= OVFL_LEVEL_MAX));                                     \
        switch(ch_mode)                                                                                         \
        {                                                                                                       \
           case RESERVED_INDEX_ATTR_NAME_PREFIX_OID[0]:    (mode) = DUP_MODE_OID;    break;                     \
           case RESERVED_INDEX_ATTR_NAME_PREFIX_PAGEID[0]: (mode) = DUP_MODE_PAGEID; break;                     \
           default: assert(false);                                                                              \
        }                                                                                                       \
 } while(0)
 // *INDENT-ON*

#define MAX_RESERVED_INDEX_ATTR_ID  MK_RESERVED_INDEX_ATTR_ID(DUP_MODE_OID, OVFL_LEVEL_MIN)
#define MIN_RESERVED_INDEX_ATTR_ID  MK_RESERVED_INDEX_ATTR_ID((DUP_MODE_LAST-1), OVFL_LEVEL_MAX)

#define IS_RESERVED_INDEX_ATTR_ID(id)      ((id) >= MIN_RESERVED_INDEX_ATTR_ID &&  (id) <= MAX_RESERVED_INDEX_ATTR_ID)
#define IS_RESERVED_INDEX_ATTR_NAME(name)  \
        (((name)[0] == '_') && !memcmp ((name), RESERVED_INDEX_ATTR_NAME_PREFIX, RESERVED_INDEX_ATTR_NAME_PREFIX_LEN))



#if defined(SERVER_MODE) || defined(SA_MODE)
extern int dk_heap_midxkey_get_reserved_index_value (int att_id, OID * rec_oid, DB_VALUE * value);

// The actual return type is OR_ATTRIBUTE*.
// But, here it is treated as void* due to collision with C++. (error: template with C linkage)
extern void *dk_find_or_reserved_index_attribute (int att_id);
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
#endif



extern void dk_reserved_index_attribute_initialized ();
extern void dk_reserved_index_attribute_finalized ();

/* ******************************************************** */
#endif /* #if !defined(SUPPORT_KEY_DUP_LEVEL) */
/* ******************************************************** */

#endif /* _DUP_KEY_H_ */
